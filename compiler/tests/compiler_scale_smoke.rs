use std::{
    fs,
    path::{Path, PathBuf},
    time::Instant,
};

use sloppyc::{compile_file, CompileOptions};

const FILES: usize = 25;
const ROUTES: usize = 200;

fn temp_project_dir() -> PathBuf {
    std::env::temp_dir().join(format!("sloppyc-scale-smoke-{}", std::process::id()))
}

fn remove_dir_if_present(path: &Path) {
    if path.exists() {
        fs::remove_dir_all(path).expect("temporary scale project should be removable");
    }
}

fn pad(value: usize) -> String {
    format!("{value:04}")
}

fn route_line(index: usize, receiver: &str) -> String {
    format!(
        "{receiver}.get(\"/route-{}/{{id:int}}\", (ctx) => Results.json({{ id: ctx.route.id, route: {} }})).withName(\"Scale.Route.{}\");",
        pad(index),
        index,
        pad(index)
    )
}

fn module_source(module_index: usize, route_start: usize, route_count: usize) -> String {
    let mut source = String::new();
    source.push_str("import { Results } from \"sloppy\";\n\n");
    source.push_str(&format!(
        "export function scaleRoutes{}(app) {{\n",
        pad(module_index)
    ));
    source.push_str(&format!(
        "  const routes = app.group(\"/scale/{}\").withTags(\"scale\", \"{}\");\n",
        pad(module_index),
        pad(module_index)
    ));
    for offset in 0..route_count {
        source.push_str("  ");
        source.push_str(&route_line(route_start + offset, "routes"));
        source.push('\n');
    }
    source.push_str("}\n");
    source
}

fn write_scale_project(root: &Path) -> PathBuf {
    let src = root.join("src");
    let routes_dir = src.join("routes");
    fs::create_dir_all(&routes_dir).expect("routes directory should be created");

    let module_count = FILES - 1;
    let module_routes = ROUTES - 1;
    let mut modules = Vec::new();
    let mut next_route = 2usize;
    let mut remaining_routes = module_routes;
    for module_index in 1..=module_count {
        let remaining_modules = module_count - module_index + 1;
        let route_count = remaining_routes.div_ceil(remaining_modules);
        fs::write(
            routes_dir.join(format!("route-{}.ts", pad(module_index))),
            module_source(module_index, next_route, route_count),
        )
        .expect("module source should be written");
        modules.push(module_index);
        next_route += route_count;
        remaining_routes -= route_count;
    }

    let mut main = String::new();
    main.push_str("import { Results, Sloppy } from \"sloppy\";\n");
    for module_index in &modules {
        main.push_str(&format!(
            "import {{ scaleRoutes{} }} from \"./routes/route-{}.ts\";\n",
            pad(*module_index),
            pad(*module_index)
        ));
    }
    main.push_str("\nconst app = Sloppy.create();\n");
    main.push_str(&route_line(1, "app"));
    main.push('\n');
    for module_index in &modules {
        main.push_str(&format!(
            "app.useModule(scaleRoutes{});\n",
            pad(*module_index)
        ));
    }
    main.push_str("export default app;\n");
    let input = src.join("main.ts");
    fs::write(&input, main).expect("entry source should be written");
    input
}

#[test]
fn medium_scale_project_compiles_under_smoke_threshold() {
    let root = temp_project_dir();
    remove_dir_if_present(&root);
    fs::create_dir_all(&root).expect("temporary project should be created");
    let input = write_scale_project(&root);
    let out_dir = root.join(".sloppy");

    let started = Instant::now();
    let output =
        compile_file(&input, &out_dir, &CompileOptions::default()).expect("scale project compiles");
    let elapsed_ms = started.elapsed().as_millis();
    let threshold_ms = std::env::var("SLOPPYC_SCALE_SMOKE_THRESHOLD_MS")
        .ok()
        .and_then(|value| value.parse::<u128>().ok())
        .unwrap_or(30_000);
    assert!(
        elapsed_ms < threshold_ms,
        "medium scale compiler smoke exceeded threshold: {elapsed_ms}ms >= {threshold_ms}ms"
    );

    assert!(output.bundle.path.exists());
    assert!(output.source_map.path.exists());
    assert!(output.plan.path.exists());
    let plan: serde_json::Value =
        serde_json::from_str(&output.plan.contents).expect("emitted plan should parse");
    let routes = plan["routes"]
        .as_array()
        .expect("plan routes should be an array");
    assert_eq!(routes.len(), ROUTES);

    remove_dir_if_present(&root);
}

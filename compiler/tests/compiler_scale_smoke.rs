use std::{
    fs,
    path::{Path, PathBuf},
    process::Command,
    time::Instant,
};

use sloppyc::{compile_file, CompileOptions};

const FILES: usize = 25;
const ROUTES: usize = 200;

fn temp_project_dir() -> PathBuf {
    std::env::temp_dir().join(format!("sloppyc-scale-smoke-{}", std::process::id()))
}

fn temp_full_surface_project_dir() -> PathBuf {
    std::env::temp_dir().join(format!(
        "sloppyc-full-surface-scale-smoke-{}",
        std::process::id()
    ))
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

fn count_occurrences(haystack: &str, needle: &str) -> usize {
    haystack.match_indices(needle).count()
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

#[test]
fn generated_full_surface_scale_project_compiles_and_preserves_framework_metadata() {
    let node = match Command::new("node").arg("--version").output() {
        Ok(output) if output.status.success() => "node",
        _ => {
            eprintln!("SKIPPED: node was not available for generated full-surface scale smoke");
            return;
        }
    };
    let repo_root = Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .expect("compiler crate should live under repo root");
    let generator = repo_root.join("tools/compiler/generate-scale-project.mjs");
    let root = temp_full_surface_project_dir();
    remove_dir_if_present(&root);

    let generated = Command::new(node)
        .arg(generator)
        .arg("--size")
        .arg("medium")
        .arg("--out")
        .arg(&root)
        .output()
        .expect("generator process should launch");
    assert!(
        generated.status.success(),
        "generator failed: {}",
        String::from_utf8_lossy(&generated.stderr)
    );

    let input = root.join("src/main.ts");
    let out_dir = root.join(".sloppy");
    let timings_json = root.join("timings.json");
    let started = Instant::now();
    let output = compile_file(
        &input,
        &out_dir,
        &CompileOptions {
            timings_json: Some(timings_json.clone()),
            ..CompileOptions::default()
        },
    )
    .expect("generated full-surface scale project compiles");
    let elapsed_ms = started.elapsed().as_millis();
    let threshold_ms = std::env::var("SLOPPYC_FULL_SCALE_SMOKE_THRESHOLD_MS")
        .ok()
        .and_then(|value| value.parse::<u128>().ok())
        .unwrap_or(30_000);
    assert!(
        elapsed_ms < threshold_ms,
        "full-surface compiler smoke exceeded threshold: {elapsed_ms}ms >= {threshold_ms}ms"
    );

    let plan: serde_json::Value =
        serde_json::from_str(&output.plan.contents).expect("emitted plan should parse");
    let routes = plan["routes"]
        .as_array()
        .expect("plan routes should be an array");
    assert!(
        routes.len() > 200,
        "generated health, controller, and CORS routes should expand route count"
    );
    assert!(routes.iter().any(|route| route["method"] == "OPTIONS"));
    assert!(routes
        .iter()
        .any(|route| route["cors"]["preflight"] == true));
    assert!(routes
        .iter()
        .any(|route| route["health"]["kind"] == "aggregate"));
    assert!(routes.iter().any(|route| route["tags"]
        .as_array()
        .is_some_and(|tags| tags.iter().any(|tag| tag == "controller"))));
    assert!(routes.iter().any(|route| route["middleware"]
        .as_array()
        .is_some_and(|middleware| middleware.iter().any(|entry| entry["kind"] == "requestId"))));
    assert!(routes.iter().any(
        |route| route["middleware"]
            .as_array()
            .is_some_and(|middleware| middleware
                .iter()
                .any(|entry| entry["kind"] == "requestLogging"))
    ));

    assert_eq!(plan["features"]["health"], true);
    assert_eq!(plan["features"]["dataProviders"], true);
    assert_eq!(plan["configuration"]["providers"][0]["provider"], "sqlite");
    assert!(output.bundle.contents.contains("__sloppy_run_middleware"));
    assert!(output.bundle.contents.contains("__sloppy_cors_preflight"));
    assert!(output
        .bundle
        .contents
        .contains("__sloppy_framework_provider_open_options"));

    let timings: serde_json::Value = serde_json::from_str(
        &fs::read_to_string(&timings_json).expect("timings should be written"),
    )
    .expect("timings should parse");
    assert_eq!(timings["schemaVersion"], 1);
    assert_eq!(timings["counters"]["routes"], routes.len() as u64);
    assert_eq!(timings["counters"]["providers"], 1);
    assert!(timings["phases"]["planEmitMs"].is_number());

    let app_js_bytes = output.bundle.contents.len();
    let plan_bytes = output.plan.contents.len();
    let source_map_bytes = output.source_map.contents.len();
    assert!(
        app_js_bytes < 500_000,
        "app.js grew unexpectedly: {app_js_bytes} bytes"
    );
    assert!(
        plan_bytes < 2_000_000,
        "app.plan.json grew unexpectedly: {plan_bytes} bytes"
    );
    assert!(
        source_map_bytes < 2_000_000,
        "app.js.map grew unexpectedly: {source_map_bytes} bytes"
    );
    assert!(
        app_js_bytes / routes.len() < 5_000,
        "app.js bytes per emitted route grew unexpectedly"
    );
    assert!(
        source_map_bytes / routes.len() < 20_000,
        "source map bytes per emitted route grew unexpectedly"
    );
    assert_eq!(
        count_occurrences(&output.bundle.contents, "function __sloppy_run_middleware"),
        1,
        "middleware helper should be emitted once"
    );
    assert_eq!(
        count_occurrences(&output.bundle.contents, "function __sloppy_finish_cors"),
        1,
        "CORS response helper should be emitted once"
    );
    assert_eq!(
        count_occurrences(&output.bundle.contents, "function __sloppy_cors_preflight"),
        1,
        "CORS preflight helper should be emitted once"
    );
    assert_eq!(
        count_occurrences(
            &output.bundle.contents,
            "function __sloppy_framework_provider_open_options"
        ),
        1,
        "provider helper should be emitted once"
    );
    let source_map: serde_json::Value =
        serde_json::from_str(&output.source_map.contents).expect("source map should parse");
    let sources = source_map["sources"]
        .as_array()
        .expect("source map sources should be an array");
    let unique_sources = sources
        .iter()
        .filter_map(|source| source.as_str())
        .collect::<std::collections::BTreeSet<_>>()
        .len();
    assert_eq!(
        sources.len(),
        unique_sources,
        "source map sources should be deduplicated"
    );

    remove_dir_if_present(&root);
}

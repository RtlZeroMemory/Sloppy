#[test]
fn web_static_files_emit_routes_and_dependency_assets_from_config_root() {
    let root = fixture_temp_dir("web-static-files-config-root");
    let src_dir = root.join("src");
    let public_dir = root.join("public");
    let input = src_dir.join("main.ts");
    let out_dir = root.join(".sloppy");
    fs::create_dir_all(&src_dir).expect("source directory should be created");
    fs::create_dir_all(&public_dir).expect("public directory should be created");
    fs::write(
        public_dir.join("index.html"),
        "<!doctype html><title>static</title>",
    )
    .expect("html asset should write");
    fs::write(public_dir.join("hello.txt"), "hello static").expect("text asset should write");
    fs::write(public_dir.join("data.json"), "{\"ok\":true}\n").expect("json asset should write");
    fs::write(public_dir.join("module.wasm"), [0x00, 0x61, 0x73, 0x6d])
        .expect("wasm asset should write");
    fs::write(public_dir.join("app.js"), "export const ok = true;\n")
        .expect("script asset should write");
    fs::write(public_dir.join("app.js.br"), [0x1b, 0x14, 0x00, 0x00])
        .expect("brotli variant should write");
    fs::write(public_dir.join("app.js.gz"), [0x1f, 0x8b, 0x08, 0x00])
        .expect("gzip variant should write");
    fs::write(
        &input,
        r#"import { Sloppy } from "sloppy";

const app = Sloppy.create();
app.use((ctx, next) => next());
app.useCors({
  origins: ["https://app.example.com"],
  methods: ["GET"],
  headers: ["x-demo"]
});
app.staticFiles("/public", {
  root: "public",
  cache: { maxAgeSeconds: 60 },
  precompressed: true
});

export default app;
"#,
    )
    .expect("entry should write");

    let options = CompileOptions {
        kind: Some(super::ProjectKind::Web),
        config_dir: Some(root.clone()),
        ..CompileOptions::default()
    };
    super::build(&input, &out_dir, &options).expect("web static files should build");
    let plan_text = fs::read_to_string(out_dir.join("app.plan.json")).expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan_text).expect("plan should parse");
    let routes = plan["routes"]
        .as_array()
        .expect("routes should be an array");
    assert!(routes.iter().any(|route| route["method"] == "GET"
        && route["pattern"] == "/public/hello.txt"
        && route["tags"]
            .as_array()
            .unwrap()
            .iter()
            .any(|tag| tag == "static")));
    let static_route = routes
        .iter()
        .find(|route| route["method"] == "GET" && route["pattern"] == "/public/hello.txt")
        .expect("static route should exist");
    assert_eq!(
        static_route["middleware"]
            .as_array()
            .expect("static route should carry middleware")
            .len(),
        1
    );
    assert_eq!(
        static_route["cors"]["origins"],
        serde_json::json!(["https://app.example.com"])
    );
    assert!(routes.iter().any(|route| route["method"] == "OPTIONS"
        && route["pattern"] == "/public/hello.txt"
        && route["cors"]["preflight"] == true));
    assert!(routes
        .iter()
        .any(|route| route["method"] == "GET" && route["pattern"] == "/public/index.html"));
    assert!(routes
        .iter()
        .any(|route| route["method"] == "GET" && route["pattern"] == "/public"));
    assert!(routes
        .iter()
        .any(|route| route["method"] == "GET" && route["pattern"] == "/public/app.js"));
    assert!(routes
        .iter()
        .any(|route| route["method"] == "GET" && route["pattern"] == "/public/data.json"));
    assert!(routes
        .iter()
        .any(|route| route["method"] == "GET" && route["pattern"] == "/public/module.wasm"));

    let app_js = fs::read_to_string(out_dir.join("app.js")).expect("app js should emit");
    assert!(app_js.contains("__sloppyStaticAssetResponse(ctx"));
    assert!(app_js.contains("__sloppy_run_middleware"));
    assert!(app_js.contains("__sloppy_cors_preflight"));
    assert!(app_js.contains("cacheControl: \"public, max-age=60\""));
    assert!(app_js.contains("contentHash: \"sha256:"));
    assert!(app_js.contains("contentEncoding: \"br\""));
    assert!(app_js.contains("contentEncoding: \"gzip\""));
    assert!(!app_js.contains("Thu, 01 Jan 1970 00:00:00 GMT"));
    assert!(!app_js.contains("Last-Modified"));

    let graph_text =
        fs::read_to_string(out_dir.join("deps.graph.json")).expect("dependency graph should emit");
    let graph: serde_json::Value =
        serde_json::from_str(&graph_text).expect("dependency graph should parse");
    assert!(graph["assets"]
        .as_array()
        .expect("assets should be an array")
        .iter()
        .any(|asset| asset["path"] == "public/hello.txt"
            && asset["includedBy"] == "app.staticFiles:/public:public"));
    assert!(graph["assets"]
        .as_array()
        .expect("assets should be an array")
        .iter()
        .any(|asset| asset["path"] == "public/index.html"
            && asset["includedBy"] == "app.staticFiles:/public:public"));
    assert!(graph["assets"]
        .as_array()
        .expect("assets should be an array")
        .iter()
        .any(|asset| asset["path"] == "public/app.js"
            && asset["includedBy"] == "app.staticFiles:/public:public"));
    assert!(graph["assets"]
        .as_array()
        .expect("assets should be an array")
        .iter()
        .any(|asset| asset["path"] == "public/app.js.br"
            && asset["includedBy"] == "app.staticFiles:/public:public:precompressed"));
    assert!(graph["assets"]
        .as_array()
        .expect("assets should be an array")
        .iter()
        .any(|asset| asset["path"] == "public/app.js.gz"
            && asset["includedBy"] == "app.staticFiles:/public:public:precompressed"));
    assert!(graph["assets"]
        .as_array()
        .expect("assets should be an array")
        .iter()
        .any(|asset| asset["path"] == "public/data.json"
            && asset["includedBy"] == "app.staticFiles:/public:public"));
    assert!(graph["assets"]
        .as_array()
        .expect("assets should be an array")
        .iter()
        .any(|asset| asset["path"] == "public/module.wasm"
            && asset["includedBy"] == "app.staticFiles:/public:public"));

    fs::remove_dir_all(&root).expect("web fixture directory should be removable");
}

#[test]
fn web_static_files_reject_traversal_root() {
    let root = fixture_temp_dir("web-static-files-traversal-root");
    let src_dir = root.join("src");
    let public_dir = root.join("public");
    let input = src_dir.join("main.ts");
    let out_dir = root.join(".sloppy");
    fs::create_dir_all(&src_dir).expect("source directory should be created");
    fs::create_dir_all(&public_dir).expect("public directory should be created");
    fs::write(public_dir.join("hello.txt"), "hello").expect("asset should write");
    fs::write(
        &input,
        r#"import { Sloppy } from "sloppy";

const app = Sloppy.create();
app.useStaticFiles({ requestPath: "/public", root: "../public" });

export default app;
"#,
    )
    .expect("entry should write");

    let options = CompileOptions {
        kind: Some(super::ProjectKind::Web),
        config_dir: Some(root.clone()),
        ..CompileOptions::default()
    };
    let failure = super::build(&input, &out_dir, &options).expect_err("traversal root should fail");
    assert_eq!(
        failure.diagnostic.code,
        "SLOPPYC_E_UNSUPPORTED_STATIC_FILES"
    );
    assert!(failure
        .diagnostic
        .message
        .contains("root must be a safe project-relative directory"));

    fs::remove_dir_all(&root).expect("web fixture directory should be removable");
}

#[test]
fn web_spa_rejects_traversal_fallback() {
    let root = fixture_temp_dir("web-spa-traversal-fallback");
    let src_dir = root.join("src");
    let public_dir = root.join("public");
    let input = src_dir.join("main.ts");
    let out_dir = root.join(".sloppy");
    fs::create_dir_all(&src_dir).expect("source directory should be created");
    fs::create_dir_all(&public_dir).expect("public directory should be created");
    fs::write(public_dir.join("index.html"), "<!doctype html>").expect("asset should write");
    fs::write(
        &input,
        r#"import { Sloppy } from "sloppy";

const app = Sloppy.create();
app.spa("/", { root: "public", fallback: "../secret.html" });

export default app;
"#,
    )
    .expect("entry should write");

    let options = CompileOptions {
        kind: Some(super::ProjectKind::Web),
        config_dir: Some(root.clone()),
        ..CompileOptions::default()
    };
    let failure =
        super::build(&input, &out_dir, &options).expect_err("traversal fallback should fail");
    assert_eq!(
        failure.diagnostic.code,
        "SLOPPYC_E_UNSUPPORTED_STATIC_FILES"
    );
    assert!(failure
        .diagnostic
        .message
        .contains("fallback must be a safe root-relative file path"));

    fs::remove_dir_all(&root).expect("web fixture directory should be removable");
}

#[test]
fn web_static_files_reject_assets_over_alpha_inline_limit() {
    let root = fixture_temp_dir("web-static-files-large-asset");
    let src_dir = root.join("src");
    let public_dir = root.join("public");
    let input = src_dir.join("main.ts");
    let out_dir = root.join(".sloppy");
    fs::create_dir_all(&src_dir).expect("source directory should be created");
    fs::create_dir_all(&public_dir).expect("public directory should be created");
    fs::write(
        public_dir.join("large.txt"),
        vec![b'x'; super::STATIC_ASSET_INLINE_MAX_BYTES as usize + 1],
    )
    .expect("large asset should write");
    fs::write(
        &input,
        r#"import { Sloppy } from "sloppy";

const app = Sloppy.create();
app.useStaticFiles({ requestPath: "/public", root: "public" });

export default app;
"#,
    )
    .expect("entry should write");

    let options = CompileOptions {
        kind: Some(super::ProjectKind::Web),
        config_dir: Some(root.clone()),
        ..CompileOptions::default()
    };
    let failure =
        super::build(&input, &out_dir, &options).expect_err("oversized static asset should fail");
    assert_eq!(failure.diagnostic.code, "SLOPPYC_E_STATIC_FILES");
    assert!(failure
        .diagnostic
        .message
        .contains("configured inline limit"));

    fs::remove_dir_all(&root).expect("web fixture directory should be removable");
}

#[test]
fn web_static_files_reject_oversized_precompressed_variant_and_spa_fallback() {
    let cases = [
        (
            "variant",
            r#"app.staticFiles("/public", { root: "public", precompressed: ["gzip"], maxFileBytes: 4 });"#,
            "public/app.js.gz",
            "12345",
            "precompressed static asset exceeds the configured inline limit",
        ),
        (
            "spa-fallback",
            r#"app.spa("/", { root: "public", fallback: "shell.spa", maxFileBytes: 4 });"#,
            "public/shell.spa",
            "12345",
            "app.spa fallback exceeds the configured inline limit",
        ),
    ];

    for (case, registration, oversized_path, oversized_contents, expected) in cases {
        let root = fixture_temp_dir(&format!("web-static-files-max-{case}"));
        let src_dir = root.join("src");
        let public_dir = root.join("public");
        let input = src_dir.join("main.ts");
        let out_dir = root.join(".sloppy");
        fs::create_dir_all(&src_dir).expect("source directory should be created");
        fs::create_dir_all(&public_dir).expect("public directory should be created");
        fs::write(public_dir.join("app.js"), "ok").expect("script asset should write");
        fs::write(root.join(oversized_path), oversized_contents)
            .expect("oversized asset should write");
        fs::write(
            &input,
            format!(
                r#"import {{ Sloppy }} from "sloppy";

const app = Sloppy.create();
{registration}

export default app;
"#
            ),
        )
        .expect("entry should write");

        let options = CompileOptions {
            kind: Some(super::ProjectKind::Web),
            config_dir: Some(root.clone()),
            ..CompileOptions::default()
        };
        let failure =
            super::build(&input, &out_dir, &options).expect_err("oversized asset should fail");
        assert_eq!(failure.diagnostic.code, "SLOPPYC_E_STATIC_FILES");
        assert!(
            failure.diagnostic.message.contains(expected),
            "expected diagnostic containing {expected}, got {:?}",
            failure.diagnostic
        );
        fs::remove_dir_all(&root).expect("web fixture directory should be removable");
    }
}

#[test]
fn dynamic_web_app_js_fails_fast_for_use_static_files() {
    let root = fixture_temp_dir("dynamic-web-static-files");
    fs::create_dir_all(root.join("public")).expect("public directory should be created");
    fs::write(root.join("public/hello.txt"), "hello").expect("asset should write");
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
function pathFor(name) { return `/${name}`; }
app.mapGet(pathFor("health"), () => Results.text("ok"));
app.useStaticFiles({ requestPath: "/public", root: "public" });
export default app;
"#;
    let app = extract_temp_input(&root, source).expect("dynamic web app should extract");
    assert_eq!(app.dynamic_routes.len(), 1);
    let emitted = super::emit_app_js(&app);
    assert!(emitted
        .source
        .contains("Sloppy app.useStaticFiles is not supported for dynamic fallback routes yet."));

    fs::remove_dir_all(&root).expect("web fixture directory should be removable");
}

#[test]
fn program_mode_marks_each_runtime_stdlib_subpath() {
    let cases = [
        ("sloppy/fs", "File", "stdlib.fs"),
        ("sloppy/net", "HttpClient", "stdlib.httpclient"),
        ("sloppy/os", "Process", "stdlib.os"),
        ("sloppy/time", "Time", "stdlib.time"),
        ("sloppy/crypto", "Random", "stdlib.crypto"),
        ("sloppy/codec", "Base64", "stdlib.codec"),
        ("sloppy/cache", "Cache", "stdlib.cache"),
        ("sloppy/redis", "Redis", "stdlib.redis"),
        ("sloppy/workers", "WorkQueue", "stdlib.workers"),
    ];
    for (index, (module, imported, feature)) in cases.iter().enumerate() {
        let root = fixture_temp_dir(&format!("program-stdlib-feature-{index}"));
        let input = root.join("main.ts");
        let out_dir = root.join(".sloppy");
        fs::write(
            &input,
            format!(
                "import {{ {imported} }} from \"{module}\";\nexport function main() {{ return {imported}; }}\n"
            ),
        )
        .expect("entry should write");

        super::build(&input, &out_dir, &CompileOptions::new()).expect("program should build");
        let plan_text =
            fs::read_to_string(out_dir.join("app.plan.json")).expect("plan should be readable");
        let plan: serde_json::Value = serde_json::from_str(&plan_text).expect("plan should parse");
        assert!(plan["requiredFeatures"]
            .as_array()
            .expect("required features should be an array")
            .contains(&serde_json::json!(feature)));

        fs::remove_dir_all(&root).expect("program fixture directory should be removable");
    }
}

#[test]
fn output_cache_fluent_route_metadata_stays_plan_visible() {
    let root = fixture_temp_dir("output-cache-route-metadata");
    let input = root.join("app.ts");
    let out_dir = root.join(".sloppy");
    fs::write(
        &input,
        r#"import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();
app.get("/products", () => Results.json([]))
  .outputCache({
    ttlMs: 30000,
    varyByQuery: ["category", "page"],
    tags: ["products"]
  })
  .cacheHeaders({
    cacheControl: "public, max-age=60",
    vary: ["Accept-Encoding"],
    etag: true
  });
app.get("/disabled", () => Results.json({ ok: true }))
  .outputCache({ ttlMs: 1000 })
  .noOutputCache();
app.get("/reenabled", () => Results.json({ ok: true }))
  .noOutputCache()
  .outputCache({ ttlMs: 5000 });

export default app;
"#,
    )
    .expect("entry should write");

    super::build(
        &input,
        &out_dir,
        &CompileOptions {
            kind: Some(super::ProjectKind::Web),
            ..CompileOptions::default()
        },
    )
    .expect("web app should build");
    let plan_text =
        fs::read_to_string(out_dir.join("app.plan.json")).expect("plan should be readable");
    let plan: serde_json::Value = serde_json::from_str(&plan_text).expect("plan should parse");

    assert!(plan["requiredFeatures"]
        .as_array()
        .expect("required features should be an array")
        .contains(&serde_json::json!("stdlib.cache")));
    assert_eq!(plan["features"]["cache"], serde_json::json!(true));
    let routes = plan["routes"]
        .as_array()
        .expect("routes should be an array");
    let product_route = routes
        .iter()
        .find(|route| route["pattern"] == "/products" && route["method"] == "GET")
        .expect("products route should exist");
    assert_eq!(
        product_route["outputCache"]["ttlMs"].as_f64(),
        Some(30000.0)
    );
    assert_eq!(
        product_route["cacheHeaders"]["cacheControl"],
        serde_json::json!("public, max-age=60")
    );
    let disabled_route = routes
        .iter()
        .find(|route| route["pattern"] == "/disabled" && route["method"] == "GET")
        .expect("disabled route should exist");
    assert!(disabled_route.get("outputCache").is_none());
    let reenabled_route = routes
        .iter()
        .find(|route| route["pattern"] == "/reenabled" && route["method"] == "GET")
        .expect("reenabled route should exist");
    assert_eq!(
        reenabled_route["outputCache"]["ttlMs"].as_f64(),
        Some(5000.0)
    );
    let output_cache_routes = plan["cache"]["outputCacheRoutes"]
        .as_array()
        .expect("output cache routes should be an array");
    assert!(output_cache_routes
        .iter()
        .any(|route| route["pattern"] == "/products"));
    assert!(output_cache_routes
        .iter()
        .any(|route| route["pattern"] == "/reenabled"));
    assert!(!output_cache_routes
        .iter()
        .any(|route| route["pattern"] == "/disabled"));

    fs::remove_dir_all(&root).expect("web fixture directory should be removable");
}

#[test]
fn root_cache_import_is_available_to_compiled_web_runtime() {
    let source = r#"import { Cache, Results, Sloppy } from "sloppy";
function createCache() {
  return Cache.memory("default");
}
const app = Sloppy.create();
app.services.addSingleton("cache.default", () => createCache());
app.get("/cached", () => Results.json({ ok: true }))
  .outputCache({ ttlMs: 1000 });
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("root Cache import should extract for web apps");
    assert!(app.uses_cache_runtime);
    let emitted_js = super::emit_app_js(&app);
    assert!(
        emitted_js.source.contains("Cache"),
        "compiled web bundle must import Cache when service helpers capture it:\n{}",
        emitted_js.source
    );
    assert!(emitted_js
        .source
        .contains("return Cache.memory(\"default\")"));
}
#[test]
fn sloppy_redis_import_emits_plan_required_feature() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { Redis, SloppyRedisError } from "sloppy/redis";
const app = Sloppy.create();
app.mapGet("/", () => Results.text(Redis.token("main").toString()));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("sloppy/redis import should be recognized");
    assert!(app.uses_redis_runtime);
    assert!(app.uses_net_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("Redis"));
    assert!(emitted_js.source.contains("SloppyRedisError"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");

    let required = value["requiredFeatures"]
        .as_array()
        .expect("required features should be an array");
    assert!(required.contains(&serde_json::json!("stdlib.redis")));
    assert!(required.contains(&serde_json::json!("stdlib.net")));
    assert_eq!(value["features"]["redis"], serde_json::json!(true));
    assert_eq!(value["redis"]["enabled"], serde_json::json!(true));
    assert!(
        value["doctorChecks"]
            .as_array()
            .expect("doctor checks should be an array")
            .iter()
            .any(|check| check["id"] == "stdlib.redis.contract")
    );
}

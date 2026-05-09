use std::{fs, path::Path, process::Command};

use sloppyc::{compile_file, CompileOptions};

fn manifest_dir() -> &'static Path {
    Path::new(env!("CARGO_MANIFEST_DIR"))
}

fn temp_output(name: &str) -> std::path::PathBuf {
    std::env::temp_dir().join(format!("sloppyc-{name}-{}", std::process::id()))
}

fn remove_dir_if_present(path: &Path) {
    if path.exists() {
        fs::remove_dir_all(path).expect("temporary compiler output should be removable");
    }
}

#[test]
fn library_api_builds_current_hello_fixture() {
    let root = manifest_dir();
    let input = root.join("../examples/compiler-hello/app.js");
    let out_dir = temp_output("library-api-hello");
    remove_dir_if_present(&out_dir);

    let output =
        compile_file(&input, &out_dir, &CompileOptions::default()).expect("hello should compile");

    assert_eq!(
        output.plan.contents,
        fs::read_to_string(root.join("../examples/compiler-hello/expected/app.plan.json"))
            .expect("expected plan should be readable")
    );
    assert_eq!(
        output.bundle.contents,
        fs::read_to_string(root.join("../examples/compiler-hello/expected/app.js"))
            .expect("expected bundle should be readable")
    );
    assert_eq!(
        output.source_map.contents,
        fs::read_to_string(root.join("../examples/compiler-hello/expected/app.js.map"))
            .expect("expected source map should be readable")
    );

    remove_dir_if_present(&out_dir);
}

#[test]
fn library_api_builds_app_graph_dogfood_fixture() {
    let root = manifest_dir();
    let input = root.join("tests/fixtures/app-graph-dogfood/input.ts");
    let out_dir = temp_output("library-api-app-graph-dogfood");
    remove_dir_if_present(&out_dir);

    let output = compile_file(&input, &out_dir, &CompileOptions::default())
        .expect("app graph dogfood should compile");

    assert_eq!(
        output.plan.contents,
        fs::read_to_string(root.join("tests/fixtures/app-graph-dogfood/expected/app.plan.json"))
            .expect("expected plan should be readable")
    );
    assert_eq!(
        output.bundle.contents,
        fs::read_to_string(root.join("tests/fixtures/app-graph-dogfood/expected/app.js"))
            .expect("expected bundle should be readable")
    );
    assert_eq!(
        output.source_map.contents,
        fs::read_to_string(root.join("tests/fixtures/app-graph-dogfood/expected/app.js.map"))
            .expect("expected source map should be readable")
    );

    remove_dir_if_present(&out_dir);
}

#[test]
fn library_api_returns_source_located_invalid_input_diagnostic() {
    let root = manifest_dir();
    let input = root.join("tests/fixtures/computed-method/input.js");
    let out_dir = temp_output("library-api-invalid");
    remove_dir_if_present(&out_dir);

    let error =
        compile_file(&input, &out_dir, &CompileOptions::default()).expect_err("input must fail");

    assert_eq!(
        error.diagnostic.code,
        "SLOPPYC_E_UNSUPPORTED_COMPUTED_ROUTE_METHOD"
    );
    assert!(error.diagnostic.span.is_some());
    assert!(!out_dir.join("app.plan.json").exists());
    assert!(!out_dir.join("app.js").exists());
    assert!(!out_dir.join("app.js.map").exists());
}

#[test]
fn cli_builds_current_supported_fixture() {
    let root = manifest_dir();
    let input = root.join("../examples/compiler-hello/app.js");
    let out_dir = temp_output("cli-hello");
    remove_dir_if_present(&out_dir);

    let binary = env!("CARGO_BIN_EXE_sloppyc");
    let status = Command::new(binary)
        .arg("build")
        .arg(&input)
        .arg("--out")
        .arg(&out_dir)
        .status()
        .expect("sloppyc binary should launch");

    assert!(status.success());
    assert_eq!(
        fs::read_to_string(out_dir.join("app.plan.json")).expect("plan should be emitted"),
        fs::read_to_string(root.join("../examples/compiler-hello/expected/app.plan.json"))
            .expect("expected plan should be readable")
    );

    remove_dir_if_present(&out_dir);
}

#[test]
fn cli_writes_timing_json_when_requested() {
    let root = manifest_dir();
    let input = root.join("../examples/compiler-hello/app.js");
    let out_dir = temp_output("cli-timings");
    remove_dir_if_present(&out_dir);
    let timings = out_dir.join("timings.json");

    let binary = env!("CARGO_BIN_EXE_sloppyc");
    let status = Command::new(binary)
        .arg("build")
        .arg(&input)
        .arg("--out")
        .arg(&out_dir)
        .arg("--timings-json")
        .arg(&timings)
        .status()
        .expect("sloppyc binary should launch");

    assert!(status.success());
    let timings_json: serde_json::Value =
        serde_json::from_str(&fs::read_to_string(&timings).expect("timings should be written"))
            .expect("timings should parse");
    assert_eq!(timings_json["schemaVersion"], 1);
    assert!(timings_json["phases"]["parseEntryMs"].is_number());
    assert!(timings_json["phases"]["bundleEmitMs"].is_number());
    assert!(timings_json["counters"]["routes"].is_number());
    assert!(timings_json["artifacts"]["appJsBytes"].is_number());

    remove_dir_if_present(&out_dir);
}

#[test]
fn cli_rejects_timing_json_path_that_would_clobber_artifacts() {
    let root = manifest_dir();
    let input = root.join("../examples/compiler-hello/app.js");
    let out_dir = temp_output("cli-timings-conflict");
    remove_dir_if_present(&out_dir);

    let binary = env!("CARGO_BIN_EXE_sloppyc");
    let output = Command::new(binary)
        .arg("build")
        .arg(&input)
        .arg("--out")
        .arg(&out_dir)
        .arg("--timings-json")
        .arg(out_dir.join("app.plan.json"))
        .output()
        .expect("sloppyc binary should launch");

    assert!(!output.status.success());
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("timings JSON output conflicts with emitted Plan path"));

    remove_dir_if_present(&out_dir);
}

#[test]
fn working_tree_has_no_generated_or_cache_artifacts_staged() {
    let status = Command::new("git")
        .args(["diff", "--cached", "--name-only"])
        .output()
        .expect("git should be available");
    assert!(status.status.success());
    let staged = String::from_utf8(status.stdout).expect("git output should be utf-8");
    for path in staged.lines() {
        assert!(
            !(path.starts_with("compiler/target/")
                || path.starts_with("target/")
                || path.starts_with("build/")
                || path.starts_with("artifacts/")
                || path.starts_with(".sloppy/")),
            "generated/cache artifact must not be staged: {path}"
        );
    }
}

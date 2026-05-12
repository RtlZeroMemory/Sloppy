//! Compile-and-graph coverage for representative npm-compat fixtures.
//!
//! The resolver matrix test asserts classification only. These tests drive the
//! full `compile_file` pipeline against a representative slice of fixtures and
//! verify that each one produces `app.plan.json`, `app.js`, `app.js.map`, and
//! `deps.graph.json`, and that the emitted dependency graph contains the
//! expected package or module record.
//!
//! Runtime execution under V8 is intentionally outside the scope of this test
//! and lives in the CMake `check_node_compat_packages` and
//! `check_create_package_command` lanes when V8 is available.

use std::{fs, path::Path};

use serde_json::Value;
use sloppyc::{compile_file, CompileOptions};

fn fixtures_root() -> std::path::PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../tests/fixtures/npm-compat")
}

fn temp_output(name: &str) -> std::path::PathBuf {
    std::env::temp_dir().join(format!(
        "sloppyc-npm-compat-build-{name}-{}",
        std::process::id()
    ))
}

fn remove_dir_if_present(path: &Path) {
    if path.exists() {
        fs::remove_dir_all(path).expect("temp output should be removable");
    }
}

struct Representative {
    fixture: &'static str,
    expected_package: Option<&'static str>,
    expected_resolved_id: Option<&'static str>,
    expected_node_builtins: &'static [&'static str],
}

const REPRESENTATIVES: &[Representative] = &[
    Representative {
        fixture: "basic-main-cjs",
        expected_package: Some("greet-cjs"),
        expected_resolved_id: Some("node_modules/greet-cjs/lib/greet.js"),
        expected_node_builtins: &[],
    },
    Representative {
        fixture: "basic-main-esm",
        expected_package: Some("greet-esm"),
        expected_resolved_id: Some("node_modules/greet-esm/greet.js"),
        expected_node_builtins: &[],
    },
    Representative {
        fixture: "exports-nested-conditions",
        expected_package: Some("nested-pkg"),
        expected_resolved_id: Some("node_modules/nested-pkg/entry.mjs"),
        expected_node_builtins: &[],
    },
    Representative {
        fixture: "imports-alias",
        expected_package: Some("imports-alias-pkg"),
        expected_resolved_id: Some("src/util.js"),
        expected_node_builtins: &[],
    },
    Representative {
        fixture: "self-reference",
        expected_package: Some("self-ref-pkg"),
        expected_resolved_id: Some("index.js"),
        expected_node_builtins: &[],
    },
    Representative {
        fixture: "interop-cjs-requires-json",
        expected_package: Some("cjs-json-pkg"),
        expected_resolved_id: Some("node_modules/cjs-json-pkg/index.js"),
        expected_node_builtins: &[],
    },
    Representative {
        fixture: "builtins-fs-promises-buffer",
        expected_package: Some("buf-fs-pkg"),
        expected_resolved_id: Some("node_modules/buf-fs-pkg/index.js"),
        expected_node_builtins: &["node:fs/promises", "node:buffer"],
    },
    Representative {
        fixture: "optional-native-unused",
        expected_package: Some("opt-native-pkg"),
        expected_resolved_id: Some("node_modules/opt-native-pkg/index.js"),
        expected_node_builtins: &[],
    },
    Representative {
        fixture: "dynamic-require-literal",
        expected_package: Some("dyn-pkg"),
        expected_resolved_id: Some("node_modules/dyn-pkg/helper.js"),
        expected_node_builtins: &[],
    },
];

fn assert_graph_contains_package(graph: &Value, expected_name: &str) {
    let packages = graph
        .get("packages")
        .and_then(Value::as_array)
        .expect("packages array");
    assert!(
        packages
            .iter()
            .any(|entry| entry.get("name").and_then(Value::as_str) == Some(expected_name)),
        "dependency graph missing expected package {expected_name}: {packages:?}"
    );
}

fn assert_graph_contains_module(graph: &Value, expected_suffix: &str) {
    let modules = graph
        .get("modules")
        .and_then(Value::as_array)
        .expect("modules array");
    assert!(
        modules.iter().any(|entry| {
            entry
                .get("id")
                .and_then(Value::as_str)
                .is_some_and(|id| id.replace('\\', "/").ends_with(expected_suffix))
        }),
        "dependency graph missing module ending with {expected_suffix}: {modules:?}"
    );
}

fn assert_graph_contains_builtin(graph: &Value, expected: &str) {
    let builtins = graph
        .get("nodeBuiltins")
        .and_then(Value::as_array)
        .expect("nodeBuiltins array");
    assert!(
        builtins
            .iter()
            .any(|entry| entry.get("specifier").and_then(Value::as_str) == Some(expected)),
        "dependency graph missing builtin {expected}: {builtins:?}"
    );
}

#[test]
fn representative_fixtures_compile_and_emit_dependency_graph() {
    let mut failures: Vec<String> = Vec::new();
    for representative in REPRESENTATIVES {
        let fixture_dir = fixtures_root().join(representative.fixture);
        let entry = fixture_dir.join("src/main.ts");
        let out_dir = temp_output(representative.fixture);
        remove_dir_if_present(&out_dir);

        let options = CompileOptions {
            config_dir: Some(fixture_dir.clone()),
            ..CompileOptions::default()
        };

        match compile_file(&entry, &out_dir, &options) {
            Ok(output) => {
                let plan: Value = match serde_json::from_str(&output.plan.contents) {
                    Ok(value) => value,
                    Err(error) => {
                        failures.push(format!(
                            "{}: plan.json failed to parse: {error}",
                            representative.fixture
                        ));
                        continue;
                    }
                };
                let graph_path = out_dir.join("deps.graph.json");
                if !graph_path.exists() {
                    failures.push(format!(
                        "{}: deps.graph.json was not emitted next to plan",
                        representative.fixture
                    ));
                    continue;
                }
                let graph_text = match fs::read_to_string(&graph_path) {
                    Ok(text) => text,
                    Err(error) => {
                        failures.push(format!(
                            "{}: deps.graph.json read failed: {error}",
                            representative.fixture
                        ));
                        continue;
                    }
                };
                let graph: Value = match serde_json::from_str(&graph_text) {
                    Ok(value) => value,
                    Err(error) => {
                        failures.push(format!(
                            "{}: deps.graph.json parse failed: {error}",
                            representative.fixture
                        ));
                        continue;
                    }
                };

                if let Some(name) = representative.expected_package {
                    assert_graph_contains_package(&graph, name);
                }
                if let Some(resolved_id) = representative.expected_resolved_id {
                    assert_graph_contains_module(&graph, resolved_id);
                }
                for builtin in representative.expected_node_builtins {
                    assert_graph_contains_builtin(&graph, builtin);
                }

                let plan_graph = plan
                    .get("dependencyGraph")
                    .expect("plan.json must embed dependencyGraph");
                if let Some(name) = representative.expected_package {
                    assert_graph_contains_package(plan_graph, name);
                }

                let bundle = out_dir.join("app.js");
                assert!(
                    bundle.exists(),
                    "{}: app.js was not emitted",
                    representative.fixture
                );
                let source_map = out_dir.join("app.js.map");
                assert!(
                    source_map.exists(),
                    "{}: app.js.map was not emitted",
                    representative.fixture
                );

                let modules = graph
                    .get("modules")
                    .and_then(Value::as_array)
                    .expect("modules array");
                for entry in modules {
                    let id = entry.get("id").and_then(Value::as_str).unwrap_or_default();
                    if let Some(byte) = id.as_bytes().get(1).copied() {
                        if byte == b':' && id.as_bytes()[0].is_ascii_alphabetic() {
                            panic!(
                                "{}: dependency graph leaks an absolute drive-letter path: {id}",
                                representative.fixture
                            );
                        }
                    }
                    assert!(
                        !id.starts_with("//?/")
                            && !id.starts_with("/Users/")
                            && !id.starts_with("/home/"),
                        "{}: dependency graph leaks absolute machine path: {id}",
                        representative.fixture
                    );
                }

                remove_dir_if_present(&out_dir);
            }
            Err(error) => {
                remove_dir_if_present(&out_dir);
                failures.push(format!(
                    "{}: compile_file rejected fixture: {} ({})",
                    representative.fixture, error.diagnostic.code, error.diagnostic.message
                ));
            }
        }
    }

    assert!(
        failures.is_empty(),
        "representative npm-compat fixtures failed to build:\n  - {}",
        failures.join("\n  - ")
    );
}

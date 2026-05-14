use std::{
    fs,
    path::{Path, PathBuf},
};

use serde_json::Value;
use sha2::{Digest, Sha256};
use sloppyc::{
    compile_file,
    compiler_contract::{validate_plan_text, ContractArtifacts, ContractViolation},
    CompileOptions,
};

fn manifest_dir() -> &'static Path {
    Path::new(env!("CARGO_MANIFEST_DIR"))
}

fn temp_output(name: &str) -> PathBuf {
    std::env::temp_dir().join(format!("sloppyc-contract-{name}-{}", std::process::id()))
}

fn remove_dir_if_present(path: &Path) {
    if path.exists() {
        fs::remove_dir_all(path).expect("temporary directory should be removable");
    }
}

fn validate_artifacts(plan_text: &str, out_dir: &Path) -> Result<(), Vec<ContractViolation>> {
    let route_artifact = fs::read(out_dir.join("routes.slrt")).ok();
    validate_with_route_artifact(plan_text, out_dir, route_artifact.as_deref())
}

fn validate_with_route_artifact(
    plan_text: &str,
    out_dir: &Path,
    route_artifact: Option<&[u8]>,
) -> Result<(), Vec<ContractViolation>> {
    let dependency_graph = fs::read_to_string(out_dir.join("deps.graph.json"))
        .ok()
        .map(|text| serde_json::from_str::<Value>(&text).expect("deps graph should parse"));
    validate_plan_text(
        plan_text,
        ContractArtifacts {
            route_artifact,
            dependency_graph: dependency_graph.as_ref(),
            artifact_root: None,
        },
    )
}

fn assert_contract_ok(plan_text: &str, out_dir: &Path) {
    if let Err(violations) = validate_artifacts(plan_text, out_dir) {
        panic!(
            "compiler contract validation failed:\n{}",
            violations
                .iter()
                .map(ToString::to_string)
                .collect::<Vec<_>>()
                .join("\n")
        );
    }
}

fn write_file(path: &Path, contents: &str) {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).expect("parent directory should be creatable");
    }
    fs::write(path, contents).expect("fixture source should be writable");
}

fn compile_temp_source(name: &str, source: &str) -> (String, PathBuf) {
    let input_dir = temp_output(&format!("{name}-input"));
    let out_dir = temp_output(name);
    remove_dir_if_present(&input_dir);
    remove_dir_if_present(&out_dir);
    fs::create_dir_all(&input_dir).expect("input directory should be creatable");
    let input = input_dir.join("app.ts");
    write_file(&input, source);
    let output = compile_file(&input, &out_dir, &CompileOptions::default())
        .expect("generated source should compile");
    remove_dir_if_present(&input_dir);
    (output.plan.contents, out_dir)
}

fn read_u32(bytes: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes(
        bytes[offset..offset + 4]
            .try_into()
            .expect("route artifact u32 range should exist"),
    )
}

fn write_u32(bytes: &mut [u8], offset: usize, value: u32) {
    bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
}

fn route_table_offset(bytes: &[u8]) -> usize {
    read_u32(bytes, 24) as usize
}

fn string_table_offset(bytes: &[u8]) -> usize {
    read_u32(bytes, 32) as usize
}

fn first_pattern_start(bytes: &[u8]) -> usize {
    let entry = route_table_offset(bytes);
    let pattern_offset = read_u32(bytes, entry + 8) as usize;
    string_table_offset(bytes) + pattern_offset
}

fn sha256_hash(bytes: &[u8]) -> String {
    let digest = Sha256::digest(bytes);
    let mut output = String::from("sha256:");
    for byte in digest {
        output.push_str(&format!("{byte:02x}"));
    }
    output
}

fn plan_with_route_artifact_hash(plan_text: &str, route_artifact: &[u8]) -> String {
    let mut plan: Value = serde_json::from_str(plan_text).expect("plan should parse");
    plan["routeDispatch"]["artifact"]["hash"] = Value::String(sha256_hash(route_artifact));
    serde_json::to_string_pretty(&plan).expect("plan should serialize")
}

#[test]
fn validator_accepts_current_full_framework_fixture() {
    let root = manifest_dir();
    let input = root.join("tests/fixtures/full-framework-app-graph/input.ts");
    let out_dir = temp_output("full-framework-fixture");
    remove_dir_if_present(&out_dir);

    let output = compile_file(&input, &out_dir, &CompileOptions::default())
        .expect("full framework fixture should compile");

    assert_contract_ok(&output.plan.contents, &out_dir);
    remove_dir_if_present(&out_dir);
}

#[test]
fn validator_rejects_provider_effect_native_static_dispatch() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
const db = app.provider("sqlite:main");
app.get("/ready", async () => {
  const row = await db.queryOne("select 1 as ok", []);
  return Results.ok({ ok: row?.ok === 1 });
});
export default app;
"#;
    let (plan_text, out_dir) = compile_temp_source("mutated-provider-dispatch", source);
    let mut plan: Value = serde_json::from_str(&plan_text).expect("plan should parse");
    plan["routes"][0]["dispatch"]["executionKind"] = Value::String("native-static-json".into());
    plan["routes"][0]["nativeResponse"] = serde_json::json!({
        "kind": "json",
        "status": 200,
        "body": "{\"ok\":true}",
        "contentType": "application/json"
    });
    plan["routeDispatch"]["nativeNoJsEndpoints"] = Value::from(1);
    let mutated = serde_json::to_string_pretty(&plan).expect("mutated plan should serialize");

    let violations = validate_artifacts(&mutated, &out_dir).expect_err("mutation must fail");
    assert!(violations
        .iter()
        .any(|violation| violation.invariant == "dispatch.provider-effects-v8"));
    assert!(violations
        .iter()
        .any(|violation| violation.invariant == "dispatch.provider-effects-no-native-response"));

    remove_dir_if_present(&out_dir);
}

#[test]
fn validator_rejects_unknown_execution_kind() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.get("/ready", () => Results.text("ok"));
export default app;
"#;
    let (plan_text, out_dir) = compile_temp_source("mutated-execution-kind", source);
    let mut plan: Value = serde_json::from_str(&plan_text).expect("plan should parse");
    plan["routes"][0]["dispatch"]["executionKind"] = Value::String("native-maybe".into());
    let mutated = serde_json::to_string_pretty(&plan).expect("mutated plan should serialize");

    let violations = validate_artifacts(&mutated, &out_dir).expect_err("mutation must fail");
    assert!(violations
        .iter()
        .any(|violation| violation.invariant == "dispatch.execution-kind"));

    remove_dir_if_present(&out_dir);
}

#[test]
fn validator_rejects_slrt_method_that_disagrees_with_plan() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.get("/ready", () => Results.text("ok"));
export default app;
"#;
    let (plan_text, out_dir) = compile_temp_source("mutated-route-artifact-method", source);
    let mut route_artifact =
        fs::read(out_dir.join("routes.slrt")).expect("route artifact should exist");
    let first_entry = route_table_offset(&route_artifact);
    write_u32(&mut route_artifact, first_entry, 2);
    let mutated_plan = plan_with_route_artifact_hash(&plan_text, &route_artifact);

    let violations = validate_with_route_artifact(&mutated_plan, &out_dir, Some(&route_artifact))
        .expect_err("method mutation must fail");
    assert!(violations
        .iter()
        .any(|violation| violation.invariant == "route-artifact.method-agreement"));

    remove_dir_if_present(&out_dir);
}

#[test]
fn validator_rejects_slrt_pattern_that_disagrees_with_plan() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.get("/ready", () => Results.text("ok"));
export default app;
"#;
    let (plan_text, out_dir) = compile_temp_source("mutated-route-artifact-pattern", source);
    let mut route_artifact =
        fs::read(out_dir.join("routes.slrt")).expect("route artifact should exist");
    let pattern_start = first_pattern_start(&route_artifact);
    route_artifact[pattern_start + 1] = b'X';
    let mutated_plan = plan_with_route_artifact_hash(&plan_text, &route_artifact);

    let violations = validate_with_route_artifact(&mutated_plan, &out_dir, Some(&route_artifact))
        .expect_err("pattern mutation must fail");
    assert!(violations
        .iter()
        .any(|violation| violation.invariant == "route-artifact.pattern-agreement"));

    remove_dir_if_present(&out_dir);
}

#[test]
fn validator_rejects_route_artifact_hash_that_disagrees_with_slrt_bytes() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.get("/ready", () => Results.text("ok"));
export default app;
"#;
    let (plan_text, out_dir) = compile_temp_source("mutated-route-artifact-hash", source);
    let route_artifact =
        fs::read(out_dir.join("routes.slrt")).expect("route artifact should exist");
    let mut plan: Value = serde_json::from_str(&plan_text).expect("plan should parse");
    plan["routeDispatch"]["artifact"]["hash"] = Value::String(format!("sha256:{}", "0".repeat(64)));
    let mutated_plan = serde_json::to_string_pretty(&plan).expect("plan should serialize");

    let violations = validate_with_route_artifact(&mutated_plan, &out_dir, Some(&route_artifact))
        .expect_err("artifact hash mutation must fail");
    assert!(violations
        .iter()
        .any(|violation| violation.invariant == "route-artifact.hash-agreement"));

    remove_dir_if_present(&out_dir);
}

#[test]
fn validator_rejects_missing_route_artifact_hash_when_slrt_bytes_are_present() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.get("/ready", () => Results.text("ok"));
export default app;
"#;
    let (plan_text, out_dir) = compile_temp_source("missing-route-artifact-hash", source);
    let route_artifact =
        fs::read(out_dir.join("routes.slrt")).expect("route artifact should exist");
    let mut plan: Value = serde_json::from_str(&plan_text).expect("plan should parse");
    plan["routeDispatch"]["artifact"]
        .as_object_mut()
        .expect("route artifact metadata should be an object")
        .remove("hash");
    let mutated_plan = serde_json::to_string_pretty(&plan).expect("plan should serialize");

    let violations = validate_with_route_artifact(&mutated_plan, &out_dir, Some(&route_artifact))
        .expect_err("missing artifact hash must fail");
    assert!(violations
        .iter()
        .any(|violation| violation.invariant == "route-artifact.hash-present"));

    remove_dir_if_present(&out_dir);
}

#[test]
fn historical_regression_seed_registry_replays() {
    let root = manifest_dir().join("tests/regressions");
    let out_root = temp_output("regression-seeds");
    remove_dir_if_present(&out_root);
    fs::create_dir_all(&out_root).expect("output root should be creatable");
    let cases = fs::read_dir(&root).expect("regression seed directory should exist");
    let mut replayed = 0usize;

    for entry in cases {
        let case_dir = entry.expect("regression entry should be readable").path();
        if !case_dir.is_dir() {
            continue;
        }
        let case_text =
            fs::read_to_string(case_dir.join("case.json")).expect("case metadata should exist");
        let case: Value = serde_json::from_str(&case_text).expect("case metadata should parse");
        let name = case["name"].as_str().expect("case name should be present");
        let input = case_dir.join(case["input"].as_str().unwrap_or("input.ts"));
        let out_dir = out_root.join(name);
        remove_dir_if_present(&out_dir);

        let output = compile_file(&input, &out_dir, &CompileOptions::default())
            .unwrap_or_else(|error| panic!("{name}: seed should compile: {:?}", error.diagnostic));
        assert_contract_ok(&output.plan.contents, &out_dir);

        let plan: Value = serde_json::from_str(&output.plan.contents).expect("plan should parse");
        if let Some(expected_routes) = case["expected"]["routes"].as_u64() {
            assert_eq!(
                plan["routes"]
                    .as_array()
                    .expect("routes should be an array")
                    .len(),
                expected_routes as usize,
                "{name}: route count mismatch"
            );
        }
        if let Some(provider_effect_routes) = case["expected"]["providerEffectRoutes"].as_u64() {
            let actual = plan["routes"]
                .as_array()
                .expect("routes should be an array")
                .iter()
                .filter(|route| {
                    route["effects"]
                        .as_array()
                        .is_some_and(|effects| !effects.is_empty())
                })
                .count();
            assert_eq!(
                actual, provider_effect_routes as usize,
                "{name}: provider effects"
            );
        }
        if let Some(native_no_js) = case["expected"]["nativeNoJsEndpoints"].as_u64() {
            assert_eq!(
                plan["routeDispatch"]["nativeNoJsEndpoints"].as_u64(),
                Some(native_no_js),
                "{name}: native no-JS endpoint count"
            );
        }
        replayed += 1;
    }

    assert!(
        replayed >= 5,
        "expected at least five historical regression seeds"
    );
    remove_dir_if_present(&out_root);
}

#[test]
fn deterministic_grammar_cases_match_expected_semantics() {
    struct Case {
        name: &'static str,
        method: &'static str,
        path: &'static str,
        handler: &'static str,
        helper: &'static str,
        expected_effect_routes: usize,
    }

    let cases = [
        Case {
            name: "direct-static-json",
            method: "get",
            path: "/direct",
            helper: "",
            handler: "() => Results.ok({ mode: \"direct\" })",
            expected_effect_routes: 0,
        },
        Case {
            name: "same-file-helper-provider",
            method: "post",
            path: "/helper",
            helper: "async function readReady(db) { return await db.queryOne(\"select 1 as ok\", []); }",
            handler: "async () => Results.ok({ row: await readReady(db) })",
            expected_effect_routes: 1,
        },
        Case {
            name: "local-shadowing",
            method: "delete",
            path: "/shadow/{id:int}",
            helper: "function readReady() { return { ok: false }; }",
            handler: "(ctx) => { const readReady = () => ({ ok: true, id: ctx.route.id }); return Results.ok(readReady()); }",
            expected_effect_routes: 0,
        },
    ];

    for case in cases {
        let provider_setup = if case.expected_effect_routes == 0 {
            ""
        } else {
            r#"import { sqlite } from "sloppy/providers/sqlite";
app.use(sqlite("main", { database: ":memory:" }));
const db = app.provider("sqlite:main");
"#
        };
        let source = format!(
            r#"import {{ Sloppy, Results }} from "sloppy";
const app = Sloppy.create();
{provider_setup}
{helper}
app.{method}("{path}", {handler});
export default app;
"#,
            provider_setup = provider_setup,
            helper = case.helper,
            method = case.method,
            path = case.path,
            handler = case.handler
        );
        let (first_plan, first_out) = compile_temp_source(case.name, &source);
        let (second_plan, second_out) =
            compile_temp_source(&format!("{}-again", case.name), &source);
        assert_eq!(
            first_plan, second_plan,
            "{} should be deterministic",
            case.name
        );
        assert_contract_ok(&first_plan, &first_out);

        let plan: Value = serde_json::from_str(&first_plan).expect("plan should parse");
        let effect_routes = plan["routes"]
            .as_array()
            .expect("routes should be an array")
            .iter()
            .filter(|route| {
                route["effects"]
                    .as_array()
                    .is_some_and(|effects| !effects.is_empty())
            })
            .count();
        assert_eq!(
            effect_routes, case.expected_effect_routes,
            "{} effect route count",
            case.name
        );

        remove_dir_if_present(&first_out);
        remove_dir_if_present(&second_out);
    }
}

#[test]
fn compiler_mega_app_validates_contract_and_determinism() {
    let root = manifest_dir().join("../tests/dogfood/compiler-mega-app");
    let input = root.join("src/main.ts");
    let first_out = temp_output("compiler-mega-app-first");
    let second_out = temp_output("compiler-mega-app-second");
    remove_dir_if_present(&first_out);
    remove_dir_if_present(&second_out);

    let first = compile_file(&input, &first_out, &CompileOptions::default())
        .expect("compiler mega app should compile");
    let second = compile_file(&input, &second_out, &CompileOptions::default())
        .expect("compiler mega app should compile repeatedly");

    assert_eq!(first.plan.contents, second.plan.contents);
    assert_eq!(
        fs::read(first_out.join("routes.slrt")).expect("route artifact should exist"),
        fs::read(second_out.join("routes.slrt")).expect("route artifact should exist")
    );
    assert_contract_ok(&first.plan.contents, &first_out);

    let plan: Value = serde_json::from_str(&first.plan.contents).expect("plan should parse");
    assert!(
        plan["routes"]
            .as_array()
            .expect("routes should be present")
            .len()
            >= 10,
        "mega app should exercise a broad route set"
    );
    assert!(
        plan["routes"]
            .as_array()
            .expect("routes should be present")
            .iter()
            .any(|route| route["method"] == "DELETE"),
        "mega app should include DELETE"
    );
    assert!(
        plan["dependencyGraph"]["packages"]
            .as_array()
            .is_some_and(|packages| packages
                .iter()
                .any(|package| package["name"] == "validator-lite")),
        "mega app should exercise package dependency graph metadata"
    );

    remove_dir_if_present(&first_out);
    remove_dir_if_present(&second_out);
}

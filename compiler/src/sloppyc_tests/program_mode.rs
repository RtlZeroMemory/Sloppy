#[test]
fn program_mode_builds_entry_and_relative_modules_without_routes() {
    let root = std::env::temp_dir().join(format!("sloppyc-program-mode-{}", std::process::id()));
    let input = root.join("main.ts");
    let helper = root.join("message.ts");
    let out_dir = root.join(".sloppy");
    if root.exists() {
        fs::remove_dir_all(&root).expect("stale program fixture should be removable");
    }
    fs::create_dir_all(&root).expect("program fixture directory should be created");
    fs::write(&helper, "export const message = \"hello\";\n").expect("helper should write");
    fs::write(
        &input,
        "import { message } from \"./message\";\nexport function main() { return message; }\n",
    )
    .expect("entry should write");

    super::build(&input, &out_dir, &CompileOptions::new()).expect("program should build");
    let plan = fs::read_to_string(out_dir.join("app.plan.json")).expect("plan should exist");
    assert!(plan.contains("\"kind\": \"program\""));
    assert!(plan.contains("\"status\": \"opaque\""));
    assert!(plan.contains("\"handlers\": []"));
    assert!(plan.contains("\"routes\": []"));
    let app_js = fs::read_to_string(out_dir.join("app.js")).expect("bundle should exist");
    assert!(app_js.contains("__sloppy_program_main"));
    assert!(app_js.contains("__sloppy_program_require(\"message.ts\")"));
}

#[test]
fn program_mode_preserves_nested_relative_module_ids() {
    let root = fixture_temp_dir("program-nested-relative-module-ids");
    let commands = root.join("commands");
    let input = root.join("main.ts");
    let helper = commands.join("echo.ts");
    let out_dir = root.join(".sloppy");
    fs::create_dir_all(&commands).expect("commands directory should be created");
    fs::write(
        &helper,
        "export function echo(value) { return `echo:${value}`; }\n",
    )
    .expect("helper should write");
    fs::write(
        &input,
        "import { echo } from \"./commands/echo.ts\";\nexport function main() { return echo(\"ok\"); }\n",
    )
    .expect("entry should write");

    super::build(&input, &out_dir, &CompileOptions::new()).expect("program should build");
    let app_js = fs::read_to_string(out_dir.join("app.js")).expect("bundle should exist");
    assert!(app_js.contains("__sloppy_program_modules[\"commands/echo.ts\"]"));
    assert!(app_js.contains("__sloppy_program_require(\"commands/echo.ts\")"));
    assert!(!app_js.contains("__sloppy_program_require(\"echo.ts\")"));

    fs::remove_dir_all(&root).expect("program fixture directory should be removable");
}

#[test]
fn program_mode_preserves_declared_capabilities() {
    let root = std::env::temp_dir().join(format!(
        "sloppyc-program-capabilities-{}",
        std::process::id()
    ));
    let input = root.join("main.ts");
    let out_dir = root.join(".sloppy");
    if root.exists() {
        fs::remove_dir_all(&root).expect("stale program fixture should be removable");
    }
    fs::create_dir_all(&root).expect("program fixture directory should be created");
    fs::write(&input, "export function main() { return \"ok\"; }\n").expect("input should write");

    let options = CompileOptions {
        kind: Some(super::ProjectKind::Program),
        declared_capabilities: vec!["fs".to_string(), "time".to_string()],
        ..CompileOptions::default()
    };
    super::build(&input, &out_dir, &options).expect("program should build");
    let plan_text =
        fs::read_to_string(out_dir.join("app.plan.json")).expect("plan should be readable");
    let plan: serde_json::Value = serde_json::from_str(&plan_text).expect("plan should parse");
    assert_eq!(plan["kind"], "program");
    assert_eq!(plan["capabilities"][0]["token"], "fs");
    assert_eq!(plan["capabilities"][0]["kind"], "filesystem");
    assert_eq!(plan["capabilities"][0]["source"]["path"], "command-line");
    assert_eq!(plan["capabilities"][1]["token"], "time");
    assert!(plan["requiredFeatures"]
        .as_array()
        .expect("required features should be an array")
        .contains(&serde_json::json!("stdlib.fs")));
    assert!(plan["requiredFeatures"]
        .as_array()
        .expect("required features should be an array")
        .contains(&serde_json::json!("stdlib.time")));

    fs::remove_dir_all(&root).expect("program fixture directory should be removable");
}

#[test]
fn program_ffi_import_emits_plan_visible_native_metadata() {
    let root = fixture_temp_dir("program-ffi-metadata");
    let input = root.join("main.ts");
    let out_dir = root.join(".sloppy");
    fs::write(
        &input,
        r#"
import { unsafeFfi as ffi, t } from "sloppy/ffi";

const native = ffi.library("ffi-test", {
  addI32: ffi.fn(t.i32, [t.i32, t.i32], { symbol: "sloppy_ffi_add_i32", convention: "cdecl" }),
  defaulted: ffi.fn(t.ntstatus, [t.handle], { symbol: "sloppy_ffi_defaulted", callback: false }),
  winFlag: ffi.fn(t.bool32, [t.bool32], { symbol: "sloppy_ffi_win_flag" })
}, { convention: "stdcall" });
const Point = ffi.struct("Point", { x: t.i32, y: t.i32 }, { layout: "sequential", pack: 4 });

export default function main() {
  void Point;
  return native.addI32(1, 2);
}
"#,
    )
    .expect("program fixture should be writable");

    super::build(&input, &out_dir, &CompileOptions::new()).expect("FFI program should build");
    let plan_text =
        fs::read_to_string(out_dir.join("app.plan.json")).expect("plan should be readable");
    let plan: serde_json::Value = serde_json::from_str(&plan_text).expect("plan should parse");
    assert_eq!(plan["kind"], "program");
    assert!(plan["requiredFeatures"]
        .as_array()
        .expect("required features should be an array")
        .iter()
        .any(|feature| feature == "stdlib.ffi"));
    assert_eq!(plan["capabilities"][0]["token"], "ffi");
    assert_eq!(plan["capabilities"][0]["kind"], "ffi");
    assert_eq!(plan["native"]["ffi"][0]["name"], "ffi-test");
    assert_eq!(plan["native"]["ffi"][0]["convention"], "stdcall");
    assert_eq!(plan["native"]["ffi"][0]["source"]["path"], "main.ts");
    assert!(
        plan["native"]["ffi"][0]["source"]["line"]
            .as_u64()
            .expect("FFI source line should be numeric")
            > 0
    );
    assert_eq!(
        plan["native"]["ffi"][0]["functions"][0]["id"],
        "ffi:ffi-test:addI32"
    );
    assert_eq!(
        plan["native"]["ffi"][0]["functions"][0]["convention"],
        "cdecl"
    );
    assert_eq!(plan["native"]["ffi"][0]["functions"][0]["return"], "i32");
    assert_eq!(
        plan["native"]["ffi"][0]["functions"][0]["parameters"],
        serde_json::json!(["i32", "i32"])
    );
    assert_eq!(
        plan["native"]["ffi"][0]["functions"][0]["marshaling"]["arguments"],
        "direct"
    );
    assert_eq!(plan["native"]["ffi"][0]["functions"][0]["safety"], "unsafe");
    assert_eq!(
        plan["native"]["ffi"][0]["functions"][1]["convention"],
        "stdcall"
    );
    assert_eq!(
        plan["native"]["ffi"][0]["functions"][1]["return"],
        "ntstatus"
    );
    assert_eq!(
        plan["native"]["ffi"][0]["functions"][1]["parameters"],
        serde_json::json!(["handle"])
    );
    assert_eq!(plan["native"]["ffi"][0]["functions"][2]["return"], "bool32");
    assert_eq!(
        plan["native"]["ffi"][0]["functions"][2]["parameters"],
        serde_json::json!(["bool32"])
    );
    assert_eq!(
        plan["native"]["ffi"][0]["functions"][0]["source"]["path"],
        "main.ts"
    );
    assert_eq!(plan["native"]["ffiStructs"][0]["name"], "Point");
    assert_eq!(plan["native"]["ffiStructs"][0]["layout"], "sequential");
    assert_eq!(plan["native"]["ffiStructs"][0]["pack"], 4);

    fs::remove_dir_all(&root).expect("program fixture directory should be removable");
}

#[test]
fn program_ffi_exported_declarations_emit_native_metadata() {
    let root = fixture_temp_dir("program-ffi-exported-metadata");
    let input = root.join("main.ts");
    let out_dir = root.join(".sloppy");
    fs::write(
        &input,
        r#"
import { unsafeFfi, t } from "sloppy/ffi";

export const native = unsafeFfi.library("ffi-test", {
  addI32: unsafeFfi.fn(t.i32, [t.i32, t.i32])
});

export default function main() {
  return native.addI32(1, 2);
}
"#,
    )
    .expect("program fixture should be writable");

    super::build(&input, &out_dir, &CompileOptions::new())
        .expect("exported FFI declaration should build");
    let plan_text =
        fs::read_to_string(out_dir.join("app.plan.json")).expect("plan should be readable");
    let plan: serde_json::Value = serde_json::from_str(&plan_text).expect("plan should parse");
    assert_eq!(plan["native"]["ffi"][0]["name"], "ffi-test");
    assert_eq!(plan["native"]["ffi"][0]["functions"][0]["name"], "addI32");

    fs::remove_dir_all(&root).expect("program fixture directory should be removable");
}

#[test]
fn program_ffi_function_specs_must_be_static() {
    let root = fixture_temp_dir("program-ffi-dynamic-spec");
    let input = root.join("main.ts");
    let out_dir = root.join(".sloppy");
    fs::write(
        &input,
        r#"
import { unsafeFfi as ffi, t } from "sloppy/ffi";

const spec = {
  addI32: ffi.fn(t.i32, [t.i32, t.i32])
};
const native = ffi.library("ffi-test", spec);

export default function main() {
  return native.addI32(1, 2);
}
"#,
    )
    .expect("program fixture should be writable");

    let error = super::build(&input, &out_dir, &CompileOptions::new())
        .expect_err("dynamic FFI specs should fail");
    assert_eq!(error.diagnostic.code, "SLOPPYC_E_FFI_DYNAMIC_DECLARATION");

    fs::remove_dir_all(&root).expect("program fixture directory should be removable");
}

#[test]
fn program_ffi_types_must_use_static_t_namespace() {
    let root = fixture_temp_dir("program-ffi-invalid-type");
    let input = root.join("main.ts");
    let out_dir = root.join(".sloppy");
    fs::write(
        &input,
        r#"
import { unsafeFfi as ffi, t } from "sloppy/ffi";

const int32 = t.i32;
const native = ffi.library("ffi-test", {
  addI32: ffi.fn(int32, [t.i32, t.i32])
});

export default function main() {
  return native.addI32(1, 2);
}
"#,
    )
    .expect("program fixture should be writable");

    let error = super::build(&input, &out_dir, &CompileOptions::new())
        .expect_err("non-static FFI type aliases should fail");
    assert_eq!(error.diagnostic.code, "SLOPPYC_E_FFI_INVALID_TYPE");

    fs::remove_dir_all(&root).expect("program fixture directory should be removable");
}

#[test]
fn program_ffi_rejects_malformed_options() {
    for (case, source) in [
        (
            "library-options",
            r#"
import { unsafeFfi as ffi, t } from "sloppy/ffi";
const native = ffi.library("ffi-test", { addI32: ffi.fn(t.i32, [t.i32]) }, { convention: false });
export default function main() { return native.addI32(1); }
"#,
        ),
        (
            "function-options",
            r#"
import { unsafeFfi as ffi, t } from "sloppy/ffi";
const native = ffi.library("ffi-test", { addI32: ffi.fn(t.i32, [t.i32], { symbol: 42 }) });
export default function main() { return native.addI32(1); }
"#,
        ),
        (
            "boolean-options",
            r#"
import { unsafeFfi as ffi, t } from "sloppy/ffi";
const native = ffi.library("ffi-test", { addI32: ffi.fn(t.i32, [t.i32], { callback: "no" }) });
export default function main() { return native.addI32(1); }
"#,
        ),
    ] {
        let root = fixture_temp_dir(&format!("program-ffi-malformed-options-{case}"));
        let input = root.join("main.ts");
        let out_dir = root.join(".sloppy");
        fs::write(&input, source).expect("program fixture should be writable");

        let error = super::build(&input, &out_dir, &CompileOptions::new())
            .expect_err("malformed FFI options should fail");
        assert_eq!(error.diagnostic.code, "SLOPPYC_E_FFI_DYNAMIC_DECLARATION");

        fs::remove_dir_all(&root).expect("program fixture directory should be removable");
    }
}

#[test]
fn program_ffi_rejects_unknown_calling_conventions() {
    let root = fixture_temp_dir("program-ffi-invalid-convention");
    let input = root.join("main.ts");
    let out_dir = root.join(".sloppy");
    fs::write(
        &input,
        r#"
import { unsafeFfi as ffi, t } from "sloppy/ffi";

const native = ffi.library("ffi-test", {
  addI32: ffi.fn(t.i32, [t.i32, t.i32])
}, { convention: "fastcall" });

export default function main() {
  return native.addI32(1, 2);
}
"#,
    )
    .expect("program fixture should be writable");

    let error = super::build(&input, &out_dir, &CompileOptions::new())
        .expect_err("unsupported FFI calling conventions should fail");
    assert_eq!(
        error.diagnostic.code,
        "SLOPPYC_E_FFI_UNSUPPORTED_CALLING_CONVENTION"
    );

    fs::remove_dir_all(&root).expect("program fixture directory should be removable");
}

#[test]
fn program_ffi_library_declarations_must_be_static() {
    let root = fixture_temp_dir("program-ffi-dynamic-library");
    let input = root.join("main.ts");
    let out_dir = root.join(".sloppy");
    fs::write(
        &input,
        r#"
import { unsafeFfi as ffi, t } from "sloppy/ffi";

const name = "ffi-test";
const native = ffi.library(name, {
  addI32: ffi.fn(t.i32, [t.i32, t.i32])
});

export default function main() {
  return native.addI32(1, 2);
}
"#,
    )
    .expect("program fixture should be writable");

    let error = super::build(&input, &out_dir, &CompileOptions::new())
        .expect_err("dynamic FFI declarations should fail");
    assert_eq!(error.diagnostic.code, "SLOPPYC_E_FFI_DYNAMIC_DECLARATION");

    fs::remove_dir_all(&root).expect("program fixture directory should be removable");
}

#[test]
fn program_ffi_rejects_unsupported_return_buffers() {
    let root = fixture_temp_dir("program-ffi-unsupported-return");
    let input = root.join("main.ts");
    let out_dir = root.join(".sloppy");
    fs::write(
        &input,
        r#"
import { unsafeFfi as ffi, t } from "sloppy/ffi";

const native = ffi.library("ffi-test", {
  getText: ffi.fn(t.cstring, [])
});

export default function main() {
  return native.getText();
}
"#,
    )
    .expect("program fixture should be writable");

    let error = super::build(&input, &out_dir, &CompileOptions::new())
        .expect_err("unsupported FFI return buffers should fail");
    assert_eq!(error.diagnostic.code, "SLOPPYC_E_FFI_UNSUPPORTED_TYPE");

    fs::remove_dir_all(&root).expect("program fixture directory should be removable");
}

#[test]
fn program_ffi_rejects_void_parameters() {
    let root = fixture_temp_dir("program-ffi-void-parameter");
    let input = root.join("main.ts");
    let out_dir = root.join(".sloppy");
    fs::write(
        &input,
        r#"
import { unsafeFfi as ffi, t } from "sloppy/ffi";

const native = ffi.library("ffi-test", {
  bad: ffi.fn(t.void, [t.void])
});

export default function main() {
  return native.bad();
}
"#,
    )
    .expect("program fixture should be writable");

    let error = super::build(&input, &out_dir, &CompileOptions::new())
        .expect_err("void FFI parameters should fail");
    assert_eq!(error.diagnostic.code, "SLOPPYC_E_FFI_UNSUPPORTED_TYPE");

    fs::remove_dir_all(&root).expect("program fixture directory should be removable");
}

#[test]
fn program_ffi_rejects_callbacks() {
    let root = fixture_temp_dir("program-ffi-callback");
    let input = root.join("main.ts");
    let out_dir = root.join(".sloppy");
    fs::write(
        &input,
        r#"
import { unsafeFfi as ffi, t } from "sloppy/ffi";

const native = ffi.library("ffi-test", {
  register: ffi.fn(t.void, [t.ptr], { callback: true })
});

export default function main() {
  return native.register(null);
}
"#,
    )
    .expect("program fixture should be writable");

    let error =
        super::build(&input, &out_dir, &CompileOptions::new()).expect_err("callbacks should fail");
    assert_eq!(error.diagnostic.code, "SLOPPYC_E_FFI_UNSUPPORTED_CALLBACK");

    fs::remove_dir_all(&root).expect("program fixture directory should be removable");
}

#[test]
fn program_ffi_rejects_variadic_functions() {
    let root = fixture_temp_dir("program-ffi-variadic");
    let input = root.join("main.ts");
    let out_dir = root.join(".sloppy");
    fs::write(
        &input,
        r#"
import { unsafeFfi as ffi, t } from "sloppy/ffi";

const native = ffi.library("ffi-test", {
  printf: ffi.fn(t.i32, [t.cstring], { variadic: true })
});

export default function main() {
  return native.printf("ok");
}
"#,
    )
    .expect("program fixture should be writable");

    let error = super::build(&input, &out_dir, &CompileOptions::new())
        .expect_err("variadic functions should fail");
    assert_eq!(error.diagnostic.code, "SLOPPYC_E_FFI_UNSUPPORTED_VARIADIC");

    fs::remove_dir_all(&root).expect("program fixture directory should be removable");
}

#[test]
fn program_ffi_rejects_struct_by_value_function_types() {
    let root = fixture_temp_dir("program-ffi-struct-by-value");
    let input = root.join("main.ts");
    let out_dir = root.join(".sloppy");
    fs::write(
        &input,
        r#"
import { unsafeFfi as ffi, t } from "sloppy/ffi";

const Point = ffi.struct("Point", { x: t.i32, y: t.i32 });
const native = ffi.library("ffi-test", {
  byValue: ffi.fn(t.i32, [Point])
});

export default function main() {
  void Point;
  return native.byValue({ x: 1, y: 2 });
}
"#,
    )
    .expect("program fixture should be writable");

    let error = super::build(&input, &out_dir, &CompileOptions::new())
        .expect_err("struct-by-value function types should fail");
    assert_eq!(error.diagnostic.code, "SLOPPYC_E_FFI_INVALID_TYPE");

    fs::remove_dir_all(&root).expect("program fixture directory should be removable");
}

#[test]
fn program_ffi_rejects_unsized_struct_fields() {
    let root = fixture_temp_dir("program-ffi-unsized-struct-field");
    let input = root.join("main.ts");
    let out_dir = root.join(".sloppy");
    fs::write(
        &input,
        r#"
import { unsafeFfi as ffi, t } from "sloppy/ffi";

const Bad = ffi.struct("Bad", { name: t.cstring });

export default function main() {
  void Bad;
  return "ok";
}
"#,
    )
    .expect("program fixture should be writable");

    let error = super::build(&input, &out_dir, &CompileOptions::new())
        .expect_err("unsized struct fields should fail");
    assert_eq!(
        error.diagnostic.code,
        "SLOPPYC_E_FFI_UNSUPPORTED_STRUCT_BY_VALUE"
    );

    fs::remove_dir_all(&root).expect("program fixture directory should be removable");
}

#[test]
fn sloppy_json_declared_program_capabilities_keep_sloppy_json_provenance() {
    let root = std::env::temp_dir().join(format!(
        "sloppyc-program-sloppy-json-capabilities-{}",
        std::process::id()
    ));
    let input = root.join("main.ts");
    let out_dir = root.join(".sloppy");
    if root.exists() {
        fs::remove_dir_all(&root).expect("stale program fixture should be removable");
    }
    fs::create_dir_all(&root).expect("program fixture directory should be created");
    fs::write(&input, "export function main() { return \"ok\"; }\n").expect("input should write");

    let options = CompileOptions {
        kind: Some(super::ProjectKind::Program),
        declared_capabilities: vec!["fs".to_string()],
        declared_capabilities_from_sloppy_json: true,
        ..CompileOptions::default()
    };
    super::build(&input, &out_dir, &options).expect("program should build");
    let plan_text =
        fs::read_to_string(out_dir.join("app.plan.json")).expect("plan should be readable");
    let plan: serde_json::Value = serde_json::from_str(&plan_text).expect("plan should parse");
    assert_eq!(plan["capabilities"][0]["source"]["path"], "sloppy.json");
    assert_eq!(plan["capabilities"][0]["source"]["line"], 1);

    fs::remove_dir_all(&root).expect("program fixture directory should be removable");
}

#[test]
fn inferred_program_mode_preserves_timing_metrics() {
    let root = std::env::temp_dir().join(format!("sloppyc-program-timings-{}", std::process::id()));
    let input = root.join("main.ts");
    let out_dir = root.join(".sloppy");
    let timings = root.join("timings.json");
    if root.exists() {
        fs::remove_dir_all(&root).expect("stale program fixture should be removable");
    }
    fs::create_dir_all(&root).expect("program fixture directory should be created");
    fs::write(&input, "type User = { name: string };\nconst user: User = { name: \"Ada\" };\nconsole.log(user.name);\n")
        .expect("input should write");

    let options = CompileOptions {
        timings_json: Some(timings.clone()),
        ..CompileOptions::default()
    };
    super::build(&input, &out_dir, &options).expect("inferred program should build");
    let timings_text = fs::read_to_string(&timings).expect("timings JSON should exist");
    let timings_json: serde_json::Value =
        serde_json::from_str(&timings_text).expect("timings JSON should parse");
    assert!(timings_json["phases"]["parseEntryMs"].is_number());
    assert!(timings_json["phases"]["extractMs"].is_number());

    fs::remove_dir_all(&root).expect("program fixture directory should be removable");
}

#[test]
fn program_mode_supports_re_export_declarations() {
    let root =
        std::env::temp_dir().join(format!("sloppyc-program-reexport-{}", std::process::id()));
    let input = root.join("main.ts");
    let dep = root.join("dep.ts");
    let out_dir = root.join(".sloppy");
    if root.exists() {
        fs::remove_dir_all(&root).expect("stale program fixture should be removable");
    }
    fs::create_dir_all(&root).expect("program fixture directory should be created");
    fs::write(
        &input,
        r#"export { value as renamed, "string-name" as "external-name" } from "./dep";
export * from "./dep";
export * as namespace from "./dep";
export { type MissingType } from "./types-only";
"#,
    )
    .expect("input should write");
    fs::write(
        &dep,
        r#"export const value = 1;
const local = 2;
export { local as "string-name" };
export default 3;
"#,
    )
    .expect("dep should write");

    super::build(&input, &out_dir, &CompileOptions::new()).expect("re-export should build");
    let app_js = fs::read_to_string(out_dir.join("app.js")).expect("bundle should exist");
    assert!(app_js.contains("__sloppy_program_require(\"dep.ts\")"));
    assert!(app_js.contains("exports.renamed = __sloppy_reexport_"));
    assert!(app_js.contains("exports[\"external-name\"] = __sloppy_reexport_"));
    assert!(app_js.contains("exports[__sloppy_key] = __sloppy_reexport_"));
    assert!(app_js.contains("exports.namespace = __sloppy_program_require(\"dep.ts\");"));
    let graph_text =
        fs::read_to_string(out_dir.join("deps.graph.json")).expect("dependency graph should emit");
    let graph: serde_json::Value =
        serde_json::from_str(&graph_text).expect("dependency graph should parse");
    let entry = graph["modules"]
        .as_array()
        .expect("modules should be an array")
        .iter()
        .find(|module| module["id"] == "main.ts")
        .expect("entry module should be recorded");
    let resolved_imports = entry["resolvedImports"]
        .as_array()
        .expect("resolved imports should be an array");
    assert!(resolved_imports
        .iter()
        .any(|import| import["specifier"] == "./dep"
            && import["resolvedId"] == "dep.ts"
            && import["kind"] == "relative"));
    assert!(!resolved_imports
        .iter()
        .any(|import| import["specifier"] == "./types-only"));

    fs::remove_dir_all(&root).expect("program fixture directory should be removable");
}

#[test]
fn program_mode_supports_sqlite_provider_imports() {
    let root = std::env::temp_dir().join(format!(
        "sloppyc-program-provider-import-{}",
        std::process::id()
    ));
    let input = root.join("main.ts");
    let out_dir = root.join(".sloppy");
    if root.exists() {
        fs::remove_dir_all(&root).expect("stale program fixture should be removable");
    }
    fs::create_dir_all(&root).expect("program fixture directory should be created");
    fs::write(
        &input,
        r#"import { sqlite } from "sloppy/providers/sqlite";
export function main() {
  const provider = sqlite("main", { database: "local.db" });
  console.log(provider.kind, provider.token, provider.options.database);
}
"#,
    )
    .expect("input should write");

    super::build(&input, &out_dir, &CompileOptions::new())
        .expect("sqlite provider import should build in program mode");
    let app_js = fs::read_to_string(out_dir.join("app.js")).expect("bundle should exist");
    assert!(app_js.contains("__sloppy_program_modules[\"sloppy/providers/sqlite\"]"));
    assert!(app_js.contains("__sloppy_program_require(\"sloppy/providers/sqlite\")"));
    assert!(app_js.contains("module.exports={sqlite,Sqlite:sqlite,default:null}"));

    fs::remove_dir_all(&root).expect("program fixture directory should be removable");
}

#[test]
fn program_plan_shape_rejects_web_routes_and_handlers() {
    let path = Path::new("app.ts");
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.get("/", () => Results.text("ok"));
export default app;
"#;
    let mut app = extract(path, source).expect("web app should extract");
    app.kind = super::ProjectKind::Program;

    let error = super::emit_plan(&app, "bundle-hash", "map-hash")
        .expect_err("program plan with web routes should fail");
    assert_eq!(error.code, "SLOPPYC_E_PROGRAM_PLAN_SHAPE");
}

#[test]
fn explicit_program_kind_allows_sources_that_import_sloppy_name() {
    let root =
        std::env::temp_dir().join(format!("sloppyc-explicit-program-{}", std::process::id()));
    let input = root.join("main.ts");
    let out_dir = root.join(".sloppy");
    if root.exists() {
        fs::remove_dir_all(&root).expect("stale program fixture should be removable");
    }
    fs::create_dir_all(&root).expect("program fixture directory should be created");
    fs::write(
        &input,
        "import { Sloppy } from \"sloppy\";\nexport async function main() { return typeof Sloppy; }\n",
    )
    .expect("entry should write");
    let options = CompileOptions {
        kind: Some(super::ProjectKind::Program),
        ..CompileOptions::default()
    };

    super::build(&input, &out_dir, &options).expect("explicit program should build");
    let plan = fs::read_to_string(out_dir.join("app.plan.json")).expect("plan should exist");
    assert!(plan.contains("\"kind\": \"program\""));
    assert!(plan.contains("\"stdlib.time\""));
    assert!(plan.contains("\"stdlib.fs\""));
}

#[test]
fn inferred_source_with_sloppy_web_import_and_no_web_shape_is_ambiguous() {
    let root =
        std::env::temp_dir().join(format!("sloppyc-ambiguous-program-{}", std::process::id()));
    let input = root.join("main.ts");
    let out_dir = root.join(".sloppy");
    if root.exists() {
        fs::remove_dir_all(&root).expect("stale program fixture should be removable");
    }
    fs::create_dir_all(&root).expect("program fixture directory should be created");
    fs::write(
        &input,
        "import { Sloppy } from \"sloppy\";\nexport function main() { return Sloppy; }\n",
    )
    .expect("entry should write");

    let failure = super::build(&input, &out_dir, &CompileOptions::new())
        .expect_err("ambiguous source should fail");
    assert_eq!(failure.diagnostic.code, "SLOPPYC_E_AMBIGUOUS_SOURCE_KIND");
    assert!(failure
        .diagnostic
        .message
        .contains("This source imports Sloppy but does not export a supported web app shape."));
}

#[test]
fn ast_kind_inference_ignores_fake_sloppy_imports_in_comments_and_strings() {
    let root = fixture_temp_dir("program-fake-sloppy-imports");
    let input = root.join("main.ts");
    let out_dir = root.join(".sloppy");
    fs::write(
        &input,
        r#"// import { Sloppy } from "sloppy";
const text = "from \"sloppy\"";
export function main() { return text; }
"#,
    )
    .expect("entry should write");

    super::build(&input, &out_dir, &CompileOptions::new()).expect("plain program should build");
    let plan_text =
        fs::read_to_string(out_dir.join("app.plan.json")).expect("plan should be readable");
    let plan: serde_json::Value = serde_json::from_str(&plan_text).expect("plan should parse");
    assert_eq!(plan["kind"], "program");

    fs::remove_dir_all(&root).expect("program fixture directory should be removable");
}

#[test]
fn ast_kind_inference_detects_web_app_shape() {
    let root = fixture_temp_dir("program-web-inference");
    let input = root.join("app.ts");
    let out_dir = root.join(".sloppy");
    fs::write(
        &input,
        r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.get("/", () => Results.text("ok"));
export default app;
"#,
    )
    .expect("entry should write");

    super::build(&input, &out_dir, &CompileOptions::new()).expect("web app should build");
    let plan_text =
        fs::read_to_string(out_dir.join("app.plan.json")).expect("plan should be readable");
    let plan: serde_json::Value = serde_json::from_str(&plan_text).expect("plan should parse");
    assert_eq!(plan["kind"], "web");
    assert_eq!(
        plan["routes"]
            .as_array()
            .expect("routes should exist")
            .len(),
        1
    );

    fs::remove_dir_all(&root).expect("program fixture directory should be removable");
}

#[test]
fn program_mode_supports_multiline_imports_types_and_default_main() {
    let root = fixture_temp_dir("program-multiline-types-default");
    let input = root.join("main.ts");
    let helper = root.join("value.ts");
    let out_dir = root.join(".sloppy");
    fs::write(&helper, "export const value: string = \"Ada\";\n").expect("helper should write");
    fs::write(
        &input,
        r#"import {
  File,
  Directory
} from "sloppy/fs";
import type { Process } from "sloppy/os";
import { value } from "./value";

type User = { name: string };
const user: User = { name: value };

export default async function main(args: string[]) {
  console.log(File, Directory, args);
  return user.name;
}
"#,
    )
    .expect("entry should write");

    super::build(&input, &out_dir, &CompileOptions::new()).expect("program should build");
    let app_js = fs::read_to_string(out_dir.join("app.js")).expect("app should exist");
    assert!(app_js.contains("const { File, Directory } = globalThis.__sloppy_runtime;"));
    assert!(app_js.contains("const { value } = __sloppy_program_require(\"value.ts\");"));
    assert!(app_js.contains("const user = {"));
    assert!(app_js.contains("exports.default = async function main(args)"));
    assert!(!app_js.contains("import type"));
    assert!(!app_js.contains(": string"));
    let plan_text =
        fs::read_to_string(out_dir.join("app.plan.json")).expect("plan should be readable");
    let plan: serde_json::Value = serde_json::from_str(&plan_text).expect("plan should parse");
    assert_eq!(plan["kind"], "program");
    assert!(plan["requiredFeatures"]
        .as_array()
        .expect("required features should be an array")
        .contains(&serde_json::json!("stdlib.fs")));
    assert!(!plan["requiredFeatures"]
        .as_array()
        .expect("required features should be an array")
        .contains(&serde_json::json!("stdlib.os")));

    fs::remove_dir_all(&root).expect("program fixture directory should be removable");
}

#[test]
fn program_mode_named_main_takes_precedence_over_default_export() {
    let root = fixture_temp_dir("program-main-precedence");
    let input = root.join("main.ts");
    let out_dir = root.join(".sloppy");
    fs::write(
        &input,
        r#"export default async function main() { return "default"; }
export async function main() { return "named"; }
"#,
    )
    .expect("entry should write");

    super::build(&input, &out_dir, &CompileOptions::new()).expect("program should build");
    let app_js = fs::read_to_string(out_dir.join("app.js")).expect("app should exist");
    assert!(app_js.contains("exports.main = main;"));
    assert!(app_js.contains("typeof entry.main === \"function\""));
    assert!(
        app_js.find("entry.main").expect("named check should exist")
            < app_js
                .find("entry.default")
                .expect("default check should exist")
    );

    fs::remove_dir_all(&root).expect("program fixture directory should be removable");
}

#[test]
fn program_mode_ignores_non_function_default_entrypoint() {
    let root = fixture_temp_dir("program-default-non-function");
    let input = root.join("main.ts");
    let out_dir = root.join(".sloppy");
    fs::write(
        &input,
        r#"const value = { message: "not an entrypoint" };
export default value;
"#,
    )
    .expect("entry should write");

    super::build(&input, &out_dir, &CompileOptions::new()).expect("program should build");
    let app_js = fs::read_to_string(out_dir.join("app.js")).expect("app should exist");
    assert!(app_js.contains("exports.default = value;"));
    assert!(!app_js.contains("return entry.default;"));

    fs::remove_dir_all(&root).expect("program fixture directory should be removable");
}

#[test]
fn program_mode_reports_missing_package_with_install_hint() {
    let root = fixture_temp_dir("program-missing-package");
    let input = root.join("main.ts");
    let out_dir = root.join(".sloppy");
    fs::write(
        &input,
        "import dependency from \"__sloppy_missing_pkg_for_test__\";\nexport function main() { return dependency; }\n",
    )
    .expect("entry should write");

    let failure =
        super::build(&input, &out_dir, &CompileOptions::new()).expect_err("missing package fails");
    assert_eq!(failure.diagnostic.code, "SLOPPYC_E_PACKAGE_NOT_FOUND");
    assert!(failure
        .diagnostic
        .hint
        .as_deref()
        .unwrap_or_default()
        .contains("npm install __sloppy_missing_pkg_for_test__"));

    fs::remove_dir_all(&root).expect("program fixture directory should be removable");
}

#[test]
fn program_mode_resolves_installed_package_and_emits_dependency_graph() {
    let root = fixture_temp_dir("program-installed-package");
    let package_dir = root.join("node_modules").join("tiny-pkg");
    let input = root.join("main.ts");
    let out_dir = root.join(".sloppy");
    fs::create_dir_all(&package_dir).expect("package directory should be created");
    fs::write(
        package_dir.join("package.json"),
        r#"{"name":"tiny-pkg","version":"1.2.3","type":"module","exports":"./index.js"}"#,
    )
    .expect("package.json should write");
    fs::write(
        package_dir.join("index.js"),
        r#"export const value = "from-package"; export default value;"#,
    )
    .expect("package entry should write");
    fs::write(
        &input,
        r#"import value from "tiny-pkg"; export function main() { return value; }"#,
    )
    .expect("entry should write");

    super::build(&input, &out_dir, &CompileOptions::new()).expect("program should build");
    let graph_text =
        fs::read_to_string(out_dir.join("deps.graph.json")).expect("dependency graph should emit");
    let graph: serde_json::Value =
        serde_json::from_str(&graph_text).expect("dependency graph should parse");
    assert!(graph["packages"]
        .as_array()
        .expect("packages should be an array")
        .iter()
        .any(|package| package["name"] == "tiny-pkg" && package["version"] == "1.2.3"));
    assert!(graph["modules"]
        .as_array()
        .expect("modules should be an array")
        .iter()
        .any(|module| module["id"] == "node_modules/tiny-pkg/index.js"));
    let app_js = fs::read_to_string(out_dir.join("app.js")).expect("app.js should emit");
    assert!(app_js.contains("exports.value = value;"));
    assert!(app_js.contains("exports.default = value;"));
    assert!(!app_js.contains("export const value"));

    fs::remove_dir_all(&root).expect("program fixture directory should be removable");
}

#[test]
fn program_mode_resolves_node_compat_shim_and_plan_features() {
    let root = fixture_temp_dir("program-node-compat-path");
    let input = root.join("main.ts");
    let out_dir = root.join(".sloppy");
    fs::write(
        &input,
        r#"import path from "node:path"; export function main() { return path.join("a", "b"); }"#,
    )
    .expect("entry should write");

    super::build(&input, &out_dir, &CompileOptions::new()).expect("program should build");
    let plan_text =
        fs::read_to_string(out_dir.join("app.plan.json")).expect("plan should be readable");
    let plan: serde_json::Value = serde_json::from_str(&plan_text).expect("plan should parse");
    assert!(plan["requiredFeatures"]
        .as_array()
        .expect("requiredFeatures should be an array")
        .contains(&serde_json::json!("node.compat.path")));
    assert!(plan["dependencyGraph"]["nodeBuiltins"]
        .as_array()
        .expect("node builtins should be an array")
        .iter()
        .any(|builtin| builtin["specifier"] == "node:path"
            && builtin["backing"] == "sloppy/node/path"));

    fs::remove_dir_all(&root).expect("program fixture directory should be removable");
}

#[test]
fn node_buffer_shim_uses_multibyte_utf8_decode_fallback() {
    assert!(super::NODE_BUFFER_SHIM.contains("rt.Text.utf8.decode"));
    assert!(super::NODE_BUFFER_SHIM.contains("Buffer.concat"));
    assert!(super::NODE_BUFFER_SHIM.contains("rt.Base64.decode"));
}

#[test]
fn program_mode_resolves_expanded_node_compat_shims() {
    let root = fixture_temp_dir("program-node-compat-expanded");
    let input = root.join("main.ts");
    let out_dir = root.join(".sloppy");
    fs::write(
        &input,
        r#"import assert from "node:assert";
import strictAssert from "node:assert/strict";
import process from "node:process";
import { Buffer } from "node:buffer";
import { Readable } from "node:stream";
import { pipeline } from "node:stream/promises";
import crypto from "node:crypto";
export function main() {
  assert(true);
  strictAssert(true);
  assert.ok(Buffer.isBuffer(Buffer.concat([Buffer.from("a"), Buffer.from("b")])));
  assert.throws(() => strictAssert.equal(1, "1"), assert.AssertionError);
  return `${process.platform}:${typeof Readable.from}:${typeof pipeline}:${typeof crypto.createHash}`;
}"#,
    )
    .expect("entry should write");

    super::build(&input, &out_dir, &CompileOptions::new()).expect("program should build");
    let plan_text =
        fs::read_to_string(out_dir.join("app.plan.json")).expect("plan should be readable");
    let plan: serde_json::Value = serde_json::from_str(&plan_text).expect("plan should parse");
    let required = plan["requiredFeatures"]
        .as_array()
        .expect("requiredFeatures should be an array");
    for feature in [
        "node.compat.assert",
        "node.compat.buffer",
        "node.compat.crypto",
        "node.compat.process",
        "node.compat.stream",
        "stdlib.crypto",
        "stdlib.os",
    ] {
        assert!(
            required.contains(&serde_json::json!(feature)),
            "missing required feature {feature}: {required:?}"
        );
    }
    let app_js = fs::read_to_string(out_dir.join("app.js")).expect("app.js should emit");
    for module_id in [
        "sloppy/node/assert",
        "sloppy/node/assert/strict",
        "sloppy/node/buffer",
        "sloppy/node/crypto",
        "sloppy/node/events",
        "sloppy/node/process",
        "sloppy/node/stream",
        "sloppy/node/stream/promises",
    ] {
        assert!(
            app_js.contains(module_id),
            "missing embedded shim {module_id}"
        );
    }

    fs::remove_dir_all(&root).expect("program fixture directory should be removable");
}

#[test]
fn program_mode_dedupes_node_compat_required_features() {
    let root = fixture_temp_dir("program-node-compat-dedupe");
    let input = root.join("main.ts");
    let out_dir = root.join(".sloppy");
    fs::write(
        &input,
        r#"import path from "node:path";
import fs from "node:fs";
import fsAgain from "node:fs";
import fsPromises from "node:fs/promises";
export function main() {
  return `${path.join("a", "b")}:${typeof fs}:${typeof fsAgain}:${typeof fsPromises}`;
}"#,
    )
    .expect("entry should write");

    super::build(&input, &out_dir, &CompileOptions::new()).expect("program should build");
    let plan_text =
        fs::read_to_string(out_dir.join("app.plan.json")).expect("plan should be readable");
    let plan: serde_json::Value = serde_json::from_str(&plan_text).expect("plan should parse");
    let required = plan["requiredFeatures"]
        .as_array()
        .expect("requiredFeatures should be an array");
    assert_eq!(
        required
            .iter()
            .filter(|feature| **feature == serde_json::json!("node.compat.fs"))
            .count(),
        1
    );
    assert_eq!(
        required
            .iter()
            .filter(|feature| **feature == serde_json::json!("node.compat.fs.promises"))
            .count(),
        1
    );
    assert_eq!(
        required
            .iter()
            .filter(|feature| **feature == serde_json::json!("node.compat.path"))
            .count(),
        1
    );

    fs::remove_dir_all(&root).expect("program fixture directory should be removable");
}

#[test]
fn program_mode_json_modules_preserve_default_export_binding() {
    let root = fixture_temp_dir("program-json-default");
    let input = root.join("main.ts");
    let out_dir = root.join(".sloppy");
    fs::write(
        root.join("data.json"),
        r#"{"message":"json-default","default":"shadow"}"#,
    )
    .expect("JSON fixture should write");
    fs::write(
        &input,
        r#"import data from "./data.json"; export function main() { return data.message; }"#,
    )
    .expect("entry should write");

    super::build(&input, &out_dir, &CompileOptions::new()).expect("program should build");
    let app_js = fs::read_to_string(out_dir.join("app.js")).expect("app.js should emit");
    assert!(app_js.contains("module.exports = {"));
    assert!(app_js.contains("\"default\":\"shadow\""));
    assert!(app_js.contains("\"message\":\"json-default\""));
    assert!(!app_js.contains("module.exports.default = __sloppy_json_module;"));
    assert!(app_js.contains("__sloppy_program_require(\"data.json\")"));

    fs::remove_dir_all(&root).expect("program fixture directory should be removable");
}

#[test]
fn program_mode_commonjs_modules_receive_wrapper_bindings() {
    let root = fixture_temp_dir("program-commonjs-wrapper");
    let input = root.join("main.ts");
    let out_dir = root.join(".sloppy");
    fs::write(
        root.join("helper.cjs"),
        r#"const suffix = require("./suffix.cjs"); module.exports = `${__dirname}/${__filename}/${suffix}`;"#,
    )
    .expect("CommonJS helper should write");
    fs::write(root.join("suffix.cjs"), r#"module.exports = "ok";"#)
        .expect("CommonJS suffix should write");
    fs::write(
        &input,
        r#"import helper from "./helper.cjs"; export function main() { return helper; }"#,
    )
    .expect("entry should write");

    super::build(&input, &out_dir, &CompileOptions::new()).expect("program should build");
    let app_js = fs::read_to_string(out_dir.join("app.js")).expect("app.js should emit");
    assert!(app_js.contains("function(exports, module, require, __filename, __dirname)"));
    assert!(app_js.contains(
        "const localRequire = function(specifier) { return __sloppy_program_require_from(id, specifier); };"
    ));
    assert!(app_js.contains(
        "localRequire.resolve = function(specifier) { return __sloppy_program_resolve(id, specifier); };"
    ));
    assert!(app_js.contains(
        "factory(module.exports, module, localRequire, id, __sloppy_program_dirname(id));"
    ));

    fs::remove_dir_all(&root).expect("program fixture directory should be removable");
}

#[test]
fn program_mode_commonjs_requires_use_require_export_condition() {
    let root = fixture_temp_dir("program-commonjs-require-condition");
    let package_dir = root.join("node_modules").join("dual-pkg");
    let input = root.join("main.ts");
    let out_dir = root.join(".sloppy");
    fs::create_dir_all(&package_dir).expect("package directory should be created");
    fs::write(
        package_dir.join("package.json"),
        r#"{"name":"dual-pkg","version":"1.0.0","exports":{"import":"./esm.js","require":"./cjs.cjs"}}"#,
    )
    .expect("package.json should write");
    fs::write(package_dir.join("esm.js"), r#"export default "esm";"#)
        .expect("ESM entry should write");
    fs::write(package_dir.join("cjs.cjs"), r#"module.exports = "cjs";"#)
        .expect("CJS entry should write");
    fs::write(
        root.join("helper.cjs"),
        r#"module.exports = require("dual-pkg");"#,
    )
    .expect("CommonJS helper should write");
    fs::write(
        &input,
        r#"import helper from "./helper.cjs"; export function main() { return helper; }"#,
    )
    .expect("entry should write");

    super::build(&input, &out_dir, &CompileOptions::new()).expect("program should build");
    let graph_text =
        fs::read_to_string(out_dir.join("deps.graph.json")).expect("dependency graph should emit");
    let graph: serde_json::Value =
        serde_json::from_str(&graph_text).expect("dependency graph should parse");
    assert!(graph["modules"]
        .as_array()
        .expect("modules should be an array")
        .iter()
        .any(|module| module["id"] == "node_modules/dual-pkg/cjs.cjs"));
    assert!(!graph["modules"]
        .as_array()
        .expect("modules should be an array")
        .iter()
        .any(|module| module["id"] == "node_modules/dual-pkg/esm.js"));

    fs::remove_dir_all(&root).expect("program fixture directory should be removable");
}

#[test]
fn program_mode_module_and_asset_include_records_sealed_graph_entries() {
    let root = fixture_temp_dir("program-module-asset-include");
    let plugin_dir = root.join("plugins");
    let asset_dir = root.join("assets");
    let input = root.join("main.ts");
    let out_dir = root.join(".sloppy");
    fs::create_dir_all(&plugin_dir).expect("plugin directory should be created");
    fs::create_dir_all(&asset_dir).expect("asset directory should be created");
    fs::write(
        plugin_dir.join("alpha.js"),
        r#"export const value = "alpha";"#,
    )
    .expect("plugin should write");
    fs::write(asset_dir.join("copy.txt"), "asset").expect("asset should write");
    fs::write(
        &input,
        r#"export async function main(name) {
  const module = await import("./plugins/" + name + ".js");
  return module.value;
}"#,
    )
    .expect("entry should write");

    let options = CompileOptions {
        kind: Some(super::ProjectKind::Program),
        module_include: vec!["plugins/*.js".to_string()],
        asset_include: vec!["assets/**/*".to_string()],
        ..CompileOptions::default()
    };
    super::build(&input, &out_dir, &options).expect("program should build");
    let graph_text =
        fs::read_to_string(out_dir.join("deps.graph.json")).expect("dependency graph should emit");
    let graph: serde_json::Value =
        serde_json::from_str(&graph_text).expect("dependency graph should parse");
    assert!(graph["modules"]
        .as_array()
        .expect("modules should be an array")
        .iter()
        .any(|module| module["id"] == "plugins/alpha.js"
            && module["includedBy"] == "moduleInclude:plugins/*.js"));
    assert!(graph["modules"]
        .as_array()
        .expect("modules should be an array")
        .iter()
        .flat_map(|module| module["dynamicImports"].as_array().into_iter().flatten())
        .any(|import| import["kind"] == "computed"));
    assert!(graph["assets"]
        .as_array()
        .expect("assets should be an array")
        .iter()
        .any(|asset| asset["path"] == "assets/copy.txt"
            && asset["includedBy"] == "assetInclude:assets/**/*"));

    fs::remove_dir_all(&root).expect("program fixture directory should be removable");
}

#[test]
fn program_mode_project_includes_are_config_root_relative() {
    let root = fixture_temp_dir("program-project-module-asset-include");
    let src_dir = root.join("src");
    let plugin_dir = src_dir.join("plugins");
    let asset_dir = root.join("public");
    let input = src_dir.join("main.ts");
    let out_dir = root.join(".sloppy");
    fs::create_dir_all(&plugin_dir).expect("plugin directory should be created");
    fs::create_dir_all(&asset_dir).expect("asset directory should be created");
    fs::write(
        plugin_dir.join("alpha.js"),
        r#"export const value = "alpha";"#,
    )
    .expect("plugin should write");
    fs::write(asset_dir.join("copy.txt"), "asset").expect("asset should write");
    fs::write(
        &input,
        r#"export async function main(name) {
  const module = await import("./plugins/" + name + ".js");
  return module.value;
}"#,
    )
    .expect("entry should write");

    let options = CompileOptions {
        kind: Some(super::ProjectKind::Program),
        config_dir: Some(root.clone()),
        module_include: vec!["src/plugins/*.js".to_string()],
        asset_include: vec!["public/**/*".to_string()],
        ..CompileOptions::default()
    };
    super::build(&input, &out_dir, &options).expect("program should build");
    let graph_text =
        fs::read_to_string(out_dir.join("deps.graph.json")).expect("dependency graph should emit");
    let graph: serde_json::Value =
        serde_json::from_str(&graph_text).expect("dependency graph should parse");

    assert!(graph["modules"]
        .as_array()
        .expect("modules should be an array")
        .iter()
        .any(|module| module["id"] == "src/plugins/alpha.js"
            && module["includedBy"] == "moduleInclude:src/plugins/*.js"));
    assert!(graph["assets"]
        .as_array()
        .expect("assets should be an array")
        .iter()
        .any(|asset| asset["path"] == "public/copy.txt"
            && asset["includedBy"] == "assetInclude:public/**/*"));

    fs::remove_dir_all(&root).expect("program fixture directory should be removable");
}

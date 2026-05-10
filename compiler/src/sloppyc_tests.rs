use std::{
    collections::BTreeSet,
    ffi::OsString,
    fs,
    path::{Path, PathBuf},
};

use oxc_allocator::Allocator;
use oxc_ast::ast::{Expression, Statement};
use oxc_parser::Parser;
use oxc_span::SourceType;

use super::{
    arrow_requires_results_import, canonical_config_key, checksum_security_context_visible,
    command_from_args, config_key_is_diagnostic_sensitive, config_key_is_sensitive, extract,
    help_text, noncrypto_hash_security_context_visible, redact_config_value,
    route_pattern_supported, CliCommand, CompileOptions, ConfigurationModel,
};

fn fixture_temp_dir(name: &str) -> PathBuf {
    let root = std::env::temp_dir().join(format!("sloppyc-{name}-{}", std::process::id()));
    if root.exists() {
        fs::remove_dir_all(&root).expect("stale test directory should be removable");
    }
    fs::create_dir_all(&root).expect("test directory should be created");
    root
}

fn extract_temp_input(root: &Path, source: &str) -> Result<super::ExtractedApp, super::Diagnostic> {
    let input = root.join("input.js");
    fs::write(&input, source).expect("fixture input should be writable");
    extract(&input, source)
}

fn parsed_arrow_requires_results_import(handler_source: &str) -> bool {
    let allocator = Allocator::default();
    let source = format!("const handler = {handler_source};");
    let parsed = Parser::new(&allocator, &source, SourceType::mjs()).parse();
    assert!(
        parsed.errors.is_empty(),
        "handler fixture should parse: {:?}",
        parsed.errors
    );
    let Statement::VariableDeclaration(declaration) = &parsed.program.body[0] else {
        panic!("fixture should declare a handler");
    };
    let init = declaration.declarations[0]
        .init
        .as_ref()
        .expect("handler declaration should have an initializer");
    let Expression::ArrowFunctionExpression(function) = init else {
        panic!("handler fixture should be an arrow function");
    };
    arrow_requires_results_import(function)
}

#[test]
fn no_argument_prints_help() {
    assert_eq!(command_from_args(Vec::<OsString>::new()), CliCommand::Help);
}

#[test]
fn version_flag_prints_version() {
    assert_eq!(
        command_from_args([OsString::from("--version")]),
        CliCommand::Version
    );
}

#[test]
fn build_requires_input_and_output() {
    assert_eq!(
        command_from_args([OsString::from("build")]),
        CliCommand::Invalid("build requires an input file".to_string())
    );
}

#[test]
fn build_args_accept_environment_and_runtime_overrides() {
    assert_eq!(
        command_from_args([
            OsString::from("build"),
            OsString::from("app.js"),
            OsString::from("--out"),
            OsString::from(".sloppy"),
            OsString::from("--environment"),
            OsString::from("Development"),
            OsString::from("--host"),
            OsString::from("127.0.0.1"),
            OsString::from("--port"),
            OsString::from("5173"),
            OsString::from("--config"),
            OsString::from("Auth:Issuer=cli"),
        ]),
        CliCommand::Build {
            input: std::path::PathBuf::from("app.js"),
            out_dir: std::path::PathBuf::from(".sloppy"),
            options: Box::new(CompileOptions {
                kind: None,
                environment: Some("Development".to_string()),
                host: Some("127.0.0.1".to_string()),
                port: Some(5173),
                config_dir: None,
                config_overrides: vec![("Auth:Issuer".to_string(), "cli".to_string())],
                declared_capabilities: Vec::new(),
                declared_capabilities_from_sloppy_json: false,
                module_include: Vec::new(),
                asset_include: Vec::new(),
                timings_json: None,
            }),
        }
    );
}

#[test]
fn build_args_accept_timings_json_output() {
    assert_eq!(
        command_from_args([
            OsString::from("build"),
            OsString::from("app.js"),
            OsString::from("--out"),
            OsString::from(".sloppy"),
            OsString::from("--timings-json"),
            OsString::from("artifacts/bench/timings.json"),
        ]),
        CliCommand::Build {
            input: std::path::PathBuf::from("app.js"),
            out_dir: std::path::PathBuf::from(".sloppy"),
            options: Box::new(CompileOptions {
                timings_json: Some(std::path::PathBuf::from("artifacts/bench/timings.json")),
                ..CompileOptions::default()
            }),
        }
    );
}

#[test]
fn build_args_accept_project_kind() {
    assert_eq!(
        command_from_args([
            OsString::from("build"),
            OsString::from("main.ts"),
            OsString::from("--out"),
            OsString::from(".sloppy"),
            OsString::from("--kind"),
            OsString::from("program"),
        ]),
        CliCommand::Build {
            input: std::path::PathBuf::from("main.ts"),
            out_dir: std::path::PathBuf::from(".sloppy"),
            options: Box::new(CompileOptions {
                kind: Some(super::ProjectKind::Program),
                ..CompileOptions::default()
            }),
        }
    );
}

#[test]
fn build_args_accept_declared_capability_handoff() {
    assert_eq!(
        command_from_args([
            OsString::from("build"),
            OsString::from("main.ts"),
            OsString::from("--out"),
            OsString::from(".sloppy"),
            OsString::from("--capability"),
            OsString::from("fs"),
        ]),
        CliCommand::Build {
            input: std::path::PathBuf::from("main.ts"),
            out_dir: std::path::PathBuf::from(".sloppy"),
            options: Box::new(CompileOptions {
                declared_capabilities: vec!["fs".to_string()],
                ..CompileOptions::default()
            }),
        }
    );
}

#[test]
fn build_args_accept_sloppy_json_capability_origin_handoff() {
    assert_eq!(
        command_from_args([
            OsString::from("build"),
            OsString::from("main.ts"),
            OsString::from("--out"),
            OsString::from(".sloppy"),
            OsString::from("--capability-origin"),
            OsString::from("sloppy.json"),
            OsString::from("--capability"),
            OsString::from("fs"),
        ]),
        CliCommand::Build {
            input: std::path::PathBuf::from("main.ts"),
            out_dir: std::path::PathBuf::from(".sloppy"),
            options: Box::new(CompileOptions {
                declared_capabilities: vec!["fs".to_string()],
                declared_capabilities_from_sloppy_json: true,
                ..CompileOptions::default()
            }),
        }
    );
}

#[test]
fn build_args_accept_module_and_asset_includes() {
    assert_eq!(
        command_from_args([
            OsString::from("build"),
            OsString::from("main.ts"),
            OsString::from("--out"),
            OsString::from(".sloppy"),
            OsString::from("--module-include"),
            OsString::from("plugins/**/*.js"),
            OsString::from("--asset-include"),
            OsString::from("assets/**/*"),
        ]),
        CliCommand::Build {
            input: std::path::PathBuf::from("main.ts"),
            out_dir: std::path::PathBuf::from(".sloppy"),
            options: Box::new(CompileOptions {
                module_include: vec!["plugins/**/*.js".to_string()],
                asset_include: vec!["assets/**/*".to_string()],
                ..CompileOptions::default()
            }),
        }
    );
}

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
fn program_mode_rejects_re_export_declarations() {
    for source in [
        "export { value } from \"./dep\";\n",
        "export * from \"./dep\";\n",
    ] {
        let root = std::env::temp_dir().join(format!(
            "sloppyc-program-reexport-{}-{}",
            std::process::id(),
            source.len()
        ));
        let input = root.join("main.ts");
        let dep = root.join("dep.ts");
        let out_dir = root.join(".sloppy");
        if root.exists() {
            fs::remove_dir_all(&root).expect("stale program fixture should be removable");
        }
        fs::create_dir_all(&root).expect("program fixture directory should be created");
        fs::write(&input, source).expect("input should write");
        fs::write(&dep, "export const value = 1;\n").expect("dep should write");

        let error = super::build(&input, &out_dir, &CompileOptions::new())
            .expect_err("re-export should fail");
        assert_eq!(error.diagnostic.code, "SLOPPYC_E_UNSUPPORTED_EXPORT");

        fs::remove_dir_all(&root).expect("program fixture directory should be removable");
    }
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
    assert!(super::NODE_BUFFER_SHIM.contains("String.fromCodePoint"));
    assert!(super::NODE_BUFFER_SHIM.contains("0xf4"));
    assert!(!super::NODE_BUFFER_SHIM.contains("fromCharCode(value[i])"));
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
    fs::write(root.join("data.json"), r#"{"message":"json-default"}"#)
        .expect("JSON fixture should write");
    fs::write(
        &input,
        r#"import data from "./data.json"; export function main() { return data.message; }"#,
    )
    .expect("entry should write");

    super::build(&input, &out_dir, &CompileOptions::new()).expect("program should build");
    let app_js = fs::read_to_string(out_dir.join("app.js")).expect("app.js should emit");
    assert!(app_js.contains("module.exports.default = __sloppy_json_module;"));
    assert!(app_js.contains("__sloppy_program_require(\"data.json\").default"));

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
        "factory(module.exports, module, function(specifier) { return __sloppy_program_require_from(id, specifier); }, id, __sloppy_program_dirname(id));"
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

#[test]
fn program_mode_marks_each_runtime_stdlib_subpath() {
    let cases = [
        ("sloppy/fs", "File", "stdlib.fs"),
        ("sloppy/net", "HttpClient", "stdlib.httpclient"),
        ("sloppy/os", "Process", "stdlib.os"),
        ("sloppy/time", "Time", "stdlib.time"),
        ("sloppy/crypto", "Random", "stdlib.crypto"),
        ("sloppy/codec", "Base64", "stdlib.codec"),
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
fn help_text_lists_diagnostics_timing_json_alias() {
    let help = help_text();
    assert!(help.contains("--timings-json|--diagnostics-timing-json <file>"));
}

#[test]
fn module_graph_root_level_entry_uses_current_directory_as_source_root() {
    let graph = super::ModuleGraph::new(Path::new("app.js"), None);
    let current_dir =
        fs::canonicalize(std::env::current_dir().expect("current directory should exist"))
            .expect("current directory should canonicalize");
    assert_eq!(graph.entry_dir, current_dir);
}

#[test]
fn keep_alive_environment_override_keys_are_canonicalized() {
    assert_eq!(
        canonical_config_key("SLOPPY:SERVER:KEEPALIVEENABLED"),
        "Sloppy:Server:KeepAliveEnabled"
    );
    assert_eq!(
        canonical_config_key("SLOPPY:SERVER:KEEPALIVEIDLETIMEOUTMS"),
        "Sloppy:Server:KeepAliveIdleTimeoutMs"
    );
    assert_eq!(
        canonical_config_key("SLOPPY:SERVER:MAXREQUESTSPERCONNECTION"),
        "Sloppy:Server:MaxRequestsPerConnection"
    );
    assert_eq!(
        canonical_config_key("SLOPPY:SERVER:TLS:PRIVATEKEYPATH"),
        "Sloppy:Server:Tls:PrivateKeyPath"
    );
    assert_eq!(
        canonical_config_key("SLOPPY:SERVER:TLS:CERTIFICATEPATH"),
        "Sloppy:Server:Tls:CertificatePath"
    );
    assert_eq!(
        canonical_config_key("SLOPPY:SERVER:TLS:PASSPHRASE"),
        "Sloppy:Server:Tls:Passphrase"
    );
    assert_eq!(
        canonical_config_key("SLOPPY:LOGGING:MINIMUMLEVEL"),
        "Sloppy:Logging:MinimumLevel"
    );
    assert_eq!(
        canonical_config_key("SLOPPY:LOGGING:CONSOLE:FORMAT"),
        "Sloppy:Logging:Console:Format"
    );
    assert_eq!(
        canonical_config_key("SLOPPY:LOGGING:FILE:PATH"),
        "Sloppy:Logging:File:Path"
    );
    assert_eq!(
        canonical_config_key("SLOPPY:PROVIDERS:POSTGRES:MAIN:CONNECTIONSTRING"),
        "Sloppy:Providers:postgres:MAIN:connectionString"
    );
    assert_eq!(
        canonical_config_key("AUTH:CLIENTSECRET"),
        "AUTH:clientSecret"
    );
    assert_eq!(canonical_config_key("AUTH:PRIVATEKEY"), "AUTH:privateKey");
}

#[test]
fn configuration_files_overlay_and_bind_sqlite_provider() {
    let root = std::env::temp_dir().join(format!("sloppyc-config-test-{}", std::process::id()));
    if root.exists() {
        fs::remove_dir_all(&root).expect("stale config test directory should be removable");
    }
    fs::create_dir_all(&root).expect("config test directory should be created");
    let input = root.join("app.js");
    fs::write(&input, "export default {};").expect("input should be written");
    fs::write(
            root.join("appsettings.json"),
            r#"{"Sloppy":{"Server":{"Port":5000},"Providers":{"sqlite":{"main":{"database":"base.db"}}}}}"#,
        )
        .expect("base appsettings should be written");
    fs::write(
            root.join("appsettings.Development.json"),
            r#"{"Sloppy":{"Server":{"Port":5173},"Providers":{"sqlite":{"main":{"database":"dev.db"}}}}}"#,
        )
        .expect("environment appsettings should be written");

    let options = CompileOptions {
        kind: None,
        environment: Some("Development".to_string()),
        host: Some("0.0.0.0".to_string()),
        port: Some(6000),
        config_dir: None,
        config_overrides: Vec::new(),
        declared_capabilities: Vec::new(),
        declared_capabilities_from_sloppy_json: false,
        module_include: Vec::new(),
        asset_include: Vec::new(),
        timings_json: None,
    };
    let config =
        super::ConfigurationModel::load(&input, &options, &[]).expect("configuration should load");
    assert_eq!(
        config
            .get_string("Sloppy:Providers:sqlite:main:database")
            .expect("database key should be string"),
        Some("dev.db".to_string())
    );
    assert_eq!(
        &config
            .get("Sloppy:Server:Port")
            .expect("port should exist")
            .value,
        &serde_json::json!(6000)
    );
    assert_eq!(
        &config
            .get("Sloppy:Server:Host")
            .expect("host should exist")
            .value,
        &serde_json::json!("0.0.0.0")
    );

    let mut app = super::ExtractedApp {
        kind: super::ProjectKind::Web,
        program_entry: None,
        program_modules: Vec::new(),
        uses_data_runtime: true,
        uses_sql_runtime: false,
        source_files: Vec::new(),
        routes: Vec::new(),
        dynamic_routes: Vec::new(),
        dynamic_entry_source: None,
        service_registrations: Vec::new(),
        modules: Vec::new(),
        helper_sources: Vec::new(),
        capabilities: vec![super::DatabaseCapability {
            token: "data.main".to_string(),
            capability_kind: "database".to_string(),
            provider: "sqlite".to_string(),
            config_name: Some("main".to_string()),
            config_key: None,
            access: "readwrite".to_string(),
            database: None,
            config_source: None,
            source_name: "app.js".to_string(),
            source: String::new(),
            span: super::Span::new(0, 0),
            from_provider_use: true,
        }],
        configuration: None,
        schemas: Vec::new(),
        config_reads: Vec::new(),
        uses_time_runtime: false,
        uses_fs_runtime: false,
        uses_crypto_runtime: false,
        noncrypto_hash_security_context_visible: false,
        uses_codec_runtime: false,
        checksum_security_context_visible: false,
        uses_net_runtime: false,
        uses_os_runtime: false,
        uses_http_client_runtime: false,
        uses_workers_runtime: false,
        uses_ffi_runtime: false,
        ffi: Vec::new(),
        ffi_structs: Vec::new(),
        uses_health: false,
        problem_details: None,
        dependency_graph: super::DependencyGraph::default(),
    };
    config
        .apply_to_app(&mut app)
        .expect("provider config should bind");
    assert_eq!(app.capabilities[0].database.as_deref(), Some("dev.db"));
    assert!(app.configuration.is_some());

    fs::remove_dir_all(&root).expect("config test directory should be removable");
}

#[test]
fn configuration_precedence_includes_local_secrets_env_and_cli_overrides() {
    let root = fixture_temp_dir("config-precedence");
    let input = root.join("app.js");
    fs::write(
        &input,
        r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
const jwt = app.config.getSecret("Auth:JwtSecret");
const issuer = app.config.getString("Auth:Issuer");
app.get("/", () => Results.text("ok"));
export default app;
"#,
    )
    .expect("input should be written");
    fs::write(
        root.join("appsettings.json"),
        r#"{"Auth":{"Issuer":"base","JwtSecret":"base-value"}}"#,
    )
    .expect("base appsettings should be written");
    fs::write(
        root.join("appsettings.Development.json"),
        r#"{"Auth":{"Issuer":"environment"}}"#,
    )
    .expect("environment appsettings should be written");
    fs::write(
        root.join("appsettings.local.json"),
        r#"{"Auth":{"Issuer":"local"}}"#,
    )
    .expect("local appsettings should be written");
    fs::write(
        root.join("appsettings.Development.local.json"),
        r#"{"Auth":{"Issuer":"environment-local"}}"#,
    )
    .expect("environment local appsettings should be written");
    fs::create_dir_all(root.join(".sloppy")).expect("secret directory should be created");
    fs::write(
        root.join(".sloppy").join("secrets.json"),
        r#"{"Auth":{"JwtSecret":"store-value"}}"#,
    )
    .expect("user secrets should be written");

    std::env::set_var("Auth__JwtSecret", "env-value");
    let mut app = extract(
        &input,
        &fs::read_to_string(&input).expect("source should read"),
    )
    .expect("config app should extract");
    let options = CompileOptions {
        kind: None,
        environment: Some("Development".to_string()),
        host: None,
        port: None,
        config_dir: None,
        config_overrides: vec![("Auth:Issuer".to_string(), "cli".to_string())],
        declared_capabilities: Vec::new(),
        declared_capabilities_from_sloppy_json: false,
        module_include: Vec::new(),
        asset_include: Vec::new(),
        timings_json: None,
    };
    let config = super::ConfigurationModel::load(&input, &options, &app.config_reads)
        .expect("configuration should load");
    assert_eq!(
        config
            .get_string("Auth:JwtSecret")
            .expect("secret key should be string"),
        Some("env-value".to_string())
    );
    assert_eq!(
        config
            .get_string("Auth:Issuer")
            .expect("issuer key should be string"),
        Some("cli".to_string())
    );
    config
        .apply_to_app(&mut app)
        .expect("configuration should apply");
    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    assert!(!plan.contains("env-value"));
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert!(plan["configuration"]["requirements"]
        .as_array()
        .expect("requirements should be an array")
        .iter()
        .any(|requirement| requirement["key"] == "Auth:JwtSecret"
            && requirement["status"] == "present"
            && requirement["redaction"] == "secret"));
    assert!(plan["configuration"]["packageManifest"]["required"]
        .as_array()
        .expect("required manifest should be an array")
        .iter()
        .any(|entry| entry["env"] == "Auth__JwtSecret" && entry["secret"] == true));
    std::env::remove_var("Auth__JwtSecret");
    fs::remove_dir_all(&root).expect("config test directory should be removable");
}

#[test]
fn config_bind_descriptors_emit_required_optional_and_secret_metadata() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
const auth = app.config.bind("Auth", {
  jwtSecret: "secret",
  tokenTtlMinutes: { type: "number", default: 60, min: 1, max: 1440 },
  issuer: { key: "Jwt:Issuer", type: "string", required: true }
});
app.get("/", () => Results.text("ok"));
export default app;
"#;
    let mut app =
        extract(std::path::Path::new("app.js"), source).expect("bind descriptors should extract");
    assert_eq!(app.config_reads.len(), 3);
    assert!(app
        .config_reads
        .iter()
        .any(|read| { read.key == "Auth:JwtSecret" && read.sensitive && read.required }));
    assert!(app
        .config_reads
        .iter()
        .any(|read| { read.key == "Auth:TokenTtlMinutes" && read.has_default && !read.required }));
    assert!(app
        .config_reads
        .iter()
        .any(|read| read.key == "Auth:Jwt:Issuer" && !read.sensitive && read.required));
    let config = super::ConfigurationModel {
        environment: "Development".to_string(),
        values: std::collections::BTreeMap::new(),
    };
    config
        .apply_to_app(&mut app)
        .expect("bind metadata should apply without requiring dev values");
    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    let requirements = plan["configuration"]["requirements"]
        .as_array()
        .expect("requirements should be an array");
    assert!(requirements
        .iter()
        .any(|requirement| requirement["key"] == "Auth:JwtSecret"
            && requirement["status"] == "missing"
            && requirement["secret"] == true));
    assert!(requirements
        .iter()
        .any(|requirement| requirement["key"] == "Auth:Jwt:Issuer"
            && requirement["status"] == "missing"
            && requirement["secret"] == false));
    assert!(plan["configuration"]["packageManifest"]["optional"]
        .as_array()
        .expect("optional manifest should be an array")
        .iter()
        .any(|entry| entry["key"] == "Auth:TokenTtlMinutes"
            && entry["default"].as_f64() == Some(60.0)));
}

#[test]
fn provider_config_preserves_declared_name_for_dotted_sqlite_names() {
    let root =
        std::env::temp_dir().join(format!("sloppyc-config-dotted-test-{}", std::process::id()));
    if root.exists() {
        fs::remove_dir_all(&root).expect("stale config test directory should be removable");
    }
    fs::create_dir_all(&root).expect("config test directory should be created");
    let input = root.join("app.js");
    fs::write(
        &input,
        r#"import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
const app = Sloppy.create();
app.use(sqlite("data.main"));
app.mapGet("/", () => Results.text("ok"));
export default app;
"#,
    )
    .expect("input should be written");
    fs::write(
            root.join("appsettings.json"),
            r#"{"Sloppy":{"Providers":{"sqlite":{"data.main":{"database":"dotted.db"},"main":{"database":"wrong.db"}}}}}"#,
        )
        .expect("appsettings should be written");
    let out_dir = root.join(".sloppy");

    super::build(&input, &out_dir, &CompileOptions::new())
        .expect("dotted provider config should bind");
    let plan = fs::read_to_string(out_dir.join("app.plan.json")).expect("plan should exist");
    assert!(plan.contains("\"database\": \"dotted.db\""));
    assert!(plan.contains("\"prefix\": \"Sloppy:Providers:sqlite:data.main\""));
    assert!(!plan.contains("\"database\": \"wrong.db\""));

    fs::remove_dir_all(&root).expect("config test directory should be removable");
}

#[test]
fn repeated_sqlite_provider_use_keeps_latest_declaration() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
const app = Sloppy.create();
app.use(sqlite("main"));
app.use(sqlite("main", { database: ":memory:" }));
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let mut app = extract(std::path::Path::new("app.js"), source)
        .expect("repeated provider use should extract");
    assert_eq!(app.capabilities.len(), 1);
    assert_eq!(app.capabilities[0].database.as_deref(), Some(":memory:"));
    let config = super::ConfigurationModel {
        environment: "Development".to_string(),
        values: std::collections::BTreeMap::new(),
    };
    config
        .apply_to_app(&mut app)
        .expect("latest inline provider declaration should not require config");
}

#[test]
fn configuration_plan_redacts_sensitive_values() {
    let mut config = super::ConfigurationModel {
        environment: "Development".to_string(),
        values: std::collections::BTreeMap::new(),
    };
    config.set(
        "Sloppy:Providers:sqlite:main:password",
        serde_json::json!("secret"),
        "test",
    );
    let keys = config.plan_keys();
    assert_eq!(keys.len(), 1);
    assert!(keys[0].sensitive);
    assert_eq!(keys[0].value, serde_json::json!("<redacted>"));
    assert!(!keys[0].value.to_string().contains("secret"));
}

#[test]
fn configuration_plan_redacts_pwd_alias_values() {
    let mut config = super::ConfigurationModel {
        environment: "Development".to_string(),
        values: std::collections::BTreeMap::new(),
    };
    config.set(
        "Sloppy:Providers:sqlite:main:Pwd",
        serde_json::json!("secret"),
        "test",
    );
    let keys = config.plan_keys();
    assert_eq!(keys.len(), 1);
    assert!(keys[0].sensitive);
    assert_eq!(keys[0].value, serde_json::json!("<redacted>"));
    assert!(!keys[0].value.to_string().contains("secret"));
}

#[test]
fn configuration_plan_keeps_tls_paths_for_runtime_but_redacts_diagnostic_hints() {
    let mut config = super::ConfigurationModel {
        environment: "Development".to_string(),
        values: std::collections::BTreeMap::new(),
    };
    config.set(
        "Sloppy:Server:Tls:CertificatePath",
        serde_json::json!("certs/server.crt"),
        "test",
    );
    config.set(
        "Sloppy:Server:Tls:PrivateKeyPath",
        serde_json::json!("C:/keys/server.key"),
        "test",
    );
    config.set(
        "Sloppy:Server:Tls:Passphrase",
        serde_json::json!("secret"),
        "test",
    );
    let keys = config.plan_keys();
    assert_eq!(keys.len(), 3);
    let certificate_path = keys
        .iter()
        .find(|key| key.key == "Sloppy:Server:Tls:CertificatePath")
        .expect("certificate path should be present");
    let key_path = keys
        .iter()
        .find(|key| key.key == "Sloppy:Server:Tls:PrivateKeyPath")
        .expect("private key path should be present");
    let passphrase = keys
        .iter()
        .find(|key| key.key == "Sloppy:Server:Tls:Passphrase")
        .expect("passphrase should be present");
    assert!(!certificate_path.sensitive);
    assert_eq!(
        certificate_path.value,
        serde_json::json!("certs/server.crt")
    );
    assert!(!key_path.sensitive);
    assert_eq!(key_path.value, serde_json::json!("C:/keys/server.key"));
    assert!(passphrase.sensitive);
    assert_eq!(passphrase.value, serde_json::json!("<redacted>"));
    assert!(!keys
        .iter()
        .any(|key| key.value.to_string().contains("secret")));
    assert!(config_key_is_diagnostic_sensitive(
        "Sloppy:Server:Tls:CertificatePath"
    ));
    assert!(config_key_is_diagnostic_sensitive(
        "Sloppy:Server:Tls:PrivateKeyPath"
    ));
    assert_eq!(
        redact_config_value("Sloppy:Server:Tls:CertificatePath", "certs/server.crt"),
        "<redacted>"
    );
    assert_eq!(
        redact_config_value("Sloppy:Server:Tls:PrivateKeyPath", "C:/keys/server.key"),
        "<redacted>"
    );
}

#[test]
fn configuration_load_anchors_tls_paths_to_config_dir() {
    let root = fixture_temp_dir("config-tls-path-root");
    if root.exists() {
        fs::remove_dir_all(&root).expect("stale config test directory should be removable");
    }
    fs::create_dir_all(&root).expect("config test directory should be created");
    fs::write(
        root.join("appsettings.json"),
        r#"{
  "Sloppy": {
    "Server": {
      "Tls": {
        "CertificatePath": "certs/dev.crt",
        "PrivateKeyPath": "keys/dev.key"
      }
    }
  }
}"#,
    )
    .expect("appsettings should be written");

    let options = CompileOptions {
        config_dir: Some(root.clone()),
        ..Default::default()
    };
    let model = ConfigurationModel::load(&root.join("src/main.ts"), &options, &[])
        .expect("configuration should load");
    let keys = model.plan_keys();
    let certificate_path = keys
        .iter()
        .find(|key| key.key == "Sloppy:Server:Tls:CertificatePath")
        .and_then(|key| key.value.as_str())
        .expect("certificate path should be present");
    let private_key_path = keys
        .iter()
        .find(|key| key.key == "Sloppy:Server:Tls:PrivateKeyPath")
        .and_then(|key| key.value.as_str())
        .expect("private key path should be present");

    assert!(Path::new(certificate_path).is_absolute());
    assert!(Path::new(private_key_path).is_absolute());
    assert!(
        certificate_path.ends_with("certs/dev.crt") || certificate_path.ends_with("certs\\dev.crt")
    );
    assert!(
        private_key_path.ends_with("keys/dev.key") || private_key_path.ends_with("keys\\dev.key")
    );

    fs::remove_dir_all(&root).expect("config test directory should be cleaned up");
}

#[test]
fn configuration_key_sensitivity_covers_alpha_secret_aliases() {
    assert!(config_key_is_sensitive("Auth:apiKey"));
    assert!(config_key_is_sensitive("Auth:clientSecret"));
    assert!(config_key_is_sensitive("Auth:privateKey"));
    assert!(config_key_is_sensitive(
        "Sloppy:Providers:postgres:main:connectionString"
    ));
    assert!(!config_key_is_sensitive("Sloppy:Server:Tls:PrivateKeyPath"));
    assert!(!config_key_is_sensitive(
        "Sloppy:HttpClient:Tls:ClientCaPath"
    ));
    assert!(config_key_is_diagnostic_sensitive(
        "Sloppy:HttpClient:Tls:ClientCertificatePath"
    ));
    assert!(config_key_is_diagnostic_sensitive(
        "Sloppy:HttpClient:Tls:ClientPrivateKeyPath"
    ));
    assert!(config_key_is_diagnostic_sensitive(
        "Sloppy:HttpClient:Tls:ClientCaPath"
    ));
    assert!(config_key_is_diagnostic_sensitive(
        "Sloppy:HttpClient:Tls:CaBundlePath"
    ));
    assert!(config_key_is_diagnostic_sensitive(
        "Sloppy:HttpClient:Tls:TrustStorePath"
    ));
    assert!(config_key_is_diagnostic_sensitive(
        "Sloppy:HttpClient:Tls:CaPath"
    ));
}

#[test]
fn configuration_json_rejects_empty_key_segments() {
    let root =
        std::env::temp_dir().join(format!("sloppyc-config-empty-key-{}", std::process::id()));
    if root.exists() {
        fs::remove_dir_all(&root).expect("stale config test directory should be removable");
    }
    fs::create_dir_all(&root).expect("config test directory should be created");
    let input = root.join("app.js");
    fs::write(&input, "export default {};").expect("input should be written");
    fs::write(
        root.join("appsettings.json"),
        r#"{"Sloppy":{"Server":{"":5173}}}"#,
    )
    .expect("appsettings should be written");

    let diagnostic = super::ConfigurationModel::load(&input, &super::CompileOptions::new(), &[])
        .expect_err("empty config key segment should fail");
    assert_eq!(diagnostic.code, "SLOPPYC_E_CONFIG_KEY");
    assert!(
        diagnostic.message.contains("empty config key segment"),
        "{}",
        diagnostic.message
    );

    fs::remove_dir_all(&root).expect("config test directory should be removable");
}

#[test]
fn extracts_literal_map_get() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("Hello"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source).expect("fixture should extract");
    assert_eq!(app.routes.len(), 1);
    assert_eq!(app.routes[0].pattern, "/");
}

#[test]
fn extracts_minimal_api_methods() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.get("/get", () => Results.text("get"));
app.post("/post", () => Results.text("post"));
app.put("/put", () => Results.text("put"));
app.patch("/patch", () => Results.text("patch"));
app.delete("/delete", () => Results.text("delete"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source).expect("fixture should extract");
    let routes = app
        .routes
        .iter()
        .map(|route| (route.method, route.pattern.as_str()))
        .collect::<Vec<_>>();
    assert_eq!(
        routes,
        [
            ("GET", "/get"),
            ("POST", "/post"),
            ("PUT", "/put"),
            ("PATCH", "/patch"),
            ("DELETE", "/delete"),
        ]
    );
}

#[test]
fn rejects_unsupported_direct_http_methods_explicitly() {
    for method in ["head", "options"] {
        let source = format!(
            r#"import {{ Sloppy, Results }} from "sloppy";
const app = Sloppy.create();
app.{method}("/", () => Results.text("unsupported"));
export default app;
"#
        );
        let diagnostic = extract(std::path::Path::new("app.js"), &source)
            .expect_err("unsupported direct HTTP method should fail");
        assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_HTTP_METHOD");
    }
}

#[test]
fn extracts_nested_route_groups() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
const api = app.group("/api");
const users = api.group("/users");
users.get("/{id:int}", () => Results.json({ ok: true }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source).expect("fixture should extract");
    assert_eq!(app.routes.len(), 1);
    assert_eq!(app.routes[0].method, "GET");
    assert_eq!(app.routes[0].pattern, "/api/users/{id:int}");
}

#[test]
fn typed_framework_route_bindings_use_full_grouped_pattern() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
const users = app.group("/users/:userId");
users.get("/posts/:postId", (userId: number, postId: number) => Results.ok({ ok: true }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("typed grouped route should extract");
    assert_eq!(app.routes.len(), 1);
    assert_eq!(app.routes[0].pattern, "/users/{userId}/posts/{postId}");
    assert_eq!(
        app.routes[0].framework_path.as_deref(),
        Some("/users/:userId/posts/:postId")
    );
    let bindings = app.routes[0]
        .handler
        .bindings
        .iter()
        .map(|binding| (binding.kind.as_str(), binding.name.as_deref()))
        .collect::<Vec<_>>();
    assert_eq!(
        bindings,
        [("route", Some("userId")), ("route", Some("postId")),]
    );
}

#[test]
fn typed_framework_colon_route_type_suffix_binds_name() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.get("/users/:id:int", (id: number) => Results.ok({ id }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("typed colon route with type suffix should extract");
    assert_eq!(app.routes.len(), 1);
    assert_eq!(app.routes[0].pattern, "/users/{id:int}");
    let bindings = app.routes[0]
        .handler
        .bindings
        .iter()
        .map(|binding| (binding.kind.as_str(), binding.name.as_deref()))
        .collect::<Vec<_>>();
    assert_eq!(bindings, [("route", Some("id"))]);
}

#[test]
fn extracts_direct_and_nested_function_module_routes() {
    let root = fixture_temp_dir("function-module-routes");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("users.js"),
        r#"import { Results } from "sloppy";

export function usersModule(app) {
    app.get("/module-health", () => Results.text("ok"));
    const api = app.group("/api");
    const users = api.group("/users");
    users.post("/", () => Results.json({ ok: true }));
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy, Results } from "sloppy";
import { usersModule } from "./modules/users.js";

const app = Sloppy.create();
app.useModule(usersModule);
app.get("/health", () => Results.text("ok"));
export default app;
"#;
    let app = extract_temp_input(&root, source).expect("fixture should extract");
    assert_eq!(app.modules.len(), 1);
    assert_eq!(app.modules[0].name, "usersModule");
    let routes = app
        .routes
        .iter()
        .map(|route| {
            (
                route.method,
                route.pattern.as_str(),
                route.module.as_deref(),
            )
        })
        .collect::<Vec<_>>();
    assert_eq!(
        routes,
        [
            ("GET", "/health", None),
            ("GET", "/module-health", Some("usersModule")),
            ("POST", "/api/users", Some("usersModule")),
        ]
    );

    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    assert!(plan.contains("\"module\": \"usersModule\""));
    assert!(plan.contains("\"path\": \"users.js\""));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn function_module_can_register_health_checks() {
    let root = fixture_temp_dir("function-module-health");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("health.js"),
        r#"export function healthModule(app) {
    app.mapHealthChecks({
        path: "/health",
        livenessPath: "/health/live",
        readinessPath: "/health/ready",
        checks: [
            { name: "database", readiness: true, check: () => true },
        ],
    });
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy } from "sloppy";
import { healthModule } from "./modules/health.js";

const app = Sloppy.create();
app.useModule(healthModule);
export default app;
"#;
    let app = extract_temp_input(&root, source).expect("module health routes should extract");
    let routes = app
        .routes
        .iter()
        .map(|route| {
            (
                route.method,
                route.pattern.as_str(),
                route.module.as_deref(),
            )
        })
        .collect::<Vec<_>>();
    assert_eq!(
        routes,
        [
            ("GET", "/health", Some("healthModule")),
            ("GET", "/health/live", Some("healthModule")),
            ("GET", "/health/ready", Some("healthModule")),
        ]
    );
    assert!(app.uses_health);

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn function_module_provider_handlers_can_use_local_helpers() {
    let root = fixture_temp_dir("function-module-provider-helpers");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("users.js"),
        r#"import { Results } from "sloppy";

export function usersModule(app) {
    const db = app.provider("sqlite:main");

    function migrateUsers() {
        db.exec("create table if not exists users (id integer primary key, name text not null)", []);
    }

    function listUsers() {
        migrateUsers();
        return db.query("select id, name from users order by id", []);
    }

    app.get("/users", () => {
        return Results.ok(listUsers());
    }).withName("Users.List");
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
import { usersModule } from "./modules/users.js";

const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
app.useModule(usersModule);
export default app;
"#;
    let app = extract_temp_input(&root, source).expect("module provider helpers should extract");
    assert_eq!(app.routes.len(), 1);
    assert_eq!(app.routes[0].pattern, "/users");
    assert_eq!(app.routes[0].module.as_deref(), Some("usersModule"));
    assert_eq!(app.routes[0].handler.effects.len(), 2);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("function migrateUsers()"));
    assert!(emitted_js.source.contains("function listUsers()"));
    assert!(emitted_js.source.contains("__sloppy_open_data_provider"));
    assert!(emitted_js
        .source
        .contains("return Results.ok(listUsers());"));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn function_module_still_rejects_multistatement_handlers_without_runtime_effects() {
    let root = fixture_temp_dir("function-module-multistatement-no-effects");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("users.js"),
        r#"import { Results } from "sloppy";

export function usersModule(app) {
    app.get("/users", () => {
        const users = [{ id: 1 }];
        return Results.ok(users);
    });
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy } from "sloppy";
import { usersModule } from "./modules/users.js";

const app = Sloppy.create();
app.useModule(usersModule);
export default app;
"#;
    let diagnostic = extract_temp_input(&root, source)
        .expect_err("module handler without runtime effects should stay bounded");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_HANDLER");
    assert!(diagnostic
        .path
        .as_deref()
        .is_some_and(|path| path.ends_with("modules/users.js")));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn typed_function_module_route_bindings_use_full_grouped_pattern() {
    let root = fixture_temp_dir("function-module-typed-groups");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("users.ts"),
        r#"import { Results } from "sloppy";

export function usersModule(app) {
    const users = app.group("/users/:userId");
    users.get("/posts/:postId", (userId: number, postId: number) => Results.ok({ ok: true }));
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy, Results } from "sloppy";
import { usersModule } from "./modules/users.ts";

const app = Sloppy.create();
app.useModule(usersModule);
export default app;
"#;
    let app = extract_temp_input(&root, source)
        .expect("typed grouped function module route should extract");
    assert_eq!(app.routes.len(), 1);
    assert_eq!(app.routes[0].pattern, "/users/{userId}/posts/{postId}");
    assert_eq!(app.routes[0].module.as_deref(), Some("usersModule"));
    let bindings = app.routes[0]
        .handler
        .bindings
        .iter()
        .map(|binding| (binding.kind.as_str(), binding.name.as_deref()))
        .collect::<Vec<_>>();
    assert_eq!(
        bindings,
        [("route", Some("userId")), ("route", Some("postId")),]
    );

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn function_module_sloppy_time_import_emits_required_feature() {
    let root = fixture_temp_dir("function-module-time-import");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("jobs.js"),
        r#"import { Results } from "sloppy";
import { Time, Deadline } from "sloppy/time";

export function jobsModule(app) {
    app.get("/jobs", () => Results.text("ok"));
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy, Results } from "sloppy";
import { jobsModule } from "./modules/jobs.js";

const app = Sloppy.create();
app.useModule(jobsModule);
app.get("/health", () => Results.text("ok"));
export default app;
"#;
    let app = extract_temp_input(&root, source).expect("fixture should extract");
    assert!(app.uses_time_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("Time, Deadline"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert_eq!(plan["requiredFeatures"], serde_json::json!(["stdlib.time"]));
    assert_eq!(plan["features"]["time"], serde_json::json!(true));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn function_module_sloppy_net_import_emits_required_feature() {
    let root = fixture_temp_dir("function-module-net-import");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("tcp.js"),
        r#"import { Results } from "sloppy";
import { TcpClient, TcpConnection } from "sloppy/net";

export function tcpModule(app) {
    app.get("/tcp", () => Results.text("ok"));
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy, Results } from "sloppy";
import { tcpModule } from "./modules/tcp.js";

const app = Sloppy.create();
app.useModule(tcpModule);
app.get("/health", () => Results.text("ok"));
export default app;
"#;
    let app = extract_temp_input(&root, source).expect("fixture should extract");
    assert!(app.uses_net_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("TcpClient"));
    assert!(emitted_js.source.contains("TcpConnection"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert_eq!(plan["requiredFeatures"], serde_json::json!(["stdlib.net"]));
    assert_eq!(plan["features"]["network"], serde_json::json!(true));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn function_module_sloppy_net_http_client_import_emits_http_client_required_feature() {
    let root = fixture_temp_dir("function-module-http-client-import");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("billing.js"),
        r#"import { Results } from "sloppy";
import { HttpClient } from "sloppy/net";

export function billingModule(app) {
    app.get("/billing", () => Results.text("ok"));
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy, Results } from "sloppy";
import { billingModule } from "./modules/billing.js";

const app = Sloppy.create();
app.useModule(billingModule);
app.get("/health", () => Results.text("ok"));
export default app;
"#;
    let app = extract_temp_input(&root, source).expect("fixture should extract");
    assert!(app.uses_http_client_runtime);
    assert!(!app.uses_net_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js
        .source
        .contains("const { Results, HttpClient } = __sloppyRuntime;"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert_eq!(
        plan["requiredFeatures"],
        serde_json::json!(["stdlib.httpclient"])
    );
    assert_eq!(plan["features"]["httpClient"], serde_json::json!(true));
    assert_eq!(
        plan["strongPlan"]["evidence"]["httpClient"],
        serde_json::json!(true)
    );
    assert_eq!(
        plan["doctorChecks"][0]["id"],
        serde_json::json!("stdlib.httpclient.contract")
    );

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn function_module_sloppy_workers_import_emits_required_feature() {
    let root = fixture_temp_dir("function-module-workers-import");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("jobs.js"),
        r#"import { Results } from "sloppy";
import { WorkerPool } from "sloppy/workers";

export function jobsModule(app) {
    app.get("/jobs", () => Results.text("ok"));
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy, Results } from "sloppy";
import { jobsModule } from "./modules/jobs.js";

const app = Sloppy.create();
app.useModule(jobsModule);
app.get("/health", () => Results.text("ok"));
export default app;
"#;
    let app = extract_temp_input(&root, source).expect("fixture should extract");
    assert!(app.uses_workers_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("WorkerPool"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert_eq!(
        plan["requiredFeatures"],
        serde_json::json!(["stdlib.workers"])
    );
    assert_eq!(plan["features"]["workers"], serde_json::json!(true));
    assert_eq!(
        plan["strongPlan"]["evidence"]["workers"],
        serde_json::json!(true)
    );

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn function_module_sloppy_codec_import_emits_required_feature() {
    let root = fixture_temp_dir("function-module-codec-import");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("payloads.js"),
        r#"import { Results } from "sloppy";
import { Base64, Base64Url, Hex, Text, Binary, Compression, Checksums } from "sloppy/codec";

export function payloadsModule(app) {
    app.get("/payloads", () => Results.text("ok"));
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy, Results } from "sloppy";
import { payloadsModule } from "./modules/payloads.js";

const app = Sloppy.create();
app.useModule(payloadsModule);
app.get("/health", () => Results.text("ok"));
export default app;
"#;
    let app = extract_temp_input(&root, source).expect("fixture should extract");
    assert!(app.uses_codec_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("Base64"));
    assert!(emitted_js.source.contains("Checksums"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert_eq!(
        plan["requiredFeatures"],
        serde_json::json!(["stdlib.codec"])
    );
    assert_eq!(plan["features"]["codec"], serde_json::json!(true));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn function_module_type_only_sloppy_net_import_does_not_emit_required_feature() {
    let root = fixture_temp_dir("function-module-type-only-net-import");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("tcp.ts"),
        r#"import { Results } from "sloppy";
import type { TcpClient } from "sloppy/net";

export function tcpModule(app) {
    app.get("/tcp", () => Results.text("ok"));
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy, Results } from "sloppy";
import { tcpModule } from "./modules/tcp.ts";

const app = Sloppy.create();
app.useModule(tcpModule);
app.get("/health", () => Results.text("ok"));
export default app;
"#;
    let input = root.join("input.ts");
    fs::write(&input, source).expect("fixture input should be writable");
    let app = extract(&input, source).expect("fixture should extract");
    assert!(!app.uses_net_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js
        .source
        .contains("const { Results } = __sloppyRuntime;"));
    assert!(!emitted_js.source.contains("TcpClient"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert!(plan.get("requiredFeatures").is_none());
    assert!(plan["features"].get("network").is_none());

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn function_module_type_only_sloppy_net_http_client_import_does_not_emit_required_feature() {
    let root = fixture_temp_dir("function-module-type-only-http-client-import");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("billing.ts"),
        r#"import { Results } from "sloppy";
import type { HttpClient } from "sloppy/net";

export function billingModule(app) {
    app.get("/billing", () => Results.text("ok"));
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy, Results } from "sloppy";
import { billingModule } from "./modules/billing.ts";

const app = Sloppy.create();
app.useModule(billingModule);
app.get("/health", () => Results.text("ok"));
export default app;
"#;
    let input = root.join("input.ts");
    fs::write(&input, source).expect("fixture input should be writable");
    let app = extract(&input, source).expect("fixture should extract");
    assert!(!app.uses_http_client_runtime);
    assert!(!app.uses_net_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js
        .source
        .contains("const { Results } = __sloppyRuntime;"));
    assert!(!emitted_js.source.contains("HttpClient"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert!(plan.get("requiredFeatures").is_none());
    assert!(plan["features"].get("httpClient").is_none());
    assert!(plan["features"].get("network").is_none());

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn function_module_type_only_sloppy_time_import_does_not_emit_required_feature() {
    let root = fixture_temp_dir("function-module-type-only-time-import");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("jobs.ts"),
        r#"import { Results } from "sloppy";
import type { Deadline } from "sloppy/time";

export function jobsModule(app) {
    app.get("/jobs", () => Results.text("ok"));
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy, Results } from "sloppy";
import { jobsModule } from "./modules/jobs.ts";

const app = Sloppy.create();
app.useModule(jobsModule);
app.get("/health", () => Results.text("ok"));
export default app;
"#;
    let input = root.join("input.ts");
    fs::write(&input, source).expect("fixture input should be writable");
    let app = extract(&input, source).expect("fixture should extract");
    assert!(!app.uses_time_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js
        .source
        .contains("const { Results } = __sloppyRuntime;"));
    assert!(!emitted_js.source.contains("Deadline"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert!(plan.get("requiredFeatures").is_none());
    assert!(plan["features"].get("time").is_none());

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn function_module_invalid_sloppy_time_import_uses_import_diagnostic() {
    let root = fixture_temp_dir("function-module-invalid-time-import");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("jobs.js"),
        r#"import { Results } from "sloppy";
import { Time as Clock } from "sloppy/time";

export function jobsModule(app) {
    app.get("/jobs", () => Results.text("ok"));
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy, Results } from "sloppy";
import { jobsModule } from "./modules/jobs.js";

const app = Sloppy.create();
app.useModule(jobsModule);
app.get("/health", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract_temp_input(&root, source)
        .expect_err("invalid sloppy/time import should be rejected");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_IMPORT");
    assert!(diagnostic
        .message
        .contains("unsupported sloppy import \"Time\""));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn emits_used_function_modules_without_routes() {
    let root = fixture_temp_dir("empty-function-module");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("empty.js"),
        r#"export function emptyModule(app) {
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy, Results } from "sloppy";
import { emptyModule } from "./modules/empty.js";

const app = Sloppy.create();
app.useModule(emptyModule);
app.get("/health", () => Results.text("ok"));
export default app;
"#;
    let app = extract_temp_input(&root, source).expect("fixture should extract");
    assert_eq!(app.routes.len(), 1);
    assert_eq!(app.modules.len(), 1);
    assert_eq!(app.modules[0].name, "emptyModule");
    assert!(app.modules[0].source_name.ends_with("empty.js"));

    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    assert!(plan.contains("\"name\": \"emptyModule\""));
    assert!(plan.contains("empty.js"));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn results_import_detection_uses_handler_ast() {
    assert!(parsed_arrow_requires_results_import(
        "() => Results.json({ ok: true })"
    ));
    assert!(parsed_arrow_requires_results_import(
        "() => Results .json({ ok: true })"
    ));
    assert!(parsed_arrow_requires_results_import(
        "() => Results/*comment*/.json({ ok: true })"
    ));
    assert!(parsed_arrow_requires_results_import(
        "() => Results?.json({ ok: true })"
    ));
    assert!(!parsed_arrow_requires_results_import(
        "() => \"Results.json\""
    ));
    assert!(!parsed_arrow_requires_results_import(
        "() => notResults.json({ ok: true })"
    ));
    assert!(!parsed_arrow_requires_results_import(
        "() => ({ notResults: \"Results.json\" })"
    ));
}

#[test]
fn entry_without_results_import_can_use_results_in_function_module() {
    let root = fixture_temp_dir("module-only-entry-without-results");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("users.js"),
        r#"import { Results } from "sloppy";

export function usersModule(app) {
    app.get("/users", () => Results.json([{ id: "ada" }]));
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy } from "sloppy";
import { usersModule } from "./modules/users.js";

const app = Sloppy.create();
app.useModule(usersModule);
export default app;
"#;
    let app = extract_temp_input(&root, source).expect("module-only entry should extract");

    assert_eq!(app.routes.len(), 1);
    assert_eq!(app.routes[0].pattern, "/users");
    assert_eq!(app.routes[0].module.as_deref(), Some("usersModule"));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn direct_results_handler_requires_results_import_in_same_file() {
    let root = fixture_temp_dir("direct-results-requires-import");
    let source = r#"import { Sloppy } from "sloppy";

const app = Sloppy.create();
app.get("/health", () => Results.json({ ok: true }));
export default app;
"#;
    let diagnostic =
        extract_temp_input(&root, source).expect_err("direct Results handler should fail");

    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_IMPORT");
    assert!(diagnostic.message.contains("call Results"));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn direct_results_handler_with_space_requires_results_import() {
    let root = fixture_temp_dir("direct-results-space-requires-import");
    let source = r#"import { Sloppy } from "sloppy";

const app = Sloppy.create();
app.get("/health", () => Results .json({ ok: true }));
export default app;
"#;
    let diagnostic =
        extract_temp_input(&root, source).expect_err("direct Results handler should fail");

    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_IMPORT");
    assert!(diagnostic.message.contains("call Results"));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn direct_results_handler_with_comment_requires_results_import() {
    let root = fixture_temp_dir("direct-results-comment-requires-import");
    let source = r#"import { Sloppy } from "sloppy";

const app = Sloppy.create();
app.get("/health", () => Results/*comment*/.json({ ok: true }));
export default app;
"#;
    let diagnostic =
        extract_temp_input(&root, source).expect_err("direct Results handler should fail");

    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_IMPORT");
    assert!(diagnostic.message.contains("call Results"));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn middleware_results_handler_requires_results_import_in_same_file() {
    let root = fixture_temp_dir("middleware-results-requires-import");
    let source = r#"import { Sloppy } from "sloppy";

const app = Sloppy.create();
app.use((ctx, next) => Results.status(401));
app.mapHealthChecks({ path: "/health", checks: [] });
export default app;
"#;
    let diagnostic =
        extract_temp_input(&root, source).expect_err("middleware Results usage should fail");

    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_IMPORT");
    assert!(diagnostic.message.contains("call Results"));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn middleware_results_handler_with_comment_requires_results_import() {
    let root = fixture_temp_dir("middleware-results-comment-requires-import");
    let source = r#"import { Sloppy } from "sloppy";

const app = Sloppy.create();
app.use((ctx, next) => Results /* comment */ .status(401));
app.mapHealthChecks({ path: "/health", checks: [] });
export default app;
"#;
    let diagnostic =
        extract_temp_input(&root, source).expect_err("middleware Results usage should fail");

    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_IMPORT");
    assert!(diagnostic.message.contains("call Results"));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn middleware_string_mentioning_results_does_not_require_results_import() {
    let root = fixture_temp_dir("middleware-results-string-no-import");
    let source = r#"import { Sloppy } from "sloppy";

const app = Sloppy.create();
app.use((ctx, next) => {
  const text = "Results.status";
  // Results.status(401) should not affect import validation.
  return next();
});
app.mapHealthChecks({ path: "/health", checks: [] });
export default app;
"#;
    let app = extract_temp_input(&root, source)
        .expect("middleware string/comment mention should not require Results import");
    assert_eq!(app.routes.len(), 3);

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn middleware_results_handler_with_import_compiles() {
    let root = fixture_temp_dir("middleware-results-with-import");
    let source = r#"import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();
app.use((ctx, next) => Results.status(401));
app.get("/health", () => Results.ok({ ok: true }));
export default app;
"#;
    let app = extract_temp_input(&root, source).expect("middleware Results import should compile");
    assert_eq!(app.routes.len(), 1);

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn function_module_results_handler_requires_module_results_import() {
    let root = fixture_temp_dir("module-results-requires-import");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("users.js"),
        r#"export function usersModule(app) {
    app.get("/users", () => Results.json([{ id: "ada" }]));
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy } from "sloppy";
import { usersModule } from "./modules/users.js";

const app = Sloppy.create();
app.useModule(usersModule);
export default app;
"#;
    let diagnostic =
        extract_temp_input(&root, source).expect_err("module Results handler should fail");

    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_IMPORT");
    assert!(diagnostic.message.contains("same source file"));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn function_module_results_import_is_not_source_order_dependent() {
    let root = fixture_temp_dir("module-results-import-after-export");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("users.js"),
        r#"export function usersModule(app) {
    app.get("/users", () => Results.json([{ id: "ada" }]));
}

import { Results } from "sloppy";
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy } from "sloppy";
import { usersModule } from "./modules/users.js";

const app = Sloppy.create();
app.useModule(usersModule);
export default app;
"#;
    let app = extract_temp_input(&root, source)
        .expect("module Results import should be honored regardless of source order");
    assert_eq!(app.routes.len(), 1);
    assert_eq!(app.routes[0].pattern, "/users");

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn rejects_invalid_composed_function_module_route_pattern() {
    let root = fixture_temp_dir("invalid-module-route-pattern");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("users.js"),
        r#"import { Results } from "sloppy";

export function usersModule(app) {
    const api = app.group("/api");
    api.get("/users/", () => Results.text("bad"));
}
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy, Results } from "sloppy";
import { usersModule } from "./modules/users.js";
const app = Sloppy.create();
app.useModule(usersModule);
export default app;
"#;
    let diagnostic =
        extract_temp_input(&root, source).expect_err("invalid module route should fail");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_ROUTE_PATTERN");
    assert!(diagnostic
        .path
        .as_deref()
        .is_some_and(|path| path.ends_with("modules/users.js")));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn rejects_duplicate_method_and_path_routes() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.get("/dupe", () => Results.text("one"));
app.get("/dupe", () => Results.text("two"));
export default app;
"#;
    let diagnostic =
        extract(std::path::Path::new("app.js"), source).expect_err("duplicate routes should fail");
    assert_eq!(diagnostic.code, "SLOPPYC_E_DUPLICATE_ROUTE");
}

#[test]
fn duplicate_module_routes_report_module_source() {
    let root = fixture_temp_dir("duplicate-module-routes");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    let module_path = modules.join("users.js");
    fs::write(
        &module_path,
        r#"import { Results } from "sloppy";

export function usersModule(app) {
    app.get("/dupe", () => Results.text("module"));
}
"#,
    )
    .expect("module fixture should be writable");
    let input = root.join("input.js");
    fs::write(
        &input,
        r#"import { Sloppy, Results } from "sloppy";
import { usersModule } from "./modules/users.js";
const app = Sloppy.create();
app.useModule(usersModule);
app.get("/dupe", () => Results.text("entry"));
export default app;
"#,
    )
    .expect("input fixture should be writable");
    let out_dir = root.join("out");

    let failure = super::build(&input, &out_dir, &CompileOptions::new())
        .expect_err("duplicate route should fail");
    assert_eq!(failure.diagnostic.code, "SLOPPYC_E_DUPLICATE_ROUTE");
    let canonical_module_path =
        fs::canonicalize(&module_path).expect("module path should canonicalize");
    assert_eq!(
        failure.diagnostic.path.as_deref(),
        Some(canonical_module_path.as_path())
    );
    let rendered = failure.diagnostic.render(failure.source.as_deref());
    assert!(rendered.contains("users.js:4:5"), "{rendered}");
    assert!(
        rendered.contains(r#"4 |     app.get("/dupe", () => Results.text("module"));"#),
        "{rendered}"
    );

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn duplicate_module_route_names_report_module_source() {
    let root = fixture_temp_dir("duplicate-module-route-names");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    let module_path = modules.join("users.js");
    fs::write(
        &module_path,
        r#"import { Results } from "sloppy";

export function usersModule(app) {
    app.get("/module", () => Results.text("module")).withName("Users.Get");
}
"#,
    )
    .expect("module fixture should be writable");
    let input = root.join("input.js");
    fs::write(
        &input,
        r#"import { Sloppy, Results } from "sloppy";
import { usersModule } from "./modules/users.js";
const app = Sloppy.create();
app.useModule(usersModule);
app.get("/entry", () => Results.text("entry")).withName("Users.Get");
export default app;
"#,
    )
    .expect("input fixture should be writable");
    let out_dir = root.join("out");

    let failure = super::build(&input, &out_dir, &CompileOptions::new())
        .expect_err("duplicate route name should fail");
    assert_eq!(failure.diagnostic.code, "SLOPPYC_E_DUPLICATE_ROUTE_NAME");
    let canonical_module_path =
        fs::canonicalize(&module_path).expect("module path should canonicalize");
    assert_eq!(
        failure.diagnostic.path.as_deref(),
        Some(canonical_module_path.as_path())
    );
    let rendered = failure.diagnostic.render(failure.source.as_deref());
    assert!(rendered.contains("users.js:4:5"), "{rendered}");
    assert!(
        rendered.contains(
            r#"4 |     app.get("/module", () => Results.text("module")).withName("Users.Get");"#
        ),
        "{rendered}"
    );

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn rejects_missing_module_function_binding() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.useModule(usersModule);
app.get("/health", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), source)
        .expect_err("missing module function should fail");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE");
}

#[test]
fn rejects_wrong_module_export_shape() {
    let root = fixture_temp_dir("wrong-module-shape");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("users.js"),
        r#"export const usersModule = () => {};
"#,
    )
    .expect("module fixture should be writable");
    let source = r#"import { Sloppy, Results } from "sloppy";
import { usersModule } from "./modules/users.js";
const app = Sloppy.create();
app.useModule(usersModule);
app.get("/health", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract_temp_input(&root, source).expect_err("wrong module shape should fail");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE");

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn constant_route_pattern_alias_stays_complete_route_metadata() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
const pattern = "/";
app.mapGet(pattern, () => Results.text("Hello"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("constant route pattern alias should compile");
    assert_eq!(app.routes.len(), 1);
    assert_eq!(app.routes[0].pattern, "/");
    assert!(app.dynamic_routes.is_empty());
}

#[test]
fn dynamic_route_pattern_emits_dynamic_metadata_instead_of_failing() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
function pathFor(name) {
  return `/${name}`;
}
app.mapGet(pathFor("health"), () => Results.text("Hello"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("dynamic route metadata should not fail extraction");
    assert!(app.routes.is_empty());
    assert_eq!(app.dynamic_routes.len(), 1);
    assert_eq!(app.dynamic_routes[0].method, Some("GET"));
    assert!(app.dynamic_routes[0].handler_known);
}

#[test]
fn rejects_static_route_segments_with_stray_braces() {
    assert!(!route_pattern_supported("/foo{bar"));
    assert!(!route_pattern_supported("/a}b"));
    assert!(!route_pattern_supported("/{id{slug}}"));
}

#[test]
fn accepts_supported_http_result_helpers() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.mapGet("/ok", () => Results.ok({ ok: true }));
app.mapGet("/empty", () => Results.noContent());
app.mapGet("/created", () => Results.created("/users/1", { id: 1 }));
app.mapGet("/accepted", () => Results.accepted({ queued: true }));
app.mapGet("/not-found", () => Results.notFound({ error: "missing" }));
app.mapGet("/bad", () => Results.badRequest({ error: "bad" }));
app.mapGet("/status", () => Results.status(202, { accepted: true }));
app.mapGet("/problem", () => Results.problem("broken"));
app.mapGet("/html", () => Results.html("<p>ok</p>"));
app.mapGet("/bytes", () => Results.bytes(new Uint8Array([0, 65, 255]), { contentType: "application/x-test" }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source).expect("fixture should extract");
    assert_eq!(app.routes.len(), 10);
    assert_eq!(app.routes[0].pattern, "/ok");
    assert_eq!(app.routes[1].pattern, "/empty");
    assert_eq!(app.routes[2].pattern, "/created");
    assert_eq!(app.routes[7].pattern, "/problem");
    assert_eq!(app.routes[8].pattern, "/html");
    assert_eq!(app.routes[9].pattern, "/bytes");
}

#[test]
fn accepts_template_literal_result_arguments_from_context() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.mapGet("/apps/{id}/builds", (ctx) => Results.created(`/apps/${ctx.route.id}/builds/b_002`, {
  appId: ctx.route.id,
  status: "queued"
}));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("context-only template literal result should extract");
    assert_eq!(app.routes.len(), 1);
    assert!(app.routes[0]
        .handler
        .source
        .contains("`/apps/${ctx.route.id}/builds/b_002`"));
}

#[test]
fn problem_details_defaults_wraps_compiled_handler_errors() {
    let source = r#"import { Sloppy, Results, ProblemDetails } from "sloppy";
const app = Sloppy.create();
app.mapGet("/boom", () => Results.text("ok"));
app.use(ProblemDetails.defaults());
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source).expect("fixture should extract");
    assert!(app.problem_details.is_some());
    assert_eq!(app.problem_details.as_ref().unwrap().detail, "never");
    assert!(app.routes[0].handler.is_async);
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("SLOPPY_E_HANDLER_ERROR"));
    assert!(app.routes[0]
        .handler
        .responses
        .iter()
        .any(|response| response.helper == "problem" && response.status == 500));

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("ProblemDetails"));
    assert!(emitted_js.source.contains("Internal Server Error"));
}

#[test]
fn map_health_checks_extracts_routes_and_metadata() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.mapHealthChecks({
  path: "/_health",
  livenessPath: "/_live",
  readinessPath: "/_ready",
  checks: [
    function database() { return { ok: true }; },
    { name: "worker", liveness: true, readiness: false, check: () => true },
    { name: "cache", liveness: true, check(ctx) { return ctx !== undefined; } }
  ]
});
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source).expect("fixture should extract");
    assert!(app.uses_health);
    assert_eq!(app.routes.len(), 3);
    assert_eq!(app.routes[0].name.as_deref(), Some("Health"));
    assert_eq!(app.routes[1].name.as_deref(), Some("Health.Liveness"));
    assert_eq!(app.routes[2].name.as_deref(), Some("Health.Readiness"));
    assert_eq!(app.routes[0].pattern, "/_health");
    assert_eq!(app.routes[1].pattern, "/_live");
    assert_eq!(app.routes[2].pattern, "/_ready");
    assert_eq!(
        app.routes[0].health.as_ref().unwrap().checks,
        vec!["database", "worker", "cache"]
    );
    assert_eq!(
        app.routes[1].health.as_ref().unwrap().checks,
        vec!["worker", "cache"]
    );
    assert_eq!(
        app.routes[2].health.as_ref().unwrap().checks,
        vec!["database", "cache"]
    );

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("__sloppy_health_checks"));
    assert!(emitted_js.source.contains("function (ctx)"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert_eq!(plan["features"]["health"], true);
    assert_eq!(plan["strongPlan"]["evidence"]["health"], true);
    assert_eq!(plan["routes"][0]["health"]["kind"], "aggregate");
    assert_eq!(
        plan["routes"][1]["health"]["checks"],
        serde_json::json!(["worker", "cache"])
    );
    assert_eq!(plan["routes"][2]["responses"][1]["status"], 503);
}

#[test]
fn map_health_checks_rejects_unsupported_static_shapes() {
    for source in [
        r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.mapHealthChecks({ path: "/same", livenessPath: "/same" });
export default app;
"#,
        r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.mapHealthChecks({ checks: [{ name: "none", liveness: false, readiness: false, check: () => true }] });
export default app;
"#,
        r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.mapHealthChecks({ checks: [{ name: "bad", check: (ctx: RequestContext) => true }] });
export default app;
"#,
        r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
const ready = true;
app.mapHealthChecks({ checks: [{ name: "captured", check: () => ready }] });
export default app;
"#,
    ] {
        let diagnostic = extract(std::path::Path::new("app.ts"), source)
            .expect_err("unsupported health shape should fail");
        assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS");
    }
}

#[test]
fn extracts_schema_binding_config_and_result_metadata() {
    let source = r#"import { Sloppy, Results, schema } from "sloppy";
const UserCreate = schema.object({
  name: schema.string().min(1),
  tags: schema.array(schema.string()).optional()
});
const app = Sloppy.create();
const host = app.config.getString("Sloppy:Server:Host", "127.0.0.1");
app.post("/users/{id:int}", (ctx) => Results.json({
  id: ctx.route.id,
  search: ctx.query.q,
  agent: ctx.header.userAgent,
  requestId: ctx.header.xRequestId,
  digest: ctx.header.contentMD5,
  body: ctx.body.json(UserCreate)
}));
export default app;
"#;
    let app =
        extract(std::path::Path::new("app.js"), source).expect("metadata fixture should extract");
    assert_eq!(app.schemas.len(), 1);
    assert_eq!(app.schemas[0].name, "UserCreate");
    assert_eq!(app.config_reads.len(), 1);
    assert_eq!(app.config_reads[0].key, "Sloppy:Server:Host");
    assert_eq!(app.routes[0].handler.bindings.len(), 6);
    assert_eq!(
        app.routes[0]
            .handler
            .response
            .as_ref()
            .map(|response| (response.helper.as_str(), response.status)),
        Some(("json", 200))
    );

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("ctx.body.json(undefined)"));
    assert!(!emitted_js.source.contains("ctx.body.json(UserCreate)"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert_eq!(plan["schemas"][0]["name"], "UserCreate");
    assert_eq!(plan["configReads"][0]["key"], "Sloppy:Server:Host");
    assert_eq!(plan["routes"][0]["bindings"][0]["kind"], "route");
    assert_eq!(plan["routes"][0]["bindings"][2]["kind"], "header");
    assert_eq!(plan["routes"][0]["bindings"][2]["name"], "user-agent");
    assert_eq!(plan["routes"][0]["bindings"][3]["kind"], "header");
    assert_eq!(plan["routes"][0]["bindings"][3]["name"], "x-request-id");
    assert_eq!(plan["routes"][0]["bindings"][4]["kind"], "header");
    assert_eq!(plan["routes"][0]["bindings"][4]["name"], "content-md5");
    assert_eq!(plan["routes"][0]["response"]["helper"], "json");
    assert_eq!(plan["features"]["metadataInference"], true);
}

#[test]
fn computed_header_facade_access_materializes_headers_conservatively() {
    let source = r#"const handler = (ctx) => {
  const userAgent = ctx.header["userAgent"];
  return "ok";
};
"#;
    let allocator = oxc_allocator::Allocator::default();
    let source_type = oxc_span::SourceType::from_path(std::path::Path::new("app.js"))
        .expect("fixture source type should be supported");
    let parsed = oxc_parser::Parser::new(&allocator, source, source_type).parse();
    assert!(
        parsed.errors.is_empty(),
        "fixture should parse without errors: {:?}",
        parsed.errors
    );

    let function = parsed
        .program
        .body
        .iter()
        .find_map(|statement| {
            let Statement::VariableDeclaration(declaration) = statement else {
                return None;
            };
            let init = declaration.declarations.first()?.init.as_ref()?;
            let Expression::ArrowFunctionExpression(function) = init else {
                return None;
            };
            Some(function)
        })
        .expect("fixture should contain an arrow handler");
    let bindings = super::request_bindings_from_arrow(function, &BTreeSet::new());

    assert_eq!(bindings.len(), 1);
    assert_eq!(bindings[0].kind, "context");
    assert_eq!(bindings[0].name.as_deref(), Some("RequestContext"));
}

#[test]
fn extracts_bindings_for_named_context_parameter() {
    let source = r#"import { Sloppy, Results, schema } from "sloppy";
const UserCreate = schema.object({ name: schema.string() });
const app = Sloppy.create();
app.post("/users/{id:int}", (request) => Results.json({
  id: request.route.id,
  body: request.body.json(UserCreate)
}));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("named context fixture should extract");
    assert_eq!(app.routes[0].handler.bindings.len(), 2);
    assert_eq!(app.routes[0].handler.bindings[0].kind, "route");
    assert_eq!(
        app.routes[0].handler.bindings[0].name.as_deref(),
        Some("id")
    );
    assert_eq!(app.routes[0].handler.bindings[1].kind, "body.json");
    assert_eq!(
        app.routes[0].handler.bindings[1].schema.as_deref(),
        Some("UserCreate")
    );

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("request.body.json(undefined)"));
    assert!(!emitted_js.source.contains("request.body.json(UserCreate)"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert_eq!(plan["features"]["metadataInference"], true);
    assert_eq!(plan["routes"][0]["response"]["helper"], "json");
}

#[test]
fn extracts_body_schema_declared_after_route() {
    let source = r#"import { Sloppy, Results, schema } from "sloppy";
const app = Sloppy.create();
app.post("/users", (ctx) => Results.json({ body: ctx.body.json(UserCreate) }));
const UserCreate = schema.object({ name: schema.string() });
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("schema name prepass should make route order independent");
    assert_eq!(
        app.routes[0].handler.bindings[0].schema.as_deref(),
        Some("UserCreate")
    );
}

#[test]
fn emits_response_metadata_for_response_only_routes() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.get("/health", () => Results.text("ok"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("response-only fixture should extract");
    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert_eq!(plan["features"]["metadataInference"], true);
    assert_eq!(plan["features"]["strongPlanMetadata"], true);
    assert_eq!(plan["completeness"]["status"], "complete");
    assert_eq!(plan["routes"][0]["completeness"]["status"], "complete");
    assert_eq!(plan["routes"][0]["response"]["helper"], "text");
    assert_eq!(plan["sourceFiles"][0]["path"], "app.js");
    assert_eq!(plan["strongPlan"]["profile"], "compiler-30-strong-plan");
}

#[test]
fn ordinary_handlers_collect_multiple_return_response_metadata() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.get("/users/{id:int}", async (ctx) => {
  const user = await loadUser(ctx.route.id);
  if (user === null) {
    return Results.notFound();
  }
  return Results.ok(user);
});
function loadUser(id) {
  return id === 1 ? { id } : null;
}
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("runnable branched handler should extract response metadata");
    let statuses = app.routes[0]
        .handler
        .responses
        .iter()
        .map(|response| response.status)
        .collect::<Vec<_>>();
    assert_eq!(statuses, vec![404, 200]);
}

#[test]
fn dynamic_status_result_does_not_emit_response_metadata() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.get("/status", (ctx) => Results.status(ctx.route.code, { ok: true }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("dynamic status route should extract");
    assert!(app.routes[0].handler.response.is_none());
    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("partial plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert_eq!(plan["completeness"]["status"], "partial");
    assert_eq!(
        plan["routes"][0]["completeness"]["reasons"][0]["code"],
        "response-metadata-missing"
    );
}

#[test]
fn body_json_without_schema_marks_route_partial() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.post("/users", (ctx) => Results.json({ body: ctx.body.json() }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("body json without schema should extract as partial metadata");
    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("partial body plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert_eq!(plan["routes"][0]["completeness"]["status"], "partial");
    assert_eq!(
        plan["routes"][0]["completeness"]["reasons"][0]["code"],
        "body-schema-missing"
    );
}

#[test]
fn does_not_extract_schema_without_schema_import() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const UserCreate = schema.object({ name: schema.string() });
const app = Sloppy.create();
app.get("/", () => Results.text("ok"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("unbound local schema expression should stay outside Sloppy DSL");
    assert!(app.schemas.is_empty());
}

#[test]
fn rejects_invalid_schema_and_config_metadata() {
    let invalid_schema = r#"import { Sloppy, Results, schema } from "sloppy";
const UserCreate = schema.object(UserShape);
const app = Sloppy.create();
app.get("/", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), invalid_schema)
        .expect_err("invalid schema should fail");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_SCHEMA");

    let invalid_config = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
const key = "Sloppy:Server:Host";
const host = app.config.getString(key);
app.get("/", () => Results.text(host));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), invalid_config)
        .expect_err("invalid config key should fail");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_CONFIG_KEY");

    let invalid_body_helper = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.post("/", (ctx) => Results.json({ form: ctx.body.formData() }));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), invalid_body_helper)
        .expect_err("invalid body helper should fail");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_HANDLER_VALUE");

    let unknown_body_schema = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
const options = {};
app.post("/", (ctx) => Results.json({ body: ctx.body.json(options) }));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), unknown_body_schema)
        .expect_err("unknown body schema identifier should fail");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_HANDLER_VALUE");

    let invalid_schema_modifier = r#"import { Sloppy, Results, schema } from "sloppy";
const UserCreate = schema.object({ email: schema.string().email("strict") });
const app = Sloppy.create();
app.get("/", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), invalid_schema_modifier)
        .expect_err("schema modifier arity should fail");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_SCHEMA");

    let conditional_schema = r#"import { Sloppy, Results, schema } from "sloppy";
const flag = true;
const UserCreate = flag ? schema.string() : schema.number();
const app = Sloppy.create();
app.get("/", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), conditional_schema)
        .expect_err("conditional schema should fail");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_SCHEMA");

    let wrapped_schema = r#"import { Sloppy, Results, schema } from "sloppy";
const UserCreate = wrap(schema.string());
const app = Sloppy.create();
app.get("/", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), wrapped_schema)
        .expect_err("schema hidden in call arguments should fail");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_SCHEMA");

    let object_schema = r#"import { Sloppy, Results, schema } from "sloppy";
const UserCreate = { value: schema.string() };
const app = Sloppy.create();
app.get("/", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), object_schema)
        .expect_err("schema hidden in object values should fail");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_SCHEMA");

    let array_schema = r#"import { Sloppy, Results, schema } from "sloppy";
const UserCreate = [schema.string()][0];
const app = Sloppy.create();
app.get("/", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), array_schema)
        .expect_err("schema hidden in array elements should fail");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_SCHEMA");
}

#[test]
fn extracts_route_metadata_without_runtime_execution() {
    let source = r#"import { Sloppy, Results, data } from "sloppy";
const builder = Sloppy.createBuilder();
builder.capabilities.addDatabase("users.db", { provider: "sqlite", access: "read" });
const app = builder.build();
app.mapPost("/users", async () => Results.json({ ok: true }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source).expect("fixture should extract");
    assert_eq!(app.routes.len(), 1);
    assert_eq!(app.routes[0].method, "POST");
    assert!(app.routes[0].handler.is_async);
    assert_eq!(app.capabilities.len(), 1);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("const { Results, data }"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    assert!(emitted_source_map.contains("\"sourcesContent\""));
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    assert!(plan.contains("\"asyncHandlers\": true"));
    assert!(plan.contains("\"method\": \"POST\""));
    assert!(plan.contains("\"provider\": \"sqlite\""));
}

#[test]
fn data_backed_handlers_may_preserve_runtime_body_shape() {
    let source = r#"import { Sloppy, Results, data } from "sloppy";
const builder = Sloppy.createBuilder();
builder.capabilities.addDatabase("data.main", {
  provider: "sqlite",
  access: "readwrite",
  database: "users-api-sqlite-runtime.db",
});
const app = builder.build();
app.mapPost("/users", (ctx) => {
  const body = ctx.request.json();
  const db = data.sqlite("main");
  try {
    db.exec("create table if not exists users (id integer primary key, name text not null)", []);
    db.exec("insert into users (name) values (?)", [body.name]);
    return Results.created("/users/1", db.queryOne("select id, name from users where id = last_insert_rowid()", []));
  } finally {
    db.close();
  }
});
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("data-backed handler body should be preserved for runtime execution");
    assert_eq!(app.routes.len(), 1);
    assert!(app.routes[0]
        .handler
        .source
        .contains("data.sqlite(\"main\")"));
    assert!(app.routes[0].handler.source.contains("ctx.request.json()"));
    assert_eq!(app.capabilities.len(), 1);
}

#[test]
fn data_backed_body_json_with_extra_arguments_is_not_sanitized() {
    let source = r#"import { Sloppy, Results, data, schema } from "sloppy";
const UserCreate = schema.object({ name: schema.string() });
const opts = {};
const builder = Sloppy.createBuilder();
builder.capabilities.addDatabase("data.main", {
  provider: "sqlite",
  access: "readwrite",
  database: "users-api-sqlite-runtime.db",
});
const app = builder.build();
app.mapPost("/users", (ctx) => Results.json({ body: ctx.body.json(UserCreate, opts) }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("data-backed handler body should be preserved for runtime execution");
    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js
        .source
        .contains("ctx.body.json(UserCreate, opts)"));
    assert!(!emitted_js.source.contains("ctx.body.json(undefined, opts)"));
}

#[test]
fn data_backed_body_schema_references_are_sanitized_in_control_flow() {
    let source = r#"import { Sloppy, Results, data, schema } from "sloppy";
const UserCreate = schema.object({ name: schema.string() });
const builder = Sloppy.createBuilder();
builder.capabilities.addDatabase("data.main", {
  provider: "sqlite",
  access: "readwrite",
  database: "users-api-sqlite-runtime.db",
});
const app = builder.build();
app.mapPost("/users", async (ctx) => {
  const first = await ctx.body.json(UserCreate);
  if ((await ctx.body.json(UserCreate)).name) {
    for (
      let current = await ctx.body.json(UserCreate);
      current.name;
      current = await ctx.body.json(UserCreate)
    ) {
      break;
    }
  }
  try {
    return Results.json({ body: await ctx.body.json(UserCreate), first });
  } catch (error) {
    throw await ctx.body.json(UserCreate);
  }
});
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("data-backed handler body should extract");
    assert_eq!(app.routes[0].handler.bindings.len(), 1);
    assert_eq!(app.routes[0].handler.bindings[0].kind, "body.json");
    assert_eq!(
        app.routes[0].handler.bindings[0].schema.as_deref(),
        Some("UserCreate")
    );

    let emitted_js = super::emit_app_js(&app);
    assert_eq!(
        emitted_js
            .source
            .matches("ctx.body.json(undefined)")
            .count(),
        6
    );
    assert!(!emitted_js.source.contains("ctx.body.json(UserCreate)"));
}

#[test]
fn infers_direct_provider_read_effect_without_manual_uses() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
const db = app.provider("sqlite:main");
app.get("/users", () => Results.json(db.query("select id, name from users", [])));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("direct provider read should infer effects");
    assert_eq!(app.capabilities[0].access, "read");
    assert_eq!(app.routes[0].handler.effects.len(), 1);
    assert_eq!(app.routes[0].handler.effects[0].provider, "data.main");
    assert_eq!(app.routes[0].handler.effects[0].capability_kind, "database");
    assert_eq!(app.routes[0].handler.effects[0].provider_kind, "sqlite");
    assert_eq!(app.routes[0].handler.effects[0].access, "read");
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("__sloppy_open_data_provider(\"sqlite\", \"data.main\")"));

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js
        .source
        .contains("__sloppy_open_data_provider(\"sqlite\", \"data.main\")"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let plan: serde_json::Value = serde_json::from_str(&plan).expect("plan should be json");
    assert_eq!(plan["capabilities"][0]["access"], "read");
    assert_eq!(plan["capabilities"][0]["kind"], "database");
    assert_eq!(plan["dataProviders"][0]["capabilityKind"], "database");
    assert_eq!(plan["dataProviders"][0]["providerKind"], "sqlite");
    assert_eq!(
        plan["routes"][0]["effects"][0]["capabilityKind"],
        "database"
    );
    assert_eq!(plan["routes"][0]["effects"][0]["providerKind"], "sqlite");
    assert_eq!(plan["routes"][0]["effects"][0]["operation"], "query");
}

#[test]
fn infers_provider_write_and_readwrite_effects() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
const db = app.provider("sqlite:main");
app.post("/users", () => {
  db.exec("insert into users (name) values (?)", ["Ada"]);
  return Results.json(db.queryOne("select id, name from users where name = ?", ["Ada"]));
});
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("mixed provider usage should infer readwrite");
    assert_eq!(app.capabilities[0].access, "readwrite");
    assert_eq!(app.routes[0].handler.effects.len(), 2);
    assert!(app.routes[0]
        .handler
        .effects
        .iter()
        .any(|effect| effect.access == "write"));
    assert!(app.routes[0]
        .handler
        .effects
        .iter()
        .any(|effect| effect.access == "read"));
}

#[test]
fn provider_effect_model_is_database_provider_generic() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const builder = Sloppy.createBuilder();
builder.capabilities.addDatabase("data.analytics", { provider: "postgres", access: "readwrite" });
builder.capabilities.addDatabase("data.reporting", { provider: "sqlserver", access: "read" });
const app = builder.build();
const analytics = app.provider("postgres:analytics");
app.get("/analytics", () => Results.json({ ok: true }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("database provider metadata should not be sqlite-only");
    assert_eq!(app.capabilities.len(), 2);
    assert_eq!(app.capabilities[0].provider, "postgres");
    assert_eq!(app.capabilities[1].provider, "sqlserver");
    assert!(app.routes[0].handler.effects.is_empty());
    let binding = super::database_provider_binding_from_token("postgres:analytics")
        .expect("postgres binding should be recognized");
    assert_eq!(binding.capability_kind, "database");
    assert_eq!(binding.provider, "postgres");
    assert_eq!(binding.token, "data.analytics");
}

#[test]
fn rejects_non_sqlite_generated_provider_bridge_until_runtime_exists() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const builder = Sloppy.createBuilder();
builder.capabilities.addDatabase("data.analytics", { provider: "postgres", access: "readwrite" });
const app = builder.build();
const analytics = app.provider("postgres:analytics");
app.get("/analytics", () => Results.json(analytics.query("select id from events", [])));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), source)
        .expect_err("non-sqlite generated bridge should be rejected honestly");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_PROVIDER_BRIDGE");
}

#[test]
fn rejects_provider_effect_without_registered_provider() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
const db = app.provider("sqlite:main");
app.get("/users", () => Results.json(db.query("select id from users", [])));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), source)
        .expect_err("provider effects require a registered provider");
    assert_eq!(diagnostic.code, "SLOPPYC_E_MISSING_PROVIDER");
    assert!(diagnostic.message.contains("database provider"));
}

#[test]
fn infers_same_file_helper_effects_without_manual_uses() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
const db = app.provider("sqlite:main");
function listUsers() {
  return db.query("select id, name from users", []);
}
app.get("/users", () => Results.json(listUsers()));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("same-file helper should infer provider effects");
    assert_eq!(app.capabilities[0].access, "read");
    assert_eq!(app.routes[0].handler.effects.len(), 1);
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("function listUsers()"));
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("__sloppy_open_data_provider(\"sqlite\", \"data.main\")"));
    assert!(!app.routes[0].handler.source.contains("uses"));
}

#[test]
fn helper_identifier_scanner_ignores_strings_comments_and_object_keys() {
    assert!(super::source_contains_identifier(
        "return Results.json(auth());",
        "auth"
    ));
    assert!(!super::source_contains_identifier(
        "return Results.json({ auth: true });",
        "auth"
    ));
    assert!(!super::source_contains_identifier(
        "return Results.text('auth'); // auth\n",
        "auth"
    ));
    assert!(!super::source_contains_identifier(
        "/* auth */ return Results.text(`auth`);",
        "auth"
    ));
    assert!(!super::source_contains_identifier(
        "return Results.json({ auth  : true });",
        "auth"
    ));
}

#[test]
fn same_file_helper_selection_ignores_non_code_identifier_matches() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
function auth() {
  throw new Error("auth helper should not be emitted");
}
app.get("/users", () => {
  // auth is mentioned in a comment only.
  return Results.json({ auth: true, label: "auth", note: `auth` });
});
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("unreferenced helper names in metadata text should not poison extraction");
    assert!(!app.routes[0]
        .handler
        .emitted_source
        .contains("function auth"));
}

#[test]
fn infers_provider_effects_inside_control_flow() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
const db = app.provider("sqlite:main");
app.get("/users", (ctx) => {
  if (ctx.query.write) {
    db.exec("insert into users (name) values (?)", ["Ada"]);
  }
  return Results.json(db.query("select id from users", []));
});
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("control-flow provider calls should infer effects");
    assert_eq!(app.capabilities[0].access, "readwrite");
    assert!(app.routes[0]
        .handler
        .effects
        .iter()
        .any(|effect| effect.access == "write"));
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("__sloppy_open_data_provider(\"sqlite\", \"data.main\")"));
}

#[test]
fn resolves_multi_hop_helper_effects() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
const db = app.provider("sqlite:main");
function readUsers() {
  return db.query("select id from users", []);
}
function listUsers() {
  return readUsers();
}
app.get("/users", () => Results.json(listUsers()));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("multi-hop helper effects should be resolved");
    assert_eq!(app.routes[0].handler.effects.len(), 1);
    assert_eq!(app.routes[0].handler.effects[0].access, "read");
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("__sloppy_open_data_provider(\"sqlite\", \"data.main\")"));
}

#[test]
fn infers_relative_helper_effects_with_provider_arguments() {
    let root = fixture_temp_dir("relative-helper-provider-effects");
    fs::write(
        root.join("usersRepository.ts"),
        r#"export function listUsers(db) {
  return db.query("select id, name from users", []);
}
"#,
    )
    .expect("helper should be writable");
    let source = r#"import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
import { listUsers } from "./usersRepository";
const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
const db = app.provider("sqlite:main");
app.get("/users", () => Results.json(listUsers(db)));
export default app;
"#;
    let app =
        extract_temp_input(&root, source).expect("relative helper should infer provider effects");
    assert_eq!(app.routes[0].handler.effects.len(), 1);
    assert_eq!(app.routes[0].handler.effects[0].access, "read");
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("function listUsers(db)"));
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("__sloppy_open_data_provider(\"sqlite\", \"data.main\")"));
}

#[test]
fn function_module_can_use_relative_provider_helpers() {
    let root = fixture_temp_dir("function-module-relative-provider-helpers");
    fs::create_dir_all(root.join("routes")).expect("routes directory should be writable");
    fs::write(
        root.join("usersRepository.ts"),
        r#"export function listUsers(db) {
  return db.query("select id, name from users", []);
}
"#,
    )
    .expect("repository helper should be writable");
    fs::write(
        root.join("routes").join("users.ts"),
        r#"import { Results } from "sloppy";
import { listUsers } from "../usersRepository";

export function usersModule(app) {
  const db = app.provider("sqlite:main");
  app.get("/users", () => Results.json(listUsers(db)));
}
"#,
    )
    .expect("route module should be writable");
    let source = r#"import { Sloppy } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
import { usersModule } from "./routes/users";
const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
app.useModule(usersModule);
export default app;
"#;
    let app = extract_temp_input(&root, source)
        .expect("function modules should use relative provider helpers");
    assert_eq!(app.routes[0].pattern, "/users");
    assert_eq!(app.routes[0].handler.effects.len(), 1);
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("function listUsers(db)"));
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("__sloppy_open_data_provider(\"sqlite\", \"data.main\")"));
}

#[test]
fn function_module_can_use_installed_package_helpers() {
    let root = fixture_temp_dir("function-module-package-helpers");
    fs::create_dir_all(root.join("src").join("routes"))
        .expect("routes directory should be writable");
    fs::create_dir_all(root.join("node_modules").join("validator-lite"))
        .expect("package directory should be writable");
    fs::write(
        root.join("node_modules")
            .join("validator-lite")
            .join("package.json"),
        r#"{"name":"validator-lite","version":"0.0.0","type":"module","exports":"./index.js"}"#,
    )
    .expect("package manifest should be writable");
    fs::write(
        root.join("node_modules")
            .join("validator-lite")
            .join("index.js"),
        r#"export function normalizeName(value) {
  return String(value || "").trim();
}

export function isUserName(value) {
  return typeof value === "string" && value.length >= 2;
}
"#,
    )
    .expect("package entry should be writable");
    fs::write(
        root.join("src").join("routes").join("users.ts"),
        r#"import { Results } from "sloppy";
import { isUserName, normalizeName } from "validator-lite";

export function usersModule(app) {
  app.get("/users/{name}", (ctx) => {
    const name = normalizeName(ctx.route.name);
    return Results.ok({ name, valid: isUserName(name) });
  });
}
"#,
    )
    .expect("route module should be writable");
    let source = r#"import { Sloppy } from "sloppy";
import { usersModule } from "./routes/users";
const app = Sloppy.create();
app.useModule(usersModule);
export default app;
"#;
    let input = root.join("src").join("main.ts");
    fs::write(&input, source).expect("input should be writable");
    let app = extract(&input, source).expect("function module package helpers should extract");
    assert_eq!(app.routes[0].pattern, "/users/{name}");
    assert!(app.dependency_graph.has_entries());
    assert!(app
        .dependency_graph
        .packages
        .iter()
        .any(|package| package.name == "validator-lite"));
    let users_module = app
        .dependency_graph
        .modules
        .iter()
        .find(|module| module.id.replace('\\', "/").ends_with("routes/users.ts"))
        .expect("route module should be recorded in dependency graph");
    assert!(users_module
        .resolved_imports
        .iter()
        .any(|import| { import.specifier == "validator-lite" && import.kind == "package" }));
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("function normalizeName(value)"));
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("function isUserName(value)"));
}

#[test]
fn function_module_records_cross_file_helper_dependency_graph() {
    let root = fixture_temp_dir("function-module-cross-file-helper-graph");
    fs::create_dir_all(root.join("src").join("routes")).expect("routes directory should exist");
    fs::create_dir_all(root.join("src").join("services")).expect("services directory should exist");
    fs::create_dir_all(root.join("src").join("db")).expect("db directory should exist");
    fs::create_dir_all(root.join("src").join("models")).expect("models directory should exist");
    fs::create_dir_all(root.join("src").join("utils")).expect("utils directory should exist");
    fs::write(
        root.join("src").join("routes").join("users.ts"),
        r#"import { Results } from "sloppy";
import { listUsers } from "../services/usersService";

function describeCount(count) {
  return `users:${count}`;
}

export function usersModule(app) {
  app.get("/users", () => {
    const users = listUsers();
    return Results.ok({ summary: describeCount(users.length), users });
  });
}
"#,
    )
    .expect("route module should be writable");
    fs::write(
        root.join("src").join("services").join("usersService.ts"),
        r#"import { repoListUsers } from "../db/usersRepository";
import { label } from "../utils/text";

export function listUsers() {
  return repoListUsers().map((user) => ({ ...user, label: label(user.name) }));
}
"#,
    )
    .expect("service helper should be writable");
    fs::write(
        root.join("src").join("db").join("usersRepository.ts"),
        r#"import { toUser } from "../models/user";

export function repoListUsers() {
  return [toUser({ id: 1, name: "ada" })];
}
"#,
    )
    .expect("repository helper should be writable");
    fs::write(
        root.join("src").join("models").join("user.ts"),
        r#"import { title } from "../utils/text";

export function toUser(row) {
  return { id: row.id, name: title(row.name) };
}
"#,
    )
    .expect("model helper should be writable");
    fs::write(
        root.join("src").join("utils").join("text.ts"),
        r#"export function title(value) {
  return String(value || "").toUpperCase();
}

export function label(value) {
  return `user:${title(value)}`;
}
"#,
    )
    .expect("utility helper should be writable");
    let source = r#"import { Sloppy } from "sloppy";
import { usersModule } from "./routes/users";

const app = Sloppy.create();
app.useModule(usersModule);
export default app;
"#;
    let input = root.join("src").join("main.ts");
    fs::write(&input, source).expect("input should be writable");
    let app = extract(&input, source)
        .expect("resolved cross-file web app should emit runnable partial metadata");

    assert_eq!(app.routes[0].pattern, "/users");
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("function describeCount(count)"));
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("function repoListUsers()"));
    let modules = &app.dependency_graph.modules;
    let has_edge = |from_suffix: &str, specifier: &str, to_suffix: &str| {
        modules.iter().any(|module| {
            module.id.replace('\\', "/").ends_with(from_suffix)
                && module.resolved_imports.iter().any(|import| {
                    import.specifier == specifier
                        && import.kind == "relative"
                        && import.resolved_id.replace('\\', "/").ends_with(to_suffix)
                })
        })
    };
    assert!(has_edge("main.ts", "./routes/users", "routes/users.ts"));
    assert!(has_edge(
        "routes/users.ts",
        "../services/usersService",
        "services/usersService.ts"
    ));
    assert!(has_edge(
        "services/usersService.ts",
        "../db/usersRepository",
        "db/usersRepository.ts"
    ));
    assert!(has_edge(
        "db/usersRepository.ts",
        "../models/user",
        "models/user.ts"
    ));
    assert!(has_edge("models/user.ts", "../utils/text", "utils/text.ts"));
}

#[test]
fn resolves_nested_relative_helpers_before_effect_inference() {
    let root = fixture_temp_dir("nested-relative-helper-effects");
    fs::write(
        root.join("queries.ts"),
        r#"export const selectUsers = "select id, name from users";"#,
    )
    .expect("query helper should be writable");
    fs::write(
        root.join("usersRepository.ts"),
        r#"import { selectUsers } from "./queries";
export function listUsers(db) {
  return db.query(selectUsers, []);
}
"#,
    )
    .expect("repository helper should be writable");
    let source = r#"import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
import { listUsers } from "./usersRepository";
const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
const db = app.provider("sqlite:main");
app.get("/users", () => Results.json(listUsers(db)));
export default app;
"#;
    let app = extract_temp_input(&root, source)
        .expect("nested relative helpers should be inlined before effect inference");
    assert_eq!(app.routes[0].handler.effects.len(), 1);
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("const selectUsers = \"select id, name from users\";"));
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("function listUsers(db)"));
}

#[test]
fn preindexes_later_function_helpers_before_route_extraction() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
const db = app.provider("sqlite:main");
app.get("/users", () => Results.json(listUsers()));
function listUsers() {
  return db.query("select id from users", []);
}
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("later helper declaration should be indexed before routes");
    assert_eq!(app.routes[0].handler.effects.len(), 1);
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("__sloppy_open_data_provider(\"sqlite\", \"data.main\")"));
}

#[test]
fn rejects_unrelated_closed_over_values_when_provider_exists_elsewhere() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
const db = app.provider("sqlite:main");
const config = { message: "hello" };
app.get("/message", () => Results.text(config.message));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), source)
        .expect_err("unrelated closed-over state should not be accepted");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_HANDLER_VALUE");
}

#[test]
fn rejects_unknown_provider_handle_usage() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
const db = app.provider("sqlite:main");
app.get("/users", () => Results.json(db.prepare("select id from users")));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), source)
        .expect_err("unknown provider method should fail closed");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_HANDLER_VALUE");
}

#[test]
fn infers_provider_effects_inside_expression_wrappers() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
const db = app.provider("sqlite:main");
app.get("/users", (ctx) => Results.json(ctx.query.all ? db.query("select id from users", []) : []));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("conditional provider calls should infer effects");
    assert_eq!(app.routes[0].handler.effects.len(), 1);
    assert_eq!(app.routes[0].handler.effects[0].access, "read");
}

#[test]
fn manual_database_capability_overrides_provider_use_duplicate() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
const builder = Sloppy.createBuilder();
builder.capabilities.addDatabase("data.main", { provider: "sqlite", access: "readwrite", database: ":memory:" });
const app = builder.build();
app.use(sqlite("main", { database: ":memory:" }));
const db = app.provider("sqlite:main");
app.get("/users", () => Results.json(db.query("select id from users", [])));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("manual capability should override synthetic provider use");
    assert_eq!(app.capabilities.len(), 1);
    assert_eq!(app.capabilities[0].access, "readwrite");
    assert!(!app.capabilities[0].from_provider_use);
}

#[test]
fn detects_dynamic_import_inside_function_helper() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
function loadPlugin() {
  return import("./plugin.js");
}
app.get("/", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), source)
        .expect_err("dynamic import in helper should be rejected");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_DYNAMIC_IMPORT");
}

#[test]
fn classifies_with_sql_as_write_by_default() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
const app = Sloppy.create();
app.use(sqlite("main", { database: ":memory:" }));
const db = app.provider("sqlite:main");
app.post("/users", () => {
  db.exec("with input(name) as (values ('Ada')) insert into users(name) select name from input", []);
  return Results.noContent();
});
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("WITH SQL should default to write access");
    assert_eq!(app.routes[0].handler.effects[0].access, "write");
    assert_eq!(app.capabilities[0].access, "write");
}

#[test]
fn database_capability_accepts_matching_path_alias() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const builder = Sloppy.createBuilder();
builder.capabilities.addDatabase("data.main", {
  provider: "sqlite",
  access: "readwrite",
  database: ":memory:",
  path: ":memory:",
});
const app = builder.build();
app.mapGet("/ok", () => Results.ok({ ok: true }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source).expect("fixture should extract");
    assert_eq!(app.capabilities.len(), 1);
    assert_eq!(app.capabilities[0].database.as_deref(), Some(":memory:"));

    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    assert!(plan.contains("\"database\": \":memory:\""));
}

#[test]
fn database_capability_accepts_path_alias_only() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const builder = Sloppy.createBuilder();
builder.capabilities.addDatabase("data.main", {
  provider: "sqlite",
  access: "readwrite",
  path: ":memory:",
});
const app = builder.build();
app.mapGet("/ok", () => Results.ok({ ok: true }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source).expect("fixture should extract");
    assert_eq!(app.capabilities.len(), 1);
    assert_eq!(app.capabilities[0].database.as_deref(), Some(":memory:"));

    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    assert!(plan.contains("\"database\": \":memory:\""));
}

#[test]
fn sloppy_fs_import_emits_plan_required_feature() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { File, Directory, Path } from "sloppy/fs";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("sloppy/fs import should be recognized");
    assert!(app.uses_fs_runtime);

    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");

    assert_eq!(value["requiredFeatures"], serde_json::json!(["stdlib.fs"]));
    assert_eq!(value["features"]["fileSystem"], serde_json::json!(true));
    assert_eq!(
        value["strongPlan"]["evidence"]["filesystem"],
        serde_json::json!(true)
    );
}

#[test]
fn sloppy_time_import_emits_plan_required_feature() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { Time, Deadline, CancellationController } from "sloppy/time";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("sloppy/time import should be recognized");
    assert!(app.uses_time_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains(
            "const { Results, Time, Deadline, CancellationController, TimeoutError, CancelledError, InvalidDeadlineError, TimerDisposedError } = __sloppyRuntime;"
        ));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");

    assert_eq!(
        value["requiredFeatures"],
        serde_json::json!(["stdlib.time"])
    );
    assert_eq!(value["features"]["time"], serde_json::json!(true));
    assert_eq!(
        value["strongPlan"]["evidence"]["time"],
        serde_json::json!(true)
    );
}

#[test]
fn sloppy_crypto_import_emits_plan_required_feature() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { Random, Hash, Hmac, Password, ConstantTime, Secret, NonCryptoHash } from "sloppy/crypto";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("sloppy/crypto import should be recognized");
    assert!(app.uses_crypto_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains(
            "const { Results, Random, Hash, Hmac, Password, ConstantTime, Secret, NonCryptoHash } = __sloppyRuntime;"
        ));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");

    assert_eq!(
        value["requiredFeatures"],
        serde_json::json!(["stdlib.crypto"])
    );
    assert_eq!(value["features"]["crypto"], serde_json::json!(true));
    assert_eq!(
        value["strongPlan"]["evidence"]["crypto"],
        serde_json::json!(true)
    );
}

#[test]
fn sloppy_crypto_noncrypto_hash_security_context_emits_doctor_warning() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { NonCryptoHash } from "sloppy/crypto";
const app = Sloppy.create();
const tokenHash = NonCryptoHash.xxHash64("token");
app.mapGet("/token", () => Results.text("ok"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("sloppy/crypto import should be recognized");
    assert!(app.noncrypto_hash_security_context_visible);

    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");

    assert_eq!(
        value["strongPlan"]["evidence"]["nonCryptoHashSecurityContext"],
        serde_json::json!(true)
    );
    assert_eq!(
        value["doctorChecks"][0]["id"],
        serde_json::json!("stdlib.crypto.noncrypto_hash.security_context")
    );
    assert_eq!(
        value["doctorChecks"][0]["status"],
        serde_json::json!("warn")
    );
}

#[test]
fn sloppy_crypto_noncrypto_hash_security_context_scans_tokens() {
    assert!(noncrypto_hash_security_context_visible(
        r#"const tokenHash = NonCryptoHash . xxHash64(value);"#
    ));
    assert!(!noncrypto_hash_security_context_visible(
        r#"const machineId = NonCryptoHash.xxHash64(value);"#
    ));
    assert!(!noncrypto_hash_security_context_visible(
        r#"const cacheHash = NonCryptoHash.xxHash64("token");"#
    ));
    assert!(!noncrypto_hash_security_context_visible(
        r#"const tokenHash = NonCryptoHashxxHash64(value);"#
    ));
}

#[test]
fn sloppy_crypto_import_alias_is_rejected() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { Random as R } from "sloppy/crypto";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), source)
        .expect_err("aliased sloppy/crypto import should be rejected");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_IMPORT");
    assert!(diagnostic
        .message
        .contains("unsupported sloppy import \"Random\""));
}

#[test]
fn sloppy_net_import_emits_plan_required_feature() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { TcpClient, TcpListener, TcpConnection, LocalEndpoint, UnixSocket, NamedPipe, NetworkAddress, SloppyNetError } from "sloppy/net";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("sloppy/net import should be recognized");
    assert!(app.uses_net_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains(
            "const { Results, TcpClient, TcpListener, TcpConnection, LocalEndpoint, UnixSocket, NamedPipe, NetworkAddress, SloppyNetError } = __sloppyRuntime;"
        ));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");

    assert_eq!(value["requiredFeatures"], serde_json::json!(["stdlib.net"]));
    assert_eq!(value["features"]["network"], serde_json::json!(true));
    assert_eq!(
        value["strongPlan"]["evidence"]["network"],
        serde_json::json!(true)
    );
}

#[test]
fn sloppy_os_import_emits_plan_required_feature() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { System, Environment, Process, ProcessHandle, Signals, OsError } from "sloppy/os";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("sloppy/os import should be recognized");
    assert!(app.uses_os_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js
        .source
        .contains("const { Results, System, Environment, Process, ProcessHandle, Signals, OsError } = __sloppyRuntime;"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");

    assert_eq!(value["requiredFeatures"], serde_json::json!(["stdlib.os"]));
    assert_eq!(value["features"]["os"], serde_json::json!(true));
    assert_eq!(
        value["strongPlan"]["evidence"]["os"],
        serde_json::json!(true)
    );
}

#[test]
fn sloppy_net_http_client_import_emits_http_client_required_feature() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { HttpClient } from "sloppy/net";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("sloppy/net HttpClient import should be recognized");
    assert!(app.uses_http_client_runtime);
    assert!(!app.uses_net_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js
        .source
        .contains("const { Results, HttpClient } = __sloppyRuntime;"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");

    assert_eq!(
        value["requiredFeatures"],
        serde_json::json!(["stdlib.httpclient"])
    );
    assert_eq!(value["features"]["httpClient"], serde_json::json!(true));
    assert_eq!(
        value["strongPlan"]["evidence"]["httpClient"],
        serde_json::json!(true)
    );
    assert_eq!(
        value["doctorChecks"][0]["id"],
        serde_json::json!("stdlib.httpclient.contract")
    );
}

#[test]
fn sloppy_workers_import_emits_plan_required_feature() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { BackgroundService, WorkQueue, WorkerPool, Worker } from "sloppy/workers";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("sloppy/workers import should be recognized");
    assert!(app.uses_workers_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains(
            "const { Results, BackgroundService, WorkQueue, WorkerPool, Worker, WorkerCancellationController, WorkerCancellationSignal, SloppyWorkerError } = __sloppyRuntime;"
        ));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");

    assert_eq!(
        value["requiredFeatures"],
        serde_json::json!(["stdlib.workers"])
    );
    assert_eq!(value["features"]["workers"], serde_json::json!(true));
    assert_eq!(
        value["strongPlan"]["evidence"]["workers"],
        serde_json::json!(true)
    );
    assert!(value.get("workers").is_none());
    assert!(value.get("doctorChecks").is_none());
}

#[test]
fn sloppy_workers_type_only_import_does_not_require_runtime_feature() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import type { WorkerPool } from "sloppy/workers";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("type-only sloppy/workers import should be recognized");
    assert!(!app.uses_workers_runtime);
    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");
    assert!(value.get("requiredFeatures").is_none());
}

#[test]
fn typed_framework_queue_injection_infers_capability_and_default_service() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import type { WorkQueue } from "sloppy/workers";
const app = Sloppy.create();
app.post("/emails", async (emails: WorkQueue<"emails">) => Results.ok({ ok: true }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("typed queue injection should extract");
    assert!(app.uses_workers_runtime);
    assert!(app.capabilities.iter().any(|capability| {
        capability.token == "queue.emails"
            && capability.capability_kind == "queue"
            && capability.access == "enqueue"
    }));

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains(
            "__sloppy_framework_services.addSingleton(\"queue.emails\", () => WorkQueue.create(\"emails\"));"
        ));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");
    assert_eq!(
        value["requiredFeatures"],
        serde_json::json!(["stdlib.workers"])
    );
    assert_eq!(
        value["capabilities"][0],
        serde_json::json!({
            "access": "enqueue",
            "kind": "queue",
            "source": {
                "column": 28,
                "line": 4,
                "path": "app.ts"
            },
            "token": "queue.emails"
        })
    );
}

#[test]
fn sloppy_codec_import_emits_plan_required_feature() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { Base64, Base64Url, Hex, Text, Binary, Compression, Checksums } from "sloppy/codec";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("sloppy/codec import should be recognized");
    assert!(app.uses_codec_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains(
            "const { Results, Base64, Base64Url, Hex, Text, Binary, Compression, Checksums } = __sloppyRuntime;"
        ));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");

    assert_eq!(
        value["requiredFeatures"],
        serde_json::json!(["stdlib.codec"])
    );
    assert_eq!(value["features"]["codec"], serde_json::json!(true));
    assert_eq!(
        value["strongPlan"]["evidence"]["codec"],
        serde_json::json!(true)
    );
}

#[test]
fn sloppy_codec_checksum_security_context_emits_doctor_warning() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { Checksums } from "sloppy/codec";
const app = Sloppy.create();
const tokenChecksum = Checksums.crc32(Text.utf8.encode("token"));
app.mapGet("/token", () => Results.text("ok"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("sloppy/codec import should be recognized");
    assert!(app.checksum_security_context_visible);

    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");

    assert_eq!(
        value["strongPlan"]["evidence"]["checksumSecurityContext"],
        serde_json::json!(true)
    );
    assert_eq!(
        value["doctorChecks"][0]["id"],
        serde_json::json!("stdlib.codec.checksum.security_context")
    );
    assert_eq!(
        value["doctorChecks"][0]["status"],
        serde_json::json!("warn")
    );
}

#[test]
fn sloppy_codec_checksum_security_context_flows_from_relative_module() {
    let root = fixture_temp_dir("codec-checksum-module");
    let modules = root.join("modules");
    fs::create_dir_all(&modules).expect("modules directory should be created");
    fs::write(
        modules.join("checks.js"),
        r#"import { Results } from "sloppy";
import { Checksums } from "sloppy/codec";

export function tokenChecksumModule(app) {
    const tokenChecksum = Checksums.crc32(new Uint8Array([1, 2, 3]));
    app.get("/token-checksum", () => Results.text("ok"));
}
"#,
    )
    .expect("module fixture should be writable");

    let source = r#"import { Sloppy, Results } from "sloppy";
import { tokenChecksumModule } from "./modules/checks.js";

const app = Sloppy.create();
app.useModule(tokenChecksumModule);
export default app;
"#;
    let app = extract_temp_input(&root, source).expect("fixture should extract");
    assert!(app.uses_codec_runtime);
    assert!(app.checksum_security_context_visible);

    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");
    assert_eq!(
        value["strongPlan"]["evidence"]["checksumSecurityContext"],
        serde_json::json!(true)
    );
    assert!(value["doctorChecks"]
        .as_array()
        .is_some_and(|checks| checks.iter().any(|check| check["id"]
            == serde_json::json!("stdlib.codec.checksum.security_context")
            && check["status"] == serde_json::json!("warn"))));

    fs::remove_dir_all(&root).expect("test directory should be removable");
}

#[test]
fn sloppy_codec_checksum_security_context_scans_tokens() {
    assert!(checksum_security_context_visible(
        r#"const tokenChecksum = Checksums . crc32(value);"#
    ));
    assert!(!checksum_security_context_visible(
        r#"const tokenChecksum = checksums.crc32(value);"#
    ));
    assert!(!checksum_security_context_visible(
        r#"const cacheChecksum = Checksums.crc32(value);"#
    ));
    assert!(!checksum_security_context_visible(
        r#"const cacheChecksum = Checksums.crc32("token");"#
    ));
    assert!(!checksum_security_context_visible(
        r#"const tokenChecksum = Checksumscrc32(value);"#
    ));
}

#[test]
fn sloppy_codec_import_alias_is_rejected() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { Base64 as B64 } from "sloppy/codec";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), source)
        .expect_err("aliased sloppy/codec import should be rejected");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_IMPORT");
    assert!(diagnostic
        .message
        .contains("unsupported sloppy import \"Base64\""));
}

#[test]
fn type_only_sloppy_net_import_does_not_emit_runtime_feature() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import type { TcpClient } from "sloppy/net";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("type-only sloppy/net import should be recognized");
    assert!(!app.uses_net_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js
        .source
        .contains("const { Results } = __sloppyRuntime;"));
    assert!(!emitted_js.source.contains("TcpClient"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");

    assert!(value.get("requiredFeatures").is_none());
    assert!(value["features"].get("network").is_none());
    assert!(value["strongPlan"]["evidence"].get("network").is_none());
}

#[test]
fn root_sloppy_import_rejects_net_only_exports() {
    let source = r#"import { Sloppy, Results, TcpClient } from "sloppy";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), source)
        .expect_err("root sloppy import should reject net-only exports");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_IMPORT");
    assert!(diagnostic
        .message
        .contains("unsupported sloppy import \"TcpClient\""));
}

#[test]
fn type_only_sloppy_os_import_does_not_emit_runtime_feature() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import type { Process } from "sloppy/os";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("type-only sloppy/os import should be recognized");
    assert!(!app.uses_os_runtime);

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js
        .source
        .contains("const { Results } = __sloppyRuntime;"));
    assert!(!emitted_js.source.contains("Process"));
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value = serde_json::from_str(&plan).expect("valid plan JSON");

    assert!(value.get("requiredFeatures").is_none());
    assert!(value["features"].get("os").is_none());
    assert!(value["strongPlan"]["evidence"].get("os").is_none());
}

#[test]
fn sloppy_net_import_alias_is_rejected() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { TcpClient as Client } from "sloppy/net";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), source)
        .expect_err("aliased sloppy/net import should be rejected");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_IMPORT");
    assert!(diagnostic
        .message
        .contains("unsupported sloppy import \"TcpClient\""));
}

#[test]
fn sloppy_os_import_alias_is_rejected() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import { Process as ChildProcess } from "sloppy/os";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), source)
        .expect_err("aliased sloppy/os import should be rejected");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_IMPORT");
    assert!(diagnostic
        .message
        .contains("unsupported sloppy import \"Process\""));
}

#[test]
fn side_effect_sloppy_fs_import_is_rejected() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import "sloppy/fs";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), source)
        .expect_err("side-effect sloppy/fs import should be rejected");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER");
    assert!(diagnostic
        .message
        .contains("unsupported import specifier \"sloppy/fs\""));
}

#[test]
fn side_effect_sloppy_time_import_is_rejected() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import "sloppy/time";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), source)
        .expect_err("side-effect sloppy/time import should be rejected");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER");
    assert!(diagnostic
        .message
        .contains("unsupported import specifier \"sloppy/time\""));
}

#[test]
fn empty_named_sloppy_fs_import_is_rejected() {
    let source = r#"import { Sloppy, Results } from "sloppy";
import {} from "sloppy/fs";
const app = Sloppy.create();
app.mapGet("/", () => Results.text("ok"));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), source)
        .expect_err("empty named sloppy/fs import should be rejected");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER");
    assert!(diagnostic
        .message
        .contains("unsupported import specifier \"sloppy/fs\""));
}

#[test]
fn database_capability_rejects_mismatched_path_alias() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const builder = Sloppy.createBuilder();
builder.capabilities.addDatabase("data.main", {
  provider: "sqlite",
  access: "readwrite",
  database: ":memory:",
  path: "app.db",
});
const app = builder.build();
app.mapGet("/ok", () => Results.ok({ ok: true }));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), source)
        .expect_err("mismatched database/path alias should fail");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_CAPABILITY_SHAPE");
    assert_eq!(
        diagnostic.message,
        "database capability cannot declare different database and path values"
    );
}

#[test]
fn extracts_function_module_routes_and_provider_metadata() {
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
    let input = root.join("tests/fixtures/function-module/input.js");
    let source = fs::read_to_string(&input).expect("fixture input should exist");
    let app = extract(&input, &source).expect("function module fixture should extract");

    assert_eq!(app.routes.len(), 2);
    assert_eq!(app.routes[0].pattern, "/health");
    assert_eq!(app.routes[1].pattern, "/users");
    assert_eq!(app.routes[1].module.as_deref(), Some("usersModule"));
    assert_eq!(app.capabilities.len(), 1);
    assert_eq!(app.capabilities[0].token, "data.main");

    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("const { Results, data }"));
    assert!(emitted_js
        .source
        .contains("__sloppy_open_data_provider(\"sqlite\", \"data.main\")"));

    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    assert!(emitted_source_map.contains("\"users.js\""));
    assert!(emitted_source_map.contains("\"input.js\""));

    let plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    assert!(plan.contains("\"module\": \"usersModule\""));
    assert!(plan.contains("\"path\": \"users.js\""));
}

#[test]
fn extracts_multiple_function_modules_from_same_file() {
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
    let input = root.join("tests/fixtures/function-module-same-file/input.js");
    let source = fs::read_to_string(&input).expect("fixture input should exist");
    let app = extract(&input, &source).expect("same-file function modules should extract");

    assert_eq!(app.routes.len(), 2);
    assert_eq!(app.routes[0].pattern, "/health");
    assert_eq!(app.routes[0].module.as_deref(), Some("healthModule"));
    assert_eq!(app.routes[1].pattern, "/users");
    assert_eq!(app.routes[1].module.as_deref(), Some("usersModule"));
}

#[test]
fn typed_framework_metadata_fixture_expected_outputs_stay_current() {
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
    let fixture_name = "framework-metadata";
    let fixture = root
        .join("tests/fixtures")
        .join(fixture_name)
        .join("input.ts");
    let source = fs::read_to_string(&fixture).expect("fixture input should exist");
    let mut app = extract(&fixture, &source).expect("framework fixture should extract");
    super::ConfigurationModel::load(&fixture, &CompileOptions::new(), &app.config_reads)
        .expect("fixture configuration should load")
        .apply_to_app(&mut app)
        .expect("fixture configuration should apply");

    let emitted_js = super::emit_app_js(&app);
    let expected_js = fs::read_to_string(
        root.join("tests/fixtures")
            .join(fixture_name)
            .join("expected/app.js"),
    )
    .expect("expected app.js should exist");
    assert_eq!(emitted_js.source, expected_js, "{fixture_name} app.js");

    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let emitted_plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let expected_plan = fs::read_to_string(
        root.join("tests/fixtures")
            .join(fixture_name)
            .join("expected/app.plan.json"),
    )
    .expect("expected app.plan.json should exist");
    assert_eq!(emitted_plan, expected_plan, "{fixture_name} app.plan.json");

    let expected_source_map = fs::read_to_string(
        root.join("tests/fixtures")
            .join(fixture_name)
            .join("expected/app.js.map"),
    )
    .expect("expected app.js.map should exist");
    assert_eq!(
        emitted_source_map, expected_source_map,
        "{fixture_name} app.js.map"
    );
}

#[test]
fn app_graph_dogfood_fixture_expected_outputs_stay_current() {
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
    for fixture_name in ["app-graph-dogfood", "full-framework-app-graph"] {
        let fixture = root
            .join("tests/fixtures")
            .join(fixture_name)
            .join("input.ts");
        let source = fs::read_to_string(&fixture).expect("fixture input should exist");
        let mut app = extract(&fixture, &source).expect("app graph dogfood fixture should extract");
        super::ConfigurationModel::load(&fixture, &CompileOptions::new(), &app.config_reads)
            .expect("fixture configuration should load")
            .apply_to_app(&mut app)
            .expect("fixture configuration should apply");

        let emitted_js = super::emit_app_js(&app);
        let expected_js = fs::read_to_string(
            root.join("tests/fixtures")
                .join(fixture_name)
                .join("expected/app.js"),
        )
        .expect("expected app.js should exist");
        assert_eq!(emitted_js.source, expected_js, "{fixture_name} app.js");

        let emitted_source_map = super::emit_source_map(&app, &emitted_js);
        let emitted_plan = super::emit_plan(
            &app,
            &super::sha256_hex(&emitted_js.source),
            &super::sha256_hex(&emitted_source_map),
        )
        .expect("plan should emit");
        let expected_plan = fs::read_to_string(
            root.join("tests/fixtures")
                .join(fixture_name)
                .join("expected/app.plan.json"),
        )
        .expect("expected app.plan.json should exist");
        assert_eq!(emitted_plan, expected_plan, "{fixture_name} app.plan.json");

        let expected_source_map = fs::read_to_string(
            root.join("tests/fixtures")
                .join(fixture_name)
                .join("expected/app.js.map"),
        )
        .expect("expected app.js.map should exist");
        assert_eq!(
            emitted_source_map, expected_source_map,
            "{fixture_name} app.js.map"
        );
    }
}

#[test]
fn typed_framework_negative_diagnostics_are_source_aware() {
    for (source, code) in [
        (
            r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.get("/users", (id: number) => Results.ok({ id }));
export default app;
"#,
            "SLOPPYC_E_AMBIGUOUS_BINDING",
        ),
        (
            r#"import { Sloppy, Results } from "sloppy";
type BodyA = { name: string };
type BodyB = { name: string };
const app = Sloppy.create();
app.post("/users", (a: BodyA, b: BodyB) => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_MULTIPLE_BODY_BINDINGS",
        ),
        (
            r#"import { Sloppy, Results, Header } from "sloppy";
const app = Sloppy.create();
app.get("/users", (trace: Header<string>) => Results.ok({ trace }));
export default app;
"#,
            "SLOPPYC_E_UNSUPPORTED_HEADER_BINDING",
        ),
        (
            r#"import { Sloppy, Results } from "sloppy";
import { Postgres } from "sloppy/providers/postgres";
const app = Sloppy.create();
app.get("/users", (db: Postgres<string>) => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_DYNAMIC_PROVIDER_NAME",
        ),
        (
            r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.get("/users", (db: Mongo<"main">) => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_UNKNOWN_INJECTION_MARKER",
        ),
        (
            r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.post("/users", (input: MissingModel) => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_UNRESOLVED_TYPE",
        ),
        (
            r#"import { Sloppy, Results } from "sloppy";
type Body = { child: MissingModel };
const app = Sloppy.create();
app.post("/users", (input: Body) => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_UNRESOLVED_TYPE",
        ),
        (
            r#"import { Sloppy, Results, Body } from "sloppy";
const app = Sloppy.create();
app.post("/users", (input: Body<MissingModel>) => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_UNRESOLVED_TYPE",
        ),
        (
            r#"import { Sloppy, Results, Body } from "sloppy";
type BodyA = { name: string };
type BodyB = { note: string };
const app = Sloppy.create();
app.post("/users", (a: Body<BodyA>, b: Body<BodyB>) => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_MULTIPLE_BODY_BINDINGS",
        ),
        (
            r#"import { Sloppy, Results, Body } from "sloppy";
type BodyA = { name: string };
type BodyB = { note: string };
const app = Sloppy.create();
app.post("/users", (a: Body<BodyA>, b: BodyB) => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_MULTIPLE_BODY_BINDINGS",
        ),
        (
            r#"import { Sloppy, Results } from "sloppy";
type UserCreate = { name: string };
const app = Sloppy.create();
app.post("/users", (input: Paginated<UserCreate>) => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_UNRESOLVED_TYPE",
        ),
        (
            r#"import { Sloppy, Results, Route } from "sloppy";
const app = Sloppy.create();
app.get("/users/:id", (routeId: Route<number>) => Results.ok({ routeId }));
export default app;
"#,
            "SLOPPYC_E_ROUTE_BINDING_MISMATCH",
        ),
        (
            r#"import { Sloppy, Results } from "sloppy";
type Maybe<T> = T extends string ? string : number;
const app = Sloppy.create();
app.get("/", () => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_UNSUPPORTED_TYPESCRIPT_SCHEMA",
        ),
        (
            r#"import { Sloppy, Results } from "sloppy";
type Node = { next: Node };
const app = Sloppy.create();
app.get("/", () => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_UNSUPPORTED_TYPESCRIPT_SCHEMA",
        ),
    ] {
        let diagnostic = extract(std::path::Path::new("app.ts"), source)
            .expect_err("unsupported framework source should fail");
        assert_eq!(diagnostic.code, code);
        assert!(
            diagnostic.span.is_some(),
            "{code} should include a source span"
        );
    }
}

#[test]
fn typed_framework_service_wrapper_emits_service_metadata() {
    let source = r#"import { Sloppy, Results, Service } from "sloppy";
const app = Sloppy.create();
app.get("/users", (users: Service<UserService>) => Results.ok({ ok: true }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("explicit service wrapper should extract metadata");
    let binding = app.routes[0]
        .handler
        .bindings
        .iter()
        .find(|binding| binding.parameter.as_deref() == Some("users"))
        .expect("service wrapper binding should be present");
    assert_eq!(binding.kind, "injection");
    assert_eq!(binding.wrapper.as_deref(), Some("Service"));
    assert_eq!(binding.injection_kind.as_deref(), Some("service"));
    assert_eq!(binding.name.as_deref(), Some("UserService"));
}

#[test]
fn framework_service_registration_rejects_captured_factory_identifiers() {
    let source = r#"import { Sloppy, Results } from "sloppy";
function makeGreeting() {
  return { prefix: "hello" };
}
const app = Sloppy.create();
app.services.addScoped("GreetingService", () => makeGreeting());
app.get("/users", () => Results.ok({ ok: true }));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.ts"), source)
        .expect_err("captured service factory identifiers should be rejected");
    assert_eq!(
        diagnostic.code,
        "SLOPPYC_E_UNSUPPORTED_SERVICE_REGISTRATION"
    );
    assert!(diagnostic.message.contains("makeGreeting"));
}

#[test]
fn builder_service_registration_extracts_service_factory() {
    let source = r#"import { Sloppy, Results, Service } from "sloppy";
type GreetingService = { greeting: string };
const builder = Sloppy.createBuilder();
builder.services.addSingleton("GreetingService", () => ({ greeting: "hello" }));
const app = builder.build();
app.get("/users", (service: Service<GreetingService>) => Results.ok({ service }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("builder service registrations should extract");
    assert_eq!(app.service_registrations.len(), 1);
    assert_eq!(app.service_registrations[0].token, "GreetingService");
    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains(
        "__sloppy_framework_services.addSingleton(\"GreetingService\", () => ({ greeting: \"hello\" }));"
    ));
}

#[test]
fn typed_config_injection_uses_plan_default_when_environment_is_absent() {
    let source = r#"import { Sloppy, Results, Config } from "sloppy";
const app = Sloppy.create();
const requiredGreeting = app.config.getString("App:Greeting");
const greeting = app.config.getString("App:Greeting", "hello");
app.get("/", (message: Config<"App:Greeting">) => Results.ok({ message }));
export default app;
"#;
    let app =
        extract(std::path::Path::new("app.ts"), source).expect("config default should extract");
    assert!(app
        .config_reads
        .iter()
        .any(|read| read.key == "App:Greeting"
            && read.default_value == Some(serde_json::json!("hello"))));
    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains(
        "const __sloppy_framework_config_defaults = new Map([[\"App:Greeting\", \"hello\"]]);"
    ));
    assert!(emitted_js.source.contains(
        "if (__sloppy_framework_config_defaults.has(binding.name)) { return __sloppy_framework_config_defaults.get(binding.name); }"
    ));
}

#[test]
fn static_framework_surfaces_are_lowered_into_generated_handlers() {
    let source = r#"import { Sloppy, Results, RequestId, RequestLogging } from "sloppy";
class UsersController {
  static inject = ["Repo"];
  constructor(repo) {
    this.repo = repo;
  }
  list(ctx) {
    return Results.ok({ users: this.repo.list(), requestId: ctx.requestId });
  }
}
const builder = Sloppy.createBuilder();
builder.services.addSingleton("Repo", () => ({ list: () => ["ada"] }));
const app = builder.build();
function requireAuth(ctx, next) {
  if (ctx.request.headers.get("authorization") !== "Bearer test") {
    return Results.status(401);
  }
  return next();
}
app.use(RequestId.defaults({ header: "x-request-id", responseHeader: true, trustIncoming: true }));
app.use(RequestLogging.defaults({ includeRoute: true, includeDuration: false, includeRequestId: true }));
app.use(requireAuth);
app.useCors({
  origins: ["https://app.example.com"],
  methods: ["GET"],
  headers: ["authorization"],
  exposedHeaders: ["x-request-id"],
  credentials: true,
  maxAgeSeconds: 600,
});
const api = app.group("/api");
api.use((ctx, next) => next());
api.get("/status", () => Results.ok({ ok: true })).withName("Status");
app.mapController("/users", UsersController, (mapper) => {
  mapper.get("/", "list", { tags: ["Users"] }).withName("Users.List");
});
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("static framework surfaces should extract");
    assert_eq!(
        app.routes.len(),
        4,
        "two GET routes plus two CORS preflight routes"
    );
    assert!(app
        .routes
        .iter()
        .any(|route| route.method == "OPTIONS" && route.pattern == "/api/status"));
    assert!(app
        .routes
        .iter()
        .any(|route| route.method == "OPTIONS" && route.pattern == "/users"));
    let status = app
        .routes
        .iter()
        .find(|route| route.method == "GET" && route.pattern == "/api/status")
        .expect("status route should exist");
    assert_eq!(status.middleware.len(), 4);
    assert!(status.cors.is_some());
    let controller = app
        .routes
        .iter()
        .find(|route| route.name.as_deref() == Some("Users.List"))
        .expect("controller route should exist");
    assert_eq!(controller.tags, vec!["Users"]);
    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("__sloppy_run_middleware"));
    assert!(emitted_js.source.contains("__sloppy_cors_preflight"));
    assert!(emitted_js.source.contains("__sloppy_request_id"));
    assert!(emitted_js.source.contains("__sloppy_request_logging"));
    assert!(emitted_js.source.contains("new UsersController"));
}

#[test]
fn unsupported_dynamic_framework_features_fail_closed() {
    for (source, code) in [
        (
            r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
const middleware = [(_ctx, next) => next()];
app.use(middleware[0]);
app.get("/", () => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_UNSUPPORTED_MIDDLEWARE",
        ),
        (
            r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
const policy = { origins: ["https://app.example.com"] };
const app = Sloppy.create();
app.useCors(policy);
app.get("/", () => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_UNSUPPORTED_CORS",
        ),
        (
            r#"import { Sloppy, Results, RequestId } from "sloppy";
const app = Sloppy.create();
app.use(RequestId.defaults({ generator: () => "req-1" }));
app.get("/", () => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_UNSUPPORTED_REQUEST_ID",
        ),
        (
            r#"import { Sloppy, Results, RequestLogging } from "sloppy";
const includeRoute = true;
const app = Sloppy.create();
app.use(RequestLogging.defaults({ includeRoute }));
app.get("/", () => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_UNSUPPORTED_REQUEST_LOGGING",
        ),
        (
            r#"import { Sloppy, Results } from "sloppy";
class UsersController {}
const app = Sloppy.create();
app.controller("/users", UsersController, (mapper) => {
  mapper.get("/", "missing");
});
app.get("/", () => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
        ),
        (
            r#"import { Sloppy, Results } from "sloppy";
const controller = class UsersController {};
const app = Sloppy.create();
app.mapController("/users", controller, (mapper) => {
  mapper.get("/", "list");
});
app.get("/", () => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
        ),
        (
            r#"import { Sloppy, Results, Testing } from "sloppy";
const app = Sloppy.create();
app.get("/", () => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_UNSUPPORTED_TESTING_IMPORT",
        ),
        (
            r#"import { Sloppy, Results, Testing as TestHost } from "sloppy";
const app = Sloppy.create();
app.get("/", () => Results.ok({ ok: true }));
export default app;
"#,
            "SLOPPYC_E_UNSUPPORTED_TESTING_IMPORT",
        ),
    ] {
        let diagnostic = extract(std::path::Path::new("app.ts"), source)
            .expect_err("recognized unsupported framework surface should fail");
        assert_eq!(diagnostic.code, code);
    }
}

#[test]
fn typed_framework_body_bindings_are_awaited_before_handler_entry() {
    let source = r#"import { Sloppy, Results, Body } from "sloppy";
type UserCreate = { name: string; email: string };
const app = Sloppy.create();
app.post("/users", (input: Body<UserCreate>) => Results.created(`/users/${input.email}`, {
  name: input.name,
  email: input.email,
}));
export default app;
"#;
    let app =
        extract(std::path::Path::new("app.ts"), source).expect("typed body handler should extract");
    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js
        .source
        .contains("const __sloppy_args = await Promise.all(["));
    assert!(emitted_js.source.contains("ctx.request.json()"));
    assert!(emitted_js
        .source
        .contains("return await __sloppy_typed_handler(...__sloppy_args);"));
    assert!(!emitted_js
        .source
        .contains("return await __sloppy_typed_handler(__sloppy_framework_arg"));
}

#[test]
fn typed_framework_handlers_emit_same_file_helpers() {
    let source = r#"import { Sloppy, Results, RequestContext } from "sloppy";
import { Sqlite } from "sloppy/providers/sqlite";
const app = Sloppy.create();
async function seedUsers(db, ctx) {
  await db.exec("create table users (id integer primary key, name text)", [], {
    signal: ctx.signal,
    deadline: ctx.deadline,
  });
}
app.get("/users", async (db: Sqlite<"main">, ctx: RequestContext) => {
  await seedUsers(db, ctx);
  return Results.ok(await db.query("select id, name from users", []));
});
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("typed handler helper should extract");
    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js
        .source
        .contains("async function seedUsers(db, ctx)"));
    assert!(emitted_js.source.contains("await seedUsers(db, ctx);"));
    assert!(emitted_js
        .source
        .contains("return await __sloppy_typed_handler(...__sloppy_args);"));
}

#[test]
fn typed_framework_handler_erases_nested_typescript_syntax() {
    let source = r#"import { Sloppy, Results, Body } from "sloppy";
import { sql } from "sloppy/data";
import { Postgres } from "sloppy/providers/postgres";
type UserCreate = { name: string; email: string };
type UserDto = { id: number; name: string; email: string };
const app = Sloppy.create();
app.post("/users", async (input: Body<UserCreate>, db: Postgres<"main">) => {
  const first: UserCreate = input! as UserCreate;
  const checked = ({ name: first.name, email: first.email } satisfies UserCreate);
  const mapped = [first].map((item: UserCreate): UserDto => ({
    id: Number.parseInt("1", 10),
    name: item.name,
    email: item.email,
  }));
  const loaded = await Promise.all<UserDto>(mapped.map(async (item: UserDto): Promise<UserDto> => {
    const row: UserDto = await db.queryOne<UserDto>("select id, name, email from users where id = $1", [item.id]);
    return row;
  }));
  const typedQuery = sql`select ${input! as UserCreate} where id = ${loaded[0]!.id}`;
  function normalize(user: UserDto): UserDto {
    return user;
  }
  return Results.created(`/users/${loaded[0]!.id}`, normalize(loaded[0]));
});
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("typed handler with nested TypeScript syntax should extract");
    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js.source.contains("const first = input;"));
    assert!(emitted_js
        .source
        .contains("const checked = ({ name: first.name, email: first.email });"));
    assert!(emitted_js.source.contains("function normalize(user)"));
    assert!(!emitted_js.source.contains("const first:"));
    assert!(!emitted_js.source.contains(" as UserCreate"));
    assert!(!emitted_js.source.contains(" satisfies UserCreate"));
    assert!(!emitted_js.source.contains("input!"));
    assert!(!emitted_js.source.contains("loaded[0]!"));
    assert!(!emitted_js.source.contains("(item:"));
    assert!(!emitted_js.source.contains("): UserDto"));
    assert!(!emitted_js.source.contains("function normalize(user:"));
    assert!(!emitted_js.source.contains("Promise.all<UserDto>"));
    assert!(!emitted_js.source.contains("queryOne<UserDto>"));
    assert!(emitted_js
        .source
        .contains("const typedQuery = sql`select ${input} where id = ${loaded[0].id}`;"));
    assert!(!emitted_js.source.contains("input! as UserCreate"));
    assert!(!emitted_js.source.contains("loaded[0]!.id"));
}

#[test]
fn typed_framework_source_maps_point_at_user_handler_source() {
    let source = r#"import { Sloppy, Results, Body } from "sloppy";
type UserCreate = { name: string };
const app = Sloppy.create();
app.post("/users", async (input: Body<UserCreate>) => {
  const name: string = input.name;
  return Results.created(`/users/${name}`, { name });
});
export default app;
"#;
    let app =
        extract(std::path::Path::new("app.ts"), source).expect("typed handler should extract");
    let emitted_js = super::emit_app_js(&app);
    assert!(app.routes[0]
        .handler
        .source
        .contains("input: Body<UserCreate>"));
    assert!(app.routes[0]
        .handler
        .emitted_source
        .contains("const __sloppy_typed_handler = async (input)"));
    let handler_start = emitted_js
        .handler_generated_starts
        .first()
        .expect("handler start should be recorded");
    let first_mapping = emitted_js
        .mappings
        .iter()
        .find(|mapping| mapping.generated_line == handler_start.generated_line)
        .expect("typed handler should have a same-line source map segment");
    let (original_line, original_column) =
        super::line_column(source, app.routes[0].handler.span.start);
    assert!(first_mapping.generated_column > handler_start.generated_column);
    assert_eq!(first_mapping.original_line, original_line.saturating_sub(1));
    assert_eq!(
        first_mapping.original_column,
        original_column.saturating_sub(1)
    );
}

#[test]
fn typed_framework_provider_injection_uses_configured_provider_options() {
    let source = r#"import { Sloppy, Results, RequestContext } from "sloppy";
import { sql } from "sloppy/data";
import { Postgres } from "sloppy/providers/postgres";
import { Sqlite } from "sloppy/providers/sqlite";
import { SqlServer } from "sloppy/providers/sqlserver";
const app = Sloppy.create();
app.get("/users", async (pg: Postgres<"main">, sqlite: Sqlite<"audit">, sqlserver: SqlServer<"search">, ctx: RequestContext) => {
  await sqlite.exec(sql`create table if not exists audit(id integer)`, { deadline: ctx.deadline });
  return Results.ok({ ok: true });
});
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("typed provider injection should extract");
    assert!(app.capabilities.iter().any(|capability| {
        capability.token == "data.main"
            && capability.capability_kind == "database"
            && capability.provider == "postgres"
            && capability.config_name.as_deref() == Some("main")
    }));
    assert!(app.capabilities.iter().any(|capability| {
        capability.token == "data.audit"
            && capability.capability_kind == "database"
            && capability.provider == "sqlite"
            && capability.config_name.as_deref() == Some("audit")
    }));
    assert!(app.capabilities.iter().any(|capability| {
        capability.token == "data.search"
            && capability.capability_kind == "database"
            && capability.provider == "sqlserver"
            && capability.config_name.as_deref() == Some("search")
    }));
    let emitted_js = super::emit_app_js(&app);
    assert!(emitted_js
        .source
        .contains("\"connectionStringKey\":\"Sloppy:Providers:postgres:main:connectionString\""));
    assert!(emitted_js.source.contains(
        "\"connectionStringEnv\":\"Sloppy__Providers__postgres__main__connectionString\""
    ));
    assert!(emitted_js.source.contains(
        "\"connectionStringKey\":\"Sloppy:Providers:sqlserver:search:connectionString\""
    ));
    assert!(emitted_js.source.contains(
        "\"connectionStringEnv\":\"Sloppy__Providers__sqlserver__search__connectionString\""
    ));
    assert!(emitted_js
        .source
        .contains("data.postgres.open(__sloppy_framework_provider_open_options"));
    assert!(emitted_js
        .source
        .contains("data.sqlserver.open(__sloppy_framework_provider_open_options"));
    assert!(emitted_js.source.contains("data.sqlite(dependencyName)"));
    assert!(!emitted_js.source.contains("data.postgres.open({ provider:"));
    assert!(!emitted_js
        .source
        .contains("data.sqlserver.open({ provider:"));
}

#[test]
fn typed_framework_provider_injection_uses_placeholder_environment_source() {
    let root = fixture_temp_dir("typed-provider-config-env-source");
    let input = root.join("app.ts");
    let source = r#"import { Sloppy, Results } from "sloppy";
import { Postgres } from "sloppy/providers/postgres";
const app = Sloppy.create();
app.get("/users", async (pg: Postgres<"main">) => Results.ok({ ok: true }));
export default app;
"#;
    fs::write(&input, source).expect("input should be written");
    fs::write(
        root.join("appsettings.json"),
        r#"{"Sloppy":{"Providers":{"postgres":{"main":{"connectionString":"${SLOPPY_TEST_PG_URL}"}}}}}"#,
    )
    .expect("appsettings should be written");
    std::env::set_var(
        "SLOPPY_TEST_PG_URL",
        "postgres://user:<PASSWORD>@example/db",
    );

    let mut app = extract(&input, source).expect("typed provider injection should extract");
    let configuration = super::ConfigurationModel::load(&input, &super::CompileOptions::new(), &[])
        .expect("configuration should load");
    configuration
        .apply_to_app(&mut app)
        .expect("configuration should apply");
    let emitted_js = super::emit_app_js(&app);
    let plan = super::emit_plan(&app, "bundle-hash", "map-hash").expect("plan should emit");

    std::env::remove_var("SLOPPY_TEST_PG_URL");
    assert!(emitted_js
        .source
        .contains("\"connectionStringEnv\":\"SLOPPY_TEST_PG_URL\""));
    assert!(!emitted_js
        .source
        .contains("postgres://user:<PASSWORD>@example/db"));
    assert!(plan.contains("\"value\": \"<redacted>\""));
    assert!(!plan.contains("postgres://user:<PASSWORD>@example/db"));
}

#[test]
fn typed_framework_response_schema_inference_is_scope_aware() {
    let source = r#"import { Sloppy, Results, Route } from "sloppy";
import { Postgres } from "sloppy/providers/postgres";
type UserDto = { id: number };
type OrderDto = { id: number };
const app = Sloppy.create();
app.get("/items/:id", async (id: Route<number>, db: Postgres<"main">) => {
  if (id > 0) {
    const payload = await db.queryOne<UserDto>("select user");
    return Results.ok(payload);
  }
  const payload = await db.queryOne<OrderDto>("select order");
  return Results.ok(payload);
});
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("scoped response schemas should extract");
    let body_schemas: Vec<_> = app.routes[0]
        .handler
        .responses
        .iter()
        .map(|response| response.body_schema.as_deref())
        .collect();
    assert_eq!(body_schemas, vec![Some("UserDto"), Some("OrderDto")]);
}

#[test]
fn typed_framework_response_dedupe_preserves_distinct_body_schema() {
    let source = r#"import { Sloppy, Results, Route } from "sloppy";
type UserDto = { id: number };
type OrderDto = { id: number };
const app = Sloppy.create();
app.get("/items/:id", (id: Route<number>) => {
  if (id > 0) {
    return Results.ok<UserDto>({ id });
  }
  return Results.ok<OrderDto>({ id });
});
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("distinct typed responses should extract");
    let body_schemas: Vec<_> = app.routes[0]
        .handler
        .responses
        .iter()
        .map(|response| response.body_schema.as_deref())
        .collect();
    assert_eq!(body_schemas, vec![Some("UserDto"), Some("OrderDto")]);
}

#[test]
fn typed_framework_response_schema_ignores_arbitrary_generic_wrappers() {
    let source = r#"import { Sloppy, Results, Route } from "sloppy";
type UserDto = { id: number };
const app = Sloppy.create();
app.get("/items/:id", async (id: Route<number>) => {
  const payload = await Promise.resolve<UserDto>({ id });
  return Results.ok(payload);
});
export default app;
"#;
    let app = extract(std::path::Path::new("app.ts"), source)
        .expect("unsupported generic wrapper should not become schema evidence");
    assert_eq!(app.routes[0].handler.responses.len(), 1);
    assert_eq!(app.routes[0].handler.responses[0].body_schema, None);
}

#[test]
fn import_call_text_in_comments_and_strings_is_not_dynamic_import() {
    let source = r#"import { Sloppy, Results } from "sloppy";
// import("./commented.js") documents unsupported syntax.
const app = Sloppy.create();
const note = "dynamic import text: import(\"./string.js\")";
app.mapGet("/", () => Results.text("Hello"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("comments and strings should not trigger dynamic import diagnostics");
    assert_eq!(app.routes.len(), 1);
}

#[test]
fn captured_member_expression_emits_dynamic_response_metadata() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
const config = { message: "captured" };
app.mapGet("/", (ctx) => Results.json({ message: config.message, id: ctx.route.id }));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("captured response value should fall back to dynamic metadata");
    assert!(app.routes.is_empty());
    assert_eq!(app.dynamic_routes.len(), 1);
    assert_eq!(app.dynamic_routes[0].method, Some("GET"));
    assert_eq!(app.dynamic_routes[0].pattern.as_deref(), Some("/"));
}

#[test]
fn rejects_destructured_or_default_handler_parameters() {
    for source in [
        r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.mapGet("/", ({ route }) => Results.json({ id: route.id }));
export default app;
"#,
        r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.mapGet("/", ([ctx]) => Results.json({ id: ctx.route.id }));
export default app;
"#,
        r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
app.mapGet("/", (ctx = {}) => Results.json({ id: ctx.route.id }));
export default app;
"#,
    ] {
        let diagnostic = extract(std::path::Path::new("app.js"), source)
            .expect_err("unsupported handler parameter should fail");
        assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_HANDLER_PARAMETERS");
    }
}

#[test]
fn ignores_unrelated_map_named_initializers() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
const items = ["ok"];
const labels = items.map((value) => value);
app.mapGet("/", () => Results.text("Hello"));
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("ordinary JavaScript map initializer should not be treated as a route");
    assert_eq!(app.routes.len(), 1);
    assert_eq!(app.routes[0].pattern, "/");
}

#[test]
fn route_metadata_errors_in_initializers_are_reported() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
const tags = ["users"];
const route = app.get("/users", { tags }, () => Results.ok({ ok: true }));
export default app;
"#;
    let diagnostic = extract(std::path::Path::new("app.js"), source)
        .expect_err("dynamic route metadata in an initializer should fail");
    assert_eq!(diagnostic.code, "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS");
    assert!(diagnostic.path.is_some());
}

#[test]
fn extracts_route_options_and_group_tags_into_plan() {
    let source = r#"import { Sloppy, Results } from "sloppy";
const app = Sloppy.create();
const users = app.group("/users").withTags("users");
users.get("/", { name: "Users.List", tags: ["list"] }, () => Results.ok([]));
const admin = app.group("/admin").withTags("admin").withTags("v1");
admin.get("/audit", () => Results.ok([])).withName("Admin.Audit").withName("Admin.Audit.Latest");
export default app;
"#;
    let app = extract(std::path::Path::new("app.js"), source)
        .expect("route metadata options should extract");
    assert_eq!(app.routes.len(), 2);
    assert_eq!(app.routes[0].name.as_deref(), Some("Users.List"));
    assert_eq!(
        app.routes[0].tags,
        vec!["users".to_string(), "list".to_string()]
    );
    assert_eq!(app.routes[1].name.as_deref(), Some("Admin.Audit.Latest"));
    assert_eq!(
        app.routes[1].tags,
        vec!["admin".to_string(), "v1".to_string()]
    );

    let emitted_js = super::emit_app_js(&app);
    let emitted_source_map = super::emit_source_map(&app, &emitted_js);
    let emitted_plan = super::emit_plan(
        &app,
        &super::sha256_hex(&emitted_js.source),
        &super::sha256_hex(&emitted_source_map),
    )
    .expect("plan should emit");
    let value: serde_json::Value =
        serde_json::from_str(&emitted_plan).expect("plan should be valid json");
    assert_eq!(
        value["routes"][0]["tags"],
        serde_json::json!(["users", "list"])
    );
    assert_eq!(
        value["routes"][1]["tags"],
        serde_json::json!(["admin", "v1"])
    );
}

#[test]
fn success_fixture_expected_outputs_stay_current() {
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
    for fixture_name in [
        "hello-mapget",
        "builder-mapget",
        "grouped-route",
        "results-json",
        "function-handler",
        "http-methods",
        "async-handler",
        "provider-capability",
        "metadata-extraction",
        "effects-capability",
        "realistic-users-api",
        "partial-body-without-schema",
        "partial-dynamic-status",
        "provider-metadata-multiple-databases",
        "function-module-empty",
        "function-module",
        "source-map",
    ] {
        let fixture = root
            .join("tests/fixtures")
            .join(fixture_name)
            .join("input.js");
        let source = fs::read_to_string(&fixture).expect("fixture input should exist");
        let mut app = extract(&fixture, &source).expect("fixture should extract");
        super::ConfigurationModel::load(&fixture, &CompileOptions::new(), &app.config_reads)
            .expect("fixture configuration should load")
            .apply_to_app(&mut app)
            .expect("fixture configuration should apply");

        let emitted_js = super::emit_app_js(&app);
        let expected_js = fs::read_to_string(
            root.join("tests/fixtures")
                .join(fixture_name)
                .join("expected/app.js"),
        )
        .expect("expected app.js should exist");
        assert_eq!(emitted_js.source, expected_js, "{fixture_name} app.js");

        let emitted_source_map = super::emit_source_map(&app, &emitted_js);
        let emitted_js_hash = super::sha256_hex(&emitted_js.source);
        let emitted_map_hash = super::sha256_hex(&emitted_source_map);
        let emitted_plan =
            super::emit_plan(&app, &emitted_js_hash, &emitted_map_hash).expect("plan should emit");
        let expected_plan = fs::read_to_string(
            root.join("tests/fixtures")
                .join(fixture_name)
                .join("expected/app.plan.json"),
        )
        .expect("expected app.plan.json should exist");
        assert_eq!(emitted_plan, expected_plan, "{fixture_name} app.plan.json");

        let expected_source_map = fs::read_to_string(
            root.join("tests/fixtures")
                .join(fixture_name)
                .join("expected/app.js.map"),
        )
        .expect("expected app.js.map should exist");
        assert_eq!(
            emitted_source_map, expected_source_map,
            "{fixture_name} app.js.map"
        );
    }
}

#[test]
fn rejected_fixture_diagnostics_stay_current() {
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
    for (fixture_name, input_name) in [
        ("unsupported-handler-parameter", "input.js"),
        ("unsupported-handler-shape", "input.js"),
        ("unsupported-import-alias", "input.js"),
        ("unsupported-data-import-alias", "input.js"),
        ("unsupported-sloppy-default-import", "input.js"),
        ("unsupported-import-specifier", "input.js"),
        ("node-fs-import", "input.js"),
        ("missing-app", "input.js"),
        ("multiple-apps", "input.js"),
        ("unsupported-http-method", "input.js"),
        ("unsupported-async-handler-body", "input.js"),
        ("unsupported-secret-capability", "input.js"),
        ("unsupported-typescript-handler", "input.ts"),
        ("unsupported-dynamic-import", "input.js"),
        ("missing-relative-import", "input.js"),
        ("missing-provider-effect", "input.js"),
        ("non-sqlite-provider-bridge", "input.js"),
        ("unsupported-provider-method", "input.js"),
        ("unsupported-route-options-dynamic-tags", "input.js"),
        ("unsupported-cors-dynamic", "input.js"),
        ("unsupported-request-id-dynamic", "input.js"),
        ("unsupported-request-logging-dynamic", "input.js"),
        ("unsupported-controller-mapping", "input.js"),
        ("unsupported-testing-import", "input.js"),
        ("unsupported-health-captured-check", "input.js"),
    ] {
        let fixture = root
            .join("tests/fixtures")
            .join(fixture_name)
            .join(input_name);
        let source = fs::read_to_string(&fixture).expect("fixture input should exist");
        let diagnostic = extract(&fixture, &source).expect_err("fixture should be rejected");
        let expected = fs::read_to_string(
            root.join("tests/fixtures")
                .join(fixture_name)
                .join("expected-diagnostics.txt"),
        )
        .expect("expected diagnostic should exist");
        let rendered = diagnostic
            .render(Some(&source))
            .replace(&crate::source::display_path(root), "<compiler>");
        assert_eq!(format!("{rendered}\n"), expected, "{fixture_name}");
    }
}

#[test]
fn rejected_build_does_not_emit_success_artifacts() {
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
    let input = root.join("tests/fixtures/unsupported-dynamic-import/input.js");
    let out_dir = std::env::temp_dir().join(format!(
        "sloppyc-rejected-build-test-{}",
        std::process::id()
    ));

    if out_dir.exists() {
        fs::remove_dir_all(&out_dir).expect("stale test output directory should be removable");
    }

    let failure = super::build(&input, &out_dir, &CompileOptions::new())
        .expect_err("fixture should fail to build");
    assert_eq!(
        failure.diagnostic.code,
        "SLOPPYC_E_UNSUPPORTED_DYNAMIC_IMPORT"
    );
    assert!(
        !out_dir.join("app.plan.json").exists()
            && !out_dir.join("app.js").exists()
            && !out_dir.join("app.js.map").exists(),
        "rejected compiler input must not leave success artifacts"
    );

    if out_dir.exists() {
        fs::remove_dir_all(&out_dir).expect("test output directory should be removable");
    }
}

#[test]
fn build_writes_expected_artifacts_to_requested_output_directory() {
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
    let input = root.join("../examples/compiler-hello/app.js");
    let out_dir = std::env::temp_dir().join(format!("sloppyc-build-test-{}", std::process::id()));

    if out_dir.exists() {
        fs::remove_dir_all(&out_dir).expect("stale test output directory should be removable");
    }

    super::build(&input, &out_dir, &CompileOptions::new()).expect("compiler example should build");

    let emitted_plan =
        fs::read_to_string(out_dir.join("app.plan.json")).expect("plan should be written");
    let expected_plan =
        fs::read_to_string(root.join("../examples/compiler-hello/expected/app.plan.json"))
            .expect("expected plan should exist");
    assert_eq!(emitted_plan, expected_plan);

    let emitted_js = fs::read_to_string(out_dir.join("app.js")).expect("app.js should be written");
    let expected_js = fs::read_to_string(root.join("../examples/compiler-hello/expected/app.js"))
        .expect("expected app.js should exist");
    assert_eq!(emitted_js, expected_js);

    let emitted_map =
        fs::read_to_string(out_dir.join("app.js.map")).expect("source map should be written");
    let expected_map =
        fs::read_to_string(root.join("../examples/compiler-hello/expected/app.js.map"))
            .expect("expected app.js.map should exist");
    assert_eq!(emitted_map, expected_map);

    fs::remove_dir_all(&out_dir).expect("test output directory should be removable");
}

#[test]
fn compiler_hello_artifacts_are_repeatable_and_path_clean() {
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
    let input = root.join("../examples/compiler-hello/app.js");
    let base = std::env::temp_dir().join(format!(
        "sloppyc-main-determinism-test-{}",
        std::process::id()
    ));
    let first = base.join("first");
    let second = base.join("second");

    if base.exists() {
        fs::remove_dir_all(&base).expect("stale test output directory should be removable");
    }

    super::build(&input, &first, &CompileOptions::new()).expect("first build should succeed");
    super::build(&input, &second, &CompileOptions::new()).expect("second build should succeed");

    for artifact in ["app.plan.json", "app.js", "app.js.map"] {
        let first_text =
            fs::read_to_string(first.join(artifact)).expect("first artifact should exist");
        let second_text =
            fs::read_to_string(second.join(artifact)).expect("second artifact should exist");
        assert_eq!(first_text, second_text, "{artifact} should be repeatable");

        assert!(
            !first_text.contains(env!("CARGO_MANIFEST_DIR")),
            "{artifact} must not contain the local compiler manifest path"
        );
        assert!(
            !first_text.contains("\\Slop\\") && !first_text.contains("/Slop/"),
            "{artifact} must not contain checkout-local paths"
        );
        assert!(
            !first_text.contains("timestamp") && !first_text.contains("random"),
            "{artifact} must not contain volatility marker text"
        );
    }

    let plan = fs::read_to_string(first.join("app.plan.json")).expect("plan should exist");
    assert!(
        plan.contains("\"id\": 1") && plan.contains("\"handlerId\": 1"),
        "MAIN hello handler IDs must remain stable"
    );

    fs::remove_dir_all(&base).expect("test output directory should be removable");
}

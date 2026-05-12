//! Real compiler-driven diagnostic coverage for the npm-compat negative
//! fixtures. The matrix in `tests/fixtures/npm-compat/` exercises the resolver
//! classification layer directly. These tests run the full `compile_file`
//! pipeline against the same fixtures and assert the surfaced diagnostic code
//! and message context.

use std::{fs, path::Path};

use sloppyc::{compile_file, CompileOptions};

fn fixtures_root() -> std::path::PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../tests/fixtures/npm-compat")
}

fn temp_output(name: &str) -> std::path::PathBuf {
    std::env::temp_dir().join(format!("sloppyc-npm-compat-{name}-{}", std::process::id()))
}

fn remove_dir_if_present(path: &Path) {
    if path.exists() {
        fs::remove_dir_all(path).expect("temp output should be removable");
    }
}

fn compile_fixture(importer_relative: &str, scratch_name: &str) -> Result<(), sloppyc::Diagnostic> {
    let input = fixtures_root().join(importer_relative);
    let out_dir = temp_output(scratch_name);
    remove_dir_if_present(&out_dir);
    let result = compile_file(&input, &out_dir, &CompileOptions::default()).map(|_| ());
    remove_dir_if_present(&out_dir);
    result.map_err(|error| error.diagnostic)
}

fn assert_diagnostic_contains(
    importer: &str,
    scratch: &str,
    expected_code: &str,
    expected_substring: &str,
) {
    let error = compile_fixture(importer, scratch).expect_err("fixture should fail");
    assert_eq!(
        error.code, expected_code,
        "diagnostic code mismatch for {importer}: expected {expected_code}, got {} ({})",
        error.code, error.message
    );
    assert!(
        error.message.contains(expected_substring),
        "diagnostic message for {importer} missing substring {expected_substring:?}: got {:?}",
        error.message
    );
}

#[test]
fn array_exports_shape_emits_explicit_diagnostic() {
    assert_diagnostic_contains(
        "exports-unsupported-shape/importer.js",
        "array-exports",
        "SLOPPYC_E_PACKAGE_EXPORT_UNSUPPORTED",
        "array-exports-pkg",
    );
}

#[test]
fn multi_star_pattern_exports_are_rejected() {
    assert_diagnostic_contains(
        "exports-multi-star/importer.js",
        "multi-star",
        "SLOPPYC_E_PACKAGE_EXPORT_UNSUPPORTED",
        "more than one wildcard",
    );
}

#[test]
fn mixed_subpath_and_condition_exports_are_rejected() {
    assert_diagnostic_contains(
        "exports-mixed-shape/importer.js",
        "mixed-shape",
        "SLOPPYC_E_PACKAGE_EXPORT_UNSUPPORTED",
        "mixes subpath keys with condition keys",
    );
}

#[test]
fn subpath_only_exports_without_root_rejects_root_import() {
    assert_diagnostic_contains(
        "exports-subpath-only/importer.js",
        "subpath-only-root",
        "SLOPPYC_E_PACKAGE_EXPORT_UNSUPPORTED",
        "no root entry",
    );
}

#[test]
fn unsupported_imports_target_is_rejected() {
    assert_diagnostic_contains(
        "imports-unsupported/src/importer.js",
        "imports-unsupported",
        "SLOPPYC_E_PACKAGE_EXPORT_UNSUPPORTED",
        "imports target file was not found on disk",
    );
}

#[test]
fn native_addon_node_entry_is_rejected() {
    assert_diagnostic_contains(
        "native-addon-used/importer.js",
        "native-addon",
        "SLOPPYC_E_NATIVE_ADDON_UNSUPPORTED",
        "native-pkg",
    );
}

#[test]
fn bindings_native_addon_signal_is_rejected() {
    assert_diagnostic_contains(
        "bindings-native-signal/importer.js",
        "bindings-native",
        "SLOPPYC_E_NATIVE_ADDON_UNSUPPORTED",
        "bindings-native-pkg",
    );
}

#[test]
fn node_child_process_emits_unsupported_builtin_diagnostic() {
    assert_diagnostic_contains(
        "unsupported-builtin-child_process-program/main.js",
        "child-process",
        "SLOPPYC_E_UNSUPPORTED_NODE_BUILTIN",
        "node:child_process",
    );
}

// Web-mode dynamic `import(literal)` is already covered by
// `compiler/tests/fixtures/unsupported-dynamic-import/`. Program-mode computed
// `require(name)` inside a function body and program-mode top-level
// `import(name)` against a non-literal specifier are deliberate runtime checks
// against the sealed `moduleInclude` graph, not compile-time diagnostics; the
// dynamic-require-computed and dynamic-import-computed fixtures exercise the
// shape but do not assert a compile-time error here.

//! Supported import resolution.
//!
//! Slop resolves documented Sloppy imports, source-root relative files, and the installed
//! package subset used by the sealed artifact dependency graph.

use std::{
    fs,
    path::{Path, PathBuf},
};

use serde_json::Value;

use crate::{graph::ModuleFormat, source::display_path};

#[derive(Debug, Clone, Eq, PartialEq)]
pub(crate) enum ImportKind {
    Relative(PathBuf),
    SlopStdlib,
    SlopTime,
    SlopFilesystem,
    SlopCrypto,
    SlopCodec,
    SlopNet,
    SlopOs,
    SlopWorkers,
    SlopFfi,
    SqliteProvider,
    NodeBuiltin(NodeBuiltinResolution),
    Package(PackageResolution),
    NativeAddonUnsupported(PackageResolution),
    PackageExportUnsupported(String),
    UnresolvedRelative(String),
    UnsupportedBare(String),
    Remote(String),
}

#[derive(Debug, Clone, Eq, PartialEq)]
pub(crate) struct NodeBuiltinResolution {
    pub(crate) specifier: String,
    pub(crate) status: &'static str,
    pub(crate) backing: Option<&'static str>,
    pub(crate) capability: Option<&'static str>,
}

#[derive(Debug, Clone, Eq, PartialEq)]
pub(crate) struct PackageResolution {
    pub(crate) name: String,
    pub(crate) version: Option<String>,
    pub(crate) root: PathBuf,
    pub(crate) package_json: Option<PathBuf>,
    pub(crate) entry: PathBuf,
    pub(crate) format: ModuleFormat,
    pub(crate) source: &'static str,
}

pub(crate) fn classify_import(from_path: &Path, specifier: &str) -> ImportKind {
    classify_import_with_mode(from_path, specifier, true)
}

pub(crate) fn classify_import_with_mode(
    from_path: &Path,
    specifier: &str,
    import_mode: bool,
) -> ImportKind {
    if specifier.starts_with("./") || specifier.starts_with("../") {
        return resolve_relative_import(from_path, specifier).map_or_else(
            || ImportKind::UnresolvedRelative(specifier.to_string()),
            ImportKind::Relative,
        );
    }
    if specifier == "sloppy" {
        return ImportKind::SlopStdlib;
    }
    if specifier == "sloppy/time" {
        return ImportKind::SlopTime;
    }
    if specifier == "sloppy/fs" {
        return ImportKind::SlopFilesystem;
    }
    if specifier == "sloppy/crypto" {
        return ImportKind::SlopCrypto;
    }
    if specifier == "sloppy/codec" {
        return ImportKind::SlopCodec;
    }
    if specifier == "sloppy/net" {
        return ImportKind::SlopNet;
    }
    if specifier == "sloppy/os" {
        return ImportKind::SlopOs;
    }
    if specifier == "sloppy/workers" {
        return ImportKind::SlopWorkers;
    }
    if specifier == "sloppy/ffi" {
        return ImportKind::SlopFfi;
    }
    if specifier == "sloppy/providers/sqlite" {
        return ImportKind::SqliteProvider;
    }
    if specifier.starts_with("http://") || specifier.starts_with("https://") {
        return ImportKind::Remote(specifier.to_string());
    }
    if let Some(node_builtin) = resolve_node_builtin(specifier) {
        return ImportKind::NodeBuiltin(node_builtin);
    }
    if specifier.starts_with("node:") {
        return ImportKind::NodeBuiltin(NodeBuiltinResolution {
            specifier: specifier.to_string(),
            status: "unsupported",
            backing: None,
            capability: None,
        });
    }
    if specifier.starts_with('#') {
        if let Some(package) = resolve_package_imports(from_path, specifier, import_mode) {
            return package;
        }
        return ImportKind::PackageExportUnsupported(specifier.to_string());
    }
    if let Some(package) = resolve_package_import(from_path, specifier, import_mode) {
        return package;
    }
    ImportKind::UnsupportedBare(specifier.to_string())
}

pub fn resolve_relative_import(from_path: &Path, specifier: &str) -> Option<PathBuf> {
    let base = from_path.parent().unwrap_or_else(|| Path::new(""));
    let candidate = base.join(specifier);
    let candidates = if let Some(extension) = candidate.extension().and_then(|ext| ext.to_str()) {
        match extension {
            "js" | "mjs" | "cjs" | "ts" | "tsx" | "json" | "node" => vec![candidate],
            _ => return None,
        }
    } else {
        vec![
            candidate.clone(),
            candidate.with_extension("js"),
            candidate.with_extension("mjs"),
            candidate.with_extension("cjs"),
            candidate.with_extension("ts"),
            candidate.with_extension("tsx"),
            candidate.with_extension("json"),
            candidate.join("index.js"),
            candidate.join("index.mjs"),
            candidate.join("index.cjs"),
            candidate.join("index.ts"),
        ]
    };
    candidates
        .into_iter()
        .find(|candidate| candidate.is_file())
        .and_then(|candidate| fs::canonicalize(candidate).ok())
}

pub(crate) fn resolve_node_builtin(specifier: &str) -> Option<NodeBuiltinResolution> {
    let normalized = match specifier.strip_prefix("node:") {
        Some(name) => format!("node:{name}"),
        None if matches!(
            specifier,
            "path"
                | "events"
                | "url"
                | "querystring"
                | "buffer"
                | "util"
                | "fs"
                | "fs/promises"
                | "os"
                | "process"
                | "crypto"
                | "assert"
                | "assert/strict"
                | "stream"
                | "stream/promises"
                | "timers"
        ) =>
        {
            format!("node:{specifier}")
        }
        None => return None,
    };
    let (status, backing, capability) = match normalized.as_str() {
        "node:path" => ("supported", Some("sloppy/node/path"), None),
        "node:events" => ("supported", Some("sloppy/node/events"), None),
        "node:url" => ("supported", Some("sloppy/node/url"), None),
        "node:querystring" => ("supported", Some("sloppy/node/querystring"), None),
        "node:buffer" => ("partial", Some("sloppy/node/buffer"), None),
        "node:util" => ("partial", Some("sloppy/node/util"), None),
        "node:timers" => ("partial", Some("sloppy/node/timers"), Some("time")),
        "node:fs" => ("partial", Some("sloppy/node/fs"), Some("fs")),
        "node:fs/promises" => ("partial", Some("sloppy/node/fs/promises"), Some("fs")),
        "node:os" => ("partial", Some("sloppy/node/os"), Some("os")),
        "node:process" => ("partial", Some("sloppy/node/process"), Some("os")),
        "node:crypto" => ("partial", Some("sloppy/node/crypto"), Some("crypto")),
        "node:assert" => ("partial", Some("sloppy/node/assert"), None),
        "node:assert/strict" => ("partial", Some("sloppy/node/assert/strict"), None),
        "node:stream" => ("partial", Some("sloppy/node/stream"), None),
        "node:stream/promises" => ("partial", Some("sloppy/node/stream/promises"), None),
        _ => ("unsupported", None, None),
    };
    Some(NodeBuiltinResolution {
        specifier: normalized,
        status,
        backing,
        capability,
    })
}

fn resolve_package_import(
    from_path: &Path,
    specifier: &str,
    import_mode: bool,
) -> Option<ImportKind> {
    let (package_name, subpath) = split_package_specifier(specifier)?;
    let (package_root, source) = find_self_package_root(from_path, &package_name)
        .map(|root| (root, "self"))
        .or_else(|| find_package_root(from_path, &package_name).map(|root| (root, "installed")))?;
    let package_json = package_root.join("package.json");
    let package_json_value = fs::read_to_string(&package_json)
        .ok()
        .and_then(|text| serde_json::from_str::<Value>(&text).ok());
    let version = package_json_value
        .as_ref()
        .and_then(|value| value.get("version"))
        .and_then(Value::as_str)
        .map(ToOwned::to_owned);
    let package_type = package_json_value
        .as_ref()
        .and_then(|value| value.get("type"))
        .and_then(Value::as_str)
        .unwrap_or("commonjs");

    let entry = match package_json_value
        .as_ref()
        .and_then(|json| resolve_package_entry(&package_root, json, subpath, import_mode))
    {
        Some(Ok(path)) => path,
        Some(Err(())) => return Some(ImportKind::PackageExportUnsupported(package_name)),
        None => {
            if !subpath.is_empty() {
                package_root.join(subpath)
            } else if let Some(main) = package_json_value
                .as_ref()
                .and_then(|value| value.get("main"))
                .and_then(Value::as_str)
            {
                package_root.join(main)
            } else {
                package_root.join("index.js")
            }
        }
    };

    let Some(entry) = resolve_file_or_directory(&entry) else {
        return Some(ImportKind::PackageExportUnsupported(package_name));
    };
    let format = module_format_for_path(&entry, package_type);
    let package = PackageResolution {
        name: package_name,
        version,
        root: fs::canonicalize(package_root.clone()).unwrap_or(package_root),
        package_json: package_json.exists().then_some(package_json),
        entry,
        format,
        source,
    };
    if package.format == ModuleFormat::CommonJs
        && package.entry.extension().and_then(|ext| ext.to_str()) == Some("node")
    {
        return Some(ImportKind::NativeAddonUnsupported(package));
    }
    if package.entry.extension().and_then(|ext| ext.to_str()) == Some("node")
        || package_json_value
            .as_ref()
            .is_some_and(package_json_has_native_addon_signal)
    {
        return Some(ImportKind::NativeAddonUnsupported(package));
    }
    Some(ImportKind::Package(package))
}

fn resolve_package_imports(
    from_path: &Path,
    specifier: &str,
    import_mode: bool,
) -> Option<ImportKind> {
    let package_root = find_nearest_package_scope(from_path)?;
    let package_json = package_root.join("package.json");
    let package_json_value = fs::read_to_string(&package_json)
        .ok()
        .and_then(|text| serde_json::from_str::<Value>(&text).ok())?;
    let imports = package_json_value.get("imports")?;
    let entry = resolve_imports_entry(&package_root, imports, specifier, import_mode)?;
    let entry = match entry {
        Ok(path) => path,
        Err(()) => return Some(ImportKind::PackageExportUnsupported(specifier.to_string())),
    };
    let Some(entry) = resolve_file_or_directory(&entry) else {
        return Some(ImportKind::PackageExportUnsupported(specifier.to_string()));
    };
    let package_type = package_json_value
        .get("type")
        .and_then(Value::as_str)
        .unwrap_or("commonjs");
    let package = PackageResolution {
        name: package_json_value
            .get("name")
            .and_then(Value::as_str)
            .unwrap_or("#imports")
            .to_string(),
        version: package_json_value
            .get("version")
            .and_then(Value::as_str)
            .map(ToOwned::to_owned),
        root: fs::canonicalize(package_root.clone()).unwrap_or(package_root),
        package_json: Some(package_json),
        entry: entry.clone(),
        format: module_format_for_path(&entry, package_type),
        source: "imports",
    };
    Some(ImportKind::Package(package))
}

fn split_package_specifier(specifier: &str) -> Option<(String, &str)> {
    if specifier.is_empty()
        || specifier.starts_with('.')
        || specifier.starts_with('/')
        || specifier.contains('\\')
    {
        return None;
    }
    if let Some(rest) = specifier.strip_prefix('@') {
        let (scope, remainder) = rest.split_once('/')?;
        let (name, subpath) = remainder.split_once('/').unwrap_or((remainder, ""));
        if scope.is_empty() || name.is_empty() {
            return None;
        }
        return Some((format!("@{scope}/{name}"), subpath));
    }
    let (name, subpath) = specifier.split_once('/').unwrap_or((specifier, ""));
    (!name.is_empty()).then(|| (name.to_string(), subpath))
}

fn find_package_root(from_path: &Path, package_name: &str) -> Option<PathBuf> {
    let mut cursor = from_path.parent();
    while let Some(dir) = cursor {
        let candidate = dir.join("node_modules").join(package_name);
        if candidate.is_dir() {
            return Some(candidate);
        }
        cursor = dir.parent();
    }
    None
}

fn find_self_package_root(from_path: &Path, package_name: &str) -> Option<PathBuf> {
    let package_root = find_nearest_package_scope(from_path)?;
    let package_json = fs::read_to_string(package_root.join("package.json")).ok()?;
    let package_json = serde_json::from_str::<Value>(&package_json).ok()?;
    (package_json
        .get("name")
        .and_then(Value::as_str)
        .is_some_and(|name| name == package_name))
    .then_some(package_root)
}

fn find_nearest_package_scope(from_path: &Path) -> Option<PathBuf> {
    let mut cursor = from_path.parent();
    while let Some(dir) = cursor {
        if dir.join("package.json").is_file() {
            return Some(dir.to_path_buf());
        }
        cursor = dir.parent();
    }
    None
}

fn resolve_package_entry(
    package_root: &Path,
    package_json: &Value,
    subpath: &str,
    import_mode: bool,
) -> Option<Result<PathBuf, ()>> {
    let exports = package_json.get("exports")?;
    let key = if subpath.is_empty() {
        "."
    } else {
        let request = format!("./{subpath}");
        if let Some(value) = exports.get(request.as_str()) {
            return Some(resolve_exports_value(package_root, value, import_mode));
        }
        return resolve_pattern_entry(package_root, exports, &request, import_mode);
    };
    if exports.is_string() || exports.is_object() && exports.get(key).is_none() {
        return Some(resolve_exports_value(package_root, exports, import_mode));
    }
    Some(resolve_exports_value(
        package_root,
        exports.get(key)?,
        import_mode,
    ))
}

fn resolve_imports_entry(
    package_root: &Path,
    imports: &Value,
    specifier: &str,
    import_mode: bool,
) -> Option<Result<PathBuf, ()>> {
    if let Some(value) = imports.get(specifier) {
        return Some(resolve_exports_value(package_root, value, import_mode));
    }
    resolve_pattern_entry(package_root, imports, specifier, import_mode)
}

fn resolve_pattern_entry(
    package_root: &Path,
    entries: &Value,
    request: &str,
    import_mode: bool,
) -> Option<Result<PathBuf, ()>> {
    let object = entries.as_object()?;
    for (pattern, value) in object {
        let Some(star) = pattern_key_match(pattern, request) else {
            continue;
        };
        return Some(resolve_exports_value_with_star(
            package_root,
            value,
            import_mode,
            Some(star.as_str()),
        ));
    }
    None
}

fn pattern_key_match(pattern: &str, request: &str) -> Option<String> {
    let (prefix, suffix) = pattern.split_once('*')?;
    request
        .strip_prefix(prefix)
        .and_then(|rest| rest.strip_suffix(suffix))
        .map(ToOwned::to_owned)
}

fn resolve_exports_value(
    package_root: &Path,
    value: &Value,
    import_mode: bool,
) -> Result<PathBuf, ()> {
    resolve_exports_value_with_star(package_root, value, import_mode, None)
}

fn resolve_exports_value_with_star(
    package_root: &Path,
    value: &Value,
    import_mode: bool,
    star: Option<&str>,
) -> Result<PathBuf, ()> {
    if let Some(path) = value.as_str() {
        let path = star.map_or_else(|| path.to_string(), |star| path.replace('*', star));
        if path.starts_with("./") && !path.contains("..") {
            return Ok(package_root.join(path));
        }
        return Err(());
    }
    let Some(object) = value.as_object() else {
        return Err(());
    };
    let conditions = if import_mode {
        ["sloppy", "import", "default"]
    } else {
        ["sloppy", "require", "default"]
    };
    for condition in conditions {
        if let Some(entry) = object.get(condition) {
            return resolve_exports_value_with_star(package_root, entry, import_mode, star);
        }
    }
    Err(())
}

fn resolve_file_or_directory(candidate: &Path) -> Option<PathBuf> {
    let candidates = if candidate.extension().is_some() {
        vec![candidate.to_path_buf()]
    } else {
        vec![
            candidate.to_path_buf(),
            candidate.with_extension("js"),
            candidate.with_extension("mjs"),
            candidate.with_extension("cjs"),
            candidate.with_extension("ts"),
            candidate.with_extension("tsx"),
            candidate.with_extension("json"),
            candidate.join("index.js"),
            candidate.join("index.mjs"),
            candidate.join("index.cjs"),
            candidate.join("index.ts"),
        ]
    };
    candidates
        .into_iter()
        .find(|path| path.is_file())
        .and_then(|path| fs::canonicalize(path).ok())
}

pub(crate) fn module_format_for_path(path: &Path, package_type: &str) -> ModuleFormat {
    match path
        .extension()
        .and_then(|ext| ext.to_str())
        .unwrap_or_default()
    {
        "cjs" => ModuleFormat::CommonJs,
        "mjs" | "ts" | "tsx" => ModuleFormat::Esm,
        "json" => ModuleFormat::Json,
        "js" if package_type == "module" => ModuleFormat::Esm,
        _ => ModuleFormat::CommonJs,
    }
}

pub(crate) fn package_type_for_path(path: &Path) -> String {
    find_nearest_package_scope(path)
        .and_then(|root| fs::read_to_string(root.join("package.json")).ok())
        .and_then(|text| serde_json::from_str::<Value>(&text).ok())
        .and_then(|value| {
            value
                .get("type")
                .and_then(Value::as_str)
                .map(ToOwned::to_owned)
        })
        .unwrap_or_else(|| "commonjs".to_string())
}

fn package_json_has_native_addon_signal(value: &Value) -> bool {
    let text = value.to_string();
    text.contains("node-gyp-build") || text.contains("\"bindings\"") || text.contains(".node")
}

pub fn normalized_artifact_id(path: &Path, project_root: &Path) -> String {
    let canonical = fs::canonicalize(path).unwrap_or_else(|_| path.to_path_buf());
    let root = fs::canonicalize(project_root).unwrap_or_else(|_| project_root.to_path_buf());
    let relative = canonical.strip_prefix(&root).unwrap_or(&canonical);
    display_path(relative)
}

pub fn stays_within_source_root(resolved: &Path, source_root: &Path) -> bool {
    let Ok(resolved) = fs::canonicalize(resolved) else {
        return false;
    };
    fs::canonicalize(source_root)
        .map(|root| resolved.starts_with(root))
        .unwrap_or(false)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn classifies_slop_owned_imports() {
        assert_eq!(
            classify_import(Path::new("app.js"), "sloppy"),
            ImportKind::SlopStdlib
        );
        assert_eq!(
            classify_import(Path::new("app.js"), "sloppy/providers/sqlite"),
            ImportKind::SqliteProvider
        );
        assert_eq!(
            classify_import(Path::new("app.js"), "sloppy/time"),
            ImportKind::SlopTime
        );
        assert_eq!(
            classify_import(Path::new("app.js"), "sloppy/fs"),
            ImportKind::SlopFilesystem
        );
        assert_eq!(
            classify_import(Path::new("app.js"), "sloppy/crypto"),
            ImportKind::SlopCrypto
        );
        assert_eq!(
            classify_import(Path::new("app.js"), "sloppy/codec"),
            ImportKind::SlopCodec
        );
        assert_eq!(
            classify_import(Path::new("app.js"), "sloppy/net"),
            ImportKind::SlopNet
        );
        assert_eq!(
            classify_import(Path::new("app.js"), "sloppy/os"),
            ImportKind::SlopOs
        );
        assert_eq!(
            classify_import(Path::new("app.js"), "sloppy/workers"),
            ImportKind::SlopWorkers
        );
        assert_eq!(
            classify_import(Path::new("app.js"), "sloppy/ffi"),
            ImportKind::SlopFfi
        );
    }

    #[test]
    fn rejects_unknown_bare_and_remote_imports() {
        assert_eq!(
            classify_import(Path::new("app.js"), "express"),
            ImportKind::UnsupportedBare("express".to_string())
        );
        assert_eq!(
            classify_import(Path::new("app.js"), "https://example.test/app.js"),
            ImportKind::Remote("https://example.test/app.js".to_string())
        );
    }

    #[test]
    fn distinguishes_missing_relative_imports_from_bare_imports() {
        assert_eq!(
            classify_import(Path::new("app.js"), "./missing.js"),
            ImportKind::UnresolvedRelative("./missing.js".to_string())
        );
    }

    #[test]
    fn rejects_explicit_relative_extensions_outside_supported_subset() {
        assert_eq!(
            classify_import(Path::new("app.js"), "./helper.tsx"),
            ImportKind::UnresolvedRelative("./helper.tsx".to_string())
        );
        assert_eq!(
            classify_import(Path::new("app.js"), "./data.json"),
            ImportKind::UnresolvedRelative("./data.json".to_string())
        );
    }

    #[test]
    fn source_root_check_canonicalizes_the_root() {
        let resolved = std::env::current_dir().expect("current directory should exist");
        let relative_root = Path::new(".");
        assert!(stays_within_source_root(&resolved, relative_root));
    }
}

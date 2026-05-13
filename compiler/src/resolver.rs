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
    SlopData,
    SlopTime,
    SlopFilesystem,
    SlopCrypto,
    SlopCodec,
    SlopNet,
    SlopHttp,
    SlopOs,
    SlopOrm,
    SlopWorkers,
    SlopFfi,
    SqliteProvider,
    NodeBuiltin(NodeBuiltinResolution),
    Package(PackageResolution),
    NativeAddonUnsupported(PackageResolution),
    PackageExportUnsupported(PackageExportFailure),
    UnresolvedRelative(String),
    UnsupportedBare(String),
    Remote(String),
}

#[derive(Debug, Clone, Eq, PartialEq)]
pub(crate) struct PackageExportFailure {
    pub(crate) subject: String,
    pub(crate) field: &'static str,
    pub(crate) subpath: String,
    pub(crate) reason: &'static str,
}

impl PackageExportFailure {
    pub(crate) fn exports(
        subject: impl Into<String>,
        subpath: impl Into<String>,
        reason: &'static str,
    ) -> Self {
        Self {
            subject: subject.into(),
            field: "exports",
            subpath: subpath.into(),
            reason,
        }
    }

    pub(crate) fn imports(
        subject: impl Into<String>,
        subpath: impl Into<String>,
        reason: &'static str,
    ) -> Self {
        Self {
            subject: subject.into(),
            field: "imports",
            subpath: subpath.into(),
            reason,
        }
    }
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
    if specifier == "sloppy/data" {
        return ImportKind::SlopData;
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
    if specifier == "sloppy/http" {
        return ImportKind::SlopHttp;
    }
    if specifier == "sloppy/os" {
        return ImportKind::SlopOs;
    }
    if specifier == "sloppy/orm" {
        return ImportKind::SlopOrm;
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
        return ImportKind::PackageExportUnsupported(PackageExportFailure::imports(
            specifier,
            specifier,
            "no enclosing package.json declared a matching imports entry",
        ));
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
                | "console"
                | "constants"
                | "util"
                | "fs"
                | "fs/promises"
                | "os"
                | "process"
                | "crypto"
                | "diagnostics_channel"
                | "assert"
                | "assert/strict"
                | "http"
                | "https"
                | "module"
                | "perf_hooks"
                | "stream"
                | "stream/promises"
                | "string_decoder"
                | "timers"
                | "tty"
                | "zlib"
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
        "node:console" => ("partial", Some("sloppy/node/console"), None),
        "node:constants" => ("partial", Some("sloppy/node/constants"), None),
        "node:util" => ("partial", Some("sloppy/node/util"), None),
        "node:timers" => ("partial", Some("sloppy/node/timers"), Some("time")),
        "node:fs" => ("partial", Some("sloppy/node/fs"), Some("fs")),
        "node:fs/promises" => ("partial", Some("sloppy/node/fs/promises"), Some("fs")),
        "node:os" => ("partial", Some("sloppy/node/os"), Some("os")),
        "node:process" => ("partial", Some("sloppy/node/process"), Some("os")),
        "node:crypto" => ("partial", Some("sloppy/node/crypto"), Some("crypto")),
        "node:diagnostics_channel" => ("partial", Some("sloppy/node/diagnostics_channel"), None),
        "node:http" => ("stubbed", Some("sloppy/node/http"), Some("net")),
        "node:https" => ("stubbed", Some("sloppy/node/https"), Some("net")),
        "node:module" => ("partial", Some("sloppy/node/module"), None),
        "node:perf_hooks" => ("partial", Some("sloppy/node/perf_hooks"), Some("time")),
        "node:assert" => ("partial", Some("sloppy/node/assert"), None),
        "node:assert/strict" => ("partial", Some("sloppy/node/assert/strict"), None),
        "node:stream" => ("partial", Some("sloppy/node/stream"), None),
        "node:stream/promises" => ("partial", Some("sloppy/node/stream/promises"), None),
        "node:string_decoder" => ("partial", Some("sloppy/node/string_decoder"), Some("codec")),
        "node:tty" => ("stubbed", Some("sloppy/node/tty"), None),
        "node:zlib" => ("partial", Some("sloppy/node/zlib"), Some("codec")),
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

    let subpath_label = if subpath.is_empty() {
        ".".to_string()
    } else {
        format!("./{subpath}")
    };
    let entry = match package_json_value
        .as_ref()
        .and_then(|json| resolve_package_entry(&package_root, json, subpath, import_mode))
    {
        Some(Ok(path)) => path,
        Some(Err(reason)) => {
            return Some(ImportKind::PackageExportUnsupported(
                PackageExportFailure::exports(package_name, subpath_label, reason),
            ));
        }
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
        return Some(ImportKind::PackageExportUnsupported(
            PackageExportFailure::exports(
                package_name,
                subpath_label,
                "package entry file was not found on disk",
            ),
        ));
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
        Err(reason) => {
            return Some(ImportKind::PackageExportUnsupported(
                PackageExportFailure::imports(specifier, specifier, reason),
            ));
        }
    };
    let Some(entry) = resolve_file_or_directory(&entry) else {
        return Some(ImportKind::PackageExportUnsupported(
            PackageExportFailure::imports(
                specifier,
                specifier,
                "imports target file was not found on disk",
            ),
        ));
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

#[derive(Debug, Clone, Copy, Eq, PartialEq)]
enum ExportsShape {
    Subpath,
    Conditions,
    Mixed,
    Empty,
}

fn detect_exports_shape(object: &serde_json::Map<String, Value>) -> ExportsShape {
    let mut subpath = false;
    let mut condition = false;
    for key in object.keys() {
        if key.starts_with("./") || key == "." {
            subpath = true;
        } else {
            condition = true;
        }
    }
    match (subpath, condition) {
        (true, false) => ExportsShape::Subpath,
        (false, true) => ExportsShape::Conditions,
        (false, false) => ExportsShape::Empty,
        (true, true) => ExportsShape::Mixed,
    }
}

fn resolve_package_entry(
    package_root: &Path,
    package_json: &Value,
    subpath: &str,
    import_mode: bool,
) -> Option<Result<PathBuf, &'static str>> {
    let exports = package_json.get("exports")?;

    if let Some(_path) = exports.as_str() {
        if !subpath.is_empty() {
            return Some(Err(
                "package exports string shorthand only describes the root entry",
            ));
        }
        return Some(resolve_exports_value(package_root, exports, import_mode));
    }

    let Some(object) = exports.as_object() else {
        return Some(Err(
            "package exports shape is unsupported (expected string or object)",
        ));
    };

    let shape = detect_exports_shape(object);
    let request = if subpath.is_empty() {
        ".".to_string()
    } else {
        format!("./{subpath}")
    };

    match shape {
        ExportsShape::Mixed => Some(Err(
            "package exports object mixes subpath keys with condition keys",
        )),
        ExportsShape::Empty => Some(Err("package exports object has no entries")),
        ExportsShape::Subpath => {
            if let Some(value) = object.get(&request) {
                return Some(resolve_exports_value(package_root, value, import_mode));
            }
            match resolve_pattern_entry(package_root, exports, &request, import_mode) {
                Some(result) => Some(result),
                None => Some(Err(if subpath.is_empty() {
                    "package exports object has no root entry"
                } else {
                    "package exports object has no entry for the requested subpath"
                })),
            }
        }
        ExportsShape::Conditions => {
            if !subpath.is_empty() {
                return Some(Err(
                    "package exports object only defines conditions for the root entry",
                ));
            }
            Some(resolve_exports_value(package_root, exports, import_mode))
        }
    }
}

fn resolve_imports_entry(
    package_root: &Path,
    imports: &Value,
    specifier: &str,
    import_mode: bool,
) -> Option<Result<PathBuf, &'static str>> {
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
) -> Option<Result<PathBuf, &'static str>> {
    let object = entries.as_object()?;
    for (pattern, value) in object {
        match pattern_key_match(pattern, request) {
            PatternMatch::Match(star) => {
                return Some(resolve_exports_value_with_star(
                    package_root,
                    value,
                    import_mode,
                    Some(star.as_str()),
                ));
            }
            PatternMatch::MultiStarPattern => {
                return Some(Err(
                    "package exports pattern has more than one wildcard, which is not supported",
                ));
            }
            PatternMatch::NoMatch => continue,
        }
    }
    None
}

#[derive(Debug, Clone, Eq, PartialEq)]
enum PatternMatch {
    Match(String),
    NoMatch,
    MultiStarPattern,
}

fn pattern_key_match(pattern: &str, request: &str) -> PatternMatch {
    let mut star_iter = pattern.match_indices('*');
    let Some((star_index, _)) = star_iter.next() else {
        return PatternMatch::NoMatch;
    };
    if star_iter.next().is_some() {
        return PatternMatch::MultiStarPattern;
    }
    let prefix = &pattern[..star_index];
    let suffix = &pattern[star_index + 1..];
    request
        .strip_prefix(prefix)
        .and_then(|rest| rest.strip_suffix(suffix))
        .map_or(PatternMatch::NoMatch, |star| {
            PatternMatch::Match(star.to_string())
        })
}

fn resolve_exports_value(
    package_root: &Path,
    value: &Value,
    import_mode: bool,
) -> Result<PathBuf, &'static str> {
    resolve_exports_value_with_star(package_root, value, import_mode, None)
}

fn resolve_exports_value_with_star(
    package_root: &Path,
    value: &Value,
    import_mode: bool,
    star: Option<&str>,
) -> Result<PathBuf, &'static str> {
    if let Some(path) = value.as_str() {
        let path = star.map_or_else(|| path.to_string(), |star| path.replace('*', star));
        if path.starts_with("./") && !path.contains("..") {
            return Ok(package_root.join(path));
        }
        return Err("package exports target must be a \"./\"-relative path without \"..\"");
    }
    let Some(object) = value.as_object() else {
        return Err(
            "package exports value shape is unsupported (expected string or condition object)",
        );
    };
    let conditions = if import_mode {
        [
            "sloppy",
            "import",
            "node",
            "development",
            "production",
            "default",
        ]
    } else {
        [
            "sloppy",
            "require",
            "node",
            "development",
            "production",
            "default",
        ]
    };
    for condition in conditions {
        if let Some(entry) = object.get(condition) {
            return resolve_exports_value_with_star(package_root, entry, import_mode, star);
        }
    }
    Err("package exports condition object had no matching condition for the current import mode")
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
            classify_import(Path::new("app.js"), "sloppy/data"),
            ImportKind::SlopData
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
            classify_import(Path::new("app.js"), "sloppy/http"),
            ImportKind::SlopHttp
        );
        assert_eq!(
            classify_import(Path::new("app.js"), "sloppy/os"),
            ImportKind::SlopOs
        );
        assert_eq!(
            classify_import(Path::new("app.js"), "sloppy/orm"),
            ImportKind::SlopOrm
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
    fn node_builtin_registry_backing_files_exist_for_every_entry() {
        let manifest_dir = Path::new(env!("CARGO_MANIFEST_DIR"));
        let stdlib_root = fs::canonicalize(manifest_dir.join("../stdlib"))
            .expect("stdlib directory should exist");
        let entries = [
            "node:path",
            "node:events",
            "node:url",
            "node:querystring",
            "node:buffer",
            "node:console",
            "node:constants",
            "node:util",
            "node:timers",
            "node:fs",
            "node:fs/promises",
            "node:os",
            "node:process",
            "node:crypto",
            "node:diagnostics_channel",
            "node:http",
            "node:https",
            "node:module",
            "node:perf_hooks",
            "node:assert",
            "node:assert/strict",
            "node:stream",
            "node:stream/promises",
            "node:string_decoder",
            "node:tty",
            "node:zlib",
        ];

        for specifier in entries {
            let resolution = resolve_node_builtin(specifier)
                .unwrap_or_else(|| panic!("registry should resolve {specifier}"));
            assert_ne!(
                resolution.status, "unsupported",
                "{specifier}: registry entry must not be classified unsupported"
            );
            let Some(backing) = resolution.backing else {
                panic!("{specifier}: registry entry must declare a backing module");
            };
            let path = stdlib_root.join(format!("{backing}.js"));
            assert!(
                path.is_file(),
                "{specifier}: backing module {backing}.js does not exist at {}",
                path.display()
            );
            let body = fs::read_to_string(&path)
                .unwrap_or_else(|error| panic!("backing for {specifier} unreadable: {error}"));
            assert!(
                !body.trim().is_empty(),
                "{specifier}: backing module {backing}.js is empty"
            );
        }
    }

    #[test]
    fn node_compatibility_docs_table_matches_registry() {
        let manifest_dir = Path::new(env!("CARGO_MANIFEST_DIR"));
        let docs_path =
            fs::canonicalize(manifest_dir.join("../docs/reference/node-compatibility.md"))
                .expect("node-compatibility.md should exist");
        let docs = fs::read_to_string(&docs_path).expect("node-compatibility.md should read");
        let entries: &[(&str, &str, &str)] = &[
            ("node:path", "supported", "sloppy/node/path"),
            ("node:events", "supported", "sloppy/node/events"),
            ("node:url", "supported", "sloppy/node/url"),
            ("node:querystring", "supported", "sloppy/node/querystring"),
            ("node:buffer", "partial", "sloppy/node/buffer"),
            ("node:console", "partial", "sloppy/node/console"),
            ("node:constants", "partial", "sloppy/node/constants"),
            ("node:util", "partial", "sloppy/node/util"),
            ("node:timers", "partial", "sloppy/node/timers"),
            ("node:fs", "partial", "sloppy/node/fs"),
            ("node:fs/promises", "partial", "sloppy/node/fs/promises"),
            ("node:os", "partial", "sloppy/node/os"),
            ("node:process", "partial", "sloppy/node/process"),
            ("node:crypto", "partial", "sloppy/node/crypto"),
            (
                "node:diagnostics_channel",
                "partial",
                "sloppy/node/diagnostics_channel",
            ),
            ("node:assert", "partial", "sloppy/node/assert"),
            ("node:assert/strict", "partial", "sloppy/node/assert/strict"),
            ("node:stream", "partial", "sloppy/node/stream"),
            (
                "node:stream/promises",
                "partial",
                "sloppy/node/stream/promises",
            ),
            ("node:module", "partial", "sloppy/node/module"),
            (
                "node:string_decoder",
                "partial",
                "sloppy/node/string_decoder",
            ),
            ("node:zlib", "partial", "sloppy/node/zlib"),
            ("node:perf_hooks", "partial", "sloppy/node/perf_hooks"),
            ("node:tty", "stubbed", "sloppy/node/tty"),
            ("node:http", "stubbed", "sloppy/node/http"),
            ("node:https", "stubbed", "sloppy/node/https"),
        ];
        for (specifier, expected_status, expected_backing) in entries {
            let resolution = resolve_node_builtin(specifier)
                .unwrap_or_else(|| panic!("registry should resolve {specifier}"));
            assert_eq!(
                resolution.status, *expected_status,
                "{specifier}: registry status disagrees with docs"
            );
            assert_eq!(
                resolution.backing,
                Some(*expected_backing),
                "{specifier}: registry backing disagrees with docs"
            );
            assert!(
                docs.contains(&format!("`{specifier}`")) && docs.contains(expected_status),
                "{specifier}: node-compatibility.md must list this specifier with status {expected_status}"
            );
        }
    }

    #[test]
    fn source_root_check_canonicalizes_the_root() {
        let resolved = std::env::current_dir().expect("current directory should exist");
        let relative_root = Path::new(".");
        assert!(stays_within_source_root(&resolved, relative_root));
    }

    #[test]
    fn npm_compat_matrix_resolves_as_expected() {
        let manifest_dir = Path::new(env!("CARGO_MANIFEST_DIR"));
        let matrix_root = fs::canonicalize(manifest_dir.join("../tests/fixtures/npm-compat"))
            .expect("npm-compat fixtures directory should exist");
        let matrix_path = matrix_root.join("matrix.json");
        let matrix_json = fs::read_to_string(&matrix_path).expect("matrix.json should be readable");
        let matrix: Value =
            serde_json::from_str(&matrix_json).expect("matrix should be valid JSON");
        let fixtures = matrix
            .get("fixtures")
            .and_then(Value::as_array)
            .expect("matrix.fixtures array should be present");

        let mut failures: Vec<String> = Vec::new();
        for entry in fixtures {
            let name = entry["name"].as_str().expect("matrix entry name");
            let importer = matrix_root.join(
                entry["importer"]
                    .as_str()
                    .expect("matrix entry importer path"),
            );
            let specifier = entry["specifier"].as_str().expect("matrix entry specifier");
            let import_mode = entry["importMode"].as_bool().unwrap_or(true);
            let expected = entry
                .get("expected")
                .expect("matrix entry expected projection");

            let result = classify_import_with_mode(&importer, specifier, import_mode);
            if let Err(message) = check_matrix_entry(expected, &result, &matrix_root) {
                failures.push(format!("{name}: {message}"));
            }
        }
        assert!(
            failures.is_empty(),
            "npm-compat matrix failures:\n  - {}",
            failures.join("\n  - ")
        );
    }

    fn check_matrix_entry(
        expected: &Value,
        result: &ImportKind,
        matrix_root: &Path,
    ) -> Result<(), String> {
        let expected_kind = expected["kind"]
            .as_str()
            .ok_or_else(|| "expected.kind is required".to_string())?;
        match (expected_kind, result) {
            ("package", ImportKind::Package(package)) => {
                check_expected_package(expected, package, matrix_root)
            }
            ("packageExportUnsupported", ImportKind::PackageExportUnsupported(failure)) => {
                if let Some(expected_specifier) = expected.get("specifier").and_then(Value::as_str)
                {
                    if failure.subject != expected_specifier {
                        return Err(format!(
                            "subject mismatch: expected {expected_specifier}, got {}",
                            failure.subject
                        ));
                    }
                }
                if let Some(expected_field) = expected.get("field").and_then(Value::as_str) {
                    if failure.field != expected_field {
                        return Err(format!(
                            "field mismatch: expected {expected_field}, got {}",
                            failure.field
                        ));
                    }
                }
                if let Some(expected_reason) = expected.get("reason").and_then(Value::as_str) {
                    if !failure.reason.contains(expected_reason) {
                        return Err(format!(
                            "reason mismatch: expected substring {expected_reason}, got {}",
                            failure.reason
                        ));
                    }
                }
                Ok(())
            }
            ("nativeAddonUnsupported", ImportKind::NativeAddonUnsupported(package)) => {
                if let Some(expected_name) = expected.get("packageName").and_then(Value::as_str) {
                    if package.name != expected_name {
                        return Err(format!(
                            "native package name mismatch: expected {expected_name}, got {}",
                            package.name
                        ));
                    }
                }
                Ok(())
            }
            ("nodeBuiltin", ImportKind::NodeBuiltin(builtin)) => {
                if let Some(expected_specifier) = expected.get("specifier").and_then(Value::as_str)
                {
                    if builtin.specifier != expected_specifier {
                        return Err(format!(
                            "builtin specifier mismatch: expected {expected_specifier}, got {}",
                            builtin.specifier
                        ));
                    }
                }
                if let Some(expected_status) = expected.get("builtinStatus").and_then(Value::as_str)
                {
                    if builtin.status != expected_status {
                        return Err(format!(
                            "builtin status mismatch: expected {expected_status}, got {}",
                            builtin.status
                        ));
                    }
                }
                Ok(())
            }
            (kind, result) => Err(format!("expected {kind}, got {result:?}")),
        }
    }

    fn check_expected_package(
        expected: &Value,
        package: &PackageResolution,
        matrix_root: &Path,
    ) -> Result<(), String> {
        if let Some(expected_name) = expected.get("packageName").and_then(Value::as_str) {
            if package.name != expected_name {
                return Err(format!(
                    "package name mismatch: expected {expected_name}, got {}",
                    package.name
                ));
            }
        }
        if let Some(expected_entry) = expected.get("entry").and_then(Value::as_str) {
            let expected_path =
                fs::canonicalize(matrix_root.join(expected_entry)).map_err(|error| {
                    format!("expected entry {expected_entry} should canonicalize: {error}")
                })?;
            if package.entry != expected_path {
                return Err(format!(
                    "entry mismatch: expected {}, got {}",
                    expected_path.display(),
                    package.entry.display()
                ));
            }
        }
        if let Some(expected_format) = expected.get("format").and_then(Value::as_str) {
            if package.format.as_str() != expected_format {
                return Err(format!(
                    "format mismatch: expected {expected_format}, got {}",
                    package.format.as_str()
                ));
            }
        }
        if let Some(expected_source) = expected.get("source").and_then(Value::as_str) {
            if package.source != expected_source {
                return Err(format!(
                    "source mismatch: expected {expected_source}, got {}",
                    package.source
                ));
            }
        }
        Ok(())
    }
}

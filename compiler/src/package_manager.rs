use std::{
    collections::{BTreeMap, BTreeSet},
    env, fs,
    io::{self, Cursor, Read, Seek, Write},
    path::{Component, Path, PathBuf},
};

use serde_json::{json, Map, Value};
use sha2::{Digest, Sha256};
use zip::{write::SimpleFileOptions, CompressionMethod, ZipArchive, ZipWriter};

const LOCKFILE_NAME: &str = "sloppy.lock.json";
const ASSETS_PATH: &str = ".sloppy/obj/project.assets.json";
const MANIFEST_NAME: &str = "manifest.json";
const SUPPORTED_TARGET: &str = "sloppy1.0";
const SUPPORTED_RIDS: &[&str] = &[
    "win-x64",
    "win-arm64",
    "linux-x64",
    "linux-arm64",
    "macos-x64",
    "macos-arm64",
];

#[derive(Debug, Clone, Eq, PartialEq)]
pub struct PackageError {
    pub code: &'static str,
    pub message: String,
}

impl PackageError {
    fn new(code: &'static str, message: impl Into<String>) -> Self {
        Self {
            code,
            message: message.into(),
        }
    }

    pub fn render(&self) -> String {
        format!("{}: {}", self.code, self.message)
    }
}

type Result<T> = std::result::Result<T, PackageError>;

#[derive(Debug, Clone, Copy, Eq, PartialEq, Ord, PartialOrd)]
struct Version {
    major: u64,
    minor: u64,
    patch: u64,
}

impl Version {
    fn parse(text: &str) -> Result<Self> {
        let parts = text.split('.').collect::<Vec<_>>();
        if parts.len() != 3 || parts.iter().any(|part| part.is_empty()) {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_VERSION_INVALID",
                format!("package version '{text}' must use major.minor.patch"),
            ));
        }
        let mut parsed = [0_u64; 3];
        for (index, part) in parts.iter().enumerate() {
            if !part.bytes().all(|byte| byte.is_ascii_digit()) {
                return Err(PackageError::new(
                    "SLOPPY_E_PACKAGE_VERSION_INVALID",
                    format!("package version '{text}' must be numeric major.minor.patch"),
                ));
            }
            parsed[index] = part.parse::<u64>().map_err(|_| {
                PackageError::new(
                    "SLOPPY_E_PACKAGE_VERSION_INVALID",
                    format!("package version '{text}' is out of range"),
                )
            })?;
        }
        Ok(Self {
            major: parsed[0],
            minor: parsed[1],
            patch: parsed[2],
        })
    }
}

impl std::fmt::Display for Version {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(formatter, "{}.{}.{}", self.major, self.minor, self.patch)
    }
}

#[derive(Debug, Clone, Eq, PartialEq)]
pub struct VersionRange {
    lower: Version,
    upper: Option<Version>,
    exact: bool,
}

impl VersionRange {
    pub fn parse(text: &str) -> Result<Self> {
        if !text.starts_with('[') || !(text.ends_with(']') || text.ends_with(')')) {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_RANGE_INVALID",
                format!("unsupported package version range '{text}'"),
            ));
        }
        let inner = &text[1..text.len() - 1];
        if !inner.contains(',') {
            if !text.ends_with(']') {
                return Err(PackageError::new(
                    "SLOPPY_E_PACKAGE_RANGE_INVALID",
                    format!("unsupported package version range '{text}'"),
                ));
            }
            return Ok(Self {
                lower: Version::parse(inner)?,
                upper: None,
                exact: true,
            });
        }
        let Some((lower, upper)) = inner.split_once(',') else {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_RANGE_INVALID",
                format!("unsupported package version range '{text}'"),
            ));
        };
        if lower.is_empty() {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_RANGE_INVALID",
                format!("range '{text}' must include a lower bound"),
            ));
        }
        Ok(Self {
            lower: Version::parse(lower)?,
            upper: if upper.is_empty() {
                None
            } else {
                Some(Version::parse(upper)?)
            },
            exact: false,
        })
    }

    fn allows(&self, version: Version) -> bool {
        if self.exact {
            return version == self.lower;
        }
        version >= self.lower && self.upper.is_none_or(|upper| version < upper)
    }
}

#[derive(Debug, Clone)]
struct PackageManifest {
    id: String,
    normalized_id: String,
    version: Version,
    dependencies: BTreeMap<String, String>,
    compile_assets: Vec<String>,
    runtime_assets: Vec<String>,
    native_libraries: BTreeMap<String, BTreeMap<String, String>>,
    capabilities: Vec<String>,
    manifest_json: Value,
}

#[derive(Debug, Clone)]
struct PackageCandidate {
    manifest: PackageManifest,
    source_display: String,
    artifact: PathBuf,
    sha256: String,
}

#[derive(Debug, Clone)]
struct ResolvedPackage {
    candidate: PackageCandidate,
    cache_path: PathBuf,
    selected_native: BTreeMap<String, String>,
    selected_hashes: BTreeMap<String, String>,
}

pub fn pack_current_project() -> Result<PathBuf> {
    let cwd = env::current_dir().map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
    let manifest_path = cwd.join("sloppy.json");
    let manifest_json = read_json_file(&manifest_path)?;
    reject_scripts(&manifest_json)?;
    let manifest = parse_package_manifest(manifest_json)?;
    let mut archive_paths = BTreeSet::new();
    let mut normalized_archive_paths = BTreeSet::new();
    archive_paths.insert(MANIFEST_NAME.to_string());
    normalized_archive_paths.insert(normalize_archive_path(MANIFEST_NAME));
    for path in declared_asset_paths(&manifest) {
        validate_relative_archive_path(&path)?;
        if !normalized_archive_paths.insert(normalize_archive_path(&path)) {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_DUPLICATE_PATH",
                format!("duplicate package archive path '{path}'"),
            ));
        }
        archive_paths.insert(path.clone());
        if !cwd.join(&path).is_file() {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
                format!("declared package file is missing: {path}"),
            ));
        }
    }

    let output_dir = cwd.join("artifacts").join("packages");
    fs::create_dir_all(&output_dir).map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
    let output_path = output_dir.join(format!(
        "{}.{}.slpkg",
        manifest.normalized_id, manifest.version
    ));
    let file =
        fs::File::create(&output_path).map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
    let mut zip = ZipWriter::new(file);
    let options = SimpleFileOptions::default()
        .compression_method(CompressionMethod::Stored)
        .last_modified_time(zip::DateTime::default());

    let mut packed_manifest = manifest.manifest_json.clone();
    let mut hashes = Map::new();
    for archive_path in archive_paths
        .iter()
        .filter(|path| path.as_str() != MANIFEST_NAME)
    {
        let bytes = fs::read(cwd.join(archive_path))
            .map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
        hashes.insert(archive_path.clone(), Value::String(sha256_bytes(&bytes)));
    }
    if let Value::Object(object) = &mut packed_manifest {
        object.insert("sha256".to_string(), Value::Object(hashes));
    }
    let manifest_bytes = serde_json::to_vec_pretty(&packed_manifest).map_err(|error| {
        PackageError::new("SLOPPY_E_PACKAGE_MANIFEST_INVALID", error.to_string())
    })?;
    zip.start_file(MANIFEST_NAME, options)
        .map_err(package_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
    zip.write_all(&manifest_bytes)
        .map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;

    for archive_path in archive_paths
        .iter()
        .filter(|path| path.as_str() != MANIFEST_NAME)
    {
        zip.start_file(archive_path, options)
            .map_err(package_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
        let bytes = fs::read(cwd.join(archive_path))
            .map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
        zip.write_all(&bytes)
            .map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
    }
    zip.finish()
        .map_err(package_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
    Ok(output_path)
}

pub fn restore_current_project(locked: bool) -> Result<()> {
    let cwd = env::current_dir().map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
    let project = read_json_file(&cwd.join("sloppy.json"))?;
    let target = json_string(&project, "target").unwrap_or_else(|| SUPPORTED_TARGET.to_string());
    if target != SUPPORTED_TARGET {
        return Err(PackageError::new(
            "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
            format!("unsupported target '{target}'"),
        ));
    }
    let runtime_identifier = json_string(&project, "runtimeIdentifier")
        .or_else(host_runtime_identifier)
        .ok_or_else(|| {
            PackageError::new(
                "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
                "runtimeIdentifier is required on this host",
            )
        })?;
    if !SUPPORTED_RIDS.contains(&runtime_identifier.as_str()) {
        return Err(PackageError::new(
            "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
            format!("unsupported runtimeIdentifier '{runtime_identifier}'"),
        ));
    }
    let dependencies = dependency_map(&project)?;
    let sources = package_sources(&cwd, &project)?;
    let previous_lock = read_optional_json(&cwd.join(LOCKFILE_NAME))?;
    let locked_versions = previous_lock
        .as_ref()
        .map(locked_version_map)
        .transpose()?
        .unwrap_or_default();
    let candidates = discover_candidates(&sources)?;
    let resolved = resolve_packages(
        &dependencies,
        &candidates,
        &locked_versions,
        &target,
        &runtime_identifier,
    )?;
    let cache_root = cache_root()?;
    let mut restored = Vec::new();
    for package in resolved {
        let cache_path = restore_to_cache(&package.candidate, &cache_root)?;
        restored.push(ResolvedPackage {
            cache_path,
            ..package
        });
    }
    let lockfile = lockfile_json(&target, &runtime_identifier, &restored);
    if locked {
        let Some(previous) = previous_lock else {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_LOCK_OUT_OF_DATE",
                "sloppy.lock.json is required for --locked restore",
            ));
        };
        if previous != lockfile {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_LOCK_OUT_OF_DATE",
                "restore would change sloppy.lock.json",
            ));
        }
    } else {
        write_json_file(&cwd.join(LOCKFILE_NAME), &lockfile)?;
    }
    let assets = assets_json(&target, &runtime_identifier, &restored);
    write_json_file(&cwd.join(ASSETS_PATH), &assets)?;
    Ok(())
}

fn read_json_file(path: &Path) -> Result<Value> {
    let text = fs::read_to_string(path).map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
    serde_json::from_str(&text).map_err(|error| {
        PackageError::new(
            "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
            format!("malformed JSON in {}: {error}", path.display()),
        )
    })
}

fn read_optional_json(path: &Path) -> Result<Option<Value>> {
    if !path.exists() {
        return Ok(None);
    }
    read_json_file(path).map(Some)
}

fn write_json_file(path: &Path, value: &Value) -> Result<()> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
    }
    let mut text = serde_json::to_string_pretty(value).map_err(|error| {
        PackageError::new("SLOPPY_E_PACKAGE_MANIFEST_INVALID", error.to_string())
    })?;
    text.push('\n');
    fs::write(path, text).map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))
}

fn parse_package_manifest(value: Value) -> Result<PackageManifest> {
    if !value.is_object() {
        return Err(PackageError::new(
            "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
            "package manifest root must be an object",
        ));
    }
    reject_scripts(&value)?;
    let id = json_string_required(&value, "id", "SLOPPY_E_PACKAGE_ID_INVALID")?;
    validate_package_id(&id)?;
    let version_text = json_string_required(&value, "version", "SLOPPY_E_PACKAGE_VERSION_INVALID")?;
    let version = Version::parse(&version_text)?;
    let dependencies = dependency_map(&value)?;
    let capabilities = string_array(&value, "capabilities")?;
    let (compile_assets, runtime_assets) = target_assets(&value)?;
    let native_libraries = native_libraries(&value)?;
    Ok(PackageManifest {
        normalized_id: normalize_id(&id),
        id,
        version,
        dependencies,
        compile_assets,
        runtime_assets,
        native_libraries,
        capabilities,
        manifest_json: value,
    })
}

fn json_string_required(value: &Value, key: &str, code: &'static str) -> Result<String> {
    json_string(value, key).ok_or_else(|| PackageError::new(code, format!("'{key}' is required")))
}

fn json_string(value: &Value, key: &str) -> Option<String> {
    value.get(key).and_then(Value::as_str).map(str::to_string)
}

fn validate_package_id(id: &str) -> Result<()> {
    if id.is_empty()
        || id.starts_with('.')
        || id.ends_with('.')
        || !id
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'.' | b'-' | b'_'))
    {
        return Err(PackageError::new(
            "SLOPPY_E_PACKAGE_ID_INVALID",
            format!("invalid package id '{id}'"),
        ));
    }
    Ok(())
}

fn normalize_id(id: &str) -> String {
    id.to_ascii_lowercase()
}

fn dependency_map(value: &Value) -> Result<BTreeMap<String, String>> {
    let mut dependencies = BTreeMap::new();
    let Some(object) = value.get("dependencies") else {
        return Ok(dependencies);
    };
    let Some(object) = object.as_object() else {
        return Err(PackageError::new(
            "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
            "dependencies must be an object",
        ));
    };
    for (id, range) in object {
        validate_package_id(id)?;
        let Some(range) = range.as_str() else {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_RANGE_INVALID",
                format!("dependency '{id}' must declare a string range"),
            ));
        };
        VersionRange::parse(range)?;
        dependencies.insert(normalize_id(id), range.to_string());
    }
    Ok(dependencies)
}

fn string_array(value: &Value, key: &str) -> Result<Vec<String>> {
    let Some(array) = value.get(key) else {
        return Ok(Vec::new());
    };
    let Some(array) = array.as_array() else {
        return Err(PackageError::new(
            "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
            format!("{key} must be an array"),
        ));
    };
    let mut strings = Vec::with_capacity(array.len());
    for item in array {
        let Some(text) = item.as_str() else {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
                format!("{key} entries must be strings"),
            ));
        };
        strings.push(text.to_string());
    }
    strings.sort();
    strings.dedup();
    Ok(strings)
}

fn target_assets(value: &Value) -> Result<(Vec<String>, Vec<String>)> {
    let Some(targets) = value.get("targets") else {
        return Ok((Vec::new(), Vec::new()));
    };
    let Some(target) = targets.get(SUPPORTED_TARGET) else {
        return Ok((Vec::new(), Vec::new()));
    };
    let compile = string_array(target, "compile")?;
    let runtime = string_array(target, "runtime")?;
    for path in compile.iter().chain(runtime.iter()) {
        validate_relative_archive_path(path)?;
    }
    Ok((compile, runtime))
}

fn native_libraries(value: &Value) -> Result<BTreeMap<String, BTreeMap<String, String>>> {
    let mut output = BTreeMap::new();
    let Some(libraries) = value
        .get("native")
        .and_then(|native| native.get("libraries"))
    else {
        return Ok(output);
    };
    let Some(libraries) = libraries.as_object() else {
        return Err(PackageError::new(
            "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
            "native.libraries must be an object",
        ));
    };
    for (logical_name, rid_map) in libraries {
        if logical_name.is_empty() {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
                "native library logical name must not be empty",
            ));
        }
        let Some(rid_map) = rid_map.as_object() else {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
                format!("native library '{logical_name}' RID map must be an object"),
            ));
        };
        let mut entries = BTreeMap::new();
        for (rid, path) in rid_map {
            if !SUPPORTED_RIDS.contains(&rid.as_str()) {
                return Err(PackageError::new(
                    "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
                    format!("unsupported runtime identifier '{rid}'"),
                ));
            }
            let Some(path) = path.as_str() else {
                return Err(PackageError::new(
                    "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
                    format!("native library '{logical_name}' path must be a string"),
                ));
            };
            validate_relative_archive_path(path)?;
            entries.insert(rid.to_string(), path.to_string());
        }
        output.insert(logical_name.to_string(), entries);
    }
    Ok(output)
}

fn reject_scripts(value: &Value) -> Result<()> {
    for key in ["install", "postinstall", "preinstall", "prepare", "scripts"] {
        if value.get(key).is_some() {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_SCRIPT_UNSUPPORTED",
                format!("package script declaration '{key}' is not supported"),
            ));
        }
    }
    Ok(())
}

fn declared_asset_paths(manifest: &PackageManifest) -> Vec<String> {
    let mut paths = BTreeSet::new();
    paths.extend(manifest.compile_assets.iter().cloned());
    paths.extend(manifest.runtime_assets.iter().cloned());
    for rid_map in manifest.native_libraries.values() {
        paths.extend(rid_map.values().cloned());
    }
    paths.into_iter().collect()
}

fn validate_relative_archive_path(path: &str) -> Result<()> {
    let path_object = Path::new(path);
    if path.is_empty()
        || path.contains('\\')
        || path_object.is_absolute()
        || path.starts_with('/')
        || path.contains('\0')
        || path_object.components().any(|component| {
            matches!(
                component,
                Component::ParentDir | Component::RootDir | Component::Prefix(_)
            )
        })
    {
        return Err(PackageError::new(
            "SLOPPY_E_PACKAGE_PATH_TRAVERSAL",
            format!("package path must be relative and stay inside the package: {path}"),
        ));
    }
    Ok(())
}

fn normalize_archive_path(path: &str) -> String {
    path.replace('\\', "/").to_ascii_lowercase()
}

fn package_sources(cwd: &Path, project: &Value) -> Result<Vec<(PathBuf, String)>> {
    let Some(sources) = project.get("packageSources").and_then(Value::as_array) else {
        return Err(PackageError::new(
            "SLOPPY_E_PACKAGE_SOURCE_MISSING",
            "sloppy.json must declare packageSources for local restore",
        ));
    };
    let mut output = Vec::new();
    for source in sources {
        let Some(source_text) = source.as_str() else {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_SOURCE_MISSING",
                "packageSources entries must be strings",
            ));
        };
        validate_source_path(source_text)?;
        let path = cwd.join(source_text);
        if !path.is_dir() {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_SOURCE_MISSING",
                format!("package source does not exist: {source_text}"),
            ));
        }
        output.push((path, source_text.replace('\\', "/")));
    }
    output.sort_by(|left, right| left.1.cmp(&right.1));
    Ok(output)
}

fn validate_source_path(path: &str) -> Result<()> {
    if path.is_empty()
        || path.contains('\0')
        || Path::new(path)
            .components()
            .any(|component| matches!(component, Component::ParentDir))
    {
        return Err(PackageError::new(
            "SLOPPY_E_PACKAGE_PATH_TRAVERSAL",
            format!("package source must be a safe local path: {path}"),
        ));
    }
    Ok(())
}

fn discover_candidates(sources: &[(PathBuf, String)]) -> Result<Vec<PackageCandidate>> {
    let mut candidates = Vec::new();
    for (source_path, source_display) in sources {
        let mut artifacts = fs::read_dir(source_path)
            .map_err(io_error("SLOPPY_E_PACKAGE_SOURCE_MISSING"))?
            .filter_map(|entry| entry.ok().map(|entry| entry.path()))
            .filter(|path| path.extension().and_then(|ext| ext.to_str()) == Some("slpkg"))
            .collect::<Vec<_>>();
        artifacts.sort();
        for artifact in artifacts {
            let bytes =
                fs::read(&artifact).map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
            let sha256 = sha256_bytes(&bytes);
            let manifest = read_manifest_from_archive(Cursor::new(bytes))?;
            candidates.push(PackageCandidate {
                manifest,
                source_display: source_display.clone(),
                artifact,
                sha256,
            });
        }
    }
    Ok(candidates)
}

fn read_manifest_from_archive<R: Read + Seek>(reader: R) -> Result<PackageManifest> {
    let mut archive =
        ZipArchive::new(reader).map_err(package_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
    let mut seen = BTreeSet::new();
    let mut manifest_text = String::new();
    for index in 0..archive.len() {
        let mut file = archive
            .by_index(index)
            .map_err(package_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
        reject_unsafe_zip_entry(file.unix_mode())?;
        let name = file.name().to_string();
        validate_relative_archive_path(&name)?;
        if name.ends_with('/') {
            continue;
        }
        if !seen.insert(name.clone()) {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_DUPLICATE_PATH",
                format!("duplicate archive path '{name}'"),
            ));
        }
        if name == MANIFEST_NAME {
            file.read_to_string(&mut manifest_text)
                .map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
        }
    }
    if manifest_text.is_empty() {
        return Err(PackageError::new(
            "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
            "package archive is missing manifest.json",
        ));
    }
    let manifest_json = serde_json::from_str(&manifest_text).map_err(|error| {
        PackageError::new(
            "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
            format!("malformed package manifest.json: {error}"),
        )
    })?;
    parse_package_manifest(manifest_json)
}

fn resolve_packages(
    direct_dependencies: &BTreeMap<String, String>,
    candidates: &[PackageCandidate],
    locked_versions: &BTreeMap<String, Version>,
    target: &str,
    runtime_identifier: &str,
) -> Result<Vec<ResolvedPackage>> {
    let mut constraints: BTreeMap<String, Vec<String>> = BTreeMap::new();
    for (id, range) in direct_dependencies {
        constraints
            .entry(id.clone())
            .or_default()
            .push(range.clone());
    }
    let mut resolved: BTreeMap<String, ResolvedPackage> = BTreeMap::new();
    let mut changed = true;
    while changed {
        changed = false;
        let pending = constraints.keys().cloned().collect::<Vec<_>>();
        for id in pending {
            if let Some(existing) = resolved.get(&id) {
                ensure_resolved_package_still_allowed(
                    &id,
                    existing.candidate.manifest.version,
                    constraints.get(&id).map(Vec::as_slice).unwrap_or(&[]),
                )?;
                continue;
            }
            let ranges = constraints.get(&id).cloned().unwrap_or_default();
            let candidate = select_candidate(&id, &ranges, candidates, locked_versions)?;
            if !candidate.manifest.compile_assets.is_empty() && target != SUPPORTED_TARGET {
                return Err(PackageError::new(
                    "SLOPPY_E_PACKAGE_CONFLICT",
                    format!(
                        "package '{}' does not support target '{target}'",
                        candidate.manifest.id
                    ),
                ));
            }
            let selected_native = select_native_assets(&candidate.manifest, runtime_identifier)?;
            let mut selected_hashes = BTreeMap::new();
            for path in candidate
                .manifest
                .compile_assets
                .iter()
                .chain(candidate.manifest.runtime_assets.iter())
                .chain(selected_native.values())
            {
                selected_hashes
                    .insert(path.clone(), hash_archive_entry(&candidate.artifact, path)?);
            }
            for (dependency_id, dependency_range) in &candidate.manifest.dependencies {
                constraints
                    .entry(dependency_id.clone())
                    .or_default()
                    .push(dependency_range.clone());
            }
            resolved.insert(
                id,
                ResolvedPackage {
                    candidate,
                    cache_path: PathBuf::new(),
                    selected_native,
                    selected_hashes,
                },
            );
            changed = true;
        }
    }
    Ok(resolved.into_values().collect())
}

fn ensure_resolved_package_still_allowed(
    id: &str,
    version: Version,
    ranges: &[String],
) -> Result<()> {
    for range in ranges {
        if !VersionRange::parse(range)?.allows(version) {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_CONFLICT",
                format!("resolved package '{id}' {version} does not satisfy {range}"),
            ));
        }
    }
    Ok(())
}

fn select_candidate(
    id: &str,
    ranges: &[String],
    candidates: &[PackageCandidate],
    locked_versions: &BTreeMap<String, Version>,
) -> Result<PackageCandidate> {
    let parsed_ranges = ranges
        .iter()
        .map(|range| VersionRange::parse(range))
        .collect::<Result<Vec<_>>>()?;
    let mut matches = candidates
        .iter()
        .filter(|candidate| candidate.manifest.normalized_id == id)
        .filter(|candidate| {
            parsed_ranges
                .iter()
                .all(|range| range.allows(candidate.manifest.version))
        })
        .cloned()
        .collect::<Vec<_>>();
    if matches.is_empty() {
        let code = if candidates
            .iter()
            .any(|candidate| candidate.manifest.normalized_id == id)
        {
            "SLOPPY_E_PACKAGE_CONFLICT"
        } else {
            "SLOPPY_E_PACKAGE_NOT_FOUND"
        };
        return Err(PackageError::new(
            code,
            format!("no package '{id}' satisfies {}", ranges.join(", ")),
        ));
    }
    matches.sort_by(|left, right| {
        left.manifest
            .version
            .cmp(&right.manifest.version)
            .then_with(|| left.source_display.cmp(&right.source_display))
            .then_with(|| left.artifact.cmp(&right.artifact))
    });
    if let Some(locked) = locked_versions.get(id) {
        if let Some(candidate) = matches
            .iter()
            .find(|candidate| candidate.manifest.version == *locked)
        {
            return Ok(candidate.clone());
        }
    }
    matches.pop().ok_or_else(|| {
        PackageError::new(
            "SLOPPY_E_PACKAGE_NOT_FOUND",
            format!("no package '{id}' satisfies {}", ranges.join(", ")),
        )
    })
}

fn select_native_assets(
    manifest: &PackageManifest,
    runtime_identifier: &str,
) -> Result<BTreeMap<String, String>> {
    let mut selected = BTreeMap::new();
    for (logical_name, rid_map) in &manifest.native_libraries {
        let Some(path) = rid_map.get(runtime_identifier) else {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_NATIVE_ASSET_MISSING",
                format!(
                    "package '{}' has no native asset '{}' for {runtime_identifier}",
                    manifest.id, logical_name
                ),
            ));
        };
        selected.insert(logical_name.clone(), path.clone());
    }
    Ok(selected)
}

fn hash_archive_entry(artifact: &Path, path: &str) -> Result<String> {
    let file = fs::File::open(artifact).map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
    let mut archive =
        ZipArchive::new(file).map_err(package_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
    let mut entry = archive.by_name(path).map_err(|_| {
        PackageError::new(
            "SLOPPY_E_PACKAGE_NATIVE_ASSET_MISSING",
            format!("package asset is missing from archive: {path}"),
        )
    })?;
    let mut bytes = Vec::new();
    entry
        .read_to_end(&mut bytes)
        .map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
    Ok(sha256_bytes(&bytes))
}

fn cache_root() -> Result<PathBuf> {
    if let Some(path) = env::var_os("SLOPPY_PACKAGE_CACHE") {
        return Ok(PathBuf::from(path));
    }
    let home = env::var_os("USERPROFILE")
        .or_else(|| env::var_os("HOME"))
        .ok_or_else(|| {
            PackageError::new(
                "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
                "USERPROFILE or HOME is required to locate the Sloppy package cache",
            )
        })?;
    Ok(PathBuf::from(home).join(".sloppy").join("packages"))
}

fn restore_to_cache(candidate: &PackageCandidate, cache_root: &Path) -> Result<PathBuf> {
    let package_root = cache_root
        .join(&candidate.manifest.normalized_id)
        .join(candidate.manifest.version.to_string());
    let marker = package_root.join(".sloppy.package.sha256");
    if package_root.exists() {
        if marker.is_file() {
            let existing =
                fs::read_to_string(&marker).map_err(io_error("SLOPPY_E_PACKAGE_HASH_MISMATCH"))?;
            if existing.trim() == candidate.sha256 {
                return Ok(package_root);
            }
        }
        return Err(PackageError::new(
            "SLOPPY_E_PACKAGE_HASH_MISMATCH",
            format!(
                "cached package '{}' {} does not match selected artifact hash",
                candidate.manifest.id, candidate.manifest.version
            ),
        ));
    }
    fs::create_dir_all(&package_root).map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
    let result = extract_archive_safely(&candidate.artifact, &package_root).and_then(|()| {
        fs::write(&marker, &candidate.sha256).map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))
    });
    if let Err(error) = result {
        let _ = fs::remove_dir_all(&package_root);
        return Err(error);
    }
    Ok(package_root)
}

fn extract_archive_safely(artifact: &Path, destination: &Path) -> Result<()> {
    let file = fs::File::open(artifact).map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
    let mut archive =
        ZipArchive::new(file).map_err(package_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
    let canonical_destination =
        fs::canonicalize(destination).map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
    let mut seen = BTreeSet::new();
    for index in 0..archive.len() {
        let mut entry = archive
            .by_index(index)
            .map_err(package_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
        reject_unsafe_zip_entry(entry.unix_mode())?;
        let name = entry.name().to_string();
        validate_relative_archive_path(&name)?;
        if name.ends_with('/') {
            continue;
        }
        if !seen.insert(name.clone()) {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_DUPLICATE_PATH",
                format!("duplicate archive path '{name}'"),
            ));
        }
        let output = destination.join(&name);
        if let Some(parent) = output.parent() {
            fs::create_dir_all(parent).map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
        }
        let canonical_parent = output
            .parent()
            .and_then(|parent| fs::canonicalize(parent).ok())
            .ok_or_else(|| {
                PackageError::new(
                    "SLOPPY_E_PACKAGE_PATH_TRAVERSAL",
                    format!("package path escaped cache root: {name}"),
                )
            })?;
        if !canonical_parent.starts_with(&canonical_destination) {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_PATH_TRAVERSAL",
                format!("package path escaped cache root: {name}"),
            ));
        }
        let mut output_file =
            fs::File::create(&output).map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
        io::copy(&mut entry, &mut output_file)
            .map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
    }
    Ok(())
}

fn reject_unsafe_zip_entry(unix_mode: Option<u32>) -> Result<()> {
    let Some(mode) = unix_mode else {
        return Ok(());
    };
    let file_type = mode & 0o170000;
    if file_type != 0 && file_type != 0o100000 && file_type != 0o040000 {
        return Err(PackageError::new(
            "SLOPPY_E_PACKAGE_PATH_TRAVERSAL",
            "package archives must not contain symlinks, hardlinks, or special entries",
        ));
    }
    Ok(())
}

fn locked_version_map(lockfile: &Value) -> Result<BTreeMap<String, Version>> {
    let mut versions = BTreeMap::new();
    let Some(packages) = lockfile.get("packages").and_then(Value::as_object) else {
        return Ok(versions);
    };
    for package in packages.values() {
        let id = json_string_required(package, "id", "SLOPPY_E_PACKAGE_LOCK_OUT_OF_DATE")?;
        let version =
            json_string_required(package, "version", "SLOPPY_E_PACKAGE_LOCK_OUT_OF_DATE")?;
        versions.insert(normalize_id(&id), Version::parse(&version)?);
    }
    Ok(versions)
}

fn lockfile_json(target: &str, runtime_identifier: &str, packages: &[ResolvedPackage]) -> Value {
    let mut package_map = Map::new();
    for package in packages {
        let key = format!(
            "{}/{}",
            package.candidate.manifest.normalized_id, package.candidate.manifest.version
        );
        package_map.insert(
            key,
            json!({
                "id": package.candidate.manifest.id,
                "version": package.candidate.manifest.version.to_string(),
                "source": package.candidate.source_display,
                "sha256": package.candidate.sha256,
                "dependencies": package.candidate.manifest.dependencies,
                "assets": {
                    "compile": package.candidate.manifest.compile_assets,
                    "runtime": package.candidate.manifest.runtime_assets,
                    "native": package.selected_native,
                },
                "assetHashes": package.selected_hashes,
                "capabilities": package.candidate.manifest.capabilities,
            }),
        );
    }
    json!({
        "version": 1,
        "target": target,
        "runtimeIdentifier": runtime_identifier,
        "packages": package_map,
    })
}

fn assets_json(target: &str, runtime_identifier: &str, packages: &[ResolvedPackage]) -> Value {
    let mut package_values = Vec::new();
    for package in packages {
        let mut native = Map::new();
        for (logical_name, path) in &package.selected_native {
            native.insert(
                logical_name.clone(),
                json!({
                    "path": path,
                    "sha256": package.selected_hashes.get(path).cloned().unwrap_or_default(),
                }),
            );
        }
        package_values.push(json!({
            "id": package.candidate.manifest.id,
            "version": package.candidate.manifest.version.to_string(),
            "path": package.cache_path.to_string_lossy().replace('\\', "/"),
            "compile": package.candidate.manifest.compile_assets,
            "runtime": package.candidate.manifest.runtime_assets,
            "nativeLibraries": native,
            "capabilities": package.candidate.manifest.capabilities,
        }));
    }
    json!({
        "version": 1,
        "target": target,
        "runtimeIdentifier": runtime_identifier,
        "packages": package_values,
    })
}

fn host_runtime_identifier() -> Option<String> {
    let os = env::consts::OS;
    let arch = env::consts::ARCH;
    match (os, arch) {
        ("windows", "x86_64") => Some("win-x64".to_string()),
        ("windows", "aarch64") => Some("win-arm64".to_string()),
        ("linux", "x86_64") => Some("linux-x64".to_string()),
        ("linux", "aarch64") => Some("linux-arm64".to_string()),
        ("macos", "x86_64") => Some("macos-x64".to_string()),
        ("macos", "aarch64") => Some("macos-arm64".to_string()),
        _ => None,
    }
}

fn sha256_bytes(bytes: &[u8]) -> String {
    let digest = Sha256::digest(bytes);
    let mut output = String::with_capacity(64);
    for byte in digest {
        output.push_str(&format!("{byte:02x}"));
    }
    output
}

fn io_error(code: &'static str) -> impl FnOnce(io::Error) -> PackageError {
    move |error| PackageError::new(code, error.to_string())
}

fn package_error<E: std::fmt::Display>(code: &'static str) -> impl FnOnce(E) -> PackageError {
    move |error| PackageError::new(code, error.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Mutex;
    use std::time::{SystemTime, UNIX_EPOCH};

    static ENV_LOCK: Mutex<()> = Mutex::new(());

    #[test]
    fn version_ranges_accept_exact_range_and_open_ended_forms() {
        let exact = VersionRange::parse("[1.2.3]").expect("exact range");
        assert!(exact.allows(Version::parse("1.2.3").expect("version")));
        assert!(!exact.allows(Version::parse("1.2.4").expect("version")));

        let bounded = VersionRange::parse("[1.0.0,2.0.0)").expect("bounded range");
        assert!(bounded.allows(Version::parse("1.0.0").expect("version")));
        assert!(bounded.allows(Version::parse("1.5.0").expect("version")));
        assert!(!bounded.allows(Version::parse("2.0.0").expect("version")));

        let open = VersionRange::parse("[1.0.0,)").expect("open range");
        assert!(open.allows(Version::parse("9.0.0").expect("version")));
        assert!(VersionRange::parse("(1.0.0,2.0.0)").is_err());
    }

    #[test]
    fn manifest_validation_rejects_missing_id_scripts_and_unsafe_paths() {
        let valid = json!({
            "id": "Sloppy.Example",
            "version": "0.1.0",
            "targets": {
                "sloppy1.0": { "compile": ["lib/index.ts"], "runtime": [] }
            }
        });
        assert!(parse_package_manifest(valid).is_ok());
        assert!(parse_package_manifest(json!({"version": "0.1.0"})).is_err());
        assert!(parse_package_manifest(json!({"id": "A", "version": "bad"})).is_err());
        assert!(
            parse_package_manifest(json!({"id": "A", "version": "0.1.0", "postinstall": "x"}))
                .is_err()
        );
        assert!(parse_package_manifest(json!({
            "id": "A",
            "version": "0.1.0",
            "targets": { "sloppy1.0": { "compile": ["/abs.ts"], "runtime": [] } }
        }))
        .is_err());
        assert!(parse_package_manifest(json!({
            "id": "A",
            "version": "0.1.0",
            "targets": { "sloppy1.0": { "compile": ["../abs.ts"], "runtime": [] } }
        }))
        .is_err());
    }

    #[test]
    fn pack_creates_deterministic_slpkg_and_rejects_duplicate_normalized_paths() {
        let _guard = ENV_LOCK
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        let temp = temp_dir("pack");
        let original = env::current_dir().expect("cwd");
        fs::create_dir_all(temp.join("lib")).expect("lib dir");
        fs::write(temp.join("lib/index.ts"), "export const value = 1;\n").expect("source");
        fs::write(
            temp.join("sloppy.json"),
            r#"{
  "id": "Sloppy.Example",
  "version": "0.1.0",
  "targets": {
    "sloppy1.0": {
      "compile": ["lib/index.ts"],
      "runtime": []
    }
  }
}
"#,
        )
        .expect("manifest");
        env::set_current_dir(&temp).expect("set cwd");
        let first = pack_current_project().expect("first pack");
        let first_bytes = fs::read(&first).expect("first archive");
        let second = pack_current_project().expect("second pack");
        let second_bytes = fs::read(second).expect("second archive");
        assert_eq!(first_bytes, second_bytes);
        let mut archive = ZipArchive::new(Cursor::new(first_bytes)).expect("zip");
        assert!(archive.by_name(MANIFEST_NAME).is_ok());
        assert!(archive.by_name("lib/index.ts").is_ok());

        fs::write(
            temp.join("sloppy.json"),
            r#"{
  "id": "Sloppy.Example",
  "version": "0.1.0",
  "targets": {
    "sloppy1.0": {
      "compile": ["lib/index.ts"],
      "runtime": ["LIB/INDEX.TS"]
    }
  }
}
"#,
        )
        .expect("manifest");
        let error = pack_current_project().expect_err("duplicate normalized path");
        assert_eq!(error.code, "SLOPPY_E_PACKAGE_DUPLICATE_PATH");
        env::set_current_dir(original).expect("restore cwd");
        let _ = fs::remove_dir_all(temp);
    }

    #[test]
    fn restore_generates_lock_assets_reuses_cache_and_locked_detects_drift() {
        let _guard = ENV_LOCK
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        let temp = temp_dir("restore");
        let cache = temp.join("cache");
        let original = env::current_dir().expect("cwd");
        let original_cache = env::var_os("SLOPPY_PACKAGE_CACHE");
        let packages = temp.join("packages");
        let app = temp.join("app");
        fs::create_dir_all(&packages).expect("packages");
        create_package_source(&temp.join("core"), "Sloppy.Core", "0.1.0", None, None);
        env::set_current_dir(temp.join("core")).expect("core cwd");
        let core_package = pack_current_project().expect("pack core");
        fs::copy(core_package, packages.join("sloppy.core.0.1.0.slpkg")).expect("copy core");

        create_package_source(
            &temp.join("example"),
            "Sloppy.Example",
            "0.1.0",
            Some(("Sloppy.Core", "[0.1.0]")),
            Some(("example", "native/win-x64/example.dll")),
        );
        env::set_current_dir(temp.join("example")).expect("example cwd");
        let example_package = pack_current_project().expect("pack example");
        fs::copy(example_package, packages.join("sloppy.example.0.1.0.slpkg"))
            .expect("copy example");

        fs::create_dir_all(&app).expect("app");
        fs::write(
            app.join("sloppy.json"),
            format!(
                r#"{{
  "name": "app",
  "entry": "src/main.ts",
  "target": "sloppy1.0",
  "runtimeIdentifier": "win-x64",
  "packageSources": ["{}"],
  "dependencies": {{
    "Sloppy.Example": "[0.1.0,2.0.0)"
  }}
}}
"#,
                packages.to_string_lossy().replace('\\', "/")
            ),
        )
        .expect("app manifest");
        env::set_current_dir(&app).expect("app cwd");
        env::set_var("SLOPPY_PACKAGE_CACHE", &cache);
        restore_current_project(false).expect("restore");
        let lockfile = read_json_file(&app.join(LOCKFILE_NAME)).expect("lockfile");
        let assets = read_json_file(&app.join(ASSETS_PATH)).expect("assets");
        assert!(
            lockfile["packages"]["sloppy.example/0.1.0"]["assets"]["native"]["example"]
                == "native/win-x64/example.dll"
        );
        assert!(assets["packages"]
            .as_array()
            .expect("packages")
            .iter()
            .any(|package| package["id"] == "Sloppy.Core"));
        restore_current_project(true).expect("locked restore");
        fs::write(
            app.join("sloppy.json"),
            format!(
                r#"{{
  "name": "app",
  "entry": "src/main.ts",
  "target": "sloppy1.0",
  "runtimeIdentifier": "win-x64",
  "packageSources": ["{}"],
  "dependencies": {{
    "Sloppy.Example": "[0.2.0,2.0.0)"
  }}
}}
"#,
                packages.to_string_lossy().replace('\\', "/")
            ),
        )
        .expect("app manifest drift");
        let error = restore_current_project(true).expect_err("locked drift");
        assert!(matches!(
            error.code,
            "SLOPPY_E_PACKAGE_LOCK_OUT_OF_DATE"
                | "SLOPPY_E_PACKAGE_CONFLICT"
                | "SLOPPY_E_PACKAGE_NOT_FOUND"
        ));
        fs::write(
            app.join("sloppy.json"),
            format!(
                r#"{{
  "name": "app",
  "entry": "src/main.ts",
  "target": "sloppy1.0",
  "runtimeIdentifier": "win-x64",
  "packageSources": ["{}"],
  "dependencies": {{
    "Sloppy.Example": "[0.1.0]",
    "Sloppy.Core": "[0.2.0]"
  }}
}}
"#,
                packages.to_string_lossy().replace('\\', "/")
            ),
        )
        .expect("app manifest conflict");
        let error = restore_current_project(false).expect_err("dependency conflict");
        assert_eq!(error.code, "SLOPPY_E_PACKAGE_CONFLICT");

        env::set_current_dir(original).expect("restore cwd");
        restore_cache_env(original_cache);
        let _ = fs::remove_dir_all(temp);
    }

    #[test]
    fn restore_fails_when_native_rid_asset_is_missing() {
        let _guard = ENV_LOCK
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        let temp = temp_dir("native-missing");
        let cache = temp.join("cache");
        let original = env::current_dir().expect("cwd");
        let original_cache = env::var_os("SLOPPY_PACKAGE_CACHE");
        let packages = temp.join("packages");
        let app = temp.join("app");
        fs::create_dir_all(&packages).expect("packages");
        create_package_source(
            &temp.join("example"),
            "Sloppy.Example",
            "0.1.0",
            None,
            Some(("example", "native/win-x64/example.dll")),
        );
        env::set_current_dir(temp.join("example")).expect("example cwd");
        let package = pack_current_project().expect("pack");
        fs::copy(package, packages.join("sloppy.example.0.1.0.slpkg")).expect("copy");
        fs::create_dir_all(&app).expect("app");
        fs::write(
            app.join("sloppy.json"),
            format!(
                r#"{{
  "target": "sloppy1.0",
  "runtimeIdentifier": "linux-x64",
  "packageSources": ["{}"],
  "dependencies": {{ "Sloppy.Example": "[0.1.0]" }}
}}
"#,
                packages.to_string_lossy().replace('\\', "/")
            ),
        )
        .expect("app manifest");
        env::set_current_dir(&app).expect("app cwd");
        env::set_var("SLOPPY_PACKAGE_CACHE", &cache);
        let error = restore_current_project(false).expect_err("missing rid");
        assert_eq!(error.code, "SLOPPY_E_PACKAGE_NATIVE_ASSET_MISSING");
        env::set_current_dir(original).expect("restore cwd");
        restore_cache_env(original_cache);
        let _ = fs::remove_dir_all(temp);
    }

    fn create_package_source(
        root: &Path,
        id: &str,
        version: &str,
        dependency: Option<(&str, &str)>,
        native: Option<(&str, &str)>,
    ) {
        fs::create_dir_all(root.join("lib")).expect("lib");
        fs::write(root.join("lib/index.ts"), "export const value = 1;\n").expect("lib file");
        let native_json = if let Some((name, path)) = native {
            let full = root.join(path);
            fs::create_dir_all(full.parent().expect("native parent")).expect("native dir");
            fs::write(&full, "native bytes").expect("native file");
            format!(
                r#",
  "native": {{
    "libraries": {{
      "{name}": {{
        "win-x64": "{path}"
      }}
    }}
  }},
  "capabilities": ["ffi/native"]"#
            )
        } else {
            String::new()
        };
        let dependency_json = if let Some((dependency_id, range)) = dependency {
            format!(r#""dependencies": {{ "{dependency_id}": "{range}" }},"#)
        } else {
            String::new()
        };
        fs::write(
            root.join("sloppy.json"),
            format!(
                r#"{{
  "id": "{id}",
  "version": "{version}",
  {dependency_json}
  "targets": {{
    "sloppy1.0": {{
      "compile": ["lib/index.ts"],
      "runtime": []
    }}
  }}{native_json}
}}
"#
            ),
        )
        .expect("manifest");
    }

    fn restore_cache_env(original: Option<std::ffi::OsString>) {
        if let Some(value) = original {
            env::set_var("SLOPPY_PACKAGE_CACHE", value);
        } else {
            env::remove_var("SLOPPY_PACKAGE_CACHE");
        }
    }

    fn temp_dir(label: &str) -> PathBuf {
        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("time")
            .as_nanos();
        env::temp_dir().join(format!("sloppy-package-manager-{label}-{nonce}"))
    }
}

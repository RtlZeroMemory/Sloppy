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
const MAX_ARCHIVE_BYTES: u64 = 256 * 1024 * 1024;
const MAX_MANIFEST_BYTES: u64 = 1024 * 1024;
const MAX_ARCHIVE_ENTRIES: usize = 4096;
const MAX_ENTRY_BYTES: u64 = 128 * 1024 * 1024;
const MAX_EXPANDED_BYTES: u64 = 512 * 1024 * 1024;

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

#[derive(Debug, Clone, Eq, PartialEq)]
struct Version {
    major: u64,
    minor: u64,
    patch: u64,
    prerelease: Option<String>,
}

impl Version {
    fn parse(text: &str) -> Result<Self> {
        let (core, prerelease) = match text.split_once('-') {
            Some((core, prerelease)) => {
                if prerelease.is_empty()
                    || !prerelease
                        .bytes()
                        .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'.' | b'-'))
                {
                    return Err(PackageError::new(
                        "SLOPPY_E_PACKAGE_VERSION_INVALID",
                        format!("package version '{text}' has an invalid prerelease suffix"),
                    ));
                }
                (core, Some(prerelease.to_ascii_lowercase()))
            }
            None => (text, None),
        };
        let parts = core.split('.').collect::<Vec<_>>();
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
            prerelease,
        })
    }
}

impl Ord for Version {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        self.major
            .cmp(&other.major)
            .then_with(|| self.minor.cmp(&other.minor))
            .then_with(|| self.patch.cmp(&other.patch))
            .then_with(|| match (&self.prerelease, &other.prerelease) {
                (None, None) => std::cmp::Ordering::Equal,
                (None, Some(_)) => std::cmp::Ordering::Greater,
                (Some(_), None) => std::cmp::Ordering::Less,
                (Some(left), Some(right)) => left.cmp(right),
            })
    }
}

impl PartialOrd for Version {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

impl std::fmt::Display for Version {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(formatter, "{}.{}.{}", self.major, self.minor, self.patch)?;
        if let Some(prerelease) = &self.prerelease {
            write!(formatter, "-{prerelease}")?;
        }
        Ok(())
    }
}

#[derive(Debug, Clone, Eq, PartialEq)]
pub struct VersionRange {
    lower: Option<(Version, bool)>,
    upper: Option<(Version, bool)>,
    exact: bool,
}

impl VersionRange {
    pub fn parse(text: &str) -> Result<Self> {
        if !text.starts_with(['[', '(']) || !(text.ends_with(']') || text.ends_with(')')) {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_RANGE_INVALID",
                format!("unsupported package version range '{text}'"),
            ));
        }
        let lower_inclusive = text.starts_with('[');
        let upper_inclusive = text.ends_with(']');
        let inner = &text[1..text.len() - 1];
        if !inner.contains(',') {
            if !lower_inclusive || !upper_inclusive || inner.is_empty() {
                return Err(PackageError::new(
                    "SLOPPY_E_PACKAGE_RANGE_INVALID",
                    format!("unsupported package version range '{text}'"),
                ));
            }
            return Ok(Self {
                lower: Some((Version::parse(inner)?, true)),
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
        Ok(Self {
            lower: if lower.is_empty() {
                None
            } else {
                Some((Version::parse(lower)?, lower_inclusive))
            },
            upper: if upper.is_empty() {
                None
            } else {
                Some((Version::parse(upper)?, upper_inclusive))
            },
            exact: false,
        })
    }

    fn allows(&self, version: &Version) -> bool {
        if self.exact {
            return self
                .lower
                .as_ref()
                .is_some_and(|(lower, _)| version == lower);
        }
        let lower_allowed = self.lower.as_ref().is_none_or(|(lower, inclusive)| {
            if *inclusive {
                version >= lower
            } else {
                version > lower
            }
        });
        let upper_allowed = self.upper.as_ref().is_none_or(|(upper, inclusive)| {
            if *inclusive {
                version <= upper
            } else {
                version < upper
            }
        });
        lower_allowed && upper_allowed
    }
}

#[derive(Debug, Clone, Eq, PartialEq)]
enum SourceKind {
    Folder,
    Sloppy,
    Npm,
}

#[derive(Debug, Clone)]
struct PackageSource {
    name: String,
    kind: SourceKind,
    display: String,
    path: Option<PathBuf>,
    url: Option<String>,
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

pub fn run_package_command(command: &str, args: &[String]) -> Result<String> {
    match command {
        "pack" => {
            if !args.is_empty() {
                return Err(PackageError::new(
                    "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
                    "sloppy pack does not accept arguments",
                ));
            }
            let path = pack_current_project()?;
            Ok(format!("Created package: {}\n", path.display()))
        }
        "restore" => {
            let mut locked = false;
            let mut json_output = false;
            for arg in args {
                match arg.as_str() {
                    "--locked" => locked = true,
                    "--json" => json_output = true,
                    other => {
                        return Err(PackageError::new(
                            "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
                            format!("unsupported restore option '{other}'"),
                        ))
                    }
                }
            }
            let summary = restore_current_project_with_summary(locked)?;
            if json_output {
                Ok(format!(
                    "{}\n",
                    serde_json::to_string_pretty(&summary).map_err(|error| {
                        PackageError::new("SLOPPY_E_PACKAGE_MANIFEST_INVALID", error.to_string())
                    })?
                ))
            } else {
                Ok(format!(
                    "Restore completed: {} package(s), target {}, RID {}.\n",
                    summary["packageCount"].as_u64().unwrap_or(0),
                    summary["target"].as_str().unwrap_or(SUPPORTED_TARGET),
                    summary["runtimeIdentifier"].as_str().unwrap_or("")
                ))
            }
        }
        "add" => command_add(args),
        "remove" => command_remove(args),
        "update" => command_update(args),
        "list" => command_list(args),
        "why" => command_why(args),
        "cache" => command_cache(args),
        "source" => command_source(args),
        "publish" => command_publish(args),
        "feed" => command_feed(args),
        "npm" => command_npm(args),
        other => Err(PackageError::new(
            "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
            format!("unsupported package manager command '{other}'"),
        )),
    }
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
        let full_path = cwd.join(&path);
        let metadata = fs::symlink_metadata(&full_path)
            .map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
        if !metadata.file_type().is_file() || metadata.file_type().is_symlink() {
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
    restore_current_project_with_summary(locked).map(|_| ())
}

fn command_add(args: &[String]) -> Result<String> {
    let cwd = env::current_dir().map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
    let mut package = None;
    let mut version = None;
    let mut source = None;
    let mut native_ok = false;
    let mut index = 0;
    while index < args.len() {
        match args[index].as_str() {
            "--version" => {
                index += 1;
                version = args.get(index).cloned();
            }
            "--source" => {
                index += 1;
                source = args.get(index).cloned();
            }
            "--native-ok" => native_ok = true,
            value if package.is_none() => package = Some(value.to_string()),
            other => {
                return Err(PackageError::new(
                    "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
                    format!("unsupported add argument '{other}'"),
                ))
            }
        }
        index += 1;
    }
    let Some(package) = package else {
        return Err(PackageError::new(
            "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
            "usage: sloppy add <package-id|path.slpkg> [--version <range>] [--source <path>] [--native-ok]",
        ));
    };
    let mut project = read_or_new_project(&cwd)?;
    let (id, range, source_path, has_native) = if package.ends_with(".slpkg") {
        let path = PathBuf::from(&package);
        let artifact = if path.is_absolute() {
            path
        } else {
            cwd.join(path)
        };
        reject_oversized_file(&artifact, MAX_ARCHIVE_BYTES)?;
        let bytes = fs::read(&artifact).map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
        let manifest = read_manifest_from_archive(Cursor::new(bytes))?;
        (
            manifest.id,
            format!("[{}]", manifest.version),
            artifact.parent().map(Path::to_path_buf),
            !manifest.native_libraries.is_empty(),
        )
    } else {
        validate_package_id(&package)?;
        (
            package,
            version.unwrap_or_else(|| "[0.0.0,)".to_string()),
            source.map(PathBuf::from),
            false,
        )
    };
    VersionRange::parse(range.strip_prefix("npm:").unwrap_or(&range))?;
    let dependencies = ensure_object_field(&mut project, "dependencies")?;
    dependencies.insert(id.clone(), Value::String(range.clone()));
    if let Some(source_path) = source_path {
        add_package_source_value(&mut project, &cwd, &source_path)?;
    }
    if has_native && native_ok {
        add_string_array_value(&mut project, "trustedNativePackages", &id)?;
    } else if has_native {
        return Err(PackageError::new(
            "SLOPPY_E_PACKAGE_NATIVE_ASSET_MISSING",
            format!(
                "package '{id}' declares native assets; rerun with --native-ok to record explicit trust"
            ),
        ));
    }
    write_json_file(&cwd.join("sloppy.json"), &project)?;
    Ok(format!("Added package dependency {id} {range}.\n"))
}

fn command_remove(args: &[String]) -> Result<String> {
    let Some(id) = args.first() else {
        return Err(PackageError::new(
            "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
            "usage: sloppy remove <package-id>",
        ));
    };
    let cwd = env::current_dir().map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
    let mut project = read_json_file(&cwd.join("sloppy.json"))?;
    let normalized = normalize_id(id);
    let removed = project
        .get_mut("dependencies")
        .and_then(Value::as_object_mut)
        .and_then(|deps| deps.remove(&normalized).or_else(|| deps.remove(id)))
        .is_some();
    remove_string_array_value(&mut project, "trustedNativePackages", id);
    write_json_file(&cwd.join("sloppy.json"), &project)?;
    if removed {
        Ok(format!("Removed package dependency {id}.\n"))
    } else {
        Ok(format!("Package dependency {id} was not present.\n"))
    }
}

fn command_update(args: &[String]) -> Result<String> {
    if args.len() > 1 {
        return Err(PackageError::new(
            "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
            "usage: sloppy update [package-id]",
        ));
    }
    if let Some(id) = args.first() {
        let project = read_json_file(&env::current_dir().unwrap_or_default().join("sloppy.json"))?;
        let deps = dependency_map(&project)?;
        if !deps.contains_key(&normalize_id(id)) {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_NOT_FOUND",
                format!("project does not depend on '{id}'"),
            ));
        }
    }
    let summary = restore_current_project_with_summary(false)?;
    Ok(format!(
        "Updated restore graph: {} package(s).\n",
        summary["packageCount"].as_u64().unwrap_or(0)
    ))
}

fn command_list(args: &[String]) -> Result<String> {
    let subject = args.first().map(String::as_str).unwrap_or("packages");
    let json_output = args.iter().any(|arg| arg == "--json");
    let cwd = env::current_dir().map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
    let assets = read_optional_json(&cwd.join(ASSETS_PATH))?;
    let lock = read_optional_json(&cwd.join(LOCKFILE_NAME))?;
    let value = match subject {
        "packages" => assets
            .as_ref()
            .and_then(|assets| assets.get("packages"))
            .cloned()
            .unwrap_or_else(|| json!([])),
        "native" => list_native_value(&assets.unwrap_or_else(|| json!({}))),
        "capabilities" => list_capabilities_value(&assets.unwrap_or_else(|| json!({}))),
        "outdated" => list_outdated_value(&cwd, &lock.unwrap_or_else(|| json!({})))?,
        other => {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
                format!("unsupported list subject '{other}'"),
            ))
        }
    };
    if json_output {
        return Ok(format!(
            "{}\n",
            serde_json::to_string_pretty(&value).unwrap_or_default()
        ));
    }
    Ok(render_list(subject, &value))
}

fn command_why(args: &[String]) -> Result<String> {
    let Some(id) = args.first() else {
        return Err(PackageError::new(
            "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
            "usage: sloppy why <package-id>",
        ));
    };
    let cwd = env::current_dir().map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
    let project = read_json_file(&cwd.join("sloppy.json"))?;
    let lock = read_json_file(&cwd.join(LOCKFILE_NAME))?;
    let normalized = normalize_id(id);
    let mut lines = Vec::new();
    if dependency_map(&project)?.contains_key(&normalized) {
        lines.push(format!("root -> {id}"));
    }
    if let Some(packages) = lock.get("packages").and_then(Value::as_object) {
        for package in packages.values() {
            let Some(display) = package.get("id").and_then(Value::as_str) else {
                continue;
            };
            if let Some(deps) = package.get("dependencies").and_then(Value::as_object) {
                if deps.contains_key(&normalized) {
                    lines.push(format!("{} -> {}", display, id));
                }
            }
        }
    }
    if lines.is_empty() {
        return Err(PackageError::new(
            "SLOPPY_E_PACKAGE_NOT_FOUND",
            format!("package '{id}' is not present in the restore graph"),
        ));
    }
    Ok(format!("{}\n", lines.join("\n")))
}

fn command_cache(args: &[String]) -> Result<String> {
    let action = args.first().map(String::as_str).unwrap_or("list");
    let root = cache_root()?;
    match action {
        "list" => Ok(render_cache_list(&root)?),
        "clean" => {
            if args.get(1).map(String::as_str) == Some("--all") {
                if root.exists() {
                    fs::remove_dir_all(&root)
                        .map_err(io_error("SLOPPY_E_PACKAGE_CACHE_CORRUPT"))?;
                }
                return Ok("Cleaned the Sloppy package cache.\n".to_string());
            }
            let Some(id) = args.get(1) else {
                return Err(PackageError::new(
                    "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
                    "usage: sloppy cache clean [--all | <package-id>]",
                ));
            };
            validate_package_id(id)?;
            let target = root.join(normalize_id(id));
            if target.exists() {
                fs::remove_dir_all(&target).map_err(io_error("SLOPPY_E_PACKAGE_CACHE_CORRUPT"))?;
            }
            Ok(format!("Cleaned cached package {id}.\n"))
        }
        other => Err(PackageError::new(
            "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
            format!("unsupported cache command '{other}'"),
        )),
    }
}

fn command_source(args: &[String]) -> Result<String> {
    let action = args.first().map(String::as_str).unwrap_or("list");
    let cwd = env::current_dir().map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
    let mut project = read_or_new_project(&cwd)?;
    match action {
        "list" => {
            let sources = project
                .get("packageSources")
                .and_then(Value::as_array)
                .cloned()
                .unwrap_or_default();
            Ok(format!(
                "{}\n",
                serde_json::to_string_pretty(&sources).unwrap_or_default()
            ))
        }
        "add" => {
            if args.len() < 3 {
                return Err(PackageError::new(
                    "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
                    "usage: sloppy source add <name> <url-or-path> [--type folder|sloppy|npm]",
                ));
            }
            let name = &args[1];
            let location = &args[2];
            let mut kind = if location.starts_with("http://") || location.starts_with("https://") {
                "sloppy"
            } else {
                "folder"
            };
            let mut index = 3;
            while index < args.len() {
                if args[index] == "--type" {
                    index += 1;
                    kind = args.get(index).map(String::as_str).unwrap_or(kind);
                }
                index += 1;
            }
            let source = if kind == "npm" || location.starts_with("http") {
                json!({"name": name, "type": kind, "url": location})
            } else {
                validate_source_path(location)?;
                json!({"name": name, "type": kind, "path": location})
            };
            push_package_source(&mut project, source)?;
            write_json_file(&cwd.join("sloppy.json"), &project)?;
            Ok(format!("Added package source {name}.\n"))
        }
        "remove" => {
            let Some(name) = args.get(1) else {
                return Err(PackageError::new(
                    "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
                    "usage: sloppy source remove <name>",
                ));
            };
            remove_package_source(&mut project, name);
            write_json_file(&cwd.join("sloppy.json"), &project)?;
            Ok(format!("Removed package source {name}.\n"))
        }
        other => Err(PackageError::new(
            "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
            format!("unsupported source command '{other}'"),
        )),
    }
}

fn command_publish(args: &[String]) -> Result<String> {
    if args.len() < 3 || args.get(1).map(String::as_str) != Some("--source") {
        return Err(PackageError::new(
            "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
            "usage: sloppy publish <path.slpkg> --source <folder-source>",
        ));
    }
    let artifact = PathBuf::from(&args[0]);
    let source = PathBuf::from(&args[2]);
    fs::create_dir_all(&source).map_err(io_error("SLOPPY_E_PACKAGE_SOURCE_MISSING"))?;
    let file_name = artifact.file_name().ok_or_else(|| {
        PackageError::new(
            "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
            "package artifact path must include a file name",
        )
    })?;
    fs::copy(&artifact, source.join(file_name))
        .map_err(io_error("SLOPPY_E_PACKAGE_SOURCE_MISSING"))?;
    generate_feed_index(&source)?;
    Ok(format!(
        "Published {} to {}.\n",
        artifact.display(),
        source.display()
    ))
}

fn command_feed(args: &[String]) -> Result<String> {
    if args.first().map(String::as_str) != Some("index") || args.len() != 2 {
        return Err(PackageError::new(
            "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
            "usage: sloppy feed index <folder>",
        ));
    }
    let folder = PathBuf::from(&args[1]);
    generate_feed_index(&folder)?;
    Ok(format!("Indexed Sloppy feed at {}.\n", folder.display()))
}

fn command_npm(args: &[String]) -> Result<String> {
    if args.first().map(String::as_str) != Some("add") {
        return Err(PackageError::new(
            "SLOPPY_E_PACKAGE_SOURCE_UNSUPPORTED",
            "only 'sloppy npm add <package> [--version <range>]' is scaffolded in this build",
        ));
    }
    let Some(name) = args.get(1) else {
        return Err(PackageError::new(
            "SLOPPY_E_NPM_PACKAGE_NOT_FOUND",
            "usage: sloppy npm add <package> [--version <range>]",
        ));
    };
    let mut range = "[0.0.0,)".to_string();
    let mut index = 2;
    while index < args.len() {
        if args[index] == "--version" {
            index += 1;
            range = args.get(index).cloned().unwrap_or(range);
        }
        index += 1;
    }
    let cwd = env::current_dir().map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
    let mut project = read_or_new_project(&cwd)?;
    ensure_object_field(&mut project, "dependencies")?
        .insert(name.clone(), Value::String(format!("npm:{range}")));
    push_package_source(
        &mut project,
        json!({"name": "npmjs", "type": "npm", "url": "https://registry.npmjs.org"}),
    )?;
    write_json_file(&cwd.join("sloppy.json"), &project)?;
    Ok(format!(
        "Added npm foreign-source dependency {name} npm:{range}. Restore support is not implemented yet.\n"
    ))
}

fn restore_current_project_with_summary(locked: bool) -> Result<Value> {
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
    reject_npm_dependencies(&dependencies)?;
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
    let mut planned = Vec::new();
    for package in resolved {
        planned.push(ResolvedPackage {
            cache_path: cache_package_path(&cache_root, &package.candidate),
            ..package
        });
    }
    let lockfile = lockfile_json(&target, &runtime_identifier, &sources, &planned);
    if locked {
        let Some(previous) = previous_lock else {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_LOCK_OUT_OF_DATE",
                "sloppy.lock.json is required for --locked restore",
            ));
        };
        verify_locked_hashes(&previous, &planned)?;
        if previous != lockfile {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_LOCK_OUT_OF_DATE",
                "restore would change sloppy.lock.json",
            ));
        }
    } else {
        write_json_file(&cwd.join(LOCKFILE_NAME), &lockfile)?;
    }
    let mut restored = Vec::new();
    for package in planned {
        let cache_path = restore_to_cache(&package.candidate, &cache_root)?;
        restored.push(ResolvedPackage {
            cache_path,
            ..package
        });
    }
    let assets = assets_json(&target, &runtime_identifier, &restored);
    write_json_file(&cwd.join(ASSETS_PATH), &assets)?;
    Ok(json!({
        "target": target,
        "runtimeIdentifier": runtime_identifier,
        "packageCount": restored.len(),
        "lockfile": LOCKFILE_NAME,
        "assets": ASSETS_PATH,
    }))
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

fn read_or_new_project(cwd: &Path) -> Result<Value> {
    let path = cwd.join("sloppy.json");
    if path.exists() {
        read_json_file(&path)
    } else {
        Ok(json!({}))
    }
}

fn ensure_object_field<'a>(value: &'a mut Value, key: &str) -> Result<&'a mut Map<String, Value>> {
    if !value.is_object() {
        *value = json!({});
    }
    let object = value.as_object_mut().ok_or_else(|| {
        PackageError::new(
            "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
            "project manifest root must be an object",
        )
    })?;
    if !object.get(key).is_some_and(Value::is_object) {
        object.insert(key.to_string(), json!({}));
    }
    object
        .get_mut(key)
        .and_then(Value::as_object_mut)
        .ok_or_else(|| {
            PackageError::new(
                "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
                format!("{key} must be an object"),
            )
        })
}

fn add_string_array_value(value: &mut Value, key: &str, text: &str) -> Result<()> {
    if !value.is_object() {
        *value = json!({});
    }
    let object = value.as_object_mut().ok_or_else(|| {
        PackageError::new(
            "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
            "project manifest root must be an object",
        )
    })?;
    if !object.get(key).is_some_and(Value::is_array) {
        object.insert(key.to_string(), json!([]));
    }
    let array = object
        .get_mut(key)
        .and_then(Value::as_array_mut)
        .ok_or_else(|| {
            PackageError::new(
                "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
                format!("{key} must be an array"),
            )
        })?;
    if !array.iter().any(|item| item.as_str() == Some(text)) {
        array.push(Value::String(text.to_string()));
    }
    array.sort_by(|left, right| left.as_str().cmp(&right.as_str()));
    Ok(())
}

fn remove_string_array_value(value: &mut Value, key: &str, text: &str) {
    if let Some(array) = value.get_mut(key).and_then(Value::as_array_mut) {
        let normalized = normalize_id(text);
        array.retain(|item| item.as_str().map(normalize_id) != Some(normalized.clone()));
    }
}

fn add_package_source_value(project: &mut Value, cwd: &Path, path: &Path) -> Result<()> {
    let display = if path.is_absolute() {
        path.strip_prefix(cwd)
            .ok()
            .map(|relative| relative.to_string_lossy().replace('\\', "/"))
            .unwrap_or_else(|| path.to_string_lossy().replace('\\', "/"))
    } else {
        path.to_string_lossy().replace('\\', "/")
    };
    validate_source_path(&display)?;
    push_package_source(project, Value::String(display))
}

fn push_package_source(project: &mut Value, source: Value) -> Result<()> {
    if !project.is_object() {
        *project = json!({});
    }
    let object = project.as_object_mut().ok_or_else(|| {
        PackageError::new(
            "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
            "project manifest root must be an object",
        )
    })?;
    if !object.get("packageSources").is_some_and(Value::is_array) {
        object.insert("packageSources".to_string(), json!([]));
    }
    let array = object
        .get_mut("packageSources")
        .and_then(Value::as_array_mut)
        .ok_or_else(|| {
            PackageError::new(
                "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
                "packageSources must be an array",
            )
        })?;
    if !array.contains(&source) {
        array.push(source);
    }
    array.sort_by(|left, right| {
        canonical_source_sort_key(left).cmp(&canonical_source_sort_key(right))
    });
    Ok(())
}

fn remove_package_source(project: &mut Value, name: &str) {
    if let Some(array) = project
        .get_mut("packageSources")
        .and_then(Value::as_array_mut)
    {
        array.retain(|source| {
            if source.as_str() == Some(name) {
                return false;
            }
            source
                .get("name")
                .and_then(Value::as_str)
                .is_none_or(|source_name| source_name != name)
        });
    }
}

fn canonical_source_sort_key(value: &Value) -> String {
    value
        .as_str()
        .map(str::to_string)
        .or_else(|| {
            value
                .get("name")
                .and_then(Value::as_str)
                .map(str::to_string)
        })
        .unwrap_or_default()
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
        if !range.starts_with("npm:") {
            VersionRange::parse(range)?;
        }
        dependencies.insert(normalize_id(id), range.to_string());
    }
    Ok(dependencies)
}

fn reject_npm_dependencies(dependencies: &BTreeMap<String, String>) -> Result<()> {
    if let Some((id, _)) = dependencies
        .iter()
        .find(|(_, range)| range.starts_with("npm:"))
    {
        return Err(PackageError::new(
            "SLOPPY_E_PACKAGE_SOURCE_UNSUPPORTED",
            format!(
                "npm foreign-source restore is scaffolded but not implemented for dependency '{id}'"
            ),
        ));
    }
    Ok(())
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

fn package_sources(cwd: &Path, project: &Value) -> Result<Vec<PackageSource>> {
    let Some(sources) = project.get("packageSources").and_then(Value::as_array) else {
        return Err(PackageError::new(
            "SLOPPY_E_PACKAGE_SOURCE_MISSING",
            "sloppy.json must declare packageSources for local restore",
        ));
    };
    let mut output = Vec::new();
    for source in sources {
        let parsed = parse_package_source(cwd, source)?;
        if parsed.kind == SourceKind::Npm {
            output.push(parsed);
            continue;
        }
        if parsed.kind == SourceKind::Sloppy && parsed.url.is_some() {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_REMOTE_UNAVAILABLE",
                format!(
                    "remote Sloppy source '{}' is declared but HTTP restore is not implemented in this build",
                    parsed.name
                ),
            ));
        }
        let Some(path) = &parsed.path else {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_SOURCE_MISSING",
                format!(
                    "package source '{}' does not declare a local path",
                    parsed.name
                ),
            ));
        };
        if !path.is_dir() {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_SOURCE_MISSING",
                format!("package source does not exist: {}", parsed.display),
            ));
        }
        output.push(parsed);
    }
    output.sort_by(|left, right| left.display.cmp(&right.display));
    Ok(output)
}

fn validate_source_path(path: &str) -> Result<()> {
    if path.is_empty()
        || path.contains('\0')
        || Path::new(path).is_absolute()
        || Path::new(path)
            .components()
            .any(|component| matches!(component, Component::ParentDir | Component::Prefix(_)))
    {
        return Err(PackageError::new(
            "SLOPPY_E_PACKAGE_PATH_TRAVERSAL",
            format!("package source must be a safe local path: {path}"),
        ));
    }
    Ok(())
}

fn parse_package_source(cwd: &Path, source: &Value) -> Result<PackageSource> {
    if let Some(source_text) = source.as_str() {
        validate_source_path(source_text)?;
        return Ok(PackageSource {
            name: source_text.replace('\\', "/"),
            kind: SourceKind::Folder,
            display: source_text.replace('\\', "/"),
            path: Some(cwd.join(source_text)),
            url: None,
        });
    }
    let Some(object) = source.as_object() else {
        return Err(PackageError::new(
            "SLOPPY_E_PACKAGE_SOURCE_MISSING",
            "packageSources entries must be strings or source objects",
        ));
    };
    let name = object
        .get("name")
        .and_then(Value::as_str)
        .unwrap_or("unnamed")
        .to_string();
    let kind_text = object
        .get("type")
        .and_then(Value::as_str)
        .unwrap_or("folder");
    let kind = match kind_text {
        "folder" => SourceKind::Folder,
        "sloppy" => SourceKind::Sloppy,
        "npm" => SourceKind::Npm,
        other => {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_SOURCE_UNSUPPORTED",
                format!("unsupported package source type '{other}'"),
            ))
        }
    };
    if kind == SourceKind::Npm {
        let url = object
            .get("url")
            .and_then(Value::as_str)
            .unwrap_or("https://registry.npmjs.org")
            .to_string();
        return Ok(PackageSource {
            name,
            kind,
            display: url.clone(),
            path: None,
            url: Some(url),
        });
    }
    if let Some(path_text) = object.get("path").and_then(Value::as_str) {
        validate_source_path(path_text)?;
        return Ok(PackageSource {
            name,
            kind,
            display: path_text.replace('\\', "/"),
            path: Some(cwd.join(path_text)),
            url: None,
        });
    }
    if let Some(url) = object.get("url").and_then(Value::as_str) {
        return Ok(PackageSource {
            name,
            kind,
            display: url.to_string(),
            path: None,
            url: Some(url.to_string()),
        });
    }
    Err(PackageError::new(
        "SLOPPY_E_PACKAGE_SOURCE_MISSING",
        format!("package source '{name}' must declare path or url"),
    ))
}

fn discover_candidates(sources: &[PackageSource]) -> Result<Vec<PackageCandidate>> {
    let mut candidates = Vec::new();
    for source in sources {
        if source.kind == SourceKind::Npm {
            continue;
        }
        let Some(source_path) = &source.path else {
            continue;
        };
        let mut artifacts = fs::read_dir(source_path)
            .map_err(io_error("SLOPPY_E_PACKAGE_SOURCE_MISSING"))?
            .filter_map(|entry| entry.ok().map(|entry| entry.path()))
            .filter(|path| path.extension().and_then(|ext| ext.to_str()) == Some("slpkg"))
            .collect::<Vec<_>>();
        artifacts.extend(discover_static_feed_artifacts(source_path)?);
        artifacts.sort();
        artifacts.dedup();
        for artifact in artifacts {
            reject_oversized_file(&artifact, MAX_ARCHIVE_BYTES)?;
            let bytes =
                fs::read(&artifact).map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
            let sha256 = sha256_bytes(&bytes);
            let manifest = read_manifest_from_archive(Cursor::new(bytes))?;
            candidates.push(PackageCandidate {
                manifest,
                source_display: source.display.clone(),
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
    if archive.len() > MAX_ARCHIVE_ENTRIES {
        return Err(PackageError::new(
            "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
            format!("package archive contains more than {MAX_ARCHIVE_ENTRIES} entries"),
        ));
    }
    let mut seen = BTreeSet::new();
    let mut normalized_seen = BTreeSet::new();
    let mut manifest_text = String::new();
    for index in 0..archive.len() {
        let mut file = archive
            .by_index(index)
            .map_err(package_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
        reject_unsafe_zip_entry(file.unix_mode())?;
        let name = file.name().to_string();
        validate_relative_archive_path(&name)?;
        reject_oversized_entry(&name, file.size())?;
        if name.ends_with('/') {
            continue;
        }
        if !seen.insert(name.clone()) {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_DUPLICATE_PATH",
                format!("duplicate archive path '{name}'"),
            ));
        }
        if !normalized_seen.insert(normalize_archive_path(&name)) {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_DUPLICATE_PATH",
                format!("duplicate normalized archive path '{name}'"),
            ));
        }
        if name == MANIFEST_NAME {
            if file.size() > MAX_MANIFEST_BYTES {
                return Err(PackageError::new(
                    "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
                    "package manifest.json is too large",
                ));
            }
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
    let manifest = parse_package_manifest(manifest_json)?;
    verify_manifest_asset_hashes(&mut archive, &manifest)?;
    Ok(manifest)
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
                if resolved_package_still_allowed(
                    &id,
                    &existing.candidate.manifest.version,
                    constraints.get(&id).map(Vec::as_slice).unwrap_or(&[]),
                )? {
                    continue;
                }
                resolved.remove(&id);
                changed = true;
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

fn resolved_package_still_allowed(id: &str, version: &Version, ranges: &[String]) -> Result<bool> {
    for range in ranges {
        if !VersionRange::parse(range)?.allows(version) {
            return Ok(false);
        }
    }
    let _ = id;
    Ok(true)
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
                .all(|range| range.allows(&candidate.manifest.version))
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
    reject_oversized_entry(path, entry.size())?;
    sha256_reader(&mut entry)
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

fn cache_package_path(cache_root: &Path, candidate: &PackageCandidate) -> PathBuf {
    cache_root
        .join(&candidate.manifest.normalized_id)
        .join(candidate.manifest.version.to_string())
}

fn restore_to_cache(candidate: &PackageCandidate, cache_root: &Path) -> Result<PathBuf> {
    let package_root = cache_package_path(cache_root, candidate);
    let marker = package_root.join(".sloppy.package.sha256");
    if package_root.exists() {
        if marker.is_file() {
            let existing =
                fs::read_to_string(&marker).map_err(io_error("SLOPPY_E_PACKAGE_HASH_MISMATCH"))?;
            if existing.trim() == candidate.sha256 {
                verify_cached_package(candidate, &package_root)?;
                return Ok(package_root);
            }
        }
        return Err(PackageError::new(
            "SLOPPY_E_PACKAGE_CACHE_CORRUPT",
            format!(
                "cached package '{}' {} is missing or has a mismatched cache marker",
                candidate.manifest.id, candidate.manifest.version
            ),
        ));
    }
    let temp_root = cache_root.join(format!(
        ".{}.{}.tmp-{}",
        candidate.manifest.normalized_id,
        candidate.manifest.version,
        std::process::id()
    ));
    fs::create_dir_all(cache_root).map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
    if temp_root.exists() {
        fs::remove_dir_all(&temp_root).map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
    }
    fs::create_dir_all(&temp_root).map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
    let result = (|| {
        extract_archive_safely(&candidate.artifact, &temp_root)?;
        fs::write(temp_root.join(".sloppy.package.sha256"), &candidate.sha256)
            .map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
        if let Some(parent) = package_root.parent() {
            fs::create_dir_all(parent).map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
        }
        fs::rename(&temp_root, &package_root).map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))
    })();
    if let Err(error) = result {
        let _ = fs::remove_dir_all(&temp_root);
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
    let mut normalized_seen = BTreeSet::new();
    let mut expanded_bytes = 0_u64;
    for index in 0..archive.len() {
        let mut entry = archive
            .by_index(index)
            .map_err(package_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
        reject_unsafe_zip_entry(entry.unix_mode())?;
        let name = entry.name().to_string();
        validate_relative_archive_path(&name)?;
        reject_oversized_entry(&name, entry.size())?;
        expanded_bytes = expanded_bytes.checked_add(entry.size()).ok_or_else(|| {
            PackageError::new(
                "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
                "package archive expanded size overflowed",
            )
        })?;
        if expanded_bytes > MAX_EXPANDED_BYTES {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
                "package archive expanded size is too large",
            ));
        }
        if name.ends_with('/') {
            continue;
        }
        if !seen.insert(name.clone()) {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_DUPLICATE_PATH",
                format!("duplicate archive path '{name}'"),
            ));
        }
        if !normalized_seen.insert(normalize_archive_path(&name)) {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_DUPLICATE_PATH",
                format!("duplicate normalized archive path '{name}'"),
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

fn reject_oversized_file(path: &Path, max_bytes: u64) -> Result<()> {
    let size = fs::metadata(path)
        .map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?
        .len();
    if size > max_bytes {
        return Err(PackageError::new(
            "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
            format!("package file is too large: {}", path.display()),
        ));
    }
    Ok(())
}

fn reject_oversized_entry(name: &str, size: u64) -> Result<()> {
    if size > MAX_ENTRY_BYTES {
        return Err(PackageError::new(
            "SLOPPY_E_PACKAGE_MANIFEST_INVALID",
            format!("package archive entry is too large: {name}"),
        ));
    }
    Ok(())
}

fn sha256_reader(reader: &mut impl Read) -> Result<String> {
    let mut hasher = Sha256::new();
    let mut buffer = [0_u8; 8192];
    loop {
        let read = reader
            .read(&mut buffer)
            .map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
        if read == 0 {
            break;
        }
        hasher.update(&buffer[..read]);
    }
    let digest = hasher.finalize();
    let mut output = String::with_capacity(64);
    for byte in digest {
        output.push_str(&format!("{byte:02x}"));
    }
    Ok(output)
}

fn verify_manifest_asset_hashes<R: Read + Seek>(
    archive: &mut ZipArchive<R>,
    manifest: &PackageManifest,
) -> Result<()> {
    let Some(hashes) = manifest
        .manifest_json
        .get("sha256")
        .and_then(Value::as_object)
    else {
        return Ok(());
    };
    for path in declared_asset_paths(manifest) {
        let Some(expected) = hashes.get(&path).and_then(Value::as_str) else {
            continue;
        };
        let mut entry = archive.by_name(&path).map_err(|_| {
            PackageError::new(
                "SLOPPY_E_PACKAGE_NATIVE_ASSET_MISSING",
                format!("package asset is missing from archive: {path}"),
            )
        })?;
        reject_oversized_entry(&path, entry.size())?;
        let actual = sha256_reader(&mut entry)?;
        if actual != expected {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_HASH_MISMATCH",
                format!("package asset hash mismatch: {path}"),
            ));
        }
    }
    Ok(())
}

fn verify_cached_package(candidate: &PackageCandidate, package_root: &Path) -> Result<()> {
    for path in declared_asset_paths(&candidate.manifest) {
        let full = package_root.join(&path);
        validate_relative_archive_path(&path)?;
        let metadata = fs::symlink_metadata(&full).map_err(|_| {
            PackageError::new(
                "SLOPPY_E_PACKAGE_CACHE_CORRUPT",
                format!("cached package asset is missing: {path}"),
            )
        })?;
        if !metadata.file_type().is_file() || metadata.file_type().is_symlink() {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_CACHE_CORRUPT",
                format!("cached package asset is not a regular file: {path}"),
            ));
        }
        let bytes = fs::read(&full).map_err(io_error("SLOPPY_E_PACKAGE_CACHE_CORRUPT"))?;
        let actual = sha256_bytes(&bytes);
        let expected = hash_archive_entry(&candidate.artifact, &path)?;
        if actual != expected {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_CACHE_CORRUPT",
                format!("cached package asset hash mismatch: {path}"),
            ));
        }
    }
    Ok(())
}

fn discover_static_feed_artifacts(source_path: &Path) -> Result<Vec<PathBuf>> {
    let root = source_path.join("v3-flatcontainer");
    if !root.is_dir() {
        return Ok(Vec::new());
    }
    let mut artifacts = Vec::new();
    for id_entry in fs::read_dir(root).map_err(io_error("SLOPPY_E_PACKAGE_SOURCE_MISSING"))? {
        let id_path = id_entry
            .map_err(io_error("SLOPPY_E_PACKAGE_SOURCE_MISSING"))?
            .path();
        if !id_path.is_dir() {
            continue;
        }
        for version_entry in
            fs::read_dir(id_path).map_err(io_error("SLOPPY_E_PACKAGE_SOURCE_MISSING"))?
        {
            let version_path = version_entry
                .map_err(io_error("SLOPPY_E_PACKAGE_SOURCE_MISSING"))?
                .path();
            if !version_path.is_dir() {
                continue;
            }
            for artifact_entry in
                fs::read_dir(version_path).map_err(io_error("SLOPPY_E_PACKAGE_SOURCE_MISSING"))?
            {
                let artifact = artifact_entry
                    .map_err(io_error("SLOPPY_E_PACKAGE_SOURCE_MISSING"))?
                    .path();
                if artifact.extension().and_then(|ext| ext.to_str()) == Some("slpkg") {
                    artifacts.push(artifact);
                }
            }
        }
    }
    Ok(artifacts)
}

fn verify_locked_hashes(lockfile: &Value, packages: &[ResolvedPackage]) -> Result<()> {
    let Some(locked_packages) = lockfile.get("packages").and_then(Value::as_object) else {
        return Err(PackageError::new(
            "SLOPPY_E_PACKAGE_LOCK_OUT_OF_DATE",
            "sloppy.lock.json is missing packages",
        ));
    };
    for package in packages {
        let key = format!(
            "{}/{}",
            package.candidate.manifest.normalized_id, package.candidate.manifest.version
        );
        let Some(locked) = locked_packages.get(&key) else {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_LOCK_OUT_OF_DATE",
                format!("locked package is missing: {key}"),
            ));
        };
        let locked_hash =
            json_string_required(locked, "sha256", "SLOPPY_E_PACKAGE_LOCK_OUT_OF_DATE")?;
        if locked_hash != package.candidate.sha256 {
            return Err(PackageError::new(
                "SLOPPY_E_PACKAGE_HASH_MISMATCH",
                format!("locked package artifact hash mismatch: {key}"),
            ));
        }
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

fn lockfile_json(
    target: &str,
    runtime_identifier: &str,
    sources: &[PackageSource],
    packages: &[ResolvedPackage],
) -> Value {
    let source_values = sources
        .iter()
        .map(|source| {
            json!({
                "name": source.name,
                "type": match source.kind {
                    SourceKind::Folder => "folder",
                    SourceKind::Sloppy => "sloppy",
                    SourceKind::Npm => "npm",
                },
                "source": source.display,
            })
        })
        .collect::<Vec<_>>();
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
                "normalizedId": package.candidate.manifest.normalized_id,
                "version": package.candidate.manifest.version.to_string(),
                "source": package.candidate.source_display,
                "sourceType": "folder",
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
        "sources": source_values,
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
                    "package": package.candidate.manifest.id,
                    "sha256": package.selected_hashes.get(path).cloned().unwrap_or_default(),
                }),
            );
        }
        package_values.push(json!({
            "id": package.candidate.manifest.id,
            "normalizedId": package.candidate.manifest.normalized_id,
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

fn list_native_value(assets: &Value) -> Value {
    let mut output = Vec::new();
    if let Some(packages) = assets.get("packages").and_then(Value::as_array) {
        for package in packages {
            let id = package.get("id").and_then(Value::as_str).unwrap_or("");
            let version = package.get("version").and_then(Value::as_str).unwrap_or("");
            if let Some(native) = package.get("nativeLibraries").and_then(Value::as_object) {
                for (name, asset) in native {
                    output.push(json!({
                        "name": name,
                        "package": id,
                        "version": version,
                        "path": asset.get("path").and_then(Value::as_str).unwrap_or(""),
                        "sha256": asset.get("sha256").and_then(Value::as_str).unwrap_or(""),
                    }));
                }
            }
        }
    }
    Value::Array(output)
}

fn list_capabilities_value(assets: &Value) -> Value {
    let mut capabilities = BTreeMap::<String, Vec<String>>::new();
    if let Some(packages) = assets.get("packages").and_then(Value::as_array) {
        for package in packages {
            let id = package.get("id").and_then(Value::as_str).unwrap_or("");
            if let Some(items) = package.get("capabilities").and_then(Value::as_array) {
                for capability in items.iter().filter_map(Value::as_str) {
                    capabilities
                        .entry(capability.to_string())
                        .or_default()
                        .push(id.to_string());
                }
            }
        }
    }
    json!(capabilities)
}

fn list_outdated_value(cwd: &Path, lock: &Value) -> Result<Value> {
    let project = read_json_file(&cwd.join("sloppy.json"))?;
    let sources = package_sources(cwd, &project)?;
    let candidates = discover_candidates(&sources)?;
    let mut output = Vec::new();
    if let Some(packages) = lock.get("packages").and_then(Value::as_object) {
        for package in packages.values() {
            let Some(id) = package.get("id").and_then(Value::as_str) else {
                continue;
            };
            let Some(current) = package.get("version").and_then(Value::as_str) else {
                continue;
            };
            let current_version = Version::parse(current)?;
            let latest = candidates
                .iter()
                .filter(|candidate| candidate.manifest.normalized_id == normalize_id(id))
                .map(|candidate| candidate.manifest.version.clone())
                .max();
            if let Some(latest) = latest {
                if latest > current_version {
                    output.push(json!({
                        "id": id,
                        "current": current,
                        "latest": latest.to_string(),
                    }));
                }
            }
        }
    }
    Ok(Value::Array(output))
}

fn render_list(subject: &str, value: &Value) -> String {
    let mut lines = Vec::new();
    match subject {
        "packages" => {
            if let Some(packages) = value.as_array() {
                for package in packages {
                    lines.push(format!(
                        "{} {}",
                        package.get("id").and_then(Value::as_str).unwrap_or(""),
                        package.get("version").and_then(Value::as_str).unwrap_or("")
                    ));
                }
            }
        }
        "native" | "outdated" => {
            if let Some(items) = value.as_array() {
                for item in items {
                    lines.push(serde_json::to_string(item).unwrap_or_default());
                }
            }
        }
        "capabilities" => {
            if let Some(map) = value.as_object() {
                for (capability, packages) in map {
                    let package_list = packages
                        .as_array()
                        .map(|items| {
                            items
                                .iter()
                                .filter_map(Value::as_str)
                                .collect::<Vec<_>>()
                                .join(", ")
                        })
                        .unwrap_or_default();
                    lines.push(format!("{capability}: {package_list}"));
                }
            }
        }
        _ => {}
    }
    if lines.is_empty() {
        format!("No {subject} found.\n")
    } else {
        format!("{}\n", lines.join("\n"))
    }
}

fn render_cache_list(root: &Path) -> Result<String> {
    if !root.exists() {
        return Ok(format!(
            "Cache root: {}\nNo packages cached.\n",
            root.display()
        ));
    }
    let mut lines = vec![format!("Cache root: {}", root.display())];
    let mut ids = fs::read_dir(root)
        .map_err(io_error("SLOPPY_E_PACKAGE_CACHE_CORRUPT"))?
        .filter_map(|entry| entry.ok().map(|entry| entry.path()))
        .filter(|path| path.is_dir())
        .collect::<Vec<_>>();
    ids.sort();
    for id_path in ids {
        let Some(id) = id_path.file_name().and_then(|name| name.to_str()) else {
            continue;
        };
        let mut versions = fs::read_dir(&id_path)
            .map_err(io_error("SLOPPY_E_PACKAGE_CACHE_CORRUPT"))?
            .filter_map(|entry| entry.ok().map(|entry| entry.path()))
            .filter(|path| path.is_dir())
            .collect::<Vec<_>>();
        versions.sort();
        for version_path in versions {
            if let Some(version) = version_path.file_name().and_then(|name| name.to_str()) {
                lines.push(format!("{id} {version}"));
            }
        }
    }
    Ok(format!("{}\n", lines.join("\n")))
}

fn generate_feed_index(folder: &Path) -> Result<()> {
    fs::create_dir_all(folder).map_err(io_error("SLOPPY_E_PACKAGE_SOURCE_MISSING"))?;
    let mut flat_versions: BTreeMap<String, Vec<Value>> = BTreeMap::new();
    let artifacts = fs::read_dir(folder)
        .map_err(io_error("SLOPPY_E_PACKAGE_SOURCE_MISSING"))?
        .filter_map(|entry| entry.ok().map(|entry| entry.path()))
        .filter(|path| path.extension().and_then(|ext| ext.to_str()) == Some("slpkg"))
        .collect::<Vec<_>>();
    for artifact in artifacts {
        reject_oversized_file(&artifact, MAX_ARCHIVE_BYTES)?;
        let bytes = fs::read(&artifact).map_err(io_error("SLOPPY_E_PACKAGE_MANIFEST_INVALID"))?;
        let sha256 = sha256_bytes(&bytes);
        let manifest = read_manifest_from_archive(Cursor::new(bytes))?;
        let id = manifest.normalized_id;
        let version = manifest.version.to_string();
        let version_dir = folder.join("v3-flatcontainer").join(&id).join(&version);
        fs::create_dir_all(&version_dir).map_err(io_error("SLOPPY_E_PACKAGE_SOURCE_MISSING"))?;
        let target = version_dir.join(format!("{id}.{version}.slpkg"));
        fs::copy(&artifact, &target).map_err(io_error("SLOPPY_E_PACKAGE_SOURCE_MISSING"))?;
        flat_versions.entry(id).or_default().push(json!({
            "version": version,
            "sha256": sha256,
            "package": target.to_string_lossy().replace('\\', "/"),
        }));
    }
    for (id, mut versions) in flat_versions {
        versions.sort_by(|left, right| {
            left["version"]
                .as_str()
                .unwrap_or("")
                .cmp(right["version"].as_str().unwrap_or(""))
        });
        write_json_file(
            &folder.join("v3-flatcontainer").join(id).join("index.json"),
            &json!({"versions": versions}),
        )?;
    }
    write_json_file(
        &folder.join("v3").join("index.json"),
        &json!({
            "version": "3.0.0",
            "resources": [
                {
                    "@id": "../v3-flatcontainer/",
                    "@type": "PackageBaseAddress/3.0.0"
                }
            ]
        }),
    )
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
        assert!(exact.allows(&Version::parse("1.2.3").expect("version")));
        assert!(!exact.allows(&Version::parse("1.2.4").expect("version")));

        let bounded = VersionRange::parse("[1.0.0,2.0.0)").expect("bounded range");
        assert!(bounded.allows(&Version::parse("1.0.0").expect("version")));
        assert!(bounded.allows(&Version::parse("1.5.0").expect("version")));
        assert!(!bounded.allows(&Version::parse("2.0.0").expect("version")));

        let open = VersionRange::parse("[1.0.0,)").expect("open range");
        assert!(open.allows(&Version::parse("9.0.0").expect("version")));
        assert!(VersionRange::parse("(1.0.0,2.0.0)").is_ok());
        assert!(VersionRange::parse("(,2.0.0)")
            .expect("open lower")
            .allows(&Version::parse("1.0.0-alpha.1").expect("pre")));
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
        copy_dir_all(&packages, &app.join("packages")).expect("copy app packages");
        fs::write(
            app.join("sloppy.json"),
            r#"{
  "name": "app",
  "entry": "src/main.ts",
  "target": "sloppy1.0",
  "runtimeIdentifier": "win-x64",
  "packageSources": ["packages"],
  "dependencies": {
    "Sloppy.Example": "[0.1.0,2.0.0)"
  }
}
"#,
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
            r#"{
  "name": "app",
  "entry": "src/main.ts",
  "target": "sloppy1.0",
  "runtimeIdentifier": "win-x64",
  "packageSources": ["packages"],
  "dependencies": {
    "Sloppy.Example": "[0.2.0,2.0.0)"
  }
}
"#,
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
            r#"{
  "name": "app",
  "entry": "src/main.ts",
  "target": "sloppy1.0",
  "runtimeIdentifier": "win-x64",
  "packageSources": ["packages"],
  "dependencies": {
    "Sloppy.Example": "[0.1.0]",
    "Sloppy.Core": "[0.2.0]"
  }
}
"#,
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
        copy_dir_all(&packages, &app.join("packages")).expect("copy app packages");
        fs::write(
            app.join("sloppy.json"),
            r#"{
  "target": "sloppy1.0",
  "runtimeIdentifier": "linux-x64",
  "packageSources": ["packages"],
  "dependencies": { "Sloppy.Example": "[0.1.0]" }
}
"#,
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

    fn copy_dir_all(from: &Path, to: &Path) -> io::Result<()> {
        fs::create_dir_all(to)?;
        for entry in fs::read_dir(from)? {
            let entry = entry?;
            let target = to.join(entry.file_name());
            if entry.file_type()?.is_dir() {
                copy_dir_all(&entry.path(), &target)?;
            } else {
                fs::copy(entry.path(), target)?;
            }
        }
        Ok(())
    }

    fn temp_dir(label: &str) -> PathBuf {
        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("time")
            .as_nanos();
        env::temp_dir().join(format!("sloppy-package-manager-{label}-{nonce}"))
    }
}

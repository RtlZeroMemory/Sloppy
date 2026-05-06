//! Supported import resolution.
//!
//! Slop resolves only documented Slop imports and source-root relative files. This is not
//! Node/npm/package-manager resolution.

use std::{
    fs,
    path::{Path, PathBuf},
};

#[derive(Debug, Clone, Eq, PartialEq)]
pub enum ImportKind {
    Relative(PathBuf),
    SlopStdlib,
    SlopTime,
    SlopFilesystem,
    SlopCrypto,
    SqliteProvider,
    UnresolvedRelative(String),
    UnsupportedBare(String),
    Remote(String),
}

pub fn classify_import(from_path: &Path, specifier: &str) -> ImportKind {
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
    if specifier == "sloppy/providers/sqlite" {
        return ImportKind::SqliteProvider;
    }
    if specifier.starts_with("http://") || specifier.starts_with("https://") {
        return ImportKind::Remote(specifier.to_string());
    }
    ImportKind::UnsupportedBare(specifier.to_string())
}

pub fn resolve_relative_import(from_path: &Path, specifier: &str) -> Option<PathBuf> {
    let base = from_path.parent().unwrap_or_else(|| Path::new(""));
    let candidate = base.join(specifier);
    let candidates = if let Some(extension) = candidate.extension().and_then(|ext| ext.to_str()) {
        match extension {
            "js" | "mjs" | "ts" => vec![candidate],
            _ => return None,
        }
    } else {
        vec![
            candidate.with_extension("js"),
            candidate.with_extension("mjs"),
            candidate.with_extension("ts"),
        ]
    };
    candidates
        .into_iter()
        .find(|candidate| candidate.is_file())
        .and_then(|candidate| fs::canonicalize(candidate).ok())
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

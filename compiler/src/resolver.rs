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
    SqliteProvider,
    UnsupportedBare(String),
    Remote(String),
}

pub fn classify_import(from_path: &Path, specifier: &str) -> ImportKind {
    if specifier.starts_with("./") || specifier.starts_with("../") {
        return resolve_relative_import(from_path, specifier).map_or_else(
            || ImportKind::UnsupportedBare(specifier.to_string()),
            ImportKind::Relative,
        );
    }
    if specifier == "sloppy" {
        return ImportKind::SlopStdlib;
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
    let candidates = if candidate.extension().is_some() {
        vec![candidate]
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
    resolved.starts_with(source_root)
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
}

//! Source-level module graph construction for compiler-owned relative imports.

use std::{
    collections::{BTreeMap, BTreeSet},
    fs,
    path::{Path, PathBuf},
};

use crate::source::source_map_source_name;

#[derive(Debug, Clone, Eq, PartialEq)]
pub struct SourceFileRecord {
    pub name: String,
    pub path: PathBuf,
    pub source: String,
}

#[derive(Debug)]
pub struct SourceModuleGraph {
    pub entry_dir: PathBuf,
    visiting: BTreeSet<PathBuf>,
    sources: BTreeMap<String, SourceFileRecord>,
}

impl SourceModuleGraph {
    pub fn new(entry_path: &Path) -> Self {
        let mut entry_dir = entry_path
            .parent()
            .unwrap_or_else(|| Path::new(""))
            .to_path_buf();
        if entry_dir.as_os_str().is_empty() {
            entry_dir = std::env::current_dir().unwrap_or_else(|_| PathBuf::from("."));
        }
        Self {
            entry_dir: fs::canonicalize(&entry_dir).unwrap_or(entry_dir),
            visiting: BTreeSet::new(),
            sources: BTreeMap::new(),
        }
    }

    pub fn record_source(&mut self, path: &Path, source: &str) -> String {
        let name = source_map_source_name(path);
        self.sources
            .entry(name.clone())
            .or_insert(SourceFileRecord {
                name: name.clone(),
                path: path.to_path_buf(),
                source: source.to_string(),
            });
        name
    }

    pub fn enter(&mut self, path: &Path) -> bool {
        self.visiting.insert(path.to_path_buf())
    }

    pub fn leave(&mut self, path: &Path) {
        self.visiting.remove(path);
    }

    pub fn is_visiting(&self, path: &Path) -> bool {
        self.visiting.contains(path)
    }

    pub fn source_records(&self) -> Vec<SourceFileRecord> {
        self.sources.values().cloned().collect()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn records_sources_deterministically_once_by_source_name() {
        let mut graph = SourceModuleGraph::new(Path::new("src/app.js"));
        graph.record_source(Path::new("src/app.js"), "one");
        graph.record_source(Path::new("src/app.js"), "two");
        let records = graph.source_records();
        assert_eq!(records.len(), 1);
        assert_eq!(records[0].name, "app.js");
        assert_eq!(records[0].source, "one");
    }
}

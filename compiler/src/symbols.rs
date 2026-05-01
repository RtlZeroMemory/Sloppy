//! Symbol binding for Slop app, route group, provider, schema, and helper identifiers.

use std::collections::BTreeMap;

#[derive(Debug, Clone, Eq, PartialEq)]
pub enum SymbolKind {
    SlopApp,
    SlopBuilder,
    RouteGroup { prefix: String },
    ProviderHandle { token: String },
    FunctionModule { export_name: String },
    Schema,
    Results,
    Config,
    Context,
}

#[derive(Debug, Default)]
pub struct SymbolTable {
    bindings: BTreeMap<String, SymbolKind>,
}

impl SymbolTable {
    pub fn bind(&mut self, name: impl Into<String>, kind: SymbolKind) {
        self.bindings.insert(name.into(), kind);
    }

    pub fn get(&self, name: &str) -> Option<&SymbolKind> {
        self.bindings.get(name)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn tracks_app_and_group_symbols() {
        let mut table = SymbolTable::default();
        table.bind("app", SymbolKind::SlopApp);
        table.bind(
            "api",
            SymbolKind::RouteGroup {
                prefix: "/api".to_string(),
            },
        );
        assert_eq!(table.get("app"), Some(&SymbolKind::SlopApp));
        assert_eq!(
            table.get("api"),
            Some(&SymbolKind::RouteGroup {
                prefix: "/api".to_string()
            })
        );
    }
}

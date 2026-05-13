// Dependency graph JSON and compatibility finding emission for Plan artifacts.
use super::*;

pub(crate) fn emit_dependency_graph(graph: &DependencyGraph) -> Result<String, Diagnostic> {
    serde_json::to_string_pretty(&dependency_graph_json(graph))
        .map(|json| format!("{json}\n"))
        .map_err(|error| {
            Diagnostic::new(
                "SLOPPYC_E_EMIT",
                format!("failed to emit deps.graph.json: {error}"),
            )
        })
}

pub(crate) fn dependency_graph_json(graph: &DependencyGraph) -> Value {
    let packages = graph
        .packages
        .iter()
        .map(|package| {
            let mut value = json!({
                "name": package.name,
                "root": package.root,
                "entry": package.entry,
                "format": package.format.as_str(),
                "source": package.source
            });
            if let Some(version) = &package.version {
                value["version"] = json!(version);
            }
            if let Some(package_json) = &package.package_json {
                value["packageJson"] = json!(package_json);
            }
            value
        })
        .collect::<Vec<_>>();

    let modules = graph
        .modules
        .iter()
        .map(|module| {
            let mut value = json!({
                "id": module.id,
                "source": module.source,
                "format": module.format.as_str(),
                "imports": module.imports,
                "resolvedImports": module
                    .resolved_imports
                    .iter()
                    .map(|import| {
                        json!({
                            "specifier": import.specifier,
                            "resolvedId": import.resolved_id,
                            "kind": import.kind
                        })
                    })
                    .collect::<Vec<_>>(),
                "dynamicImports": module
                    .dynamic_imports
                    .iter()
                    .map(|import| {
                        let mut value = json!({
                            "kind": import.kind
                        });
                        if let Some(specifier) = &import.specifier {
                            value["specifier"] = json!(specifier);
                        }
                        if let Some(resolved_id) = &import.resolved_id {
                            value["resolvedId"] = json!(resolved_id);
                        }
                        value
                    })
                    .collect::<Vec<_>>()
            });
            if let Some(package) = &module.package {
                value["package"] = json!(package);
            }
            if let Some(included_by) = &module.included_by {
                value["includedBy"] = json!(included_by);
            }
            value
        })
        .collect::<Vec<_>>();

    let assets = graph
        .assets
        .iter()
        .map(|asset| {
            json!({
                "path": asset.path,
                "includedBy": asset.included_by
            })
        })
        .collect::<Vec<_>>();

    let node_builtins = graph
        .node_builtins
        .iter()
        .map(|builtin| {
            let mut value = json!({
                "specifier": builtin.specifier,
                "status": builtin.status
            });
            if let Some(backing) = &builtin.backing {
                value["backing"] = json!(backing);
            }
            if let Some(capability) = &builtin.capability {
                value["capability"] = json!(capability);
            }
            value
        })
        .collect::<Vec<_>>();

    let compatibility_findings = graph
        .compatibility_findings
        .iter()
        .map(|finding| {
            let mut value = json!({
                "code": finding.code,
                "severity": finding.severity,
                "message": finding.message
            });
            if let Some(source) = &finding.source {
                value["source"] = json!(source);
            }
            if let Some(package) = &finding.package {
                value["package"] = json!(package);
            }
            if let Some(specifier) = &finding.specifier {
                value["specifier"] = json!(specifier);
            }
            if let Some(hint) = &finding.hint {
                value["hint"] = json!(hint);
            }
            value
        })
        .collect::<Vec<_>>();

    json!({
        "schemaVersion": 1,
        "resolver": {
            "profiles": graph.resolver_profiles,
            "conditions": graph.resolver_conditions
        },
        "packages": packages,
        "modules": modules,
        "assets": assets,
        "nodeBuiltins": node_builtins,
        "compatibilityFindings": compatibility_findings
    })
}

pub(super) fn source_location_json(source_name: &str, source: &str, span: Span) -> Value {
    let (line, column) = line_column(source, span.start);
    json!({
        "path": source_name,
        "line": line,
        "column": column
    })
}

pub(super) fn ffi_argument_marshaling(parameters: &[String]) -> &'static str {
    if parameters.iter().any(|parameter| {
        matches!(
            parameter.as_str(),
            "cstring" | "lpcstr" | "utf16" | "lpcwstr" | "bytes" | "mutBytes"
        )
    }) {
        "call-duration"
    } else {
        "direct"
    }
}

pub(super) fn package_manifest_entry_json(entry: &ConfigurationPackageEntry) -> Value {
    let mut value = json!({
        "key": entry.key,
        "env": entry.env,
        "type": entry.value_type,
        "secret": entry.sensitive
    });
    if let Some(default_value) = &entry.default_value {
        value["default"] = if entry.sensitive {
            json!("<redacted>")
        } else {
            default_value.clone()
        };
    }
    value
}

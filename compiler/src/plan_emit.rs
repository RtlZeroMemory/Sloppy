//! Deterministic `app.plan.json` emission from the internal AppGraph.

use std::collections::{BTreeMap, BTreeSet};

use oxc_span::Span;
use serde_json::{json, Map, Value};

use crate::diagnostic::Diagnostic;
use crate::graph::{
    route_parameter_names, route_pattern_has_params, AuthSchemeMetadata, ConfigurationPackageEntry,
    DependencyGraph, ExtractedApp, ProjectKind, RequestBinding, ResponseMetadata,
};
use crate::hash::sha256_hex;
use crate::route_artifact::{
    route_execution_kind, RouteDispatchArtifactMetadata, RouteExecutionKind,
};
use crate::source::line_column;
use crate::validation::{
    plan_completeness, route_completeness, Completeness, CompletenessReason, RouteCompletenessInput,
};
use crate::version::{COMPILER_VERSION, RUNTIME_MINIMUM_VERSION, STDLIB_VERSION};

fn completeness_json(completeness: &Completeness) -> Value {
    json!({
        "status": completeness.status.as_str(),
        "reasons": completeness
            .reasons
            .iter()
            .map(|reason| {
                json!({
                    "code": reason.code,
                    "message": reason.message
                })
            })
            .collect::<Vec<_>>()
    })
}

fn route_pattern_leading_static_segment(pattern: &str) -> Option<&str> {
    let segment = pattern
        .split('/')
        .skip(1)
        .find(|segment| !segment.is_empty())?;
    (!segment.starts_with('{')).then_some(segment)
}

fn route_pattern_constraint_names(pattern: &str) -> Vec<String> {
    pattern
        .split('/')
        .skip(1)
        .filter_map(|segment| {
            if !(segment.starts_with('{') && segment.ends_with('}')) {
                return None;
            }
            let inner = &segment[1..segment.len() - 1];
            let (_, constraint) = inner.split_once(':')?;
            Some(constraint.to_string())
        })
        .collect()
}

fn route_pattern_segment_trie_key(segment: &str) -> String {
    if segment.starts_with('{') && segment.ends_with('}') {
        let inner = &segment[1..segment.len() - 1];
        let constraint = inner.split_once(':').map(|(_, ty)| ty).unwrap_or("str");
        return format!("p:{constraint}");
    }
    format!("s:{segment}")
}

fn route_dispatch_segment_trie_nodes(app: &ExtractedApp) -> usize {
    let mut prefixes = BTreeSet::<Vec<String>>::new();

    for route in app
        .routes
        .iter()
        .filter(|route| route_pattern_has_params(&route.pattern))
    {
        let mut prefix = vec![format!("m:{}", route.method)];
        prefixes.insert(prefix.clone());
        for segment in route
            .pattern
            .split('/')
            .skip(1)
            .filter(|segment| !segment.is_empty())
        {
            prefix.push(route_pattern_segment_trie_key(segment));
            prefixes.insert(prefix.clone());
        }
    }

    prefixes.len()
}

fn route_json_request_plan(bindings: &[RequestBinding]) -> Value {
    let Some(binding) = bindings.iter().find(|binding| binding.kind == "body.json") else {
        return json!({
            "mode": "none",
            "materialization": "none",
            "unknownFields": "ignore"
        });
    };

    if let Some(schema) = &binding.schema {
        if !schema.is_empty() {
            return json!({
                "mode": "native-schema",
                "schema": schema,
                "materialization": "materialize-once",
                "unknownFields": "ignore",
                "maxBodyBytes": 65536,
                "maxDepth": 50
            });
        }
    }

    json!({
        "mode": "generic",
        "materialization": "generic",
        "unknownFields": "ignore",
        "fallbackReason": "schema-missing",
        "maxBodyBytes": 65536,
        "maxDepth": 50
    })
}

fn skip_ascii_whitespace(source: &str, mut index: usize) -> usize {
    while index < source.len() && source.as_bytes()[index].is_ascii_whitespace() {
        index += 1;
    }
    index
}

fn matching_delimiter(source: &str, start: usize, open: u8, close: u8) -> Option<usize> {
    if *source.as_bytes().get(start)? != open {
        return None;
    }
    let mut depth = 0usize;
    let mut index = start;
    let mut quote: Option<u8> = None;
    while index < source.len() {
        let current = source.as_bytes()[index];
        if let Some(active_quote) = quote {
            if current == b'\\' {
                index += 2;
                continue;
            }
            if current == active_quote {
                quote = None;
            }
            index += 1;
            continue;
        }
        if current == b'"' || current == b'\'' || current == b'`' {
            quote = Some(current);
        } else if current == open {
            depth += 1;
        } else if current == close {
            depth = depth.checked_sub(1)?;
            if depth == 0 {
                return Some(index);
            }
        }
        index += 1;
    }
    None
}

fn parse_static_identifier(source: &str, start: usize) -> Option<(&str, usize)> {
    let start = skip_ascii_whitespace(source, start);
    let mut end = start;
    while end < source.len() {
        let ch = source.as_bytes()[end];
        if ch == b'_' || ch == b'$' || ch.is_ascii_alphanumeric() {
            end += 1;
        } else {
            break;
        }
    }
    (end > start).then_some((&source[start..end], end))
}

pub(crate) fn split_top_level_properties(object_source: &str) -> Vec<&str> {
    let mut parts = Vec::new();
    let mut start = 0usize;
    let mut index = 0usize;
    let mut paren = 0usize;
    let mut brace = 0usize;
    let mut bracket = 0usize;
    let mut quote: Option<u8> = None;
    while index < object_source.len() {
        let current = object_source.as_bytes()[index];
        if let Some(active_quote) = quote {
            if current == b'\\' {
                index += 2;
                continue;
            }
            if current == active_quote {
                quote = None;
            }
            index += 1;
            continue;
        }
        match current {
            b'"' | b'\'' | b'`' => quote = Some(current),
            b'(' => paren += 1,
            b')' => paren = paren.saturating_sub(1),
            b'{' => brace += 1,
            b'}' => brace = brace.saturating_sub(1),
            b'[' => bracket += 1,
            b']' => bracket = bracket.saturating_sub(1),
            b',' if paren == 0 && brace == 0 && bracket == 0 => {
                let part = object_source[start..index].trim();
                if !part.is_empty() {
                    parts.push(part);
                }
                start = index + 1;
            }
            _ => {}
        }
        index += 1;
    }
    let tail = object_source[start..].trim();
    if !tail.is_empty() {
        parts.push(tail);
    }
    parts
}

pub(crate) fn parse_property_name(part: &str) -> Option<(&str, &str)> {
    let colon = part.find(':')?;
    let raw_name = part[..colon].trim();
    if raw_name.is_empty() {
        return None;
    }
    let name = raw_name
        .strip_prefix('"')
        .and_then(|value| value.strip_suffix('"'))
        .or_else(|| {
            raw_name
                .strip_prefix('\'')
                .and_then(|value| value.strip_suffix('\''))
        })
        .unwrap_or(raw_name);
    Some((name, part[colon + 1..].trim()))
}

pub(crate) fn parse_reference(expression: &str) -> Option<Value> {
    let reference = expression.find(".references(")?;
    let after_arrow = expression[reference..].find("=>")? + reference + 2;
    let target = expression[after_arrow..].trim_start();
    let table_end = target.find('.')?;
    let table = target[..table_end].trim();
    let column_start = table_end + 1;
    let column_end = target[column_start..]
        .find(|ch: char| !(ch.is_ascii_alphanumeric() || ch == '_' || ch == '$'))
        .map(|index| column_start + index)
        .unwrap_or(target.len());
    let column = target[column_start..column_end].trim();
    if table.is_empty() || column.is_empty() {
        return None;
    }
    Some(json!({ "tableModel": table, "column": column }))
}

fn parse_static_column_ref(expression: &str) -> Option<Value> {
    let expression = expression.trim();
    let dot = expression.find('.')?;
    let table = expression[..dot].trim();
    let column_start = dot + 1;
    let column_end = expression[column_start..]
        .find(|ch: char| !(ch.is_ascii_alphanumeric() || ch == '_' || ch == '$'))
        .map(|index| column_start + index)
        .unwrap_or(expression.len());
    let column = expression[column_start..column_end].trim();
    if table.is_empty() || column.is_empty() {
        return None;
    }
    Some(json!({ "tableModel": table, "column": column }))
}

fn parse_relation_options(expression: &str) -> Option<(Value, Value)> {
    let expression = expression.trim();
    if !expression.starts_with('{') {
        return None;
    }
    let end = matching_delimiter(expression, 0, b'{', b'}')?;
    let mut local = None;
    let mut foreign = None;
    for part in split_top_level_properties(&expression[1..end]) {
        let Some((name, value)) = parse_property_name(part) else {
            continue;
        };
        match name {
            "local" => local = parse_static_column_ref(value),
            "foreign" => foreign = parse_static_column_ref(value),
            _ => {}
        }
    }
    Some((local?, foreign?))
}

pub(crate) fn parse_relation_definition(name: &str, expression: &str) -> Option<Value> {
    let expression = expression.trim();
    let (kind, mut index) = parse_static_identifier(expression, 0)?;
    if !matches!(kind, "one" | "many") {
        return None;
    }
    index = skip_ascii_whitespace(expression, index);
    if expression.as_bytes().get(index) != Some(&b'(') {
        return None;
    }
    let end = matching_delimiter(expression, index, b'(', b')')?;
    let args = split_top_level_properties(&expression[index + 1..end]);
    if args.len() < 2 {
        return None;
    }
    let (target_model, target_end) = parse_static_identifier(args[0], 0)?;
    if skip_ascii_whitespace(args[0], target_end) != args[0].len() {
        return None;
    }
    let (local, foreign) = parse_relation_options(args[1])?;
    Some(json!({
        "name": name,
        "kind": kind,
        "targetModel": target_model,
        "local": local,
        "foreign": foreign,
    }))
}

pub(crate) fn relation_object_source(callback_source: &str) -> Option<&str> {
    let arrow = callback_source.find("=>")?;
    let after_arrow = callback_source[arrow + 2..].trim_start();
    if !after_arrow.starts_with('(') {
        return None;
    }
    let brace_relative = after_arrow.find('{')?;
    let brace = arrow + 2 + callback_source[arrow + 2..].find('{')?;
    if !after_arrow[..brace_relative]
        .trim()
        .chars()
        .all(|ch| ch == '(')
    {
        return None;
    }
    let end = matching_delimiter(callback_source, brace, b'{', b'}')?;
    Some(&callback_source[brace + 1..end])
}

fn schema_response_native_fallback_reason(schema: &Value) -> Option<&'static str> {
    let Some(kind) = schema.get("kind").and_then(Value::as_str) else {
        return Some("schema-kind-missing");
    };

    match kind {
        "object" => {
            let Some(properties_value) = schema.get("properties") else {
                return Some("object-properties-missing");
            };
            let Some(properties) = properties_value.as_object() else {
                return Some("object-properties-invalid");
            };
            for property in properties.values() {
                if let Some(reason) = schema_response_native_fallback_reason(property) {
                    return Some(reason);
                }
            }
            None
        }
        "array" => {
            let Some(items) = schema.get("items") else {
                return Some("array-items-missing");
            };
            schema_response_native_fallback_reason(items)
        }
        "string" | "number" | "int" | "boolean" | "null" => None,
        "literal" => Some("literal-schema-unsupported"),
        "literalUnion" => Some("literal-union-schema-unsupported"),
        "ref" => Some("schema-reference-unresolved"),
        _ => Some("schema-kind-unsupported"),
    }
}

fn route_json_response_plan(
    response: Option<&ResponseMetadata>,
    responses: &[ResponseMetadata],
    resolved_schemas: &BTreeMap<String, Value>,
) -> Value {
    let json_responses = if responses.is_empty() {
        response
            .filter(|candidate| candidate.kind == "json")
            .into_iter()
            .collect::<Vec<_>>()
    } else {
        responses
            .iter()
            .filter(|candidate| candidate.kind == "json")
            .collect::<Vec<_>>()
    };
    if json_responses.is_empty() {
        return json!({
            "mode": "none",
            "writer": "none"
        });
    }
    if json_responses.len() == 1 && json_responses[0].native_body.is_some() {
        return json!({
            "mode": "native-static",
            "writer": "preencoded",
            "contentType": "application/json"
        });
    }
    if json_responses
        .iter()
        .any(|candidate| candidate.native_body.is_some())
    {
        return json!({
            "mode": "fallback",
            "writer": "none",
            "fallbackReason": "multiple-json-response-shapes"
        });
    }
    let schema_names = json_responses
        .iter()
        .filter_map(|candidate| candidate.body_schema.as_deref())
        .collect::<BTreeSet<_>>();
    if schema_names.len() > 1 {
        return json!({
            "mode": "fallback",
            "writer": "none",
            "fallbackReason": "multiple-json-response-schemas"
        });
    }
    if let Some(schema) = schema_names.iter().next().copied() {
        if json_responses
            .iter()
            .any(|candidate| candidate.body_schema.as_deref() != Some(schema))
        {
            return json!({
                "mode": "fallback",
                "writer": "none",
                "fallbackReason": "mixed-json-response-schema-coverage"
            });
        }
        if let Some(definition) = resolved_schemas.get(schema) {
            if let Some(reason) = schema_response_native_fallback_reason(definition) {
                return json!({
                    "mode": "fallback",
                    "writer": "none",
                    "schema": schema,
                    "fallbackReason": format!("native-schema-response-writer-unsupported:{reason}")
                });
            }
            return json!({
                "mode": "native-schema",
                "writer": "bounded",
                "schema": schema,
                "contentType": "application/json"
            });
        }
        return json!({
            "mode": "fallback",
            "writer": "none",
            "schema": schema,
            "fallbackReason": "native-schema-response-writer-unsupported:schema-missing"
        });
    }

    json!({
        "mode": "generic",
        "writer": "none",
        "fallbackReason": "dynamic-handler-result"
    })
}

fn route_json_mode(value: &Value) -> &str {
    value.get("mode").and_then(Value::as_str).unwrap_or("none")
}

fn route_dispatch_json(
    app: &ExtractedApp,
    route_completeness_values: &[Completeness],
    route_artifact: Option<&RouteDispatchArtifactMetadata>,
    resolved_schemas: &BTreeMap<String, Value>,
) -> Value {
    let static_routes = app
        .routes
        .iter()
        .filter(|route| !route_pattern_has_params(&route.pattern))
        .count();
    let parameter_routes = app.routes.len().saturating_sub(static_routes);
    let segment_trie_nodes = route_dispatch_segment_trie_nodes(app);
    let parameter_candidate_buckets = app
        .routes
        .iter()
        .filter(|route| route_pattern_has_params(&route.pattern))
        .map(|route| {
            (
                route.method,
                route_pattern_leading_static_segment(&route.pattern)
                    .unwrap_or("")
                    .to_string(),
            )
        })
        .collect::<BTreeSet<_>>()
        .len();
    let partial_routes = route_completeness_values
        .iter()
        .filter(|route| route.status.as_str() != "complete")
        .count();
    let constraints = app
        .routes
        .iter()
        .flat_map(|route| route_pattern_constraint_names(&route.pattern))
        .collect::<BTreeSet<_>>()
        .into_iter()
        .collect::<Vec<_>>();
    let native_no_js_endpoints = app
        .routes
        .iter()
        .filter(|route| {
            let response_kind = route
                .handler
                .response
                .as_ref()
                .map(|response| response.kind.as_str());
            let native_body = route
                .handler
                .response
                .as_ref()
                .and_then(|response| response.native_body.as_deref());
            route_execution_kind(response_kind, native_body) != RouteExecutionKind::V8Handler
        })
        .count();
    let request_native_json_routes = app
        .routes
        .iter()
        .filter(|route| {
            route_json_mode(&route_json_request_plan(&route.handler.bindings)) == "native-schema"
        })
        .count();
    let request_generic_json_routes = app
        .routes
        .iter()
        .filter(|route| {
            route_json_mode(&route_json_request_plan(&route.handler.bindings)) == "generic"
        })
        .count();
    let request_fallback_json_routes = app
        .routes
        .iter()
        .filter(|route| {
            route_json_mode(&route_json_request_plan(&route.handler.bindings)) == "fallback"
        })
        .count();
    let response_native_json_routes = app
        .routes
        .iter()
        .filter(|route| {
            let plan = route_json_response_plan(
                route.handler.response.as_ref(),
                &route.handler.responses,
                resolved_schemas,
            );
            route_json_mode(&plan) == "native-static" || route_json_mode(&plan) == "native-schema"
        })
        .count();
    let response_generic_json_routes = app
        .routes
        .iter()
        .filter(|route| {
            route_json_mode(&route_json_response_plan(
                route.handler.response.as_ref(),
                &route.handler.responses,
                resolved_schemas,
            )) == "generic"
        })
        .count();
    let response_fallback_json_routes = app
        .routes
        .iter()
        .filter(|route| {
            route_json_mode(&route_json_response_plan(
                route.handler.response.as_ref(),
                &route.handler.responses,
                resolved_schemas,
            )) == "fallback"
        })
        .count();
    let artifact = route_artifact.map_or_else(
        || {
            json!({
                "kind": "none",
                "reason": "route dispatch artifact metadata was not supplied by this Plan emission call"
            })
        },
        |artifact| {
            json!({
                "kind": "slrt",
                "path": artifact.path,
                "hash": artifact.hash
            })
        },
    );

    json!({
        "version": 1,
        "mode": if route_artifact.is_some() { "native-compiled" } else { "native-compiled-in-memory" },
        "artifact": artifact,
        "routeCount": app.routes.len(),
        "endpointCount": app.routes.len(),
        "staticRoutes": static_routes,
        "parameterRoutes": parameter_routes,
        "catchAllRoutes": 0,
        "nativeNoJsEndpoints": native_no_js_endpoints,
        "urlGeneration": true,
        "urlWriters": app.routes.iter().filter(|route| route.name.is_some()).count(),
        "dispatchStats": {
            "exactStaticPaths": static_routes,
            "parameterCandidateBuckets": parameter_candidate_buckets,
            "segmentTrieNodes": segment_trie_nodes,
            "staticEdgeStrategies": [
                "open-addressed-exact-hash",
                "segment-trie",
                "first-static-segment-bucket-fallback"
            ],
            "constraints": constraints
        },
        "json": {
            "request": {
                "native": request_native_json_routes,
                "generic": request_generic_json_routes,
                "fallback": request_fallback_json_routes,
                "materialized": request_native_json_routes
            },
            "response": {
                "native": response_native_json_routes,
                "generic": response_generic_json_routes,
                "fallback": response_fallback_json_routes
            }
        },
        "fallback": {
            "classicAvailable": true,
            "dynamicRoutes": app.dynamic_routes.len(),
            "partialRoutes": partial_routes
        }
    })
}

#[derive(Debug, Default)]
struct SchemaReferenceResolution {
    partial_references: BTreeSet<SchemaReferenceFinding>,
}

#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord)]
struct SchemaReferenceFinding {
    schema: String,
    reference: Option<String>,
    reason: &'static str,
}

impl SchemaReferenceResolution {
    fn mark_partial(&mut self, schema_name: &str, reference: Option<&str>, reason: &'static str) {
        self.partial_references.insert(SchemaReferenceFinding {
            schema: schema_name.to_string(),
            reference: reference.map(ToString::to_string),
            reason,
        });
    }

    fn doctor_checks(&self) -> Vec<Value> {
        self.partial_references
            .iter()
            .map(|finding| {
                let reference = finding.reference.as_deref().unwrap_or("<missing>");
                json!({
                    "id": "schema.reference.partial",
                    "status": "warn",
                    "message": format!(
                        "Schema '{}' contains a '{}' reference that could not be fully resolved ({}); runtime-safe schema metadata was emitted as partial.",
                        finding.schema,
                        reference,
                        finding.reason
                    ),
                    "schema": finding.schema,
                    "reference": reference,
                    "reason": finding.reason
                })
            })
            .collect()
    }
}

fn schema_definition_map(app: &ExtractedApp) -> BTreeMap<String, Value> {
    app.schemas
        .iter()
        .map(|schema| (schema.name.clone(), schema.definition.clone()))
        .collect()
}

fn partial_schema_reference(name: Option<&str>, reason: &'static str) -> Value {
    let mut value = json!({
        "kind": "object",
        "properties": {},
        "partial": true,
        "partialReason": reason
    });
    if let Some(name) = name {
        value["reference"] = json!(name);
    }
    value
}

fn apply_schema_reference_overrides(reference: &Map<String, Value>, resolved: &mut Value) {
    let Value::Object(target) = resolved else {
        return;
    };
    for (key, value) in reference {
        if key == "kind" || key == "name" {
            continue;
        }
        target.insert(key.clone(), value.clone());
    }
}

fn resolve_schema_references(
    schema_name: &str,
    value: &Value,
    definitions: &BTreeMap<String, Value>,
    stack: &mut BTreeSet<String>,
    resolution: &mut SchemaReferenceResolution,
) -> Value {
    match value {
        Value::Object(object) => {
            if object.get("kind").and_then(Value::as_str) == Some("ref") {
                let reference = object.get("name").and_then(Value::as_str);
                let Some(reference_name) = reference else {
                    resolution.mark_partial(schema_name, None, "missing-name");
                    return partial_schema_reference(None, "missing-name");
                };
                if stack.contains(reference_name) {
                    resolution.mark_partial(schema_name, Some(reference_name), "cycle");
                    return partial_schema_reference(Some(reference_name), "cycle");
                }
                let Some(definition) = definitions.get(reference_name) else {
                    resolution.mark_partial(schema_name, Some(reference_name), "unresolved");
                    return partial_schema_reference(Some(reference_name), "unresolved");
                };
                stack.insert(reference_name.to_string());
                let mut resolved = resolve_schema_references(
                    schema_name,
                    definition,
                    definitions,
                    stack,
                    resolution,
                );
                stack.remove(reference_name);
                apply_schema_reference_overrides(object, &mut resolved);
                return resolved;
            }

            let resolved = object
                .iter()
                .map(|(key, child)| {
                    (
                        key.clone(),
                        resolve_schema_references(
                            schema_name,
                            child,
                            definitions,
                            stack,
                            resolution,
                        ),
                    )
                })
                .collect();
            Value::Object(resolved)
        }
        Value::Array(items) => Value::Array(
            items
                .iter()
                .map(|child| {
                    resolve_schema_references(schema_name, child, definitions, stack, resolution)
                })
                .collect(),
        ),
        _ => value.clone(),
    }
}

fn skip_ascii_ws(source: &str, mut index: usize) -> usize {
    while index < source.len() && source.as_bytes()[index].is_ascii_whitespace() {
        index += 1;
    }
    index
}

fn is_identifier_byte(byte: u8) -> bool {
    byte.is_ascii_alphanumeric() || byte == b'_' || byte == b'$'
}

fn parse_string_literal_at(source: &str, index: usize) -> Option<(String, usize)> {
    let quote = *source.as_bytes().get(index)?;
    if quote != b'\'' && quote != b'"' {
        return None;
    }
    let mut output = String::new();
    let mut cursor = index + 1;
    while cursor < source.len() {
        let byte = source.as_bytes()[cursor];
        if byte == quote {
            return Some((output, cursor + 1));
        }
        if byte == b'\\' {
            cursor += 1;
            if cursor >= source.len() {
                return None;
            }
        }
        output.push(source.as_bytes()[cursor] as char);
        cursor += 1;
    }
    None
}

fn skip_template_literal_at(source: &str, index: usize) -> Option<usize> {
    if source.as_bytes().get(index) != Some(&b'`') {
        return None;
    }
    let mut cursor = index + 1;
    while cursor < source.len() {
        let byte = source.as_bytes()[cursor];
        if byte == b'`' {
            return Some(cursor + 1);
        }
        if byte == b'\\' {
            cursor += 1;
        }
        cursor += 1;
    }
    None
}

fn skip_line_comment_at(source: &str, index: usize) -> Option<usize> {
    if !source.as_bytes().get(index..)?.starts_with(b"//") {
        return None;
    }
    let mut cursor = index + 2;
    while cursor < source.len() && source.as_bytes()[cursor] != b'\n' {
        cursor += 1;
    }
    Some(cursor)
}

fn skip_block_comment_at(source: &str, index: usize) -> Option<usize> {
    if !source.as_bytes().get(index..)?.starts_with(b"/*") {
        return None;
    }
    let mut cursor = index + 2;
    while cursor + 1 < source.len() {
        if source.as_bytes()[cursor] == b'*' && source.as_bytes()[cursor + 1] == b'/' {
            return Some(cursor + 2);
        }
        cursor += 1;
    }
    Some(source.len())
}

fn skip_non_code_at(source: &str, index: usize) -> Option<usize> {
    let byte = *source.as_bytes().get(index)?;
    if byte == b'\'' || byte == b'"' {
        return parse_string_literal_at(source, index).map(|(_, end)| end);
    }
    if byte == b'`' {
        return skip_template_literal_at(source, index);
    }
    skip_line_comment_at(source, index).or_else(|| skip_block_comment_at(source, index))
}

fn find_code_token(source: &str, token: &str, start: usize) -> Option<usize> {
    let token = token.as_bytes();
    let mut cursor = start;
    while cursor + token.len() <= source.len() {
        if let Some(next) = skip_non_code_at(source, cursor) {
            cursor = next;
            continue;
        }
        if source.as_bytes()[cursor..].starts_with(token)
            && token_has_code_boundaries(source, cursor, token)
        {
            return Some(cursor);
        }
        cursor += 1;
    }
    None
}

fn token_has_code_boundaries(source: &str, index: usize, token: &[u8]) -> bool {
    let starts_with_identifier = token.first().is_some_and(|byte| is_identifier_byte(*byte));
    let ends_with_identifier = token.last().is_some_and(|byte| is_identifier_byte(*byte));
    if starts_with_identifier {
        let before = index
            .checked_sub(1)
            .and_then(|idx| source.as_bytes().get(idx));
        if before.is_some_and(|byte| is_identifier_byte(*byte) || *byte == b'.') {
            return false;
        }
    }
    if ends_with_identifier {
        let after = source.as_bytes().get(index + token.len());
        if after.is_some_and(|byte| is_identifier_byte(*byte)) {
            return false;
        }
    }
    true
}

fn find_matching_delimiter(source: &str, open_index: usize, open: u8, close: u8) -> Option<usize> {
    let mut depth = 0usize;
    let mut cursor = open_index;
    while cursor < source.len() {
        if let Some(next) = skip_non_code_at(source, cursor) {
            cursor = next;
            continue;
        }
        let byte = source.as_bytes()[cursor];
        if byte == open {
            depth += 1;
        } else if byte == close {
            depth = depth.checked_sub(1)?;
            if depth == 0 {
                return Some(cursor);
            }
        }
        cursor += 1;
    }
    None
}

fn find_property_value(source: &str, property: &str) -> Option<usize> {
    let mut cursor = 0usize;
    let needle = property.as_bytes();
    while cursor + needle.len() <= source.len() {
        let property_index = find_code_token(source, property, cursor)?;
        let before = property_index
            .checked_sub(1)
            .and_then(|idx| source.as_bytes().get(idx));
        let after = source.as_bytes().get(property_index + needle.len());
        let identifier_before = before.is_some_and(|byte| is_identifier_byte(*byte));
        let identifier_after = after.is_some_and(|byte| is_identifier_byte(*byte));
        if !identifier_before && !identifier_after {
            let colon = skip_ascii_ws(source, property_index + needle.len());
            if source.as_bytes().get(colon) == Some(&b':') {
                return Some(skip_ascii_ws(source, colon + 1));
            }
        }
        cursor = property_index + needle.len();
    }
    None
}

fn parse_http_base_url(source: &str) -> (Option<String>, Option<String>) {
    let Some(value_index) = find_property_value(source, "baseUrl") else {
        return (None, None);
    };
    if let Some((literal, _)) = parse_string_literal_at(source, value_index) {
        return (Some(literal), None);
    }
    let config_call = "Config.required(";
    if source[value_index..].starts_with(config_call) {
        let string_index = skip_ascii_ws(source, value_index + config_call.len());
        if let Some((key, _)) = parse_string_literal_at(source, string_index) {
            return (None, Some(key));
        }
    }
    (None, None)
}

fn parse_http_endpoints(source: &str) -> Vec<Value> {
    let mut endpoints = Vec::new();
    for method in ["get", "post", "put", "patch", "delete"] {
        let mut cursor = 0usize;
        let needle = format!("Http.{method}(");
        while let Some(call_index) = find_code_token(source, &needle, cursor) {
            let path_index = skip_ascii_ws(source, call_index + needle.len());
            if let Some((path, end_index)) = parse_string_literal_at(source, path_index) {
                let chain_end = find_code_token(source, "Http.", end_index).unwrap_or(source.len());
                let chain = &source[end_index..chain_end.min(end_index + 1000)];
                let mut returns = Vec::new();
                let mut return_cursor = 0usize;
                while let Some(return_index) = find_code_token(chain, ".returns(", return_cursor) {
                    let status_index = skip_ascii_ws(chain, return_index + ".returns(".len());
                    let status_text = chain[status_index..]
                        .chars()
                        .take_while(|ch| ch.is_ascii_digit())
                        .collect::<String>();
                    if let Ok(status) = status_text.parse::<u16>() {
                        if (100..=599).contains(&status) {
                            returns.push(json!({ "status": status }));
                        }
                    }
                    return_cursor = status_index + status_text.len();
                }
                endpoints.push(json!({
                    "method": method.to_ascii_uppercase(),
                    "path": path,
                    "returns": returns
                }));
            }
            cursor = call_index + needle.len();
        }
    }
    endpoints
}

fn http_clients_from_sources(app: &ExtractedApp) -> Vec<Value> {
    let mut clients = Vec::new();
    for source_file in &app.source_files {
        let source = &source_file.source;
        for (call, kind) in [("Http.client(", "named"), ("Http.typedClient(", "typed")] {
            let mut cursor = 0usize;
            while let Some(call_index) = find_code_token(source, call, cursor) {
                let name_index = skip_ascii_ws(source, call_index + call.len());
                let Some((name, after_name)) = parse_string_literal_at(source, name_index) else {
                    clients.push(json!({
                        "target": "dynamic",
                        "kind": kind,
                        "source": source_file.name
                    }));
                    cursor = call_index + call.len();
                    continue;
                };
                let end_index =
                    find_matching_delimiter(source, call_index + call.len() - 1, b'(', b')')
                        .unwrap_or_else(|| source.len().saturating_sub(1));
                let call_source = &source[after_name..end_index];
                let (base_url, base_url_config_key) = parse_http_base_url(call_source);
                let mut client = json!({
                    "name": name,
                    "kind": kind,
                    "target": if base_url.is_some() || base_url_config_key.is_some() { "static" } else { "dynamic" },
                    "source": source_file.name
                });
                if let Some(base_url) = base_url {
                    client["baseUrl"] = json!(base_url);
                }
                if let Some(config_key) = base_url_config_key {
                    client["baseUrlConfigKey"] = json!(config_key);
                }
                let endpoints = parse_http_endpoints(call_source);
                if !endpoints.is_empty() {
                    client["endpoints"] = json!(endpoints);
                }
                clients.push(client);
                cursor = end_index.saturating_add(1);
            }
        }
    }
    clients
}

#[cfg(test)]
pub(crate) fn emit_plan(
    app: &ExtractedApp,
    bundle_hash: &str,
    source_map_hash: &str,
) -> Result<String, Diagnostic> {
    let route_artifact = crate::route_artifact::emit_route_artifact(app)?;
    let route_artifact_metadata =
        route_artifact
            .as_ref()
            .map(|bytes| RouteDispatchArtifactMetadata {
                path: crate::route_artifact::ROUTE_ARTIFACT_PATH.to_string(),
                hash: crate::hash::sha256_bytes_hex(bytes),
            });
    emit_plan_with_route_artifact(
        app,
        bundle_hash,
        source_map_hash,
        route_artifact_metadata.as_ref(),
    )
}

pub(crate) fn emit_plan_with_route_artifact(
    app: &ExtractedApp,
    bundle_hash: &str,
    source_map_hash: &str,
    route_artifact: Option<&RouteDispatchArtifactMetadata>,
) -> Result<String, Diagnostic> {
    if app.kind == ProjectKind::Program && !app.routes.is_empty() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_PROGRAM_PLAN_SHAPE",
            "program plans must not contain web routes or handlers",
        ));
    }

    let has_async_handlers = app.routes.iter().any(|route| route.handler.is_async);
    let emits_app_metadata = !app.schemas.is_empty()
        || !app.config_reads.is_empty()
        || app.uses_health
        || !app.auth.schemes.is_empty()
        || !app.auth.policies.is_empty();
    let route_completeness_values = app
        .routes
        .iter()
        .map(|route| {
            route_completeness(&RouteCompletenessInput {
                has_response_metadata: route.handler.response.is_some(),
                partial_response_metadata: route
                    .handler
                    .response
                    .as_ref()
                    .is_some_and(|response| response.partial),
                body_json_without_schema: route
                    .handler
                    .bindings
                    .iter()
                    .any(|binding| binding.kind == "body.json" && binding.schema.is_none()),
                missing_provider_registration: route.handler.effects.iter().any(|effect| {
                    !app.capabilities.iter().any(|capability| {
                        capability.token == effect.provider
                            && capability.capability_kind == effect.capability_kind
                            && capability.provider == effect.provider_kind
                    })
                }),
                runtime_only: route.handler.runtime_deferred,
                schema_metadata_conflict: route.handler.schema_metadata_conflict,
            })
        })
        .collect::<Vec<_>>();
    let mut all_route_completeness_values = route_completeness_values.clone();
    all_route_completeness_values.extend(app.dynamic_routes.iter().map(|route| {
        Completeness::dynamic(vec![CompletenessReason::new("dynamic-route", route.reason)])
    }));
    let app_completeness = if app.kind == ProjectKind::Program {
        Completeness::opaque(vec![CompletenessReason::new(
            "program-mode",
            "program mode does not require static web route metadata",
        )])
    } else {
        plan_completeness(&all_route_completeness_values)
    };
    let emits_binding_metadata = |index: usize, route: &crate::graph::Route| {
        !route.handler.bindings.is_empty()
            || route_completeness_values[index].status.as_str() == "complete"
    };
    let emits_metadata = emits_app_metadata
        || app.routes.iter().enumerate().any(|(index, route)| {
            emits_binding_metadata(index, route)
                || route.handler.response.is_some()
                || !route.handler.responses.is_empty()
                || route.handler.runtime_deferred
                || !route.handler.effects.is_empty()
                || route.health.is_some()
                || !route.middleware.is_empty()
                || route.auth.is_some()
                || route.cors.is_some()
        });
    let handlers = app
        .routes
        .iter()
        .enumerate()
        .map(|(index, route)| {
            let id = index + 1;
            let (line, column) = line_column(&route.handler.source_text, route.handler.span.start);
            let mut handler = json!({
                "async": route.handler.is_async,
                "id": id,
                "exportName": format!("__sloppy_handler_{id}"),
                "displayName": route.name.clone().unwrap_or_else(|| format!("{} {}", route.method, route.pattern)),
                "source": {
                    "path": route.handler.source_name,
                    "line": line,
                    "column": column
                }
            });
            if route.handler.runtime_deferred {
                handler["runtimeDispatch"] = json!("deferred");
            }
            handler
        })
        .collect::<Vec<_>>();

    let schema_definitions = schema_definition_map(app);
    let mut schema_reference_resolution = SchemaReferenceResolution::default();
    let resolved_schema_definitions = app
        .schemas
        .iter()
        .map(|schema| {
            let mut stack = BTreeSet::from([schema.name.clone()]);
            let definition = resolve_schema_references(
                &schema.name,
                &schema.definition,
                &schema_definitions,
                &mut stack,
                &mut schema_reference_resolution,
            );
            (schema.name.clone(), definition)
        })
        .collect::<BTreeMap<_, _>>();

    let routes = app
        .routes
        .iter()
        .enumerate()
        .map(|(index, route)| {
            let id = index + 1;
            let (line, column) = line_column(&route.source, route.span.start);
            let completeness = &route_completeness_values[index];
            let mut route_json = json!({
                "method": route.method,
                "pattern": route.pattern,
                "handlerId": id,
                "name": route.name,
                "source": {
                    "path": route.source_name,
                    "line": line,
                    "column": column
                }
            });
            if route.kind != "http" {
                route_json["kind"] = json!(route.kind);
            }
            if let Some(module) = &route.module {
                route_json["module"] = json!(module);
            }
            if !route.tags.is_empty() {
                route_json["tags"] = json!(route.tags);
            }
            if let Some(summary) = &route.summary {
                route_json["summary"] = json!(summary);
            }
            if let Some(description) = &route.description {
                route_json["description"] = json!(description);
            }
            if let Some(deprecated) = &route.deprecated {
                route_json["deprecated"] = json!(deprecated != "false");
                if deprecated != "true" && deprecated != "false" {
                    route_json["deprecatedReason"] = json!(deprecated);
                }
            }
            if !route.consumes.is_empty() {
                route_json["consumes"] = json!(route.consumes);
            }
            if !route.produces.is_empty() {
                route_json["produces"] = json!(route.produces);
            }
            if !route.headers.is_empty() {
                route_json["headers"] = json!(route
                    .headers
                    .iter()
                    .map(|header| {
                        json!({
                            "name": header.name,
                            "schema": header.schema,
                            "required": header.required,
                            "description": header.description
                        })
                    })
                    .collect::<Vec<_>>());
            }
            if let Some(schema) = &route.query_schema {
                route_json["querySchema"] = json!(schema);
            }
            if let Some(schema) = &route.params_schema {
                route_json["paramsSchema"] = json!(schema);
            }
            if let Some(override_value) = &route.openapi_override {
                route_json["openapi"] = override_value.clone();
            }
            if let Some(docs) = &route.docs {
                route_json["docsInternal"] = json!(true);
                route_json["docs"] = json!({
                    "kind": docs.kind,
                    "strict": docs.strict
                });
            }
            if let Some(health) = &route.health {
                route_json["health"] = json!({
                    "kind": health.kind,
                    "checks": health.checks
                });
            }
            if !route.middleware.is_empty() {
                route_json["middleware"] = json!(route
                    .middleware
                    .iter()
                    .map(|middleware| {
                        json!({
                            "kind": middleware.kind,
                            "sequence": middleware.sequence,
                            "source": source_location_json(
                                &middleware.source_name,
                                &middleware.source_text,
                                middleware.span
                            )
                        })
                    })
                    .collect::<Vec<_>>());
            }
            if let Some(auth) = &route.auth {
                route_json["auth"] = json!({
                    "required": auth.required,
                    "allowAnonymous": auth.allow_anonymous,
                    "schemes": auth.schemes,
                    "scopes": auth.scopes,
                    "roles": auth.roles,
                    "claims": auth.claims,
                    "policy": auth.policy
                });
            }
            if let Some(cors) = &route.cors {
                route_json["cors"] = json!({
                    "origins": cors.origins,
                    "methods": cors.methods,
                    "headers": cors.headers,
                    "exposedHeaders": cors.exposed_headers,
                    "credentials": cors.credentials,
                    "maxAgeSeconds": cors.max_age_seconds,
                    "preflight": route.cors_preflight
                });
            }
            if let Some(framework_path) = &route.framework_path {
                route_json["frameworkPath"] = json!(framework_path);
            }
            if route.handler.runtime_deferred {
                let route_params = route_parameter_names(
                    route.framework_path.as_deref().unwrap_or(&route.pattern),
                )
                .into_iter()
                .map(|name| json!({ "name": name }))
                .collect::<Vec<_>>();
                if !route_params.is_empty() {
                    route_json["routeParams"] = json!(route_params);
                }
            }
            let emits_route_metadata = emits_app_metadata
                || emits_binding_metadata(index, route)
                || route.handler.response.is_some()
                || !route.handler.responses.is_empty()
                || !route.handler.effects.is_empty()
                || route.health.is_some()
                || !route.middleware.is_empty()
                || route.auth.is_some()
                || route.cors.is_some()
                || route.summary.is_some()
                || route.description.is_some()
                || route.deprecated.is_some()
                || !route.consumes.is_empty()
                || !route.produces.is_empty()
                || !route.headers.is_empty()
                || route.query_schema.is_some()
                || route.params_schema.is_some()
                || route.openapi_override.is_some()
                || route.docs.is_some();
            if emits_binding_metadata(index, route) {
                route_json["bindings"] = json!(route
                    .handler
                    .bindings
                    .iter()
                    .map(|binding| {
                        let mut binding_json = json!({
                            "kind": binding.kind,
                            "name": binding.name,
                            "schema": binding.schema
                        });
                        if let Some(parameter) = &binding.parameter {
                            binding_json["parameter"] = json!(parameter);
                        }
                        if let Some(type_name) = &binding.type_name {
                            binding_json["type"] = json!(type_name);
                        }
                        if let Some(wrapper) = &binding.wrapper {
                            binding_json["wrapper"] = json!(wrapper);
                        }
                        if let Some(kind) = &binding.injection_kind {
                            binding_json["injectionKind"] = json!(kind);
                        }
                        if let Some(provider_kind) = &binding.provider_kind {
                            binding_json["providerKind"] = json!(provider_kind);
                        }
                        if let Some(capability) = &binding.capability {
                            binding_json["capability"] = json!(capability);
                        }
                        if let Some(semantic) = &binding.semantic {
                            binding_json["semantic"] = json!(semantic);
                        }
                        if binding.redacted {
                            binding_json["secret"] = json!(true);
                            binding_json["redaction"] = json!("secret");
                        }
                        if let Some(span) = binding.span {
                            binding_json["source"] = source_location_json(
                                binding
                                    .source_name
                                    .as_deref()
                                    .unwrap_or(&route.handler.source_name),
                                binding
                                    .source_text
                                    .as_deref()
                                    .unwrap_or(&route.handler.source_text),
                                span,
                            );
                        }
                        binding_json
                    })
                    .collect::<Vec<_>>());
                let injections = route
                    .handler
                    .bindings
                    .iter()
                    .filter(|binding| binding.injection_kind.is_some())
                    .map(|binding| {
                        json!({
                            "kind": binding.injection_kind,
                            "providerKind": binding.provider_kind,
                            "name": binding.name,
                            "parameter": binding.parameter,
                            "capability": binding.capability
                        })
                    })
                    .collect::<Vec<_>>();
                if !injections.is_empty() {
                    route_json["injections"] = json!(injections);
                }
            }
            if emits_route_metadata {
                if let Some(response) = &route.handler.response {
                    let mut response_json = json!({
                        "helper": response.helper,
                        "status": response.status,
                        "kind": response.kind
                    });
                    if response.body_schema.is_some() {
                        response_json["bodySchema"] = json!(response.body_schema);
                    }
                    if response.partial {
                        response_json["partial"] = json!(true);
                    }
                    if let Some(native_body) = &response.native_body {
                        response_json["nativeBody"] = json!(native_body);
                        route_json["nativeResponse"] = json!({
                            "kind": response.kind,
                            "status": response.status,
                            "body": native_body,
                            "contentType": if response.kind == "json" {
                                "application/json"
                            } else {
                                "text/plain; charset=utf-8"
                            }
                        });
                    }
                    route_json["response"] = response_json;
                }
                route_json["jsonRequest"] = route_json_request_plan(&route.handler.bindings);
                route_json["jsonResponse"] = route_json_response_plan(
                    route.handler.response.as_ref(),
                    &route.handler.responses,
                    &resolved_schema_definitions,
                );
                if route.handler.responses.len() > 1 || route.handler.runtime_deferred {
                    route_json["responses"] = json!(route
                        .handler
                        .responses
                        .iter()
                        .map(|response| {
                            let mut value = json!({
                                "helper": response.helper,
                                "status": response.status,
                                "kind": response.kind,
                                "bodySchema": response.body_schema,
                                "nativeBody": response.native_body,
                                "partial": response.partial
                            });
                            if let (Some(source_name), Some(source_text), Some(span)) =
                                (&response.source_name, &response.source_text, response.span)
                            {
                                value["source"] =
                                    source_location_json(source_name, source_text, span);
                            }
                            value
                        })
                        .collect::<Vec<_>>());
                }
            }
            if !route.handler.effects.is_empty() {
                route_json["effects"] = json!(route
                    .handler
                    .effects
                    .iter()
                    .map(|effect| {
                        json!({
                            "provider": effect.provider,
                            "capabilityKind": effect.capability_kind,
                            "providerKind": effect.provider_kind,
                            "access": effect.access,
                            "operation": effect.operation,
                            "reason": effect.reason,
                            "source": source_location_json(
                                &effect.source_name,
                                &effect.source_text,
                                effect.span
                            )
                        })
                    })
                    .collect::<Vec<_>>());
            }
            let response_kind = route.handler.response.as_ref().map(|response| response.kind.as_str());
            let native_body = route
                .handler
                .response
                .as_ref()
                .and_then(|response| response.native_body.as_deref());
            let execution_kind = route_execution_kind(response_kind, native_body);
            route_json["dispatch"] = json!({
                "endpointId": id,
                "mode": if route_artifact.is_some() { "native-compiled" } else { "native-compiled-in-memory" },
                "strategy": if route_pattern_has_params(&route.pattern) {
                    "segment-trie"
                } else {
                    "exact-static-hash"
                },
                "specializable": true,
                "executionKind": execution_kind.as_plan_str()
            });
            route_json["completeness"] = completeness_json(completeness);
            route_json
        })
        .collect::<Vec<_>>();

    let dynamic_routes = app
        .dynamic_routes
        .iter()
        .map(|route| {
            let (line, column) = line_column(&route.source, route.span.start);
            json!({
                "kind": "dynamicRoute",
                "method": {
                    "known": route.method.is_some(),
                    "value": route.method
                },
                "pattern": {
                    "known": route.pattern.is_some(),
                    "value": route.pattern,
                    "reason": route.pattern_reason
                },
                "handler": {
                    "known": route.handler_known
                },
                "source": {
                    "path": route.source_name,
                    "line": line,
                    "column": column
                },
                "metadata": {
                    "completeness": "dynamic",
                    "unknown": if route.pattern.is_some() {
                        json!(["response"])
                    } else {
                        json!(["pattern", "response"])
                    },
                    "reason": route.reason
                }
            })
        })
        .collect::<Vec<_>>();

    let dynamic_findings = app
        .dynamic_routes
        .iter()
        .map(|route| {
            let (line, column) = line_column(&route.source, route.span.start);
            json!({
                "code": "SLOPPYC_W_DYNAMIC_ROUTE",
                "severity": "warning",
                "message": route.reason,
                "source": {
                    "path": route.source_name,
                    "line": line,
                    "column": column
                },
                "hint": "The app can build, but native dispatch and OpenAPI only use statically known route metadata."
            })
        })
        .collect::<Vec<_>>();

    let modules = app
        .modules
        .iter()
        .map(|module| {
            json!({
                "name": module.name,
                "source": {
                    "path": module.source_name
                }
            })
        })
        .collect::<Vec<_>>();

    let source_files = app
        .source_files
        .iter()
        .map(|source_file| {
            json!({
                "path": source_file.name,
                "hash": sha256_hex(&source_file.source)
            })
        })
        .collect::<Vec<_>>();

    let data_provider_capabilities = app
        .capabilities
        .iter()
        .filter(|capability| capability.capability_kind == "database")
        .collect::<Vec<_>>();

    let data_providers = data_provider_capabilities
        .iter()
        .map(|capability| {
            let mut provider = json!({
                "token": capability.token,
                "capabilityKind": capability.capability_kind,
                "providerKind": capability.provider,
                "provider": capability.provider,
                "capability": capability.token,
                "service": null,
                "source": source_location_json(
                    &capability.source_name,
                    &capability.source,
                    capability.span
                )
            });
            if let Some(database) = &capability.database {
                provider["database"] = json!(database);
            }
            if let Some(config_key) = &capability.config_key {
                provider["configKey"] = json!(config_key);
            }
            provider
        })
        .collect::<Vec<_>>();

    let capabilities = app
        .capabilities
        .iter()
        .map(|capability| {
            let mut value = json!({
                "token": capability.token,
                "kind": capability.capability_kind,
                "access": capability.access,
                "source": source_location_json(
                    &capability.source_name,
                    &capability.source,
                    capability.span
                )
            });
            if capability.capability_kind == "database" {
                value["provider"] = json!(capability.token);
            }
            if let Some(config_key) = &capability.config_key {
                value["configKey"] = json!(config_key);
            }
            value
        })
        .collect::<Vec<_>>();

    let configuration = app.configuration.as_ref().map(|configuration| {
        let keys = configuration
            .keys
            .iter()
            .map(|key| {
                json!({
                    "key": key.key,
                    "source": key.source,
                    "value": key.value,
                    "sensitive": key.sensitive
                })
            })
            .collect::<Vec<_>>();
        let providers = configuration
            .providers
            .iter()
            .map(|provider| {
                json!({
                    "provider": provider.provider,
                    "name": provider.name,
                    "prefix": provider.prefix,
                    "source": provider.source
                })
            })
            .collect::<Vec<_>>();
        let requirements = configuration
            .requirements
            .iter()
            .map(|requirement| {
                let mut value = json!({
                    "key": requirement.key,
                    "type": requirement.value_type,
                    "required": requirement.required,
                    "status": requirement.status,
                    "source": requirement.source,
                    "requiredBy": requirement.required_by,
                    "redaction": if requirement.sensitive { "secret" } else { "none" },
                    "secret": requirement.sensitive
                });
                if let Some(default_value) = &requirement.default_value {
                    value["default"] = if requirement.sensitive {
                        json!("<redacted>")
                    } else {
                        default_value.clone()
                    };
                }
                value
            })
            .collect::<Vec<_>>();
        let package_required = configuration
            .package_manifest
            .required
            .iter()
            .map(package_manifest_entry_json)
            .collect::<Vec<_>>();
        let package_optional = configuration
            .package_manifest
            .optional
            .iter()
            .map(package_manifest_entry_json)
            .collect::<Vec<_>>();
        let mut configuration_json = json!({
            "environment": configuration.environment,
            "keys": keys,
            "providers": providers
        });
        if !requirements.is_empty() {
            configuration_json["requirements"] = json!(requirements);
        }
        if !package_required.is_empty() || !package_optional.is_empty() {
            configuration_json["packageManifest"] = json!({
                "required": package_required,
                "optional": package_optional
            });
        }
        configuration_json
    });

    let schemas = app
        .schemas
        .iter()
        .map(|schema| {
            let (line, column) = line_column(&schema.source, schema.span.start);
            let definition = resolved_schema_definitions
                .get(&schema.name)
                .cloned()
                .unwrap_or_else(|| schema.definition.clone());
            json!({
                "name": schema.name,
                "definition": definition,
                "source": {
                    "path": schema.source_name,
                    "line": line,
                    "column": column
                }
            })
        })
        .collect::<Vec<_>>();

    let ffi_libraries = app
        .ffi
        .iter()
        .map(|library| {
            json!({
                "name": library.name,
                "convention": library.convention,
                "source": source_location_json(&library.source_name, &library.source, library.span),
                "functions": library
                    .functions
                    .iter()
                    .map(|function| {
                        json!({
                            "id": function.id,
                            "name": function.name,
                            "symbol": function.symbol,
                            "convention": function.convention,
                            "return": function.return_type,
                            "parameters": function.parameters,
                            "marshaling": {
                                "arguments": ffi_argument_marshaling(&function.parameters),
                                "return": "direct"
                            },
                            "safety": "unsafe",
                            "source": source_location_json(
                                &function.source_name,
                                &function.source,
                                function.span
                            )
                        })
                    })
                    .collect::<Vec<_>>()
            })
        })
        .collect::<Vec<_>>();

    let ffi_structs = app
        .ffi_structs
        .iter()
        .map(|layout| {
            let mut value = json!({
                "name": layout.name,
                "layout": layout.layout,
                "fields": layout
                    .fields
                    .iter()
                    .map(|field| {
                        json!({
                            "name": field.name,
                            "type": field.type_name
                        })
                    })
                    .collect::<Vec<_>>(),
                "source": source_location_json(&layout.source_name, &layout.source, layout.span)
            });
            if let Some(pack) = layout.pack {
                value["pack"] = json!(pack);
            }
            value
        })
        .collect::<Vec<_>>();

    let config_reads = app
        .config_reads
        .iter()
        .map(|read| {
            let (line, column) = line_column(&read.source, read.span.start);
            json!({
                "key": read.key,
                "type": read.value_type,
                "hasDefault": read.has_default,
                "required": read.required,
                "secret": read.sensitive,
                "source": {
                    "path": read.source_name,
                    "line": line,
                    "column": column
                }
            })
        })
        .collect::<Vec<_>>();

    let auth_schemes = app
        .auth
        .schemes
        .iter()
        .map(|scheme| match scheme {
            AuthSchemeMetadata::JwtBearer {
                name,
                issuer,
                audience,
                clock_skew_seconds,
                secret_config_key,
            } => json!({
                "kind": "jwtBearer",
                "name": name,
                "scheme": "bearer",
                "bearerFormat": "JWT",
                "algorithm": "HS256",
                "issuer": issuer,
                "audience": audience,
                "clockSkewSeconds": clock_skew_seconds,
                "secretConfigKey": secret_config_key,
                "secret": "<redacted>"
            }),
            AuthSchemeMetadata::ApiKey {
                name,
                header,
                config_key,
            } => json!({
                "kind": "apiKey",
                "name": name,
                "in": "header",
                "header": header,
                "configKey": config_key,
                "secret": "<redacted>"
            }),
            AuthSchemeMetadata::CookieSession {
                name,
                cookie,
                secure,
                http_only,
                same_site,
                path,
                max_age_seconds,
                store,
                idle_timeout_ms,
                absolute_timeout_ms,
                rotation,
                secret_config_key,
            } => json!({
                "kind": "cookieSession",
                "name": name,
                "in": "cookie",
                "cookie": cookie,
                "secure": secure,
                "httpOnly": http_only,
                "sameSite": same_site,
                "path": path,
                "maxAgeSeconds": max_age_seconds,
                "store": store,
                "idleTimeoutMs": idle_timeout_ms,
                "absoluteTimeoutMs": absolute_timeout_ms,
                "rotation": rotation,
                "configKey": secret_config_key,
                "secret": "<redacted>"
            }),
        })
        .collect::<Vec<_>>();
    let auth_policies = app
        .auth
        .policies
        .iter()
        .map(|policy| policy.name.clone())
        .collect::<Vec<_>>();

    let dependency_graph = if app.dependency_graph.has_entries() {
        Some((
            dependency_graph_json(&app.dependency_graph),
            emit_dependency_graph(&app.dependency_graph)?,
        ))
    } else {
        None
    };

    let mut value = json!({
        "schemaVersion": 1,
        "kind": app.kind.as_str(),
        "compilerVersion": COMPILER_VERSION,
        "runtimeMinimumVersion": RUNTIME_MINIMUM_VERSION,
        "stdlibVersion": STDLIB_VERSION,
        "target": {
            "platform": "windows-x64",
            "engine": "v8"
        },
        "bundle": {
            "path": "app.js",
            "id": "sloppyc-app-js",
            "hash": bundle_hash
        },
        "sourceMap": {
            "path": "app.js.map",
            "id": "sloppyc-app-js-map",
            "hash": source_map_hash
        },
        "handlers": handlers,
        "routes": routes,
        "modules": modules,
        "sourceFiles": source_files,
        "dataProviders": data_providers,
        "capabilities": capabilities,
        "completeness": completeness_json(&app_completeness),
        "strongPlan": {
            "version": 1,
            "profile": "compiler-30-strong-plan",
            "compatibility": {
                "schemaVersion": 1,
                "unknownOptionalFields": "ignored",
                "unknownRequiredFeatures": "rejected"
            },
            "evidence": {
                "sourceGraph": true,
                "routes": true,
                "providers": !data_providers.is_empty(),
                "bindings": app.routes.iter().any(|route| !route.handler.bindings.is_empty()),
            "effects": app.routes.iter().any(|route| !route.handler.effects.is_empty()),
            "capabilities": !app.capabilities.is_empty(),
            "ffi": !ffi_libraries.is_empty() || !ffi_structs.is_empty()
        }
    },
        "features": {
            "asyncHandlers": has_async_handlers,
            "dataProviders": !data_providers.is_empty(),
            "capabilities": !app.capabilities.is_empty(),
            "strongPlanMetadata": true,
            "sourceMaps": true
        }
    });
    if !dynamic_routes.is_empty() {
        value["dynamicRoutes"] = json!(dynamic_routes);
    }
    if !dynamic_findings.is_empty() {
        value["findings"] = json!(dynamic_findings);
    }
    if app.kind == ProjectKind::Web {
        value["routeDispatch"] = route_dispatch_json(
            app,
            &route_completeness_values,
            route_artifact,
            &resolved_schema_definitions,
        );
        value["features"]["nativeEndpointDispatch"] = json!(true);
        value["features"]["nativeJsonDispatch"] = json!(true);
        value["strongPlan"]["evidence"]["routeDispatch"] = json!(true);
        value["strongPlan"]["evidence"]["nativeJsonDispatch"] = json!(true);
    }
    if app.kind == ProjectKind::Web && !app.dynamic_routes.is_empty() {
        let complete_count = route_completeness_values
            .iter()
            .filter(|route| route.status.as_str() == "complete")
            .count();
        let partial_count = route_completeness_values
            .iter()
            .filter(|route| route.status.as_str() == "partial")
            .count();
        value["metadata"] = json!({
            "completeness": completeness_json(&app_completeness),
            "routes": {
                "complete": complete_count,
                "partial": partial_count,
                "dynamic": app.dynamic_routes.len()
            }
        });
    }
    if app.kind == ProjectKind::Program {
        value["metadata"] = json!({
            "completeness": completeness_json(&app_completeness),
            "program": {
                "entry": app.program_entry
            }
        });
        value["strongPlan"]["evidence"]["routes"] = json!(false);
        value["strongPlan"]["evidence"]["program"] = json!(true);
        value["features"]["program"] = json!(true);
    }
    if let Some((graph_json, graph_text)) = &dependency_graph {
        value["dependencyGraph"] = graph_json.clone();
        value["dependencyGraphArtifact"] = json!({
            "path": "deps.graph.json",
            "id": "sloppyc-deps-graph",
            "hash": sha256_hex(graph_text)
        });
        value["features"]["dependencyGraph"] = json!(true);
        value["strongPlan"]["evidence"]["dependencyGraph"] = json!(true);
    }

    let mut required_features = Vec::new();
    let mut doctor_checks = Vec::new();
    doctor_checks.extend(schema_reference_resolution.doctor_checks());
    let health_endpoints = app
        .routes
        .iter()
        .filter_map(|route| {
            route.health.as_ref().map(|health| {
                json!({
                    "method": route.method,
                    "path": route.pattern,
                    "name": route.name,
                    "kind": health.kind,
                    "checks": health.checks
                })
            })
        })
        .collect::<Vec<_>>();
    let management_endpoints = app
        .routes
        .iter()
        .filter(|route| {
            route
                .name
                .as_deref()
                .is_some_and(|name| name.starts_with("Management."))
        })
        .map(|route| {
            json!({
                "method": route.method,
                "path": route.pattern,
                "name": route.name
            })
        })
        .collect::<Vec<_>>();
    if !health_endpoints.is_empty() || !management_endpoints.is_empty() {
        value["ops"] = json!({
            "health": {
                "enabled": !health_endpoints.is_empty(),
                "endpoints": health_endpoints
            },
            "management": {
                "enabled": !management_endpoints.is_empty(),
                "endpoints": management_endpoints,
                "requiresProtection": !management_endpoints.is_empty()
            },
            "metrics": {
                "prometheus": app.routes.iter().any(|route| route.name.as_deref() == Some("Management.Metrics")),
                "json": app.routes.iter().any(|route| route.name.as_deref() == Some("Management.MetricsJson"))
            }
        });
        value["strongPlan"]["evidence"]["ops"] = json!(true);
        value["features"]["ops"] = json!(true);
    }
    if app.routes.iter().any(|route| {
        route
            .name
            .as_deref()
            .is_some_and(|name| name.starts_with("Management."))
    }) {
        value["features"]["management"] = json!(true);
        doctor_checks.push(json!({
            "id": "ops.management.protection",
            "status": "warn",
            "message": "management endpoints are exposed; protect detailed health, metrics, info, and runtime routes before exposing them outside a trusted network"
        }));
    }
    if app
        .routes
        .iter()
        .any(|route| route.name.as_deref() == Some("Management.Metrics"))
    {
        value["features"]["metrics"] = json!(true);
        doctor_checks.push(json!({
            "id": "ops.metrics.prometheus",
            "status": "info",
            "message": "Prometheus metrics endpoint is Plan-visible and package-safe"
        }));
    }
    if app.kind == ProjectKind::Program {
        doctor_checks.push(json!({
            "id": "program.metadata",
            "status": "info",
            "message": "program Plan has no route metadata by design"
        }));
    }
    if app.uses_time_runtime {
        required_features.push("stdlib.time".to_string());
        value["strongPlan"]["evidence"]["time"] = json!(true);
        value["features"]["time"] = json!(true);
    }
    if app.uses_fs_runtime {
        required_features.push("stdlib.fs".to_string());
        value["strongPlan"]["evidence"]["filesystem"] = json!(true);
        value["features"]["fileSystem"] = json!(true);
    }
    if app.uses_crypto_runtime {
        required_features.push("stdlib.crypto".to_string());
        value["strongPlan"]["evidence"]["crypto"] = json!(true);
        value["features"]["crypto"] = json!(true);
    }
    if app.uses_codec_runtime {
        required_features.push("stdlib.codec".to_string());
        value["strongPlan"]["evidence"]["codec"] = json!(true);
        value["features"]["codec"] = json!(true);
    }
    if app.uses_crypto_runtime && app.noncrypto_hash_security_context_visible {
        value["strongPlan"]["evidence"]["nonCryptoHashSecurityContext"] = json!(true);
        doctor_checks.push(json!({
            "id": "stdlib.crypto.noncrypto_hash.security_context",
            "status": "warn",
            "message": "NonCryptoHash.xxHash64 is visible in a security-looking context; use Password, Hash, or Hmac for security or attacker-resistance"
        }));
    }
    if app.uses_codec_runtime && app.checksum_security_context_visible {
        value["strongPlan"]["evidence"]["checksumSecurityContext"] = json!(true);
        doctor_checks.push(json!({
            "id": "stdlib.codec.checksum.security_context",
            "status": "warn",
            "message": "Checksums.crc32 is visible in a security-looking context; use Hash or Hmac for security or attacker-resistance"
        }));
    }
    if app.uses_net_runtime {
        required_features.push("stdlib.net".to_string());
        value["strongPlan"]["evidence"]["network"] = json!(true);
        value["features"]["network"] = json!(true);
    }
    if app.uses_os_runtime {
        required_features.push("stdlib.os".to_string());
        value["strongPlan"]["evidence"]["os"] = json!(true);
        value["features"]["os"] = json!(true);
    }
    if app.uses_http_client_runtime {
        required_features.push("stdlib.httpclient".to_string());
        value["strongPlan"]["evidence"]["httpClient"] = json!(true);
        value["features"]["httpClient"] = json!(true);
        let http_clients = http_clients_from_sources(app);
        if !http_clients.is_empty() {
            let static_http_clients = http_clients
                .iter()
                .all(|client| client["target"] == "static");
            value["httpClients"] = json!(http_clients);
            value["strongPlan"]["evidence"]["httpClients"] = json!(static_http_clients);
        }
        doctor_checks.push(json!({
            "id": "stdlib.httpclient.contract",
            "status": "ok",
            "message": "HttpClient feature metadata is Plan-visible"
        }));
        if value["httpClients"]
            .as_array()
            .is_some_and(|clients| clients.iter().any(|client| client["target"] == "static"))
        {
            doctor_checks.push(json!({
                "id": "stdlib.httpclient.static-targets",
                "status": "ok",
                "message": "static outbound HTTP client metadata is visible without secret values"
            }));
        }
        if value["httpClients"]
            .as_array()
            .is_some_and(|clients| clients.iter().any(|client| client["target"] == "dynamic"))
        {
            doctor_checks.push(json!({
                "id": "stdlib.httpclient.dynamic-targets",
                "status": "warn",
                "message": "dynamic outbound HTTP client metadata is partial and must be checked at runtime"
            }));
        }
    }
    if app.uses_orm_runtime {
        let orm_tables = app.orm_tables.clone();
        let orm_relations = app.orm_relations.clone();
        let (extraction_status, extraction_reason, doctor_message) = if app.orm_extraction_partial {
            (
                "partial",
                "runtime ORM is available; dynamic table or relation shapes compile and run while static metadata remains partial",
                "sloppy/orm is Plan-visible; runtime ORM works dynamically while some table or relation metadata is partial",
            )
        } else if !orm_tables.is_empty() || !orm_relations.is_empty() {
            (
                "static",
                "runtime ORM is available; static table and relation metadata was extracted from AST call expressions",
                "sloppy/orm is Plan-visible; static table and relation metadata was extracted from AST call expressions",
            )
        } else {
            (
                "runtime-dynamic",
                "runtime ORM is available; no static table or relation metadata was extracted, so ORM shape metadata remains runtime-dynamic",
                "sloppy/orm is Plan-visible; runtime ORM works dynamically and no static table or relation metadata was extracted",
            )
        };
        value["strongPlan"]["evidence"]["orm"] = json!(
            !app.orm_extraction_partial && (!orm_tables.is_empty() || !orm_relations.is_empty())
        );
        value["features"]["orm"] = json!(true);
        value["orm"] = json!({
            "mode": "runtime-dynamic",
            "tables": orm_tables,
            "relations": orm_relations,
            "migrationSnapshots": [],
            "extraction": {
                "status": extraction_status,
                "reason": extraction_reason
            }
        });
        doctor_checks.push(json!({
            "id": "stdlib.orm.dynamic_metadata",
            "status": "info",
            "message": doctor_message
        }));
    }
    if app.uses_workers_runtime {
        required_features.push("stdlib.workers".to_string());
        value["strongPlan"]["evidence"]["workers"] = json!(true);
        value["features"]["workers"] = json!(true);
    }
    if app.uses_ffi_runtime || !ffi_libraries.is_empty() || !ffi_structs.is_empty() {
        required_features.push("stdlib.ffi".to_string());
        value["strongPlan"]["evidence"]["ffi"] = json!(true);
        value["features"]["ffi"] = json!(true);
        value["native"] = json!({
            "ffi": ffi_libraries,
            "ffiStructs": ffi_structs
        });
        doctor_checks.push(json!({
            "id": "stdlib.ffi.unsafe_boundary",
            "status": "warn",
            "message": "unsafeFfi is Plan-visible; native libraries execute in-process and must be trusted"
        }));
    }
    if app.uses_health {
        value["strongPlan"]["evidence"]["health"] = json!(true);
        value["features"]["health"] = json!(true);
    }
    if app.uses_realtime_runtime {
        required_features.push("runtime.realtime".to_string());
        value["strongPlan"]["evidence"]["realtime"] = json!(true);
        value["features"]["realtime"] = json!(true);
        doctor_checks.push(json!({
            "id": "app.realtime.metadata",
            "status": "warn",
            "message": "realtime route metadata is Plan-visible; WebSocket upgrade execution is partial in this alpha"
        }));
    }
    if !auth_schemes.is_empty() || !auth_policies.is_empty() {
        value["auth"] = json!({
            "schemes": auth_schemes,
            "policies": auth_policies
        });
        value["strongPlan"]["evidence"]["auth"] = json!(true);
        value["features"]["auth"] = json!(true);
        doctor_checks.push(json!({
            "id": "app.auth.metadata",
            "status": "info",
            "message": "auth schemes and route authorization requirements are Plan-visible without secret values"
        }));
    }
    if app.dependency_graph.has_entries() {
        for builtin in &app.dependency_graph.node_builtins {
            if builtin.status != "unsupported" {
                let feature_name = match builtin.specifier.as_str() {
                    "node:assert/strict" => "assert".to_string(),
                    "node:stream/promises" => "stream".to_string(),
                    _ => builtin
                        .specifier
                        .strip_prefix("node:")
                        .unwrap_or(&builtin.specifier)
                        .replace('/', "."),
                };
                required_features.push(format!("node.compat.{feature_name}"));
            }
        }
        doctor_checks.push(json!({
            "id": "dependency.graph",
            "status": "info",
            "message": format!(
                "Dependency graph emitted with {} package(s), {} module(s), and {} Node compatibility binding(s).",
                app.dependency_graph.packages.len(),
                app.dependency_graph.modules.len(),
                app.dependency_graph.node_builtins.len()
            )
        }));
        for finding in &app.dependency_graph.compatibility_findings {
            doctor_checks.push(json!({
                "id": finding.code,
                "status": if finding.severity == "error" { "error" } else { "warn" },
                "message": finding.message,
                "package": finding.package,
                "specifier": finding.specifier,
                "hint": finding.hint
            }));
        }
        if !doctor_checks.is_empty() {
            value["doctorChecks"] = json!(doctor_checks);
        }
    }
    if !required_features.is_empty() {
        required_features.sort();
        required_features.dedup();
        value["requiredFeatures"] = json!(required_features);
    }
    if !doctor_checks.is_empty() {
        value["doctorChecks"] = json!(doctor_checks);
    }
    if let Some(configuration) = configuration {
        value["configuration"] = configuration;
    }
    if !schemas.is_empty() {
        value["schemas"] = json!(schemas);
    }
    if !config_reads.is_empty() {
        value["configReads"] = json!(config_reads);
    }
    if emits_metadata {
        value["features"]["metadataInference"] = json!(true);
    }

    serde_json::to_string_pretty(&value)
        .map(|json| format!("{json}\n"))
        .map_err(|error| {
            Diagnostic::new(
                "SLOPPYC_E_EMIT",
                format!("failed to emit app.plan.json: {error}"),
            )
        })
}

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

fn source_location_json(source_name: &str, source: &str, span: Span) -> Value {
    let (line, column) = line_column(source, span.start);
    json!({
        "path": source_name,
        "line": line,
        "column": column
    })
}

fn ffi_argument_marshaling(parameters: &[String]) -> &'static str {
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

fn package_manifest_entry_json(entry: &ConfigurationPackageEntry) -> Value {
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

#[cfg(test)]
mod tests {
    use super::*;

    fn json_response(schema: Option<&str>) -> ResponseMetadata {
        ResponseMetadata {
            helper: "ok".to_string(),
            status: 200,
            kind: "json".to_string(),
            body_schema: schema.map(str::to_string),
            native_body: None,
            source_name: None,
            source_text: None,
            span: None,
            partial: false,
        }
    }

    fn text_response() -> ResponseMetadata {
        ResponseMetadata {
            helper: "text".to_string(),
            status: 200,
            kind: "text".to_string(),
            body_schema: None,
            native_body: None,
            source_name: None,
            source_text: None,
            span: None,
            partial: false,
        }
    }

    #[test]
    fn object_schema_without_properties_is_an_explicit_response_fallback() {
        let schema = json!({ "kind": "object" });

        assert_eq!(
            schema_response_native_fallback_reason(&schema),
            Some("object-properties-missing")
        );
    }

    #[test]
    fn route_json_response_plan_considers_all_declared_json_responses() {
        let primary = json_response(Some("UserDto"));
        let responses = vec![primary.clone(), json_response(Some("OrderDto"))];
        let mut resolved = BTreeMap::new();
        resolved.insert(
            "UserDto".to_string(),
            json!({ "kind": "object", "properties": { "id": { "kind": "int" } } }),
        );
        resolved.insert(
            "OrderDto".to_string(),
            json!({ "kind": "object", "properties": { "id": { "kind": "int" } } }),
        );

        let plan = route_json_response_plan(Some(&primary), &responses, &resolved);

        assert_eq!(plan["mode"], "fallback");
        assert_eq!(plan["writer"], "none");
        assert_eq!(plan["fallbackReason"], "multiple-json-response-schemas");
    }

    #[test]
    fn route_json_response_plan_uses_declared_json_responses_when_primary_is_non_json() {
        let primary = text_response();
        let responses = vec![primary.clone(), json_response(Some("UserDto"))];
        let mut resolved = BTreeMap::new();
        resolved.insert(
            "UserDto".to_string(),
            json!({ "kind": "object", "properties": { "id": { "kind": "int" } } }),
        );

        let plan = route_json_response_plan(Some(&primary), &responses, &resolved);

        assert_eq!(plan["mode"], "native-schema");
        assert_eq!(plan["writer"], "bounded");
        assert_eq!(plan["schema"], "UserDto");
    }
}

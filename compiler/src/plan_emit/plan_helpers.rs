// Shared Plan emission helpers for route plans, literal parsing, schema references, and HTTP client metadata.
use super::*;

pub(super) fn completeness_json(completeness: &Completeness) -> Value {
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

pub(super) fn route_pattern_leading_static_segment(pattern: &str) -> Option<&str> {
    let segment = pattern
        .split('/')
        .skip(1)
        .find(|segment| !segment.is_empty())?;
    (!segment.starts_with('{')).then_some(segment)
}

pub(super) fn route_pattern_constraint_names(pattern: &str) -> Vec<String> {
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

pub(super) fn route_pattern_segment_trie_key(segment: &str) -> String {
    if segment.starts_with('{') && segment.ends_with('}') {
        let inner = &segment[1..segment.len() - 1];
        let constraint = inner.split_once(':').map(|(_, ty)| ty).unwrap_or("str");
        return format!("p:{constraint}");
    }
    format!("s:{segment}")
}

pub(super) fn route_dispatch_segment_trie_nodes(app: &ExtractedApp) -> usize {
    let mut nodes = vec![BTreeMap::<String, usize>::new()];
    let mut node_count = 0usize;

    for route in app
        .routes
        .iter()
        .filter(|route| route_pattern_has_params(&route.pattern))
    {
        let mut current = 0usize;
        for key in std::iter::once(format!("m:{}", route.method)).chain(
            route
                .pattern
                .split('/')
                .skip(1)
                .filter(|segment| !segment.is_empty())
                .map(route_pattern_segment_trie_key),
        ) {
            if let Some(next) = nodes[current].get(&key).copied() {
                current = next;
                continue;
            }

            nodes.push(BTreeMap::new());
            let next = nodes.len() - 1;
            nodes[current].insert(key, next);
            current = next;
            node_count += 1;
        }
    }

    node_count
}

pub(super) fn route_json_request_plan(bindings: &[RequestBinding]) -> Value {
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

pub(super) fn skip_ascii_whitespace(source: &str, mut index: usize) -> usize {
    while index < source.len() && source.as_bytes()[index].is_ascii_whitespace() {
        index += 1;
    }
    index
}

pub(super) fn matching_delimiter(source: &str, start: usize, open: u8, close: u8) -> Option<usize> {
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

pub(super) fn parse_static_identifier(source: &str, start: usize) -> Option<(&str, usize)> {
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

pub(super) fn parse_static_column_ref(expression: &str) -> Option<Value> {
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

pub(super) fn parse_relation_options(expression: &str) -> Option<(Value, Value)> {
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

pub(super) fn schema_response_native_fallback_reason(schema: &Value) -> Option<&'static str> {
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

pub(super) fn route_json_response_plan(
    response: Option<&ResponseMetadata>,
    responses: &[ResponseMetadata],
    resolved_schemas: &BTreeMap<String, Value>,
    allow_native_static: bool,
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
    if allow_native_static && json_responses.len() == 1 && json_responses[0].native_body.is_some() {
        return json!({
            "mode": "native-static",
            "writer": "preencoded",
            "contentType": "application/json"
        });
    }
    if allow_native_static
        && json_responses
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

pub(super) fn route_json_mode(value: &Value) -> &str {
    value.get("mode").and_then(Value::as_str).unwrap_or("none")
}

pub(super) fn websocket_options_json(options: &WebSocketRouteOptionsMetadata) -> Value {
    let origins = match &options.origins {
        Some(WebSocketOriginsMetadata::Any) => json!("*"),
        Some(WebSocketOriginsMetadata::List(origins)) => json!(origins),
        None => Value::Null,
    };
    json!({
        "protocols": &options.protocols,
        "origins": origins,
        "maxMessageBytes": options.max_message_bytes,
        "maxSendQueueBytes": options.max_send_queue_bytes,
        "heartbeatMs": options.heartbeat_ms,
        "idleTimeoutMs": options.idle_timeout_ms,
        "closeTimeoutMs": options.close_timeout_ms,
        "slowClientPolicy": options.slow_client_policy.as_ref(),
        "compression": options.compression.unwrap_or(false)
    })
}

pub(super) fn route_dispatch_json(
    app: &ExtractedApp,
    route_completeness_values: &[Completeness],
    route_artifact: Option<&RouteDispatchArtifactMetadata>,
    resolved_schemas: &BTreeMap<String, Value>,
) -> Value {
    let mut static_routes = 0usize;
    let mut parameter_candidate_keys = BTreeSet::new();
    let mut constraints = BTreeSet::new();
    let mut native_no_js_endpoints = 0usize;
    let mut request_native_json_routes = 0usize;
    let mut request_generic_json_routes = 0usize;
    let mut request_fallback_json_routes = 0usize;
    let mut response_native_json_routes = 0usize;
    let mut response_generic_json_routes = 0usize;
    let mut response_fallback_json_routes = 0usize;
    let mut url_writers = 0usize;

    for route in &app.routes {
        let has_params = route_pattern_has_params(&route.pattern);
        if has_params {
            parameter_candidate_keys.insert((
                route.method,
                route_pattern_leading_static_segment(&route.pattern)
                    .unwrap_or("")
                    .to_string(),
            ));
        } else {
            static_routes += 1;
        }
        constraints.extend(route_pattern_constraint_names(&route.pattern));

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
        if route_execution_kind(
            response_kind,
            native_body,
            !route.handler.effects.is_empty(),
        ) != RouteExecutionKind::V8Handler
        {
            native_no_js_endpoints += 1;
        }

        if route.name.is_some() {
            url_writers += 1;
        }

        match route_json_mode(&route_json_request_plan(&route.handler.bindings)) {
            "native-schema" => request_native_json_routes += 1,
            "generic" => request_generic_json_routes += 1,
            "fallback" => request_fallback_json_routes += 1,
            _ => {}
        }

        let response_plan = route_json_response_plan(
            route.handler.response.as_ref(),
            &route.handler.responses,
            resolved_schemas,
            route.handler.effects.is_empty(),
        );
        match route_json_mode(&response_plan) {
            "native-static" | "native-schema" => response_native_json_routes += 1,
            "generic" => response_generic_json_routes += 1,
            "fallback" => response_fallback_json_routes += 1,
            _ => {}
        }
    }

    let parameter_routes = app.routes.len().saturating_sub(static_routes);
    let segment_trie_nodes = route_dispatch_segment_trie_nodes(app);
    let parameter_candidate_buckets = parameter_candidate_keys.len();
    let partial_routes = route_completeness_values
        .iter()
        .filter(|route| route.status.as_str() != "complete")
        .count();
    let constraints = constraints.into_iter().collect::<Vec<_>>();
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
        "urlWriters": url_writers,
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
pub(super) struct SchemaReferenceResolution {
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

    pub(super) fn doctor_checks(&self) -> Vec<Value> {
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

pub(super) fn schema_definition_map(app: &ExtractedApp) -> BTreeMap<String, Value> {
    app.schemas
        .iter()
        .map(|schema| (schema.name.clone(), schema.definition.clone()))
        .collect()
}

pub(super) fn partial_schema_reference(name: Option<&str>, reason: &'static str) -> Value {
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

pub(super) fn apply_schema_reference_overrides(
    reference: &Map<String, Value>,
    resolved: &mut Value,
) {
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

pub(super) fn resolve_schema_references(
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

pub(super) fn skip_ascii_ws(source: &str, mut index: usize) -> usize {
    while index < source.len() && source.as_bytes()[index].is_ascii_whitespace() {
        index += 1;
    }
    index
}

pub(super) fn is_identifier_byte(byte: u8) -> bool {
    byte.is_ascii_alphanumeric() || byte == b'_' || byte == b'$'
}

pub(super) fn parse_string_literal_at(source: &str, index: usize) -> Option<(String, usize)> {
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

pub(super) fn skip_template_literal_at(source: &str, index: usize) -> Option<usize> {
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

pub(super) fn skip_line_comment_at(source: &str, index: usize) -> Option<usize> {
    if !source.as_bytes().get(index..)?.starts_with(b"//") {
        return None;
    }
    let mut cursor = index + 2;
    while cursor < source.len() && source.as_bytes()[cursor] != b'\n' {
        cursor += 1;
    }
    Some(cursor)
}

pub(super) fn skip_block_comment_at(source: &str, index: usize) -> Option<usize> {
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

pub(super) fn skip_non_code_at(source: &str, index: usize) -> Option<usize> {
    let byte = *source.as_bytes().get(index)?;
    if byte == b'\'' || byte == b'"' {
        return parse_string_literal_at(source, index).map(|(_, end)| end);
    }
    if byte == b'`' {
        return skip_template_literal_at(source, index);
    }
    skip_line_comment_at(source, index).or_else(|| skip_block_comment_at(source, index))
}

pub(super) fn find_code_token(source: &str, token: &str, start: usize) -> Option<usize> {
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

pub(super) fn token_has_code_boundaries(source: &str, index: usize, token: &[u8]) -> bool {
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

pub(super) fn find_matching_delimiter(
    source: &str,
    open_index: usize,
    open: u8,
    close: u8,
) -> Option<usize> {
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

pub(super) fn find_property_value(source: &str, property: &str) -> Option<usize> {
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

pub(super) fn parse_http_base_url(source: &str) -> (Option<String>, Option<String>) {
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

pub(super) fn parse_http_endpoints(source: &str) -> Vec<Value> {
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

pub(super) fn http_clients_from_sources(app: &ExtractedApp) -> Vec<Value> {
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

//! Semantic validation for compiler artifacts.
//!
//! Goldens prove bytes did not drift. This module checks whether emitted
//! artifacts still agree with the compiler correctness contract.

use std::{
    collections::{BTreeMap, BTreeSet},
    fmt,
    path::Path,
};

use serde_json::Value;
use sha2::{Digest, Sha256};

#[derive(Debug, Clone, Eq, PartialEq)]
pub struct ContractViolation {
    pub invariant: &'static str,
    pub path: String,
    pub message: String,
}

impl ContractViolation {
    fn new(invariant: &'static str, path: impl Into<String>, message: impl Into<String>) -> Self {
        Self {
            invariant,
            path: path.into(),
            message: message.into(),
        }
    }
}

impl fmt::Display for ContractViolation {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            formatter,
            "{} at {}: {}",
            self.invariant, self.path, self.message
        )
    }
}

#[derive(Debug, Clone, Default)]
pub struct ContractArtifacts<'a> {
    pub route_artifact: Option<&'a [u8]>,
    pub dependency_graph: Option<&'a Value>,
    pub artifact_root: Option<&'a Path>,
}

#[derive(Debug, Clone)]
struct RouteArtifactEntry {
    method: &'static str,
    handler_id: u32,
    pattern: String,
    execution_kind: &'static str,
}

const ALLOWED_EXECUTION_KINDS: &[&str] = &[
    "v8-handler",
    "native-static-text",
    "native-static-json",
    "native-static-empty",
    "native-static-problem",
];

pub fn validate_plan_text(
    plan_text: &str,
    artifacts: ContractArtifacts<'_>,
) -> Result<(), Vec<ContractViolation>> {
    let plan = match serde_json::from_str::<Value>(plan_text) {
        Ok(plan) => plan,
        Err(error) => {
            return Err(vec![ContractViolation::new(
                "plan.valid-json",
                "$",
                format!("app.plan.json did not parse: {error}"),
            )])
        }
    };
    validate_plan_value(&plan, artifacts)
}

pub fn validate_plan_value(
    plan: &Value,
    artifacts: ContractArtifacts<'_>,
) -> Result<(), Vec<ContractViolation>> {
    let mut violations = Vec::new();
    validate_handlers(plan, &mut violations);
    validate_dispatch(plan, artifacts.route_artifact, &mut violations);
    validate_provider_effects(plan, &mut violations);
    validate_completeness(plan, &mut violations);
    validate_openapi_response_metadata(plan, &mut violations);
    validate_dependency_graph(
        plan,
        artifacts.dependency_graph,
        artifacts.artifact_root,
        &mut violations,
    );

    if violations.is_empty() {
        Ok(())
    } else {
        Err(violations)
    }
}

fn validate_handlers(plan: &Value, violations: &mut Vec<ContractViolation>) {
    let routes = array_at(plan, "routes", violations);
    let handlers = array_at(plan, "handlers", violations);
    let mut handler_counts = BTreeMap::<u64, usize>::new();
    for (index, handler) in handlers.iter().enumerate() {
        match handler.get("id").and_then(Value::as_u64) {
            Some(id) => *handler_counts.entry(id).or_default() += 1,
            None => violations.push(ContractViolation::new(
                "handlers.id",
                format!("$.handlers[{index}]"),
                "handler id must be a number",
            )),
        }
    }

    for (id, count) in &handler_counts {
        if *count != 1 {
            violations.push(ContractViolation::new(
                "handlers.unique-id",
                "$.handlers",
                format!("handler id {id} appears {count} times"),
            ));
        }
    }

    for (index, route) in routes.iter().enumerate() {
        let path = route_label(index, route);
        let Some(handler_id) = route.get("handlerId").and_then(Value::as_u64) else {
            violations.push(ContractViolation::new(
                "routes.handler-id",
                path,
                "route handlerId must be present",
            ));
            continue;
        };
        match handler_counts.get(&handler_id).copied().unwrap_or(0) {
            1 => {}
            0 => violations.push(ContractViolation::new(
                "routes.handler-resolves",
                path,
                format!("route handlerId {handler_id} does not resolve to a handler"),
            )),
            count => violations.push(ContractViolation::new(
                "routes.handler-resolves",
                path,
                format!("route handlerId {handler_id} resolves to {count} handlers"),
            )),
        }

        if let Some(endpoint_id) = route
            .get("dispatch")
            .and_then(|dispatch| dispatch.get("endpointId"))
            .and_then(Value::as_u64)
        {
            if endpoint_id != handler_id {
                violations.push(ContractViolation::new(
                    "dispatch.handler-agreement",
                    route_label(index, route),
                    format!(
                        "dispatch endpointId {endpoint_id} disagrees with handlerId {handler_id}"
                    ),
                ));
            }
        } else {
            violations.push(ContractViolation::new(
                "dispatch.handler-agreement",
                route_label(index, route),
                "dispatch endpointId must be present",
            ));
        }
    }
}

fn validate_dispatch(
    plan: &Value,
    route_artifact: Option<&[u8]>,
    violations: &mut Vec<ContractViolation>,
) {
    let routes = array_at(plan, "routes", violations);
    let mut native_no_js = 0usize;
    for (index, route) in routes.iter().enumerate() {
        let execution_kind = route
            .get("dispatch")
            .and_then(|dispatch| dispatch.get("executionKind"))
            .and_then(Value::as_str);
        let Some(execution_kind) = execution_kind else {
            violations.push(ContractViolation::new(
                "dispatch.execution-kind",
                route_label(index, route),
                "dispatch executionKind must be present",
            ));
            continue;
        };
        if !ALLOWED_EXECUTION_KINDS.contains(&execution_kind) {
            violations.push(ContractViolation::new(
                "dispatch.execution-kind",
                route_label(index, route),
                format!("unsupported executionKind {execution_kind}"),
            ));
            continue;
        }
        if execution_kind != "v8-handler" {
            native_no_js += 1;
        }
        let has_effects = route
            .get("effects")
            .and_then(Value::as_array)
            .is_some_and(|effects| !effects.is_empty());
        if has_effects {
            if execution_kind != "v8-handler" {
                violations.push(ContractViolation::new(
                    "dispatch.provider-effects-v8",
                    route_label(index, route),
                    format!("provider-effect route used executionKind {execution_kind}"),
                ));
            }
            if route.get("nativeResponse").is_some() {
                violations.push(ContractViolation::new(
                    "dispatch.provider-effects-no-native-response",
                    route_label(index, route),
                    "provider-effect route emitted nativeResponse metadata",
                ));
            }
        }
    }

    if let Some(actual) = plan
        .get("routeDispatch")
        .and_then(|dispatch| dispatch.get("nativeNoJsEndpoints"))
        .and_then(Value::as_u64)
    {
        if actual as usize != native_no_js {
            violations.push(ContractViolation::new(
                "route-dispatch.native-no-js-count",
                "$.routeDispatch.nativeNoJsEndpoints",
                format!("expected {native_no_js}, got {actual}"),
            ));
        }
    } else if plan.get("kind").and_then(Value::as_str).unwrap_or("web") == "web" {
        violations.push(ContractViolation::new(
            "route-dispatch.native-no-js-count",
            "$.routeDispatch.nativeNoJsEndpoints",
            "web Plan must expose nativeNoJsEndpoints",
        ));
    }

    if let Some(bytes) = route_artifact {
        validate_route_artifact_hash(plan, bytes, violations);
        match parse_route_artifact(bytes) {
            Ok(entries) => {
                if entries.len() != routes.len() {
                    violations.push(ContractViolation::new(
                        "route-artifact.route-count",
                        "$.routeDispatch.artifact",
                        format!(
                            "route artifact has {} entries but Plan has {} routes",
                            entries.len(),
                            routes.len()
                        ),
                    ));
                }
                for (index, entry) in entries.iter().enumerate() {
                    let Some(route) = routes.get(index) else {
                        continue;
                    };
                    let method = route.get("method").and_then(Value::as_str);
                    if method != Some(entry.method) {
                        violations.push(ContractViolation::new(
                            "route-artifact.method-agreement",
                            route_label(index, route),
                            format!(
                                "route artifact method {} disagrees with Plan {}",
                                entry.method,
                                method.unwrap_or("<missing>")
                            ),
                        ));
                    }
                    let pattern = route.get("pattern").and_then(Value::as_str);
                    if pattern != Some(entry.pattern.as_str()) {
                        violations.push(ContractViolation::new(
                            "route-artifact.pattern-agreement",
                            route_label(index, route),
                            format!(
                                "route artifact pattern {} disagrees with Plan {}",
                                entry.pattern,
                                pattern.unwrap_or("<missing>")
                            ),
                        ));
                    }
                    let handler_id = route.get("handlerId").and_then(Value::as_u64);
                    if handler_id != Some(u64::from(entry.handler_id)) {
                        violations.push(ContractViolation::new(
                            "route-artifact.handler-agreement",
                            route_label(index, route),
                            format!(
                                "route artifact handlerId {} disagrees with Plan {:?}",
                                entry.handler_id, handler_id
                            ),
                        ));
                    }
                    let plan_kind = route
                        .get("dispatch")
                        .and_then(|dispatch| dispatch.get("executionKind"))
                        .and_then(Value::as_str);
                    if plan_kind != Some(entry.execution_kind) {
                        violations.push(ContractViolation::new(
                            "route-artifact.execution-kind-agreement",
                            route_label(index, route),
                            format!(
                                "route artifact executionKind {} disagrees with Plan {}",
                                entry.execution_kind,
                                plan_kind.unwrap_or("<missing>")
                            ),
                        ));
                    }
                }
            }
            Err(message) => violations.push(ContractViolation::new(
                "route-artifact.parse",
                "$.routeDispatch.artifact",
                message,
            )),
        }
    }
}

fn validate_route_artifact_hash(
    plan: &Value,
    route_artifact: &[u8],
    violations: &mut Vec<ContractViolation>,
) {
    let Some(expected_hash) = plan
        .get("routeDispatch")
        .and_then(|dispatch| dispatch.get("artifact"))
        .and_then(|artifact| artifact.get("hash"))
        .and_then(Value::as_str)
    else {
        violations.push(ContractViolation::new(
            "route-artifact.hash-present",
            "$.routeDispatch.artifact.hash",
            "route artifact bytes were provided but Plan did not include routeDispatch.artifact.hash",
        ));
        return;
    };
    let actual_hash = sha256_hash(route_artifact);
    if expected_hash != actual_hash {
        violations.push(ContractViolation::new(
            "route-artifact.hash-agreement",
            "$.routeDispatch.artifact.hash",
            format!("expected {expected_hash}, got {actual_hash}"),
        ));
    }
}

fn validate_provider_effects(plan: &Value, violations: &mut Vec<ContractViolation>) {
    let mut capabilities = BTreeSet::<(String, String, String)>::new();
    for capability in plan
        .get("capabilities")
        .and_then(Value::as_array)
        .into_iter()
        .flatten()
    {
        let Some(token) = capability.get("token").and_then(Value::as_str) else {
            continue;
        };
        let Some(kind) = capability.get("kind").and_then(Value::as_str) else {
            continue;
        };
        capabilities.insert((token.to_string(), kind.to_string(), String::new()));
    }

    for (route_index, route) in array_at(plan, "routes", violations).iter().enumerate() {
        let effects = route
            .get("effects")
            .and_then(Value::as_array)
            .map(Vec::as_slice)
            .unwrap_or(&[]);
        for (effect_index, effect) in effects.iter().enumerate() {
            let provider = effect.get("provider").and_then(Value::as_str).unwrap_or("");
            let capability_kind = effect
                .get("capabilityKind")
                .and_then(Value::as_str)
                .unwrap_or("");
            let provider_kind = effect
                .get("providerKind")
                .and_then(Value::as_str)
                .unwrap_or("");
            if capabilities.contains(&(
                provider.to_string(),
                capability_kind.to_string(),
                String::new(),
            )) {
                continue;
            }
            if !completeness_has_reason(route, "missing-provider") {
                violations.push(ContractViolation::new(
                    "effects.capability-resolution",
                    format!("{}.effects[{effect_index}]", route_label(route_index, route)),
                    format!(
                        "effect {provider}/{capability_kind}/{provider_kind} does not resolve to a declared capability or honest missing-provider metadata"
                    ),
                ));
            }
        }
    }
}

fn validate_completeness(plan: &Value, violations: &mut Vec<ContractViolation>) {
    for (index, route) in array_at(plan, "routes", violations).iter().enumerate() {
        let status = route
            .get("completeness")
            .and_then(|completeness| completeness.get("status"))
            .and_then(Value::as_str);
        if status != Some("complete") && !completeness_has_any_reason(route) {
            violations.push(ContractViolation::new(
                "metadata.completeness-honesty",
                route_label(index, route),
                format!(
                    "non-complete metadata status {} must include at least one reason",
                    status.unwrap_or("<missing>")
                ),
            ));
        }
        let response_partial = route
            .get("response")
            .and_then(|response| response.get("partial"))
            .and_then(Value::as_bool)
            .unwrap_or(false);
        if response_partial && !completeness_has_reason(route, "response-metadata-partial") {
            violations.push(ContractViolation::new(
                "metadata.completeness-honesty",
                route_label(index, route),
                "partial response metadata must be reflected in completeness reasons",
            ));
        }
    }
}

fn validate_openapi_response_metadata(plan: &Value, violations: &mut Vec<ContractViolation>) {
    for (index, route) in array_at(plan, "routes", violations).iter().enumerate() {
        let Some(response) = route.get("response") else {
            continue;
        };
        let Some(status) = response.get("status").and_then(Value::as_u64) else {
            continue;
        };
        let Some(openapi) = route.get("openapi") else {
            continue;
        };
        let Some(responses) = openapi.get("responses").and_then(Value::as_object) else {
            continue;
        };
        if !responses.contains_key(&status.to_string()) {
            violations.push(ContractViolation::new(
                "openapi.response-agreement",
                route_label(index, route),
                format!("static response status {status} is absent from route OpenAPI override"),
            ));
        }
    }
}

fn validate_dependency_graph(
    plan: &Value,
    dependency_graph: Option<&Value>,
    artifact_root: Option<&Path>,
    violations: &mut Vec<ContractViolation>,
) {
    let graph = dependency_graph
        .or_else(|| plan.get("dependencyGraph"))
        .unwrap_or(&Value::Null);
    let Some(packages) = graph.get("packages").and_then(Value::as_array) else {
        return;
    };
    for (index, package) in packages.iter().enumerate() {
        let Some(root) = package.get("root").and_then(Value::as_str) else {
            violations.push(ContractViolation::new(
                "dependency-graph.package-root",
                format!("$.dependencyGraph.packages[{index}]"),
                "package root must be present",
            ));
            continue;
        };
        if let Some(artifact_root) = artifact_root {
            let root_path = Path::new(root);
            let candidate = if root_path.is_absolute() {
                root_path.to_path_buf()
            } else {
                artifact_root.join(root_path)
            };
            if !candidate.exists() {
                violations.push(ContractViolation::new(
                    "dependency-graph.package-root",
                    format!("$.dependencyGraph.packages[{index}].root"),
                    format!("package root does not exist: {}", candidate.display()),
                ));
            }
        }
    }
}

fn array_at<'a>(
    plan: &'a Value,
    field: &'static str,
    violations: &mut Vec<ContractViolation>,
) -> &'a [Value] {
    static EMPTY: [Value; 0] = [];
    match plan.get(field).and_then(Value::as_array) {
        Some(values) => values.as_slice(),
        None => {
            if plan.get("kind").and_then(Value::as_str).unwrap_or("web") == "web" {
                violations.push(ContractViolation::new(
                    "plan.required-array",
                    format!("$.{field}"),
                    "web Plan field must be an array",
                ));
            }
            &EMPTY
        }
    }
}

fn route_label(index: usize, route: &Value) -> String {
    let method = route
        .get("method")
        .and_then(Value::as_str)
        .unwrap_or("<method>");
    let pattern = route
        .get("pattern")
        .and_then(Value::as_str)
        .unwrap_or("<pattern>");
    format!("$.routes[{index}] {method} {pattern}")
}

fn completeness_has_reason(route: &Value, reason_code: &str) -> bool {
    route
        .get("completeness")
        .and_then(|completeness| completeness.get("reasons"))
        .and_then(Value::as_array)
        .is_some_and(|reasons| {
            reasons.iter().any(|reason| {
                reason
                    .get("code")
                    .and_then(Value::as_str)
                    .is_some_and(|code| code == reason_code)
            })
        })
}

fn completeness_has_any_reason(route: &Value) -> bool {
    route
        .get("completeness")
        .and_then(|completeness| completeness.get("reasons"))
        .and_then(Value::as_array)
        .is_some_and(|reasons| !reasons.is_empty())
}

fn parse_route_artifact(bytes: &[u8]) -> Result<Vec<RouteArtifactEntry>, String> {
    if bytes.len() < 64 {
        return Err("route artifact is shorter than the SLRT header".to_string());
    }
    if &bytes[0..4] != b"SLRT" {
        return Err("route artifact magic is not SLRT".to_string());
    }
    let version = read_u32(bytes, 4)?;
    if version != 1 {
        return Err(format!("unsupported route artifact version {version}"));
    }
    let route_count = read_u32(bytes, 16)? as usize;
    let table_offset = read_u32(bytes, 24)? as usize;
    let table_size = read_u32(bytes, 28)? as usize;
    let string_table_offset = read_u32(bytes, 32)? as usize;
    let string_table_size = read_u32(bytes, 36)? as usize;
    if table_size != route_count.saturating_mul(48) {
        return Err(format!(
            "route table size {table_size} disagrees with route count {route_count}"
        ));
    }
    if bytes.len() < table_offset.saturating_add(table_size) {
        return Err("route artifact route table extends past end of file".to_string());
    }
    if bytes.len() < string_table_offset.saturating_add(string_table_size) {
        return Err("route artifact string table extends past end of file".to_string());
    }
    let mut entries = Vec::with_capacity(route_count);
    for index in 0..route_count {
        let offset = table_offset + index * 48;
        let method_code = read_u32(bytes, offset)?;
        let handler_id = read_u32(bytes, offset + 4)?;
        let pattern_offset = read_u32(bytes, offset + 8)? as usize;
        let pattern_len = read_u32(bytes, offset + 12)? as usize;
        let execution_code = read_u32(bytes, offset + 28)?;
        let pattern_start = string_table_offset
            .checked_add(pattern_offset)
            .ok_or_else(|| "route artifact pattern offset overflowed".to_string())?;
        let pattern_end = pattern_start
            .checked_add(pattern_len)
            .ok_or_else(|| "route artifact pattern length overflowed".to_string())?;
        if pattern_offset > string_table_size
            || pattern_len > string_table_size.saturating_sub(pattern_offset)
            || pattern_end > bytes.len()
        {
            return Err("route artifact pattern range is outside the string table".to_string());
        }
        let pattern = std::str::from_utf8(&bytes[pattern_start..pattern_end])
            .map_err(|_| "route artifact pattern is not valid UTF-8".to_string())?
            .to_string();
        entries.push(RouteArtifactEntry {
            method: method_from_artifact(method_code)?,
            handler_id,
            pattern,
            execution_kind: execution_kind_from_artifact(execution_code)?,
        });
    }
    Ok(entries)
}

fn read_u32(bytes: &[u8], offset: usize) -> Result<u32, String> {
    let slice = bytes
        .get(offset..offset + 4)
        .ok_or_else(|| format!("missing uint32 at byte offset {offset}"))?;
    Ok(u32::from_le_bytes([slice[0], slice[1], slice[2], slice[3]]))
}

fn method_from_artifact(code: u32) -> Result<&'static str, String> {
    match code {
        1 => Ok("GET"),
        2 => Ok("POST"),
        3 => Ok("PUT"),
        4 => Ok("DELETE"),
        5 => Ok("PATCH"),
        6 => Ok("OPTIONS"),
        7 => Ok("HEAD"),
        _ => Err(format!("unsupported route method code {code}")),
    }
}

fn execution_kind_from_artifact(code: u32) -> Result<&'static str, String> {
    match code {
        1 => Ok("v8-handler"),
        2 => Ok("native-static-text"),
        3 => Ok("native-static-json"),
        4 => Ok("native-static-empty"),
        5 => Ok("native-static-problem"),
        _ => Err(format!("unsupported route execution kind code {code}")),
    }
}

fn sha256_hash(bytes: &[u8]) -> String {
    let digest = Sha256::digest(bytes);
    let mut output = String::with_capacity("sha256:".len() + digest.len() * 2);
    output.push_str("sha256:");
    for byte in digest {
        output.push_str(&format!("{byte:02x}"));
    }
    output
}

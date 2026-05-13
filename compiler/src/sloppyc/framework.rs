// Static framework middleware, CORS, request logging, and controller extraction.
use super::*;

pub(super) fn app_use_request_id_call(
    path: &Path,
    source: &str,
    source_name: &str,
    expression: &Expression<'_>,
    state: &mut AppState,
) -> Result<bool, Diagnostic> {
    let Some((call, descriptor_call)) = app_use_descriptor_call(expression, state, "RequestId")
    else {
        return Ok(false);
    };
    if !state.request_id_imported {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_REQUEST_ID",
            "RequestId.defaults() requires importing RequestId from \"sloppy\"",
        )
        .with_path(path)
        .with_span(descriptor_call.span));
    }
    let options = request_id_options_from_call(path, descriptor_call)?;
    let source_json = serde_json::to_string(&json!({
        "header": options.header,
        "responseHeader": options.response_header,
        "trustIncoming": options.trust_incoming
    }))
    .unwrap_or_else(|_| "{}".to_string());
    let middleware = FrameworkMiddleware {
        kind: FrameworkMiddlewareKind::RequestId,
        source: format!("__sloppy_request_id({source_json})"),
        sequence: state.next_middleware_sequence,
        source_name: source_name.to_string(),
        source_text: source.to_string(),
        span: call.span,
    };
    state.next_middleware_sequence += 1;
    state.middleware.push(middleware);
    Ok(true)
}

pub(super) fn app_use_request_logging_call(
    path: &Path,
    source: &str,
    source_name: &str,
    expression: &Expression<'_>,
    state: &mut AppState,
) -> Result<bool, Diagnostic> {
    let Some((call, descriptor_call)) =
        app_use_descriptor_call(expression, state, "RequestLogging")
    else {
        return Ok(false);
    };
    if !state.request_logging_imported {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_REQUEST_LOGGING",
            "RequestLogging.defaults() requires importing RequestLogging from \"sloppy\"",
        )
        .with_path(path)
        .with_span(descriptor_call.span));
    }
    let options = request_logging_options_from_call(path, descriptor_call)?;
    let source_json = serde_json::to_string(&json!({
        "includeRoute": options.include_route,
        "includeDuration": options.include_duration,
        "includeRequestId": options.include_request_id
    }))
    .unwrap_or_else(|_| "{}".to_string());
    let middleware = FrameworkMiddleware {
        kind: FrameworkMiddlewareKind::RequestLogging,
        source: format!("__sloppy_request_logging({source_json})"),
        sequence: state.next_middleware_sequence,
        source_name: source_name.to_string(),
        source_text: source.to_string(),
        span: call.span,
    };
    state.next_middleware_sequence += 1;
    state.middleware.push(middleware);
    Ok(true)
}

fn app_use_descriptor_call<'a>(
    expression: &'a Expression<'a>,
    state: &AppState,
    descriptor: &str,
) -> Option<(&'a CallExpression<'a>, &'a CallExpression<'a>)> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    let (receiver, property) = static_member_name(&call.callee)?;
    if property != "use" || !state.app_vars.contains(receiver) || call.arguments.len() != 1 {
        return None;
    }
    let Some(Argument::CallExpression(descriptor_call)) = call.arguments.first() else {
        return None;
    };
    let (object, method) = static_member_name(&descriptor_call.callee)?;
    (object == descriptor && method == "defaults").then_some((call, descriptor_call))
}

fn request_id_options_from_call(
    path: &Path,
    call: &CallExpression<'_>,
) -> Result<RequestIdOptions, Diagnostic> {
    if call.arguments.len() > 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_REQUEST_ID",
            "RequestId.defaults accepts at most one literal options object",
        )
        .with_path(path)
        .with_span(call.span));
    }
    let mut options = RequestIdOptions {
        header: "x-request-id".to_string(),
        response_header: true,
        trust_incoming: false,
    };
    let Some(argument) = call.arguments.first() else {
        return Ok(options);
    };
    let Some(object) = object_argument(argument) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_REQUEST_ID",
            "dynamic RequestId options are not supported by compiler extraction",
        )
        .with_path(path)
        .with_span(argument_span(argument).unwrap_or(call.span)));
    };
    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_REQUEST_ID",
                "RequestId options must use literal properties",
            )
            .with_path(path)
            .with_span(object.span));
        };
        if property.kind != PropertyKind::Init || property.method || property.computed {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_REQUEST_ID",
                "RequestId options must use simple literal properties",
            )
            .with_path(path)
            .with_span(property.span));
        }
        let Some(name) = property_key_name(&property.key) else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_REQUEST_ID",
                "RequestId option names must be literal",
            )
            .with_path(path)
            .with_span(property.span));
        };
        match name {
            "header" => {
                let Expression::StringLiteral(value) = &property.value else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_REQUEST_ID",
                        "RequestId header must be a string literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                };
                if !compiler_header_name_allowed(value.value.as_str()) {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_REQUEST_ID",
                        "RequestId header must be a safe unmanaged HTTP header name",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                }
                options.header = value.value.as_str().to_string();
            }
            "responseHeader" => {
                let Expression::BooleanLiteral(value) = &property.value else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_REQUEST_ID",
                        "RequestId responseHeader option must be a boolean literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                };
                options.response_header = value.value;
            }
            "trustIncoming" => {
                let Expression::BooleanLiteral(value) = &property.value else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_REQUEST_ID",
                        "RequestId trustIncoming option must be a boolean literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                };
                options.trust_incoming = value.value;
            }
            "generator" => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_REQUEST_ID",
                    "RequestId generator callbacks are unsupported in compiler source input",
                )
                .with_path(path)
                .with_span(property.value.span())
                .with_hint("Use static RequestId options in compiler input; generator callbacks remain an app-host test helper."));
            }
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_REQUEST_ID",
                    format!("unsupported RequestId option '{name}'"),
                )
                .with_path(path)
                .with_span(property.span));
            }
        }
    }
    Ok(options)
}

fn request_logging_options_from_call(
    path: &Path,
    call: &CallExpression<'_>,
) -> Result<RequestLoggingOptions, Diagnostic> {
    if call.arguments.len() > 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_REQUEST_LOGGING",
            "RequestLogging.defaults accepts at most one literal options object",
        )
        .with_path(path)
        .with_span(call.span));
    }
    let mut options = RequestLoggingOptions {
        include_route: true,
        include_duration: true,
        include_request_id: true,
    };
    let Some(argument) = call.arguments.first() else {
        return Ok(options);
    };
    let Some(object) = object_argument(argument) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_REQUEST_LOGGING",
            "dynamic RequestLogging options are not supported by compiler extraction",
        )
        .with_path(path)
        .with_span(argument_span(argument).unwrap_or(call.span)));
    };
    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_REQUEST_LOGGING",
                "RequestLogging options must use literal properties",
            )
            .with_path(path)
            .with_span(object.span));
        };
        if property.kind != PropertyKind::Init || property.method || property.computed {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_REQUEST_LOGGING",
                "RequestLogging options must use simple literal properties",
            )
            .with_path(path)
            .with_span(property.span));
        }
        let Some(name) = property_key_name(&property.key) else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_REQUEST_LOGGING",
                "RequestLogging option names must be literal",
            )
            .with_path(path)
            .with_span(property.span));
        };
        let Expression::BooleanLiteral(value) = &property.value else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_REQUEST_LOGGING",
                "RequestLogging options must be boolean literals",
            )
            .with_path(path)
            .with_span(property.value.span()));
        };
        match name {
            "includeRoute" => options.include_route = value.value,
            "includeDuration" => options.include_duration = value.value,
            "includeRequestId" => options.include_request_id = value.value,
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_REQUEST_LOGGING",
                    format!("unsupported RequestLogging option '{name}'"),
                )
                .with_path(path)
                .with_span(property.span));
            }
        }
    }
    Ok(options)
}

pub(super) fn app_use_middleware_call(
    path: &Path,
    source: &str,
    source_name: &str,
    expression: &Expression<'_>,
    state: &mut AppState,
) -> Result<bool, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(false);
    };
    let Some((receiver, property)) = static_member_name(&call.callee) else {
        return Ok(false);
    };
    if property != "use" {
        return Ok(false);
    }
    let target_is_app = state.app_vars.contains(receiver);
    let target_is_group = state.group_vars.contains_key(receiver);
    if !target_is_app && !target_is_group {
        return Ok(false);
    }
    if call.arguments.len() != 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_MIDDLEWARE",
            "app.use/group.use accepts exactly one static middleware function in compiler input",
        )
        .with_path(path)
        .with_span(call.span));
    }
    let Some(argument) = call.arguments.first() else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_MIDDLEWARE",
            "app.use/group.use accepts exactly one static middleware function in compiler input",
        )
        .with_path(path)
        .with_span(call.span));
    };
    let middleware = middleware_from_argument(path, source, source_name, argument, state)?;
    if target_is_app {
        state.middleware.push(middleware);
    } else if let Some(group) = state.group_vars.get_mut(receiver) {
        group.middleware.push(middleware);
    }
    state.next_middleware_sequence += 1;
    Ok(true)
}

fn middleware_from_argument(
    path: &Path,
    source: &str,
    source_name: &str,
    argument: &Argument<'_>,
    state: &mut AppState,
) -> Result<FrameworkMiddleware, Diagnostic> {
    match argument {
        Argument::ArrowFunctionExpression(function) => {
            if arrow_requires_results_import(function)
                && !state.results_imported
                && state.results_required_span.is_none()
            {
                state.results_required_span = Some(function.span);
            }
            validate_middleware_arrow(path, function, state)?;
            let source_text = source_slice(source, function.span).ok_or_else(|| {
                Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_MIDDLEWARE",
                    "middleware source could not be extracted",
                )
                .with_path(path)
                .with_span(function.span)
            })?;
            Ok(FrameworkMiddleware {
                kind: FrameworkMiddlewareKind::User,
                source: source_text,
                sequence: state.next_middleware_sequence,
                source_name: source_name.to_string(),
                source_text: source.to_string(),
                span: function.span,
            })
        }
        Argument::FunctionExpression(function) => {
            if function_requires_results_import(function)
                && !state.results_imported
                && state.results_required_span.is_none()
            {
                state.results_required_span = Some(function.span);
            }
            validate_middleware_function(path, function, state)?;
            let source_text = source_slice(source, function.span).ok_or_else(|| {
                Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_MIDDLEWARE",
                    "middleware source could not be extracted",
                )
                .with_path(path)
                .with_span(function.span)
            })?;
            Ok(FrameworkMiddleware {
                kind: FrameworkMiddlewareKind::User,
                source: source_text,
                sequence: state.next_middleware_sequence,
                source_name: source_name.to_string(),
                source_text: source.to_string(),
                span: function.span,
            })
        }
        Argument::Identifier(identifier) => {
            let name = identifier.name.as_str();
            if !state.helper_sources.contains_key(name) {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_MIDDLEWARE",
                    "named middleware must reference a top-level function helper",
                )
                .with_path(path)
                .with_span(identifier.span));
            }
            if state.helper_effects.get(name).is_some_and(|summary| {
                !summary.effects.is_empty()
                    || !summary.provider_bindings.is_empty()
                    || summary.unknown_provider_usage
            }) {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_MIDDLEWARE",
                    "named middleware cannot capture provider handles in compiler input",
                )
                .with_path(path)
                .with_span(identifier.span));
            }
            Ok(FrameworkMiddleware {
                kind: FrameworkMiddlewareKind::User,
                source: name.to_string(),
                sequence: state.next_middleware_sequence,
                source_name: source_name.to_string(),
                source_text: source.to_string(),
                span: identifier.span,
            })
        }
        _ => Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_MIDDLEWARE",
            "middleware must be an inline function or a top-level function identifier",
        )
        .with_path(path)
        .with_span(argument_span(argument).unwrap_or_else(|| Span::new(0, 0)))),
    }
}

fn validate_middleware_arrow(
    path: &Path,
    function: &oxc_ast::ast::ArrowFunctionExpression<'_>,
    state: &AppState,
) -> Result<(), Diagnostic> {
    if function.params.rest.is_some()
        || function.params.items.len() > 2
        || arrow_has_typescript_syntax(function)
    {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_MIDDLEWARE",
            "middleware functions may declare at most ctx and next parameters without TypeScript syntax",
        )
        .with_path(path)
        .with_span(function.span));
    }
    reject_middleware_captures(
        path,
        function.span,
        middleware_arrow_free_identifiers(function),
        state,
    )
}

fn validate_middleware_function(
    path: &Path,
    function: &oxc_ast::ast::Function<'_>,
    state: &AppState,
) -> Result<(), Diagnostic> {
    if function.generator
        || function.body.is_none()
        || function.params.rest.is_some()
        || function.params.items.len() > 2
        || function_has_typescript_syntax(function)
    {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_MIDDLEWARE",
            "middleware functions may declare at most ctx and next parameters without TypeScript syntax",
        )
        .with_path(path)
        .with_span(function.span));
    }
    reject_middleware_captures(
        path,
        function.span,
        middleware_function_free_identifiers(function),
        state,
    )
}

fn reject_middleware_captures(
    path: &Path,
    span: Span,
    mut free_identifiers: BTreeSet<String>,
    state: &AppState,
) -> Result<(), Diagnostic> {
    free_identifiers.remove("Results");
    free_identifiers.remove("Promise");
    for name in state.helper_sources.keys() {
        free_identifiers.remove(name);
    }
    if free_identifiers.is_empty() {
        return Ok(());
    }
    let identifier = free_identifiers
        .iter()
        .next()
        .map(String::as_str)
        .unwrap_or("");
    Err(Diagnostic::new(
        "SLOPPYC_E_UNSUPPORTED_MIDDLEWARE",
        format!("middleware captures unsupported identifier '{identifier}'"),
    )
    .with_path(path)
    .with_span(span)
    .with_hint("Use middleware that depends on ctx, next, Results, local values, or emitted top-level helpers."))
}

fn middleware_arrow_free_identifiers(
    function: &oxc_ast::ast::ArrowFunctionExpression<'_>,
) -> BTreeSet<String> {
    let mut scope = BTreeSet::new();
    collect_formal_parameter_bindings(&function.params, &mut scope);
    collect_function_body_bindings(&function.body.statements, &mut scope);
    let mut free = BTreeSet::new();
    collect_statement_list_identifier_references(&function.body.statements, &scope, &mut free);
    free
}

fn middleware_function_free_identifiers(function: &oxc_ast::ast::Function<'_>) -> BTreeSet<String> {
    let mut scope = BTreeSet::new();
    collect_formal_parameter_bindings(&function.params, &mut scope);
    if let Some(identifier) = &function.id {
        scope.insert(identifier.name.as_str().to_string());
    }
    let Some(body) = &function.body else {
        return BTreeSet::new();
    };
    collect_function_body_bindings(&body.statements, &mut scope);
    let mut free = BTreeSet::new();
    collect_statement_list_identifier_references(&body.statements, &scope, &mut free);
    free
}

pub(super) fn app_use_cors_call(
    path: &Path,
    expression: &Expression<'_>,
    state: &mut AppState,
) -> Result<bool, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(false);
    };
    let Some((receiver, property)) = static_member_name(&call.callee) else {
        return Ok(false);
    };
    if property != "useCors" || !state.app_vars.contains(receiver) {
        return Ok(false);
    }
    if call.arguments.len() != 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CORS",
            "app.useCors requires one literal policy for compiler extraction",
        )
        .with_path(path)
        .with_span(call.span));
    }
    let Some(argument) = call.arguments.first() else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CORS",
            "app.useCors requires one literal policy for compiler extraction",
        )
        .with_path(path)
        .with_span(call.span));
    };
    let Some(object) = object_argument(argument) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CORS",
            "dynamic CORS policies are not supported by compiler extraction",
        )
        .with_path(path)
        .with_span(argument_span(argument).unwrap_or(call.span)));
    };
    state.cors_policy = Some(cors_policy_from_object(path, object)?);
    Ok(true)
}

fn cors_policy_from_object(
    path: &Path,
    object: &oxc_ast::ast::ObjectExpression<'_>,
) -> Result<CorsPolicy, Diagnostic> {
    let mut origins: Option<Vec<String>> = None;
    let mut methods: Option<Vec<String>> = None;
    let mut headers: Option<Vec<String>> = None;
    let mut exposed_headers: Option<Vec<String>> = None;
    let mut credentials = false;
    let mut max_age_seconds: Option<u64> = None;

    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_CORS",
                "CORS policies must use literal properties",
            )
            .with_path(path)
            .with_span(object.span));
        };
        if property.kind != PropertyKind::Init || property.method || property.computed {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_CORS",
                "CORS policies must use simple literal properties",
            )
            .with_path(path)
            .with_span(property.span));
        }
        let Some(name) = property_key_name(&property.key) else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_CORS",
                "CORS policy option names must be literal",
            )
            .with_path(path)
            .with_span(property.span));
        };
        match name {
            "origins" | "origin" => {
                origins = Some(cors_string_list(path, &property.value, "origins", false)?);
            }
            "methods" => {
                methods = Some(cors_methods(path, &property.value)?);
            }
            "headers" | "allowHeaders" => {
                headers = Some(cors_string_list(path, &property.value, "headers", true)?);
            }
            "exposedHeaders" | "exposeHeaders" => {
                exposed_headers = Some(cors_string_list(
                    path,
                    &property.value,
                    "exposedHeaders",
                    false,
                )?);
            }
            "credentials" => {
                let Expression::BooleanLiteral(value) = &property.value else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_CORS",
                        "CORS credentials must be a boolean literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                };
                credentials = value.value;
            }
            "maxAgeSeconds" | "maxAge" => {
                let Expression::NumericLiteral(value) = &property.value else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_CORS",
                        "CORS maxAgeSeconds must be a non-negative integer literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                };
                if value.value < 0.0 || value.value.fract() != 0.0 {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_CORS",
                        "CORS maxAgeSeconds must be a non-negative integer literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                }
                max_age_seconds = Some(value.value as u64);
            }
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_CORS",
                    format!("unsupported CORS policy option '{name}'"),
                )
                .with_path(path)
                .with_span(property.span));
            }
        }
    }

    let origins = origins.unwrap_or_default();
    if origins.is_empty() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CORS",
            "CORS origins must include at least one origin or '*'",
        )
        .with_path(path)
        .with_span(object.span));
    }
    let allow_any_origin = origins.iter().any(|origin| origin == "*");
    if allow_any_origin && origins.len() != 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CORS",
            "CORS '*' origin cannot be combined with other origins",
        )
        .with_path(path)
        .with_span(object.span));
    }
    if allow_any_origin && credentials {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CORS",
            "CORS credentials require explicit origins",
        )
        .with_path(path)
        .with_span(object.span));
    }
    Ok(CorsPolicy {
        origins,
        methods: methods.unwrap_or_default(),
        headers: headers.unwrap_or_default(),
        exposed_headers: exposed_headers.unwrap_or_default(),
        credentials,
        max_age_seconds,
    })
}

fn cors_string_list(
    path: &Path,
    expression: &Expression<'_>,
    subject: &str,
    lower: bool,
) -> Result<Vec<String>, Diagnostic> {
    let values = match expression {
        Expression::StringLiteral(value) => vec![value.value.as_str().to_string()],
        Expression::ArrayExpression(array) => {
            let mut values = Vec::new();
            for element in &array.elements {
                let ArrayExpressionElement::StringLiteral(value) = element else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_CORS",
                        format!("CORS {subject} entries must be string literals"),
                    )
                    .with_path(path)
                    .with_span(array.span));
                };
                values.push(value.value.as_str().to_string());
            }
            values
        }
        _ => {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_CORS",
                format!("CORS {subject} must be a string literal or string literal array"),
            )
            .with_path(path)
            .with_span(expression.span()));
        }
    };
    let mut normalized = Vec::new();
    for value in values {
        if value.is_empty() || value.bytes().any(|byte| byte < 0x20 || byte == 0x7f) {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_CORS",
                format!(
                    "CORS {subject} entries must be non-empty strings without control characters"
                ),
            )
            .with_path(path)
            .with_span(expression.span()));
        }
        let value = if lower { value.to_lowercase() } else { value };
        if matches!(subject, "headers" | "exposedHeaders") && !compiler_header_token(&value) {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_CORS",
                format!("CORS {subject} entries must be HTTP token strings"),
            )
            .with_path(path)
            .with_span(expression.span()));
        }
        if !normalized.contains(&value) {
            normalized.push(value);
        }
    }
    Ok(normalized)
}

fn cors_methods(path: &Path, expression: &Expression<'_>) -> Result<Vec<String>, Diagnostic> {
    let methods = cors_string_list(path, expression, "methods", false)?
        .into_iter()
        .map(|method| method.to_uppercase())
        .collect::<Vec<_>>();
    for method in &methods {
        if !matches!(
            method.as_str(),
            "GET" | "POST" | "PUT" | "PATCH" | "DELETE" | "HEAD" | "OPTIONS"
        ) {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_CORS",
                "CORS methods must be supported HTTP methods",
            )
            .with_path(path)
            .with_span(expression.span()));
        }
    }
    Ok(methods.into_iter().fold(Vec::new(), |mut acc, method| {
        if !acc.contains(&method) {
            acc.push(method);
        }
        acc
    }))
}

fn compiler_header_name_allowed(name: &str) -> bool {
    compiler_header_token(name)
        && !matches!(
            name.to_ascii_lowercase().as_str(),
            "connection" | "content-type" | "content-length" | "transfer-encoding" | "keep-alive"
        )
}

fn compiler_header_token(name: &str) -> bool {
    !name.is_empty()
        && name.bytes().all(|byte| {
            byte.is_ascii_alphanumeric()
                || matches!(
                    byte,
                    b'!' | b'#'
                        | b'$'
                        | b'%'
                        | b'&'
                        | b'\''
                        | b'*'
                        | b'+'
                        | b'-'
                        | b'.'
                        | b'^'
                        | b'_'
                        | b'`'
                        | b'|'
                        | b'~'
                )
        })
}

pub(super) fn app_map_controller_call(
    path: &Path,
    source: &str,
    source_name: &str,
    expression: &Expression<'_>,
    state: &mut AppState,
) -> Result<Option<Vec<Route>>, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(None);
    };
    let Some((receiver, property)) = static_member_name(&call.callee) else {
        return Ok(None);
    };
    if !matches!(property, "mapController" | "controller") || !state.app_vars.contains(receiver) {
        return Ok(None);
    }
    if !matches!(call.arguments.len(), 2 | 3) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
            "app.mapController requires a literal prefix, controller class, and optional mapper callback",
        )
        .with_path(path)
        .with_span(call.span));
    }
    let Some(prefix) = call.arguments.first().and_then(string_argument) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
            "controller prefix must be a string literal",
        )
        .with_path(path)
        .with_span(
            call.arguments
                .first()
                .and_then(argument_span)
                .unwrap_or(call.span),
        ));
    };
    let Some(controller_name) = call.arguments.get(1).and_then(argument_identifier) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
            "controller mapping requires a static controller class identifier",
        )
        .with_path(path)
        .with_span(
            call.arguments
                .get(1)
                .and_then(argument_span)
                .unwrap_or(call.span),
        ));
    };
    let Some(controller) = state.controllers.get(controller_name).cloned() else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
            "controller mapping requires a top-level controller class",
        )
        .with_path(path)
        .with_span(
            call.arguments
                .get(1)
                .and_then(argument_span)
                .unwrap_or(call.span),
        ));
    };
    let Some(configure_argument) = call.arguments.get(2) else {
        return Ok(Some(Vec::new()));
    };
    let (mapper_name, statements, configure_span) =
        controller_mapper_callback(path, configure_argument)?;
    let mut routes = Vec::new();
    for statement in statements {
        let Statement::ExpressionStatement(statement) = statement else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
                "controller mapper callback supports literal mapper route calls only",
            )
            .with_path(path)
            .with_span(statement.span()));
        };
        let (route_expr, fluent_metadata) = route_metadata_chain(&statement.expression)
            .map_err(|diagnostic| diagnostic.with_path(path))?;
        let Some((method, pattern, action, route_metadata)) =
            controller_route_parts(route_expr, mapper_name)?
        else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
                "controller mapper callback supports mapper.get/post/put/patch/delete calls only",
            )
            .with_path(path)
            .with_span(statement.span));
        };
        let Some(controller_method) = controller.methods.get(action).cloned() else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
                format!("controller action '{action}' must name a prototype method"),
            )
            .with_path(path)
            .with_span(route_expr.span()));
        };
        let full_pattern = join_route_patterns(prefix, pattern);
        let normalized_pattern = normalize_framework_route_pattern(&full_pattern);
        if !route_pattern_supported(&normalized_pattern) {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_ROUTE_PATTERN",
                "controller route pattern is outside the Plan v1 alpha route syntax",
            )
            .with_path(path)
            .with_span(statement.span)
            .with_hint("Use '/', static segments, {name}, {name:str}, or {name:int}."));
        }
        let contract_metadata = merged_route_metadata(&route_metadata, &fluent_metadata);
        let tags = contract_metadata.tags.clone();
        let middleware = route_middleware_metadata(&state.middleware);
        let cors = state.cors_policy.clone();
        let handler_source = source_slice(&controller.source_text, controller_method.span)
            .unwrap_or_else(|| format!("{controller_name}.{action}"));
        let emitted_source = controller_handler_source(controller_name, action);
        let mut handler = Handler {
            source: handler_source.clone(),
            emitted_source,
            span: controller_method.span,
            requires_results_import: controller_method.requires_results_import,
            is_async: true,
            runtime_deferred: false,
            source_name: controller.source_name.clone(),
            source_text: controller.source_text.clone(),
            source_map_line_offset: 0,
            source_map_column_offset: 0,
            bindings: Vec::new(),
            response: None,
            responses: Vec::new(),
            effects: Vec::new(),
            schema_metadata_conflict: false,
        };
        apply_route_schema_metadata(
            path,
            statement.span,
            &state.schema_names,
            &fluent_metadata,
            &mut handler,
        )?;
        let auth = contract_metadata.auth.clone();
        if let Some(requirement) = &auth {
            handler.emitted_source = wrap_handler_with_auth(&handler.emitted_source, requirement);
            handler.is_async = true;
        }
        handler.emitted_source = wrap_handler_with_framework_pipeline(
            &handler.emitted_source,
            &state.middleware,
            cors.as_ref(),
        );
        handler.is_async = true;
        routes.push(Route {
            method,
            kind: "http",
            websocket: None,
            realtime: None,
            framework_path: (normalized_pattern != full_pattern).then_some(full_pattern),
            pattern: normalized_pattern,
            name: contract_metadata.name.clone(),
            tags,
            summary: contract_metadata.summary.clone(),
            description: contract_metadata.description.clone(),
            deprecated: contract_metadata.deprecated.clone(),
            consumes: contract_metadata.consumes.clone(),
            produces: contract_metadata.produces.clone(),
            headers: contract_metadata.headers.clone(),
            query_schema: contract_metadata.query_schema.clone(),
            params_schema: contract_metadata.params_schema.clone(),
            openapi_override: contract_metadata.openapi_override.clone(),
            output_cache: contract_metadata.output_cache.clone(),
            cache_headers: contract_metadata.cache_headers.clone(),
            rate_limits: contract_metadata.rate_limits.clone(),
            docs: None,
            health: None,
            middleware,
            auth,
            cors: cors.as_ref().map(cors_policy_metadata),
            cors_preflight: false,
            span: statement.span,
            source_path: path.to_path_buf(),
            source_name: source_name.to_string(),
            source: source.to_string(),
            module: None,
            handler,
        });
    }
    if routes.is_empty() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
            "controller mapper callback must register at least one route",
        )
        .with_path(path)
        .with_span(configure_span));
    }
    Ok(Some(routes))
}

fn controller_mapper_callback<'a>(
    path: &Path,
    argument: &'a Argument<'a>,
) -> Result<(&'a str, &'a [Statement<'a>], Span), Diagnostic> {
    match argument {
        Argument::ArrowFunctionExpression(function) => {
            if function.params.items.len() != 1 || function.params.rest.is_some() {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
                    "controller mapper callback must declare one mapper parameter",
                )
                .with_path(path)
                .with_span(function.span));
            }
            let Some(mapper) = function
                .params
                .items
                .first()
                .and_then(|parameter| binding_identifier(&parameter.pattern))
            else {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
                    "controller mapper parameter must be a simple identifier",
                )
                .with_path(path)
                .with_span(function.span));
            };
            Ok((mapper, &function.body.statements, function.span))
        }
        Argument::FunctionExpression(function) => {
            if function.params.items.len() != 1 || function.params.rest.is_some() {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
                    "controller mapper callback must declare one mapper parameter",
                )
                .with_path(path)
                .with_span(function.span));
            }
            let Some(mapper) = function
                .params
                .items
                .first()
                .and_then(|parameter| binding_identifier(&parameter.pattern))
            else {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
                    "controller mapper parameter must be a simple identifier",
                )
                .with_path(path)
                .with_span(function.span));
            };
            let Some(body) = &function.body else {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
                    "controller mapper callback must have a body",
                )
                .with_path(path)
                .with_span(function.span));
            };
            Ok((mapper, &body.statements, function.span))
        }
        _ => Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
            "controller mapper must be an inline function",
        )
        .with_path(path)
        .with_span(argument_span(argument).unwrap_or_else(|| Span::new(0, 0)))),
    }
}

fn controller_route_parts<'a>(
    expression: &'a Expression<'a>,
    mapper_name: &str,
) -> Result<Option<(&'static str, &'a str, &'a str, RouteMetadata)>, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(None);
    };
    let Some((receiver, property)) = static_member_name(&call.callee) else {
        return Ok(None);
    };
    if receiver != mapper_name {
        return Ok(None);
    }
    let method = route_method_from_property(property);
    let Some(method) = method else {
        return Ok(None);
    };
    if !matches!(call.arguments.len(), 2 | 3) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
            "controller mapper routes require pattern and action literals",
        )
        .with_span(call.span));
    }
    let Some(pattern) = call.arguments.first().and_then(string_argument) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
            "controller mapper route pattern must be a string literal",
        )
        .with_span(call.span));
    };
    let Some(action) = call.arguments.get(1).and_then(string_argument) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
            "controller mapper action must be a string literal",
        )
        .with_span(call.span));
    };
    let metadata = if call.arguments.len() == 3 {
        route_metadata_from_options_argument(&call.arguments[2])?
    } else {
        RouteMetadata::default()
    };
    Ok(Some((method, pattern, action, metadata)))
}

fn controller_handler_source(controller_name: &str, action: &str) -> String {
    let action = serde_json::to_string(action).unwrap_or_else(|_| "\"\"".to_string());
    format!(
        "async function(ctx) {{ const __sloppy_scope = __sloppy_framework_services.createScope(ctx); ctx.services = __sloppy_scope; try {{ const __sloppy_tokens = {controller_name}.inject ?? {controller_name}.dependencies ?? []; if (!Array.isArray(__sloppy_tokens)) {{ throw new TypeError(\"Sloppy controller inject metadata must be an array when provided.\"); }} const __sloppy_controller = new {controller_name}(...__sloppy_tokens.map((token) => __sloppy_scope.get(token))); return await __sloppy_controller[{action}](ctx); }} finally {{ await __sloppy_scope.dispose(); }} }}"
    )
}

pub(super) fn route_middleware_metadata(
    middleware: &[FrameworkMiddleware],
) -> Vec<RouteMiddlewareMetadata> {
    middleware
        .iter()
        .map(|entry| RouteMiddlewareMetadata {
            kind: match &entry.kind {
                FrameworkMiddlewareKind::User => "user".to_string(),
                FrameworkMiddlewareKind::RequestId => "requestId".to_string(),
                FrameworkMiddlewareKind::RequestLogging => "requestLogging".to_string(),
            },
            source: entry.source.clone(),
            sequence: entry.sequence,
            source_name: entry.source_name.clone(),
            source_text: entry.source_text.clone(),
            span: entry.span,
        })
        .collect()
}

pub(super) fn cors_policy_metadata(policy: &CorsPolicy) -> CorsPolicyMetadata {
    CorsPolicyMetadata {
        origins: policy.origins.clone(),
        methods: policy.methods.clone(),
        headers: policy.headers.clone(),
        exposed_headers: policy.exposed_headers.clone(),
        credentials: policy.credentials,
        max_age_seconds: policy.max_age_seconds,
    }
}

fn cors_policy_json(policy: &CorsPolicy) -> String {
    serde_json::to_string(&json!({
        "origins": policy.origins,
        "methods": policy.methods,
        "headers": policy.headers,
        "exposedHeaders": policy.exposed_headers,
        "credentials": policy.credentials,
        "maxAgeSeconds": policy.max_age_seconds
    }))
    .unwrap_or_else(|_| "null".to_string())
}

pub(super) fn auth_schemes_json(auth: &AuthMetadata) -> String {
    serde_json::to_string(
        &auth
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
                    "issuer": issuer,
                    "audience": audience,
                    "clockSkewSeconds": clock_skew_seconds,
                    "secretConfigKey": secret_config_key,
                    "secretEnvKey": secret_config_key.as_deref().map(config_key_to_env_name)
                }),
                AuthSchemeMetadata::ApiKey {
                    name,
                    header,
                    config_key,
                } => json!({
                    "kind": "apiKey",
                    "name": name,
                    "header": header,
                    "configKey": config_key,
                    "configEnvKey": config_key.as_deref().map(config_key_to_env_name)
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
                    "secretConfigKey": secret_config_key,
                    "secretEnvKey": secret_config_key.as_deref().map(config_key_to_env_name)
                }),
            })
            .collect::<Vec<_>>(),
    )
    .unwrap_or_else(|_| "[]".to_string())
}

pub(super) fn auth_policies_js(auth: &AuthMetadata) -> String {
    let entries = auth
        .policies
        .iter()
        .filter_map(|policy| {
            let source = policy.source.as_ref()?;
            let name = serde_json::to_string(&policy.name).ok()?;
            Some(format!("[{name}, __sloppy_auth_policy_function({source})]"))
        })
        .collect::<Vec<_>>()
        .join(", ");
    format!("new Map([{entries}])")
}

pub(super) fn wrap_handler_with_framework_pipeline(
    handler_source: &str,
    middleware: &[FrameworkMiddleware],
    cors: Option<&CorsPolicy>,
) -> String {
    if middleware.is_empty() && cors.is_none() {
        return handler_source.to_string();
    }

    let terminal = format!("() => ({handler_source})(ctx)");
    let invocation = if middleware.is_empty() {
        terminal
    } else {
        let middleware_sources = middleware
            .iter()
            .map(|entry| entry.source.clone())
            .collect::<Vec<_>>()
            .join(", ");
        format!("() => __sloppy_run_middleware(ctx, [{middleware_sources}], {terminal})")
    };

    if let Some(policy) = cors {
        let policy = cors_policy_json(policy);
        format!(
            "async function(ctx) {{ return await __sloppy_finish_cors(await ({invocation})(), {policy}, ctx); }}"
        )
    } else {
        format!("async function(ctx) {{ return await ({invocation})(); }}")
    }
}

pub(super) fn append_cors_preflight_routes(
    path: &Path,
    routes: &mut Vec<Route>,
) -> Result<(), Diagnostic> {
    let mut preflights = BTreeMap::<String, (CorsPolicyMetadata, Vec<String>, usize)>::new();
    for (index, route) in routes.iter().enumerate() {
        if route.cors_preflight {
            continue;
        }
        let Some(cors) = &route.cors else {
            continue;
        };
        let entry = preflights
            .entry(route.pattern.clone())
            .or_insert_with(|| (cors.clone(), Vec::new(), index));
        if !cors_policy_metadata_equal(&entry.0, cors) {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_CORS",
                "multiple CORS policies for the same route pattern are not supported by compiler extraction",
            )
            .with_path(path)
            .with_span(route.span));
        }
        if !entry.1.iter().any(|method| method == route.method) {
            entry.1.push(route.method.to_string());
        }
        entry.2 = index;
    }

    for (pattern, (policy, methods, route_index)) in preflights {
        let Some(route) = routes.get(route_index).cloned() else {
            continue;
        };
        let policy_json = serde_json::to_string(&json!({
            "origins": policy.origins,
            "methods": policy.methods,
            "headers": policy.headers,
            "exposedHeaders": policy.exposed_headers,
            "credentials": policy.credentials,
            "maxAgeSeconds": policy.max_age_seconds
        }))
        .unwrap_or_else(|_| "null".to_string());
        let methods_json = serde_json::to_string(&methods).unwrap_or_else(|_| "[]".to_string());
        let terminal_source = format!(
            "function(ctx) {{ return __sloppy_cors_preflight({policy_json}, {methods_json}, ctx); }}"
        );
        let handler_source = if route.middleware.is_empty() {
            format!("async function(ctx) {{ return await ({terminal_source})(ctx); }}")
        } else {
            let middleware_sources = route
                .middleware
                .iter()
                .map(|entry| entry.source.clone())
                .collect::<Vec<_>>()
                .join(", ");
            format!(
                "async function(ctx) {{ return await __sloppy_run_middleware(ctx, [{middleware_sources}], () => ({terminal_source})(ctx)); }}"
            )
        };
        routes.push(Route {
            method: "OPTIONS",
            kind: "http",
            websocket: None,
            realtime: None,
            framework_path: None,
            pattern,
            name: None,
            tags: Vec::new(),
            summary: None,
            description: None,
            deprecated: None,
            consumes: Vec::new(),
            produces: Vec::new(),
            headers: Vec::new(),
            query_schema: None,
            params_schema: None,
            openapi_override: None,
            output_cache: None,
            cache_headers: None,
            rate_limits: Vec::new(),
            docs: None,
            health: None,
            middleware: route.middleware.clone(),
            auth: None,
            cors: Some(policy),
            cors_preflight: true,
            span: route.span,
            source_path: route.source_path.clone(),
            source_name: route.source_name.clone(),
            source: route.source.clone(),
            module: route.module.clone(),
            handler: Handler {
                source: "app.useCors(...)".to_string(),
                emitted_source: handler_source,
                span: route.handler.span,
                requires_results_import: false,
                is_async: true,
                runtime_deferred: false,
                source_name: route.handler.source_name.clone(),
                source_text: route.handler.source_text.clone(),
                source_map_line_offset: 0,
                source_map_column_offset: 0,
                bindings: Vec::new(),
                response: Some(ResponseMetadata {
                    helper: "status".to_string(),
                    status: 204,
                    kind: "empty".to_string(),
                    body_schema: None,
                    native_body: None,
                    source_name: None,
                    source_text: None,
                    span: None,
                    partial: false,
                }),
                responses: vec![ResponseMetadata {
                    helper: "status".to_string(),
                    status: 204,
                    kind: "empty".to_string(),
                    body_schema: None,
                    native_body: None,
                    source_name: None,
                    source_text: None,
                    span: None,
                    partial: false,
                }],
                effects: Vec::new(),
                schema_metadata_conflict: false,
            },
        });
    }
    Ok(())
}

fn cors_policy_metadata_equal(left: &CorsPolicyMetadata, right: &CorsPolicyMetadata) -> bool {
    left.origins == right.origins
        && left.methods == right.methods
        && left.headers == right.headers
        && left.exposed_headers == right.exposed_headers
        && left.credentials == right.credentials
        && left.max_age_seconds == right.max_age_seconds
}

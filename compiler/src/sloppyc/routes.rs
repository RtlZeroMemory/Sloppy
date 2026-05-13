// Route call and route metadata extraction.
use super::*;

#[allow(clippy::too_many_arguments)]
pub(super) fn route_call<'a>(
    expression: &'a Expression<'a>,
    source: &str,
    source_name: &str,
    allow_data_handler_body: bool,
    schema_names: &BTreeSet<String>,
    provider_bindings: &BTreeMap<String, ProviderBinding>,
    helper_effects: &BTreeMap<String, FunctionEffectSummary>,
    static_strings: &StaticStringEnv,
) -> Result<Option<ExtractedRouteCall<'a>>, Diagnostic> {
    let Some((receiver, method, kind, pattern, _metadata, handler_arg)) =
        route_call_parts(expression, source, static_strings)?
    else {
        return Ok(None);
    };
    let context = HandlerExtractionContext {
        route_pattern: &pattern,
        route_kind: kind,
        source,
        source_name,
        allow_data_handler_body,
        schema_names,
        provider_bindings,
        helper_effects,
    };
    let Some(handler) = handler_from_argument(handler_arg, &context) else {
        return Ok(None);
    };
    Ok(Some((receiver, method, kind, pattern, handler)))
}

pub(super) fn route_call_parts<'a>(
    expression: &'a Expression<'a>,
    source: &str,
    static_strings: &StaticStringEnv,
) -> Result<Option<RouteCallParts<'a>>, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(None);
    };
    let Some((receiver, property)) = static_member_name(&call.callee) else {
        return Ok(None);
    };
    let Some(method) = route_method_from_property(property) else {
        return Ok(None);
    };
    let kind = crate::slop_dsl::route_kind_from_property(property).unwrap_or("http");
    if property == "realtime" && !matches!(call.arguments.len(), 3 | 4) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_INVALID_REALTIME_ROUTE_ARGS",
            "Realtime routes must be declared as realtime(path, channel, handler[, options])",
        )
        .with_span(call.span));
    }
    if property != "realtime" && !matches!(call.arguments.len(), 2 | 3) {
        return Ok(None);
    }

    let Some(pattern) = call
        .arguments
        .first()
        .and_then(|argument| eval_string_argument(argument, static_strings))
    else {
        return Ok(None);
    };
    let (metadata, handler_arg) = if property == "realtime" {
        let channel_source =
            argument_span(&call.arguments[1]).map(|span| span_source(source, span).to_string());
        let realtime_options_source = if call.arguments.len() == 4 {
            argument_span(&call.arguments[3]).map(|span| span_source(source, span).to_string())
        } else {
            None
        };
        let metadata = RouteMetadata {
            websocket: if call.arguments.len() == 4 {
                Some(realtime_websocket_options_from_argument(
                    &call.arguments[3],
                )?)
            } else {
                None
            },
            realtime_channel_source: channel_source,
            realtime_options_source,
            ..Default::default()
        };
        (metadata, &call.arguments[2])
    } else if kind == "websocket" && call.arguments.len() == 3 {
        if route_handler_argument(&call.arguments[1]) {
            let metadata = RouteMetadata {
                websocket: Some(websocket_options_from_argument(&call.arguments[2])?),
                ..Default::default()
            };
            (metadata, &call.arguments[1])
        } else {
            let metadata = RouteMetadata {
                websocket: Some(websocket_options_from_argument(&call.arguments[1])?),
                ..Default::default()
            };
            (metadata, &call.arguments[2])
        }
    } else if kind == "websocket" {
        let metadata = RouteMetadata {
            websocket: Some(default_websocket_options()),
            ..Default::default()
        };
        (metadata, &call.arguments[1])
    } else if call.arguments.len() == 3 {
        (
            route_metadata_from_options_argument(&call.arguments[1])?,
            &call.arguments[2],
        )
    } else {
        (RouteMetadata::default(), &call.arguments[1])
    };
    Ok(Some((
        receiver,
        method,
        kind,
        pattern,
        metadata,
        handler_arg,
    )))
}

fn route_handler_argument(argument: &Argument<'_>) -> bool {
    matches!(
        argument,
        Argument::ArrowFunctionExpression(_) | Argument::FunctionExpression(_)
    )
}

fn default_websocket_options() -> WebSocketRouteOptionsMetadata {
    WebSocketRouteOptionsMetadata {
        protocols: Vec::new(),
        origins: None,
        max_message_bytes: Some(64 * 1024),
        max_send_queue_bytes: Some(1024 * 1024),
        heartbeat_ms: None,
        idle_timeout_ms: None,
        close_timeout_ms: Some(5000),
        slow_client_policy: Some("error".to_string()),
        compression: Some(false),
    }
}

fn merge_websocket_options(
    existing: &mut WebSocketRouteOptionsMetadata,
    incoming: WebSocketRouteOptionsMetadata,
) {
    if !incoming.protocols.is_empty() {
        existing.protocols = incoming.protocols;
    }
    if incoming.origins.is_some() {
        existing.origins = incoming.origins;
    }
    if incoming.max_message_bytes.is_some() {
        existing.max_message_bytes = incoming.max_message_bytes;
    }
    if incoming.max_send_queue_bytes.is_some() {
        existing.max_send_queue_bytes = incoming.max_send_queue_bytes;
    }
    if incoming.heartbeat_ms.is_some() {
        existing.heartbeat_ms = incoming.heartbeat_ms;
    }
    if incoming.idle_timeout_ms.is_some() {
        existing.idle_timeout_ms = incoming.idle_timeout_ms;
    }
    if incoming.close_timeout_ms.is_some() {
        existing.close_timeout_ms = incoming.close_timeout_ms;
    }
    if incoming.slow_client_policy.is_some() {
        existing.slow_client_policy = incoming.slow_client_policy;
    }
    if incoming.compression.is_some() {
        existing.compression = incoming.compression;
    }
}

fn websocket_options_from_argument(
    argument: &Argument<'_>,
) -> Result<WebSocketRouteOptionsMetadata, Diagnostic> {
    let Some(object) = object_argument(argument) else {
        let diagnostic = Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_WEBSOCKET_OPTIONS",
            "WebSocket options must be a static object literal",
        );
        return Err(with_argument_span(diagnostic, argument));
    };
    let mut options = default_websocket_options();
    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_WEBSOCKET_OPTIONS",
                "WebSocket options must use literal properties",
            )
            .with_span(object.span));
        };
        if property.computed {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_WEBSOCKET_OPTIONS",
                "WebSocket option names must be literal",
            )
            .with_span(property.span));
        }
        let Some(key) = property_key_name(&property.key) else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_WEBSOCKET_OPTIONS",
                "WebSocket option names must be literal",
            )
            .with_span(property.span));
        };
        match key {
            "protocols" => {
                options.protocols = websocket_protocols_from_expression(&property.value)?;
            }
            "origins" => {
                options.origins = Some(websocket_origins_from_expression(&property.value)?);
            }
            "maxMessageBytes" => {
                options.max_message_bytes = Some(websocket_positive_integer(&property.value, key)?);
            }
            "maxSendQueueBytes" => {
                options.max_send_queue_bytes =
                    Some(websocket_positive_integer(&property.value, key)?);
            }
            "heartbeatMs" => {
                options.heartbeat_ms = Some(websocket_positive_integer(&property.value, key)?);
            }
            "idleTimeoutMs" => {
                options.idle_timeout_ms = Some(websocket_positive_integer(&property.value, key)?);
            }
            "closeTimeoutMs" => {
                options.close_timeout_ms = Some(websocket_positive_integer(&property.value, key)?);
            }
            "slowClientPolicy" => {
                let Some(policy) = expression_string_literal(&property.value) else {
                    return Err(websocket_options_diagnostic(
                        "WebSocket slowClientPolicy must be 'error' or 'close'",
                        property.value.span(),
                    ));
                };
                if policy != "error" && policy != "close" {
                    return Err(websocket_options_diagnostic(
                        "WebSocket slowClientPolicy must be 'error' or 'close'",
                        property.value.span(),
                    ));
                }
                options.slow_client_policy = Some(policy.to_string());
            }
            "compression" => {
                if !matches!(&property.value, Expression::BooleanLiteral(value) if !value.value) {
                    return Err(websocket_options_diagnostic(
                        "WebSocket compression must be the literal false",
                        property.value.span(),
                    ));
                }
                options.compression = Some(false);
            }
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_WEBSOCKET_OPTIONS",
                    format!("unsupported WebSocket option '{key}'"),
                )
                .with_span(property.span));
            }
        }
    }
    Ok(options)
}

fn realtime_websocket_options_from_argument(
    argument: &Argument<'_>,
) -> Result<WebSocketRouteOptionsMetadata, Diagnostic> {
    let Some(object) = object_argument(argument) else {
        let diagnostic = Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_REALTIME_OPTIONS",
            "Realtime route options must be a static object literal",
        );
        return Err(with_argument_span(diagnostic, argument));
    };
    let mut options = default_websocket_options();
    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_REALTIME_OPTIONS",
                "Realtime route options must use literal properties",
            )
            .with_span(object.span));
        };
        if property.computed {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_REALTIME_OPTIONS",
                "Realtime route option names must be literal",
            )
            .with_span(property.span));
        }
        let Some(key) = property_key_name(&property.key) else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_REALTIME_OPTIONS",
                "Realtime route option names must be literal",
            )
            .with_span(property.span));
        };
        match key {
            "websocket" => {
                let Expression::ObjectExpression(_) = &property.value else {
                    return Err(websocket_options_diagnostic(
                        "Realtime websocket option must be a static object literal",
                        property.value.span(),
                    ));
                };
                let websocket = websocket_options_from_expression_object(&property.value)?;
                merge_websocket_options(&mut options, websocket);
            }
            "protocols" => {
                options.protocols = websocket_protocols_from_expression(&property.value)?;
            }
            "origins" => {
                options.origins = Some(websocket_origins_from_expression(&property.value)?);
            }
            "maxMessageBytes" => {
                options.max_message_bytes = Some(websocket_positive_integer(&property.value, key)?);
            }
            "maxSendQueueBytes" => {
                options.max_send_queue_bytes =
                    Some(websocket_positive_integer(&property.value, key)?);
            }
            "heartbeatMs" => {
                options.heartbeat_ms = Some(websocket_positive_integer(&property.value, key)?);
            }
            "idleTimeoutMs" => {
                options.idle_timeout_ms = Some(websocket_positive_integer(&property.value, key)?);
            }
            "closeTimeoutMs" => {
                options.close_timeout_ms = Some(websocket_positive_integer(&property.value, key)?);
            }
            "slowClientPolicy" => {
                let Some(policy) = expression_string_literal(&property.value) else {
                    return Err(websocket_options_diagnostic(
                        "WebSocket slowClientPolicy must be 'error' or 'close'",
                        property.value.span(),
                    ));
                };
                if policy != "error" && policy != "close" {
                    return Err(websocket_options_diagnostic(
                        "WebSocket slowClientPolicy must be 'error' or 'close'",
                        property.value.span(),
                    ));
                }
                options.slow_client_policy = Some(policy.to_string());
            }
            "compression" => {
                if !matches!(&property.value, Expression::BooleanLiteral(value) if !value.value) {
                    return Err(websocket_options_diagnostic(
                        "WebSocket compression must be the literal false",
                        property.value.span(),
                    ));
                }
                options.compression = Some(false);
            }
            "presence"
            | "backplane"
            | "unknownEventPolicy"
            | "validationFailurePolicy"
            | "handlerErrorPolicy" => {}
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_REALTIME_OPTIONS",
                    format!("unsupported Realtime route option '{key}'"),
                )
                .with_span(property.span));
            }
        }
    }
    Ok(options)
}

fn websocket_options_from_expression_object(
    expression: &Expression<'_>,
) -> Result<WebSocketRouteOptionsMetadata, Diagnostic> {
    let Expression::ObjectExpression(object) = expression else {
        return Err(websocket_options_diagnostic(
            "WebSocket options must be a static object literal",
            expression.span(),
        ));
    };
    let mut options = default_websocket_options();
    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_WEBSOCKET_OPTIONS",
                "WebSocket options must use literal properties",
            )
            .with_span(object.span));
        };
        if property.computed {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_WEBSOCKET_OPTIONS",
                "WebSocket option names must be literal",
            )
            .with_span(property.span));
        }
        let Some(key) = property_key_name(&property.key) else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_WEBSOCKET_OPTIONS",
                "WebSocket option names must be literal",
            )
            .with_span(property.span));
        };
        match key {
            "protocols" => {
                options.protocols = websocket_protocols_from_expression(&property.value)?;
            }
            "origins" => {
                options.origins = Some(websocket_origins_from_expression(&property.value)?);
            }
            "maxMessageBytes" => {
                options.max_message_bytes = Some(websocket_positive_integer(&property.value, key)?);
            }
            "maxSendQueueBytes" => {
                options.max_send_queue_bytes =
                    Some(websocket_positive_integer(&property.value, key)?);
            }
            "heartbeatMs" => {
                options.heartbeat_ms = Some(websocket_positive_integer(&property.value, key)?);
            }
            "idleTimeoutMs" => {
                options.idle_timeout_ms = Some(websocket_positive_integer(&property.value, key)?);
            }
            "closeTimeoutMs" => {
                options.close_timeout_ms = Some(websocket_positive_integer(&property.value, key)?);
            }
            "slowClientPolicy" => {
                let Some(policy) = expression_string_literal(&property.value) else {
                    return Err(websocket_options_diagnostic(
                        "WebSocket slowClientPolicy must be 'error' or 'close'",
                        property.value.span(),
                    ));
                };
                if policy != "error" && policy != "close" {
                    return Err(websocket_options_diagnostic(
                        "WebSocket slowClientPolicy must be 'error' or 'close'",
                        property.value.span(),
                    ));
                }
                options.slow_client_policy = Some(policy.to_string());
            }
            "compression" => {
                if !matches!(&property.value, Expression::BooleanLiteral(value) if !value.value) {
                    return Err(websocket_options_diagnostic(
                        "WebSocket compression must be the literal false",
                        property.value.span(),
                    ));
                }
                options.compression = Some(false);
            }
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_WEBSOCKET_OPTIONS",
                    format!("unsupported WebSocket option '{key}'"),
                )
                .with_span(property.span));
            }
        }
    }
    Ok(options)
}

fn websocket_protocols_from_expression(
    expression: &Expression<'_>,
) -> Result<Vec<String>, Diagnostic> {
    let Expression::ArrayExpression(array) = expression else {
        return Err(websocket_options_diagnostic(
            "WebSocket protocols must be a string literal array",
            expression.span(),
        ));
    };
    let mut protocols = Vec::new();
    for element in &array.elements {
        let ArrayExpressionElement::StringLiteral(value) = element else {
            return Err(websocket_options_diagnostic(
                "WebSocket protocols must contain only string literals",
                array.span,
            ));
        };
        let protocol = value.value.as_str();
        if !websocket_protocol_token_supported(protocol) {
            return Err(websocket_options_diagnostic(
                "WebSocket protocols must be non-empty WebSocket subprotocol tokens",
                value.span,
            ));
        }
        if !protocols.iter().any(|existing| existing == protocol) {
            protocols.push(protocol.to_string());
        }
    }
    Ok(protocols)
}

fn websocket_protocol_token_supported(value: &str) -> bool {
    !value.is_empty()
        && value.bytes().all(|byte| {
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

fn websocket_origins_from_call(
    call: &CallExpression<'_>,
) -> Result<WebSocketOriginsMetadata, Diagnostic> {
    if call.arguments.len() != 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_WEBSOCKET_OPTIONS",
            "allowedOrigins requires exactly one static origin argument",
        )
        .with_span(call.span));
    }
    websocket_origins_from_argument(&call.arguments[0])
}

fn websocket_origins_from_argument(
    argument: &Argument<'_>,
) -> Result<WebSocketOriginsMetadata, Diagnostic> {
    match argument {
        Argument::StringLiteral(value) => websocket_origin_string(value.value.as_str(), value.span),
        Argument::ArrayExpression(array) => websocket_origin_array(array),
        _ => {
            let diagnostic = Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_WEBSOCKET_OPTIONS",
                "WebSocket origins must be '*', a string literal, or a string literal array",
            );
            Err(with_argument_span(diagnostic, argument))
        }
    }
}

fn websocket_origins_from_expression(
    expression: &Expression<'_>,
) -> Result<WebSocketOriginsMetadata, Diagnostic> {
    match expression {
        Expression::StringLiteral(value) => {
            websocket_origin_string(value.value.as_str(), value.span)
        }
        Expression::ArrayExpression(array) => websocket_origin_array(array),
        Expression::ParenthesizedExpression(parenthesized) => {
            websocket_origins_from_expression(&parenthesized.expression)
        }
        _ => Err(websocket_options_diagnostic(
            "WebSocket origins must be '*', a string literal, or a string literal array",
            expression.span(),
        )),
    }
}

fn websocket_origin_string(
    value: &str,
    span: Span,
) -> Result<WebSocketOriginsMetadata, Diagnostic> {
    if value.is_empty() {
        return Err(websocket_options_diagnostic(
            "WebSocket origins must be non-empty strings",
            span,
        ));
    }
    if value == "*" {
        return Ok(WebSocketOriginsMetadata::Any);
    }
    Ok(WebSocketOriginsMetadata::List(vec![value.to_string()]))
}

fn websocket_origin_array(
    array: &ArrayExpression<'_>,
) -> Result<WebSocketOriginsMetadata, Diagnostic> {
    let mut origins = Vec::new();
    for element in &array.elements {
        let ArrayExpressionElement::StringLiteral(value) = element else {
            return Err(websocket_options_diagnostic(
                "WebSocket origins must contain only string literals",
                array.span,
            ));
        };
        let origin = value.value.as_str();
        if origin.is_empty() {
            return Err(websocket_options_diagnostic(
                "WebSocket origins must be non-empty strings",
                value.span,
            ));
        }
        if origin == "*" && array.elements.len() != 1 {
            return Err(websocket_options_diagnostic(
                "WebSocket '*' origin cannot be combined with explicit origins",
                value.span,
            ));
        }
        if !origins.iter().any(|existing| existing == origin) {
            origins.push(origin.to_string());
        }
    }
    if origins.is_empty() {
        return Err(websocket_options_diagnostic(
            "WebSocket origins must not be an empty array",
            array.span,
        ));
    }
    if origins[0] == "*" {
        Ok(WebSocketOriginsMetadata::Any)
    } else {
        Ok(WebSocketOriginsMetadata::List(origins))
    }
}

fn websocket_positive_integer(expression: &Expression<'_>, name: &str) -> Result<u64, Diagnostic> {
    let Some(value) = expression_positive_integer(expression) else {
        return Err(websocket_options_diagnostic(
            format!("WebSocket {name} must be a positive integer literal"),
            expression.span(),
        ));
    };
    Ok(value)
}

fn expression_positive_integer(expression: &Expression<'_>) -> Option<u64> {
    match expression {
        Expression::NumericLiteral(value) => {
            if value.value > 0.0 && value.value.fract() == 0.0 && value.value <= u64::MAX as f64 {
                Some(value.value as u64)
            } else {
                None
            }
        }
        Expression::ParenthesizedExpression(parenthesized) => {
            expression_positive_integer(&parenthesized.expression)
        }
        Expression::BinaryExpression(binary) => {
            let left = expression_positive_integer(&binary.left)?;
            let right = expression_positive_integer(&binary.right)?;
            match binary.operator {
                BinaryOperator::Addition => left.checked_add(right),
                BinaryOperator::Multiplication => left.checked_mul(right),
                _ => None,
            }
        }
        _ => None,
    }
}

fn websocket_options_diagnostic(message: impl Into<String>, span: Span) -> Diagnostic {
    Diagnostic::new("SLOPPYC_E_UNSUPPORTED_WEBSOCKET_OPTIONS", message.into()).with_span(span)
}

pub(super) fn route_method_from_property(property: &str) -> Option<&'static str> {
    crate::slop_dsl::route_method_from_property(property)
}

pub(super) fn route_metadata_chain<'a>(
    expression: &'a Expression<'a>,
) -> Result<(&'a Expression<'a>, RouteMetadata), Diagnostic> {
    let mut current = expression;
    let mut metadata = RouteMetadata::default();
    let mut auth_replaced_by_later_setter = false;
    let mut output_cache_setter_seen = false;
    while let Expression::CallExpression(call) = current {
        let Expression::StaticMemberExpression(member) = &call.callee else {
            break;
        };
        match member.property.name.as_str() {
            "withName" | "name" => {
                if metadata.name.is_none() {
                    metadata.name = Some(route_name_from_argument(call)?);
                }
                current = &member.object;
            }
            "summary" => {
                if metadata.summary.is_none() {
                    metadata.summary = Some(route_string_metadata_from_call(call, "summary")?);
                }
                current = &member.object;
            }
            "description" => {
                if metadata.description.is_none() {
                    metadata.description =
                        Some(route_string_metadata_from_call(call, "description")?);
                }
                current = &member.object;
            }
            "deprecated" => {
                if metadata.deprecated.is_none() {
                    metadata.deprecated = Some(route_deprecated_from_call(call)?);
                }
                current = &member.object;
            }
            "requireAuth" | "requiresAuth" | "security" => {
                let requirement = auth_requirement_from_call(call)?;
                if metadata.auth.is_none() {
                    metadata.auth = Some(requirement);
                    auth_replaced_by_later_setter = true;
                } else if !auth_replaced_by_later_setter {
                    merge_auth_requirement(&mut metadata.auth, requirement);
                }
                current = &member.object;
            }
            "allowAnonymous" => {
                let requirement = anonymous_auth_requirement();
                if metadata.auth.is_none() {
                    metadata.auth = Some(requirement);
                    auth_replaced_by_later_setter = true;
                } else if !auth_replaced_by_later_setter {
                    merge_auth_requirement(&mut metadata.auth, requirement);
                }
                current = &member.object;
            }
            "authorize" => {
                let policy = route_name_from_argument(call)?;
                if !auth_replaced_by_later_setter {
                    merge_auth_requirement(
                        &mut metadata.auth,
                        AuthRequirementMetadata {
                            required: true,
                            policy: Some(policy),
                            ..AuthRequirementMetadata::default()
                        },
                    );
                }
                current = &member.object;
            }
            "requiresScope" => {
                let scopes = route_tags_from_arguments(call)?;
                if !auth_replaced_by_later_setter {
                    merge_auth_requirement(
                        &mut metadata.auth,
                        AuthRequirementMetadata {
                            required: true,
                            scopes,
                            ..AuthRequirementMetadata::default()
                        },
                    );
                }
                current = &member.object;
            }
            "requiresRole" => {
                let roles = route_tags_from_arguments(call)?;
                if !auth_replaced_by_later_setter {
                    merge_auth_requirement(
                        &mut metadata.auth,
                        AuthRequirementMetadata {
                            required: true,
                            roles,
                            ..AuthRequirementMetadata::default()
                        },
                    );
                }
                current = &member.object;
            }
            "allowedOrigins" => {
                let origins = websocket_origins_from_call(call)?;
                let mut websocket = metadata.websocket.unwrap_or_default();
                websocket.origins = Some(origins);
                metadata.websocket = Some(websocket);
                current = &member.object;
            }
            "accepts" => {
                if metadata.accepts_schema.is_none() {
                    metadata.accepts_schema = Some(route_schema_from_argument(call, "accepts", 2)?);
                }
                current = &member.object;
            }
            "returns" => {
                let (status, schema) = route_returns_from_call(call)?;
                if metadata.returns_status.is_none() {
                    metadata.returns_status = status;
                }
                if metadata.returns_schema.is_none() {
                    metadata.returns_schema = schema;
                }
                current = &member.object;
            }
            "tags" | "withTags" => {
                metadata.tags.extend(route_tags_from_arguments(call)?);
                current = &member.object;
            }
            "consumes" => {
                metadata
                    .consumes
                    .push(route_string_metadata_from_call(call, "consumes")?);
                current = &member.object;
            }
            "produces" => {
                metadata
                    .produces
                    .push(route_string_metadata_from_call(call, "produces")?);
                current = &member.object;
            }
            "header" => {
                metadata
                    .headers
                    .push(route_contract_parameter_from_call(call, "header")?);
                current = &member.object;
            }
            "query" => {
                if metadata.query_schema.is_none() {
                    metadata.query_schema = Some(route_schema_from_argument(call, "query", 2)?);
                }
                current = &member.object;
            }
            "params" => {
                if metadata.params_schema.is_none() {
                    metadata.params_schema = Some(route_schema_from_argument(call, "params", 2)?);
                }
                current = &member.object;
            }
            "openapi" => {
                if metadata.openapi_override.is_none() {
                    metadata.openapi_override = Some(route_openapi_override_from_call(call)?);
                }
                current = &member.object;
            }
            "outputCache" => {
                if !output_cache_setter_seen {
                    metadata.output_cache =
                        Some(route_static_metadata_from_call(call, "outputCache")?);
                    output_cache_setter_seen = true;
                }
                current = &member.object;
            }
            "noOutputCache" => {
                if !output_cache_setter_seen {
                    metadata.output_cache = None;
                    output_cache_setter_seen = true;
                }
                current = &member.object;
            }
            "cacheHeaders" => {
                if metadata.cache_headers.is_none() {
                    metadata.cache_headers =
                        Some(route_static_metadata_from_call(call, "cacheHeaders")?);
                }
                current = &member.object;
            }
            "rateLimit" => {
                metadata.rate_limits.push(route_rate_limit_from_call(call)?);
                current = &member.object;
            }
            _ => break,
        }
    }

    Ok((current, metadata))
}

pub(super) fn merge_auth_requirement(
    existing: &mut Option<AuthRequirementMetadata>,
    incoming: AuthRequirementMetadata,
) {
    let Some(existing) = existing else {
        *existing = Some(incoming);
        return;
    };

    extend_unique(&mut existing.schemes, incoming.schemes);
    extend_unique(&mut existing.scopes, incoming.scopes);
    extend_unique(&mut existing.roles, incoming.roles);
    extend_unique(&mut existing.claims, incoming.claims);
    if existing.policy.is_none() {
        existing.policy = incoming.policy;
    }
}

pub(super) fn anonymous_auth_requirement() -> AuthRequirementMetadata {
    AuthRequirementMetadata {
        required: false,
        allow_anonymous: true,
        ..AuthRequirementMetadata::default()
    }
}

fn extend_unique(values: &mut Vec<String>, incoming: Vec<String>) {
    for value in incoming {
        if !values.contains(&value) {
            values.push(value);
        }
    }
}

fn route_rate_limit_from_call(call: &CallExpression<'_>) -> Result<RateLimitMetadata, Diagnostic> {
    if call.arguments.is_empty() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_RATE_LIMIT",
            "rateLimit requires a RateLimit policy",
        )
        .with_span(call.span));
    }
    let arity_partial = call.arguments.len() != 1;
    let Argument::CallExpression(policy_call) = &call.arguments[0] else {
        return Ok(RateLimitMetadata {
            name: None,
            algorithm: "dynamic".to_string(),
            store: None,
            partition: None,
            partial: true,
        });
    };
    let Some(chain) = static_member_chain(&policy_call.callee) else {
        return Ok(RateLimitMetadata {
            name: None,
            algorithm: "dynamic".to_string(),
            store: None,
            partition: None,
            partial: true,
        });
    };
    if chain.len() < 2 || chain[0] != "RateLimit" {
        return Ok(RateLimitMetadata {
            name: None,
            algorithm: "dynamic".to_string(),
            store: None,
            partition: None,
            partial: true,
        });
    }
    let supported_algorithm = matches!(
        chain[1],
        "fixedWindow" | "slidingWindow" | "tokenBucket" | "concurrency"
    );
    let algorithm = if supported_algorithm {
        chain[1].to_string()
    } else {
        "dynamic".to_string()
    };
    let mut metadata = RateLimitMetadata {
        name: None,
        algorithm,
        store: None,
        partition: None,
        partial: arity_partial || !supported_algorithm,
    };
    let Some(options) = policy_call.arguments.first().and_then(object_argument) else {
        metadata.partial = true;
        return Ok(metadata);
    };
    for property in &options.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            metadata.partial = true;
            continue;
        };
        if property.computed {
            metadata.partial = true;
            continue;
        }
        let Some(key) = property_key_name(&property.key) else {
            metadata.partial = true;
            continue;
        };
        match key {
            "name" => {
                metadata.name = expression_string_literal(&property.value).map(str::to_string);
                metadata.partial = metadata.partial || metadata.name.is_none();
            }
            "store" => {
                metadata.store = expression_string_literal(&property.value).map(str::to_string);
                metadata.partial = metadata.partial || metadata.store.is_none();
            }
            "partitionBy" => {
                metadata.partition = rate_limit_partition_name(&property.value);
                metadata.partial = metadata.partial || metadata.partition.is_none();
            }
            _ => {}
        }
    }
    Ok(metadata)
}

fn rate_limit_partition_name(expression: &Expression<'_>) -> Option<String> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    let chain = static_member_chain(&call.callee)?;
    if chain.len() >= 3 && chain[0] == "RateLimit" && chain[1] == "partition" {
        return Some(chain[2].to_string());
    }
    None
}

pub(super) fn merged_route_metadata(
    options: &RouteMetadata,
    fluent: &RouteMetadata,
) -> RouteMetadata {
    let mut merged = options.clone();
    if fluent.name.is_some() {
        merged.name = fluent.name.clone();
    }
    if !fluent.tags.is_empty() {
        merged.tags.extend(fluent.tags.clone());
    }
    if fluent.auth.is_some() {
        merged.auth = fluent.auth.clone();
    }
    if fluent.accepts_schema.is_some() {
        merged.accepts_schema = fluent.accepts_schema.clone();
    }
    if fluent.returns_schema.is_some() {
        merged.returns_schema = fluent.returns_schema.clone();
    }
    if fluent.returns_status.is_some() {
        merged.returns_status = fluent.returns_status;
    }
    if fluent.summary.is_some() {
        merged.summary = fluent.summary.clone();
    }
    if fluent.description.is_some() {
        merged.description = fluent.description.clone();
    }
    if fluent.deprecated.is_some() {
        merged.deprecated = fluent.deprecated.clone();
    }
    if !fluent.consumes.is_empty() {
        merged.consumes.extend(fluent.consumes.clone());
    }
    if !fluent.produces.is_empty() {
        merged.produces.extend(fluent.produces.clone());
    }
    if !fluent.headers.is_empty() {
        merged.headers.extend(fluent.headers.clone());
    }
    if fluent.query_schema.is_some() {
        merged.query_schema = fluent.query_schema.clone();
    }
    if fluent.params_schema.is_some() {
        merged.params_schema = fluent.params_schema.clone();
    }
    if fluent.openapi_override.is_some() {
        merged.openapi_override = fluent.openapi_override.clone();
    }
    if fluent.output_cache.is_some() {
        merged.output_cache = fluent.output_cache.clone();
    }
    if fluent.cache_headers.is_some() {
        merged.cache_headers = fluent.cache_headers.clone();
    }
    if !fluent.rate_limits.is_empty() {
        merged.rate_limits.extend(fluent.rate_limits.clone());
    }
    if let Some(fluent_websocket) = &fluent.websocket {
        let mut websocket = merged.websocket.unwrap_or_default();
        merge_websocket_options(&mut websocket, fluent_websocket.clone());
        merged.websocket = Some(websocket);
    }
    merged
}

pub(super) fn realtime_route_metadata_from_contract(
    metadata: &RouteMetadata,
) -> Option<RealtimeRouteMetadata> {
    metadata
        .realtime_channel_source
        .as_ref()
        .map(|channel_source| RealtimeRouteMetadata {
            channel_source: channel_source.clone(),
            options_source: metadata.realtime_options_source.clone(),
        })
}

pub(super) fn auth_requirement_from_call(
    call: &CallExpression<'_>,
) -> Result<AuthRequirementMetadata, Diagnostic> {
    if call.arguments.len() > 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_AUTH",
            "requireAuth accepts at most one literal options object",
        )
        .with_span(call.span));
    }
    let mut requirement = AuthRequirementMetadata {
        required: true,
        ..AuthRequirementMetadata::default()
    };
    let Some(argument) = call.arguments.first() else {
        return Ok(requirement);
    };
    if let Some(scheme) = string_argument(argument) {
        requirement.schemes.push(scheme.to_string());
        return Ok(requirement);
    }
    if let Argument::ArrayExpression(array) = argument {
        requirement
            .schemes
            .extend(auth_string_list_from_array(array, "requireAuth schemes")?);
        return Ok(requirement);
    }
    let Some(object) = object_argument(argument) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_AUTH",
            "requireAuth options must be an object literal",
        )
        .with_span(argument_span(argument).unwrap_or(call.span)));
    };
    auth_requirement_from_object(object)
}

pub(super) fn auth_requirement_from_object(
    object: &ObjectExpression<'_>,
) -> Result<AuthRequirementMetadata, Diagnostic> {
    let mut requirement = AuthRequirementMetadata {
        required: true,
        ..AuthRequirementMetadata::default()
    };
    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_AUTH",
                "requireAuth options must use literal properties",
            )
            .with_span(object.span));
        };
        if property.computed {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_AUTH",
                "requireAuth option names must be literal",
            )
            .with_span(property.span));
        }
        let Some(key) = property_key_name(&property.key) else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_AUTH",
                "requireAuth option names must be literal",
            )
            .with_span(property.span));
        };
        match key {
            "scheme" => {
                let Some(scheme) = expression_string_literal(&property.value) else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_AUTH",
                        "requireAuth scheme must be a string literal",
                    )
                    .with_span(property.value.span()));
                };
                requirement.schemes.push(scheme.to_string());
            }
            "schemes" => {
                requirement.schemes.extend(auth_string_list_from_expression(
                    &property.value,
                    "requireAuth schemes",
                )?);
            }
            "scope" => {
                let Some(scope) = expression_string_literal(&property.value) else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_AUTH",
                        "requireAuth scope must be a string literal",
                    )
                    .with_span(property.value.span()));
                };
                requirement.scopes.push(scope.to_string());
            }
            "scopes" => {
                requirement.scopes.extend(auth_string_list_from_expression(
                    &property.value,
                    "requireAuth scopes",
                )?);
            }
            "role" => {
                let Some(role) = expression_string_literal(&property.value) else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_AUTH",
                        "requireAuth role must be a string literal",
                    )
                    .with_span(property.value.span()));
                };
                requirement.roles.push(role.to_string());
            }
            "roles" => {
                requirement
                    .roles
                    .extend(route_tags_from_expression(&property.value)?);
            }
            "policy" => {
                let Some(policy) = expression_string_literal(&property.value) else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_AUTH",
                        "requireAuth policy must be a string literal",
                    )
                    .with_span(property.value.span()));
                };
                requirement.policy = Some(policy.to_string());
            }
            "claim" => {
                let Some(claim) = expression_string_literal(&property.value) else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_AUTH",
                        "requireAuth claim must be a string literal",
                    )
                    .with_span(property.value.span()));
                };
                requirement.claims.push(claim.to_string());
            }
            "allowAnonymous" => {
                if !matches!(&property.value, Expression::BooleanLiteral(value) if value.value) {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_AUTH",
                        "requireAuth allowAnonymous must be the literal true when provided",
                    )
                    .with_span(property.value.span()));
                }
                requirement.required = false;
                requirement.allow_anonymous = true;
            }
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_AUTH",
                    format!("unsupported requireAuth option '{key}'"),
                )
                .with_span(property.span));
            }
        }
    }
    Ok(requirement)
}

fn auth_string_list_from_expression(
    expression: &Expression<'_>,
    subject: &str,
) -> Result<Vec<String>, Diagnostic> {
    if let Some(value) = expression_string_literal(expression) {
        return Ok(vec![value.to_string()]);
    }
    let Expression::ArrayExpression(array) = expression else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_AUTH",
            format!("{subject} must be a string literal or string literal array"),
        )
        .with_span(expression.span()));
    };
    auth_string_list_from_array(array, subject)
}

fn auth_string_list_from_array(
    array: &ArrayExpression<'_>,
    subject: &str,
) -> Result<Vec<String>, Diagnostic> {
    let mut values = Vec::new();
    for element in &array.elements {
        let ArrayExpressionElement::StringLiteral(value) = element else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_AUTH",
                format!("{subject} must contain only string literals"),
            )
            .with_span(array.span));
        };
        values.push(value.value.as_str().to_string());
    }
    Ok(values)
}

fn route_string_metadata_from_call(
    call: &CallExpression<'_>,
    method: &str,
) -> Result<String, Diagnostic> {
    if call.arguments.len() != 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
            format!("{method} requires exactly one string literal"),
        )
        .with_span(call.span));
    }
    let Some(value) = call.arguments.first().and_then(string_argument) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
            format!("{method} requires a string literal"),
        )
        .with_span(call.span));
    };
    if value.is_empty() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
            format!("{method} requires a non-empty string literal"),
        )
        .with_span(call.span));
    }
    Ok(value.to_string())
}

fn route_deprecated_from_call(call: &CallExpression<'_>) -> Result<String, Diagnostic> {
    if call.arguments.is_empty() {
        return Ok("true".to_string());
    }
    if call.arguments.len() != 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
            "deprecated accepts at most one boolean or reason string",
        )
        .with_span(call.span));
    }
    match call.arguments.first() {
        Some(Argument::BooleanLiteral(value)) => {
            Ok(if value.value { "true" } else { "false" }.to_string())
        }
        Some(argument) => {
            let Some(reason) = string_argument(argument) else {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
                    "deprecated metadata must be a boolean literal or reason string",
                )
                .with_span(argument_span(argument).unwrap_or(call.span)));
            };
            Ok(reason.to_string())
        }
        None => Ok("true".to_string()),
    }
}

fn route_schema_from_argument(
    call: &CallExpression<'_>,
    method: &str,
    max_arguments: usize,
) -> Result<String, Diagnostic> {
    if call.arguments.is_empty() || call.arguments.len() > max_arguments {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_SCHEMA",
            format!("{method} requires a schema identifier"),
        )
        .with_span(call.span));
    }

    let Some(schema) = call.arguments.first().and_then(argument_identifier) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_SCHEMA",
            format!("{method} schema must be a static identifier"),
        )
        .with_span(call.span));
    };
    Ok(schema.to_string())
}

fn argument_status(argument: &Argument<'_>) -> Option<u16> {
    let Argument::NumericLiteral(literal) = argument else {
        return None;
    };
    let value = literal.value;
    if value.fract() == 0.0 && (100.0..=599.0).contains(&value) {
        Some(value as u16)
    } else {
        None
    }
}

fn route_returns_from_call(
    call: &CallExpression<'_>,
) -> Result<(Option<u16>, Option<String>), Diagnostic> {
    if call.arguments.is_empty() || call.arguments.len() > 3 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_SCHEMA",
            "returns requires a schema identifier or status plus schema identifier",
        )
        .with_span(call.span));
    }
    if let Some(status) = call.arguments.first().and_then(argument_status) {
        let schema = match call.arguments.get(1) {
            Some(argument) => {
                let Some(schema) = argument_identifier(argument) else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_ROUTE_SCHEMA",
                        "returns status overload requires a static schema identifier when a schema is provided",
                    )
                    .with_span(argument_span(argument).unwrap_or(call.span)));
                };
                Some(schema.to_string())
            }
            None => None,
        };
        return Ok((Some(status), schema));
    }
    Ok((None, Some(route_schema_from_argument(call, "returns", 2)?)))
}

fn route_contract_parameter_from_call(
    call: &CallExpression<'_>,
    method: &str,
) -> Result<RouteContractParameter, Diagnostic> {
    if call.arguments.len() < 2 || call.arguments.len() > 3 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
            format!("{method} requires a name and schema identifier"),
        )
        .with_span(call.span));
    }
    let Some(name) = call.arguments.first().and_then(string_argument) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
            format!("{method} name must be a string literal"),
        )
        .with_span(call.span));
    };
    let Some(schema) = call.arguments.get(1).and_then(argument_identifier) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_SCHEMA",
            format!("{method} schema must be a static identifier"),
        )
        .with_span(call.span));
    };
    let options = call.arguments.get(2).and_then(object_argument);
    Ok(RouteContractParameter {
        name: name.to_string(),
        schema: schema.to_string(),
        required: options
            .and_then(|object| object_bool_property_value(object, "required"))
            .unwrap_or(false),
        description: options
            .and_then(|object| object_string_property_value(object, "description"))
            .map(ToString::to_string),
    })
}

fn route_openapi_override_from_call(call: &CallExpression<'_>) -> Result<Value, Diagnostic> {
    if call.arguments.len() != 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
            "openapi requires exactly one object literal",
        )
        .with_span(call.span));
    }
    let Some(value) = call.arguments.first().and_then(argument_json_value) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
            "openapi override must be a static JSON-compatible object literal",
        )
        .with_span(call.span));
    };
    if !value.is_object() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
            "openapi override must be an object literal",
        )
        .with_span(call.span));
    }
    Ok(value)
}

fn route_static_metadata_from_call(
    call: &CallExpression<'_>,
    method: &str,
) -> Result<Value, Diagnostic> {
    if call.arguments.len() != 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
            format!("{method} requires exactly one options object"),
        )
        .with_span(call.span));
    }
    let Some(value) = call.arguments.first().and_then(argument_json_value) else {
        return Ok(json!({
            "static": false
        }));
    };
    if value.is_object() {
        Ok(value)
    } else {
        Ok(json!({
            "static": false
        }))
    }
}

fn validate_route_schema_reference(
    path: &Path,
    span: Span,
    schema: &str,
    schema_names: &BTreeSet<String>,
) -> Result<(), Diagnostic> {
    if schema_names.contains(schema) {
        return Ok(());
    }
    Err(unresolved_schema_diagnostic(path, span, schema))
}

pub(super) fn unresolved_schema_diagnostic(path: &Path, span: Span, schema: &str) -> Diagnostic {
    Diagnostic::new(
        "SLOPPYC_E_UNRESOLVED_SCHEMA",
        format!("route schema '{schema}' is not declared as a supported Sloppy schema"),
    )
    .with_path(path)
    .with_span(span)
    .with_hint("Declare the schema with Schema.object(...), Schema.array(...), or another supported Schema constructor in compiler-visible source.")
}

pub(super) fn apply_route_schema_metadata(
    path: &Path,
    span: Span,
    schema_names: &BTreeSet<String>,
    metadata: &RouteMetadata,
    handler: &mut Handler,
) -> Result<(), Diagnostic> {
    let mut schema_metadata_conflict = false;
    if let Some(schema) = &metadata.accepts_schema {
        validate_route_schema_reference(path, span, schema, schema_names)?;
        if let Some(binding) = handler
            .bindings
            .iter_mut()
            .find(|binding| binding.kind == "body.json")
        {
            schema_metadata_conflict |= apply_binding_schema(&mut binding.schema, schema);
        } else {
            handler.bindings.push(framework_binding(
                "body.json",
                None,
                Some(schema),
                None,
                span,
            ));
        }
    }

    if let Some(schema) = &metadata.returns_schema {
        validate_route_schema_reference(path, span, schema, schema_names)?;
        let mut applied = false;
        if let Some(response) = &mut handler.response {
            if response.kind == "json" {
                schema_metadata_conflict |= apply_response_schema(response, schema);
                applied = true;
            }
        }
        for response in &mut handler.responses {
            if response.kind == "json" {
                schema_metadata_conflict |= apply_response_schema(response, schema);
                applied = true;
            }
        }
        if !applied {
            let response = ResponseMetadata {
                helper: "returns".to_string(),
                status: metadata.returns_status.unwrap_or(200),
                kind: "json".to_string(),
                body_schema: Some(schema.clone()),
                native_body: None,
                source_name: None,
                source_text: None,
                span: Some(span),
                partial: false,
            };
            if handler.response.is_none() {
                handler.response = Some(response.clone());
            }
            handler.responses.push(response);
        }
    }
    for header in &metadata.headers {
        validate_route_schema_reference(path, span, &header.schema, schema_names)?;
    }
    if let Some(schema) = &metadata.query_schema {
        validate_route_schema_reference(path, span, schema, schema_names)?;
    }
    if let Some(schema) = &metadata.params_schema {
        validate_route_schema_reference(path, span, schema, schema_names)?;
    }
    handler.schema_metadata_conflict |= schema_metadata_conflict;
    Ok(())
}

fn apply_binding_schema(target: &mut Option<String>, schema: &str) -> bool {
    match target {
        None => {
            *target = Some(schema.to_string());
            false
        }
        Some(existing) if existing == schema => false,
        Some(_) => {
            *target = None;
            true
        }
    }
}

fn apply_response_schema(response: &mut ResponseMetadata, schema: &str) -> bool {
    match &mut response.body_schema {
        None => {
            response.body_schema = Some(schema.to_string());
            false
        }
        Some(existing) if existing == schema => false,
        Some(_) => {
            response.partial = true;
            true
        }
    }
}

pub(super) fn duplicate_schema_diagnostic(path: &Path, span: Span, schema: &str) -> Diagnostic {
    Diagnostic::new(
        "SLOPPYC_E_DUPLICATE_SCHEMA",
        format!("schema '{schema}' is declared more than once"),
    )
    .with_path(path)
    .with_span(span)
    .with_hint("Use unique schema names across the entry module and imported function modules.")
}

fn route_name_from_argument(call: &CallExpression<'_>) -> Result<String, Diagnostic> {
    if call.arguments.len() != 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_NAME",
            "withName requires exactly one literal name",
        )
        .with_span(call.span));
    }

    let Some(name) = call.arguments.first().and_then(string_argument) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_NAME",
            "route names must be string literals",
        )
        .with_span(call.span));
    };
    if name.is_empty() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_NAME",
            "route names must be non-empty string literals",
        )
        .with_span(call.span));
    }
    Ok(name.to_string())
}

pub(super) fn route_metadata_from_options_argument(
    argument: &Argument<'_>,
) -> Result<RouteMetadata, Diagnostic> {
    let Some(object) = object_argument(argument) else {
        let diagnostic = Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
            "route metadata options must be an object literal",
        );
        return Err(with_argument_span(diagnostic, argument));
    };
    let mut metadata = RouteMetadata::default();
    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
                "route metadata options must use literal properties",
            )
            .with_span(object.span));
        };
        if property.computed {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
                "route metadata option names must be literal",
            )
            .with_span(property.span));
        }
        let Some(key) = property_key_name(&property.key) else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
                "route metadata option names must be literal",
            )
            .with_span(property.span));
        };
        match key {
            "name" => {
                let Some(name) = expression_string_literal(&property.value) else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_ROUTE_NAME",
                        "route option name must be a string literal",
                    )
                    .with_span(property.value.span()));
                };
                if name.is_empty() {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_ROUTE_NAME",
                        "route option name must be a non-empty string literal",
                    )
                    .with_span(property.value.span()));
                }
                metadata.name = Some(name.to_string());
            }
            "tags" => {
                metadata.tags = route_tags_from_expression(&property.value)?;
            }
            "summary" => {
                let Some(value) = expression_string_literal(&property.value) else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
                        "route option summary must be a string literal",
                    )
                    .with_span(property.value.span()));
                };
                metadata.summary = Some(value.to_string());
            }
            "description" => {
                let Some(value) = expression_string_literal(&property.value) else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
                        "route option description must be a string literal",
                    )
                    .with_span(property.value.span()));
                };
                metadata.description = Some(value.to_string());
            }
            "deprecated" => match &property.value {
                Expression::BooleanLiteral(value) => {
                    metadata.deprecated =
                        Some(if value.value { "true" } else { "false" }.to_string());
                }
                _ => {
                    let Some(value) = expression_string_literal(&property.value) else {
                        return Err(Diagnostic::new(
                            "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
                            "route option deprecated must be a boolean or reason string",
                        )
                        .with_span(property.value.span()));
                    };
                    metadata.deprecated = Some(value.to_string());
                }
            },
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
                    format!("unsupported route metadata option '{key}'"),
                )
                .with_span(property.span));
            }
        }
    }
    Ok(metadata)
}

pub(super) fn route_tags_from_arguments(
    call: &CallExpression<'_>,
) -> Result<Vec<String>, Diagnostic> {
    let mut tags = Vec::new();
    for argument in &call.arguments {
        let Some(tag) = string_argument(argument) else {
            let diagnostic = Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
                "group.withTags(...) arguments must be string literals",
            );
            return Err(with_argument_span(diagnostic, argument));
        };
        if tag.is_empty() {
            let diagnostic = Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
                "route tags must be non-empty string literals",
            );
            return Err(with_argument_span(diagnostic, argument));
        }
        tags.push(tag.to_string());
    }
    Ok(tags)
}

pub(super) fn route_tags_from_expression(
    expression: &Expression<'_>,
) -> Result<Vec<String>, Diagnostic> {
    let Expression::ArrayExpression(array) = expression else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
            "route option tags must be a string literal array",
        )
        .with_span(expression.span()));
    };
    let mut tags = Vec::new();
    for element in &array.elements {
        let ArrayExpressionElement::StringLiteral(value) = element else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
                "route option tags must contain only string literals",
            )
            .with_span(array.span));
        };
        let tag = value.value.as_str();
        if tag.is_empty() {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS",
                "route tags must be non-empty string literals",
            )
            .with_span(value.span));
        }
        tags.push(tag.to_string());
    }
    Ok(tags)
}

pub(super) fn expression_string_literal<'a>(expression: &'a Expression<'a>) -> Option<&'a str> {
    match expression {
        Expression::StringLiteral(value) => Some(value.value.as_str()),
        Expression::ParenthesizedExpression(parenthesized) => {
            expression_string_literal(&parenthesized.expression)
        }
        _ => None,
    }
}

fn with_argument_span(diagnostic: Diagnostic, argument: &Argument<'_>) -> Diagnostic {
    if let Some(span) = argument_span(argument) {
        diagnostic.with_span(span)
    } else {
        diagnostic
    }
}

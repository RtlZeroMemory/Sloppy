// Health, management, and docs route extraction.
use super::*;

pub(super) fn app_map_health_checks_call(
    path: &Path,
    source: &str,
    source_name: &str,
    expression: &Expression<'_>,
    state: &AppState,
) -> Result<Option<Vec<Route>>, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(None);
    };
    let Some((receiver, property)) = static_member_name(&call.callee) else {
        return Ok(None);
    };
    if property != "mapHealthChecks" || !state.app_vars.contains(receiver) {
        return Ok(None);
    }
    if call.arguments.len() > 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "app.mapHealthChecks accepts at most one literal options argument",
        )
        .with_path(path)
        .with_span(call.span));
    }

    let options = health_options_from_call(path, source, call)?;
    Ok(Some(health_routes_from_options(
        path,
        source,
        source_name,
        call.span,
        &options,
        &state.middleware,
        state.cors_policy.as_ref(),
    )?))
}

pub(super) fn app_health_expose_call(
    path: &Path,
    source: &str,
    source_name: &str,
    expression: &Expression<'_>,
    state: &AppState,
) -> Result<Option<Vec<Route>>, Diagnostic> {
    let Expression::CallExpression(expose_call) = expression else {
        return Ok(None);
    };
    let Some((receiver, property)) = static_member_expression(&expose_call.callee) else {
        return Ok(None);
    };
    if property != "expose" {
        return Ok(None);
    }
    if expose_call.arguments.len() > 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "app.health().expose accepts at most one literal options argument",
        )
        .with_path(path)
        .with_span(expose_call.span));
    }

    let mut options = health_expose_options_from_call(path, expose_call)?;
    let Some(checks) = health_check_chain_from_expression(path, source, receiver, state)? else {
        return Ok(None);
    };
    options.checks = checks;
    Ok(Some(health_routes_from_options(
        path,
        source,
        source_name,
        expose_call.span,
        &options,
        &state.middleware,
        state.cors_policy.as_ref(),
    )?))
}

pub(super) fn app_management_call(
    path: &Path,
    source: &str,
    source_name: &str,
    expression: &Expression<'_>,
    state: &AppState,
) -> Result<Option<Vec<Route>>, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(None);
    };
    let Some((receiver, property)) = static_member_name(&call.callee) else {
        return Ok(None);
    };
    if property != "management" || !state.app_vars.contains(receiver) {
        return Ok(None);
    }
    if call.arguments.len() > 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_MANAGEMENT",
            "app.management accepts at most one literal options argument",
        )
        .with_path(path)
        .with_span(call.span));
    }
    let options = management_options_from_call(path, call)?;
    Ok(Some(management_routes_from_options(
        path,
        source,
        source_name,
        call.span,
        &options,
        &state.middleware,
        state.cors_policy.as_ref(),
    )?))
}

fn health_expose_options_from_call(
    path: &Path,
    call: &CallExpression<'_>,
) -> Result<HealthOptions, Diagnostic> {
    let mut options = HealthOptions {
        path: DEFAULT_HEALTH_PATH.to_string(),
        liveness_path: "/live".to_string(),
        readiness_path: "/ready".to_string(),
        startup_path: Some("/startup".to_string()),
        checks: Vec::new(),
    };

    let Some(argument) = call.arguments.first() else {
        health_paths_are_distinct(path, call.span, &options)?;
        return Ok(options);
    };
    let Some(object) = object_argument(argument) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "app.health().expose options must be an object literal",
        )
        .with_path(path)
        .with_span(argument_span(argument).unwrap_or(call.span)));
    };

    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                "health expose options do not support spread properties",
            )
            .with_path(path)
            .with_span(object.span));
        };
        if property.computed
            || property.shorthand
            || property.method
            || property.kind != PropertyKind::Init
        {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                "health expose options must use simple literal properties",
            )
            .with_path(path)
            .with_span(property.span));
        }
        let Some(key) = property_key_name(&property.key) else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                "health expose option names must be literal",
            )
            .with_path(path)
            .with_span(property.span));
        };
        match key {
            "health" => {
                options.path =
                    health_path_from_expression(path, &property.value, "health", property.span)?;
            }
            "live" | "liveness" | "livenessPath" => {
                options.liveness_path =
                    health_path_from_expression(path, &property.value, "liveness", property.span)?;
            }
            "ready" | "readiness" | "readinessPath" => {
                options.readiness_path =
                    health_path_from_expression(path, &property.value, "readiness", property.span)?;
            }
            "startup" | "startupPath" => {
                options.startup_path = Some(health_path_from_expression(
                    path,
                    &property.value,
                    "startup",
                    property.span,
                )?);
            }
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                    format!("unsupported health expose option '{key}'"),
                )
                .with_path(path)
                .with_span(property.span));
            }
        }
    }
    health_paths_are_distinct(path, object.span, &options)?;
    Ok(options)
}

fn health_check_chain_from_expression(
    path: &Path,
    source: &str,
    expression: &Expression<'_>,
    state: &AppState,
) -> Result<Option<Vec<HealthCheck>>, Diagnostic> {
    let mut checks = Vec::new();
    let mut current = expression;
    loop {
        let Expression::CallExpression(call) = current else {
            return Ok(None);
        };
        let Some((receiver, property)) = static_member_expression(&call.callee) else {
            return Ok(None);
        };
        match property {
            "check" => {
                checks.push(health_check_from_chain_call(path, source, call)?);
                current = receiver;
            }
            "health" => {
                if !call.arguments.is_empty() {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                        "app.health does not accept compiler-visible arguments",
                    )
                    .with_path(path)
                    .with_span(call.span));
                }
                let Some((root_receiver, root_property)) = static_member_name(&call.callee) else {
                    return Ok(None);
                };
                if root_property != "health" || !state.app_vars.contains(root_receiver) {
                    return Ok(None);
                }
                checks.reverse();
                return Ok(Some(checks));
            }
            _ => return Ok(None),
        }
    }
}

fn health_check_from_chain_call(
    path: &Path,
    source: &str,
    call: &CallExpression<'_>,
) -> Result<HealthCheck, Diagnostic> {
    if !(2..=3).contains(&call.arguments.len()) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "app.health().check requires a name, check function, and optional literal options",
        )
        .with_path(path)
        .with_span(call.span));
    }
    let Some(name) = call.arguments.first().and_then(string_argument) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "health check name must be a string literal",
        )
        .with_path(path)
        .with_span(
            call.arguments
                .first()
                .and_then(argument_span)
                .unwrap_or(call.span),
        ));
    };
    if name.is_empty() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "health check name must be non-empty",
        )
        .with_path(path)
        .with_span(
            call.arguments
                .first()
                .and_then(argument_span)
                .unwrap_or(call.span),
        ));
    }

    let Some(check_argument) = call.arguments.get(1) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "health check requires a check callback or built-in check",
        )
        .with_path(path)
        .with_span(call.span));
    };
    let check_source = health_chain_check_source(path, source, check_argument)?;
    let mut tags = Vec::new();
    let mut critical = true;
    let mut degraded_is_unhealthy = false;
    if let Some(options_argument) = call.arguments.get(2) {
        let Some(object) = object_argument(options_argument) else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                "health check options must be an object literal",
            )
            .with_path(path)
            .with_span(argument_span(options_argument).unwrap_or(call.span)));
        };
        for property in &object.properties {
            let ObjectPropertyKind::ObjectProperty(property) = property else {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                    "health check options do not support spread properties",
                )
                .with_path(path)
                .with_span(object.span));
            };
            if property.computed
                || property.shorthand
                || property.method
                || property.kind != PropertyKind::Init
            {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                    "health check options must use simple literal properties",
                )
                .with_path(path)
                .with_span(property.span));
            }
            let Some(key) = property_key_name(&property.key) else {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                    "health check option names must be literal",
                )
                .with_path(path)
                .with_span(property.span));
            };
            match key {
                "tags" => {
                    tags = route_tags_from_expression(&property.value)
                        .map_err(|diagnostic| diagnostic.with_path(path))?;
                }
                "critical" => {
                    let Expression::BooleanLiteral(value) = &property.value else {
                        return Err(Diagnostic::new(
                            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                            "health check critical flag must be a boolean literal",
                        )
                        .with_path(path)
                        .with_span(property.value.span()));
                    };
                    critical = value.value;
                }
                "degradedIsUnhealthy" => {
                    let Expression::BooleanLiteral(value) = &property.value else {
                        return Err(Diagnostic::new(
                            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                            "health check degradedIsUnhealthy flag must be a boolean literal",
                        )
                        .with_path(path)
                        .with_span(property.value.span()));
                    };
                    degraded_is_unhealthy = value.value;
                }
                "timeoutMs" | "cacheMs" => {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                        format!("compiler-generated health does not support health check option '{key}'"),
                    )
                    .with_path(path)
                    .with_span(property.span));
                }
                _ => {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                        format!("unsupported health check option '{key}'"),
                    )
                    .with_path(path)
                    .with_span(property.span));
                }
            }
        }
    }

    Ok(HealthCheck {
        name: name.to_string(),
        check_source,
        liveness: tags.iter().any(|tag| tag == "live"),
        readiness: tags.iter().any(|tag| tag == "ready"),
        startup: tags.iter().any(|tag| tag == "startup"),
        critical,
        degraded_is_unhealthy,
        tags,
    })
}

fn health_chain_check_source(
    path: &Path,
    source: &str,
    argument: &Argument<'_>,
) -> Result<String, Diagnostic> {
    match argument {
        Argument::ArrowFunctionExpression(function) => {
            health_check_arrow_source(path, source, function)
        }
        Argument::FunctionExpression(function) => {
            health_check_function_source(path, source, function, false)
        }
        Argument::CallExpression(call) => {
            let Some((receiver, property)) = static_member_name(&call.callee) else {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                    "compiler-visible built-in health checks must use Health.*()",
                )
                .with_path(path)
                .with_span(call.span));
            };
            if receiver != "Health" || !call.arguments.is_empty() {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                    "compiler-visible built-in health checks support only zero-argument Health.self(), Health.runtime(), Health.memory(), and Health.openApi(); richer Health.* checks are app-host-only",
                )
                .with_path(path)
                .with_span(call.span));
            }
            match property {
                "self" => Ok("() => ({ status: \"healthy\" })".to_string()),
                "runtime" => Ok("(ctx) => ((ctx && ctx.lifecycle && ctx.lifecycle.shuttingDown) ? { status: \"unhealthy\", errorCode: \"SLOPPY_HEALTH_SHUTTING_DOWN\" } : { status: \"healthy\" })".to_string()),
                "memory" => Ok("() => ({ status: \"healthy\" })".to_string()),
                "openApi" => Ok("() => ({ status: \"healthy\" })".to_string()),
                _ => Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                    format!("unsupported compiler-visible built-in health check Health.{property}()"),
                )
                .with_path(path)
                .with_span(call.span)),
            }
        }
        _ => Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "health check must be an inline function or supported Health.*() built-in",
        )
        .with_path(path)
        .with_span(argument_span(argument).unwrap_or_default())),
    }
}

fn management_options_from_call(
    path: &Path,
    call: &CallExpression<'_>,
) -> Result<ManagementOptions, Diagnostic> {
    let mut options = ManagementOptions {
        path: DEFAULT_MANAGEMENT_PATH.to_string(),
        health: true,
        metrics: true,
        info: true,
        runtime: true,
    };
    let Some(argument) = call.arguments.first() else {
        return Ok(options);
    };
    let Some(object) = object_argument(argument) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_MANAGEMENT",
            "app.management options must be an object literal",
        )
        .with_path(path)
        .with_span(argument_span(argument).unwrap_or(call.span)));
    };
    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_MANAGEMENT",
                "management options do not support spread properties",
            )
            .with_path(path)
            .with_span(object.span));
        };
        if property.computed
            || property.shorthand
            || property.method
            || property.kind != PropertyKind::Init
        {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_MANAGEMENT",
                "management options must use simple literal properties",
            )
            .with_path(path)
            .with_span(property.span));
        }
        let Some(key) = property_key_name(&property.key) else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_MANAGEMENT",
                "management option names must be literal",
            )
            .with_path(path)
            .with_span(property.span));
        };
        match key {
            "path" => {
                options.path = health_path_from_expression(
                    path,
                    &property.value,
                    "management",
                    property.span,
                )?;
            }
            "health" => {
                options.health = literal_bool_option(path, &property.value, property.span, key)?
            }
            "metrics" => {
                options.metrics = literal_bool_option(path, &property.value, property.span, key)?
            }
            "info" => {
                options.info = literal_bool_option(path, &property.value, property.span, key)?
            }
            "runtime" => {
                options.runtime = literal_bool_option(path, &property.value, property.span, key)?
            }
            "protect" => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_MANAGEMENT",
                    "compiler-visible app.management does not support protect hooks; use the bootstrap app host for protected management endpoints",
                )
                .with_path(path)
                .with_span(property.span));
            }
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_MANAGEMENT",
                    format!("unsupported management option '{key}'"),
                )
                .with_path(path)
                .with_span(property.span));
            }
        }
    }
    Ok(options)
}

fn literal_bool_option(
    path: &Path,
    expression: &Expression<'_>,
    span: Span,
    name: &str,
) -> Result<bool, Diagnostic> {
    let Expression::BooleanLiteral(value) = expression else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_MANAGEMENT",
            format!("management option '{name}' must be a boolean literal"),
        )
        .with_path(path)
        .with_span(span));
    };
    Ok(value.value)
}

fn health_options_from_call(
    path: &Path,
    source: &str,
    call: &CallExpression<'_>,
) -> Result<HealthOptions, Diagnostic> {
    let mut options = HealthOptions {
        path: DEFAULT_HEALTH_PATH.to_string(),
        liveness_path: DEFAULT_LIVENESS_PATH.to_string(),
        readiness_path: DEFAULT_READINESS_PATH.to_string(),
        startup_path: None,
        checks: Vec::new(),
    };

    let Some(argument) = call.arguments.first() else {
        return Ok(options);
    };
    if let Some(path_value) = string_argument(argument) {
        options.path = health_path(
            path,
            path_value,
            argument_span(argument).unwrap_or(call.span),
        )?;
        health_paths_are_distinct(path, call.span, &options)?;
        return Ok(options);
    }

    let Some(object) = object_argument(argument) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "app.mapHealthChecks options must be a string literal or object literal",
        )
        .with_path(path)
        .with_span(argument_span(argument).unwrap_or(call.span)));
    };

    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                "health options do not support spread properties",
            )
            .with_path(path)
            .with_span(object.span));
        };
        if property.computed
            || property.shorthand
            || property.method
            || property.kind != PropertyKind::Init
        {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                "health options must use simple literal properties",
            )
            .with_path(path)
            .with_span(property.span));
        }
        let Some(key) = property_key_name(&property.key) else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                "health option names must be literal",
            )
            .with_path(path)
            .with_span(property.span));
        };
        match key {
            "path" => {
                options.path =
                    health_path_from_expression(path, &property.value, "health", property.span)?;
            }
            "livenessPath" => {
                options.liveness_path =
                    health_path_from_expression(path, &property.value, "liveness", property.span)?;
            }
            "readinessPath" => {
                options.readiness_path =
                    health_path_from_expression(path, &property.value, "readiness", property.span)?;
            }
            "startupPath" => {
                options.startup_path = Some(health_path_from_expression(
                    path,
                    &property.value,
                    "startup",
                    property.span,
                )?);
            }
            "checks" => {
                options.checks = health_checks_from_expression(path, source, &property.value)?;
            }
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                    format!("unsupported health option '{key}'"),
                )
                .with_path(path)
                .with_span(property.span));
            }
        }
    }

    health_paths_are_distinct(path, object.span, &options)?;
    Ok(options)
}

fn health_path_from_expression(
    path: &Path,
    expression: &Expression<'_>,
    subject: &str,
    fallback_span: Span,
) -> Result<String, Diagnostic> {
    let Some(value) = expression_string_literal(expression) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            format!("health {subject} path must be a string literal"),
        )
        .with_path(path)
        .with_span(expression.span()));
    };
    health_path(path, value, fallback_span)
}

fn health_path(path: &Path, value: &str, span: Span) -> Result<String, Diagnostic> {
    if value.is_empty() || !value.starts_with('/') {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "health paths must be non-empty string literals starting with '/'",
        )
        .with_path(path)
        .with_span(span));
    }
    let normalized = normalize_framework_route_pattern(value);
    if !route_pattern_supported(&normalized) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "health path is outside the Plan v1 alpha route syntax",
        )
        .with_path(path)
        .with_span(span)
        .with_hint("Use '/', static segments, {name}, {name:str}, or {name:int}."));
    }
    Ok(value.to_string())
}

fn health_paths_are_distinct(
    path: &Path,
    span: Span,
    options: &HealthOptions,
) -> Result<(), Diagnostic> {
    let mut seen = BTreeSet::new();
    if !seen.insert(options.path.clone())
        || !seen.insert(options.liveness_path.clone())
        || !seen.insert(options.readiness_path.clone())
        || options
            .startup_path
            .as_ref()
            .is_some_and(|startup_path| !seen.insert(startup_path.clone()))
    {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "health, liveness, readiness, and startup paths must be distinct",
        )
        .with_path(path)
        .with_span(span));
    }
    Ok(())
}

fn health_checks_from_expression(
    path: &Path,
    source: &str,
    expression: &Expression<'_>,
) -> Result<Vec<HealthCheck>, Diagnostic> {
    let Expression::ArrayExpression(array) = expression else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "health checks must be an array literal",
        )
        .with_path(path)
        .with_span(expression.span()));
    };

    let mut checks = Vec::new();
    for (index, element) in array.elements.iter().enumerate() {
        checks.push(health_check_from_array_element(
            path, source, element, index, array.span,
        )?);
    }
    Ok(checks)
}

fn health_check_from_array_element(
    path: &Path,
    source: &str,
    element: &ArrayExpressionElement<'_>,
    index: usize,
    fallback_span: Span,
) -> Result<HealthCheck, Diagnostic> {
    match element {
        ArrayExpressionElement::ArrowFunctionExpression(function) => Ok(HealthCheck {
            name: format!("check-{}", index + 1),
            check_source: health_check_arrow_source(path, source, function)?,
            liveness: false,
            readiness: true,
            startup: false,
            critical: true,
            degraded_is_unhealthy: false,
            tags: Vec::new(),
        }),
        ArrayExpressionElement::FunctionExpression(function) => {
            let name = function
                .id
                .as_ref()
                .map(|identifier| identifier.name.as_str())
                .filter(|name| !name.is_empty())
                .map_or_else(|| format!("check-{}", index + 1), ToOwned::to_owned);
            Ok(HealthCheck {
                name,
                check_source: health_check_function_source(path, source, function, false)?,
                liveness: false,
                readiness: true,
                startup: false,
                critical: true,
                degraded_is_unhealthy: false,
                tags: Vec::new(),
            })
        }
        ArrayExpressionElement::ObjectExpression(object) => {
            health_check_from_object(path, source, object)
        }
        _ => Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "health checks must be inline functions or literal check objects",
        )
        .with_path(path)
        .with_span(fallback_span)),
    }
}

fn health_check_from_object(
    path: &Path,
    source: &str,
    object: &oxc_ast::ast::ObjectExpression<'_>,
) -> Result<HealthCheck, Diagnostic> {
    let mut name = None;
    let mut check_source = None;
    let mut liveness = false;
    let mut readiness = true;
    let mut startup = false;
    let mut critical = true;
    let mut degraded_is_unhealthy = false;
    let mut tags = Vec::new();

    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                "health check objects do not support spread properties",
            )
            .with_path(path)
            .with_span(object.span));
        };
        if property.computed || property.shorthand || property.kind != PropertyKind::Init {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                "health check objects must use simple literal properties",
            )
            .with_path(path)
            .with_span(property.span));
        }
        let Some(key) = property_key_name(&property.key) else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                "health check property names must be literal",
            )
            .with_path(path)
            .with_span(property.span));
        };
        if property.method && key != "check" {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                "health check objects only support method syntax for the check function",
            )
            .with_path(path)
            .with_span(property.span));
        }
        match key {
            "name" => {
                let Some(value) = expression_string_literal(&property.value) else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                        "health check name must be a string literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                };
                if value.is_empty() {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                        "health check name must be non-empty",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                }
                name = Some(value.to_string());
            }
            "check" => {
                check_source = Some(health_check_source_from_expression(
                    path,
                    source,
                    &property.value,
                    property.method,
                    property.span,
                )?);
            }
            "liveness" => {
                let Expression::BooleanLiteral(value) = &property.value else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                        "health check liveness flag must be a boolean literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                };
                liveness = value.value;
            }
            "readiness" => {
                let Expression::BooleanLiteral(value) = &property.value else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                        "health check readiness flag must be a boolean literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                };
                readiness = value.value;
            }
            "startup" => {
                let Expression::BooleanLiteral(value) = &property.value else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                        "health check startup flag must be a boolean literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                };
                startup = value.value;
            }
            "critical" => {
                let Expression::BooleanLiteral(value) = &property.value else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                        "health check critical flag must be a boolean literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                };
                critical = value.value;
            }
            "degradedIsUnhealthy" => {
                let Expression::BooleanLiteral(value) = &property.value else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                        "health check degradedIsUnhealthy flag must be a boolean literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                };
                degraded_is_unhealthy = value.value;
            }
            "timeoutMs" | "cacheMs" => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                    format!(
                        "compiler-generated health does not support health check option '{key}'"
                    ),
                )
                .with_path(path)
                .with_span(property.span));
            }
            "tags" => {
                tags = route_tags_from_expression(&property.value)
                    .map_err(|diagnostic| diagnostic.with_path(path))?;
                liveness |= tags.iter().any(|tag| tag == "live");
                readiness |= tags.iter().any(|tag| tag == "ready");
                startup |= tags.iter().any(|tag| tag == "startup");
            }
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                    format!("unsupported health check option '{key}'"),
                )
                .with_path(path)
                .with_span(property.span));
            }
        }
    }

    let Some(name) = name else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "health check objects must include a literal name",
        )
        .with_path(path)
        .with_span(object.span));
    };
    let Some(check_source) = check_source else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "health check objects must include an inline check function",
        )
        .with_path(path)
        .with_span(object.span));
    };
    if !liveness && !readiness && !startup {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "health check must target readiness, liveness, or startup",
        )
        .with_path(path)
        .with_span(object.span));
    }

    Ok(HealthCheck {
        name,
        check_source,
        liveness,
        readiness,
        startup,
        critical,
        degraded_is_unhealthy,
        tags,
    })
}

fn health_check_source_from_expression(
    path: &Path,
    source: &str,
    expression: &Expression<'_>,
    method: bool,
    fallback_span: Span,
) -> Result<String, Diagnostic> {
    match expression {
        Expression::ArrowFunctionExpression(function) => {
            health_check_arrow_source(path, source, function)
        }
        Expression::FunctionExpression(function) => {
            health_check_function_source(path, source, function, method)
        }
        _ => Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "health check must be an inline function",
        )
        .with_path(path)
        .with_span(fallback_span)),
    }
}

fn health_check_arrow_source(
    path: &Path,
    source: &str,
    function: &oxc_ast::ast::ArrowFunctionExpression<'_>,
) -> Result<String, Diagnostic> {
    if handler_parameters_are_unsupported(&function.params) || arrow_has_typescript_syntax(function)
    {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "health check functions may declare at most one untyped context parameter",
        )
        .with_path(path)
        .with_span(function.span));
    }
    reject_health_check_captures(
        path,
        function.span,
        health_check_arrow_free_identifiers(function),
    )?;
    source_slice(source, function.span).ok_or_else(|| {
        Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "health check function source could not be extracted",
        )
        .with_path(path)
        .with_span(function.span)
    })
}

fn health_check_function_source(
    path: &Path,
    source: &str,
    function: &oxc_ast::ast::Function<'_>,
    method: bool,
) -> Result<String, Diagnostic> {
    if function.generator
        || function.body.is_none()
        || handler_parameters_are_unsupported(&function.params)
        || function_has_typescript_syntax(function)
    {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "health check functions may declare at most one untyped context parameter",
        )
        .with_path(path)
        .with_span(function.span));
    }
    reject_health_check_captures(
        path,
        function.span,
        health_check_function_free_identifiers(function),
    )?;
    if method {
        let params = source_slice(source, function.params.span).ok_or_else(|| {
            Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                "health check function source could not be extracted",
            )
            .with_path(path)
            .with_span(function.span)
        })?;
        let Some(function_body) = function.body.as_ref() else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                "health check function source could not be extracted",
            )
            .with_path(path)
            .with_span(function.span));
        };
        let body = source_slice(source, function_body.span).ok_or_else(|| {
            Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
                "health check function source could not be extracted",
            )
            .with_path(path)
            .with_span(function.span)
        })?;
        let async_prefix = if function.r#async { "async " } else { "" };
        return Ok(format!("{async_prefix}function {params} {body}"));
    }
    source_slice(source, function.span).ok_or_else(|| {
        Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
            "health check function source could not be extracted",
        )
        .with_path(path)
        .with_span(function.span)
    })
}

fn reject_health_check_captures(
    path: &Path,
    span: Span,
    free_identifiers: BTreeSet<String>,
) -> Result<(), Diagnostic> {
    if free_identifiers.is_empty() {
        return Ok(());
    }
    let identifier = free_identifiers
        .iter()
        .next()
        .map(String::as_str)
        .unwrap_or("");
    Err(Diagnostic::new(
        "SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS",
        format!("health check captures unsupported identifier '{identifier}'"),
    )
    .with_path(path)
    .with_span(span)
    .with_hint("Use inline health checks that only depend on their optional context parameter and local values."))
}

fn health_check_arrow_free_identifiers(
    function: &oxc_ast::ast::ArrowFunctionExpression<'_>,
) -> BTreeSet<String> {
    let mut scope = BTreeSet::new();
    collect_formal_parameter_bindings(&function.params, &mut scope);
    collect_function_body_bindings(&function.body.statements, &mut scope);
    let mut free = BTreeSet::new();
    collect_statement_list_identifier_references(&function.body.statements, &scope, &mut free);
    free
}

fn health_check_function_free_identifiers(
    function: &oxc_ast::ast::Function<'_>,
) -> BTreeSet<String> {
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

fn health_routes_from_options(
    path: &Path,
    source: &str,
    source_name: &str,
    span: Span,
    options: &HealthOptions,
    middleware: &[FrameworkMiddleware],
    cors: Option<&CorsPolicy>,
) -> Result<Vec<Route>, Diagnostic> {
    let mut routes = vec![
        health_route(
            path,
            source,
            source_name,
            span,
            HealthRouteSpec {
                framework_path: &options.path,
                name: "Health",
                kind: "aggregate",
                checks: options.checks.iter().collect(),
            },
            middleware,
            cors,
        )?,
        health_route(
            path,
            source,
            source_name,
            span,
            HealthRouteSpec {
                framework_path: &options.liveness_path,
                name: "Health.Liveness",
                kind: "liveness",
                checks: options
                    .checks
                    .iter()
                    .filter(|check| check.liveness)
                    .collect(),
            },
            middleware,
            cors,
        )?,
        health_route(
            path,
            source,
            source_name,
            span,
            HealthRouteSpec {
                framework_path: &options.readiness_path,
                name: "Health.Readiness",
                kind: "readiness",
                checks: options
                    .checks
                    .iter()
                    .filter(|check| check.readiness)
                    .collect(),
            },
            middleware,
            cors,
        )?,
    ];
    if let Some(startup_path) = &options.startup_path {
        routes.push(health_route(
            path,
            source,
            source_name,
            span,
            HealthRouteSpec {
                framework_path: startup_path,
                name: "Health.Startup",
                kind: "startup",
                checks: options
                    .checks
                    .iter()
                    .filter(|check| check.startup)
                    .collect(),
            },
            middleware,
            cors,
        )?);
    }
    Ok(routes)
}

fn management_routes_from_options(
    path: &Path,
    source: &str,
    source_name: &str,
    span: Span,
    options: &ManagementOptions,
    middleware: &[FrameworkMiddleware],
    cors: Option<&CorsPolicy>,
) -> Result<Vec<Route>, Diagnostic> {
    let mut routes = Vec::new();
    let ops_context = OpsRouteContext {
        path,
        source,
        source_name,
        span,
        middleware,
        cors,
    };
    if options.health {
        let health_options = HealthOptions {
            path: join_route_patterns(&options.path, "health"),
            liveness_path: join_route_patterns(&options.path, "live"),
            readiness_path: join_route_patterns(&options.path, "ready"),
            startup_path: Some(join_route_patterns(&options.path, "startup")),
            checks: vec![
                HealthCheck {
                    name: "self".to_string(),
                    check_source: "() => ({ status: \"healthy\" })".to_string(),
                    liveness: true,
                    readiness: true,
                    startup: true,
                    critical: true,
                    degraded_is_unhealthy: false,
                    tags: vec![
                        "live".to_string(),
                        "ready".to_string(),
                        "startup".to_string(),
                    ],
                },
                HealthCheck {
                    name: "runtime".to_string(),
                    check_source: "(ctx) => ((ctx && ctx.lifecycle && ctx.lifecycle.shuttingDown) ? { status: \"unhealthy\", errorCode: \"SLOPPY_HEALTH_SHUTTING_DOWN\" } : { status: \"healthy\" })".to_string(),
                    liveness: false,
                    readiness: true,
                    startup: true,
                    critical: true,
                    degraded_is_unhealthy: false,
                    tags: vec!["ready".to_string(), "startup".to_string()],
                },
                HealthCheck {
                    name: "memory".to_string(),
                    check_source: "() => ({ status: \"healthy\" })".to_string(),
                    liveness: false,
                    readiness: false,
                    startup: false,
                    critical: false,
                    degraded_is_unhealthy: false,
                    tags: vec!["health".to_string()],
                },
            ],
        };
        routes.extend(management_health_routes(
            path,
            source,
            source_name,
            span,
            &health_options,
            middleware,
            cors,
        )?);
    }
    if options.metrics {
        routes.push(ops_route(
            &ops_context,
            &join_route_patterns(&options.path, "metrics"),
            "Management.Metrics",
            "function() { return Results.text(\"# HELP sloppy_management_info Static management endpoint metadata\\n# TYPE sloppy_management_info gauge\\nsloppy_management_info 1\\n\", { contentType: \"text/plain; version=0.0.4; charset=utf-8\" }); }",
            "text",
            200,
        )?);
        routes.push(ops_route(
            &ops_context,
            &join_route_patterns(&options.path, "metrics.json"),
            "Management.MetricsJson",
            "function() { return Results.json({ counters: {}, gauges: { sloppy_management_info: { value: 1, labels: {} } }, histograms: {} }); }",
            "json",
            200,
        )?);
    }
    if options.info {
        routes.push(ops_route(
            &ops_context,
            &join_route_patterns(&options.path, "info"),
            "Management.Info",
            "function() { return Results.json({ app: { name: \"sloppy-app\" }, runtime: { name: \"sloppy\" }, management: { safe: true } }); }",
            "json",
            200,
        )?);
    }
    if options.runtime {
        routes.push(ops_route(
            &ops_context,
            &join_route_patterns(&options.path, "runtime"),
            "Management.Runtime",
            "function() { return Results.json({ lifecycle: { startupComplete: true, shuttingDown: false }, diagnostics: { count: 0 }, management: { safe: true } }); }",
            "json",
            200,
        )?);
    }
    Ok(routes)
}

fn management_health_routes(
    path: &Path,
    source: &str,
    source_name: &str,
    span: Span,
    options: &HealthOptions,
    middleware: &[FrameworkMiddleware],
    cors: Option<&CorsPolicy>,
) -> Result<Vec<Route>, Diagnostic> {
    let mut routes =
        health_routes_from_options(path, source, source_name, span, options, middleware, cors)?;
    for route in &mut routes {
        route.name = match route.health.as_ref().map(|health| health.kind) {
            Some("aggregate") => Some("Management.Health".to_string()),
            Some("liveness") => Some("Management.Live".to_string()),
            Some("readiness") => Some("Management.Ready".to_string()),
            Some("startup") => Some("Management.Startup".to_string()),
            _ => route.name.clone(),
        };
    }
    Ok(routes)
}

struct OpsRouteContext<'a> {
    path: &'a Path,
    source: &'a str,
    source_name: &'a str,
    span: Span,
    middleware: &'a [FrameworkMiddleware],
    cors: Option<&'a CorsPolicy>,
}

fn ops_route(
    context: &OpsRouteContext<'_>,
    framework_path: &str,
    name: &str,
    handler_source: &str,
    response_kind: &str,
    status: u16,
) -> Result<Route, Diagnostic> {
    let normalized_pattern = normalize_framework_route_pattern(framework_path);
    let response = ResponseMetadata {
        helper: response_kind.to_string(),
        status,
        kind: response_kind.to_string(),
        body_schema: None,
        native_body: None,
        source_name: Some(context.source_name.to_string()),
        source_text: Some(context.source.to_string()),
        span: Some(context.span),
        partial: false,
    };
    let emitted_source =
        wrap_handler_with_framework_pipeline(handler_source, context.middleware, context.cors);
    Ok(Route {
        method: "GET",
        kind: "http",
        websocket: None,
        realtime: None,
        framework_path: (normalized_pattern != framework_path).then(|| framework_path.to_string()),
        pattern: normalized_pattern,
        name: Some(name.to_string()),
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
        middleware: route_middleware_metadata(context.middleware),
        auth: None,
        cors: context.cors.map(cors_policy_metadata),
        cors_preflight: false,
        span: context.span,
        source_path: context.path.to_path_buf(),
        source_name: context.source_name.to_string(),
        source: context.source.to_string(),
        module: None,
        handler: Handler {
            source: source_slice(context.source, context.span)
                .unwrap_or_else(|| "app.management()".to_string()),
            emitted_source,
            span: context.span,
            requires_results_import: false,
            is_async: false,
            runtime_deferred: false,
            source_name: context.source_name.to_string(),
            source_text: context.source.to_string(),
            source_map_line_offset: 0,
            source_map_column_offset: 0,
            bindings: Vec::new(),
            response: Some(response.clone()),
            responses: vec![response],
            effects: Vec::new(),
            schema_metadata_conflict: false,
        },
    })
}

fn health_route(
    path: &Path,
    source: &str,
    source_name: &str,
    span: Span,
    spec: HealthRouteSpec<'_>,
    middleware: &[FrameworkMiddleware],
    cors: Option<&CorsPolicy>,
) -> Result<Route, Diagnostic> {
    let normalized_pattern = normalize_framework_route_pattern(spec.framework_path);
    let response = health_response_metadata("ok", 200, span, source_name, source);
    let responses = vec![
        response.clone(),
        health_response_metadata("status", 503, span, source_name, source),
    ];
    let check_names = spec
        .checks
        .iter()
        .map(|check| check.name.clone())
        .collect::<Vec<_>>();
    let mut handler_source = health_handler_source(spec.checks);
    handler_source = wrap_handler_with_framework_pipeline(&handler_source, middleware, cors);
    Ok(Route {
        method: "GET",
        kind: "http",
        websocket: None,
        realtime: None,
        framework_path: (normalized_pattern != spec.framework_path)
            .then(|| spec.framework_path.to_string()),
        pattern: normalized_pattern,
        name: Some(spec.name.to_string()),
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
        health: Some(HealthRouteMetadata {
            kind: spec.kind,
            checks: check_names,
        }),
        middleware: route_middleware_metadata(middleware),
        auth: None,
        cors: cors.map(cors_policy_metadata),
        cors_preflight: false,
        span,
        source_path: path.to_path_buf(),
        source_name: source_name.to_string(),
        source: source.to_string(),
        module: None,
        handler: Handler {
            source: source_slice(source, span)
                .unwrap_or_else(|| "app.mapHealthChecks()".to_string()),
            emitted_source: handler_source,
            span,
            requires_results_import: false,
            is_async: true,
            runtime_deferred: false,
            source_name: source_name.to_string(),
            source_text: source.to_string(),
            source_map_line_offset: 0,
            source_map_column_offset: 0,
            bindings: Vec::new(),
            response: Some(response),
            responses,
            effects: Vec::new(),
            schema_metadata_conflict: false,
        },
    })
}

fn health_response_metadata(
    helper: &str,
    status: u16,
    span: Span,
    source_name: &str,
    source: &str,
) -> ResponseMetadata {
    ResponseMetadata {
        helper: helper.to_string(),
        status,
        kind: "json".to_string(),
        body_schema: None,
        native_body: None,
        source_name: Some(source_name.to_string()),
        source_text: Some(source.to_string()),
        span: Some(span),
        partial: false,
    }
}

pub(super) fn app_docs_call(
    path: &Path,
    source: &str,
    source_name: &str,
    expression: &Expression<'_>,
    state: &AppState,
) -> Result<Option<Vec<Route>>, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(None);
    };
    let Some((receiver, property)) = static_member_name(&call.callee) else {
        return Ok(None);
    };
    if property != "docs" || !state.app_vars.contains(receiver) {
        return Ok(None);
    }
    if call.arguments.len() > 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_DOCS",
            "app.docs accepts at most one literal options argument",
        )
        .with_path(path)
        .with_span(call.span));
    }

    let options = docs_options_from_call(path, call)?;
    if !options.enabled {
        return Ok(Some(Vec::new()));
    }
    docs_paths_are_distinct(path, call.span, &options)?;
    Ok(Some(vec![
        docs_route(DocsRouteInput {
            path,
            source,
            source_name,
            span: call.span,
            framework_path: &options.openapi_path,
            name: "Docs.OpenApi",
            kind: "json",
            helper: "openapi",
            handler_source: docs_openapi_handler_source(),
            docs: Some(DocsRouteMetadata {
                kind: "openapi",
                strict: options.strict,
            }),
            auth: options.require_auth.clone(),
            middleware: &state.middleware,
            cors: state.cors_policy.as_ref(),
        })?,
        docs_route(DocsRouteInput {
            path,
            source,
            source_name,
            span: call.span,
            framework_path: &options.path,
            name: "Docs.Ui",
            kind: "html",
            helper: "html",
            handler_source: docs_html_handler_source(&options.title, &options.openapi_path),
            docs: Some(DocsRouteMetadata {
                kind: "ui",
                strict: options.strict,
            }),
            auth: options.require_auth,
            middleware: &state.middleware,
            cors: state.cors_policy.as_ref(),
        })?,
    ]))
}

fn docs_options_from_call(
    path: &Path,
    call: &CallExpression<'_>,
) -> Result<DocsOptions, Diagnostic> {
    let mut options = DocsOptions {
        enabled: true,
        strict: false,
        path: "/docs".to_string(),
        openapi_path: "/openapi.json".to_string(),
        title: "Sloppy API".to_string(),
        require_auth: None,
    };
    let Some(argument) = call.arguments.first() else {
        return Ok(options);
    };
    let Some(object) = object_argument(argument) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_DOCS",
            "app.docs options must be an object literal",
        )
        .with_path(path)
        .with_span(argument_span(argument).unwrap_or(call.span)));
    };
    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_DOCS",
                "app.docs options do not support spread properties",
            )
            .with_path(path)
            .with_span(object.span));
        };
        let Some(key) = property_key_name(&property.key) else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_DOCS",
                "app.docs option names must be literal",
            )
            .with_path(path)
            .with_span(property.span));
        };
        match key {
            "enabled" => {
                let Expression::BooleanLiteral(enabled) = &property.value else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_DOCS",
                        "app.docs enabled must be a boolean literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                };
                options.enabled = enabled.value;
            }
            "strict" => {
                let Expression::BooleanLiteral(strict) = &property.value else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_DOCS",
                        "app.docs strict must be a boolean literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                };
                options.strict = strict.value;
            }
            "path" => {
                let Some(value) = expression_string_literal(&property.value) else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_DOCS",
                        "app.docs path must be a string literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                };
                options.path = docs_path(path, value, property.span)?;
            }
            "openapiPath" => {
                let Some(value) = expression_string_literal(&property.value) else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_DOCS",
                        "app.docs openapiPath must be a string literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                };
                options.openapi_path = docs_path(path, value, property.span)?;
            }
            "title" => {
                let Some(value) = expression_string_literal(&property.value) else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_DOCS",
                        "app.docs title must be a string literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                };
                options.title = value.to_string();
            }
            "requireAuth" => match &property.value {
                Expression::BooleanLiteral(required) => {
                    if required.value {
                        options.require_auth = Some(AuthRequirementMetadata {
                            required: true,
                            ..AuthRequirementMetadata::default()
                        });
                    }
                }
                Expression::ObjectExpression(auth_object) => {
                    options.require_auth = Some(auth_requirement_from_object(auth_object)?);
                }
                _ => {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_DOCS",
                        "app.docs requireAuth must be a boolean literal or object literal",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                }
            },
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_DOCS",
                    format!("unsupported app.docs option '{key}'"),
                )
                .with_path(path)
                .with_span(property.span));
            }
        }
    }
    Ok(options)
}

fn docs_path(path: &Path, value: &str, span: Span) -> Result<String, Diagnostic> {
    if value.is_empty() || !value.starts_with('/') || (value.len() > 1 && value.ends_with('/')) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_DOCS",
            "app.docs paths must be absolute string literals without trailing slash",
        )
        .with_path(path)
        .with_span(span));
    }
    let normalized = normalize_framework_route_pattern(value);
    if !route_pattern_supported(&normalized) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_DOCS",
            "app.docs path is outside the Plan v1 alpha route syntax",
        )
        .with_path(path)
        .with_span(span));
    }
    Ok(normalized)
}

fn docs_paths_are_distinct(
    path: &Path,
    span: Span,
    options: &DocsOptions,
) -> Result<(), Diagnostic> {
    if options.path == options.openapi_path {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_DOCS",
            "app.docs path and openapiPath must be distinct",
        )
        .with_path(path)
        .with_span(span));
    }
    Ok(())
}

struct DocsRouteInput<'a> {
    path: &'a Path,
    source: &'a str,
    source_name: &'a str,
    span: Span,
    framework_path: &'a str,
    name: &'a str,
    kind: &'a str,
    helper: &'a str,
    handler_source: String,
    docs: Option<DocsRouteMetadata>,
    auth: Option<AuthRequirementMetadata>,
    middleware: &'a [FrameworkMiddleware],
    cors: Option<&'a CorsPolicy>,
}

fn docs_route(input: DocsRouteInput<'_>) -> Result<Route, Diagnostic> {
    let response = ResponseMetadata {
        helper: input.helper.to_string(),
        status: 200,
        kind: input.kind.to_string(),
        body_schema: None,
        native_body: None,
        source_name: Some(input.source_name.to_string()),
        source_text: Some(input.source.to_string()),
        span: Some(input.span),
        partial: false,
    };
    let handler_source =
        wrap_handler_with_framework_pipeline(&input.handler_source, input.middleware, input.cors);
    Ok(Route {
        method: "GET",
        kind: "http",
        websocket: None,
        realtime: None,
        framework_path: None,
        pattern: input.framework_path.to_string(),
        name: Some(input.name.to_string()),
        tags: vec!["Documentation".to_string()],
        summary: None,
        description: None,
        deprecated: None,
        consumes: Vec::new(),
        produces: vec![if input.kind == "html" {
            "text/html".to_string()
        } else {
            "application/json".to_string()
        }],
        headers: Vec::new(),
        query_schema: None,
        params_schema: None,
        openapi_override: None,
        output_cache: None,
        cache_headers: None,
        rate_limits: Vec::new(),
        docs: input.docs,
        health: None,
        middleware: route_middleware_metadata(input.middleware),
        auth: input.auth,
        cors: input.cors.map(cors_policy_metadata),
        cors_preflight: false,
        span: input.span,
        source_path: input.path.to_path_buf(),
        source_name: input.source_name.to_string(),
        source: input.source.to_string(),
        module: None,
        handler: Handler {
            source: source_slice(input.source, input.span)
                .unwrap_or_else(|| "app.docs()".to_string()),
            emitted_source: handler_source,
            span: input.span,
            requires_results_import: false,
            is_async: !input.middleware.is_empty() || input.cors.is_some(),
            runtime_deferred: false,
            source_name: input.source_name.to_string(),
            source_text: input.source.to_string(),
            source_map_line_offset: 0,
            source_map_column_offset: 0,
            bindings: Vec::new(),
            response: Some(response.clone()),
            responses: vec![response],
            effects: Vec::new(),
            schema_metadata_conflict: false,
        },
    })
}

fn docs_openapi_handler_source() -> String {
    "function() { return Results.json({ ok: false, code: \"SLOPPY_E_OPENAPI_ARTIFACT_REQUIRED\" }, { status: 500 }); }".to_string()
}

fn docs_html_handler_source(title: &str, openapi_path: &str) -> String {
    let html = format!(
        "<!doctype html><html><head><meta charset=\"utf-8\"><title>{}</title></head><body><pre id=\"spec\">Loading...</pre><script>fetch({}).then(r=>r.json()).then(j=>document.getElementById(\"spec\").textContent=JSON.stringify(j,null,2));</script></body></html>",
        title,
        serde_json::to_string(openapi_path).unwrap_or_else(|_| "\"/openapi.json\"".to_string())
    );
    let html = serde_json::to_string(&html).unwrap_or_else(|_| "\"\"".to_string());
    format!("function() {{ return Results.html({html}); }}")
}

fn health_handler_source(checks: Vec<&HealthCheck>) -> String {
    let entries = checks
        .iter()
        .map(|check| {
            let name = serde_json::to_string(&check.name).unwrap_or_else(|_| "\"\"".to_string());
            let critical = if check.critical { "true" } else { "false" };
            let degraded_is_unhealthy = if check.degraded_is_unhealthy {
                "true"
            } else {
                "false"
            };
            let tags = serde_json::to_string(&check.tags).unwrap_or_else(|_| "[]".to_string());
            format!(
                "{{ name: {name}, check: {}, critical: {critical}, degradedIsUnhealthy: {degraded_is_unhealthy}, tags: {tags} }}",
                check.check_source
            )
        })
        .collect::<Vec<_>>()
        .join(", ");
    format!(
        "async function(ctx) {{ const __sloppy_health_started = Date.now(); const __sloppy_health_checks = [{entries}]; const __sloppy_health_results = []; let __sloppy_health_rank = 0; for (const __sloppy_health_check of __sloppy_health_checks) {{ const __sloppy_check_started = Date.now(); let __sloppy_check_status = \"healthy\"; try {{ const __sloppy_health_value = await __sloppy_health_check.check(ctx); if (__sloppy_health_value === false) __sloppy_check_status = \"unhealthy\"; else if (__sloppy_health_value && typeof __sloppy_health_value === \"object\") {{ if (__sloppy_health_value.status === \"degraded\" || __sloppy_health_value.status === \"unhealthy\") __sloppy_check_status = __sloppy_health_value.status; else if (__sloppy_health_value.ok === false) __sloppy_check_status = \"unhealthy\"; }} }} catch {{ __sloppy_check_status = \"unhealthy\"; }} let __sloppy_aggregate_status = __sloppy_check_status; if (!__sloppy_health_check.critical && __sloppy_aggregate_status === \"unhealthy\") __sloppy_aggregate_status = \"degraded\"; if (__sloppy_health_check.critical && __sloppy_check_status === \"degraded\" && __sloppy_health_check.degradedIsUnhealthy === true) __sloppy_aggregate_status = \"unhealthy\"; const __sloppy_aggregate_rank = __sloppy_aggregate_status === \"unhealthy\" ? 2 : (__sloppy_aggregate_status === \"degraded\" ? 1 : 0); if (__sloppy_aggregate_rank > __sloppy_health_rank) __sloppy_health_rank = __sloppy_aggregate_rank; __sloppy_health_results.push({{ name: __sloppy_health_check.name, status: __sloppy_check_status, critical: __sloppy_health_check.critical, tags: __sloppy_health_check.tags, durationMs: Date.now() - __sloppy_check_started, checkedAtUtc: new Date().toISOString() }}); }} const __sloppy_health_status = __sloppy_health_rank === 2 ? \"unhealthy\" : (__sloppy_health_rank === 1 ? \"degraded\" : \"healthy\"); const __sloppy_health_body = {{ status: __sloppy_health_status, durationMs: Date.now() - __sloppy_health_started, checkedAtUtc: new Date().toISOString(), checks: __sloppy_health_results }}; return __sloppy_health_status === \"unhealthy\" ? Results.status(503, __sloppy_health_body) : Results.ok(__sloppy_health_body); }}"
    )
}

const PROBLEM_DETAILS_WRAPPER_PREFIX: &str = "async function(ctx) { try { return await (";

fn wrap_handler_with_problem_details(source: &str, detail: &str) -> String {
    let detail_branch = match detail {
        "always" => "true",
        "development" => "(String((ctx && ctx.config && typeof ctx.config.get === \"function\") ? (ctx.config.get(\"Sloppy:Environment\") ?? \"\") : \"\")).toLowerCase() === \"development\"",
        _ => "false",
    };
    format!(
        "{prefix}{source})(ctx); }} catch (error) {{ const __sloppy_problem = {{ status: 500, title: \"Internal Server Error\", code: \"SLOPPY_E_HANDLER_ERROR\" }}; if ({detail_branch}) {{ __sloppy_problem.detail = String((error && error.message) ?? error); }} return Results.problem(__sloppy_problem, {{ status: 500 }}); }} }}",
        prefix = PROBLEM_DETAILS_WRAPPER_PREFIX,
    )
}

pub(super) fn apply_problem_details_to_routes(
    path: &Path,
    routes: &mut [Route],
    descriptor: &ProblemDetailsDescriptor,
) -> Result<(), Diagnostic> {
    for route in routes {
        route.handler.emitted_source =
            wrap_handler_with_problem_details(&route.handler.emitted_source, &descriptor.detail);
        route.handler.is_async = true;
        // The wrapper prepends a single-line prefix; shift the column offset
        // so source-map mappings still anchor on the original handler body.
        route.handler.source_map_column_offset = route
            .handler
            .source_map_column_offset
            .checked_add(PROBLEM_DETAILS_WRAPPER_PREFIX.len())
            .ok_or_else(|| {
                Diagnostic::new(
                    "SLOPPYC_E_INTERNAL_OFFSET_OVERFLOW",
                    "ProblemDetails wrapper produced a source-map offset that overflows usize",
                )
                .with_path(path)
                .with_span(route.handler.span)
            })?;
        let problem_response = ResponseMetadata {
            helper: "problem".to_string(),
            status: 500,
            kind: "problem".to_string(),
            body_schema: None,
            native_body: None,
            source_name: None,
            source_text: None,
            span: None,
            partial: true,
        };
        let already_has_problem = route.handler.responses.iter().any(|response| {
            response.helper == "problem" && response.status == 500 && response.kind == "problem"
        });
        if !already_has_problem {
            route.handler.responses.push(problem_response.clone());
        }
        if route.handler.response.is_none() {
            route.handler.response = Some(problem_response);
        }
    }
    Ok(())
}

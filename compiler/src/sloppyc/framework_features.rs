use std::path::Path;

use oxc_ast::ast::{Argument, CallExpression, Expression, ObjectPropertyKind, PropertyKind};
use oxc_span::{GetSpan, Span};

use crate::diagnostic::Diagnostic;

use super::{argument_span, object_argument, property_key_name, static_member_name, AppState};

pub(super) fn unsupported_framework_feature_diagnostic(
    path: &Path,
    expression: &Expression<'_>,
    state: &AppState,
) -> Option<Diagnostic> {
    unsupported_request_id_call(path, expression, state)
        .or_else(|| unsupported_request_logging_call(path, expression, state))
        .or_else(|| unsupported_cors_call(path, expression, state))
        .or_else(|| unsupported_controller_call(path, expression, state))
        .or_else(|| unsupported_middleware_call(path, expression, state))
}

fn unsupported_request_id_call(
    path: &Path,
    expression: &Expression<'_>,
    state: &AppState,
) -> Option<Diagnostic> {
    let (call, descriptor_call) = app_use_descriptor_call(expression, state, "RequestId")?;
    if !state.request_id_imported {
        return Some(
            Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_REQUEST_ID",
                "RequestId.defaults() requires importing RequestId from \"sloppy\"",
            )
            .with_path(path)
            .with_span(descriptor_call.span),
        );
    }
    if descriptor_call.arguments.len() > 1 {
        return Some(
            Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_REQUEST_ID",
                "RequestId.defaults accepts at most one literal options object",
            )
            .with_path(path)
            .with_span(descriptor_call.span),
        );
    }
    if let Some(argument) = descriptor_call.arguments.first() {
        let Some(options) = object_argument(argument) else {
            return Some(
                Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_REQUEST_ID",
                    "dynamic RequestId options are not supported by compiler extraction",
                )
                .with_path(path)
                .with_span(argument_span(argument).unwrap_or(descriptor_call.span)),
            );
        };
        if let Some(span) = unsupported_request_id_option_span(options) {
            return Some(
                Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_REQUEST_ID",
                    "RequestId generator callbacks and dynamic options are not executable by the compiler-generated pipeline yet",
                )
                .with_path(path)
                .with_span(span),
            );
        }
    }
    Some(
        Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_REQUEST_ID",
            "RequestId middleware is recognized but not emitted by the compiler-generated pipeline yet",
        )
        .with_path(path)
        .with_span(call.span)
        .with_hint("Keep RequestId.defaults() in JavaScript app-host tests or defer it until compiler middleware emission lands."),
    )
}

fn unsupported_request_logging_call(
    path: &Path,
    expression: &Expression<'_>,
    state: &AppState,
) -> Option<Diagnostic> {
    let (call, descriptor_call) = app_use_descriptor_call(expression, state, "RequestLogging")?;
    if !state.request_logging_imported {
        return Some(
            Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_REQUEST_LOGGING",
                "RequestLogging.defaults() requires importing RequestLogging from \"sloppy\"",
            )
            .with_path(path)
            .with_span(descriptor_call.span),
        );
    }
    if descriptor_call.arguments.len() > 1 {
        return Some(
            Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_REQUEST_LOGGING",
                "RequestLogging.defaults accepts at most one literal options object",
            )
            .with_path(path)
            .with_span(descriptor_call.span),
        );
    }
    if let Some(argument) = descriptor_call.arguments.first() {
        let Some(options) = object_argument(argument) else {
            return Some(
                Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_REQUEST_LOGGING",
                    "dynamic RequestLogging options are not supported by compiler extraction",
                )
                .with_path(path)
                .with_span(argument_span(argument).unwrap_or(descriptor_call.span)),
            );
        };
        if let Some(span) = unsupported_request_logging_option_span(options) {
            return Some(
                Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_REQUEST_LOGGING",
                    "RequestLogging options must be literal booleans for compiler extraction",
                )
                .with_path(path)
                .with_span(span),
            );
        }
    }
    Some(
        Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_REQUEST_LOGGING",
            "RequestLogging middleware is recognized but not emitted by the compiler-generated pipeline yet",
        )
        .with_path(path)
        .with_span(call.span)
        .with_hint("Keep RequestLogging.defaults() in JavaScript app-host tests or defer it until compiler middleware emission lands."),
    )
}

fn unsupported_cors_call(
    path: &Path,
    expression: &Expression<'_>,
    state: &AppState,
) -> Option<Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    let (receiver, property) = static_member_name(&call.callee)?;
    if property != "useCors" || !state.app_vars.contains(receiver) {
        return None;
    }
    if call.arguments.len() != 1 {
        return Some(
            Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_CORS",
                "app.useCors requires one literal policy for compiler extraction",
            )
            .with_path(path)
            .with_span(call.span),
        );
    }
    let argument = call.arguments.first()?;
    if object_argument(argument).is_none() {
        return Some(
            Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_CORS",
                "dynamic CORS policies are not supported by compiler extraction",
            )
            .with_path(path)
            .with_span(argument_span(argument).unwrap_or(call.span)),
        );
    }
    Some(
        Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CORS",
            "CORS policies are recognized but not emitted by the compiler-generated Plan/native route table yet",
        )
        .with_path(path)
        .with_span(call.span)
        .with_hint("Plan v1 route metadata does not yet carry generated OPTIONS preflight routes."),
    )
}

fn unsupported_controller_call(
    path: &Path,
    expression: &Expression<'_>,
    state: &AppState,
) -> Option<Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    let (receiver, property) = static_member_name(&call.callee)?;
    if !matches!(property, "mapController" | "controller") {
        return None;
    }
    if !(state.app_vars.contains(receiver) || state.group_vars.contains_key(receiver)) {
        return None;
    }
    Some(
        Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CONTROLLER",
            "controller mapping is recognized but not extracted into AppGraph yet",
        )
        .with_path(path)
        .with_span(call.span)
        .with_hint("Use explicit top-level route declarations for compiler-extracted apps."),
    )
}

fn unsupported_middleware_call(
    path: &Path,
    expression: &Expression<'_>,
    state: &AppState,
) -> Option<Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    let (receiver, property) = static_member_name(&call.callee)?;
    if property != "use" {
        return None;
    }
    if state.group_vars.contains_key(receiver) {
        return Some(
            Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_MIDDLEWARE",
                "route group middleware is recognized but not extracted into AppGraph yet",
            )
            .with_path(path)
            .with_span(call.span)
            .with_hint("Use route-local handlers or defer group middleware until compiler middleware emission lands."),
        );
    }
    if !state.app_vars.contains(receiver) || call.arguments.len() != 1 {
        return None;
    }
    let argument = call.arguments.first()?;
    if matches!(
        argument,
        Argument::ArrowFunctionExpression(_) | Argument::FunctionExpression(_)
    ) {
        return Some(
            Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_MIDDLEWARE",
                "app middleware functions are recognized but not emitted by the compiler-generated pipeline yet",
            )
            .with_path(path)
            .with_span(argument_span(argument).unwrap_or(call.span))
            .with_hint("Use explicit route handlers or defer app.use(fn) until compiler middleware emission lands."),
        );
    }
    Some(
        Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_MIDDLEWARE",
            "app.use only supports extracted providers and ProblemDetails in compiler input today",
        )
        .with_path(path)
        .with_span(call.span),
    )
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
    if object == descriptor && method == "defaults" {
        Some((call, descriptor_call))
    } else {
        None
    }
}

fn unsupported_request_id_option_span(object: &oxc_ast::ast::ObjectExpression<'_>) -> Option<Span> {
    unsupported_option_span(object, |name, value| match name {
        "header" => !matches!(value, Expression::StringLiteral(_)),
        "responseHeader" | "trustIncoming" => !matches!(value, Expression::BooleanLiteral(_)),
        "generator" => true,
        _ => true,
    })
}

fn unsupported_request_logging_option_span(
    object: &oxc_ast::ast::ObjectExpression<'_>,
) -> Option<Span> {
    unsupported_option_span(object, |name, value| match name {
        "includeRoute" | "includeDuration" | "includeRequestId" => {
            !matches!(value, Expression::BooleanLiteral(_))
        }
        _ => true,
    })
}

fn unsupported_option_span(
    object: &oxc_ast::ast::ObjectExpression<'_>,
    unsupported: impl Fn(&str, &Expression<'_>) -> bool,
) -> Option<Span> {
    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Some(object.span);
        };
        if property.computed
            || property.shorthand
            || property.method
            || property.kind != PropertyKind::Init
        {
            return Some(property.span);
        }
        let Some(name) = property_key_name(&property.key) else {
            return Some(property.span);
        };
        if unsupported(name, &property.value) {
            return Some(property.value.span());
        }
    }
    None
}

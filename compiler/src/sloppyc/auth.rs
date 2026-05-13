// Application auth provider, policy, and group extraction.
use super::modules::{skip_js_block_comment, skip_js_line_comment, skip_js_quoted_literal};
use super::*;

pub(super) fn app_use_auth_provider_call(
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
    if property != "use" || !state.app_vars.contains(receiver) {
        return Ok(false);
    }
    let Some(argument) = call.arguments.first() else {
        return Ok(false);
    };
    let Some((kind, options)) = auth_provider_argument(argument) else {
        return Ok(false);
    };
    if !state.auth_imported {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_AUTH",
            "Auth providers require importing Auth from \"sloppy\"",
        )
        .with_path(path)
        .with_span(argument_span(argument).unwrap_or(call.span)));
    }
    match kind {
        "jwtBearer" => {
            let issuer = object_string_property_value(options, "issuer").map(str::to_string);
            let audience = object_string_property_value(options, "audience").map(str::to_string);
            let clock_skew_seconds = object_integer_property_value(options, "clockSkewSeconds")
                .or_else(|| object_integer_property_value(options, "clockSkew"))
                .unwrap_or(0);
            let secret_config_key = object_property_expression(options, "secret")
                .and_then(config_required_key_from_expression)
                .ok_or_else(|| {
                    Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_AUTH",
                        "Auth.jwtBearer requires secret: Config.required(\"...\") for compiler extraction",
                    )
                    .with_path(path)
                    .with_span(call.span)
                })?
                .to_string();
            state.config_reads.push(config_required_read(
                &secret_config_key,
                "secret",
                true,
                source,
                source_name,
                call.span,
            ));
            state.auth.schemes.push(AuthSchemeMetadata::JwtBearer {
                name: "bearerAuth".to_string(),
                issuer,
                audience,
                clock_skew_seconds,
                secret_config_key: Some(secret_config_key),
            });
        }
        "apiKey" => {
            let header = object_string_property_value(options, "header")
                .unwrap_or("x-api-key")
                .to_string();
            let config_keys = if let Some(key) = object_string_property_value(options, "configKey")
            {
                vec![key.to_string()]
            } else {
                config_required_keys_from_source(&source_slice(source, call.span).unwrap_or_default())
                    .ok_or_else(|| {
                        Diagnostic::new(
                            "SLOPPYC_E_UNSUPPORTED_AUTH",
                            "Auth.apiKey requires a literal Config.required(\"...\") validator reference or configKey for compiler extraction",
                        )
                        .with_path(path)
                        .with_span(call.span)
                    })?
            };
            let [config_key] = config_keys.as_slice() else {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_AUTH",
                    "Auth.apiKey requires exactly one config key for compiler extraction",
                )
                .with_path(path)
                .with_span(call.span));
            };
            state.config_reads.push(config_required_read(
                config_key,
                "secret",
                true,
                source,
                source_name,
                call.span,
            ));
            state.auth.schemes.push(AuthSchemeMetadata::ApiKey {
                name: "apiKeyAuth".to_string(),
                header,
                config_key: Some(config_key.clone()),
            });
        }
        "cookieSession" => {
            let cookie = object_string_property_value(options, "name")
                .unwrap_or("sloppy.session")
                .to_string();
            let secure = object_bool_property_value(options, "secure").unwrap_or(true);
            let http_only = object_bool_property_value(options, "httpOnly").unwrap_or(true);
            let same_site = object_string_property_value(options, "sameSite")
                .unwrap_or("lax")
                .to_string();
            let cookie_path = object_string_property_value(options, "path")
                .unwrap_or("/")
                .to_string();
            let max_age_seconds = object_integer_property_value(options, "maxAgeSeconds")
                .or_else(|| object_integer_property_value(options, "maxAge"));
            let store = if let Some(store_expr) = object_property_expression(options, "store") {
                if cookie_session_store_kind(store_expr) == Some("memory") {
                    Some("memory".to_string())
                } else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_AUTH",
                        "Auth.cookieSession compiler extraction supports Auth.sessionStore.memory() for generated session stores; DB session stores remain stdlib runtime objects",
                    )
                    .with_path(path)
                    .with_span(store_expr.span()));
                }
            } else {
                None
            };
            let idle_timeout_ms = object_integer_property_value(options, "idleTimeoutMs")
                .or_else(|| object_integer_property_value(options, "idleTimeout"));
            let absolute_timeout_ms = object_integer_property_value(options, "absoluteTimeoutMs")
                .or_else(|| object_integer_property_value(options, "absoluteTimeout"));
            let rotation = object_bool_property_value(options, "rotation")
                .or_else(|| object_bool_property_value(options, "rotate"))
                .unwrap_or(false);
            let secret_config_key = object_property_expression(options, "secret")
                .and_then(config_required_key_from_expression)
                .ok_or_else(|| {
                    Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_AUTH",
                        "Auth.cookieSession requires secret: Config.required(\"...\") for compiler extraction",
                    )
                    .with_path(path)
                    .with_span(call.span)
                })?
                .to_string();
            state.config_reads.push(config_required_read(
                &secret_config_key,
                "secret",
                true,
                source,
                source_name,
                call.span,
            ));
            state.auth.schemes.push(AuthSchemeMetadata::CookieSession {
                name: "cookieSessionAuth".to_string(),
                cookie,
                secure,
                http_only,
                same_site,
                path: cookie_path,
                max_age_seconds,
                store,
                idle_timeout_ms,
                absolute_timeout_ms,
                rotation,
                secret_config_key: Some(secret_config_key),
            });
        }
        _ => {}
    }
    Ok(true)
}

pub(super) fn app_auth_policy_call(
    path: &Path,
    source: &str,
    expression: &Expression<'_>,
    state: &mut AppState,
) -> Result<bool, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(false);
    };
    let Expression::StaticMemberExpression(method) = &call.callee else {
        return Ok(false);
    };
    if method.property.name.as_str() != "addPolicy" {
        return Ok(false);
    }
    let Expression::StaticMemberExpression(auth_member) = &method.object else {
        return Ok(false);
    };
    if auth_member.property.name.as_str() != "auth" {
        return Ok(false);
    }
    let Expression::Identifier(app) = &auth_member.object else {
        return Ok(false);
    };
    if !state.app_vars.contains(app.name.as_str()) {
        return Ok(false);
    }
    let Some(name) = call.arguments.first().and_then(string_argument) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_AUTH",
            "app.auth.addPolicy requires a literal policy name",
        )
        .with_path(path)
        .with_span(call.span));
    };
    let Some(policy_argument) = call.arguments.get(1) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_AUTH",
            "app.auth.addPolicy requires an inline policy function",
        )
        .with_path(path)
        .with_span(call.span));
    };
    let policy_span = argument_span(policy_argument).unwrap_or(call.span);
    let policy_source = source_slice(source, policy_span).ok_or_else(|| {
        Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_AUTH",
            "app.auth.addPolicy policy source could not be extracted",
        )
        .with_path(path)
        .with_span(policy_span)
    })?;
    let policy_source = if policy_source.trim_start().starts_with("Auth.policy") {
        policy_source.replacen("Auth.policy", "__sloppy_auth_policy", 1)
    } else {
        validate_auth_policy_source(path, &policy_source, policy_span)?;
        policy_source
    };
    if !state.auth.policies.iter().any(|policy| policy.name == name) {
        state.auth.policies.push(AuthPolicyMetadata {
            name: name.to_string(),
            source: Some(policy_source),
        });
    }
    Ok(true)
}

pub(super) fn route_group_auth_call(
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
    if !state.group_vars.contains_key(receiver) {
        return Ok(false);
    }
    let requirement = match property {
        "requireAuth" | "requiresAuth" => auth_requirement_from_call(call)
            .map_err(|diagnostic| diagnostic.with_path(path).with_span(call.span))?,
        "allowAnonymous" => anonymous_auth_requirement(),
        _ => return Ok(false),
    };
    if let Some(group) = state.group_vars.get_mut(receiver) {
        group.auth = Some(requirement);
    }
    Ok(true)
}

fn auth_provider_argument<'a>(
    argument: &'a Argument<'a>,
) -> Option<(&'a str, &'a oxc_ast::ast::ObjectExpression<'a>)> {
    let Argument::CallExpression(call) = argument else {
        return None;
    };
    let (receiver, property) = static_member_name(&call.callee)?;
    if receiver != "Auth"
        || !matches!(property, "jwtBearer" | "apiKey" | "cookieSession")
        || call.arguments.len() != 1
    {
        return None;
    }
    let options = object_argument(call.arguments.first()?)?;
    Some((property, options))
}

fn object_property_expression<'a>(
    object: &'a oxc_ast::ast::ObjectExpression<'a>,
    name: &str,
) -> Option<&'a Expression<'a>> {
    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            continue;
        };
        if property.computed || property_key_name(&property.key) != Some(name) {
            continue;
        }
        return Some(&property.value);
    }
    None
}

fn config_required_key_from_expression<'a>(expression: &'a Expression<'a>) -> Option<&'a str> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    let (receiver, property) = static_member_name(&call.callee)?;
    if receiver != "Config" || property != "required" || call.arguments.len() != 1 {
        return None;
    }
    call.arguments.first().and_then(string_argument)
}

fn cookie_session_store_kind(expression: &Expression<'_>) -> Option<&'static str> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    let chain = static_member_chain(&call.callee)?;
    if chain == ["Auth", "sessionStore", "memory"] {
        return Some("memory");
    }
    None
}

fn config_required_keys_from_source(source: &str) -> Option<Vec<String>> {
    let mut keys = Vec::new();
    let mut rest = source;
    while let Some(index) = rest.find("Config.required") {
        rest = &rest[index + "Config.required".len()..];
        let open = rest.find('(')?;
        rest = &rest[open + 1..];
        let trimmed = rest.trim_start();
        let quote = trimmed
            .chars()
            .next()
            .filter(|ch| *ch == '"' || *ch == '\'')?;
        let key_start = quote.len_utf8();
        let end = trimmed[key_start..].find(quote)?;
        keys.push(trimmed[key_start..key_start + end].to_string());
        rest = &trimmed[key_start + end + quote.len_utf8()..];
    }
    Some(keys)
}

fn validate_auth_policy_source(path: &Path, source: &str, span: Span) -> Result<(), Diagnostic> {
    let params = auth_policy_parameters(source).ok_or_else(|| {
        Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_AUTH",
            "app.auth.addPolicy compiler extraction supports inline functions with simple parameters",
        )
        .with_path(path)
        .with_span(span)
    })?;
    if let Some(identifier) = captured_auth_policy_identifier(source, &params) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_AUTH",
            format!(
                "app.auth.addPolicy inline policy cannot capture '{identifier}' in compiler output"
            ),
        )
        .with_path(path)
        .with_span(span));
    }
    Ok(())
}

fn auth_policy_parameters(source: &str) -> Option<BTreeSet<String>> {
    let mut trimmed = source.trim_start();
    if let Some(rest) = trimmed.strip_prefix("async") {
        if rest.starts_with(char::is_whitespace) {
            trimmed = rest.trim_start();
        }
    }
    if let Some(rest) = trimmed.strip_prefix("function") {
        let open = rest.find('(')?;
        let close = rest[open + 1..].find(')')? + open + 1;
        return auth_policy_parameter_list(&rest[open + 1..close]);
    }
    if let Some(rest) = trimmed.strip_prefix('(') {
        let close = rest.find(')')?;
        let after = rest[close + 1..].trim_start();
        if !after.starts_with("=>") {
            return None;
        }
        return auth_policy_parameter_list(&rest[..close]);
    }
    let arrow = trimmed.find("=>")?;
    let parameter = trimmed[..arrow].trim();
    if js_identifier(parameter) {
        let mut params = BTreeSet::new();
        params.insert(parameter.to_string());
        return Some(params);
    }
    None
}

fn auth_policy_parameter_list(source: &str) -> Option<BTreeSet<String>> {
    let mut params = BTreeSet::new();
    let trimmed = source.trim();
    if trimmed.is_empty() {
        return Some(params);
    }
    for part in trimmed.split(',') {
        let name = part.trim();
        if !js_identifier(name) {
            return None;
        }
        params.insert(name.to_string());
    }
    Some(params)
}

fn captured_auth_policy_identifier(source: &str, params: &BTreeSet<String>) -> Option<String> {
    let bytes = source.as_bytes();
    let mut index = 0usize;
    while index < bytes.len() {
        match bytes[index] {
            b'\'' | b'"' | b'`' => {
                index = skip_js_quoted_literal(bytes, index);
                continue;
            }
            b'/' if index + 1 < bytes.len() && bytes[index + 1] == b'/' => {
                index = skip_js_line_comment(bytes, index + 2);
                continue;
            }
            b'/' if index + 1 < bytes.len() && bytes[index + 1] == b'*' => {
                index = skip_js_block_comment(bytes, index + 2);
                continue;
            }
            _ => {}
        }
        if js_identifier_start(bytes[index]) {
            let start = index;
            index += 1;
            while index < bytes.len() && js_identifier_part(bytes[index]) {
                index += 1;
            }
            let identifier = &source[start..index];
            if auth_policy_identifier_allowed(source, start, index, identifier, params) {
                continue;
            }
            return Some(identifier.to_string());
        }
        index += 1;
    }
    None
}

fn auth_policy_identifier_allowed(
    source: &str,
    start: usize,
    end: usize,
    identifier: &str,
    params: &BTreeSet<String>,
) -> bool {
    if params.contains(identifier)
        || auth_policy_keyword(identifier)
        || auth_policy_global(identifier)
    {
        return true;
    }
    if previous_non_ws_byte(source.as_bytes(), start) == Some(b'.') {
        return true;
    }
    if next_non_ws_byte(source.as_bytes(), end) == Some(b':') {
        return true;
    }
    false
}

fn auth_policy_keyword(identifier: &str) -> bool {
    matches!(
        identifier,
        "async"
            | "await"
            | "break"
            | "case"
            | "catch"
            | "const"
            | "continue"
            | "default"
            | "do"
            | "else"
            | "false"
            | "finally"
            | "for"
            | "function"
            | "if"
            | "in"
            | "instanceof"
            | "let"
            | "new"
            | "null"
            | "return"
            | "switch"
            | "throw"
            | "true"
            | "try"
            | "typeof"
            | "undefined"
            | "var"
            | "void"
            | "while"
    )
}

fn auth_policy_global(identifier: &str) -> bool {
    matches!(
        identifier,
        "Array"
            | "Boolean"
            | "Date"
            | "JSON"
            | "Map"
            | "Math"
            | "Number"
            | "Object"
            | "RegExp"
            | "Set"
            | "String"
    )
}

fn js_identifier(source: &str) -> bool {
    let mut bytes = source.bytes();
    let Some(first) = bytes.next() else {
        return false;
    };
    js_identifier_start(first) && bytes.all(js_identifier_part)
}

fn js_identifier_start(byte: u8) -> bool {
    byte.is_ascii_alphabetic() || byte == b'_' || byte == b'$'
}

fn js_identifier_part(byte: u8) -> bool {
    js_identifier_start(byte) || byte.is_ascii_digit()
}

fn previous_non_ws_byte(bytes: &[u8], start: usize) -> Option<u8> {
    let mut index = start;
    while index > 0 {
        index -= 1;
        if !bytes[index].is_ascii_whitespace() {
            return Some(bytes[index]);
        }
    }
    None
}

fn next_non_ws_byte(bytes: &[u8], start: usize) -> Option<u8> {
    let mut index = start;
    while index < bytes.len() {
        if !bytes[index].is_ascii_whitespace() {
            return Some(bytes[index]);
        }
        index += 1;
    }
    None
}

fn config_required_read(
    key: &str,
    value_type: &str,
    sensitive: bool,
    source: &str,
    source_name: &str,
    span: Span,
) -> ConfigReadMetadata {
    ConfigReadMetadata {
        key: key.to_string(),
        value_type: value_type.to_string(),
        has_default: false,
        default_value: None,
        required: true,
        sensitive: sensitive || config_key_is_sensitive(key),
        source_name: source_name.to_string(),
        source: source.to_string(),
        span,
    }
}

// App-level capabilities, config/provider metadata, ORM declarations, and service registration.
use super::*;

pub(super) fn database_capability_call(
    path: &Path,
    source: &str,
    source_name: &str,
    expression: &Expression<'_>,
    state: &AppState,
) -> Result<Option<DatabaseCapability>, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(None);
    };
    let Some(chain) = static_member_chain(&call.callee) else {
        return Ok(None);
    };
    if chain.len() != 3 || chain[1] != "capabilities" || chain[2] != "addDatabase" {
        return Ok(None);
    }
    if !state.builder_vars.contains(chain[0]) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CAPABILITY_TARGET",
            "database capabilities must be declared on the extracted builder",
        )
        .with_path(path)
        .with_span(call.span)
        .with_hint("Use builder.capabilities.addDatabase(...) before builder.build()."));
    }
    if call.arguments.len() != 2 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CAPABILITY_SHAPE",
            "addDatabase requires a literal token and an options object",
        )
        .with_path(path)
        .with_span(call.span));
    }

    let token = string_argument(call.arguments.first().ok_or_else(|| {
        Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CAPABILITY_SHAPE",
            "missing database capability token",
        )
        .with_path(path)
        .with_span(call.span)
    })?)
    .ok_or_else(|| {
        Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CAPABILITY_TOKEN",
            "database capability token must be a string literal",
        )
        .with_path(path)
        .with_span(argument_span(&call.arguments[0]).unwrap_or(call.span))
    })?;
    if !plan_token_supported(token) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CAPABILITY_TOKEN",
            "database capability token uses unsupported characters",
        )
        .with_path(path)
        .with_span(argument_span(&call.arguments[0]).unwrap_or(call.span))
        .with_hint("Use letters, digits, '.', '_', and '-' in capability tokens."));
    }

    let options = object_argument(call.arguments.get(1).ok_or_else(|| {
        Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CAPABILITY_SHAPE",
            "missing database capability options",
        )
        .with_path(path)
        .with_span(call.span)
    })?)
    .ok_or_else(|| {
        Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CAPABILITY_SHAPE",
            "database capability options must be an object literal",
        )
        .with_path(path)
        .with_span(argument_span(&call.arguments[1]).unwrap_or(call.span))
    })?;

    reject_secret_option_fields(path, options)?;

    let provider = required_object_string_property(path, options, "provider")?;
    if !database_provider_supported(provider) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_DATA_PROVIDER",
            "compiler-emitted database provider metadata supports sqlite, postgres, and sqlserver",
        )
        .with_path(path)
        .with_span(options.span)
        .with_hint(
            "Use one of the first-party database provider values supported by Plan metadata.",
        ));
    }

    let access = optional_object_string_property(path, options, "access")?
        .unwrap_or("readwrite")
        .to_string();
    if !matches!(access.as_str(), "read" | "write" | "readwrite") {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CAPABILITY_ACCESS",
            "database capability access must be read, write, or readwrite",
        )
        .with_path(path)
        .with_span(options.span));
    }

    // `path` is a transitional alias: output canonicalizes to `database`, and conflicting
    // dual-field values are rejected so generated plans stay unambiguous.
    let database = optional_object_string_property(path, options, "database")?;
    let path_option = optional_object_string_property(path, options, "path")?;
    if let (Some(database), Some(path_option)) = (database, path_option) {
        if database != path_option {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_CAPABILITY_SHAPE",
                "database capability cannot declare different database and path values",
            )
            .with_path(path)
            .with_span(options.span)
            .with_hint(
                "Use the canonical database option; path is accepted only as a transitional alias.",
            ));
        }
    }
    let database = database.or(path_option).map(|value| value.to_string());

    Ok(Some(DatabaseCapability {
        token: token.to_string(),
        capability_kind: "database".to_string(),
        provider: provider.to_string(),
        config_name: None,
        config_key: optional_object_string_property(path, options, "configKey")?
            .map(ToOwned::to_owned),
        access,
        database,
        config_source: None,
        source_name: source_name.to_string(),
        source: source.to_string(),
        span: call.span,
        from_provider_use: false,
    }))
}

pub(super) fn database_provider_supported(provider: &str) -> bool {
    matches!(provider, "sqlite" | "postgres" | "sqlserver")
}

pub(super) fn top_level_statement_diagnostic(
    path: &Path,
    source: &str,
    statement: &Statement<'_>,
) -> Diagnostic {
    let span = statement.span();
    let text = source_slice(source, span).unwrap_or_default();
    if top_level_statement_is_conditional(statement) && text.contains(".map") {
        return Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CONDITIONAL_ROUTE_REGISTRATION",
            "conditional route registration cannot be represented as complete static metadata",
        )
        .with_path(path)
        .with_span(span)
        .with_hint("Register compiler-extracted routes unconditionally at the top level.");
    }
    if top_level_statement_is_loop(statement) && text.contains(".map") {
        return Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_LOOP_ROUTE_REGISTRATION",
            "loop-based route registration cannot be represented as complete static metadata",
        )
        .with_path(path)
        .with_span(span)
        .with_hint("List compiler-extracted routes as explicit top-level route method calls.");
    }

    Diagnostic::new(
        "SLOPPYC_E_UNSUPPORTED_TOP_LEVEL",
        "unsupported top-level syntax in supported app compiler",
    )
    .with_path(path)
    .with_span(span)
    .with_hint(
        "Use imports, const app/builder/group declarations, literal route calls, and export default app.",
    )
}

pub(super) fn record_dynamic_top_level_route_statement(
    source: &str,
    source_name: &str,
    statement: &Statement<'_>,
    state: &mut AppState,
) -> bool {
    if !(top_level_statement_is_conditional(statement) || top_level_statement_is_loop(statement)) {
        return false;
    }
    let text = source_slice(source, statement.span()).unwrap_or_default();
    if !text.contains(".get(")
        && !text.contains(".post(")
        && !text.contains(".put(")
        && !text.contains(".patch(")
        && !text.contains(".delete(")
        && !text.contains(".map")
    {
        return false;
    }
    state.dynamic_routes.push(DynamicRoute {
        method: None,
        pattern: None,
        pattern_reason: "route registration is inside dynamic control flow",
        handler_known: false,
        reason: if top_level_statement_is_conditional(statement) {
            "conditional route registration cannot be represented as complete static metadata"
        } else {
            "loop-based route registration cannot be represented as complete static metadata"
        },
        span: statement.span(),
        source_name: source_name.to_string(),
        source: source.to_string(),
    });
    true
}

pub(super) fn top_level_statement_is_conditional(statement: &Statement<'_>) -> bool {
    matches!(
        statement,
        Statement::IfStatement(_) | Statement::SwitchStatement(_)
    )
}

pub(super) fn top_level_statement_is_loop(statement: &Statement<'_>) -> bool {
    matches!(
        statement,
        Statement::ForStatement(_)
            | Statement::ForInStatement(_)
            | Statement::ForOfStatement(_)
            | Statement::WhileStatement(_)
            | Statement::DoWhileStatement(_)
    )
}

pub(super) fn export_default_identifier(
    declaration: &oxc_ast::ast::ExportDefaultDeclarationKind<'_>,
) -> Option<String> {
    match declaration {
        oxc_ast::ast::ExportDefaultDeclarationKind::Identifier(identifier) => {
            Some(identifier.name.as_str().to_string())
        }
        _ => None,
    }
}

pub(super) fn binding_identifier<'a>(binding: &'a BindingPattern<'a>) -> Option<&'a str> {
    match binding {
        BindingPattern::BindingIdentifier(identifier) => Some(identifier.name.as_str()),
        _ => None,
    }
}

pub(super) fn is_sloppy_factory_call(expression: &Expression<'_>, method: &str) -> bool {
    let Expression::CallExpression(call) = expression else {
        return false;
    };
    static_member_name(&call.callee)
        .is_some_and(|(object, property)| object == "Sloppy" && property == method)
        && call.arguments.is_empty()
}

pub(super) fn builder_build_object<'a>(expression: &'a Expression<'a>) -> Option<&'a str> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    if !call.arguments.is_empty() {
        return None;
    }
    static_member_name(&call.callee).and_then(|(object, property)| {
        if property == "build" {
            Some(object)
        } else {
            None
        }
    })
}

pub(super) fn app_group_call<'a>(
    expression: &'a Expression<'a>,
) -> Result<Option<(&'a str, &'a str, RouteMetadata)>, Diagnostic> {
    let mut current = expression;
    let mut metadata = RouteMetadata::default();
    let mut tag_groups = Vec::new();
    loop {
        let Expression::CallExpression(call) = current else {
            return Ok(None);
        };
        let Expression::StaticMemberExpression(member) = &call.callee else {
            break;
        };
        match member.property.name.as_str() {
            "withTags" => {
                tag_groups.push(route_tags_from_arguments(call)?);
                current = &member.object;
            }
            "requireAuth" | "requiresAuth" => {
                merge_auth_requirement(&mut metadata.auth, auth_requirement_from_call(call)?);
                current = &member.object;
            }
            "allowAnonymous" => {
                merge_auth_requirement(&mut metadata.auth, anonymous_auth_requirement());
                current = &member.object;
            }
            _ => break,
        }
    }
    for tags in tag_groups.into_iter().rev() {
        metadata.tags.extend(tags);
    }

    let Expression::CallExpression(call) = current else {
        return Ok(None);
    };
    let Some((object, property)) = static_member_name(&call.callee) else {
        return Ok(None);
    };
    if !matches!(property, "mapGroup" | "group") || call.arguments.len() != 1 {
        return Ok(None);
    }
    let Some(prefix) = call.arguments.first().and_then(string_argument) else {
        return Ok(None);
    };
    Ok(Some((object, prefix, metadata)))
}

pub(super) fn app_provider_lookup(
    expression: &Expression<'_>,
    state: &AppState,
) -> Option<ProviderBinding> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    let (receiver, property) = static_member_name(&call.callee)?;
    if property != "provider" || !state.app_vars.contains(receiver) || call.arguments.len() != 1 {
        return None;
    }
    let token = string_argument(call.arguments.first()?)?;
    database_provider_binding_from_token(token)
}

pub(super) fn config_read_metadata(
    _path: &Path,
    source: &str,
    source_name: &str,
    state: &AppState,
    expression: &Expression<'_>,
) -> Result<Option<Vec<ConfigReadMetadata>>, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(None);
    };
    if let Some(read) = config_call_metadata(call, source, source_name, state) {
        return Ok(Some(vec![read]));
    }
    let Some(reads) = config_bind_metadata(call, source, source_name, state) else {
        return Ok(None);
    };
    Ok(Some(reads))
}

pub(super) fn config_call_metadata(
    call: &CallExpression<'_>,
    source: &str,
    source_name: &str,
    state: &AppState,
) -> Option<ConfigReadMetadata> {
    let Expression::StaticMemberExpression(method_member) = &call.callee else {
        return None;
    };
    let method = method_member.property.name.as_str();
    let Expression::StaticMemberExpression(config_member) = &method_member.object else {
        return None;
    };
    if config_member.property.name.as_str() != "config" {
        return None;
    }
    let Expression::Identifier(app) = &config_member.object else {
        return None;
    };
    if !state.app_vars.contains(app.name.as_str()) || call.arguments.is_empty() {
        return None;
    }
    let key = string_argument(call.arguments.first()?)?.to_string();
    let value_type = match method {
        "getString" => "string",
        "getInt" => "int",
        "getNumber" => "number",
        "getBoolean" => "bool",
        "getBool" => "bool",
        "getDuration" => "duration",
        "getSize" | "getBytes" => "size",
        "getArray" => "array",
        "getObject" => "object",
        "getSecret" => "secret",
        _ => return None,
    };
    let default_value = call.arguments.get(1).and_then(argument_json_value);
    let has_default = call.arguments.len() > 1;
    Some(ConfigReadMetadata {
        sensitive: method == "getSecret" || config_key_is_sensitive(&key),
        required: !has_default,
        key,
        value_type: value_type.to_string(),
        has_default,
        default_value,
        source_name: source_name.to_string(),
        source: source.to_string(),
        span: call.span,
    })
}

pub(super) fn config_bind_metadata(
    call: &CallExpression<'_>,
    source: &str,
    source_name: &str,
    state: &AppState,
) -> Option<Vec<ConfigReadMetadata>> {
    let Expression::StaticMemberExpression(method_member) = &call.callee else {
        return None;
    };
    if method_member.property.name.as_str() != "bind" {
        return None;
    }
    let Expression::StaticMemberExpression(config_member) = &method_member.object else {
        return None;
    };
    if config_member.property.name.as_str() != "config" {
        return None;
    }
    let Expression::Identifier(app) = &config_member.object else {
        return None;
    };
    if !state.app_vars.contains(app.name.as_str()) || call.arguments.is_empty() {
        return None;
    }
    let prefix = string_argument(call.arguments.first()?)?;
    let Some(schema) = call.arguments.get(1).and_then(object_argument) else {
        return Some(vec![ConfigReadMetadata {
            key: provider_config_prefix(prefix),
            value_type: "object".to_string(),
            has_default: false,
            default_value: None,
            required: false,
            sensitive: false,
            source_name: source_name.to_string(),
            source: source.to_string(),
            span: call.span,
        }]);
    };
    let mut reads = Vec::new();
    for property in &schema.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            continue;
        };
        if property.computed {
            continue;
        }
        let name = property_key_name(&property.key)?;
        let Some((key_segment, value_type, has_default, default_value, required, sensitive)) =
            bind_descriptor_metadata(name, &property.value)
        else {
            continue;
        };
        let key = format!("{}:{}", provider_config_prefix(prefix), key_segment);
        reads.push(ConfigReadMetadata {
            key,
            value_type,
            has_default,
            default_value,
            required,
            sensitive,
            source_name: source_name.to_string(),
            source: source.to_string(),
            span: property.span,
        });
    }
    Some(reads)
}

pub(super) fn config_bind_helper_source(
    binding_name: &str,
    expression: &Expression<'_>,
    source: &str,
    source_name: &str,
    state: &AppState,
) -> Option<(Vec<ConfigReadMetadata>, String)> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    let Expression::StaticMemberExpression(method_member) = &call.callee else {
        return None;
    };
    if method_member.property.name.as_str() != "bind" {
        return None;
    }
    let Expression::StaticMemberExpression(config_member) = &method_member.object else {
        return None;
    };
    if config_member.property.name.as_str() != "config" {
        return None;
    }
    let Expression::Identifier(app) = &config_member.object else {
        return None;
    };
    if !state.app_vars.contains(app.name.as_str()) || call.arguments.is_empty() {
        return None;
    }
    let prefix = string_argument(call.arguments.first()?)?;
    let schema = call.arguments.get(1).and_then(object_argument)?;
    let mut reads = Vec::new();
    let mut fields = Vec::new();
    for property in &schema.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            continue;
        };
        if property.computed {
            continue;
        }
        let name = property_key_name(&property.key)?;
        let Some((key_segment, value_type, has_default, default_value, required, sensitive)) =
            bind_descriptor_metadata(name, &property.value)
        else {
            continue;
        };
        let key = format!("{}:{}", provider_config_prefix(prefix), key_segment);
        reads.push(ConfigReadMetadata {
            key: key.clone(),
            value_type: value_type.clone(),
            has_default,
            default_value: default_value.clone(),
            required,
            sensitive,
            source_name: source_name.to_string(),
            source: source.to_string(),
            span: property.span,
        });
        fields.push(json!({
            "property": name,
            "key": key,
            "type": value_type,
            "hasDefault": has_default,
            "default": default_value,
            "required": required,
        }));
    }
    let fields = serde_json::to_string(&fields).ok()?;
    Some((
        reads,
        format!("const {binding_name} = __sloppy_config_bind({fields});"),
    ))
}

pub(super) fn bind_descriptor_metadata(
    name: &str,
    expression: &Expression<'_>,
) -> Option<(String, String, bool, Option<Value>, bool, bool)> {
    match expression {
        Expression::StringLiteral(literal) => {
            let value_type = literal.value.as_str();
            if !config_value_type_supported(value_type) {
                return None;
            }
            Some((
                config_bind_descriptor_segment(name),
                value_type.to_string(),
                false,
                None,
                true,
                value_type == "secret",
            ))
        }
        Expression::ObjectExpression(object) => {
            let value_type = object_string_property_value(object, "type")
                .or_else(|| {
                    object_bool_property_value(object, "secret")
                        .filter(|secret| *secret)
                        .map(|_| "secret")
                })
                .unwrap_or("string");
            if !config_value_type_supported(value_type) {
                return None;
            }
            let default_value = object_json_property_value(object, "default");
            let has_default = default_value.is_some();
            let required = object_bool_property_value(object, "required").unwrap_or(!has_default);
            let key_segment = object_string_property_value(object, "key")
                .map(str::to_string)
                .unwrap_or_else(|| config_bind_descriptor_segment(name));
            Some((
                key_segment,
                value_type.to_string(),
                has_default,
                default_value,
                required,
                value_type == "secret"
                    || object_bool_property_value(object, "secret").unwrap_or(false),
            ))
        }
        _ => None,
    }
}

pub(super) fn config_value_type_supported(value_type: &str) -> bool {
    matches!(
        value_type,
        "array"
            | "bool"
            | "boolean"
            | "bytes"
            | "duration"
            | "int"
            | "integer"
            | "number"
            | "object"
            | "secret"
            | "size"
            | "string"
    )
}

pub(super) fn provider_config_prefix(prefix: &str) -> String {
    if prefix.contains(':') && !prefix.starts_with("Sloppy:") && prefix.split(':').count() == 2 {
        let mut segments = prefix.split(':');
        let provider = segments.next().unwrap_or_default();
        let name = segments.next().unwrap_or_default();
        return format!("Sloppy:Providers:{provider}:{name}");
    }
    prefix.to_string()
}

pub(super) fn config_bind_descriptor_segment(name: &str) -> String {
    if name.contains(':') {
        return name.to_string();
    }
    let mut chars = name.chars();
    let Some(first) = chars.next() else {
        return name.to_string();
    };
    first.to_ascii_uppercase().to_string() + chars.as_str()
}

pub(super) fn malformed_config_read_diagnostic(
    path: &Path,
    state: &AppState,
    expression: &Expression<'_>,
) -> Option<Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    let Expression::StaticMemberExpression(method_member) = &call.callee else {
        return None;
    };
    let method = method_member.property.name.as_str();
    if !matches!(
        method,
        "bind"
            | "getArray"
            | "getBool"
            | "getBoolean"
            | "getBytes"
            | "getDuration"
            | "getInt"
            | "getNumber"
            | "getObject"
            | "getSecret"
            | "getSize"
            | "getString"
    ) {
        return None;
    }
    let Expression::StaticMemberExpression(config_member) = &method_member.object else {
        return None;
    };
    if config_member.property.name.as_str() != "config" {
        return None;
    }
    let Expression::Identifier(app) = &config_member.object else {
        return None;
    };
    if !state.app_vars.contains(app.name.as_str()) {
        return None;
    }
    Some(
        Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_CONFIG_KEY",
            "config helper keys must be string literals",
        )
        .with_path(path)
        .with_span(call.span),
    )
}

pub(super) fn sqlite_provider_call(
    expression: &Expression<'_>,
    source: &str,
    source_name: &str,
) -> Option<DatabaseCapability> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    sqlite_provider_call_expression(call, source, source_name)
}

pub(super) fn sqlite_provider_call_expression(
    call: &CallExpression<'_>,
    source: &str,
    source_name: &str,
) -> Option<DatabaseCapability> {
    let Expression::Identifier(callee) = &call.callee else {
        return None;
    };
    if callee.name.as_str() != "sqlite" {
        return None;
    };
    if call.arguments.is_empty() || call.arguments.len() > 2 {
        return None;
    }
    let name = string_argument(call.arguments.first()?)?;
    Some(DatabaseCapability {
        token: normalize_sqlite_provider_token(name),
        capability_kind: "database".to_string(),
        provider: "sqlite".to_string(),
        config_name: Some(name.to_string()),
        config_key: None,
        access: "readwrite".to_string(),
        database: None,
        config_source: None,
        source_name: source_name.to_string(),
        source: source.to_string(),
        span: call.span,
        from_provider_use: true,
    })
}

pub(super) fn app_use_provider_call(
    path: &Path,
    source: &str,
    source_name: &str,
    expression: &Expression<'_>,
    state: &AppState,
) -> Result<Option<DatabaseCapability>, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(None);
    };
    let Some((receiver, property)) = static_member_name(&call.callee) else {
        return Ok(None);
    };
    if property != "use" || !state.app_vars.contains(receiver) || call.arguments.len() != 1 {
        return Ok(None);
    }
    let Some(Argument::CallExpression(provider_call)) = call.arguments.first() else {
        return Ok(None);
    };
    let Some(mut provider) = sqlite_provider_call_expression(provider_call, source, source_name)
    else {
        return Ok(None);
    };
    if let Some(options_argument) = provider_call.arguments.get(1) {
        let options = object_argument(options_argument).ok_or_else(|| {
            Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_PROVIDER_IMPORT",
                "sqlite provider import options must be an object literal",
            )
            .with_path(path)
            .with_span(argument_span(options_argument).unwrap_or(provider_call.span))
        })?;
        provider.database =
            optional_object_string_property(path, options, "database")?.map(ToOwned::to_owned);
        if provider.database.is_some() {
            provider.config_source = Some("inline".to_string());
        }
    }
    Ok(Some(provider))
}

pub(super) fn app_use_problem_details_call(
    path: &Path,
    expression: &Expression<'_>,
    state: &AppState,
) -> Result<Option<ProblemDetailsDescriptor>, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(None);
    };
    let Some((receiver, property)) = static_member_name(&call.callee) else {
        return Ok(None);
    };
    if property != "use" || !state.app_vars.contains(receiver) {
        return Ok(None);
    }
    let Some(Argument::CallExpression(descriptor_call)) = call.arguments.first() else {
        return Ok(None);
    };
    if call.arguments.len() != 1 {
        return Ok(None);
    }
    let Some((descriptor, method)) = static_member_name(&descriptor_call.callee) else {
        return Ok(None);
    };
    if descriptor != "ProblemDetails" || method != "defaults" {
        return Ok(None);
    }
    if !state.problem_details_imported {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_IMPORT",
            "ProblemDetails.defaults() requires importing ProblemDetails from \"sloppy\"",
        )
        .with_path(path)
        .with_span(descriptor_call.span));
    }
    if descriptor_call.arguments.len() > 1 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_PROBLEM_DETAILS",
            "ProblemDetails.defaults accepts at most one options object",
        )
        .with_path(path)
        .with_span(descriptor_call.span));
    }
    let mut detail = "never".to_string();
    if let Some(argument) = descriptor_call.arguments.first() {
        let Some(object) = object_argument(argument) else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_PROBLEM_DETAILS",
                "ProblemDetails.defaults options must be an object literal",
            )
            .with_path(path)
            .with_span(argument_span(argument).unwrap_or(descriptor_call.span)));
        };
        if let Some(value) = object_string_property_value(object, "detail") {
            if value != "never" && value != "development" && value != "always" {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_PROBLEM_DETAILS",
                    "ProblemDetails detail policy must be \"never\", \"development\", or \"always\"",
                )
                .with_path(path)
                .with_span(object.span));
            }
            detail = value.to_string();
        }
    }
    Ok(Some(ProblemDetailsDescriptor { detail }))
}

pub(super) fn app_use_module_call(
    expression: &Expression<'_>,
    state: &AppState,
) -> Option<(String, Span)> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    let (receiver, property) = static_member_name(&call.callee)?;
    if property != "useModule" || !state.app_vars.contains(receiver) || call.arguments.len() != 1 {
        return None;
    }
    let Argument::Identifier(identifier) = call.arguments.first()? else {
        return None;
    };
    Some((identifier.name.as_str().to_string(), identifier.span))
}

pub(super) fn orm_table_declaration_source(
    path: &Path,
    source: &str,
    name: &str,
    expression: &Expression<'_>,
    state: &mut AppState,
) -> Result<Option<String>, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(None);
    };
    let Expression::Identifier(callee) = &call.callee else {
        return Ok(None);
    };
    if callee.name.as_str() != "table" || !state.orm_imported {
        return Ok(None);
    }
    if call.arguments.len() < 2 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ORM_TABLE",
            "ORM table declarations require a literal table name and static column object",
        )
        .with_path(path)
        .with_span(call.span)
        .with_hint(orm_table_hint()));
    }
    let columns_argument = &call.arguments[1];
    if let Argument::ObjectExpression(columns) = columns_argument {
        if columns.properties.is_empty() {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_ORM_TABLE",
                "ORM table declarations require a non-empty column object",
            )
            .with_path(path)
            .with_span(argument_span(columns_argument).unwrap_or(call.span))
            .with_hint(orm_table_hint()));
        }
    }
    if let Some(metadata) =
        orm_table_metadata_from_call(source_name_from_path(path), source, name, call)
    {
        state.orm_tables.push(metadata);
    } else {
        state.orm_extraction_partial = true;
    }
    let Some(init_source) = source_slice(source, expression.span()) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ORM_TABLE",
            "ORM table declaration source could not be extracted",
        )
        .with_path(path)
        .with_span(call.span)
        .with_hint(orm_table_hint()));
    };
    Ok(Some(format!("const {name} = {init_source};")))
}

pub(super) fn source_name_from_path(path: &Path) -> String {
    path.file_name()
        .and_then(|name| name.to_str())
        .unwrap_or("app")
        .to_string()
}

pub(super) fn orm_table_metadata_from_call(
    source_name: String,
    source: &str,
    model: &str,
    call: &CallExpression<'_>,
) -> Option<Value> {
    let table_name = string_argument(call.arguments.first()?)?;
    let columns_object = object_argument(call.arguments.get(1)?)?;
    let mut columns = Vec::new();
    for property in &columns_object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return None;
        };
        if property.computed || property.method || property.shorthand {
            return None;
        }
        let column_name = property_key_name(&property.key)?.to_string();
        let column_expression = source_slice(source, property.value.span())?;
        let column_type = orm_column_type_from_expression(&property.value)?;
        columns.push(json!({
            "name": column_name,
            "type": column_type,
            "primaryKey": orm_column_has_modifier(&property.value, "primaryKey"),
            "nullable": orm_column_has_modifier(&property.value, "nullable"),
            "notNull": orm_column_has_modifier(&property.value, "notNull"),
            "unique": orm_column_has_modifier(&property.value, "unique"),
            "index": orm_column_has_modifier(&property.value, "index"),
            "private": orm_column_has_modifier(&property.value, "private"),
            "softDelete": orm_column_has_modifier(&property.value, "softDelete"),
            "concurrencyToken": orm_column_has_modifier(&property.value, "concurrencyToken"),
            "default": orm_column_has_modifier(&property.value, "default"),
            "defaultNow": orm_column_has_modifier(&property.value, "defaultNow"),
            "generated": orm_column_has_modifier(&property.value, "generated"),
            "reference": crate::plan_emit::parse_reference(&column_expression),
        }));
    }
    Some(json!({
        "model": model,
        "name": table_name,
        "source": source_name,
        "columns": columns,
    }))
}

pub(super) fn orm_column_type_from_expression<'a>(
    expression: &'a Expression<'a>,
) -> Option<&'a str> {
    let mut current = expression;
    loop {
        let Expression::CallExpression(call) = current else {
            return None;
        };
        let Expression::StaticMemberExpression(member) = &call.callee else {
            return None;
        };
        if let Expression::Identifier(object) = &member.object {
            if object.name.as_str() == "column" {
                let column_type = member.property.name.as_str();
                return if orm_column_type_supported(column_type) {
                    Some(column_type)
                } else {
                    None
                };
            }
        }
        current = &member.object;
    }
}

pub(super) fn orm_column_type_supported(column_type: &str) -> bool {
    matches!(
        column_type,
        "text"
            | "string"
            | "int"
            | "integer"
            | "bigint"
            | "number"
            | "float"
            | "decimal"
            | "bool"
            | "boolean"
            | "uuid"
            | "instant"
            | "timestamp"
            | "date"
            | "json"
            | "blob"
            | "bytes"
            | "enum"
    )
}

pub(super) fn orm_column_has_modifier(expression: &Expression<'_>, modifier: &str) -> bool {
    let mut current = expression;
    while let Expression::CallExpression(call) = current {
        let Expression::StaticMemberExpression(member) = &call.callee else {
            return false;
        };
        if member.property.name.as_str() == modifier {
            return true;
        }
        current = &member.object;
    }
    false
}

pub(super) fn orm_table_hint() -> &'static str {
    "Use:\n  const Users = table(\"users\", {\n    id: column.uuid().primaryKey(),\n    teamId: column.uuid().references(() => Teams.id),\n  });"
}

pub(super) fn orm_relation_hint() -> &'static str {
    "Use:\n  relation(Users, ({ one, many }) => ({\n    team: one(Teams, {\n      local: Users.teamId,\n      foreign: Teams.id,\n    }),\n  }));"
}

pub(super) fn orm_relation_metadata_call(
    path: &Path,
    source: &str,
    statement: &ExpressionStatement<'_>,
    state: &mut AppState,
) -> Result<bool, Diagnostic> {
    let expression = &statement.expression;
    let Expression::CallExpression(call) = expression else {
        return Ok(false);
    };
    let Expression::Identifier(callee) = &call.callee else {
        return Ok(false);
    };
    if callee.name.as_str() != "relation" {
        return Ok(false);
    }
    if !state.orm_imported {
        return Ok(false);
    }
    if call.arguments.len() < 2 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ORM_RELATION",
            "ORM relation declarations require a table identifier and an inline callback",
        )
        .with_path(path)
        .with_span(call.span)
        .with_hint(orm_relation_hint()));
    }
    let Some(table_argument) = call.arguments.first() else {
        return Ok(false);
    };
    if !matches!(table_argument, Argument::Identifier(_)) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ORM_RELATION",
            "ORM relation declarations require a static table identifier",
        )
        .with_path(path)
        .with_span(argument_span(table_argument).unwrap_or(call.span))
        .with_hint(orm_relation_hint()));
    }
    let Some(callback_argument) = call.arguments.get(1) else {
        return Ok(false);
    };
    let Some(statement_source) = source_slice(source, statement.span) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_ORM_RELATION",
            "ORM relation declaration source could not be extracted",
        )
        .with_path(path)
        .with_span(statement.span)
        .with_hint(orm_relation_hint()));
    };
    state
        .orm_metadata_sources
        .push((statement.span.start, statement_source.to_string()));
    if matches!(
        callback_argument,
        Argument::ArrowFunctionExpression(_) | Argument::FunctionExpression(_)
    ) {
        if let Some(callback_source) = source_slice(
            source,
            argument_span(callback_argument).unwrap_or(call.span),
        ) {
            if let Some(object_source) = crate::plan_emit::relation_object_source(&callback_source)
            {
                let Argument::Identifier(table_identifier) = table_argument else {
                    unreachable!("table argument shape checked above");
                };
                for part in crate::plan_emit::split_top_level_properties(object_source) {
                    let Some((relation_name, expression)) =
                        crate::plan_emit::parse_property_name(part)
                    else {
                        state.orm_extraction_partial = true;
                        continue;
                    };
                    let Some(mut relation) =
                        crate::plan_emit::parse_relation_definition(relation_name, expression)
                    else {
                        state.orm_extraction_partial = true;
                        continue;
                    };
                    if let Value::Object(ref mut object) = relation {
                        object.insert(
                            "tableModel".to_string(),
                            json!(table_identifier.name.as_str()),
                        );
                        object.insert("source".to_string(), json!(source_name_from_path(path)));
                    }
                    state.orm_relations.push(relation);
                }
            } else {
                state.orm_extraction_partial = true;
            }
        } else {
            state.orm_extraction_partial = true;
        }
    } else {
        state.orm_extraction_partial = true;
    }
    Ok(true)
}

pub(super) fn app_service_registration_call(
    path: &Path,
    source: &str,
    _source_name: &str,
    expression: &Expression<'_>,
    state: &AppState,
) -> Result<Option<ServiceRegistration>, Diagnostic> {
    let Expression::CallExpression(call) = expression else {
        return Ok(None);
    };
    let Some(chain) = static_member_chain(&call.callee) else {
        return Ok(None);
    };
    if chain.len() != 3
        || chain[1] != "services"
        || !(state.app_vars.contains(chain[0]) || state.builder_vars.contains(chain[0]))
    {
        return Ok(None);
    }
    let lifetime = match chain[2] {
        "addSingleton" => "singleton",
        "addScoped" => "scoped",
        "addTransient" => "transient",
        _ => return Ok(None),
    };
    if call.arguments.len() != 2 {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_SERVICE_REGISTRATION",
            "service registrations require a string token and one factory",
        )
        .with_path(path)
        .with_span(call.span));
    }
    let Some(token_argument) = call.arguments.first() else {
        return Ok(None);
    };
    let Some(factory_argument) = call.arguments.get(1) else {
        return Ok(None);
    };
    let token = string_argument(token_argument).ok_or_else(|| {
        Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_SERVICE_REGISTRATION",
            "service registration token must be a string literal",
        )
        .with_path(path)
        .with_span(argument_span(token_argument).unwrap_or(call.span))
    })?;
    if !matches!(
        factory_argument,
        Argument::ArrowFunctionExpression(_) | Argument::FunctionExpression(_)
    ) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_SERVICE_REGISTRATION",
            "service registration factory must be an inline function",
        )
        .with_path(path)
        .with_span(argument_span(factory_argument).unwrap_or(call.span)));
    }
    let Some(factory_source) =
        argument_span(factory_argument).and_then(|span| source_slice(source, span))
    else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_SERVICE_REGISTRATION",
            "service registration factory source could not be extracted",
        )
        .with_path(path)
        .with_span(call.span));
    };
    let free_identifiers = service_factory_free_identifiers(factory_argument);
    let unsupported_capture = free_identifiers
        .iter()
        .find(|identifier| !service_factory_capture_is_emit_safe(identifier, state));
    if let Some(identifier) = unsupported_capture {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_SERVICE_REGISTRATION",
            format!(
                "service registration '{}' factory captures unsupported identifier '{}'",
                token, identifier
            ),
        )
        .with_path(path)
        .with_span(argument_span(factory_argument).unwrap_or(call.span))
        .with_hint("Use an inline factory that depends only on its scope parameter, emitted top-level helper functions, JavaScript globals, or local values."));
    }
    Ok(Some(ServiceRegistration {
        token: token.to_string(),
        lifetime,
        factory_source,
    }))
}

pub(super) fn service_factory_capture_is_emit_safe(identifier: &str, state: &AppState) -> bool {
    if state.helper_sources.contains_key(identifier)
        && helper_source_is_safe_for_top_level(state.helper_effects.get(identifier))
    {
        return true;
    }
    if matches!(
        identifier,
        "Array"
            | "Boolean"
            | "Date"
            | "Error"
            | "Intl"
            | "JSON"
            | "Map"
            | "Math"
            | "Number"
            | "Object"
            | "Promise"
            | "RegExp"
            | "Set"
            | "String"
            | "TypeError"
            | "URL"
            | "URLSearchParams"
            | "console"
            | "globalThis"
            | "undefined"
    ) {
        return true;
    }
    matches!(
        identifier,
        "data" if state.data_imported
    ) || matches!(
        identifier,
        "sql" if state.sql_imported
    )
}

pub(super) fn service_factory_free_identifiers(argument: &Argument<'_>) -> BTreeSet<String> {
    let mut scope = BTreeSet::new();
    match argument {
        Argument::ArrowFunctionExpression(function) => {
            collect_formal_parameter_bindings(&function.params, &mut scope);
            collect_function_body_bindings(&function.body.statements, &mut scope);
            let mut free = BTreeSet::new();
            collect_statement_list_identifier_references(
                &function.body.statements,
                &scope,
                &mut free,
            );
            free
        }
        Argument::FunctionExpression(function) => {
            collect_formal_parameter_bindings(&function.params, &mut scope);
            if let Some(body) = &function.body {
                collect_function_body_bindings(&body.statements, &mut scope);
                let mut free = BTreeSet::new();
                collect_statement_list_identifier_references(&body.statements, &scope, &mut free);
                free
            } else {
                BTreeSet::new()
            }
        }
        _ => BTreeSet::new(),
    }
}

pub(super) fn collect_formal_parameter_bindings(
    parameters: &oxc_ast::ast::FormalParameters<'_>,
    scope: &mut BTreeSet<String>,
) {
    for parameter in &parameters.items {
        collect_binding_roots(&parameter.pattern, scope);
    }
}

pub(super) fn collect_function_body_bindings(
    statements: &[Statement<'_>],
    scope: &mut BTreeSet<String>,
) {
    for statement in statements {
        match statement {
            Statement::VariableDeclaration(declaration) => {
                for declarator in &declaration.declarations {
                    collect_binding_roots(&declarator.id, scope);
                }
            }
            Statement::FunctionDeclaration(function) => {
                if let Some(identifier) = &function.id {
                    scope.insert(identifier.name.as_str().to_string());
                }
            }
            _ => {}
        }
    }
}

pub(super) fn collect_statement_list_identifier_references(
    statements: &[Statement<'_>],
    scope: &BTreeSet<String>,
    free: &mut BTreeSet<String>,
) {
    for statement in statements {
        collect_statement_identifier_references(statement, scope, free);
    }
}

pub(super) fn collect_statement_identifier_references(
    statement: &Statement<'_>,
    scope: &BTreeSet<String>,
    free: &mut BTreeSet<String>,
) {
    match statement {
        Statement::BlockStatement(block) => {
            collect_statement_list_identifier_references(&block.body, scope, free);
        }
        Statement::ExpressionStatement(statement) => {
            collect_expression_identifier_references(&statement.expression, scope, free);
        }
        Statement::ReturnStatement(statement) => {
            if let Some(argument) = &statement.argument {
                collect_expression_identifier_references(argument, scope, free);
            }
        }
        Statement::VariableDeclaration(declaration) => {
            for declarator in &declaration.declarations {
                if let Some(init) = &declarator.init {
                    collect_expression_identifier_references(init, scope, free);
                }
            }
        }
        Statement::IfStatement(statement) => {
            collect_expression_identifier_references(&statement.test, scope, free);
            collect_statement_identifier_references(&statement.consequent, scope, free);
            if let Some(alternate) = &statement.alternate {
                collect_statement_identifier_references(alternate, scope, free);
            }
        }
        Statement::ThrowStatement(statement) => {
            collect_expression_identifier_references(&statement.argument, scope, free);
        }
        _ => {}
    }
}

pub(super) fn collect_expression_identifier_references(
    expression: &Expression<'_>,
    scope: &BTreeSet<String>,
    free: &mut BTreeSet<String>,
) {
    match expression {
        Expression::Identifier(identifier) => {
            let name = identifier.name.as_str();
            if !scope.contains(name) && !service_factory_allowed_global(name) {
                free.insert(name.to_string());
            }
        }
        Expression::CallExpression(call) => {
            collect_expression_identifier_references(&call.callee, scope, free);
            for argument in &call.arguments {
                collect_argument_identifier_references(argument, scope, free);
            }
        }
        Expression::NewExpression(expression) => {
            collect_expression_identifier_references(&expression.callee, scope, free);
            for argument in &expression.arguments {
                collect_argument_identifier_references(argument, scope, free);
            }
        }
        Expression::AwaitExpression(expression) => {
            collect_expression_identifier_references(&expression.argument, scope, free);
        }
        Expression::ObjectExpression(object) => {
            for property in &object.properties {
                match property {
                    ObjectPropertyKind::ObjectProperty(property) => {
                        collect_expression_identifier_references(&property.value, scope, free);
                    }
                    ObjectPropertyKind::SpreadProperty(property) => {
                        collect_expression_identifier_references(&property.argument, scope, free);
                    }
                }
            }
        }
        Expression::ArrayExpression(array) => {
            for element in &array.elements {
                collect_array_element_identifier_references(element, scope, free);
            }
        }
        Expression::ParenthesizedExpression(expression) => {
            collect_expression_identifier_references(&expression.expression, scope, free);
        }
        Expression::StaticMemberExpression(expression) => {
            collect_expression_identifier_references(&expression.object, scope, free);
        }
        Expression::ComputedMemberExpression(expression) => {
            collect_expression_identifier_references(&expression.object, scope, free);
            collect_expression_identifier_references(&expression.expression, scope, free);
        }
        Expression::BinaryExpression(expression) => {
            collect_expression_identifier_references(&expression.left, scope, free);
            collect_expression_identifier_references(&expression.right, scope, free);
        }
        Expression::LogicalExpression(expression) => {
            collect_expression_identifier_references(&expression.left, scope, free);
            collect_expression_identifier_references(&expression.right, scope, free);
        }
        Expression::ConditionalExpression(expression) => {
            collect_expression_identifier_references(&expression.test, scope, free);
            collect_expression_identifier_references(&expression.consequent, scope, free);
            collect_expression_identifier_references(&expression.alternate, scope, free);
        }
        Expression::ArrowFunctionExpression(function) => {
            let mut nested_scope = scope.clone();
            collect_formal_parameter_bindings(&function.params, &mut nested_scope);
            collect_function_body_bindings(&function.body.statements, &mut nested_scope);
            collect_statement_list_identifier_references(
                &function.body.statements,
                &nested_scope,
                free,
            );
        }
        Expression::FunctionExpression(function) => {
            let mut nested_scope = scope.clone();
            collect_formal_parameter_bindings(&function.params, &mut nested_scope);
            if let Some(body) = &function.body {
                collect_function_body_bindings(&body.statements, &mut nested_scope);
                collect_statement_list_identifier_references(&body.statements, &nested_scope, free);
            }
        }
        _ => {}
    }
}

pub(super) fn collect_argument_identifier_references(
    argument: &Argument<'_>,
    scope: &BTreeSet<String>,
    free: &mut BTreeSet<String>,
) {
    match argument {
        Argument::Identifier(identifier) => {
            let name = identifier.name.as_str();
            if !scope.contains(name) && !service_factory_allowed_global(name) {
                free.insert(name.to_string());
            }
        }
        Argument::ArrowFunctionExpression(function) => {
            let mut nested_scope = scope.clone();
            collect_formal_parameter_bindings(&function.params, &mut nested_scope);
            collect_function_body_bindings(&function.body.statements, &mut nested_scope);
            collect_statement_list_identifier_references(
                &function.body.statements,
                &nested_scope,
                free,
            );
        }
        Argument::FunctionExpression(function) => {
            let mut nested_scope = scope.clone();
            collect_formal_parameter_bindings(&function.params, &mut nested_scope);
            if let Some(body) = &function.body {
                collect_function_body_bindings(&body.statements, &mut nested_scope);
                collect_statement_list_identifier_references(&body.statements, &nested_scope, free);
            }
        }
        Argument::ObjectExpression(object) => {
            for property in &object.properties {
                match property {
                    ObjectPropertyKind::ObjectProperty(property) => {
                        collect_expression_identifier_references(&property.value, scope, free);
                    }
                    ObjectPropertyKind::SpreadProperty(property) => {
                        collect_expression_identifier_references(&property.argument, scope, free);
                    }
                }
            }
        }
        Argument::ArrayExpression(array) => {
            for element in &array.elements {
                collect_array_element_identifier_references(element, scope, free);
            }
        }
        _ => {
            if let Some(expression) = argument.as_expression() {
                collect_expression_identifier_references(expression, scope, free);
            }
        }
    }
}

pub(super) fn collect_array_element_identifier_references(
    element: &ArrayExpressionElement<'_>,
    scope: &BTreeSet<String>,
    free: &mut BTreeSet<String>,
) {
    if let Some(expression) = element.as_expression() {
        collect_expression_identifier_references(expression, scope, free);
    }
}

pub(super) fn service_factory_allowed_global(name: &str) -> bool {
    matches!(
        name,
        "Array"
            | "ArrayBuffer"
            | "Boolean"
            | "Date"
            | "Error"
            | "JSON"
            | "Map"
            | "Math"
            | "Number"
            | "Object"
            | "Promise"
            | "Set"
            | "String"
            | "Symbol"
            | "TypeError"
            | "Uint8Array"
            | "WeakMap"
            | "WeakSet"
            | "undefined"
    )
}

pub(super) fn add_sqlite_provider_capability(state: &mut AppState, provider: DatabaseCapability) {
    if state
        .capabilities
        .iter()
        .any(|capability| !capability.from_provider_use && capability.token == provider.token)
    {
        state.app_provider_uses.insert(provider.token);
        return;
    }

    if state.app_provider_uses.insert(provider.token.clone()) {
        state.capabilities.push(provider);
        return;
    }

    if let Some(existing) = state
        .capabilities
        .iter_mut()
        .find(|capability| capability.from_provider_use && capability.token == provider.token)
    {
        *existing = provider;
    }
}

pub(super) fn add_manual_database_capability(state: &mut AppState, capability: DatabaseCapability) {
    state
        .capabilities
        .retain(|existing| !(existing.from_provider_use && existing.token == capability.token));
    state.capabilities.push(capability);
}

pub(super) fn normalize_sqlite_provider_token(name: &str) -> String {
    normalize_database_provider_token(name)
}

pub(super) fn normalize_database_provider_token(name: &str) -> String {
    if name.starts_with("data.") {
        name.to_string()
    } else {
        format!("data.{name}")
    }
}

pub(super) fn database_provider_binding_from_token(token: &str) -> Option<ProviderBinding> {
    let (provider, name) = token.split_once(':')?;
    if !database_provider_supported(provider) {
        return None;
    }
    Some(ProviderBinding {
        token: normalize_database_provider_token(name),
        capability_kind: "database".to_string(),
        provider: provider.to_string(),
    })
}

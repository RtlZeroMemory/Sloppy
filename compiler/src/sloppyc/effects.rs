use super::*;

pub(super) fn helper_effects_from_initializer(
    expression: &Expression<'_>,
    provider_bindings: &BTreeMap<String, ProviderBinding>,
    helper_effects: &BTreeMap<String, FunctionEffectSummary>,
    source: &str,
    source_name: &str,
) -> FunctionEffectSummary {
    match expression {
        Expression::ArrowFunctionExpression(function) => function_effects_from_arrow_impl(
            function,
            provider_bindings,
            helper_effects,
            source,
            source_name,
            true,
        ),
        Expression::FunctionExpression(function) => function_effects_from_function_impl(
            function,
            provider_bindings,
            helper_effects,
            source,
            source_name,
            true,
        ),
        _ => FunctionEffectSummary::default(),
    }
}

pub(super) fn function_effects_from_arrow(
    function: &oxc_ast::ast::ArrowFunctionExpression<'_>,
    provider_bindings: &BTreeMap<String, ProviderBinding>,
    helper_effects: &BTreeMap<String, FunctionEffectSummary>,
    source: &str,
    source_name: &str,
) -> FunctionEffectSummary {
    function_effects_from_arrow_impl(
        function,
        provider_bindings,
        helper_effects,
        source,
        source_name,
        false,
    )
}

pub(super) fn helper_effects_from_function(
    function: &oxc_ast::ast::Function<'_>,
    provider_bindings: &BTreeMap<String, ProviderBinding>,
    helper_effects: &BTreeMap<String, FunctionEffectSummary>,
    source: &str,
    source_name: &str,
) -> FunctionEffectSummary {
    function_effects_from_function_impl(
        function,
        provider_bindings,
        helper_effects,
        source,
        source_name,
        true,
    )
}

fn function_effects_from_arrow_impl(
    function: &oxc_ast::ast::ArrowFunctionExpression<'_>,
    provider_bindings: &BTreeMap<String, ProviderBinding>,
    helper_effects: &BTreeMap<String, FunctionEffectSummary>,
    source: &str,
    source_name: &str,
    _treat_parameters_as_provider_candidates: bool,
) -> FunctionEffectSummary {
    let parameters = function_param_names(&function.params);
    let mut summary = FunctionEffectSummary {
        provider_bindings: provider_bindings.clone(),
        parameters,
        source_name: source_name.to_string(),
        source_text: source.to_string(),
        ..FunctionEffectSummary::default()
    };
    for statement in &function.body.statements {
        collect_statement_effects(statement, helper_effects, &mut summary);
    }
    dedupe_effects(&mut summary.effects);
    summary
}

pub(super) fn function_effects_from_function(
    function: &oxc_ast::ast::Function<'_>,
    provider_bindings: &BTreeMap<String, ProviderBinding>,
    helper_effects: &BTreeMap<String, FunctionEffectSummary>,
    source: &str,
    source_name: &str,
) -> FunctionEffectSummary {
    function_effects_from_function_impl(
        function,
        provider_bindings,
        helper_effects,
        source,
        source_name,
        false,
    )
}

fn function_effects_from_function_impl(
    function: &oxc_ast::ast::Function<'_>,
    provider_bindings: &BTreeMap<String, ProviderBinding>,
    helper_effects: &BTreeMap<String, FunctionEffectSummary>,
    source: &str,
    source_name: &str,
    _treat_parameters_as_provider_candidates: bool,
) -> FunctionEffectSummary {
    let parameters = function_param_names(&function.params);
    let mut summary = FunctionEffectSummary {
        provider_bindings: provider_bindings.clone(),
        parameters,
        source_name: source_name.to_string(),
        source_text: source.to_string(),
        ..FunctionEffectSummary::default()
    };
    if let Some(body) = &function.body {
        for statement in &body.statements {
            collect_statement_effects(statement, helper_effects, &mut summary);
        }
    }
    dedupe_effects(&mut summary.effects);
    summary
}

fn function_param_names(params: &oxc_ast::ast::FormalParameters<'_>) -> Vec<String> {
    params
        .items
        .iter()
        .filter_map(|parameter| binding_identifier(&parameter.pattern).map(str::to_string))
        .collect()
}

fn parameter_provider_token(name: &str) -> String {
    format!("$param:{name}")
}

fn provider_token_parameter(token: &str) -> Option<&str> {
    token.strip_prefix("$param:")
}

pub(super) fn collect_statement_effects(
    statement: &Statement<'_>,
    helper_effects: &BTreeMap<String, FunctionEffectSummary>,
    summary: &mut FunctionEffectSummary,
) {
    match statement {
        Statement::VariableDeclaration(declaration) => {
            for declarator in &declaration.declarations {
                if let Some(name) = binding_identifier(&declarator.id) {
                    if let Some(init) = &declarator.init {
                        if let Some(binding) = data_provider_binding(init) {
                            summary.provider_bindings.insert(name.to_string(), binding);
                        } else {
                            collect_expression_effects(init, helper_effects, summary);
                        }
                    }
                }
            }
        }
        Statement::ReturnStatement(statement) => {
            if let Some(argument) = &statement.argument {
                collect_expression_effects(argument, helper_effects, summary);
            }
        }
        Statement::ExpressionStatement(statement) => {
            collect_expression_effects(&statement.expression, helper_effects, summary);
        }
        Statement::BlockStatement(block) => {
            for statement in &block.body {
                collect_statement_effects(statement, helper_effects, summary);
            }
        }
        Statement::IfStatement(statement) => {
            collect_expression_effects(&statement.test, helper_effects, summary);
            collect_statement_effects(&statement.consequent, helper_effects, summary);
            if let Some(alternate) = &statement.alternate {
                collect_statement_effects(alternate, helper_effects, summary);
            }
        }
        Statement::DoWhileStatement(statement) => {
            collect_statement_effects(&statement.body, helper_effects, summary);
            collect_expression_effects(&statement.test, helper_effects, summary);
        }
        Statement::WhileStatement(statement) => {
            collect_expression_effects(&statement.test, helper_effects, summary);
            collect_statement_effects(&statement.body, helper_effects, summary);
        }
        Statement::ForStatement(statement) => {
            if let Some(init) = &statement.init {
                collect_for_init_effects(init, helper_effects, summary);
            }
            if let Some(test) = &statement.test {
                collect_expression_effects(test, helper_effects, summary);
            }
            if let Some(update) = &statement.update {
                collect_expression_effects(update, helper_effects, summary);
            }
            collect_statement_effects(&statement.body, helper_effects, summary);
        }
        Statement::ForInStatement(statement) => {
            collect_expression_effects(&statement.right, helper_effects, summary);
            collect_statement_effects(&statement.body, helper_effects, summary);
        }
        Statement::ForOfStatement(statement) => {
            collect_expression_effects(&statement.right, helper_effects, summary);
            collect_statement_effects(&statement.body, helper_effects, summary);
        }
        Statement::SwitchStatement(statement) => {
            collect_expression_effects(&statement.discriminant, helper_effects, summary);
            for case in &statement.cases {
                if let Some(test) = &case.test {
                    collect_expression_effects(test, helper_effects, summary);
                }
                for statement in &case.consequent {
                    collect_statement_effects(statement, helper_effects, summary);
                }
            }
        }
        Statement::TryStatement(statement) => {
            for statement in &statement.block.body {
                collect_statement_effects(statement, helper_effects, summary);
            }
            if let Some(handler) = &statement.handler {
                for statement in &handler.body.body {
                    collect_statement_effects(statement, helper_effects, summary);
                }
            }
            if let Some(finalizer) = &statement.finalizer {
                for statement in &finalizer.body {
                    collect_statement_effects(statement, helper_effects, summary);
                }
            }
        }
        Statement::ThrowStatement(statement) => {
            collect_expression_effects(&statement.argument, helper_effects, summary);
        }
        _ => {}
    }
}

pub(super) fn collect_for_init_effects(
    init: &ForStatementInit<'_>,
    helper_effects: &BTreeMap<String, FunctionEffectSummary>,
    summary: &mut FunctionEffectSummary,
) {
    match init {
        ForStatementInit::VariableDeclaration(declaration) => {
            for declarator in &declaration.declarations {
                if let Some(init) = &declarator.init {
                    collect_expression_effects(init, helper_effects, summary);
                }
            }
        }
        ForStatementInit::CallExpression(call) => {
            collect_call_effects(call, helper_effects, summary);
            for argument in &call.arguments {
                collect_argument_effects(argument, helper_effects, summary);
            }
        }
        ForStatementInit::ParenthesizedExpression(parenthesized) => {
            collect_expression_effects(&parenthesized.expression, helper_effects, summary);
        }
        _ => {}
    }
}

pub(super) fn collect_expression_effects(
    expression: &Expression<'_>,
    helper_effects: &BTreeMap<String, FunctionEffectSummary>,
    summary: &mut FunctionEffectSummary,
) {
    match expression {
        Expression::CallExpression(call) => {
            collect_call_effects(call, helper_effects, summary);
            for argument in &call.arguments {
                collect_argument_effects(argument, helper_effects, summary);
            }
        }
        Expression::ConditionalExpression(conditional) => {
            collect_expression_effects(&conditional.test, helper_effects, summary);
            collect_expression_effects(&conditional.consequent, helper_effects, summary);
            collect_expression_effects(&conditional.alternate, helper_effects, summary);
        }
        Expression::LogicalExpression(logical) => {
            collect_expression_effects(&logical.left, helper_effects, summary);
            collect_expression_effects(&logical.right, helper_effects, summary);
        }
        Expression::SequenceExpression(sequence) => {
            for expression in &sequence.expressions {
                collect_expression_effects(expression, helper_effects, summary);
            }
        }
        Expression::ChainExpression(chain) => {
            collect_chain_effects(&chain.expression, helper_effects, summary);
        }
        Expression::ObjectExpression(object) => {
            for property in &object.properties {
                if let ObjectPropertyKind::ObjectProperty(property) = property {
                    collect_expression_effects(&property.value, helper_effects, summary);
                }
            }
        }
        Expression::ArrayExpression(array) => {
            for element in &array.elements {
                collect_array_element_effects(element, helper_effects, summary);
            }
        }
        Expression::ParenthesizedExpression(parenthesized) => {
            collect_expression_effects(&parenthesized.expression, helper_effects, summary);
        }
        Expression::AwaitExpression(await_expression) => {
            collect_expression_effects(&await_expression.argument, helper_effects, summary);
        }
        Expression::StaticMemberExpression(member) => {
            if let Expression::Identifier(identifier) = &member.object {
                if summary
                    .provider_bindings
                    .contains_key(identifier.name.as_str())
                {
                    summary.unknown_provider_usage = true;
                    return;
                }
            }
            collect_expression_effects(&member.object, helper_effects, summary);
        }
        Expression::Identifier(identifier)
            if summary
                .provider_bindings
                .contains_key(identifier.name.as_str()) =>
        {
            summary.unknown_provider_usage = true;
        }
        _ => {}
    }
}

pub(super) fn collect_chain_effects(
    expression: &ChainElement<'_>,
    helper_effects: &BTreeMap<String, FunctionEffectSummary>,
    summary: &mut FunctionEffectSummary,
) {
    match expression {
        ChainElement::CallExpression(call) => {
            collect_call_effects(call, helper_effects, summary);
            for argument in &call.arguments {
                collect_argument_effects(argument, helper_effects, summary);
            }
        }
        ChainElement::StaticMemberExpression(member) => {
            collect_expression_effects(&member.object, helper_effects, summary);
        }
        ChainElement::ComputedMemberExpression(member) => {
            collect_expression_effects(&member.object, helper_effects, summary);
            collect_expression_effects(&member.expression, helper_effects, summary);
        }
        ChainElement::TSNonNullExpression(expression) => {
            collect_expression_effects(&expression.expression, helper_effects, summary);
        }
        _ => {}
    }
}

pub(super) fn collect_argument_effects(
    argument: &Argument<'_>,
    helper_effects: &BTreeMap<String, FunctionEffectSummary>,
    summary: &mut FunctionEffectSummary,
) {
    match argument {
        Argument::CallExpression(call) => {
            collect_call_effects(call, helper_effects, summary);
            for argument in &call.arguments {
                collect_argument_effects(argument, helper_effects, summary);
            }
        }
        Argument::ConditionalExpression(conditional) => {
            collect_expression_effects(&conditional.test, helper_effects, summary);
            collect_expression_effects(&conditional.consequent, helper_effects, summary);
            collect_expression_effects(&conditional.alternate, helper_effects, summary);
        }
        Argument::LogicalExpression(logical) => {
            collect_expression_effects(&logical.left, helper_effects, summary);
            collect_expression_effects(&logical.right, helper_effects, summary);
        }
        Argument::SequenceExpression(sequence) => {
            for expression in &sequence.expressions {
                collect_expression_effects(expression, helper_effects, summary);
            }
        }
        Argument::ChainExpression(chain) => {
            collect_chain_effects(&chain.expression, helper_effects, summary);
        }
        Argument::ObjectExpression(object) => {
            for property in &object.properties {
                if let ObjectPropertyKind::ObjectProperty(property) = property {
                    collect_expression_effects(&property.value, helper_effects, summary);
                }
            }
        }
        Argument::ArrayExpression(array) => {
            for element in &array.elements {
                collect_array_element_effects(element, helper_effects, summary);
            }
        }
        Argument::ParenthesizedExpression(parenthesized) => {
            collect_expression_effects(&parenthesized.expression, helper_effects, summary);
        }
        Argument::AwaitExpression(await_expression) => {
            collect_expression_effects(&await_expression.argument, helper_effects, summary);
        }
        _ => {}
    }
}

pub(super) fn collect_array_element_effects(
    element: &ArrayExpressionElement<'_>,
    helper_effects: &BTreeMap<String, FunctionEffectSummary>,
    summary: &mut FunctionEffectSummary,
) {
    match element {
        ArrayExpressionElement::CallExpression(call) => {
            collect_call_effects(call, helper_effects, summary);
        }
        ArrayExpressionElement::ObjectExpression(object) => {
            for property in &object.properties {
                if let ObjectPropertyKind::ObjectProperty(property) = property {
                    collect_expression_effects(&property.value, helper_effects, summary);
                }
            }
        }
        ArrayExpressionElement::ArrayExpression(array) => {
            for element in &array.elements {
                collect_array_element_effects(element, helper_effects, summary);
            }
        }
        _ => {}
    }
}

pub(super) fn collect_call_effects(
    call: &CallExpression<'_>,
    helper_effects: &BTreeMap<String, FunctionEffectSummary>,
    summary: &mut FunctionEffectSummary,
) {
    if let Some((receiver, method)) = static_member_name(&call.callee) {
        if let Some(binding) = summary.provider_bindings.get(receiver) {
            if let Some(access) = provider_method_access(binding, method, call) {
                summary.effects.push(EffectMetadata {
                    provider: binding.token.clone(),
                    capability_kind: binding.capability_kind.clone(),
                    provider_kind: binding.provider.clone(),
                    access,
                    operation: method.to_string(),
                    reason: format!("{receiver}.{method}"),
                    source_name: summary.source_name.clone(),
                    source_text: summary.source_text.clone(),
                    span: call.span,
                });
            } else if method != "close" {
                summary.unknown_provider_usage = true;
            }
        } else if summary
            .parameters
            .iter()
            .any(|parameter| parameter == receiver)
        {
            let binding = ProviderBinding {
                token: parameter_provider_token(receiver),
                capability_kind: "database".to_string(),
                provider: "sqlite".to_string(),
            };
            if let Some(access) = provider_method_access(&binding, method, call) {
                summary.effects.push(EffectMetadata {
                    provider: binding.token,
                    capability_kind: binding.capability_kind,
                    provider_kind: binding.provider,
                    access,
                    operation: method.to_string(),
                    reason: format!("{receiver}.{method}"),
                    source_name: summary.source_name.clone(),
                    source_text: summary.source_text.clone(),
                    span: call.span,
                });
            }
        }
    }
    collect_callee_object_effects(&call.callee, helper_effects, summary);

    if let Expression::Identifier(identifier) = &call.callee {
        let helper_name = identifier.name.as_str();
        summary.helper_calls.insert(helper_name.to_string());
        if let Some(helper) = helper_effects.get(helper_name) {
            apply_helper_effects(call, helper, summary);
            summary.unknown_provider_usage |= helper.unknown_provider_usage;
            for (name, binding) in &helper.provider_bindings {
                if provider_token_parameter(&binding.token).is_some() {
                    continue;
                }
                summary
                    .provider_bindings
                    .entry(name.clone())
                    .or_insert_with(|| binding.clone());
            }
        }
    }
}

fn collect_callee_object_effects(
    callee: &Expression<'_>,
    helper_effects: &BTreeMap<String, FunctionEffectSummary>,
    summary: &mut FunctionEffectSummary,
) {
    match callee {
        Expression::StaticMemberExpression(member) => {
            if !matches!(member.object, Expression::Identifier(_)) {
                collect_expression_effects(&member.object, helper_effects, summary);
            }
        }
        Expression::ComputedMemberExpression(member) => {
            if !matches!(member.object, Expression::Identifier(_)) {
                collect_expression_effects(&member.object, helper_effects, summary);
            }
            collect_expression_effects(&member.expression, helper_effects, summary);
        }
        Expression::ChainExpression(chain) => {
            collect_chain_effects(&chain.expression, helper_effects, summary)
        }
        Expression::ParenthesizedExpression(parenthesized) => {
            collect_callee_object_effects(&parenthesized.expression, helper_effects, summary);
        }
        _ => {}
    }
}

fn apply_helper_effects(
    call: &CallExpression<'_>,
    helper: &FunctionEffectSummary,
    summary: &mut FunctionEffectSummary,
) {
    for effect in &helper.effects {
        let Some(parameter) = provider_token_parameter(&effect.provider) else {
            summary.effects.push(effect.clone());
            continue;
        };
        let Some(argument_binding) = helper
            .parameters
            .iter()
            .position(|name| name == parameter)
            .and_then(|index| call.arguments.get(index))
            .and_then(argument_identifier)
            .and_then(|name| argument_provider_binding(name, summary))
        else {
            summary.unknown_provider_usage = true;
            continue;
        };
        let mut substituted = effect.clone();
        substituted.provider = argument_binding.token.clone();
        substituted.capability_kind = argument_binding.capability_kind.clone();
        substituted.provider_kind = argument_binding.provider.clone();
        summary.effects.push(substituted);
    }
}

fn argument_provider_binding(
    name: &str,
    summary: &FunctionEffectSummary,
) -> Option<ProviderBinding> {
    if let Some(binding) = summary.provider_bindings.get(name) {
        return Some(binding.clone());
    }
    if summary.parameters.iter().any(|parameter| parameter == name) {
        return Some(ProviderBinding {
            token: parameter_provider_token(name),
            capability_kind: "database".to_string(),
            provider: "sqlite".to_string(),
        });
    }
    None
}

fn argument_identifier<'a>(argument: &'a Argument<'a>) -> Option<&'a str> {
    match argument {
        Argument::Identifier(identifier) => Some(identifier.name.as_str()),
        Argument::ParenthesizedExpression(parenthesized) => {
            if let Expression::Identifier(identifier) = &parenthesized.expression {
                Some(identifier.name.as_str())
            } else {
                None
            }
        }
        _ => None,
    }
}

pub(super) fn resolve_helper_effect_callgraph(
    helper_effects: &mut BTreeMap<String, FunctionEffectSummary>,
) {
    let mut changed = true;
    while changed {
        changed = false;
        let snapshot = helper_effects.clone();
        for summary in helper_effects.values_mut() {
            let calls = summary.helper_calls.iter().cloned().collect::<Vec<_>>();
            for helper_name in calls {
                let Some(callee) = snapshot.get(&helper_name) else {
                    continue;
                };
                let before_effects = summary.effects.len();
                summary
                    .effects
                    .extend(callee.effects.iter().filter_map(|effect| {
                        if provider_token_parameter(&effect.provider).is_some() {
                            None
                        } else {
                            Some(effect.clone())
                        }
                    }));
                dedupe_effects(&mut summary.effects);
                if summary.effects.len() != before_effects {
                    changed = true;
                }
                for (name, binding) in &callee.provider_bindings {
                    if provider_token_parameter(&binding.token).is_some() {
                        continue;
                    }
                    if summary
                        .provider_bindings
                        .insert(name.clone(), binding.clone())
                        .is_none()
                    {
                        changed = true;
                    }
                }
                if callee.unknown_provider_usage && !summary.unknown_provider_usage {
                    summary.unknown_provider_usage = true;
                    changed = true;
                }
            }
        }
    }
}

pub(super) fn data_provider_binding(expression: &Expression<'_>) -> Option<ProviderBinding> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    let (receiver, method) = static_member_name(&call.callee)?;
    if receiver != "data" || !database_provider_supported(method) || call.arguments.len() != 1 {
        return None;
    }
    let name = string_argument(call.arguments.first()?)?;
    Some(ProviderBinding {
        token: normalize_database_provider_token(name),
        capability_kind: "database".to_string(),
        provider: method.to_string(),
    })
}

pub(super) fn provider_method_access(
    binding: &ProviderBinding,
    method: &str,
    call: &CallExpression<'_>,
) -> Option<&'static str> {
    if binding.capability_kind != "database" {
        return None;
    }
    match method {
        "query" | "queryOne" => Some("read"),
        "exec" => Some(sql_access_from_call(call).unwrap_or("write")),
        "transaction" => Some("readwrite"),
        _ => None,
    }
}

pub(super) fn sql_access_from_call(call: &CallExpression<'_>) -> Option<&'static str> {
    let sql = call.arguments.first().and_then(string_argument)?;
    let trimmed = sql.trim_start().to_ascii_uppercase();
    if trimmed.starts_with("SELECT") {
        Some("read")
    } else {
        Some("write")
    }
}

pub(super) fn dedupe_effects(effects: &mut Vec<EffectMetadata>) {
    let mut seen = BTreeSet::new();
    effects.retain(|effect| {
        seen.insert(format!(
            "{}:{}:{}",
            effect.provider, effect.access, effect.operation
        ))
    });
}

pub(super) fn coalesce_manual_capability_overrides(capabilities: &mut Vec<DatabaseCapability>) {
    let manual_tokens = capabilities
        .iter()
        .filter(|capability| !capability.from_provider_use)
        .map(|capability| capability.token.clone())
        .collect::<BTreeSet<_>>();
    capabilities.retain(|capability| {
        !capability.from_provider_use || !manual_tokens.contains(&capability.token)
    });
}

pub(super) fn add_inferred_framework_capabilities(state: &mut AppState) {
    let mut seen = state
        .capabilities
        .iter()
        .map(|capability| capability.token.clone())
        .collect::<BTreeSet<_>>();

    for route in &state.routes {
        for binding in &route.handler.bindings {
            let Some(injection_kind) = binding.injection_kind.as_deref() else {
                continue;
            };
            let Some(token) = binding.capability.as_deref() else {
                continue;
            };
            if !seen.insert(token.to_string()) {
                continue;
            }

            let source_name = binding
                .source_name
                .clone()
                .unwrap_or_else(|| route.handler.source_name.clone());
            let source = binding
                .source_text
                .clone()
                .unwrap_or_else(|| route.handler.source_text.clone());
            let span = binding.span.unwrap_or(route.handler.span);

            if injection_kind == "provider" {
                let Some(provider) = binding.provider_kind.as_deref() else {
                    continue;
                };
                if !database_provider_supported(provider) {
                    continue;
                }
                state.capabilities.push(DatabaseCapability {
                    token: token.to_string(),
                    capability_kind: "database".to_string(),
                    provider: provider.to_string(),
                    config_name: binding.name.clone(),
                    config_key: None,
                    access: "readwrite".to_string(),
                    database: None,
                    config_source: None,
                    source_name,
                    source,
                    span,
                    from_provider_use: false,
                });
            } else if injection_kind == "queue" {
                state.capabilities.push(DatabaseCapability {
                    token: token.to_string(),
                    capability_kind: "queue".to_string(),
                    provider: String::new(),
                    config_name: binding.name.clone(),
                    config_key: None,
                    access: "enqueue".to_string(),
                    database: None,
                    config_source: None,
                    source_name,
                    source,
                    span,
                    from_provider_use: false,
                });
            }
        }
    }
}

pub(super) fn validate_provider_effect_registrations(
    _path: &Path,
    routes: &[Route],
    capabilities: &[DatabaseCapability],
) -> Result<(), Diagnostic> {
    for route in routes {
        for effect in &route.handler.effects {
            let registered = capabilities.iter().any(|capability| {
                capability.token == effect.provider
                    && capability.capability_kind == effect.capability_kind
                    && capability.provider == effect.provider_kind
            });
            if !registered {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_MISSING_PROVIDER",
                    format!(
                        "route uses unregistered {} provider '{}'",
                        effect.capability_kind, effect.provider
                    ),
                )
                .with_path(&route.source_path)
                .with_span(route.span)
                .with_hint(
                    "Register the provider with app.use(...), builder.capabilities metadata, or an explicit runtime-only escape hatch once that pattern is supported.",
                ));
            }
        }
    }
    Ok(())
}

pub(super) fn apply_inferred_capability_access(
    capabilities: &mut [DatabaseCapability],
    routes: &[Route],
) {
    let mut inferred = BTreeMap::<String, &'static str>::new();
    for route in routes {
        for effect in &route.handler.effects {
            inferred
                .entry(effect.provider.clone())
                .and_modify(|access| *access = merge_access(access, effect.access))
                .or_insert(effect.access);
        }
    }

    for capability in capabilities {
        if capability.from_provider_use {
            if let Some(access) = inferred.get(&capability.token) {
                capability.access = (*access).to_string();
            }
        }
    }
}

pub(super) fn merge_access(left: &'static str, right: &'static str) -> &'static str {
    match (left, right) {
        ("read", "read") => "read",
        ("write", "write") => "write",
        ("readwrite", _) | (_, "readwrite") => "readwrite",
        _ => "readwrite",
    }
}

// Handler extraction, request bindings, response metadata, and result support checks.
use super::*;

pub(super) fn handler_from_argument(
    argument: &Argument<'_>,
    context: &HandlerExtractionContext<'_>,
) -> Option<Handler> {
    match argument {
        Argument::ArrowFunctionExpression(function) => {
            if typed_framework_handler_parameters(&function.params) {
                return typed_framework_handler_from_arrow(
                    function,
                    context.route_pattern,
                    context.source,
                    context.source_name,
                    context.schema_names,
                );
            }
            let effects = function_effects_from_arrow(
                function,
                context.provider_bindings,
                context.helper_effects,
                context.source,
                context.source_name,
            );
            let realtime_route = context.route_kind == "sse" || context.route_kind == "websocket";
            if handler_parameters_are_unsupported_for_kind(&function.params, context.route_kind)
                || arrow_has_typescript_syntax(function)
                || effects.unknown_provider_usage
                || (!realtime_route
                    && !context.allow_data_handler_body
                    && effects.effects.is_empty()
                    && effects.helper_calls.is_empty()
                    && !handler_body_is_supported_arrow(function, context.schema_names))
            {
                return None;
            }
            let schema_spans = handler_context_parameter_name(&function.params)
                .map(|ctx_name| {
                    body_json_schema_argument_spans_arrow(function, &ctx_name, context.schema_names)
                })
                .unwrap_or_default();
            let handler_source = source_slice(context.source, function.span)?;
            let handler_source = sanitize_handler_schema_references(
                handler_source,
                function.span.start,
                &schema_spans,
            );
            let responses = response_metadata_many_from_arrow(
                function,
                context.source_name,
                context.source,
                context.schema_names,
            );
            Some(Handler {
                source: handler_source.clone(),
                emitted_source: handler_source,
                span: function.span,
                requires_results_import: arrow_requires_results_import(function),
                is_async: function.r#async,
                runtime_deferred: false,
                source_name: context.source_name.to_string(),
                source_text: context.source.to_string(),
                source_map_line_offset: 0,
                source_map_column_offset: 0,
                bindings: request_bindings_from_arrow(function, context.schema_names),
                response: responses.first().cloned(),
                responses,
                effects: effects.effects,
                schema_metadata_conflict: false,
            })
        }
        Argument::FunctionExpression(function) => {
            if typed_framework_handler_parameters(&function.params) {
                return typed_framework_handler_from_function(
                    function,
                    context.route_pattern,
                    context.source,
                    context.source_name,
                    context.schema_names,
                );
            }
            let effects = function_effects_from_function(
                function,
                context.provider_bindings,
                context.helper_effects,
                context.source,
                context.source_name,
            );
            let realtime_route = context.route_kind == "sse" || context.route_kind == "websocket";
            if handler_parameters_are_unsupported_for_kind(&function.params, context.route_kind)
                || function_has_typescript_syntax(function)
                || effects.unknown_provider_usage
                || (!realtime_route
                    && !context.allow_data_handler_body
                    && effects.effects.is_empty()
                    && effects.helper_calls.is_empty()
                    && !handler_body_is_supported_function(function, context.schema_names))
            {
                return None;
            }
            let schema_spans = handler_context_parameter_name(&function.params)
                .map(|ctx_name| {
                    body_json_schema_argument_spans_function(
                        function,
                        &ctx_name,
                        context.schema_names,
                    )
                })
                .unwrap_or_default();
            let handler_source = source_slice(context.source, function.span)?;
            let handler_source = sanitize_handler_schema_references(
                handler_source,
                function.span.start,
                &schema_spans,
            );
            let responses = response_metadata_many_from_function(
                function,
                context.source_name,
                context.source,
                context.schema_names,
            );
            Some(Handler {
                source: handler_source.clone(),
                emitted_source: handler_source,
                span: function.span,
                requires_results_import: function_requires_results_import(function),
                is_async: function.r#async,
                runtime_deferred: false,
                source_name: context.source_name.to_string(),
                source_text: context.source.to_string(),
                source_map_line_offset: 0,
                source_map_column_offset: 0,
                bindings: request_bindings_from_function(function, context.schema_names),
                response: responses.first().cloned(),
                responses,
                effects: effects.effects,
                schema_metadata_conflict: false,
            })
        }
        _ => None,
    }
}

pub(super) fn validate_handler_body_validate_schema_references(
    path: &Path,
    argument: &Argument<'_>,
    schema_names: &BTreeSet<String>,
) -> Result<(), Diagnostic> {
    let unresolved = match argument {
        Argument::ArrowFunctionExpression(function) => {
            handler_context_parameter_name(&function.params).and_then(|ctx_name| {
                function.body.statements.iter().find_map(|statement| {
                    unresolved_body_validate_schema_in_statement(statement, &ctx_name, schema_names)
                })
            })
        }
        Argument::FunctionExpression(function) => handler_context_parameter_name(&function.params)
            .and_then(|ctx_name| {
                function.body.as_ref().and_then(|body| {
                    body.statements.iter().find_map(|statement| {
                        unresolved_body_validate_schema_in_statement(
                            statement,
                            &ctx_name,
                            schema_names,
                        )
                    })
                })
            }),
        _ => None,
    };
    if let Some((schema, span)) = unresolved {
        return Err(unresolved_schema_diagnostic(path, span, &schema));
    }
    Ok(())
}

pub(super) fn unresolved_body_validate_schema_in_statement(
    statement: &Statement<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
) -> Option<(String, Span)> {
    match statement {
        Statement::BlockStatement(block) => block.body.iter().find_map(|statement| {
            unresolved_body_validate_schema_in_statement(statement, ctx_name, schema_names)
        }),
        Statement::ReturnStatement(statement) => statement.argument.as_ref().and_then(|argument| {
            unresolved_body_validate_schema_in_expression(argument, ctx_name, schema_names)
        }),
        Statement::ExpressionStatement(statement) => unresolved_body_validate_schema_in_expression(
            &statement.expression,
            ctx_name,
            schema_names,
        ),
        Statement::VariableDeclaration(declaration) => {
            declaration.declarations.iter().find_map(|declarator| {
                declarator.init.as_ref().and_then(|init| {
                    unresolved_body_validate_schema_in_expression(init, ctx_name, schema_names)
                })
            })
        }
        Statement::IfStatement(statement) => {
            unresolved_body_validate_schema_in_expression(&statement.test, ctx_name, schema_names)
                .or_else(|| {
                    unresolved_body_validate_schema_in_statement(
                        &statement.consequent,
                        ctx_name,
                        schema_names,
                    )
                })
                .or_else(|| {
                    statement.alternate.as_ref().and_then(|alternate| {
                        unresolved_body_validate_schema_in_statement(
                            alternate,
                            ctx_name,
                            schema_names,
                        )
                    })
                })
        }
        Statement::ThrowStatement(statement) => unresolved_body_validate_schema_in_expression(
            &statement.argument,
            ctx_name,
            schema_names,
        ),
        Statement::DoWhileStatement(statement) => {
            unresolved_body_validate_schema_in_statement(&statement.body, ctx_name, schema_names)
                .or_else(|| {
                    unresolved_body_validate_schema_in_expression(
                        &statement.test,
                        ctx_name,
                        schema_names,
                    )
                })
        }
        Statement::WhileStatement(statement) => {
            unresolved_body_validate_schema_in_expression(&statement.test, ctx_name, schema_names)
                .or_else(|| {
                    unresolved_body_validate_schema_in_statement(
                        &statement.body,
                        ctx_name,
                        schema_names,
                    )
                })
        }
        Statement::ForStatement(statement) => statement
            .init
            .as_ref()
            .and_then(|init| {
                unresolved_body_validate_schema_in_for_init(init, ctx_name, schema_names)
            })
            .or_else(|| {
                statement.test.as_ref().and_then(|test| {
                    unresolved_body_validate_schema_in_expression(test, ctx_name, schema_names)
                })
            })
            .or_else(|| {
                statement.update.as_ref().and_then(|update| {
                    unresolved_body_validate_schema_in_expression(update, ctx_name, schema_names)
                })
            })
            .or_else(|| {
                unresolved_body_validate_schema_in_statement(
                    &statement.body,
                    ctx_name,
                    schema_names,
                )
            }),
        Statement::ForInStatement(statement) => {
            unresolved_body_validate_schema_in_expression(&statement.right, ctx_name, schema_names)
                .or_else(|| {
                    unresolved_body_validate_schema_in_statement(
                        &statement.body,
                        ctx_name,
                        schema_names,
                    )
                })
        }
        Statement::ForOfStatement(statement) => {
            unresolved_body_validate_schema_in_expression(&statement.right, ctx_name, schema_names)
                .or_else(|| {
                    unresolved_body_validate_schema_in_statement(
                        &statement.body,
                        ctx_name,
                        schema_names,
                    )
                })
        }
        Statement::SwitchStatement(statement) => unresolved_body_validate_schema_in_expression(
            &statement.discriminant,
            ctx_name,
            schema_names,
        )
        .or_else(|| {
            statement.cases.iter().find_map(|case| {
                case.test
                    .as_ref()
                    .and_then(|test| {
                        unresolved_body_validate_schema_in_expression(test, ctx_name, schema_names)
                    })
                    .or_else(|| {
                        case.consequent.iter().find_map(|statement| {
                            unresolved_body_validate_schema_in_statement(
                                statement,
                                ctx_name,
                                schema_names,
                            )
                        })
                    })
            })
        }),
        Statement::TryStatement(statement) => statement
            .block
            .body
            .iter()
            .find_map(|statement| {
                unresolved_body_validate_schema_in_statement(statement, ctx_name, schema_names)
            })
            .or_else(|| {
                statement.handler.as_ref().and_then(|handler| {
                    handler.body.body.iter().find_map(|statement| {
                        unresolved_body_validate_schema_in_statement(
                            statement,
                            ctx_name,
                            schema_names,
                        )
                    })
                })
            })
            .or_else(|| {
                statement.finalizer.as_ref().and_then(|finalizer| {
                    finalizer.body.iter().find_map(|statement| {
                        unresolved_body_validate_schema_in_statement(
                            statement,
                            ctx_name,
                            schema_names,
                        )
                    })
                })
            }),
        _ => None,
    }
}

pub(super) fn unresolved_body_validate_schema_in_for_init(
    init: &ForStatementInit<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
) -> Option<(String, Span)> {
    match init {
        ForStatementInit::VariableDeclaration(declaration) => {
            declaration.declarations.iter().find_map(|declarator| {
                declarator.init.as_ref().and_then(|init| {
                    unresolved_body_validate_schema_in_expression(init, ctx_name, schema_names)
                })
            })
        }
        _ => init.as_expression().and_then(|expression| {
            unresolved_body_validate_schema_in_expression(expression, ctx_name, schema_names)
        }),
    }
}

pub(super) fn unresolved_body_validate_schema_in_expression(
    expression: &Expression<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
) -> Option<(String, Span)> {
    match expression {
        Expression::CallExpression(call) => {
            unresolved_body_validate_schema_in_call(call, ctx_name, schema_names)
        }
        Expression::AwaitExpression(expression) => unresolved_body_validate_schema_in_expression(
            &expression.argument,
            ctx_name,
            schema_names,
        ),
        Expression::ObjectExpression(object) => object.properties.iter().find_map(|property| {
            if let ObjectPropertyKind::ObjectProperty(property) = property {
                unresolved_body_validate_schema_in_expression(
                    &property.value,
                    ctx_name,
                    schema_names,
                )
            } else {
                None
            }
        }),
        Expression::ArrayExpression(array) => array.elements.iter().find_map(|element| {
            unresolved_body_validate_schema_in_array_element(element, ctx_name, schema_names)
        }),
        Expression::StaticMemberExpression(member) => {
            unresolved_body_validate_schema_in_expression(&member.object, ctx_name, schema_names)
        }
        Expression::ComputedMemberExpression(member) => {
            unresolved_body_validate_schema_in_expression(&member.object, ctx_name, schema_names)
                .or_else(|| {
                    unresolved_body_validate_schema_in_expression(
                        &member.expression,
                        ctx_name,
                        schema_names,
                    )
                })
        }
        Expression::ParenthesizedExpression(expression) => {
            unresolved_body_validate_schema_in_expression(
                &expression.expression,
                ctx_name,
                schema_names,
            )
        }
        Expression::ConditionalExpression(expression) => {
            unresolved_body_validate_schema_in_expression(&expression.test, ctx_name, schema_names)
                .or_else(|| {
                    unresolved_body_validate_schema_in_expression(
                        &expression.consequent,
                        ctx_name,
                        schema_names,
                    )
                })
                .or_else(|| {
                    unresolved_body_validate_schema_in_expression(
                        &expression.alternate,
                        ctx_name,
                        schema_names,
                    )
                })
        }
        Expression::UnaryExpression(expression) => unresolved_body_validate_schema_in_expression(
            &expression.argument,
            ctx_name,
            schema_names,
        ),
        Expression::BinaryExpression(expression) => {
            unresolved_body_validate_schema_in_expression(&expression.left, ctx_name, schema_names)
                .or_else(|| {
                    unresolved_body_validate_schema_in_expression(
                        &expression.right,
                        ctx_name,
                        schema_names,
                    )
                })
        }
        Expression::LogicalExpression(expression) => {
            unresolved_body_validate_schema_in_expression(&expression.left, ctx_name, schema_names)
                .or_else(|| {
                    unresolved_body_validate_schema_in_expression(
                        &expression.right,
                        ctx_name,
                        schema_names,
                    )
                })
        }
        Expression::SequenceExpression(expression) => {
            expression.expressions.iter().find_map(|expression| {
                unresolved_body_validate_schema_in_expression(expression, ctx_name, schema_names)
            })
        }
        _ => None,
    }
}

pub(super) fn unresolved_body_validate_schema_in_argument(
    argument: &Argument<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
) -> Option<(String, Span)> {
    match argument {
        Argument::CallExpression(call) => {
            unresolved_body_validate_schema_in_call(call, ctx_name, schema_names)
        }
        _ => argument.as_expression().and_then(|expression| {
            unresolved_body_validate_schema_in_expression(expression, ctx_name, schema_names)
        }),
    }
}

pub(super) fn unresolved_body_validate_schema_in_array_element(
    element: &ArrayExpressionElement<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
) -> Option<(String, Span)> {
    match element {
        ArrayExpressionElement::CallExpression(call) => {
            unresolved_body_validate_schema_in_call(call, ctx_name, schema_names)
        }
        _ => element.as_expression().and_then(|expression| {
            unresolved_body_validate_schema_in_expression(expression, ctx_name, schema_names)
        }),
    }
}

pub(super) fn unresolved_body_validate_schema_in_call(
    call: &CallExpression<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
) -> Option<(String, Span)> {
    if let Some(chain) = static_member_chain(&call.callee) {
        if chain.len() == 3 && chain[0] == ctx_name && chain[1] == "body" && chain[2] == "validate"
        {
            let Some(Argument::Identifier(identifier)) = call.arguments.first() else {
                return None;
            };
            let schema = identifier.name.as_str();
            if schema == "undefined" {
                return None;
            }
            return (!schema_names.contains(schema)).then(|| (schema.to_string(), identifier.span));
        }
    }
    unresolved_body_validate_schema_in_expression(&call.callee, ctx_name, schema_names).or_else(
        || {
            call.arguments.iter().find_map(|argument| {
                unresolved_body_validate_schema_in_argument(argument, ctx_name, schema_names)
            })
        },
    )
}

pub(super) fn arrow_requires_results_import(
    function: &oxc_ast::ast::ArrowFunctionExpression<'_>,
) -> bool {
    function
        .body
        .statements
        .iter()
        .any(statement_requires_results_import)
}

pub(super) fn function_requires_results_import(function: &oxc_ast::ast::Function<'_>) -> bool {
    function.body.as_ref().is_some_and(|body| {
        body.statements
            .iter()
            .any(statement_requires_results_import)
    })
}

pub(super) fn statement_requires_results_import(statement: &Statement<'_>) -> bool {
    match statement {
        Statement::VariableDeclaration(declaration) => {
            declaration.declarations.iter().any(|declarator| {
                declarator
                    .init
                    .as_ref()
                    .is_some_and(expression_requires_results_import)
            })
        }
        Statement::ReturnStatement(statement) => statement
            .argument
            .as_ref()
            .is_some_and(expression_requires_results_import),
        Statement::ExpressionStatement(statement) => {
            expression_requires_results_import(&statement.expression)
        }
        Statement::BlockStatement(block) => {
            block.body.iter().any(statement_requires_results_import)
        }
        Statement::IfStatement(statement) => {
            expression_requires_results_import(&statement.test)
                || statement_requires_results_import(&statement.consequent)
                || statement
                    .alternate
                    .as_ref()
                    .is_some_and(|alternate| statement_requires_results_import(alternate))
        }
        Statement::DoWhileStatement(statement) => {
            statement_requires_results_import(&statement.body)
                || expression_requires_results_import(&statement.test)
        }
        Statement::WhileStatement(statement) => {
            expression_requires_results_import(&statement.test)
                || statement_requires_results_import(&statement.body)
        }
        Statement::ForStatement(statement) => {
            statement
                .init
                .as_ref()
                .is_some_and(for_init_requires_results_import)
                || statement
                    .test
                    .as_ref()
                    .is_some_and(expression_requires_results_import)
                || statement
                    .update
                    .as_ref()
                    .is_some_and(expression_requires_results_import)
                || statement_requires_results_import(&statement.body)
        }
        Statement::ForInStatement(statement) => {
            expression_requires_results_import(&statement.right)
                || statement_requires_results_import(&statement.body)
        }
        Statement::ForOfStatement(statement) => {
            expression_requires_results_import(&statement.right)
                || statement_requires_results_import(&statement.body)
        }
        Statement::SwitchStatement(statement) => {
            expression_requires_results_import(&statement.discriminant)
                || statement.cases.iter().any(|case| {
                    case.test
                        .as_ref()
                        .is_some_and(expression_requires_results_import)
                        || case
                            .consequent
                            .iter()
                            .any(statement_requires_results_import)
                })
        }
        Statement::TryStatement(statement) => {
            statement
                .block
                .body
                .iter()
                .any(statement_requires_results_import)
                || statement.handler.as_ref().is_some_and(|handler| {
                    handler
                        .body
                        .body
                        .iter()
                        .any(statement_requires_results_import)
                })
                || statement.finalizer.as_ref().is_some_and(|finalizer| {
                    finalizer.body.iter().any(statement_requires_results_import)
                })
        }
        Statement::ThrowStatement(statement) => {
            expression_requires_results_import(&statement.argument)
        }
        _ => false,
    }
}

pub(super) fn for_init_requires_results_import(init: &ForStatementInit<'_>) -> bool {
    match init {
        ForStatementInit::VariableDeclaration(declaration) => {
            declaration.declarations.iter().any(|declarator| {
                declarator
                    .init
                    .as_ref()
                    .is_some_and(expression_requires_results_import)
            })
        }
        ForStatementInit::CallExpression(call) => call_requires_results_import(call),
        ForStatementInit::ParenthesizedExpression(parenthesized) => {
            expression_requires_results_import(&parenthesized.expression)
        }
        _ => false,
    }
}

pub(super) fn expression_requires_results_import(expression: &Expression<'_>) -> bool {
    match expression {
        Expression::CallExpression(call) => call_requires_results_import(call),
        Expression::NewExpression(expression) => {
            expression_requires_results_import(&expression.callee)
                || expression
                    .arguments
                    .iter()
                    .any(argument_requires_results_import)
        }
        Expression::AwaitExpression(expression) => {
            expression_requires_results_import(&expression.argument)
        }
        Expression::ArrayExpression(array) => array
            .elements
            .iter()
            .any(array_element_requires_results_import),
        Expression::ObjectExpression(object) => {
            object.properties.iter().any(|property| match property {
                ObjectPropertyKind::ObjectProperty(property) => {
                    expression_requires_results_import(&property.value)
                }
                ObjectPropertyKind::SpreadProperty(property) => {
                    expression_requires_results_import(&property.argument)
                }
            })
        }
        Expression::ParenthesizedExpression(parenthesized) => {
            expression_requires_results_import(&parenthesized.expression)
        }
        Expression::StaticMemberExpression(member) => {
            member_object_is_results(&member.object)
                || expression_requires_results_import(&member.object)
        }
        Expression::ComputedMemberExpression(member) => {
            member_object_is_results(&member.object)
                || expression_requires_results_import(&member.object)
                || expression_requires_results_import(&member.expression)
        }
        Expression::ChainExpression(chain) => {
            chain_element_requires_results_import(&chain.expression)
        }
        Expression::BinaryExpression(expression) => {
            expression_requires_results_import(&expression.left)
                || expression_requires_results_import(&expression.right)
        }
        Expression::LogicalExpression(expression) => {
            expression_requires_results_import(&expression.left)
                || expression_requires_results_import(&expression.right)
        }
        Expression::ConditionalExpression(expression) => {
            expression_requires_results_import(&expression.test)
                || expression_requires_results_import(&expression.consequent)
                || expression_requires_results_import(&expression.alternate)
        }
        Expression::SequenceExpression(expression) => expression
            .expressions
            .iter()
            .any(expression_requires_results_import),
        Expression::TaggedTemplateExpression(expression) => {
            expression_requires_results_import(&expression.tag)
        }
        Expression::UnaryExpression(expression) => {
            expression_requires_results_import(&expression.argument)
        }
        Expression::UpdateExpression(_) => false,
        Expression::YieldExpression(expression) => expression
            .argument
            .as_ref()
            .is_some_and(expression_requires_results_import),
        Expression::AssignmentExpression(expression) => {
            expression_requires_results_import(&expression.right)
        }
        Expression::ArrowFunctionExpression(function) => arrow_requires_results_import(function),
        Expression::FunctionExpression(function) => function_requires_results_import(function),
        Expression::TSAsExpression(expression) => {
            expression_requires_results_import(&expression.expression)
        }
        Expression::TSSatisfiesExpression(expression) => {
            expression_requires_results_import(&expression.expression)
        }
        Expression::TSNonNullExpression(expression) => {
            expression_requires_results_import(&expression.expression)
        }
        Expression::TSInstantiationExpression(expression) => {
            expression_requires_results_import(&expression.expression)
        }
        _ => false,
    }
}

pub(super) fn call_requires_results_import(call: &CallExpression<'_>) -> bool {
    expression_requires_results_import(&call.callee)
        || call.arguments.iter().any(argument_requires_results_import)
}

pub(super) fn argument_requires_results_import(argument: &Argument<'_>) -> bool {
    argument
        .as_expression()
        .is_some_and(expression_requires_results_import)
}

pub(super) fn array_element_requires_results_import(element: &ArrayExpressionElement<'_>) -> bool {
    element
        .as_expression()
        .is_some_and(expression_requires_results_import)
}

pub(super) fn chain_element_requires_results_import(element: &ChainElement<'_>) -> bool {
    match element {
        ChainElement::CallExpression(call) => call_requires_results_import(call),
        ChainElement::StaticMemberExpression(member) => {
            member_object_is_results(&member.object)
                || expression_requires_results_import(&member.object)
        }
        ChainElement::ComputedMemberExpression(member) => {
            member_object_is_results(&member.object)
                || expression_requires_results_import(&member.object)
                || expression_requires_results_import(&member.expression)
        }
        ChainElement::PrivateFieldExpression(member) => {
            expression_requires_results_import(&member.object)
        }
        ChainElement::TSNonNullExpression(expression) => {
            expression_requires_results_import(&expression.expression)
        }
    }
}

pub(super) fn member_object_is_results(expression: &Expression<'_>) -> bool {
    match expression {
        Expression::Identifier(identifier) => identifier.name.as_str() == "Results",
        Expression::ParenthesizedExpression(parenthesized) => {
            member_object_is_results(&parenthesized.expression)
        }
        _ => false,
    }
}

pub(super) fn typed_framework_handler_parameters(
    parameters: &oxc_ast::ast::FormalParameters<'_>,
) -> bool {
    parameters
        .items
        .iter()
        .any(|parameter| parameter.type_annotation.is_some())
}

pub(super) fn typed_framework_handler_from_arrow(
    function: &oxc_ast::ast::ArrowFunctionExpression<'_>,
    route_pattern: &str,
    source: &str,
    source_name: &str,
    schema_names: &BTreeSet<String>,
) -> Option<Handler> {
    let mut bindings =
        typed_framework_bindings_from_parameters(&function.params, route_pattern, schema_names)?;
    specialize_typed_context_bindings_for_usage(
        &mut bindings,
        &request_bindings_from_arrow(function, schema_names),
    );
    let responses = response_metadata_many_from_arrow(function, source_name, source, schema_names);
    let schema_replacements = handler_context_parameter_name_from_bindings(&bindings)
        .map(|ctx_name| {
            body_json_schema_argument_spans_arrow(function, &ctx_name, schema_names)
                .into_iter()
                .map(|edit| (edit.argument_span, "undefined"))
                .collect::<Vec<_>>()
        })
        .unwrap_or_default();
    let handler_source = crate::framework_runtime::typed_arrow_handler_source_with_replacements(
        function,
        source,
        &bindings,
        &schema_replacements,
    )?;
    Some(Handler {
        source: handler_source.original_source,
        emitted_source: handler_source.emitted_source,
        span: function.span,
        requires_results_import: arrow_requires_results_import(function),
        is_async: function.r#async,
        runtime_deferred: false,
        source_name: source_name.to_string(),
        source_text: source.to_string(),
        source_map_line_offset: handler_source.source_map_line_offset,
        source_map_column_offset: handler_source.source_map_column_offset,
        bindings,
        response: responses.first().cloned(),
        responses,
        effects: Vec::new(),
        schema_metadata_conflict: false,
    })
}

pub(super) fn typed_framework_handler_from_function(
    function: &oxc_ast::ast::Function<'_>,
    route_pattern: &str,
    source: &str,
    source_name: &str,
    schema_names: &BTreeSet<String>,
) -> Option<Handler> {
    let mut bindings =
        typed_framework_bindings_from_parameters(&function.params, route_pattern, schema_names)?;
    specialize_typed_context_bindings_for_usage(
        &mut bindings,
        &request_bindings_from_function(function, schema_names),
    );
    let responses =
        response_metadata_many_from_function(function, source_name, source, schema_names);
    let schema_replacements = handler_context_parameter_name_from_bindings(&bindings)
        .map(|ctx_name| {
            body_json_schema_argument_spans_function(function, &ctx_name, schema_names)
                .into_iter()
                .map(|edit| (edit.argument_span, "undefined"))
                .collect::<Vec<_>>()
        })
        .unwrap_or_default();
    let handler_source = crate::framework_runtime::typed_function_handler_source_with_replacements(
        function,
        source,
        &bindings,
        &schema_replacements,
    )?;
    Some(Handler {
        source: handler_source.original_source,
        emitted_source: handler_source.emitted_source,
        span: function.span,
        requires_results_import: function_requires_results_import(function),
        is_async: function.r#async,
        runtime_deferred: false,
        source_name: source_name.to_string(),
        source_text: source.to_string(),
        source_map_line_offset: handler_source.source_map_line_offset,
        source_map_column_offset: handler_source.source_map_column_offset,
        bindings,
        response: responses.first().cloned(),
        responses,
        effects: Vec::new(),
        schema_metadata_conflict: false,
    })
}

pub(super) fn typed_framework_bindings_from_parameters(
    parameters: &oxc_ast::ast::FormalParameters<'_>,
    route_pattern: &str,
    schema_names: &BTreeSet<String>,
) -> Option<Vec<RequestBinding>> {
    if parameters.rest.is_some() {
        return None;
    }
    let route_names = route_parameter_names(route_pattern);
    let mut bound_route_names = BTreeSet::new();
    let mut implicit_body_parameter = None::<String>;
    let mut bindings = Vec::new();
    for parameter in &parameters.items {
        if parameter.initializer.is_some()
            || parameter.optional
            || parameter.accessibility.is_some()
            || parameter.readonly
            || parameter.r#override
        {
            return None;
        }
        let BindingPattern::BindingIdentifier(identifier) = &parameter.pattern else {
            return None;
        };
        let parameter_name = identifier.name.as_str();
        let annotation = parameter.type_annotation.as_ref()?;
        let type_name = type_display_name(&annotation.type_annotation);
        let span = parameter.span;
        let binding = binding_from_typed_parameter(
            parameter_name,
            &annotation.type_annotation,
            &route_names,
            &mut bound_route_names,
            &mut implicit_body_parameter,
            schema_names,
            span,
        )?;
        let mut binding = binding;
        binding.type_name = Some(type_name);
        bindings.push(binding);
    }
    if route_names
        .iter()
        .any(|name| !bound_route_names.contains(name))
    {
        return None;
    }
    Some(dedupe_request_bindings(bindings))
}

pub(super) fn specialize_typed_context_bindings_for_usage(
    bindings: &mut [RequestBinding],
    inferred_bindings: &[RequestBinding],
) {
    let request_facade_only = !inferred_bindings.is_empty()
        && inferred_bindings
            .iter()
            .all(|binding| binding.kind == "context" && binding.name.as_deref() == Some("request"));
    if !request_facade_only {
        return;
    }
    for binding in bindings {
        if binding.kind == "context" && binding.name.as_deref() == Some("RequestContext") {
            binding.name = Some("request".to_string());
        }
    }
}

pub(super) fn binding_from_typed_parameter(
    parameter_name: &str,
    ty: &TSType<'_>,
    route_names: &BTreeSet<String>,
    bound_route_names: &mut BTreeSet<String>,
    body_parameter: &mut Option<String>,
    schema_names: &BTreeSet<String>,
    span: Span,
) -> Option<RequestBinding> {
    if let Some(wrapper) = wrapper_type_reference(ty) {
        if matches!(wrapper.0, "Body" | "Query" | "Route" | "Header")
            && unresolved_body_type(wrapper.1, schema_names)
        {
            return None;
        }
        if wrapper.0 == "Route" && !route_names.contains(parameter_name) {
            return None;
        }
        if wrapper.0 == "Route" {
            bound_route_names.insert(parameter_name.to_string());
        }
        if wrapper.0 == "Body" {
            if body_parameter
                .as_ref()
                .is_some_and(|existing| existing != parameter_name)
            {
                return None;
            }
            *body_parameter = Some(parameter_name.to_string());
        }
        return explicit_wrapper_binding(parameter_name, wrapper, schema_names, span);
    }
    if let Some(context_kind) = framework_context_type(ty) {
        return Some(framework_binding(
            "context",
            Some(context_kind),
            None,
            Some(parameter_name),
            span,
        ));
    }
    if let Some((provider_kind, name)) = provider_type_reference(ty) {
        let capability = provider_capability_token(provider_kind, &name);
        let mut binding =
            framework_binding("injection", Some(&name), None, Some(parameter_name), span);
        binding.injection_kind = Some("provider".to_string());
        binding.provider_kind = Some(provider_kind.to_string());
        binding.capability = Some(capability);
        return Some(binding);
    }
    if let Some(name) = work_queue_type_reference(ty) {
        let mut binding =
            framework_binding("injection", Some(&name), None, Some(parameter_name), span);
        binding.injection_kind = Some("queue".to_string());
        binding.provider_kind = Some("workqueue".to_string());
        binding.capability = Some(format!("queue.{name}"));
        return Some(binding);
    }
    if route_names.contains(parameter_name) && primitive_type_schema(ty).is_some() {
        bound_route_names.insert(parameter_name.to_string());
        let schema = primitive_type_schema(ty);
        let mut binding = framework_binding(
            "route",
            Some(parameter_name),
            schema.as_deref(),
            Some(parameter_name),
            span,
        );
        binding.semantic = semantic_name_from_type(ty).map(ToOwned::to_owned);
        return Some(binding);
    }
    if primitive_type_schema(ty).is_some() {
        return None;
    }
    let schema = schema_name_from_type(ty, schema_names)?;
    if body_parameter
        .as_ref()
        .is_some_and(|existing| existing != parameter_name)
    {
        return None;
    }
    *body_parameter = Some(parameter_name.to_string());
    Some(framework_binding(
        "body.json",
        None,
        Some(&schema),
        Some(parameter_name),
        span,
    ))
}

pub(super) fn explicit_wrapper_binding(
    parameter_name: &str,
    wrapper: (&str, &TSType<'_>, Option<String>),
    schema_names: &BTreeSet<String>,
    span: Span,
) -> Option<RequestBinding> {
    let (wrapper_name, target_type, key) = wrapper;
    let schema = match wrapper_name {
        "Body" => schema_name_from_type(target_type, schema_names),
        _ => primitive_type_schema(target_type)
            .or_else(|| schema_name_from_type(target_type, schema_names))
            .or_else(|| semantic_name_from_type(target_type).map(ToOwned::to_owned)),
    };
    let kind = match wrapper_name {
        "Route" => "route",
        "Query" => "query",
        "Body" => "body.json",
        "Header" => "header",
        "Service" => "injection",
        "Config" => "config",
        _ => return None,
    };
    let name = if wrapper_name == "Service" {
        Some(type_display_name(target_type))
    } else if wrapper_name == "Config" {
        type_string_literal_value(target_type).or_else(|| Some(parameter_name.to_string()))
    } else {
        key.as_deref()
            .or(Some(parameter_name))
            .map(ToOwned::to_owned)
    };
    let mut binding = framework_binding(
        kind,
        name.as_deref(),
        schema.as_deref(),
        Some(parameter_name),
        span,
    );
    binding.wrapper = Some(wrapper_name.to_string());
    if wrapper_name == "Service" {
        binding.injection_kind = Some("service".to_string());
    }
    binding.semantic = semantic_name_from_type(target_type).map(ToOwned::to_owned);
    binding.redacted = semantic_name_from_type(target_type)
        .is_some_and(|name| matches!(name, "PasswordString" | "SecretString"));
    Some(binding)
}

pub(super) fn framework_binding(
    kind: &str,
    name: Option<&str>,
    schema: Option<&str>,
    parameter: Option<&str>,
    span: Span,
) -> RequestBinding {
    RequestBinding {
        kind: kind.to_string(),
        name: name.map(ToOwned::to_owned),
        schema: schema.map(ToOwned::to_owned),
        parameter: parameter.map(ToOwned::to_owned),
        type_name: None,
        source_name: None,
        source_text: None,
        span: Some(span),
        wrapper: None,
        injection_kind: None,
        provider_kind: None,
        capability: None,
        semantic: None,
        redacted: false,
    }
}

pub(super) fn primitive_type_schema(ty: &TSType<'_>) -> Option<String> {
    match ty {
        TSType::TSStringKeyword(_) => Some("string".to_string()),
        TSType::TSNumberKeyword(_) => Some("number".to_string()),
        TSType::TSBooleanKeyword(_) => Some("bool".to_string()),
        TSType::TSTypeReference(reference) => {
            let name = typescript_type_name(&reference.type_name)?;
            match name {
                "Email" | "NonEmptyString" | "PasswordString" | "SecretString" | "Uuid"
                | "DateTime" | "Instant" => Some("string".to_string()),
                "PositiveInt" => Some("int".to_string()),
                _ => None,
            }
        }
        _ => None,
    }
}

pub(super) fn schema_name_from_type(
    ty: &TSType<'_>,
    schema_names: &BTreeSet<String>,
) -> Option<String> {
    match ty {
        TSType::TSTypeReference(reference) => {
            let name = typescript_type_name(&reference.type_name)?;
            if schema_names.contains(name) {
                Some(name.to_string())
            } else {
                None
            }
        }
        TSType::TSTypeLiteral(_) => Some("<inline>".to_string()),
        _ => None,
    }
}

pub(super) fn semantic_name_from_type(ty: &TSType<'_>) -> Option<&'static str> {
    let TSType::TSTypeReference(reference) = ty else {
        return None;
    };
    match typescript_type_name(&reference.type_name)? {
        "Email" => Some("Email"),
        "NonEmptyString" => Some("NonEmptyString"),
        "PasswordString" => Some("PasswordString"),
        "SecretString" => Some("SecretString"),
        "Uuid" => Some("Uuid"),
        "PositiveInt" => Some("PositiveInt"),
        "DateTime" => Some("DateTime"),
        "Instant" => Some("Instant"),
        _ => None,
    }
}

pub(super) fn wrapper_type_reference<'a>(
    ty: &'a TSType<'a>,
) -> Option<(&'a str, &'a TSType<'a>, Option<String>)> {
    let TSType::TSTypeReference(reference) = ty else {
        return None;
    };
    let wrapper = typescript_type_name(&reference.type_name)?;
    if !matches!(
        wrapper,
        "Route" | "Query" | "Body" | "Header" | "Service" | "Config"
    ) {
        return None;
    }
    let arguments = reference.type_arguments.as_ref()?;
    if wrapper == "Header" {
        let key = arguments
            .params
            .first()
            .and_then(type_string_literal_value)?;
        let target = arguments.params.get(1).unwrap_or(arguments.params.first()?);
        return Some((wrapper, target, Some(key)));
    }
    let target = arguments.params.first()?;
    Some((wrapper, target, None))
}

pub(super) fn framework_context_type(ty: &TSType<'_>) -> Option<&'static str> {
    let TSType::TSTypeReference(reference) = ty else {
        return None;
    };
    match typescript_type_name(&reference.type_name)? {
        "RequestContext" => Some("RequestContext"),
        "SlopRequest" => Some("SlopRequest"),
        "SlopResponse" => Some("SlopResponse"),
        "CancellationSignal" => Some("CancellationSignal"),
        "Deadline" => Some("Deadline"),
        _ => None,
    }
}

pub(super) fn provider_type_reference(ty: &TSType<'_>) -> Option<(&'static str, String)> {
    let TSType::TSTypeReference(reference) = ty else {
        return None;
    };
    let provider_kind = match typescript_type_name(&reference.type_name)? {
        "Postgres" => "postgres",
        "Sqlite" => "sqlite",
        "SqlServer" => "sqlserver",
        _ => return None,
    };
    let name = reference
        .type_arguments
        .as_ref()
        .and_then(|arguments| arguments.params.first())
        .and_then(type_string_literal_value)?;
    Some((provider_kind, name))
}

pub(super) fn work_queue_type_reference(ty: &TSType<'_>) -> Option<String> {
    let TSType::TSTypeReference(reference) = ty else {
        return None;
    };
    if typescript_type_name(&reference.type_name)? != "WorkQueue" {
        return None;
    }
    reference
        .type_arguments
        .as_ref()
        .and_then(|arguments| arguments.params.first())
        .and_then(type_string_literal_value)
}

pub(super) fn provider_capability_token(_provider_kind: &str, name: &str) -> String {
    normalize_database_provider_token(name)
}

pub(super) fn type_string_literal_value(ty: &TSType<'_>) -> Option<String> {
    let TSType::TSLiteralType(literal) = ty else {
        return None;
    };
    let TSLiteral::StringLiteral(value) = &literal.literal else {
        return None;
    };
    Some(value.value.as_str().to_string())
}

pub(super) fn type_display_name(ty: &TSType<'_>) -> String {
    match ty {
        TSType::TSStringKeyword(_) => "string".to_string(),
        TSType::TSNumberKeyword(_) => "number".to_string(),
        TSType::TSBooleanKeyword(_) => "boolean".to_string(),
        TSType::TSNullKeyword(_) => "null".to_string(),
        TSType::TSArrayType(array) => format!("{}[]", type_display_name(&array.element_type)),
        TSType::TSTypeReference(reference) => {
            let name = typescript_type_name(&reference.type_name).unwrap_or("<qualified>");
            if let Some(arguments) = &reference.type_arguments {
                let values = arguments
                    .params
                    .iter()
                    .map(type_display_name)
                    .collect::<Vec<_>>();
                format!("{name}<{}>", values.join(", "))
            } else {
                name.to_string()
            }
        }
        TSType::TSLiteralType(literal) => match &literal.literal {
            TSLiteral::StringLiteral(value) => format!("\"{}\"", value.value.as_str()),
            TSLiteral::NumericLiteral(value) => value.value.to_string(),
            TSLiteral::BooleanLiteral(value) => value.value.to_string(),
            _ => "<literal>".to_string(),
        },
        _ => "<unsupported>".to_string(),
    }
}

pub(super) fn framework_binding_diagnostic(
    path: &Path,
    argument: &Argument<'_>,
    route_pattern: &str,
    schema_names: &BTreeSet<String>,
) -> Option<Diagnostic> {
    let parameters = match argument {
        Argument::ArrowFunctionExpression(function)
            if typed_framework_handler_parameters(&function.params) =>
        {
            &function.params
        }
        Argument::FunctionExpression(function)
            if typed_framework_handler_parameters(&function.params) =>
        {
            &function.params
        }
        _ => return None,
    };
    let route_names = route_parameter_names(route_pattern);
    if parameters.rest.is_some() {
        return Some(
            Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_HANDLER_PARAMETERS",
                "framework route handlers do not support rest parameters",
            )
            .with_path(path)
            .with_span(parameters.span),
        );
    }
    let mut body_parameter = None::<String>;
    let mut bound_route_names = BTreeSet::new();
    for parameter in &parameters.items {
        let BindingPattern::BindingIdentifier(identifier) = &parameter.pattern else {
            return Some(
                Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_HANDLER_PARAMETERS",
                    "framework route handler parameters must be simple identifiers",
                )
                .with_path(path)
                .with_span(parameter.span),
            );
        };
        let parameter_name = identifier.name.as_str();
        let Some(annotation) = &parameter.type_annotation else {
            return Some(
                Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_HANDLER_PARAMETERS",
                    "framework route handler parameters must have TypeScript type annotations",
                )
                .with_path(path)
                .with_span(parameter.span),
            );
        };
        let ty = &annotation.type_annotation;
        if malformed_header_wrapper(ty) {
            return Some(
                Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_HEADER_BINDING",
                    "Header<T> binding requires a string literal header name",
                )
                .with_path(path)
                .with_span(parameter.span)
                .with_hint("Use Header<\"x-request-id\"> or Header<\"x-request-id\", string>."),
            );
        }
        if let Some(provider_kind) = provider_type_reference_without_literal_name(ty) {
            return Some(
                Diagnostic::new(
                    "SLOPPYC_E_DYNAMIC_PROVIDER_NAME",
                    format!("{provider_kind} provider injection requires a string literal name"),
                )
                .with_path(path)
                .with_span(parameter.span)
                .with_hint("Use Postgres<\"main\">, Sqlite<\"main\">, or SqlServer<\"main\">."),
            );
        }
        if let Some(marker) = unknown_generic_injection_marker(ty) {
            return Some(
                Diagnostic::new(
                    "SLOPPYC_E_UNKNOWN_INJECTION_MARKER",
                    format!("unknown provider/service/queue marker '{marker}'"),
                )
                .with_path(path)
                .with_span(parameter.span)
                .with_hint(
                    "Use supported provider markers or wrap ordinary body models with Body<T>.",
                ),
            );
        }
        if let Some(wrapper) = wrapper_type_reference(ty) {
            if wrapper.0 == "Header" && wrapper.2.is_none() {
                return Some(
                    Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_HEADER_BINDING",
                        "Header<T> binding requires a string literal header name",
                    )
                    .with_path(path)
                    .with_span(parameter.span)
                    .with_hint("Use Header<\"x-request-id\"> or Header<\"x-request-id\", string>."),
                );
            }
            if matches!(wrapper.0, "Body" | "Query" | "Route" | "Header")
                && unresolved_body_type(wrapper.1, schema_names)
            {
                return Some(
                    Diagnostic::new(
                        "SLOPPYC_E_UNRESOLVED_TYPE",
                        format!(
                            "handler parameter '{parameter_name}' references an unresolved binding type"
                        ),
                    )
                    .with_path(path)
                    .with_span(parameter.span)
                    .with_hint("Declare a concrete type alias or interface in the same source file."),
                );
            }
            if wrapper.0 == "Route" {
                if !route_names.contains(parameter_name) {
                    return Some(
                        Diagnostic::new(
                            "SLOPPYC_E_ROUTE_BINDING_MISMATCH",
                            format!(
                                "explicit Route<T> parameter '{parameter_name}' does not match any route segment"
                            ),
                        )
                        .with_path(path)
                        .with_span(parameter.span)
                        .with_hint("Rename the parameter to match a route segment or use Query<T>, Header<...>, or Body<T>."),
                    );
                }
                bound_route_names.insert(parameter_name.to_string());
            }
            if wrapper.0 == "Body" {
                if body_parameter
                    .as_ref()
                    .is_some_and(|existing| existing != parameter_name)
                {
                    return Some(
                        Diagnostic::new(
                            "SLOPPYC_E_MULTIPLE_BODY_BINDINGS",
                            "route handlers may have only one body parameter",
                        )
                        .with_path(path)
                        .with_span(parameter.span)
                        .with_hint(
                            "Use one Body<T> wrapper or combine the body shape into one model.",
                        ),
                    );
                }
                body_parameter = Some(parameter_name.to_string());
            }
            continue;
        }
        if framework_context_type(ty).is_some()
            || provider_type_reference(ty).is_some()
            || work_queue_type_reference(ty).is_some()
        {
            continue;
        }
        if route_names.contains(parameter_name) && primitive_type_schema(ty).is_some() {
            bound_route_names.insert(parameter_name.to_string());
            continue;
        }
        if primitive_type_schema(ty).is_some() {
            return Some(
                Diagnostic::new(
                    "SLOPPYC_E_AMBIGUOUS_BINDING",
                    format!("primitive handler parameter '{parameter_name}' has no binding source"),
                )
                .with_path(path)
                .with_span(parameter.span)
                .with_hint(
                    "Use Query<T>, Route<T>, Header<...>, or Body<T> to make the binding explicit.",
                ),
            );
        }
        if unresolved_body_type(ty, schema_names) {
            return Some(
                Diagnostic::new(
                    "SLOPPYC_E_UNRESOLVED_TYPE",
                    format!(
                        "handler parameter '{parameter_name}' references an unresolved body type"
                    ),
                )
                .with_path(path)
                .with_span(parameter.span)
                .with_hint("Declare a concrete type alias or interface in the same source file."),
            );
        }
        if body_parameter
            .as_ref()
            .is_some_and(|existing| existing != parameter_name)
        {
            return Some(
                Diagnostic::new(
                    "SLOPPYC_E_MULTIPLE_BODY_BINDINGS",
                    "route handlers may have only one body parameter",
                )
                .with_path(path)
                .with_span(parameter.span)
                .with_hint(
                    "Use explicit Body<T> wrappers or combine the body shape into one model.",
                ),
            );
        }
        body_parameter = Some(parameter_name.to_string());
    }
    if let Some(unbound) = route_names
        .iter()
        .find(|name| !bound_route_names.contains(*name))
    {
        return Some(
            Diagnostic::new(
                "SLOPPYC_E_UNBOUND_ROUTE_PARAMETER",
                format!("route parameter '{unbound}' is not bound by the handler signature"),
            )
            .with_path(path)
            .with_span(parameters.span)
            .with_hint("Add a matching parameter name or an explicit Route<T> binding."),
        );
    }
    None
}

pub(super) fn malformed_header_wrapper(ty: &TSType<'_>) -> bool {
    let TSType::TSTypeReference(reference) = ty else {
        return false;
    };
    typescript_type_name(&reference.type_name) == Some("Header")
        && reference
            .type_arguments
            .as_ref()
            .and_then(|arguments| arguments.params.first())
            .and_then(type_string_literal_value)
            .is_none()
}

pub(super) fn provider_type_reference_without_literal_name(
    ty: &TSType<'_>,
) -> Option<&'static str> {
    let TSType::TSTypeReference(reference) = ty else {
        return None;
    };
    let provider_kind = match typescript_type_name(&reference.type_name)? {
        "Postgres" => "postgres",
        "Sqlite" => "sqlite",
        "SqlServer" => "sqlserver",
        _ => return None,
    };
    if reference
        .type_arguments
        .as_ref()
        .and_then(|arguments| arguments.params.first())
        .and_then(type_string_literal_value)
        .is_some()
    {
        None
    } else {
        Some(provider_kind)
    }
}

pub(super) fn unknown_generic_injection_marker(ty: &TSType<'_>) -> Option<String> {
    let TSType::TSTypeReference(reference) = ty else {
        return None;
    };
    let name = typescript_type_name(&reference.type_name)?;
    let Some(arguments) = &reference.type_arguments else {
        return None;
    };
    if matches!(
        name,
        "Route"
            | "Query"
            | "Body"
            | "Header"
            | "Service"
            | "Config"
            | "Postgres"
            | "Sqlite"
            | "SqlServer"
            | "WorkQueue"
            | "PasswordString"
    ) {
        return None;
    }
    arguments
        .params
        .first()
        .and_then(type_string_literal_value)?;
    Some(name.to_string())
}

pub(super) fn unresolved_body_type(ty: &TSType<'_>, schema_names: &BTreeSet<String>) -> bool {
    let TSType::TSTypeReference(reference) = ty else {
        return false;
    };
    let Some(name) = typescript_type_name(&reference.type_name) else {
        return false;
    };
    if schema_names.contains(name) {
        return false;
    }
    !matches!(
        name,
        "Email"
            | "NonEmptyString"
            | "PasswordString"
            | "SecretString"
            | "Uuid"
            | "PositiveInt"
            | "DateTime"
            | "Instant"
            | "Postgres"
            | "Sqlite"
            | "SqlServer"
            | "WorkQueue"
            | "RequestContext"
            | "SlopRequest"
            | "SlopResponse"
            | "CancellationSignal"
            | "Deadline"
            | "Route"
            | "Query"
            | "Body"
            | "Header"
            | "Service"
            | "Config"
    )
}

pub(super) fn handler_diagnostic(
    path: &Path,
    argument: &Argument<'_>,
    route_pattern: &str,
    schema_names: &BTreeSet<String>,
    fallback_span: Span,
) -> Diagnostic {
    if let Some(diagnostic) =
        framework_binding_diagnostic(path, argument, route_pattern, schema_names)
    {
        return diagnostic;
    }
    let (code, message, hint) = match argument {
        Argument::ArrowFunctionExpression(function) => {
            if handler_parameters_are_unsupported(&function.params) {
                (
                    "SLOPPYC_E_UNSUPPORTED_HANDLER_PARAMETERS",
                    "compiled route handlers may declare zero parameters or one simple context parameter",
                    Some("The current runtime passes one plain request context object."),
                )
            } else if arrow_has_typescript_syntax(function) {
                (
                    "SLOPPYC_E_UNSUPPORTED_TYPESCRIPT_HANDLER",
                    "TypeScript handler syntax is not supported by the supported app compiler",
                    Some("Use JavaScript handler syntax until TypeScript lowering is implemented."),
                )
            } else if handler_result_uses_unsupported_values_arrow(function, schema_names) {
                (
                    "SLOPPYC_E_UNSUPPORTED_HANDLER_VALUE",
                    "route handler result arguments must be inline JSON-safe values",
                    Some("Use literals, arrays, and object literals instead of closed-over bindings."),
                )
            } else if function.r#async {
                (
                    "SLOPPYC_E_UNSUPPORTED_ASYNC_HANDLER_BODY",
                    "async handler extraction currently supports one direct Results.* return",
                    Some("Keep async handlers to direct Results.* returns for the current Promise settlement contract."),
                )
            } else {
                (
                    "SLOPPYC_E_UNSUPPORTED_HANDLER",
                    "route handler must be a simple function returning a supported Results.* descriptor",
                    None,
                )
            }
        }
        Argument::FunctionExpression(function) => {
            if handler_parameters_are_unsupported(&function.params) {
                (
                    "SLOPPYC_E_UNSUPPORTED_HANDLER_PARAMETERS",
                    "compiled route handlers may declare zero parameters or one simple context parameter",
                    Some("The current runtime passes one plain request context object."),
                )
            } else if function_has_typescript_syntax(function) {
                (
                    "SLOPPYC_E_UNSUPPORTED_TYPESCRIPT_HANDLER",
                    "TypeScript handler syntax is not supported by the supported app compiler",
                    Some("Use JavaScript handler syntax until TypeScript lowering is implemented."),
                )
            } else if handler_result_uses_unsupported_values_function(function, schema_names) {
                (
                    "SLOPPYC_E_UNSUPPORTED_HANDLER_VALUE",
                    "route handler result arguments must be inline JSON-safe values",
                    Some("Use literals, arrays, and object literals instead of closed-over bindings."),
                )
            } else if function.r#async {
                (
                    "SLOPPYC_E_UNSUPPORTED_ASYNC_HANDLER_BODY",
                    "async handler extraction currently supports one direct Results.* return",
                    Some("Keep async handlers to direct Results.* returns for the current Promise settlement contract."),
                )
            } else {
                (
                    "SLOPPYC_E_UNSUPPORTED_HANDLER",
                    "route handler must be a simple function returning a supported Results.* descriptor",
                    None,
                )
            }
        }
        _ => (
            "SLOPPYC_E_UNSUPPORTED_HANDLER",
            "route handler must be an inline function or arrow expression",
            None,
        ),
    };

    let mut diagnostic = Diagnostic::new(code, message)
        .with_path(path)
        .with_span(argument_span(argument).unwrap_or(fallback_span));
    if let Some(hint) = hint {
        diagnostic = diagnostic.with_hint(hint);
    }
    diagnostic
}

pub(super) fn handler_metadata_failure_can_fallback_to_dynamic(
    argument: &Argument<'_>,
    source: &str,
    state: &AppState,
    diagnostic: &Diagnostic,
) -> bool {
    if diagnostic.code != "SLOPPYC_E_UNSUPPORTED_HANDLER_VALUE" {
        return false;
    }
    if state.data_imported || state.sql_imported || !state.provider_bindings.is_empty() {
        return false;
    }

    let Some(span) = argument_span(argument) else {
        return false;
    };
    let handler_source = source_slice(source, span).unwrap_or_default();
    if handler_source.contains(".body.")
        || handler_source.contains(".body[")
        || handler_source.contains(".provider(")
        || handler_source.contains("app.provider(")
        || handler_source.contains("db.")
    {
        return false;
    }

    true
}

pub(super) fn handler_parameters_are_unsupported(
    parameters: &oxc_ast::ast::FormalParameters<'_>,
) -> bool {
    if parameters.items.len() > 1 || parameters.rest.is_some() {
        return true;
    }

    let Some(parameter) = parameters.items.first() else {
        return false;
    };

    parameter.initializer.is_some()
        || !matches!(parameter.pattern, BindingPattern::BindingIdentifier(_))
}

pub(super) fn handler_parameters_are_unsupported_for_kind(
    parameters: &oxc_ast::ast::FormalParameters<'_>,
    route_kind: &str,
) -> bool {
    let max_parameters = if route_kind == "sse" || route_kind == "websocket" {
        2
    } else {
        1
    };
    if parameters.items.len() > max_parameters || parameters.rest.is_some() {
        return true;
    }
    parameters.items.iter().any(|parameter| {
        parameter.initializer.is_some()
            || !matches!(parameter.pattern, BindingPattern::BindingIdentifier(_))
    })
}

pub(super) fn handler_context_parameter_name(
    parameters: &oxc_ast::ast::FormalParameters<'_>,
) -> Option<String> {
    let parameter = parameters.items.first()?;
    let BindingPattern::BindingIdentifier(identifier) = &parameter.pattern else {
        return None;
    };
    Some(identifier.name.as_str().to_string())
}

pub(super) fn handler_context_parameter_name_from_bindings(
    bindings: &[RequestBinding],
) -> Option<String> {
    bindings.iter().find_map(|binding| {
        (binding.kind == "context" && binding.name.as_deref() == Some("RequestContext"))
            .then(|| binding.parameter.clone())
            .flatten()
    })
}

pub(super) fn arrow_has_typescript_syntax(
    function: &oxc_ast::ast::ArrowFunctionExpression<'_>,
) -> bool {
    function.type_parameters.is_some()
        || function.return_type.is_some()
        || parameters_have_typescript_syntax(&function.params)
}

pub(super) fn function_has_typescript_syntax(function: &oxc_ast::ast::Function<'_>) -> bool {
    function.type_parameters.is_some()
        || function.this_param.is_some()
        || function.return_type.is_some()
        || parameters_have_typescript_syntax(&function.params)
}

pub(super) fn parameters_have_typescript_syntax(
    parameters: &oxc_ast::ast::FormalParameters<'_>,
) -> bool {
    parameters.items.iter().any(|parameter| {
        parameter.type_annotation.is_some()
            || parameter.optional
            || parameter.accessibility.is_some()
            || parameter.readonly
            || parameter.r#override
    }) || parameters
        .rest
        .as_ref()
        .is_some_and(|rest| rest.type_annotation.is_some())
}

pub(super) fn handler_result_uses_unsupported_values_arrow(
    function: &oxc_ast::ast::ArrowFunctionExpression<'_>,
    schema_names: &BTreeSet<String>,
) -> bool {
    let roots = function_parameter_roots(&function.params);
    if function.expression {
        return function
            .body
            .statements
            .first()
            .and_then(expression_statement_result_call)
            .is_some_and(|call| !results_call_arguments_are_supported(call, &roots, schema_names));
    }

    function
        .body
        .statements
        .first()
        .and_then(return_statement_result_call)
        .is_some_and(|call| !results_call_arguments_are_supported(call, &roots, schema_names))
}

pub(super) fn handler_result_uses_unsupported_values_function(
    function: &oxc_ast::ast::Function<'_>,
    schema_names: &BTreeSet<String>,
) -> bool {
    let roots = function_parameter_roots(&function.params);
    let Some(body) = &function.body else {
        return false;
    };
    body.statements
        .first()
        .and_then(return_statement_result_call)
        .is_some_and(|call| !results_call_arguments_are_supported(call, &roots, schema_names))
}

pub(super) fn response_metadata_many_from_arrow(
    function: &oxc_ast::ast::ArrowFunctionExpression<'_>,
    source_name: &str,
    source: &str,
    schema_names: &BTreeSet<String>,
) -> Vec<ResponseMetadata> {
    let mut responses = Vec::new();
    let mut response_schema_scopes = vec![BTreeMap::new()];
    for statement in &function.body.statements {
        collect_statement_responses(
            statement,
            source_name,
            source,
            &mut response_schema_scopes,
            schema_names,
            &mut responses,
        );
    }
    dedupe_response_metadata(responses)
}

pub(super) fn response_metadata_many_from_function(
    function: &oxc_ast::ast::Function<'_>,
    source_name: &str,
    source: &str,
    schema_names: &BTreeSet<String>,
) -> Vec<ResponseMetadata> {
    let mut responses = Vec::new();
    if let Some(body) = &function.body {
        let mut response_schema_scopes = vec![BTreeMap::new()];
        for statement in &body.statements {
            collect_statement_responses(
                statement,
                source_name,
                source,
                &mut response_schema_scopes,
                schema_names,
                &mut responses,
            );
        }
    }
    dedupe_response_metadata(responses)
}

pub(super) fn collect_statement_responses(
    statement: &Statement<'_>,
    source_name: &str,
    source: &str,
    response_schema_scopes: &mut Vec<BTreeMap<String, String>>,
    schema_names: &BTreeSet<String>,
    responses: &mut Vec<ResponseMetadata>,
) {
    match statement {
        Statement::VariableDeclaration(declaration) => {
            for declarator in &declaration.declarations {
                let BindingPattern::BindingIdentifier(identifier) = &declarator.id else {
                    continue;
                };
                let Some(init) = &declarator.init else {
                    continue;
                };
                let Some(schema) =
                    response_schema_from_expression(init, response_schema_scopes, schema_names)
                else {
                    continue;
                };
                if let Some(scope) = response_schema_scopes.last_mut() {
                    scope.insert(identifier.name.as_str().to_string(), schema);
                }
            }
        }
        Statement::ReturnStatement(statement) => {
            if let Some(argument) = &statement.argument {
                collect_expression_responses(
                    argument,
                    source_name,
                    source,
                    response_schema_scopes,
                    schema_names,
                    responses,
                );
            }
        }
        Statement::ExpressionStatement(statement) => {
            collect_expression_responses(
                &statement.expression,
                source_name,
                source,
                response_schema_scopes,
                schema_names,
                responses,
            );
        }
        Statement::BlockStatement(block) => {
            response_schema_scopes.push(BTreeMap::new());
            for statement in &block.body {
                collect_statement_responses(
                    statement,
                    source_name,
                    source,
                    response_schema_scopes,
                    schema_names,
                    responses,
                );
            }
            response_schema_scopes.pop();
        }
        Statement::IfStatement(statement) => {
            let mut consequent_scopes = response_schema_scopes.clone();
            collect_statement_responses(
                &statement.consequent,
                source_name,
                source,
                &mut consequent_scopes,
                schema_names,
                responses,
            );
            if let Some(alternate) = &statement.alternate {
                let mut alternate_scopes = response_schema_scopes.clone();
                collect_statement_responses(
                    alternate,
                    source_name,
                    source,
                    &mut alternate_scopes,
                    schema_names,
                    responses,
                );
            }
        }
        _ => {}
    }
}

pub(super) fn collect_expression_responses(
    expression: &Expression<'_>,
    source_name: &str,
    source: &str,
    response_schema_scopes: &mut Vec<BTreeMap<String, String>>,
    schema_names: &BTreeSet<String>,
    responses: &mut Vec<ResponseMetadata>,
) {
    if let Some(call) = result_call(expression) {
        if let Some(mut response) = response_metadata_from_call(call) {
            response.body_schema =
                response_schema_from_result_call(call, response_schema_scopes, schema_names);
            response.source_name = Some(source_name.to_string());
            response.source_text = Some(source.to_string());
            response.span = Some(call.span);
            responses.push(response);
        }
        return;
    }
    match expression {
        Expression::ConditionalExpression(conditional) => {
            collect_expression_responses(
                &conditional.consequent,
                source_name,
                source,
                response_schema_scopes,
                schema_names,
                responses,
            );
            collect_expression_responses(
                &conditional.alternate,
                source_name,
                source,
                response_schema_scopes,
                schema_names,
                responses,
            );
        }
        Expression::ParenthesizedExpression(parenthesized) => {
            collect_expression_responses(
                &parenthesized.expression,
                source_name,
                source,
                response_schema_scopes,
                schema_names,
                responses,
            );
        }
        _ => {}
    }
}

pub(super) fn response_schema_from_result_call(
    call: &CallExpression<'_>,
    response_schema_scopes: &[BTreeMap<String, String>],
    schema_names: &BTreeSet<String>,
) -> Option<String> {
    response_schema_from_result_type_arguments(call, schema_names).or_else(|| {
        response_body_argument(call).and_then(|argument| {
            response_schema_from_argument(argument, response_schema_scopes, schema_names)
        })
    })
}

pub(super) fn response_schema_from_result_type_arguments(
    call: &CallExpression<'_>,
    schema_names: &BTreeSet<String>,
) -> Option<String> {
    let (receiver, _) = static_member_name(&call.callee)?;
    if receiver != "Results" {
        return None;
    }
    response_schema_from_type_arguments(call, schema_names)
}

pub(super) fn response_schema_from_data_call_type_arguments(
    call: &CallExpression<'_>,
    schema_names: &BTreeSet<String>,
) -> Option<String> {
    let (_, method) = static_member_name(&call.callee)?;
    if !matches!(method, "queryOne") {
        return None;
    }
    response_schema_from_type_arguments(call, schema_names)
}

pub(super) fn response_schema_from_type_arguments(
    call: &CallExpression<'_>,
    schema_names: &BTreeSet<String>,
) -> Option<String> {
    let ty = call.type_arguments.as_ref()?.params.first()?;
    schema_name_from_type(ty, schema_names)
}

pub(super) fn response_body_argument<'a>(call: &'a CallExpression<'a>) -> Option<&'a Argument<'a>> {
    let (_, helper) = static_member_name(&call.callee)?;
    let index = match helper {
        "ok" | "json" | "bytes" | "accepted" | "badRequest" | "unauthorized" | "notFound"
        | "problem" => 0,
        "created" | "status" => 1,
        _ => return None,
    };
    call.arguments.get(index)
}

pub(super) fn response_schema_from_argument(
    argument: &Argument<'_>,
    response_schema_scopes: &[BTreeMap<String, String>],
    schema_names: &BTreeSet<String>,
) -> Option<String> {
    match argument {
        Argument::Identifier(identifier) => {
            response_schema_lookup(response_schema_scopes, identifier.name.as_str())
        }
        Argument::CallExpression(call) => {
            response_schema_from_data_call_type_arguments(call, schema_names)
        }
        Argument::ParenthesizedExpression(parenthesized) => response_schema_from_expression(
            &parenthesized.expression,
            response_schema_scopes,
            schema_names,
        ),
        _ => None,
    }
}

pub(super) fn response_schema_from_expression(
    expression: &Expression<'_>,
    response_schema_scopes: &[BTreeMap<String, String>],
    schema_names: &BTreeSet<String>,
) -> Option<String> {
    match expression {
        Expression::Identifier(identifier) => {
            response_schema_lookup(response_schema_scopes, identifier.name.as_str())
        }
        Expression::CallExpression(call) => {
            response_schema_from_data_call_type_arguments(call, schema_names)
        }
        Expression::AwaitExpression(await_expression) => response_schema_from_expression(
            &await_expression.argument,
            response_schema_scopes,
            schema_names,
        ),
        Expression::ParenthesizedExpression(parenthesized) => response_schema_from_expression(
            &parenthesized.expression,
            response_schema_scopes,
            schema_names,
        ),
        _ => None,
    }
}

pub(super) fn response_schema_lookup(
    response_schema_scopes: &[BTreeMap<String, String>],
    name: &str,
) -> Option<String> {
    response_schema_scopes
        .iter()
        .rev()
        .find_map(|scope| scope.get(name).cloned())
}

pub(super) fn dedupe_response_metadata(responses: Vec<ResponseMetadata>) -> Vec<ResponseMetadata> {
    let mut seen = BTreeSet::new();
    let mut deduped = Vec::new();
    for response in responses {
        let key = format!(
            "{}:{}:{}:{}:{}:{}",
            response.helper,
            response.status,
            response.kind,
            response.body_schema.as_deref().unwrap_or(""),
            response.native_body.as_deref().unwrap_or(""),
            response.partial
        );
        if seen.insert(key) {
            deduped.push(response);
        }
    }
    deduped
}

pub(super) fn response_metadata_from_call(call: &CallExpression<'_>) -> Option<ResponseMetadata> {
    let (receiver, helper) = static_member_name(&call.callee)?;
    let (status, mut kind) = match receiver {
        "Auth" => match helper {
            "signIn" => (200, "json"),
            "signOut" => (204, "empty"),
            _ => return None,
        },
        "Results" => match helper {
            "ok" => (200, "json"),
            "json" => (200, "json"),
            "text" => (200, "text"),
            "html" => (200, "html"),
            "bytes" => (200, "bytes"),
            "created" => (201, "json"),
            "accepted" => (202, "json"),
            "noContent" => (204, "empty"),
            "stream" => (200, "stream"),
            "badRequest" => (400, "json"),
            "unauthorized" => (401, "json"),
            "notFound" => (404, "json"),
            "problem" => (problem_result_code(call).unwrap_or(500), "problem"),
            "status" => (status_result_code(call)?, "json"),
            _ => return None,
        },
        _ => return None,
    };
    if receiver == "Results" && helper == "status" && status_result_has_no_body(call) {
        kind = "empty";
    }
    Some(ResponseMetadata {
        helper: helper.to_string(),
        status,
        kind: kind.to_string(),
        body_schema: None,
        native_body: native_response_body_from_call(helper, call),
        source_name: None,
        source_text: None,
        span: Some(call.span),
        partial: false,
    })
}

pub(super) fn native_response_body_from_call(
    helper: &str,
    call: &CallExpression<'_>,
) -> Option<String> {
    match helper {
        "text" => {
            let Argument::StringLiteral(literal) = call.arguments.first()? else {
                return None;
            };
            Some(literal.value.to_string())
        }
        "noContent" => Some(String::new()),
        "status" => {
            if status_result_has_no_body(call) {
                return Some(String::new());
            }
            let value = json_value_from_argument(call.arguments.get(1)?)?;
            serde_json::to_string(&value).ok()
        }
        "problem" => {
            let value = json_value_from_argument(call.arguments.first()?)?;
            serde_json::to_string(&value).ok()
        }
        "json" | "ok" => {
            let value = json_value_from_argument(call.arguments.first()?)?;
            serde_json::to_string(&value).ok()
        }
        _ => None,
    }
}

pub(super) fn json_value_from_argument(argument: &Argument<'_>) -> Option<Value> {
    match argument {
        Argument::StringLiteral(literal) => Some(Value::String(literal.value.to_string())),
        Argument::NumericLiteral(literal) => {
            serde_json::Number::from_f64(literal.value).map(Value::Number)
        }
        Argument::BooleanLiteral(literal) => Some(Value::Bool(literal.value)),
        Argument::NullLiteral(_) => Some(Value::Null),
        Argument::ArrayExpression(array) => {
            let mut values = Vec::with_capacity(array.elements.len());
            for element in &array.elements {
                values.push(json_value_from_array_element(element)?);
            }
            Some(Value::Array(values))
        }
        Argument::ObjectExpression(object) => json_value_from_object(object),
        Argument::ParenthesizedExpression(parenthesized) => {
            json_value_from_expression(&parenthesized.expression)
        }
        _ => None,
    }
}

pub(super) fn json_value_from_expression(expression: &Expression<'_>) -> Option<Value> {
    match expression {
        Expression::StringLiteral(literal) => Some(Value::String(literal.value.to_string())),
        Expression::NumericLiteral(literal) => {
            serde_json::Number::from_f64(literal.value).map(Value::Number)
        }
        Expression::BooleanLiteral(literal) => Some(Value::Bool(literal.value)),
        Expression::NullLiteral(_) => Some(Value::Null),
        Expression::ArrayExpression(array) => {
            let mut values = Vec::with_capacity(array.elements.len());
            for element in &array.elements {
                values.push(json_value_from_array_element(element)?);
            }
            Some(Value::Array(values))
        }
        Expression::ObjectExpression(object) => json_value_from_object(object),
        Expression::ParenthesizedExpression(parenthesized) => {
            json_value_from_expression(&parenthesized.expression)
        }
        _ => None,
    }
}

pub(super) fn json_value_from_array_element(element: &ArrayExpressionElement<'_>) -> Option<Value> {
    match element {
        ArrayExpressionElement::StringLiteral(literal) => {
            Some(Value::String(literal.value.to_string()))
        }
        ArrayExpressionElement::NumericLiteral(literal) => {
            serde_json::Number::from_f64(literal.value).map(Value::Number)
        }
        ArrayExpressionElement::BooleanLiteral(literal) => Some(Value::Bool(literal.value)),
        ArrayExpressionElement::NullLiteral(_) => Some(Value::Null),
        ArrayExpressionElement::ArrayExpression(array) => {
            let mut values = Vec::with_capacity(array.elements.len());
            for element in &array.elements {
                values.push(json_value_from_array_element(element)?);
            }
            Some(Value::Array(values))
        }
        ArrayExpressionElement::ObjectExpression(object) => json_value_from_object(object),
        _ => None,
    }
}

pub(super) fn json_value_from_object(object: &oxc_ast::ast::ObjectExpression<'_>) -> Option<Value> {
    let mut map = serde_json::Map::new();
    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return None;
        };
        if property.kind != PropertyKind::Init
            || property.method
            || property.shorthand
            || property.computed
        {
            return None;
        }
        let key = json_property_key_name(&property.key)?;
        map.insert(key, json_value_from_expression(&property.value)?);
    }
    Some(Value::Object(map))
}

pub(super) fn json_property_key_name(key: &PropertyKey<'_>) -> Option<String> {
    match key {
        PropertyKey::StaticIdentifier(identifier) => Some(identifier.name.to_string()),
        PropertyKey::StringLiteral(literal) => Some(literal.value.to_string()),
        PropertyKey::NumericLiteral(literal) => {
            if literal.value.fract() == 0.0 {
                Some(format!("{}", literal.value as i64))
            } else {
                Some(literal.value.to_string())
            }
        }
        _ => None,
    }
}

pub(super) fn status_result_code(call: &CallExpression<'_>) -> Option<u16> {
    let Argument::NumericLiteral(literal) = call.arguments.first()? else {
        return None;
    };
    let value = literal.value;
    if value.fract() == 0.0 && (100.0..=599.0).contains(&value) {
        Some(value as u16)
    } else {
        None
    }
}

pub(super) fn status_result_has_no_body(call: &CallExpression<'_>) -> bool {
    call.arguments.len() == 1
}

pub(super) fn problem_result_code(call: &CallExpression<'_>) -> Option<u16> {
    if let Some(status) = call
        .arguments
        .get(1)
        .and_then(json_value_from_argument)
        .and_then(|value| value.get("status").and_then(json_http_status_value))
    {
        return Some(status);
    }

    call.arguments
        .first()
        .and_then(json_value_from_argument)
        .and_then(|value| value.get("status").and_then(json_http_status_value))
}

pub(super) fn json_http_status_value(value: &Value) -> Option<u16> {
    let number = value.as_f64()?;
    if number.fract() == 0.0 && (100.0..=599.0).contains(&number) {
        Some(number as u16)
    } else {
        None
    }
}

pub(super) fn request_bindings_from_arrow(
    function: &oxc_ast::ast::ArrowFunctionExpression<'_>,
    schema_names: &BTreeSet<String>,
) -> Vec<RequestBinding> {
    let Some(ctx_name) = handler_context_parameter_name(&function.params) else {
        return Vec::new();
    };
    let mut bindings = Vec::new();
    for statement in &function.body.statements {
        collect_statement_request_bindings(statement, &ctx_name, schema_names, &mut bindings);
    }
    dedupe_request_bindings(bindings)
}

pub(super) fn request_bindings_from_function(
    function: &oxc_ast::ast::Function<'_>,
    schema_names: &BTreeSet<String>,
) -> Vec<RequestBinding> {
    let Some(ctx_name) = handler_context_parameter_name(&function.params) else {
        return Vec::new();
    };
    let mut bindings = Vec::new();
    if let Some(body) = &function.body {
        for statement in &body.statements {
            collect_statement_request_bindings(statement, &ctx_name, schema_names, &mut bindings);
        }
    }
    dedupe_request_bindings(bindings)
}

pub(super) fn dedupe_request_bindings(bindings: Vec<RequestBinding>) -> Vec<RequestBinding> {
    let mut seen = BTreeSet::new();
    let mut deduped = Vec::new();
    for binding in bindings {
        let key = format!(
            "{}:{}:{}",
            binding.kind,
            binding.name.clone().unwrap_or_default(),
            binding.schema.clone().unwrap_or_default()
        );
        if seen.insert(key) {
            deduped.push(binding);
        }
    }
    deduped
}

pub(super) fn collect_statement_request_bindings(
    statement: &Statement<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
    bindings: &mut Vec<RequestBinding>,
) {
    match statement {
        Statement::BlockStatement(block) => {
            for statement in &block.body {
                collect_statement_request_bindings(statement, ctx_name, schema_names, bindings);
            }
        }
        Statement::ReturnStatement(statement) => {
            if let Some(argument) = &statement.argument {
                collect_expression_request_bindings(argument, ctx_name, schema_names, bindings);
            }
        }
        Statement::ExpressionStatement(statement) => {
            collect_expression_request_bindings(
                &statement.expression,
                ctx_name,
                schema_names,
                bindings,
            );
        }
        Statement::VariableDeclaration(declaration) => {
            for declarator in &declaration.declarations {
                if let Some(init) = &declarator.init {
                    collect_expression_request_bindings(init, ctx_name, schema_names, bindings);
                }
            }
        }
        Statement::IfStatement(statement) => {
            collect_expression_request_bindings(&statement.test, ctx_name, schema_names, bindings);
            collect_statement_request_bindings(
                &statement.consequent,
                ctx_name,
                schema_names,
                bindings,
            );
            if let Some(alternate) = &statement.alternate {
                collect_statement_request_bindings(alternate, ctx_name, schema_names, bindings);
            }
        }
        Statement::ThrowStatement(statement) => {
            collect_expression_request_bindings(
                &statement.argument,
                ctx_name,
                schema_names,
                bindings,
            );
        }
        Statement::DoWhileStatement(statement) => {
            collect_statement_request_bindings(&statement.body, ctx_name, schema_names, bindings);
            collect_expression_request_bindings(&statement.test, ctx_name, schema_names, bindings);
        }
        Statement::WhileStatement(statement) => {
            collect_expression_request_bindings(&statement.test, ctx_name, schema_names, bindings);
            collect_statement_request_bindings(&statement.body, ctx_name, schema_names, bindings);
        }
        Statement::ForStatement(statement) => {
            if let Some(init) = &statement.init {
                collect_for_init_request_bindings(init, ctx_name, schema_names, bindings);
            }
            if let Some(test) = &statement.test {
                collect_expression_request_bindings(test, ctx_name, schema_names, bindings);
            }
            if let Some(update) = &statement.update {
                collect_expression_request_bindings(update, ctx_name, schema_names, bindings);
            }
            collect_statement_request_bindings(&statement.body, ctx_name, schema_names, bindings);
        }
        Statement::ForInStatement(statement) => {
            collect_expression_request_bindings(&statement.right, ctx_name, schema_names, bindings);
            collect_statement_request_bindings(&statement.body, ctx_name, schema_names, bindings);
        }
        Statement::ForOfStatement(statement) => {
            collect_expression_request_bindings(&statement.right, ctx_name, schema_names, bindings);
            collect_statement_request_bindings(&statement.body, ctx_name, schema_names, bindings);
        }
        Statement::SwitchStatement(statement) => {
            collect_expression_request_bindings(
                &statement.discriminant,
                ctx_name,
                schema_names,
                bindings,
            );
            for case in &statement.cases {
                if let Some(test) = &case.test {
                    collect_expression_request_bindings(test, ctx_name, schema_names, bindings);
                }
                for statement in &case.consequent {
                    collect_statement_request_bindings(statement, ctx_name, schema_names, bindings);
                }
            }
        }
        Statement::TryStatement(statement) => {
            for statement in &statement.block.body {
                collect_statement_request_bindings(statement, ctx_name, schema_names, bindings);
            }
            if let Some(handler) = &statement.handler {
                for statement in &handler.body.body {
                    collect_statement_request_bindings(statement, ctx_name, schema_names, bindings);
                }
            }
            if let Some(finalizer) = &statement.finalizer {
                for statement in &finalizer.body {
                    collect_statement_request_bindings(statement, ctx_name, schema_names, bindings);
                }
            }
        }
        _ => {}
    }
}

pub(super) fn collect_for_init_request_bindings(
    init: &ForStatementInit<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
    bindings: &mut Vec<RequestBinding>,
) {
    match init {
        ForStatementInit::VariableDeclaration(declaration) => {
            for declarator in &declaration.declarations {
                if let Some(init) = &declarator.init {
                    collect_expression_request_bindings(init, ctx_name, schema_names, bindings);
                }
            }
        }
        _ => {
            if let Some(expression) = init.as_expression() {
                collect_expression_request_bindings(expression, ctx_name, schema_names, bindings);
            }
        }
    }
}

pub(super) fn collect_expression_request_bindings(
    expression: &Expression<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
    bindings: &mut Vec<RequestBinding>,
) {
    if let Some(binding) = request_binding_from_expression(expression, ctx_name) {
        bindings.push(binding);
    }
    match expression {
        Expression::CallExpression(call) => {
            if let Some(binding) = request_binding_from_call(call, ctx_name, schema_names) {
                bindings.push(binding);
            } else {
                collect_expression_request_bindings(&call.callee, ctx_name, schema_names, bindings);
            }
            for argument in &call.arguments {
                collect_argument_request_bindings(argument, ctx_name, schema_names, bindings);
            }
        }
        Expression::AwaitExpression(expression) => {
            collect_expression_request_bindings(
                &expression.argument,
                ctx_name,
                schema_names,
                bindings,
            );
        }
        Expression::ObjectExpression(object) => {
            for property in &object.properties {
                if let ObjectPropertyKind::ObjectProperty(property) = property {
                    collect_expression_request_bindings(
                        &property.value,
                        ctx_name,
                        schema_names,
                        bindings,
                    );
                }
            }
        }
        Expression::ArrayExpression(array) => {
            for element in &array.elements {
                collect_array_element_request_bindings(element, ctx_name, schema_names, bindings);
            }
        }
        Expression::ParenthesizedExpression(parenthesized) => {
            collect_expression_request_bindings(
                &parenthesized.expression,
                ctx_name,
                schema_names,
                bindings,
            );
        }
        Expression::StaticMemberExpression(member)
            if static_member_chain(expression)
                .is_none_or(|chain| chain.first() != Some(&ctx_name)) =>
        {
            collect_expression_request_bindings(&member.object, ctx_name, schema_names, bindings);
        }
        Expression::StaticMemberExpression(_) => {}
        Expression::ComputedMemberExpression(member) => {
            collect_expression_request_bindings(&member.object, ctx_name, schema_names, bindings);
            collect_expression_request_bindings(
                &member.expression,
                ctx_name,
                schema_names,
                bindings,
            );
        }
        Expression::BinaryExpression(expression) => {
            collect_expression_request_bindings(&expression.left, ctx_name, schema_names, bindings);
            collect_expression_request_bindings(
                &expression.right,
                ctx_name,
                schema_names,
                bindings,
            );
        }
        Expression::LogicalExpression(expression) => {
            collect_expression_request_bindings(&expression.left, ctx_name, schema_names, bindings);
            collect_expression_request_bindings(
                &expression.right,
                ctx_name,
                schema_names,
                bindings,
            );
        }
        Expression::ConditionalExpression(expression) => {
            collect_expression_request_bindings(&expression.test, ctx_name, schema_names, bindings);
            collect_expression_request_bindings(
                &expression.consequent,
                ctx_name,
                schema_names,
                bindings,
            );
            collect_expression_request_bindings(
                &expression.alternate,
                ctx_name,
                schema_names,
                bindings,
            );
        }
        _ => {}
    }
}

pub(super) fn collect_argument_request_bindings(
    argument: &Argument<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
    bindings: &mut Vec<RequestBinding>,
) {
    match argument {
        Argument::CallExpression(call) => {
            if let Some(binding) = request_binding_from_call(call, ctx_name, schema_names) {
                bindings.push(binding);
            } else {
                collect_expression_request_bindings(&call.callee, ctx_name, schema_names, bindings);
            }
            for argument in &call.arguments {
                collect_argument_request_bindings(argument, ctx_name, schema_names, bindings);
            }
        }
        Argument::ObjectExpression(object) => {
            for property in &object.properties {
                if let ObjectPropertyKind::ObjectProperty(property) = property {
                    collect_expression_request_bindings(
                        &property.value,
                        ctx_name,
                        schema_names,
                        bindings,
                    );
                }
            }
        }
        Argument::ArrayExpression(array) => {
            for element in &array.elements {
                collect_array_element_request_bindings(element, ctx_name, schema_names, bindings);
            }
        }
        Argument::StaticMemberExpression(_) => {
            if let Some(expression) = argument.as_expression() {
                collect_expression_request_bindings(expression, ctx_name, schema_names, bindings);
            }
        }
        _ => {
            if let Some(expression) = argument.as_expression() {
                collect_expression_request_bindings(expression, ctx_name, schema_names, bindings);
            }
        }
    }
}

pub(super) fn collect_array_element_request_bindings(
    element: &ArrayExpressionElement<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
    bindings: &mut Vec<RequestBinding>,
) {
    match element {
        ArrayExpressionElement::CallExpression(call) => {
            if let Some(binding) = request_binding_from_call(call, ctx_name, schema_names) {
                bindings.push(binding);
            } else {
                collect_expression_request_bindings(&call.callee, ctx_name, schema_names, bindings);
            }
            for argument in &call.arguments {
                collect_argument_request_bindings(argument, ctx_name, schema_names, bindings);
            }
        }
        ArrayExpressionElement::ObjectExpression(object) => {
            for property in &object.properties {
                if let ObjectPropertyKind::ObjectProperty(property) = property {
                    collect_expression_request_bindings(
                        &property.value,
                        ctx_name,
                        schema_names,
                        bindings,
                    );
                }
            }
        }
        ArrayExpressionElement::ArrayExpression(array) => {
            for element in &array.elements {
                collect_array_element_request_bindings(element, ctx_name, schema_names, bindings);
            }
        }
        _ => {
            if let Some(expression) = element.as_expression() {
                collect_expression_request_bindings(expression, ctx_name, schema_names, bindings);
            }
        }
    }
}

pub(super) fn request_binding_from_expression(
    expression: &Expression<'_>,
    ctx_name: &str,
) -> Option<RequestBinding> {
    if matches!(expression, Expression::Identifier(identifier) if identifier.name.as_str() == ctx_name)
    {
        return Some(context_request_binding());
    }

    let chain = static_member_chain(expression)?;
    if chain.len() == 3 && chain[0] == ctx_name && matches!(chain[1], "route" | "query" | "header")
    {
        let name = if chain[1] == "header" {
            header_facade_binding_name(chain[2])
        } else {
            chain[2].to_string()
        };
        return Some(RequestBinding {
            kind: chain[1].to_string(),
            name: Some(name),
            schema: None,
            parameter: None,
            type_name: None,
            source_name: None,
            source_text: None,
            span: None,
            wrapper: None,
            injection_kind: None,
            provider_kind: None,
            capability: None,
            semantic: None,
            redacted: false,
        });
    }
    if chain.len() == 2 && chain[0] == ctx_name && matches!(chain[1], "route" | "query" | "header")
    {
        return Some(context_request_binding());
    }
    if chain.len() == 3
        && chain[0] == ctx_name
        && chain[1] == "request"
        && matches!(
            chain[2],
            "contentLength"
                | "contentType"
                | "id"
                | "method"
                | "path"
                | "protocol"
                | "queryString"
                | "rawTarget"
                | "scheme"
        )
    {
        return Some(request_facade_context_binding());
    }
    if chain.len() >= 2 && chain[0] == ctx_name {
        return Some(context_request_binding());
    }
    None
}

pub(super) fn context_request_binding() -> RequestBinding {
    RequestBinding {
        kind: "context".to_string(),
        name: Some("RequestContext".to_string()),
        schema: None,
        parameter: None,
        type_name: Some("RequestContext".to_string()),
        source_name: None,
        source_text: None,
        span: None,
        wrapper: None,
        injection_kind: None,
        provider_kind: None,
        capability: None,
        semantic: None,
        redacted: false,
    }
}

pub(super) fn request_facade_context_binding() -> RequestBinding {
    RequestBinding {
        kind: "context".to_string(),
        name: Some("request".to_string()),
        schema: None,
        parameter: None,
        type_name: Some("RequestContext".to_string()),
        source_name: None,
        source_text: None,
        span: None,
        wrapper: None,
        injection_kind: None,
        provider_kind: None,
        capability: None,
        semantic: None,
        redacted: false,
    }
}

pub(super) fn header_facade_binding_name(property: &str) -> String {
    let mut output = String::with_capacity(property.len());
    let mut previous_was_lower_or_digit = false;

    for ch in property.chars() {
        if ch.is_ascii_uppercase() {
            if !output.is_empty() && previous_was_lower_or_digit {
                output.push('-');
            }
            output.push(ch.to_ascii_lowercase());
            previous_was_lower_or_digit = false;
        } else {
            output.push(ch.to_ascii_lowercase());
            previous_was_lower_or_digit = ch.is_ascii_lowercase() || ch.is_ascii_digit();
        }
    }
    output
}

pub(super) fn request_binding_from_call(
    call: &CallExpression<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
) -> Option<RequestBinding> {
    let chain = static_member_chain(&call.callee)?;
    if chain.len() == 3 && chain[0] == ctx_name && chain[1] == "request" {
        let kind = match chain[2] {
            "form" if call.arguments.is_empty() => Some("body.form"),
            "multipart" if call.arguments.is_empty() => Some("body.multipart"),
            _ => None,
        };
        if let Some(kind) = kind {
            return Some(RequestBinding {
                kind: kind.to_string(),
                name: None,
                schema: None,
                parameter: None,
                type_name: None,
                source_name: None,
                source_text: None,
                span: None,
                wrapper: None,
                injection_kind: None,
                provider_kind: None,
                capability: None,
                semantic: None,
                redacted: false,
            });
        }
    }
    if chain.len() == 3 && chain[0] == ctx_name && chain[1] == "cookies" && chain[2] == "get" {
        if call.arguments.len() != 1 {
            return None;
        }
        let name = call.arguments.first().and_then(argument_string_literal)?;
        return Some(RequestBinding {
            kind: "cookie".to_string(),
            name: Some(name),
            schema: None,
            parameter: None,
            type_name: None,
            source_name: None,
            source_text: None,
            span: None,
            wrapper: None,
            injection_kind: None,
            provider_kind: None,
            capability: None,
            semantic: None,
            redacted: true,
        });
    }
    if chain.len() >= 2 && chain[0] == ctx_name && chain[1] == "request" {
        return Some(context_request_binding());
    }
    if chain.len() == 3 && chain[0] == ctx_name && chain[1] == "body" {
        let schema = body_binding_schema(call, chain[2], schema_names)?;
        let kind = if chain[2] == "validate" {
            "body.json".to_string()
        } else {
            format!("body.{}", chain[2])
        };
        return Some(RequestBinding {
            kind,
            name: None,
            schema,
            parameter: None,
            type_name: None,
            source_name: None,
            source_text: None,
            span: None,
            wrapper: None,
            injection_kind: None,
            provider_kind: None,
            capability: None,
            semantic: None,
            redacted: false,
        });
    }
    None
}

pub(super) fn body_binding_schema(
    call: &CallExpression<'_>,
    method: &str,
    schema_names: &BTreeSet<String>,
) -> Option<Option<String>> {
    match method {
        "text" if call.arguments.is_empty() => Some(None),
        "json" if call.arguments.is_empty() => Some(None),
        "form" if call.arguments.is_empty() => Some(None),
        "multipart" if call.arguments.is_empty() => Some(None),
        "json" if call.arguments.len() == 1 => {
            let schema = call.arguments.first().and_then(argument_identifier)?;
            if schema == "undefined" {
                Some(None)
            } else if schema_names.contains(schema) {
                Some(Some(schema.to_string()))
            } else {
                None
            }
        }
        "validate" if call.arguments.len() == 1 => {
            let schema = call.arguments.first().and_then(argument_identifier)?;
            if schema == "undefined" {
                Some(None)
            } else if schema_names.contains(schema) {
                Some(Some(schema.to_string()))
            } else {
                None
            }
        }
        _ => None,
    }
}

pub(super) fn body_binding_call_is_supported(
    call: &CallExpression<'_>,
    allowed_roots: &BTreeSet<String>,
    schema_names: &BTreeSet<String>,
) -> bool {
    let Some(chain) = static_member_chain(&call.callee) else {
        return false;
    };
    chain.len() == 3
        && allowed_roots.contains(chain[0])
        && chain[1] == "body"
        && body_binding_schema(call, chain[2], schema_names).is_some()
}

pub(super) fn body_json_schema_argument_spans_arrow(
    function: &oxc_ast::ast::ArrowFunctionExpression<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
) -> Vec<SchemaReferenceEdit> {
    let mut spans = Vec::new();
    for statement in &function.body.statements {
        collect_statement_schema_argument_spans(statement, ctx_name, schema_names, &mut spans);
    }
    spans
}

pub(super) fn body_json_schema_argument_spans_function(
    function: &oxc_ast::ast::Function<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
) -> Vec<SchemaReferenceEdit> {
    let mut spans = Vec::new();
    if let Some(body) = &function.body {
        for statement in &body.statements {
            collect_statement_schema_argument_spans(statement, ctx_name, schema_names, &mut spans);
        }
    }
    spans
}

pub(super) fn collect_statement_schema_argument_spans(
    statement: &Statement<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
    spans: &mut Vec<SchemaReferenceEdit>,
) {
    match statement {
        Statement::BlockStatement(block) => {
            for statement in &block.body {
                collect_statement_schema_argument_spans(statement, ctx_name, schema_names, spans);
            }
        }
        Statement::ReturnStatement(statement) => {
            if let Some(argument) = &statement.argument {
                collect_expression_schema_argument_spans(argument, ctx_name, schema_names, spans);
            }
        }
        Statement::ExpressionStatement(statement) => {
            collect_expression_schema_argument_spans(
                &statement.expression,
                ctx_name,
                schema_names,
                spans,
            );
        }
        Statement::VariableDeclaration(declaration) => {
            for declarator in &declaration.declarations {
                if let Some(init) = &declarator.init {
                    collect_expression_schema_argument_spans(init, ctx_name, schema_names, spans);
                }
            }
        }
        Statement::IfStatement(statement) => {
            collect_expression_schema_argument_spans(
                &statement.test,
                ctx_name,
                schema_names,
                spans,
            );
            collect_statement_schema_argument_spans(
                &statement.consequent,
                ctx_name,
                schema_names,
                spans,
            );
            if let Some(alternate) = &statement.alternate {
                collect_statement_schema_argument_spans(alternate, ctx_name, schema_names, spans);
            }
        }
        Statement::ThrowStatement(statement) => {
            collect_expression_schema_argument_spans(
                &statement.argument,
                ctx_name,
                schema_names,
                spans,
            );
        }
        Statement::DoWhileStatement(statement) => {
            collect_statement_schema_argument_spans(&statement.body, ctx_name, schema_names, spans);
            collect_expression_schema_argument_spans(
                &statement.test,
                ctx_name,
                schema_names,
                spans,
            );
        }
        Statement::WhileStatement(statement) => {
            collect_expression_schema_argument_spans(
                &statement.test,
                ctx_name,
                schema_names,
                spans,
            );
            collect_statement_schema_argument_spans(&statement.body, ctx_name, schema_names, spans);
        }
        Statement::ForStatement(statement) => {
            if let Some(init) = &statement.init {
                collect_for_init_schema_argument_spans(init, ctx_name, schema_names, spans);
            }
            if let Some(test) = &statement.test {
                collect_expression_schema_argument_spans(test, ctx_name, schema_names, spans);
            }
            if let Some(update) = &statement.update {
                collect_expression_schema_argument_spans(update, ctx_name, schema_names, spans);
            }
            collect_statement_schema_argument_spans(&statement.body, ctx_name, schema_names, spans);
        }
        Statement::ForInStatement(statement) => {
            collect_expression_schema_argument_spans(
                &statement.right,
                ctx_name,
                schema_names,
                spans,
            );
            collect_statement_schema_argument_spans(&statement.body, ctx_name, schema_names, spans);
        }
        Statement::ForOfStatement(statement) => {
            collect_expression_schema_argument_spans(
                &statement.right,
                ctx_name,
                schema_names,
                spans,
            );
            collect_statement_schema_argument_spans(&statement.body, ctx_name, schema_names, spans);
        }
        Statement::SwitchStatement(statement) => {
            collect_expression_schema_argument_spans(
                &statement.discriminant,
                ctx_name,
                schema_names,
                spans,
            );
            for case in &statement.cases {
                if let Some(test) = &case.test {
                    collect_expression_schema_argument_spans(test, ctx_name, schema_names, spans);
                }
                for statement in &case.consequent {
                    collect_statement_schema_argument_spans(
                        statement,
                        ctx_name,
                        schema_names,
                        spans,
                    );
                }
            }
        }
        Statement::TryStatement(statement) => {
            for statement in &statement.block.body {
                collect_statement_schema_argument_spans(statement, ctx_name, schema_names, spans);
            }
            if let Some(handler) = &statement.handler {
                for statement in &handler.body.body {
                    collect_statement_schema_argument_spans(
                        statement,
                        ctx_name,
                        schema_names,
                        spans,
                    );
                }
            }
            if let Some(finalizer) = &statement.finalizer {
                for statement in &finalizer.body {
                    collect_statement_schema_argument_spans(
                        statement,
                        ctx_name,
                        schema_names,
                        spans,
                    );
                }
            }
        }
        _ => {}
    }
}

pub(super) fn collect_for_init_schema_argument_spans(
    init: &ForStatementInit<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
    spans: &mut Vec<SchemaReferenceEdit>,
) {
    match init {
        ForStatementInit::VariableDeclaration(declaration) => {
            for declarator in &declaration.declarations {
                if let Some(init) = &declarator.init {
                    collect_expression_schema_argument_spans(init, ctx_name, schema_names, spans);
                }
            }
        }
        _ => {
            if let Some(expression) = init.as_expression() {
                collect_expression_schema_argument_spans(expression, ctx_name, schema_names, spans);
            }
        }
    }
}

pub(super) fn collect_expression_schema_argument_spans(
    expression: &Expression<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
    spans: &mut Vec<SchemaReferenceEdit>,
) {
    match expression {
        Expression::CallExpression(call) => {
            if let Some(edit) = body_json_schema_reference_edit(call, ctx_name, schema_names) {
                spans.push(edit);
            } else {
                collect_expression_schema_argument_spans(
                    &call.callee,
                    ctx_name,
                    schema_names,
                    spans,
                );
            }
            for argument in &call.arguments {
                collect_argument_schema_argument_spans(argument, ctx_name, schema_names, spans);
            }
        }
        Expression::AwaitExpression(expression) => {
            collect_expression_schema_argument_spans(
                &expression.argument,
                ctx_name,
                schema_names,
                spans,
            );
        }
        Expression::ObjectExpression(object) => {
            for property in &object.properties {
                if let ObjectPropertyKind::ObjectProperty(property) = property {
                    collect_expression_schema_argument_spans(
                        &property.value,
                        ctx_name,
                        schema_names,
                        spans,
                    );
                }
            }
        }
        Expression::ArrayExpression(array) => {
            for element in &array.elements {
                collect_array_element_schema_argument_spans(element, ctx_name, schema_names, spans);
            }
        }
        Expression::ParenthesizedExpression(parenthesized) => {
            collect_expression_schema_argument_spans(
                &parenthesized.expression,
                ctx_name,
                schema_names,
                spans,
            );
        }
        Expression::StaticMemberExpression(member)
            if static_member_chain(expression)
                .is_none_or(|chain| chain.first() != Some(&ctx_name)) =>
        {
            collect_expression_schema_argument_spans(&member.object, ctx_name, schema_names, spans);
        }
        Expression::StaticMemberExpression(_) => {}
        Expression::ComputedMemberExpression(member) => {
            collect_expression_schema_argument_spans(&member.object, ctx_name, schema_names, spans);
            collect_expression_schema_argument_spans(
                &member.expression,
                ctx_name,
                schema_names,
                spans,
            );
        }
        Expression::BinaryExpression(expression) => {
            collect_expression_schema_argument_spans(
                &expression.left,
                ctx_name,
                schema_names,
                spans,
            );
            collect_expression_schema_argument_spans(
                &expression.right,
                ctx_name,
                schema_names,
                spans,
            );
        }
        Expression::LogicalExpression(expression) => {
            collect_expression_schema_argument_spans(
                &expression.left,
                ctx_name,
                schema_names,
                spans,
            );
            collect_expression_schema_argument_spans(
                &expression.right,
                ctx_name,
                schema_names,
                spans,
            );
        }
        Expression::ConditionalExpression(expression) => {
            collect_expression_schema_argument_spans(
                &expression.test,
                ctx_name,
                schema_names,
                spans,
            );
            collect_expression_schema_argument_spans(
                &expression.consequent,
                ctx_name,
                schema_names,
                spans,
            );
            collect_expression_schema_argument_spans(
                &expression.alternate,
                ctx_name,
                schema_names,
                spans,
            );
        }
        Expression::SequenceExpression(expression) => {
            for expression in &expression.expressions {
                collect_expression_schema_argument_spans(expression, ctx_name, schema_names, spans);
            }
        }
        Expression::TaggedTemplateExpression(expression) => {
            collect_expression_schema_argument_spans(
                &expression.tag,
                ctx_name,
                schema_names,
                spans,
            );
        }
        Expression::UnaryExpression(expression) => {
            collect_expression_schema_argument_spans(
                &expression.argument,
                ctx_name,
                schema_names,
                spans,
            );
        }
        Expression::YieldExpression(expression) => {
            if let Some(argument) = &expression.argument {
                collect_expression_schema_argument_spans(argument, ctx_name, schema_names, spans);
            }
        }
        Expression::AssignmentExpression(expression) => {
            collect_expression_schema_argument_spans(
                &expression.right,
                ctx_name,
                schema_names,
                spans,
            );
        }
        Expression::TSAsExpression(expression) => {
            collect_expression_schema_argument_spans(
                &expression.expression,
                ctx_name,
                schema_names,
                spans,
            );
        }
        Expression::TSSatisfiesExpression(expression) => {
            collect_expression_schema_argument_spans(
                &expression.expression,
                ctx_name,
                schema_names,
                spans,
            );
        }
        Expression::TSNonNullExpression(expression) => {
            collect_expression_schema_argument_spans(
                &expression.expression,
                ctx_name,
                schema_names,
                spans,
            );
        }
        Expression::TSInstantiationExpression(expression) => {
            collect_expression_schema_argument_spans(
                &expression.expression,
                ctx_name,
                schema_names,
                spans,
            );
        }
        _ => {}
    }
}

pub(super) fn collect_argument_schema_argument_spans(
    argument: &Argument<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
    spans: &mut Vec<SchemaReferenceEdit>,
) {
    match argument {
        Argument::CallExpression(call) => {
            if let Some(edit) = body_json_schema_reference_edit(call, ctx_name, schema_names) {
                spans.push(edit);
            } else {
                collect_expression_schema_argument_spans(
                    &call.callee,
                    ctx_name,
                    schema_names,
                    spans,
                );
            }
            for argument in &call.arguments {
                collect_argument_schema_argument_spans(argument, ctx_name, schema_names, spans);
            }
        }
        Argument::ObjectExpression(object) => {
            for property in &object.properties {
                if let ObjectPropertyKind::ObjectProperty(property) = property {
                    collect_expression_schema_argument_spans(
                        &property.value,
                        ctx_name,
                        schema_names,
                        spans,
                    );
                }
            }
        }
        Argument::ArrayExpression(array) => {
            for element in &array.elements {
                collect_array_element_schema_argument_spans(element, ctx_name, schema_names, spans);
            }
        }
        Argument::StaticMemberExpression(_) => {
            if let Some(expression) = argument.as_expression() {
                collect_expression_schema_argument_spans(expression, ctx_name, schema_names, spans);
            }
        }
        _ => {
            if let Some(expression) = argument.as_expression() {
                collect_expression_schema_argument_spans(expression, ctx_name, schema_names, spans);
            }
        }
    }
}

pub(super) fn collect_array_element_schema_argument_spans(
    element: &ArrayExpressionElement<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
    spans: &mut Vec<SchemaReferenceEdit>,
) {
    match element {
        ArrayExpressionElement::CallExpression(call) => {
            if let Some(edit) = body_json_schema_reference_edit(call, ctx_name, schema_names) {
                spans.push(edit);
            } else {
                collect_expression_schema_argument_spans(
                    &call.callee,
                    ctx_name,
                    schema_names,
                    spans,
                );
            }
            for argument in &call.arguments {
                collect_argument_schema_argument_spans(argument, ctx_name, schema_names, spans);
            }
        }
        ArrayExpressionElement::ObjectExpression(object) => {
            for property in &object.properties {
                if let ObjectPropertyKind::ObjectProperty(property) = property {
                    collect_expression_schema_argument_spans(
                        &property.value,
                        ctx_name,
                        schema_names,
                        spans,
                    );
                }
            }
        }
        ArrayExpressionElement::ArrayExpression(array) => {
            for element in &array.elements {
                collect_array_element_schema_argument_spans(element, ctx_name, schema_names, spans);
            }
        }
        _ => {
            if let Some(expression) = element.as_expression() {
                collect_expression_schema_argument_spans(expression, ctx_name, schema_names, spans);
            }
        }
    }
}

pub(super) fn body_json_schema_reference_edit(
    call: &CallExpression<'_>,
    ctx_name: &str,
    schema_names: &BTreeSet<String>,
) -> Option<SchemaReferenceEdit> {
    let chain = static_member_chain(&call.callee)?;
    if chain.len() == 3
        && chain[0] == ctx_name
        && chain[1] == "body"
        && matches!(chain[2], "json" | "validate")
    {
        if call.arguments.len() != 1 {
            return None;
        }
        let Argument::Identifier(identifier) = call.arguments.first()? else {
            return None;
        };
        let schema = identifier.name.as_str();
        if schema != "undefined" && !schema_names.contains(schema) {
            return None;
        }
        return Some(SchemaReferenceEdit {
            argument_span: identifier.span,
        });
    }
    None
}

pub(super) fn sanitize_handler_schema_references(
    mut source: String,
    handler_start: u32,
    edits: &[SchemaReferenceEdit],
) -> String {
    let mut replacements = Vec::new();
    for edit in edits {
        replacements.push((edit.argument_span, "undefined"));
    }
    replacements.sort_by_key(|(span, _)| std::cmp::Reverse(span.start));
    for (span, replacement) in replacements {
        let Some(start) = span.start.checked_sub(handler_start) else {
            continue;
        };
        let Some(end) = span.end.checked_sub(handler_start) else {
            continue;
        };
        let Ok(start) = usize::try_from(start) else {
            continue;
        };
        let Ok(end) = usize::try_from(end) else {
            continue;
        };
        if start <= end && end <= source.len() {
            source.replace_range(start..end, replacement);
        }
    }
    source
}

pub(super) fn argument_identifier<'a>(argument: &'a Argument<'a>) -> Option<&'a str> {
    match argument {
        Argument::Identifier(identifier) => Some(identifier.name.as_str()),
        _ => None,
    }
}

pub(super) fn argument_string_literal(argument: &Argument<'_>) -> Option<String> {
    match argument {
        Argument::StringLiteral(value) => Some(value.value.as_str().to_string()),
        _ => None,
    }
}

pub(super) fn argument_span(argument: &Argument<'_>) -> Option<Span> {
    match argument {
        Argument::SpreadElement(node) => Some(node.span),
        Argument::BooleanLiteral(node) => Some(node.span),
        Argument::NullLiteral(node) => Some(node.span),
        Argument::NumericLiteral(node) => Some(node.span),
        Argument::BigIntLiteral(node) => Some(node.span),
        Argument::RegExpLiteral(node) => Some(node.span),
        Argument::StringLiteral(node) => Some(node.span),
        Argument::TemplateLiteral(node) => Some(node.span),
        Argument::Identifier(node) => Some(node.span),
        Argument::MetaProperty(node) => Some(node.span),
        Argument::Super(node) => Some(node.span),
        Argument::ArrayExpression(node) => Some(node.span),
        Argument::ArrowFunctionExpression(node) => Some(node.span),
        Argument::AssignmentExpression(node) => Some(node.span),
        Argument::AwaitExpression(node) => Some(node.span),
        Argument::BinaryExpression(node) => Some(node.span),
        Argument::CallExpression(node) => Some(node.span),
        Argument::ChainExpression(node) => Some(node.span),
        Argument::ClassExpression(node) => Some(node.span),
        Argument::ConditionalExpression(node) => Some(node.span),
        Argument::FunctionExpression(node) => Some(node.span),
        Argument::ImportExpression(node) => Some(node.span),
        Argument::LogicalExpression(node) => Some(node.span),
        Argument::NewExpression(node) => Some(node.span),
        Argument::ObjectExpression(node) => Some(node.span),
        Argument::ParenthesizedExpression(node) => Some(node.span),
        Argument::SequenceExpression(node) => Some(node.span),
        Argument::TaggedTemplateExpression(node) => Some(node.span),
        Argument::ThisExpression(node) => Some(node.span),
        Argument::UnaryExpression(node) => Some(node.span),
        Argument::UpdateExpression(node) => Some(node.span),
        Argument::YieldExpression(node) => Some(node.span),
        Argument::PrivateInExpression(node) => Some(node.span),
        Argument::JSXElement(node) => Some(node.span),
        Argument::JSXFragment(node) => Some(node.span),
        Argument::TSAsExpression(node) => Some(node.span),
        Argument::TSSatisfiesExpression(node) => Some(node.span),
        Argument::TSTypeAssertion(node) => Some(node.span),
        Argument::TSNonNullExpression(node) => Some(node.span),
        Argument::TSInstantiationExpression(node) => Some(node.span),
        Argument::V8IntrinsicExpression(node) => Some(node.span),
        Argument::ComputedMemberExpression(node) => Some(node.span),
        Argument::StaticMemberExpression(node) => Some(node.span),
        Argument::PrivateFieldExpression(node) => Some(node.span),
    }
}

pub(super) fn handler_body_is_supported_arrow(
    function: &oxc_ast::ast::ArrowFunctionExpression<'_>,
    schema_names: &BTreeSet<String>,
) -> bool {
    let roots = function_parameter_roots(&function.params);
    if function.expression {
        return function.body.statements.len() == 1
            && function.body.statements.first().is_some_and(|statement| {
                expression_statement_is_supported_result(statement, &roots, schema_names)
            });
    }

    function.body.statements.len() == 1
        && function.body.statements.first().is_some_and(|statement| {
            return_statement_returns_supported_result(statement, &roots, schema_names)
                || statement_is_supported_throw(statement)
        })
}

pub(super) fn handler_body_is_supported_function(
    function: &oxc_ast::ast::Function<'_>,
    schema_names: &BTreeSet<String>,
) -> bool {
    let roots = function_parameter_roots(&function.params);
    if function.generator || function.body.is_none() {
        return false;
    }
    let Some(body) = &function.body else {
        return false;
    };
    body.statements.len() == 1
        && body.statements.first().is_some_and(|statement| {
            return_statement_returns_supported_result(statement, &roots, schema_names)
                || statement_is_supported_throw(statement)
        })
}

pub(super) fn return_statement_returns_supported_result(
    statement: &Statement<'_>,
    allowed_roots: &BTreeSet<String>,
    schema_names: &BTreeSet<String>,
) -> bool {
    if return_statement_result_call(statement)
        .is_some_and(|call| results_call_arguments_are_supported(call, allowed_roots, schema_names))
    {
        return true;
    }

    return_statement_expression(statement).is_some_and(|expression| {
        expression_is_supported_handler_return_value(expression, allowed_roots, schema_names)
    })
}

pub(super) fn expression_statement_is_supported_result(
    statement: &Statement<'_>,
    allowed_roots: &BTreeSet<String>,
    schema_names: &BTreeSet<String>,
) -> bool {
    if expression_statement_result_call(statement)
        .is_some_and(|call| results_call_arguments_are_supported(call, allowed_roots, schema_names))
    {
        return true;
    }

    expression_statement_expression(statement).is_some_and(|expression| {
        expression_is_supported_handler_return_value(expression, allowed_roots, schema_names)
    })
}

pub(super) fn statement_is_supported_throw(statement: &Statement<'_>) -> bool {
    matches!(statement, Statement::ThrowStatement(_))
}

pub(super) fn return_statement_expression<'a>(
    statement: &'a Statement<'a>,
) -> Option<&'a Expression<'a>> {
    let Statement::ReturnStatement(return_statement) = statement else {
        return None;
    };
    return_statement.argument.as_ref()
}

pub(super) fn expression_statement_expression<'a>(
    statement: &'a Statement<'a>,
) -> Option<&'a Expression<'a>> {
    let Statement::ExpressionStatement(expression_statement) = statement else {
        return None;
    };
    Some(&expression_statement.expression)
}

pub(super) fn return_statement_result_call<'a>(
    statement: &'a Statement<'a>,
) -> Option<&'a CallExpression<'a>> {
    let Statement::ReturnStatement(return_statement) = statement else {
        return None;
    };
    let argument = return_statement.argument.as_ref()?;
    result_call(argument)
}

pub(super) fn expression_statement_result_call<'a>(
    statement: &'a Statement<'a>,
) -> Option<&'a CallExpression<'a>> {
    let Statement::ExpressionStatement(expression_statement) = statement else {
        return None;
    };
    result_call(&expression_statement.expression)
}

pub(super) fn result_call<'a>(expression: &'a Expression<'a>) -> Option<&'a CallExpression<'a>> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    if static_member_name(&call.callee).is_some_and(|(object, property)| {
        (object == "Results" && results_helper_is_supported(property))
            || (object == "Auth" && auth_result_helper_is_supported(property))
    }) {
        Some(call)
    } else {
        None
    }
}

pub(super) fn auth_result_helper_is_supported(property: &str) -> bool {
    matches!(property, "signIn" | "signOut")
}

pub(super) fn results_helper_is_supported(property: &str) -> bool {
    matches!(
        property,
        "text"
            | "html"
            | "bytes"
            | "json"
            | "ok"
            | "created"
            | "accepted"
            | "noContent"
            | "notFound"
            | "badRequest"
            | "unauthorized"
            | "status"
            | "problem"
            | "stream"
    )
}

pub(super) fn results_call_arguments_are_supported(
    call: &CallExpression<'_>,
    allowed_roots: &BTreeSet<String>,
    schema_names: &BTreeSet<String>,
) -> bool {
    let Some((object, property)) = static_member_name(&call.callee) else {
        return false;
    };
    if object == "Auth" {
        return auth_result_call_arguments_are_supported(
            call,
            property,
            allowed_roots,
            schema_names,
        );
    }
    if object != "Results" {
        return false;
    }

    let argument_count_supported = match property {
        "text" | "html" | "bytes" => matches!(call.arguments.len(), 1 | 2),
        "json" | "ok" | "accepted" | "notFound" | "badRequest" | "unauthorized" => {
            call.arguments.len() <= 2
        }
        "stream" => matches!(call.arguments.len(), 1 | 2),
        "created" | "status" => (1..=3).contains(&call.arguments.len()),
        "noContent" => call.arguments.is_empty(),
        "problem" => call.arguments.len() <= 2,
        _ => false,
    };

    if property == "bytes" {
        return matches!(call.arguments.len(), 1 | 2)
            && call
                .arguments
                .first()
                .is_some_and(argument_is_inline_bytes_value)
            && call.arguments.get(1).is_none_or(|argument| {
                argument_is_inline_json_safe_value(argument, allowed_roots, schema_names)
            });
    }

    if property == "stream" {
        return matches!(call.arguments.len(), 1 | 2)
            && call.arguments.first().is_some_and(|argument| {
                argument_is_stream_writer_callback(argument, allowed_roots)
            })
            && call.arguments.get(1).is_none_or(|argument| {
                argument_is_inline_json_safe_value(argument, allowed_roots, schema_names)
            });
    }

    argument_count_supported
        && call.arguments.iter().all(|argument| {
            argument_is_inline_json_safe_value(argument, allowed_roots, schema_names)
        })
}

pub(super) fn auth_result_call_arguments_are_supported(
    call: &CallExpression<'_>,
    property: &str,
    allowed_roots: &BTreeSet<String>,
    schema_names: &BTreeSet<String>,
) -> bool {
    match property {
        "signIn" => {
            call.arguments.len() == 2
                && call.arguments.first().is_some_and(|argument| {
                    argument_is_allowed_root_identifier(argument, allowed_roots)
                })
                && call.arguments.get(1).is_some_and(|argument| {
                    argument_is_inline_json_safe_value(argument, allowed_roots, schema_names)
                })
        }
        "signOut" => {
            call.arguments.len() == 1
                && call.arguments.first().is_some_and(|argument| {
                    argument_is_allowed_root_identifier(argument, allowed_roots)
                })
        }
        _ => false,
    }
}

pub(super) fn argument_is_allowed_root_identifier(
    argument: &Argument<'_>,
    allowed_roots: &BTreeSet<String>,
) -> bool {
    match argument {
        Argument::Identifier(identifier) => allowed_roots.contains(identifier.name.as_str()),
        Argument::ParenthesizedExpression(parenthesized) => {
            expression_is_allowed_root_identifier(&parenthesized.expression, allowed_roots)
        }
        _ => false,
    }
}

pub(super) fn expression_is_allowed_root_identifier(
    expression: &Expression<'_>,
    allowed_roots: &BTreeSet<String>,
) -> bool {
    match expression {
        Expression::Identifier(identifier) => allowed_roots.contains(identifier.name.as_str()),
        Expression::ParenthesizedExpression(parenthesized) => {
            expression_is_allowed_root_identifier(&parenthesized.expression, allowed_roots)
        }
        _ => false,
    }
}

pub(super) fn argument_is_stream_writer_callback(
    argument: &Argument<'_>,
    allowed_roots: &BTreeSet<String>,
) -> bool {
    let mut free_identifiers = service_factory_free_identifiers(argument);
    free_identifiers.remove("Results");
    free_identifiers.remove("Promise");
    free_identifiers.remove("Uint8Array");
    free_identifiers.retain(|identifier| !allowed_roots.contains(identifier));
    matches!(
        argument,
        Argument::ArrowFunctionExpression(_) | Argument::FunctionExpression(_)
    ) && free_identifiers.is_empty()
}

pub(super) fn argument_is_inline_bytes_value(argument: &Argument<'_>) -> bool {
    match argument {
        Argument::NewExpression(expression) => {
            matches!(&expression.callee, Expression::Identifier(identifier) if identifier.name == "Uint8Array")
                && expression.arguments.len() == 1
                && expression
                    .arguments
                    .first()
                    .is_some_and(argument_is_inline_byte_array)
        }
        Argument::ParenthesizedExpression(parenthesized) => {
            expression_is_inline_bytes_value(&parenthesized.expression)
        }
        _ => false,
    }
}

pub(super) fn expression_is_inline_bytes_value(expression: &Expression<'_>) -> bool {
    match expression {
        Expression::NewExpression(expression) => {
            matches!(&expression.callee, Expression::Identifier(identifier) if identifier.name == "Uint8Array")
                && expression.arguments.len() == 1
                && expression
                    .arguments
                    .first()
                    .is_some_and(argument_is_inline_byte_array)
        }
        Expression::ParenthesizedExpression(parenthesized) => {
            expression_is_inline_bytes_value(&parenthesized.expression)
        }
        _ => false,
    }
}

pub(super) fn argument_is_inline_byte_array(argument: &Argument<'_>) -> bool {
    match argument {
        Argument::ArrayExpression(array) => {
            array.elements.iter().all(array_element_is_byte_literal)
        }
        Argument::ParenthesizedExpression(parenthesized) => {
            expression_is_inline_byte_array(&parenthesized.expression)
        }
        _ => false,
    }
}

pub(super) fn expression_is_inline_byte_array(expression: &Expression<'_>) -> bool {
    match expression {
        Expression::ArrayExpression(array) => {
            array.elements.iter().all(array_element_is_byte_literal)
        }
        Expression::ParenthesizedExpression(parenthesized) => {
            expression_is_inline_byte_array(&parenthesized.expression)
        }
        _ => false,
    }
}

pub(super) fn array_element_is_byte_literal(element: &ArrayExpressionElement<'_>) -> bool {
    let ArrayExpressionElement::NumericLiteral(literal) = element else {
        return false;
    };
    literal.value.is_finite()
        && literal.value.fract() == 0.0
        && literal.value >= 0.0
        && literal.value <= 255.0
}

pub(super) fn argument_is_inline_json_safe_value(
    argument: &Argument<'_>,
    allowed_roots: &BTreeSet<String>,
    schema_names: &BTreeSet<String>,
) -> bool {
    match argument {
        Argument::StringLiteral(_)
        | Argument::NumericLiteral(_)
        | Argument::BooleanLiteral(_)
        | Argument::NullLiteral(_) => true,
        Argument::TemplateLiteral(template) => template.expressions.iter().all(|expression| {
            expression_is_inline_json_safe_value(expression, allowed_roots, schema_names)
        }),
        Argument::ArrayExpression(array) => array.elements.iter().all(|element| {
            array_element_is_inline_json_safe_value(element, allowed_roots, schema_names)
        }),
        Argument::ObjectExpression(object) => {
            object.properties.iter().all(|property| match property {
                ObjectPropertyKind::ObjectProperty(property) => {
                    property.kind == PropertyKind::Init
                        && !property.method
                        && !property.shorthand
                        && !property.computed
                        && property_key_is_inline_json_safe(&property.key)
                        && expression_is_inline_json_safe_value(
                            &property.value,
                            allowed_roots,
                            schema_names,
                        )
                }
                ObjectPropertyKind::SpreadProperty(_) => false,
            })
        }
        Argument::StaticMemberExpression(member) => {
            static_member_root_name(&member.object).is_some_and(|root| allowed_roots.contains(root))
        }
        Argument::CallExpression(call) => {
            body_binding_call_is_supported(call, allowed_roots, schema_names)
        }
        Argument::AwaitExpression(await_expression) => expression_is_inline_json_safe_value(
            &await_expression.argument,
            allowed_roots,
            schema_names,
        ),
        Argument::ParenthesizedExpression(parenthesized) => expression_is_inline_json_safe_value(
            &parenthesized.expression,
            allowed_roots,
            schema_names,
        ),
        _ => false,
    }
}

pub(super) fn array_element_is_inline_json_safe_value(
    element: &ArrayExpressionElement<'_>,
    allowed_roots: &BTreeSet<String>,
    schema_names: &BTreeSet<String>,
) -> bool {
    match element {
        ArrayExpressionElement::StringLiteral(_)
        | ArrayExpressionElement::NumericLiteral(_)
        | ArrayExpressionElement::BooleanLiteral(_)
        | ArrayExpressionElement::NullLiteral(_) => true,
        ArrayExpressionElement::ArrayExpression(array) => array.elements.iter().all(|element| {
            array_element_is_inline_json_safe_value(element, allowed_roots, schema_names)
        }),
        ArrayExpressionElement::ObjectExpression(object) => {
            object.properties.iter().all(|property| match property {
                ObjectPropertyKind::ObjectProperty(property) => {
                    property.kind == PropertyKind::Init
                        && !property.method
                        && !property.shorthand
                        && !property.computed
                        && property_key_is_inline_json_safe(&property.key)
                        && expression_is_inline_json_safe_value(
                            &property.value,
                            allowed_roots,
                            schema_names,
                        )
                }
                ObjectPropertyKind::SpreadProperty(_) => false,
            })
        }
        _ => false,
    }
}

pub(super) fn expression_is_inline_json_safe_value(
    expression: &Expression<'_>,
    allowed_roots: &BTreeSet<String>,
    schema_names: &BTreeSet<String>,
) -> bool {
    match expression {
        Expression::StringLiteral(_)
        | Expression::NumericLiteral(_)
        | Expression::BooleanLiteral(_)
        | Expression::NullLiteral(_) => true,
        Expression::TemplateLiteral(template) => template.expressions.iter().all(|expression| {
            expression_is_inline_json_safe_value(expression, allowed_roots, schema_names)
        }),
        Expression::ArrayExpression(array) => array.elements.iter().all(|element| {
            array_element_is_inline_json_safe_value(element, allowed_roots, schema_names)
        }),
        Expression::ObjectExpression(object) => {
            object.properties.iter().all(|property| match property {
                ObjectPropertyKind::ObjectProperty(property) => {
                    property.kind == PropertyKind::Init
                        && !property.method
                        && !property.shorthand
                        && !property.computed
                        && property_key_is_inline_json_safe(&property.key)
                        && expression_is_inline_json_safe_value(
                            &property.value,
                            allowed_roots,
                            schema_names,
                        )
                }
                ObjectPropertyKind::SpreadProperty(_) => false,
            })
        }
        Expression::ParenthesizedExpression(parenthesized) => expression_is_inline_json_safe_value(
            &parenthesized.expression,
            allowed_roots,
            schema_names,
        ),
        Expression::CallExpression(call) => {
            body_binding_call_is_supported(call, allowed_roots, schema_names)
        }
        Expression::AwaitExpression(await_expression) => expression_is_inline_json_safe_value(
            &await_expression.argument,
            allowed_roots,
            schema_names,
        ),
        Expression::StaticMemberExpression(member) => {
            static_member_root_name(&member.object).is_some_and(|root| allowed_roots.contains(root))
        }
        _ => false,
    }
}

pub(super) fn expression_is_supported_handler_return_value(
    expression: &Expression<'_>,
    allowed_roots: &BTreeSet<String>,
    schema_names: &BTreeSet<String>,
) -> bool {
    expression_is_inline_json_safe_value(expression, allowed_roots, schema_names)
        || expression_is_undefined_identifier(expression)
}

pub(super) fn expression_is_undefined_identifier(expression: &Expression<'_>) -> bool {
    match expression {
        Expression::Identifier(identifier) => identifier.name == "undefined",
        Expression::ParenthesizedExpression(parenthesized) => {
            expression_is_undefined_identifier(&parenthesized.expression)
        }
        _ => false,
    }
}

pub(super) fn function_parameter_roots(
    parameters: &oxc_ast::ast::FormalParameters<'_>,
) -> BTreeSet<String> {
    let mut roots = BTreeSet::new();
    for parameter in &parameters.items {
        collect_binding_roots(&parameter.pattern, &mut roots);
    }
    roots
}

pub(super) fn collect_binding_roots(binding: &BindingPattern<'_>, roots: &mut BTreeSet<String>) {
    match binding {
        BindingPattern::BindingIdentifier(identifier) => {
            roots.insert(identifier.name.as_str().to_string());
        }
        BindingPattern::ObjectPattern(pattern) => {
            for property in &pattern.properties {
                collect_binding_roots(&property.value, roots);
            }
            if let Some(rest) = &pattern.rest {
                collect_binding_roots(&rest.argument, roots);
            }
        }
        BindingPattern::ArrayPattern(pattern) => {
            for element in pattern.elements.iter().flatten() {
                collect_binding_roots(element, roots);
            }
            if let Some(rest) = &pattern.rest {
                collect_binding_roots(&rest.argument, roots);
            }
        }
        BindingPattern::AssignmentPattern(pattern) => {
            collect_binding_roots(&pattern.left, roots);
        }
    }
}

pub(super) fn static_member_root_name<'a>(expression: &'a Expression<'a>) -> Option<&'a str> {
    match expression {
        Expression::Identifier(identifier) => Some(identifier.name.as_str()),
        Expression::StaticMemberExpression(member) => static_member_root_name(&member.object),
        Expression::ParenthesizedExpression(parenthesized) => {
            static_member_root_name(&parenthesized.expression)
        }
        _ => None,
    }
}

pub(super) fn property_key_is_inline_json_safe(key: &PropertyKey<'_>) -> bool {
    matches!(
        key,
        PropertyKey::StaticIdentifier(_)
            | PropertyKey::StringLiteral(_)
            | PropertyKey::NumericLiteral(_)
    )
}

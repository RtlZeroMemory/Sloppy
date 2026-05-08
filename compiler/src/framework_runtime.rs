use oxc_ast::ast::{
    Argument, ArrayExpressionElement, Expression, ForStatementInit, ObjectPropertyKind, Statement,
};
use oxc_span::Span;
use serde_json::{json, Value};

use crate::sloppyc::{ts_type_span, RequestBinding};

pub(crate) fn typed_arrow_handler_source(
    function: &oxc_ast::ast::ArrowFunctionExpression<'_>,
    source: &str,
    bindings: &[RequestBinding],
) -> Option<String> {
    let handler_source = source_slice(source, function.span)?;
    let mut erase_spans = Vec::new();
    collect_parameter_erase_spans(&function.params, &mut erase_spans);
    if let Some(type_parameters) = &function.type_parameters {
        erase_spans.push(type_parameters.span);
    }
    if let Some(return_type) = &function.return_type {
        erase_spans.push(return_type.span);
    }
    for statement in &function.body.statements {
        collect_statement_erase_spans(statement, &mut erase_spans);
    }
    let executable_source =
        erase_spans_from_source(&handler_source, function.span.start, &erase_spans)?;
    Some(wrapper_source(&executable_source, bindings))
}

pub(crate) fn typed_function_handler_source(
    function: &oxc_ast::ast::Function<'_>,
    source: &str,
    bindings: &[RequestBinding],
) -> Option<String> {
    let handler_source = source_slice(source, function.span)?;
    let mut erase_spans = Vec::new();
    collect_parameter_erase_spans(&function.params, &mut erase_spans);
    if let Some(type_parameters) = &function.type_parameters {
        erase_spans.push(type_parameters.span);
    }
    if let Some(return_type) = &function.return_type {
        erase_spans.push(return_type.span);
    }
    if let Some(body) = &function.body {
        for statement in &body.statements {
            collect_statement_erase_spans(statement, &mut erase_spans);
        }
    }
    let executable_source =
        erase_spans_from_source(&handler_source, function.span.start, &erase_spans)?;
    Some(wrapper_source(&executable_source, bindings))
}

fn collect_parameter_erase_spans(
    parameters: &oxc_ast::ast::FormalParameters<'_>,
    erase_spans: &mut Vec<Span>,
) {
    for parameter in &parameters.items {
        if let Some(annotation) = &parameter.type_annotation {
            erase_spans.push(annotation.span);
        }
    }
}

fn collect_block_erase_spans(
    block: &oxc_ast::ast::BlockStatement<'_>,
    erase_spans: &mut Vec<Span>,
) {
    for statement in &block.body {
        collect_statement_erase_spans(statement, erase_spans);
    }
}

fn collect_statement_erase_spans(statement: &Statement<'_>, erase_spans: &mut Vec<Span>) {
    match statement {
        Statement::BlockStatement(statement) => {
            for statement in &statement.body {
                collect_statement_erase_spans(statement, erase_spans);
            }
        }
        Statement::ExpressionStatement(statement) => {
            collect_expression_erase_spans(&statement.expression, erase_spans);
        }
        Statement::ReturnStatement(statement) => {
            if let Some(argument) = &statement.argument {
                collect_expression_erase_spans(argument, erase_spans);
            }
        }
        Statement::VariableDeclaration(statement) => {
            for declarator in &statement.declarations {
                if let Some(init) = &declarator.init {
                    collect_expression_erase_spans(init, erase_spans);
                }
            }
        }
        Statement::IfStatement(statement) => {
            collect_expression_erase_spans(&statement.test, erase_spans);
            collect_statement_erase_spans(&statement.consequent, erase_spans);
            if let Some(alternate) = &statement.alternate {
                collect_statement_erase_spans(alternate, erase_spans);
            }
        }
        Statement::ForStatement(statement) => {
            if let Some(init) = &statement.init {
                if let ForStatementInit::VariableDeclaration(declaration) = init {
                    for declarator in &declaration.declarations {
                        if let Some(init) = &declarator.init {
                            collect_expression_erase_spans(init, erase_spans);
                        }
                    }
                } else if let Some(expression) = init.as_expression() {
                    collect_expression_erase_spans(expression, erase_spans);
                }
            }
            if let Some(test) = &statement.test {
                collect_expression_erase_spans(test, erase_spans);
            }
            if let Some(update) = &statement.update {
                collect_expression_erase_spans(update, erase_spans);
            }
            collect_statement_erase_spans(&statement.body, erase_spans);
        }
        Statement::WhileStatement(statement) => {
            collect_expression_erase_spans(&statement.test, erase_spans);
            collect_statement_erase_spans(&statement.body, erase_spans);
        }
        Statement::ThrowStatement(statement) => {
            collect_expression_erase_spans(&statement.argument, erase_spans);
        }
        Statement::TryStatement(statement) => {
            collect_block_erase_spans(&statement.block, erase_spans);
            if let Some(handler) = &statement.handler {
                collect_block_erase_spans(&handler.body, erase_spans);
            }
            if let Some(finalizer) = &statement.finalizer {
                collect_block_erase_spans(finalizer, erase_spans);
            }
        }
        _ => {}
    }
}

fn collect_expression_erase_spans(expression: &Expression<'_>, erase_spans: &mut Vec<Span>) {
    match expression {
        Expression::CallExpression(call) => {
            if let Some(type_arguments) = &call.type_arguments {
                erase_spans.push(type_arguments.span);
            }
            collect_expression_erase_spans(&call.callee, erase_spans);
            for argument in &call.arguments {
                collect_argument_erase_spans(argument, erase_spans);
            }
        }
        Expression::AwaitExpression(expression) => {
            collect_expression_erase_spans(&expression.argument, erase_spans);
        }
        Expression::BinaryExpression(expression) => {
            collect_expression_erase_spans(&expression.left, erase_spans);
            collect_expression_erase_spans(&expression.right, erase_spans);
        }
        Expression::LogicalExpression(expression) => {
            collect_expression_erase_spans(&expression.left, erase_spans);
            collect_expression_erase_spans(&expression.right, erase_spans);
        }
        Expression::ConditionalExpression(expression) => {
            collect_expression_erase_spans(&expression.test, erase_spans);
            collect_expression_erase_spans(&expression.consequent, erase_spans);
            collect_expression_erase_spans(&expression.alternate, erase_spans);
        }
        Expression::ObjectExpression(object) => {
            for property in &object.properties {
                if let ObjectPropertyKind::ObjectProperty(property) = property {
                    collect_expression_erase_spans(&property.value, erase_spans);
                }
            }
        }
        Expression::ArrayExpression(array) => {
            for element in &array.elements {
                collect_array_element_erase_spans(element, erase_spans);
            }
        }
        Expression::ParenthesizedExpression(expression) => {
            collect_expression_erase_spans(&expression.expression, erase_spans);
        }
        Expression::StaticMemberExpression(expression) => {
            collect_expression_erase_spans(&expression.object, erase_spans);
        }
        Expression::ComputedMemberExpression(expression) => {
            collect_expression_erase_spans(&expression.object, erase_spans);
            collect_expression_erase_spans(&expression.expression, erase_spans);
        }
        Expression::TSAsExpression(expression) => {
            erase_spans.push(ts_type_span(&expression.type_annotation));
            collect_expression_erase_spans(&expression.expression, erase_spans);
        }
        Expression::TSSatisfiesExpression(expression) => {
            erase_spans.push(ts_type_span(&expression.type_annotation));
            collect_expression_erase_spans(&expression.expression, erase_spans);
        }
        Expression::TSNonNullExpression(expression) => {
            collect_expression_erase_spans(&expression.expression, erase_spans);
        }
        _ => {}
    }
}

fn collect_argument_erase_spans(argument: &Argument<'_>, erase_spans: &mut Vec<Span>) {
    match argument {
        Argument::CallExpression(call) => {
            if let Some(type_arguments) = &call.type_arguments {
                erase_spans.push(type_arguments.span);
            }
            collect_expression_erase_spans(&call.callee, erase_spans);
            for argument in &call.arguments {
                collect_argument_erase_spans(argument, erase_spans);
            }
        }
        Argument::AwaitExpression(expression) => {
            collect_expression_erase_spans(&expression.argument, erase_spans);
        }
        Argument::ObjectExpression(object) => {
            for property in &object.properties {
                if let ObjectPropertyKind::ObjectProperty(property) = property {
                    collect_expression_erase_spans(&property.value, erase_spans);
                }
            }
        }
        Argument::ArrayExpression(array) => {
            for element in &array.elements {
                collect_array_element_erase_spans(element, erase_spans);
            }
        }
        Argument::StaticMemberExpression(expression) => {
            collect_expression_erase_spans(&expression.object, erase_spans);
        }
        Argument::ComputedMemberExpression(expression) => {
            collect_expression_erase_spans(&expression.object, erase_spans);
            collect_expression_erase_spans(&expression.expression, erase_spans);
        }
        _ => {}
    }
}

fn collect_array_element_erase_spans(
    element: &ArrayExpressionElement<'_>,
    erase_spans: &mut Vec<Span>,
) {
    match element {
        ArrayExpressionElement::CallExpression(call) => {
            if let Some(type_arguments) = &call.type_arguments {
                erase_spans.push(type_arguments.span);
            }
            collect_expression_erase_spans(&call.callee, erase_spans);
            for argument in &call.arguments {
                collect_argument_erase_spans(argument, erase_spans);
            }
        }
        ArrayExpressionElement::ObjectExpression(object) => {
            for property in &object.properties {
                if let ObjectPropertyKind::ObjectProperty(property) = property {
                    collect_expression_erase_spans(&property.value, erase_spans);
                }
            }
        }
        ArrayExpressionElement::ArrayExpression(array) => {
            for element in &array.elements {
                collect_array_element_erase_spans(element, erase_spans);
            }
        }
        _ => {}
    }
}

fn erase_spans_from_source(
    handler_source: &str,
    handler_start: u32,
    spans: &[Span],
) -> Option<String> {
    let mut output = handler_source.to_string();
    let mut spans = spans.to_vec();
    spans.sort_by_key(|span| std::cmp::Reverse(span.start));
    for span in spans {
        let start = usize::try_from(span.start.checked_sub(handler_start)?).ok()?;
        let end = usize::try_from(span.end.checked_sub(handler_start)?).ok()?;
        if end > output.len() || start > end {
            return None;
        }
        output.replace_range(start..end, "");
    }
    Some(output)
}

fn wrapper_source(handler_source: &str, bindings: &[RequestBinding]) -> String {
    let args = bindings
        .iter()
        .map(|binding| {
            let descriptor = serde_json::to_string(&binding_descriptor(binding)).ok()?;
            Some(format!("__sloppy_framework_arg(ctx, {descriptor})"))
        })
        .collect::<Option<Vec<_>>>()
        .unwrap_or_default()
        .join(", ");
    format!(
        "(ctx) => {{ const __sloppy_typed_handler = {handler_source}; return __sloppy_typed_handler({args}); }}"
    )
}

fn binding_descriptor(binding: &RequestBinding) -> Value {
    json!({
        "kind": binding.kind,
        "name": binding.name,
        "parameter": binding.parameter,
        "type": binding.type_name,
        "schema": binding.schema,
        "wrapper": binding.wrapper,
        "injectionKind": binding.injection_kind,
        "providerKind": binding.provider_kind,
        "capability": binding.capability
    })
}

fn source_slice(source: &str, span: Span) -> Option<String> {
    let start = usize::try_from(span.start).ok()?;
    let end = usize::try_from(span.end).ok()?;
    source.get(start..end).map(ToString::to_string)
}

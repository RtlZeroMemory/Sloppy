use oxc_ast::ast::{
    Argument, ArrayExpressionElement, Expression, ForStatementInit, ObjectPropertyKind, Statement,
    TaggedTemplateExpression, TemplateLiteral, VariableDeclaration,
};
use oxc_span::{GetSpan, Span};
use serde_json::{json, Value};

use crate::{graph::RequestBinding, sloppyc::ts_type_span};

pub(crate) struct TypedHandlerSource {
    pub original_source: String,
    pub emitted_source: String,
    pub source_map_line_offset: usize,
    pub source_map_column_offset: usize,
}

pub(crate) fn typed_arrow_handler_source_with_replacements(
    function: &oxc_ast::ast::ArrowFunctionExpression<'_>,
    source: &str,
    bindings: &[RequestBinding],
    replacements: &[(Span, &'static str)],
) -> Option<TypedHandlerSource> {
    let handler_source = source_slice(source, function.span)?;
    let mut erase_spans = Vec::new();
    collect_arrow_erase_spans(function, &mut erase_spans);
    let executable_source = edit_source_spans(
        &handler_source,
        function.span.start,
        &erase_spans,
        replacements,
    )?;
    let wrapper = wrapper_source(&executable_source, bindings, function.r#async);
    Some(TypedHandlerSource {
        original_source: handler_source,
        emitted_source: wrapper.source,
        source_map_line_offset: wrapper.handler_line_offset,
        source_map_column_offset: wrapper.handler_column_offset,
    })
}

pub(crate) fn typed_function_handler_source_with_replacements(
    function: &oxc_ast::ast::Function<'_>,
    source: &str,
    bindings: &[RequestBinding],
    replacements: &[(Span, &'static str)],
) -> Option<TypedHandlerSource> {
    let handler_source = source_slice(source, function.span)?;
    let mut erase_spans = Vec::new();
    collect_function_erase_spans(function, &mut erase_spans);
    let executable_source = edit_source_spans(
        &handler_source,
        function.span.start,
        &erase_spans,
        replacements,
    )?;
    let wrapper = wrapper_source(&executable_source, bindings, function.r#async);
    Some(TypedHandlerSource {
        original_source: handler_source,
        emitted_source: wrapper.source,
        source_map_line_offset: wrapper.handler_line_offset,
        source_map_column_offset: wrapper.handler_column_offset,
    })
}

fn collect_arrow_erase_spans(
    function: &oxc_ast::ast::ArrowFunctionExpression<'_>,
    erase_spans: &mut Vec<Span>,
) {
    collect_parameter_erase_spans(&function.params, erase_spans);
    if let Some(type_parameters) = &function.type_parameters {
        erase_spans.push(type_parameters.span);
    }
    if let Some(return_type) = &function.return_type {
        erase_spans.push(return_type.span);
    }
    for statement in &function.body.statements {
        collect_statement_erase_spans(statement, erase_spans);
    }
}

fn collect_function_erase_spans(
    function: &oxc_ast::ast::Function<'_>,
    erase_spans: &mut Vec<Span>,
) {
    collect_parameter_erase_spans(&function.params, erase_spans);
    if let Some(type_parameters) = &function.type_parameters {
        erase_spans.push(type_parameters.span);
    }
    if let Some(this_param) = &function.this_param {
        erase_spans.push(this_param.span);
    }
    if let Some(return_type) = &function.return_type {
        erase_spans.push(return_type.span);
    }
    if let Some(body) = &function.body {
        for statement in &body.statements {
            collect_statement_erase_spans(statement, erase_spans);
        }
    }
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
            collect_variable_declaration_erase_spans(statement, erase_spans);
        }
        Statement::FunctionDeclaration(function) => {
            collect_function_erase_spans(function, erase_spans);
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
                    collect_variable_declaration_erase_spans(declaration, erase_spans);
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

fn collect_variable_declaration_erase_spans(
    declaration: &VariableDeclaration<'_>,
    erase_spans: &mut Vec<Span>,
) {
    for declarator in &declaration.declarations {
        if let Some(annotation) = &declarator.type_annotation {
            erase_spans.push(annotation.span);
        }
        if let Some(init) = &declarator.init {
            collect_expression_erase_spans(init, erase_spans);
        }
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
        Expression::ArrowFunctionExpression(function) => {
            collect_arrow_erase_spans(function, erase_spans);
        }
        Expression::FunctionExpression(function) => {
            collect_function_erase_spans(function, erase_spans);
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
        Expression::TemplateLiteral(template) => {
            collect_template_literal_erase_spans(template, erase_spans);
        }
        Expression::TaggedTemplateExpression(expression) => {
            collect_tagged_template_erase_spans(expression, erase_spans);
        }
        Expression::TSAsExpression(expression) => {
            erase_spans.push(Span::new(
                expression.expression.span().end,
                ts_type_span(&expression.type_annotation).end,
            ));
            collect_expression_erase_spans(&expression.expression, erase_spans);
        }
        Expression::TSSatisfiesExpression(expression) => {
            erase_spans.push(Span::new(
                expression.expression.span().end,
                ts_type_span(&expression.type_annotation).end,
            ));
            collect_expression_erase_spans(&expression.expression, erase_spans);
        }
        Expression::TSNonNullExpression(expression) => {
            erase_spans.push(Span::new(
                expression.expression.span().end,
                expression.span.end,
            ));
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
        Argument::ArrowFunctionExpression(function) => {
            collect_arrow_erase_spans(function, erase_spans);
        }
        Argument::FunctionExpression(function) => {
            collect_function_erase_spans(function, erase_spans);
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
        Argument::TemplateLiteral(template) => {
            collect_template_literal_erase_spans(template, erase_spans);
        }
        Argument::TaggedTemplateExpression(expression) => {
            collect_tagged_template_erase_spans(expression, erase_spans);
        }
        Argument::TSAsExpression(expression) => {
            erase_spans.push(Span::new(
                expression.expression.span().end,
                ts_type_span(&expression.type_annotation).end,
            ));
            collect_expression_erase_spans(&expression.expression, erase_spans);
        }
        Argument::TSSatisfiesExpression(expression) => {
            erase_spans.push(Span::new(
                expression.expression.span().end,
                ts_type_span(&expression.type_annotation).end,
            ));
            collect_expression_erase_spans(&expression.expression, erase_spans);
        }
        Argument::TSNonNullExpression(expression) => {
            erase_spans.push(Span::new(
                expression.expression.span().end,
                expression.span.end,
            ));
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
        ArrayExpressionElement::ArrowFunctionExpression(function) => {
            collect_arrow_erase_spans(function, erase_spans);
        }
        ArrayExpressionElement::FunctionExpression(function) => {
            collect_function_erase_spans(function, erase_spans);
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
        ArrayExpressionElement::TemplateLiteral(template) => {
            collect_template_literal_erase_spans(template, erase_spans);
        }
        ArrayExpressionElement::TaggedTemplateExpression(expression) => {
            collect_tagged_template_erase_spans(expression, erase_spans);
        }
        ArrayExpressionElement::TSAsExpression(expression) => {
            erase_spans.push(Span::new(
                expression.expression.span().end,
                ts_type_span(&expression.type_annotation).end,
            ));
            collect_expression_erase_spans(&expression.expression, erase_spans);
        }
        ArrayExpressionElement::TSSatisfiesExpression(expression) => {
            erase_spans.push(Span::new(
                expression.expression.span().end,
                ts_type_span(&expression.type_annotation).end,
            ));
            collect_expression_erase_spans(&expression.expression, erase_spans);
        }
        ArrayExpressionElement::TSNonNullExpression(expression) => {
            erase_spans.push(Span::new(
                expression.expression.span().end,
                expression.span.end,
            ));
            collect_expression_erase_spans(&expression.expression, erase_spans);
        }
        _ => {}
    }
}

fn collect_template_literal_erase_spans(
    template: &TemplateLiteral<'_>,
    erase_spans: &mut Vec<Span>,
) {
    for expression in &template.expressions {
        collect_expression_erase_spans(expression, erase_spans);
    }
}

fn collect_tagged_template_erase_spans(
    expression: &TaggedTemplateExpression<'_>,
    erase_spans: &mut Vec<Span>,
) {
    if let Some(type_arguments) = &expression.type_arguments {
        erase_spans.push(type_arguments.span);
    }
    collect_expression_erase_spans(&expression.tag, erase_spans);
    collect_template_literal_erase_spans(&expression.quasi, erase_spans);
}

fn edit_source_spans(
    handler_source: &str,
    handler_start: u32,
    erase_spans: &[Span],
    replacements: &[(Span, &'static str)],
) -> Option<String> {
    enum Edit {
        Erase(Span),
        Replace(Span, &'static str),
    }

    let mut output = handler_source.to_string();
    let mut edits = Vec::new();
    edits.extend(erase_spans.iter().copied().map(Edit::Erase));
    edits.extend(
        replacements
            .iter()
            .copied()
            .map(|(span, replacement)| Edit::Replace(span, replacement)),
    );
    edits.sort_by(|left, right| {
        let left_span = match left {
            Edit::Erase(span) | Edit::Replace(span, _) => span,
        };
        let right_span = match right {
            Edit::Erase(span) | Edit::Replace(span, _) => span,
        };
        right_span
            .start
            .cmp(&left_span.start)
            .then_with(|| right_span.end.cmp(&left_span.end))
    });
    for edit in edits {
        let (span, replacement) = match edit {
            Edit::Erase(span) => (span, ""),
            Edit::Replace(span, replacement) => (span, replacement),
        };
        let start = usize::try_from(span.start.checked_sub(handler_start)?).ok()?;
        let end = usize::try_from(span.end.checked_sub(handler_start)?).ok()?;
        if end > output.len() || start > end {
            return None;
        }
        output.replace_range(start..end, replacement);
    }
    Some(output)
}

struct WrapperSource {
    source: String,
    handler_line_offset: usize,
    handler_column_offset: usize,
}

fn typed_binding_can_sync_without_scope(binding: &RequestBinding) -> bool {
    matches!(
        binding.kind.as_str(),
        "body.json" | "config" | "context" | "header" | "query" | "route"
    )
}

fn wrapper_source(
    handler_source: &str,
    bindings: &[RequestBinding],
    handler_is_async: bool,
) -> WrapperSource {
    let args_with_scope = |scope_expr: &str| {
        bindings
            .iter()
            .map(|binding| {
                let descriptor = serde_json::to_string(&binding_descriptor(binding)).ok()?;
                Some(format!(
                    "__sloppy_framework_arg(ctx, {scope_expr}, {descriptor})"
                ))
            })
            .collect::<Option<Vec<_>>>()
            .unwrap_or_default()
            .join(", ")
    };

    if !handler_is_async && bindings.iter().all(typed_binding_can_sync_without_scope) {
        let args = args_with_scope("undefined");
        let prefix = "(ctx) => { const __sloppy_typed_handler = ";
        return WrapperSource {
            source: format!("{prefix}{handler_source}; return __sloppy_typed_handler({args}); }}"),
            handler_line_offset: 0,
            handler_column_offset: prefix.len(),
        };
    }

    let args = args_with_scope("__sloppy_scope");
    let prefix = "async (ctx) => { const __sloppy_scope = __sloppy_framework_services.createScope(ctx); ctx.services = __sloppy_scope; try { const __sloppy_typed_handler = ";
    WrapperSource {
        source: format!(
            "{prefix}{handler_source}; const __sloppy_args = await Promise.all([{args}]); return await __sloppy_typed_handler(...__sloppy_args); }} finally {{ await __sloppy_scope.dispose(); }} }}"
        ),
        handler_line_offset: 0,
        handler_column_offset: prefix.len(),
    }
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

use super::*;

pub(super) fn schema_declaration(
    path: &Path,
    source: &str,
    source_name: &str,
    name: &str,
    expression: &Expression<'_>,
    known_schema_names: &BTreeSet<String>,
) -> Result<Option<SchemaMetadata>, Diagnostic> {
    if expression_is_realtime_descriptor_root(expression) {
        return Ok(None);
    }
    if !expression_mentions_schema(expression) {
        return Ok(None);
    }
    let definition =
        schema_definition(expression, known_schema_names, Some(name)).ok_or_else(|| {
            Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_SCHEMA",
                "schema declarations must use the supported schema DSL",
            )
            .with_path(path)
            .with_span(expression.span())
            .with_hint("Use schema.object/string/int/number/bool/array with literal object fields.")
        })?;
    Ok(Some(SchemaMetadata {
        name: name.to_string(),
        definition,
        source_name: source_name.to_string(),
        source: source.to_string(),
        span: expression.span(),
    }))
}

fn expression_is_realtime_descriptor_root(expression: &Expression<'_>) -> bool {
    match expression {
        Expression::CallExpression(call) => {
            if static_member_name(&call.callee).is_some_and(|(object, property)| {
                object == "Realtime" && matches!(property, "channel" | "event")
            }) {
                return true;
            }
            if call
                .arguments
                .iter()
                .any(argument_is_realtime_descriptor_root)
            {
                return true;
            }
            match &call.callee {
                Expression::StaticMemberExpression(member) => {
                    expression_is_realtime_descriptor_root(&member.object)
                }
                Expression::ParenthesizedExpression(parenthesized) => {
                    expression_is_realtime_descriptor_root(&parenthesized.expression)
                }
                _ => false,
            }
        }
        Expression::ParenthesizedExpression(parenthesized) => {
            expression_is_realtime_descriptor_root(&parenthesized.expression)
        }
        Expression::TSAsExpression(node) => {
            expression_is_realtime_descriptor_root(&node.expression)
        }
        Expression::TSSatisfiesExpression(node) => {
            expression_is_realtime_descriptor_root(&node.expression)
        }
        Expression::TSTypeAssertion(node) => {
            expression_is_realtime_descriptor_root(&node.expression)
        }
        Expression::TSNonNullExpression(node) => {
            expression_is_realtime_descriptor_root(&node.expression)
        }
        Expression::TSInstantiationExpression(node) => {
            expression_is_realtime_descriptor_root(&node.expression)
        }
        _ => false,
    }
}

fn argument_is_realtime_descriptor_root(argument: &Argument<'_>) -> bool {
    argument
        .as_expression()
        .is_some_and(expression_is_realtime_descriptor_root)
}

pub(super) fn typescript_type_alias_schema(
    path: &Path,
    source: &str,
    source_name: &str,
    alias: &oxc_ast::ast::TSTypeAliasDeclaration<'_>,
    known_schema_names: &BTreeSet<String>,
) -> Result<Option<SchemaMetadata>, Diagnostic> {
    if alias.type_parameters.is_some() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_TYPESCRIPT_SCHEMA",
            "generic type aliases are not supported by framework schema inference",
        )
        .with_path(path)
        .with_span(alias.span)
        .with_hint(
            "Use concrete object aliases or interfaces for compiler-emitted schema metadata.",
        ));
    }
    let name = alias.id.name.as_str();
    let definition = typescript_schema_definition(
        path,
        source,
        &alias.type_annotation,
        known_schema_names,
        Some(name),
    )?;
    Ok(Some(SchemaMetadata {
        name: name.to_string(),
        definition,
        source_name: source_name.to_string(),
        source: source.to_string(),
        span: alias.span,
    }))
}

pub(super) fn typescript_interface_schema(
    path: &Path,
    source: &str,
    source_name: &str,
    interface: &oxc_ast::ast::TSInterfaceDeclaration<'_>,
    known_schema_names: &BTreeSet<String>,
) -> Result<Option<SchemaMetadata>, Diagnostic> {
    if interface.type_parameters.is_some() || !interface.extends.is_empty() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_TYPESCRIPT_SCHEMA",
            "generic or inherited interfaces are not supported by framework schema inference",
        )
        .with_path(path)
        .with_span(interface.span)
        .with_hint(
            "Use a concrete interface without extends for compiler-emitted schema metadata.",
        ));
    }
    let name = interface.id.name.as_str();
    let definition = typescript_object_schema_from_signatures(
        path,
        source,
        &interface.body.body,
        known_schema_names,
        Some(name),
    )?;
    Ok(Some(SchemaMetadata {
        name: name.to_string(),
        definition,
        source_name: source_name.to_string(),
        source: source.to_string(),
        span: interface.span,
    }))
}

pub(super) fn typescript_schema_definition(
    path: &Path,
    source: &str,
    ty: &TSType<'_>,
    known_schema_names: &BTreeSet<String>,
    current_schema_name: Option<&str>,
) -> Result<Value, Diagnostic> {
    match ty {
        TSType::TSStringKeyword(_) => Ok(json!({ "kind": "string" })),
        TSType::TSNumberKeyword(_) => Ok(json!({ "kind": "number" })),
        TSType::TSBooleanKeyword(_) => Ok(json!({ "kind": "bool" })),
        TSType::TSNullKeyword(_) => Ok(json!({ "kind": "null" })),
        TSType::TSArrayType(array) => {
            let items = typescript_schema_definition(
                path,
                source,
                &array.element_type,
                known_schema_names,
                current_schema_name,
            )?;
            Ok(json!({ "kind": "array", "items": items }))
        }
        TSType::TSTypeLiteral(literal) => typescript_object_schema_from_signatures(
            path,
            source,
            &literal.members,
            known_schema_names,
            current_schema_name,
        ),
        TSType::TSTypeReference(reference) => {
            let Some(name) = typescript_type_name(&reference.type_name) else {
                return Err(unsupported_typescript_schema_diagnostic(
                    path,
                    reference.span,
                    "qualified or computed type references are not supported by framework schema inference",
                ));
            };
            semantic_or_reference_schema(
                path,
                reference.span,
                name,
                reference.type_arguments.as_deref(),
                known_schema_names,
                current_schema_name,
            )
        }
        TSType::TSLiteralType(literal) => literal_type_schema(path, literal),
        TSType::TSUnionType(union) => union_type_schema(
            path,
            source,
            &union.types,
            union.span,
            known_schema_names,
            current_schema_name,
        ),
        TSType::TSParenthesizedType(parenthesized) => typescript_schema_definition(
            path,
            source,
            &parenthesized.type_annotation,
            known_schema_names,
            current_schema_name,
        ),
        TSType::TSAnyKeyword(_)
        | TSType::TSUnknownKeyword(_)
        | TSType::TSConditionalType(_)
        | TSType::TSMappedType(_)
        | TSType::TSFunctionType(_)
        | TSType::TSImportType(_)
        | TSType::TSIndexedAccessType(_)
        | TSType::TSInferType(_)
        | TSType::TSIntersectionType(_)
        | TSType::TSTemplateLiteralType(_)
        | TSType::TSTupleType(_)
        | TSType::TSTypeOperatorType(_)
        | TSType::TSTypeQuery(_) => Err(unsupported_typescript_schema_diagnostic(
            path,
            ts_type_span(ty),
            "unsupported TypeScript type shape in framework schema inference",
        )),
        _ => Err(unsupported_typescript_schema_diagnostic(
            path,
            ts_type_span(ty),
            "unsupported TypeScript type keyword in framework schema inference",
        )),
    }
}

pub(super) fn typescript_object_schema_from_signatures(
    path: &Path,
    source: &str,
    signatures: &[TSSignature<'_>],
    known_schema_names: &BTreeSet<String>,
    current_schema_name: Option<&str>,
) -> Result<Value, Diagnostic> {
    let mut properties = serde_json::Map::new();
    for signature in signatures {
        let TSSignature::TSPropertySignature(property) = signature else {
            return Err(unsupported_typescript_schema_diagnostic(
                path,
                ts_signature_span(signature),
                "only object properties are supported in framework schema metadata",
            ));
        };
        if property.computed {
            return Err(unsupported_typescript_schema_diagnostic(
                path,
                property.span,
                "computed TypeScript property names are not supported in framework schema metadata",
            ));
        }
        let Some(annotation) = &property.type_annotation else {
            return Err(unsupported_typescript_schema_diagnostic(
                path,
                property.span,
                "schema properties must include TypeScript type annotations",
            ));
        };
        let Some(key) = property_key_string(&property.key) else {
            return Err(unsupported_typescript_schema_diagnostic(
                path,
                property.span,
                "schema property names must be static strings or identifiers",
            ));
        };
        let mut value = typescript_schema_definition(
            path,
            source,
            &annotation.type_annotation,
            known_schema_names,
            current_schema_name,
        )?;
        if property.optional {
            value["optional"] = json!(true);
        }
        if schema_value_is_secret(&value) || key_is_secret_like(&key) {
            value["secret"] = json!(true);
            value["redaction"] = json!("secret");
        }
        properties.insert(key, value);
    }
    Ok(json!({ "kind": "object", "properties": properties }))
}

pub(super) fn semantic_or_reference_schema(
    path: &Path,
    span: Span,
    name: &str,
    type_arguments: Option<&oxc_ast::ast::TSTypeParameterInstantiation<'_>>,
    known_schema_names: &BTreeSet<String>,
    current_schema_name: Option<&str>,
) -> Result<Value, Diagnostic> {
    match name {
        "Email" if type_arguments.is_none() => Ok(json!({
            "kind": "string",
            "semantic": "Email",
            "validation": "email"
        })),
        "NonEmptyString" if type_arguments.is_none() => Ok(json!({
            "kind": "string",
            "semantic": "NonEmptyString",
            "min": 1
        })),
        "SecretString" if type_arguments.is_none() => Ok(json!({
            "kind": "string",
            "semantic": "SecretString",
            "secret": true,
            "redaction": "secret"
        })),
        "PasswordString" => {
            let min = type_arguments
                .and_then(|arguments| arguments.params.first())
                .and_then(type_numeric_literal_value)
                .ok_or_else(|| {
                    Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_TYPESCRIPT_SCHEMA",
                        "PasswordString requires a numeric literal minimum length",
                    )
                    .with_path(path)
                    .with_span(span)
                    .with_hint("Use PasswordString<8> or another numeric literal.")
                })?;
            Ok(json!({
                "kind": "string",
                "semantic": "PasswordString",
                "min": min,
                "secret": true,
                "redaction": "secret"
            }))
        }
        "Uuid" if type_arguments.is_none() => Ok(json!({
            "kind": "string",
            "semantic": "Uuid",
            "validation": "uuid"
        })),
        "DateTime" | "Instant" if type_arguments.is_none() => Ok(json!({
            "kind": "string",
            "semantic": name,
            "validation": "datetime"
        })),
        "PositiveInt" if type_arguments.is_none() => Ok(json!({
            "kind": "int",
            "semantic": "PositiveInt",
            "min": 1
        })),
        _ if type_arguments.is_none() && current_schema_name == Some(name) => Err(
            unsupported_typescript_schema_diagnostic(
                path,
                span,
                "recursive TypeScript schema references are not supported by framework schema inference",
            ),
        ),
        _ if type_arguments.is_none() && known_schema_names.contains(name) => {
            Ok(json!({ "kind": "ref", "name": name }))
        }
        _ if type_arguments.is_none() => Err(Diagnostic::new(
            "SLOPPYC_E_UNRESOLVED_TYPE",
            "unresolved TypeScript type reference in framework schema inference",
        )
        .with_path(path)
        .with_span(span)
        .with_hint("Use a local type alias/interface or a supported Sloppy semantic built-in.")),
        _ => Err(unsupported_typescript_schema_diagnostic(
            path,
            span,
            "generic type references are not supported by framework schema inference",
        )),
    }
}

pub(super) fn literal_type_schema(
    path: &Path,
    literal: &oxc_ast::ast::TSLiteralType<'_>,
) -> Result<Value, Diagnostic> {
    match &literal.literal {
        TSLiteral::StringLiteral(value) => {
            Ok(json!({ "kind": "literal", "value": value.value.as_str() }))
        }
        TSLiteral::NumericLiteral(value) => Ok(json!({ "kind": "literal", "value": value.value })),
        TSLiteral::BooleanLiteral(value) => Ok(json!({ "kind": "literal", "value": value.value })),
        _ => Err(unsupported_typescript_schema_diagnostic(
            path,
            literal.span,
            "only string, number, and boolean literal types are supported",
        )),
    }
}

pub(super) fn union_type_schema(
    path: &Path,
    source: &str,
    types: &[TSType<'_>],
    span: Span,
    known_schema_names: &BTreeSet<String>,
    current_schema_name: Option<&str>,
) -> Result<Value, Diagnostic> {
    let mut nullable = false;
    let mut variants = Vec::new();
    for ty in types {
        if matches!(ty, TSType::TSNullKeyword(_)) {
            nullable = true;
            continue;
        }
        variants.push(typescript_schema_definition(
            path,
            source,
            ty,
            known_schema_names,
            current_schema_name,
        )?);
    }
    if nullable && variants.len() == 1 {
        let mut value = variants.remove(0);
        value["nullable"] = json!(true);
        return Ok(value);
    }
    if variants.is_empty() {
        return Err(unsupported_typescript_schema_diagnostic(
            path,
            span,
            "union types must contain at least one non-null variant",
        ));
    }
    if variants.iter().all(|value| value["kind"] == "literal") {
        return Ok(json!({ "kind": "literalUnion", "variants": variants }));
    }
    Err(unsupported_typescript_schema_diagnostic(
        path,
        span,
        "only nullable unions and literal unions are supported by framework schema inference",
    ))
}

pub(super) fn unsupported_typescript_schema_diagnostic(
    path: &Path,
    span: Span,
    message: &str,
) -> Diagnostic {
    Diagnostic::new("SLOPPYC_E_UNSUPPORTED_TYPESCRIPT_SCHEMA", message)
        .with_path(path)
        .with_span(span)
        .with_hint("Use concrete aliases, interfaces, object literals, arrays, primitives, semantic built-ins, nullable unions, or literal unions.")
}

pub(super) fn type_numeric_literal_value(ty: &TSType<'_>) -> Option<u64> {
    let TSType::TSLiteralType(literal) = ty else {
        return None;
    };
    let TSLiteral::NumericLiteral(value) = &literal.literal else {
        return None;
    };
    if value.value.fract() == 0.0 && value.value >= 0.0 {
        Some(value.value as u64)
    } else {
        None
    }
}

pub(super) fn typescript_type_name<'a>(name: &'a TSTypeName<'a>) -> Option<&'a str> {
    match name {
        TSTypeName::IdentifierReference(identifier) => Some(identifier.name.as_str()),
        _ => None,
    }
}

pub(super) fn schema_value_is_secret(value: &Value) -> bool {
    value
        .get("secret")
        .and_then(Value::as_bool)
        .unwrap_or(false)
}

pub(super) fn key_is_secret_like(key: &str) -> bool {
    let lower = key.to_ascii_lowercase();
    lower.contains("password") || lower.contains("secret") || lower.contains("token")
}

pub(crate) fn ts_type_span(ty: &TSType<'_>) -> Span {
    match ty {
        TSType::TSAnyKeyword(node) => node.span,
        TSType::TSBigIntKeyword(node) => node.span,
        TSType::TSBooleanKeyword(node) => node.span,
        TSType::TSIntrinsicKeyword(node) => node.span,
        TSType::TSNeverKeyword(node) => node.span,
        TSType::TSNullKeyword(node) => node.span,
        TSType::TSNumberKeyword(node) => node.span,
        TSType::TSObjectKeyword(node) => node.span,
        TSType::TSStringKeyword(node) => node.span,
        TSType::TSSymbolKeyword(node) => node.span,
        TSType::TSUndefinedKeyword(node) => node.span,
        TSType::TSUnknownKeyword(node) => node.span,
        TSType::TSVoidKeyword(node) => node.span,
        TSType::TSArrayType(node) => node.span,
        TSType::TSConditionalType(node) => node.span,
        TSType::TSConstructorType(node) => node.span,
        TSType::TSFunctionType(node) => node.span,
        TSType::TSImportType(node) => node.span,
        TSType::TSIndexedAccessType(node) => node.span,
        TSType::TSInferType(node) => node.span,
        TSType::TSIntersectionType(node) => node.span,
        TSType::TSLiteralType(node) => node.span,
        TSType::TSMappedType(node) => node.span,
        TSType::TSNamedTupleMember(node) => node.span,
        TSType::TSTemplateLiteralType(node) => node.span,
        TSType::TSThisType(node) => node.span,
        TSType::TSTupleType(node) => node.span,
        TSType::TSTypeLiteral(node) => node.span,
        TSType::TSTypeOperatorType(node) => node.span,
        TSType::TSTypePredicate(node) => node.span,
        TSType::TSTypeQuery(node) => node.span,
        TSType::TSTypeReference(node) => node.span,
        TSType::TSUnionType(node) => node.span,
        TSType::TSParenthesizedType(node) => node.span,
        TSType::JSDocNullableType(node) => node.span,
        TSType::JSDocNonNullableType(node) => node.span,
        TSType::JSDocUnknownType(node) => node.span,
    }
}

pub(super) fn ts_signature_span(signature: &TSSignature<'_>) -> Span {
    match signature {
        TSSignature::TSIndexSignature(node) => node.span,
        TSSignature::TSPropertySignature(node) => node.span,
        TSSignature::TSCallSignatureDeclaration(node) => node.span,
        TSSignature::TSConstructSignatureDeclaration(node) => node.span,
        TSSignature::TSMethodSignature(node) => node.span,
    }
}

pub(super) fn schema_definition(
    expression: &Expression<'_>,
    known_schema_names: &BTreeSet<String>,
    current_schema_name: Option<&str>,
) -> Option<Value> {
    match expression {
        Expression::CallExpression(call) => {
            schema_definition_call(call, known_schema_names, current_schema_name)
        }
        Expression::Identifier(identifier)
            if current_schema_name != Some(identifier.name.as_str())
                && known_schema_names.contains(identifier.name.as_str()) =>
        {
            Some(json!({ "kind": "ref", "name": identifier.name.as_str() }))
        }
        Expression::ParenthesizedExpression(parenthesized) => schema_definition(
            &parenthesized.expression,
            known_schema_names,
            current_schema_name,
        ),
        _ => None,
    }
}

pub(super) fn schema_definition_call(
    call: &CallExpression<'_>,
    known_schema_names: &BTreeSet<String>,
    current_schema_name: Option<&str>,
) -> Option<Value> {
    if let Expression::StaticMemberExpression(member) = &call.callee {
        let property = member.property.name.as_str();
        if matches!(
            property,
            "optional"
                | "nullable"
                | "default"
                | "min"
                | "max"
                | "minLength"
                | "maxLength"
                | "email"
                | "uuid"
                | "pattern"
        ) {
            let mut base =
                schema_definition(&member.object, known_schema_names, current_schema_name)?;
            if property == "optional" {
                if !call.arguments.is_empty() {
                    return None;
                }
                base["optional"] = json!(true);
            } else if property == "nullable" {
                if !call.arguments.is_empty() {
                    return None;
                }
                base["nullable"] = json!(true);
            } else if property == "default" {
                if call.arguments.len() != 1 {
                    return None;
                }
                let value = call
                    .arguments
                    .first()
                    .and_then(schema_json_argument_value)?;
                base["optional"] = json!(true);
                base["default"] = value;
            } else if property == "email" {
                if !call.arguments.is_empty() {
                    return None;
                }
                base["format"] = json!("email");
            } else if property == "uuid" {
                if !call.arguments.is_empty() {
                    return None;
                }
                base["format"] = json!("uuid");
            } else if property == "pattern" {
                if !matches!(call.arguments.len(), 1 | 2) {
                    return None;
                }
                let Some(Argument::RegExpLiteral(regex)) = call.arguments.first() else {
                    return None;
                };
                if call.arguments.len() == 2
                    && !matches!(call.arguments.get(1), Some(Argument::StringLiteral(_)))
                {
                    return None;
                }
                let flags = regex.regex.flags.to_string().replace(['g', 'y'], "");
                base["pattern"] = json!(regex.regex.pattern.text.as_str());
                if !flags.is_empty() {
                    base["patternFlags"] = json!(flags);
                }
                if let Some(Argument::StringLiteral(message)) = call.arguments.get(1) {
                    base["patternMessage"] = json!(message.value.as_str());
                }
            } else {
                if call.arguments.len() != 1 {
                    return None;
                }
                let number = call
                    .arguments
                    .first()
                    .and_then(numeric_argument_json_value)?;
                let key = match property {
                    "minLength" => "min",
                    "maxLength" => "max",
                    _ => property,
                };
                base[key] = number;
            }
            return Some(base);
        }
    }

    let (object, method) = static_member_name(&call.callee)?;
    if !matches!(object, "schema" | "Schema") {
        return None;
    }
    match method {
        "string" | "int" | "integer" | "number" | "bool" | "boolean"
            if call.arguments.is_empty() =>
        {
            let kind = match method {
                "integer" => "int",
                "boolean" => "bool",
                _ => method,
            };
            Some(json!({ "kind": kind }))
        }
        "array" if call.arguments.len() == 1 => {
            let inner = call.arguments.first().and_then(|argument| {
                argument_schema_definition(argument, known_schema_names, current_schema_name)
            })?;
            Some(json!({ "kind": "array", "items": inner }))
        }
        "object" if call.arguments.len() == 1 => {
            let Argument::ObjectExpression(object) = call.arguments.first()? else {
                return None;
            };
            let mut properties = serde_json::Map::new();
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
                let key = property_key_string(&property.key)?;
                let value =
                    schema_definition(&property.value, known_schema_names, current_schema_name)?;
                properties.insert(key, value);
            }
            Some(json!({ "kind": "object", "properties": properties }))
        }
        "literal" if call.arguments.len() == 1 => {
            let value = call
                .arguments
                .first()
                .and_then(literal_argument_json_value)?;
            Some(json!({ "kind": "literal", "value": value }))
        }
        "enum" if call.arguments.len() == 1 => {
            let Argument::ArrayExpression(array) = call.arguments.first()? else {
                return None;
            };
            let mut variants = Vec::new();
            for element in &array.elements {
                let value = literal_array_element_json_value(element)?;
                variants.push(json!({ "kind": "literal", "value": value }));
            }
            if variants.is_empty() {
                return None;
            }
            Some(json!({ "kind": "literalUnion", "variants": variants }))
        }
        _ => None,
    }
}

pub(super) fn argument_schema_definition(
    argument: &Argument<'_>,
    known_schema_names: &BTreeSet<String>,
    current_schema_name: Option<&str>,
) -> Option<Value> {
    match argument {
        Argument::CallExpression(call) => {
            schema_definition_call(call, known_schema_names, current_schema_name)
        }
        Argument::Identifier(identifier)
            if current_schema_name != Some(identifier.name.as_str())
                && known_schema_names.contains(identifier.name.as_str()) =>
        {
            Some(json!({ "kind": "ref", "name": identifier.name.as_str() }))
        }
        Argument::ParenthesizedExpression(parenthesized) => schema_definition(
            &parenthesized.expression,
            known_schema_names,
            current_schema_name,
        ),
        _ => None,
    }
}

pub(super) fn expression_mentions_schema(expression: &Expression<'_>) -> bool {
    match expression {
        Expression::CallExpression(call) => call_mentions_schema(call),
        Expression::ObjectExpression(object) => object
            .properties
            .iter()
            .any(object_property_mentions_schema),
        Expression::ArrayExpression(array) => {
            array.elements.iter().any(array_element_mentions_schema)
        }
        Expression::ComputedMemberExpression(member) => {
            expression_mentions_schema(&member.object)
                || expression_mentions_schema(&member.expression)
        }
        Expression::StaticMemberExpression(member) => expression_mentions_schema(&member.object),
        Expression::ParenthesizedExpression(node) => expression_mentions_schema(&node.expression),
        Expression::ConditionalExpression(node) => {
            expression_mentions_schema(&node.test)
                || expression_mentions_schema(&node.consequent)
                || expression_mentions_schema(&node.alternate)
        }
        Expression::UnaryExpression(node) => expression_mentions_schema(&node.argument),
        Expression::BinaryExpression(node) => {
            expression_mentions_schema(&node.left) || expression_mentions_schema(&node.right)
        }
        Expression::LogicalExpression(node) => {
            expression_mentions_schema(&node.left) || expression_mentions_schema(&node.right)
        }
        Expression::SequenceExpression(node) => {
            node.expressions.iter().any(expression_mentions_schema)
        }
        Expression::TSAsExpression(node) => expression_mentions_schema(&node.expression),
        Expression::TSSatisfiesExpression(node) => expression_mentions_schema(&node.expression),
        Expression::TSTypeAssertion(node) => expression_mentions_schema(&node.expression),
        Expression::TSNonNullExpression(node) => expression_mentions_schema(&node.expression),
        Expression::TSInstantiationExpression(node) => expression_mentions_schema(&node.expression),
        _ => false,
    }
}

pub(super) fn call_mentions_schema(call: &CallExpression<'_>) -> bool {
    static_member_name(&call.callee)
        .is_some_and(|(object, _)| matches!(object, "schema" | "Schema"))
        || match &call.callee {
            Expression::StaticMemberExpression(member) => {
                expression_mentions_schema(&member.object)
            }
            _ => false,
        }
        || call.arguments.iter().any(argument_mentions_schema)
}

pub(super) fn argument_mentions_schema(argument: &Argument<'_>) -> bool {
    match argument {
        Argument::CallExpression(call) => call_mentions_schema(call),
        Argument::ObjectExpression(object) => object
            .properties
            .iter()
            .any(object_property_mentions_schema),
        Argument::ArrayExpression(array) => {
            array.elements.iter().any(array_element_mentions_schema)
        }
        Argument::ComputedMemberExpression(member) => {
            expression_mentions_schema(&member.object)
                || expression_mentions_schema(&member.expression)
        }
        Argument::ParenthesizedExpression(parenthesized) => {
            expression_mentions_schema(&parenthesized.expression)
        }
        Argument::StaticMemberExpression(member) => expression_mentions_schema(&member.object),
        _ => false,
    }
}

pub(super) fn object_property_mentions_schema(property: &ObjectPropertyKind<'_>) -> bool {
    match property {
        ObjectPropertyKind::ObjectProperty(property) => expression_mentions_schema(&property.value),
        _ => false,
    }
}

pub(super) fn array_element_mentions_schema(element: &ArrayExpressionElement<'_>) -> bool {
    match element {
        ArrayExpressionElement::CallExpression(call) => call_mentions_schema(call),
        ArrayExpressionElement::ObjectExpression(object) => object
            .properties
            .iter()
            .any(object_property_mentions_schema),
        ArrayExpressionElement::ArrayExpression(array) => {
            array.elements.iter().any(array_element_mentions_schema)
        }
        _ => false,
    }
}

pub(super) fn property_key_string(key: &PropertyKey<'_>) -> Option<String> {
    match key {
        PropertyKey::StaticIdentifier(identifier) => Some(identifier.name.as_str().to_string()),
        PropertyKey::StringLiteral(literal) => Some(literal.value.as_str().to_string()),
        PropertyKey::NumericLiteral(literal) => Some(literal.value.to_string()),
        _ => None,
    }
}

pub(super) fn numeric_argument_value(argument: &Argument<'_>) -> Option<f64> {
    match argument {
        Argument::NumericLiteral(literal) => Some(literal.value),
        _ => None,
    }
}

pub(super) fn numeric_argument_json_value(argument: &Argument<'_>) -> Option<Value> {
    let value = numeric_argument_value(argument)?;
    if value.is_finite() && value.fract() == 0.0 {
        if value >= i64::MIN as f64 && value <= i64::MAX as f64 {
            return Some(json!(value as i64));
        }
        if value >= 0.0 && value <= u64::MAX as f64 {
            return Some(json!(value as u64));
        }
    }
    Some(json!(value))
}

pub(super) fn literal_argument_json_value(argument: &Argument<'_>) -> Option<Value> {
    match argument {
        Argument::StringLiteral(value) => Some(json!(value.value.as_str())),
        Argument::NumericLiteral(_) => numeric_argument_json_value(argument),
        Argument::BooleanLiteral(value) => Some(json!(value.value)),
        Argument::ParenthesizedExpression(parenthesized) => {
            literal_expression_json_value(&parenthesized.expression)
        }
        _ => None,
    }
}

pub(super) fn literal_expression_json_value(expression: &Expression<'_>) -> Option<Value> {
    match expression {
        Expression::StringLiteral(value) => Some(json!(value.value.as_str())),
        Expression::NumericLiteral(value) => numeric_json_value(value.value),
        Expression::BooleanLiteral(value) => Some(json!(value.value)),
        Expression::ParenthesizedExpression(parenthesized) => {
            literal_expression_json_value(&parenthesized.expression)
        }
        _ => None,
    }
}

pub(super) fn literal_array_element_json_value(
    element: &ArrayExpressionElement<'_>,
) -> Option<Value> {
    match element {
        ArrayExpressionElement::StringLiteral(value) => Some(json!(value.value.as_str())),
        ArrayExpressionElement::NumericLiteral(value) => numeric_json_value(value.value),
        ArrayExpressionElement::BooleanLiteral(value) => Some(json!(value.value)),
        _ => None,
    }
}

pub(super) fn schema_json_argument_value(argument: &Argument<'_>) -> Option<Value> {
    match argument {
        Argument::StringLiteral(value) => Some(json!(value.value.as_str())),
        Argument::NumericLiteral(_) => numeric_argument_json_value(argument),
        Argument::BooleanLiteral(value) => Some(json!(value.value)),
        Argument::NullLiteral(_) => Some(Value::Null),
        Argument::ArrayExpression(array) => {
            let mut values = Vec::new();
            for element in &array.elements {
                values.push(schema_json_array_element_value(element)?);
            }
            Some(Value::Array(values))
        }
        Argument::ObjectExpression(object) => schema_json_object_value(object),
        Argument::ParenthesizedExpression(parenthesized) => {
            schema_json_expression_value(&parenthesized.expression)
        }
        _ => None,
    }
}

pub(super) fn schema_json_expression_value(expression: &Expression<'_>) -> Option<Value> {
    match expression {
        Expression::StringLiteral(value) => Some(json!(value.value.as_str())),
        Expression::NumericLiteral(value) => numeric_json_value(value.value),
        Expression::BooleanLiteral(value) => Some(json!(value.value)),
        Expression::NullLiteral(_) => Some(Value::Null),
        Expression::ArrayExpression(array) => {
            let mut values = Vec::new();
            for element in &array.elements {
                values.push(schema_json_array_element_value(element)?);
            }
            Some(Value::Array(values))
        }
        Expression::ObjectExpression(object) => schema_json_object_value(object),
        Expression::ParenthesizedExpression(parenthesized) => {
            schema_json_expression_value(&parenthesized.expression)
        }
        _ => None,
    }
}

pub(super) fn schema_json_array_element_value(
    element: &ArrayExpressionElement<'_>,
) -> Option<Value> {
    match element {
        ArrayExpressionElement::StringLiteral(value) => Some(json!(value.value.as_str())),
        ArrayExpressionElement::NumericLiteral(value) => numeric_json_value(value.value),
        ArrayExpressionElement::BooleanLiteral(value) => Some(json!(value.value)),
        ArrayExpressionElement::NullLiteral(_) => Some(Value::Null),
        ArrayExpressionElement::ArrayExpression(array) => {
            let mut values = Vec::new();
            for element in &array.elements {
                values.push(schema_json_array_element_value(element)?);
            }
            Some(Value::Array(values))
        }
        ArrayExpressionElement::ObjectExpression(object) => schema_json_object_value(object),
        _ => None,
    }
}

pub(super) fn schema_json_object_value(
    object: &oxc_ast::ast::ObjectExpression<'_>,
) -> Option<Value> {
    let mut values = serde_json::Map::new();
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
        let key = property_key_string(&property.key)?;
        let value = schema_json_expression_value(&property.value)?;
        values.insert(key, value);
    }
    Some(Value::Object(values))
}

fn numeric_json_value(value: f64) -> Option<Value> {
    if !value.is_finite() {
        return None;
    }
    if value.fract() == 0.0 {
        if value >= i64::MIN as f64 && value <= i64::MAX as f64 {
            return Some(json!(value as i64));
        }
        if value >= 0.0 && value <= u64::MAX as f64 {
            return Some(json!(value as u64));
        }
    }
    Some(json!(value))
}

// Static sloppy/ffi declaration extraction.
use super::*;
use serde_json::json;

#[derive(Default)]
struct FfiImportBindings {
    ffi_names: BTreeSet<String>,
    type_names: BTreeSet<String>,
    handle_names: BTreeMap<String, String>,
    struct_names: BTreeMap<String, String>,
    callback_names: BTreeMap<String, String>,
}

struct FfiSourceContext<'a> {
    path: &'a Path,
    source: &'a str,
    source_name: &'a str,
}

pub(super) struct FfiMetadataSink<'a> {
    pub(super) libraries: &'a mut Vec<FfiLibraryMetadata>,
    pub(super) structs: &'a mut Vec<FfiStructMetadata>,
    pub(super) handles: &'a mut Vec<FfiHandleMetadata>,
    pub(super) callbacks: &'a mut Vec<FfiCallbackMetadata>,
    pub(super) dispatch_tables: &'a mut Vec<FfiDispatchTableMetadata>,
}

impl FfiImportBindings {
    fn has_bindings(&self) -> bool {
        !self.ffi_names.is_empty() || !self.type_names.is_empty()
    }
}

fn ffi_import_bindings(statements: &[Statement<'_>]) -> FfiImportBindings {
    let mut bindings = FfiImportBindings::default();

    for statement in statements {
        let Statement::ImportDeclaration(import) = statement else {
            continue;
        };
        if import.source.value.as_str() != "sloppy/ffi" {
            continue;
        }
        let Some(specifiers) = &import.specifiers else {
            continue;
        };
        for specifier in specifiers {
            let ImportDeclarationSpecifier::ImportSpecifier(specifier) = specifier else {
                continue;
            };
            if !import_specifier_is_runtime_value(import, specifier) {
                continue;
            }
            let imported = specifier.imported.name().as_str();
            let local = specifier.local.name.as_str();
            match imported {
                "unsafeFfi" => {
                    bindings.ffi_names.insert(local.to_string());
                }
                "t" => {
                    bindings.type_names.insert(local.to_string());
                }
                _ => {}
            }
        }
    }

    if bindings.has_bindings() {
        collect_static_ffi_descriptor_names(statements, &mut bindings);
    }

    bindings
}

fn collect_static_ffi_descriptor_names(
    statements: &[Statement<'_>],
    bindings: &mut FfiImportBindings,
) {
    for statement in statements {
        let Some(declaration) = ffi_variable_declaration_from_statement(statement) else {
            continue;
        };
        for declarator in &declaration.declarations {
            let Some(local_name) = binding_identifier(&declarator.id) else {
                continue;
            };
            let Some(init) = &declarator.init else {
                continue;
            };
            let Some((member, call)) = ffi_static_member_call(init, &bindings.ffi_names) else {
                continue;
            };
            match member {
                "handle" => {
                    if let Some(name) = call.arguments.first().and_then(string_argument) {
                        bindings
                            .handle_names
                            .insert(local_name.to_string(), name.to_string());
                    }
                }
                "struct" => {
                    if let Some(name) = call.arguments.first().and_then(string_argument) {
                        bindings
                            .struct_names
                            .insert(local_name.to_string(), name.to_string());
                    }
                }
                "callback" => {
                    bindings
                        .callback_names
                        .insert(local_name.to_string(), local_name.to_string());
                }
                _ => {}
            }
        }
    }
}

pub(super) fn extract_ffi_declarations_from_statements(
    path: &Path,
    source: &str,
    source_name: &str,
    statements: &[Statement<'_>],
    sink: FfiMetadataSink<'_>,
) -> Result<(), Diagnostic> {
    let bindings = ffi_import_bindings(statements);
    if !bindings.has_bindings() {
        return Ok(());
    }
    let context = FfiSourceContext {
        path,
        source,
        source_name,
    };

    for statement in statements {
        let Some(declaration) = ffi_variable_declaration_from_statement(statement) else {
            continue;
        };
        for declarator in &declaration.declarations {
            let Some(init) = &declarator.init else {
                continue;
            };
            let Some(call) = ffi_static_member_call(init, &bindings.ffi_names) else {
                continue;
            };
            match call.0 {
                "library" => {
                    let library = extract_ffi_library_declaration(&context, call.1, &bindings)?;
                    sink.libraries.push(library);
                }
                "struct" => {
                    let layout = extract_ffi_struct_declaration(&context, call.1, &bindings)?;
                    sink.structs.push(layout);
                }
                "handle" => {
                    sink.handles
                        .push(extract_ffi_handle_declaration(&context, call.1)?);
                }
                "callback" => {
                    sink.callbacks.push(extract_ffi_callback_declaration(
                        &context, call.1, &bindings,
                    )?);
                }
                "dispatchTable" => {
                    sink.dispatch_tables
                        .push(extract_ffi_dispatch_table_declaration(
                            &context, call.1, &bindings,
                        )?);
                }
                _ => {}
            }
        }
    }

    Ok(())
}

fn ffi_variable_declaration_from_statement<'a>(
    statement: &'a Statement<'a>,
) -> Option<&'a VariableDeclaration<'a>> {
    match statement {
        Statement::VariableDeclaration(declaration) => Some(declaration),
        Statement::ExportNamedDeclaration(export)
            if export.export_kind != ImportOrExportKind::Type && export.source.is_none() =>
        {
            match &export.declaration {
                Some(Declaration::VariableDeclaration(declaration)) => Some(declaration),
                _ => None,
            }
        }
        _ => None,
    }
}

fn ffi_static_member_call<'a>(
    expression: &'a Expression<'a>,
    ffi_names: &BTreeSet<String>,
) -> Option<(&'a str, &'a CallExpression<'a>)> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    let Expression::StaticMemberExpression(member) = &call.callee else {
        return None;
    };
    let Expression::Identifier(object) = &member.object else {
        return None;
    };
    if !ffi_names.contains(object.name.as_str()) {
        return None;
    }
    Some((member.property.name.as_str(), call))
}

fn ffi_function_descriptor_call<'a>(
    expression: &'a Expression<'a>,
    bindings: &FfiImportBindings,
) -> Option<&'a CallExpression<'a>> {
    let (member, call) = ffi_static_member_call(expression, &bindings.ffi_names)?;
    (member == "fn").then_some(call)
}

fn extract_ffi_library_declaration(
    context: &FfiSourceContext<'_>,
    call: &CallExpression<'_>,
    bindings: &FfiImportBindings,
) -> Result<FfiLibraryMetadata, Diagnostic> {
    let Some(library_name) = call.arguments.first().and_then(string_argument) else {
        return Err(ffi_dynamic_declaration_diag(
            context.path,
            call.span,
            "unsafeFfi.library requires a static non-empty library name",
        ));
    };
    if library_name.is_empty() {
        return Err(ffi_dynamic_declaration_diag(
            context.path,
            call.span,
            "unsafeFfi.library requires a non-empty library name",
        ));
    }
    let Some(functions_object) = call.arguments.get(1).and_then(object_argument) else {
        return Err(ffi_dynamic_declaration_diag(
            context.path,
            call.span,
            "unsafeFfi.library requires a static object literal of functions",
        ));
    };
    let options = ffi_options_object(context, call, 2, "unsafeFfi.library")?;
    let convention = match options {
        Some(object) => {
            ffi_string_option(context, call.span, object, "convention")?.unwrap_or("system")
        }
        None => "system",
    };
    validate_ffi_convention(context.path, call.span, convention)?;

    let mut functions = Vec::new();
    for property in &functions_object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(ffi_dynamic_declaration_diag(
                context.path,
                call.span,
                "unsafeFfi.library function declarations do not support spreads",
            ));
        };
        if property.computed {
            return Err(ffi_dynamic_declaration_diag(
                context.path,
                property.span,
                "unsafeFfi.library function names must be static properties",
            ));
        }
        let Some(function_name) = property_key_name(&property.key) else {
            return Err(ffi_dynamic_declaration_diag(
                context.path,
                property.span,
                "unsafeFfi.library function names must be static properties",
            ));
        };
        let Some(fn_call) = ffi_function_descriptor_call(&property.value, bindings) else {
            return Err(ffi_dynamic_declaration_diag(
                context.path,
                property.span,
                "unsafeFfi.library values must be static unsafeFfi.fn(...) descriptors",
            ));
        };
        functions.push(extract_ffi_function_declaration(
            context,
            library_name,
            function_name,
            convention,
            fn_call,
            bindings,
        )?);
    }
    if functions.is_empty() {
        return Err(ffi_dynamic_declaration_diag(
            context.path,
            call.span,
            "unsafeFfi.library requires at least one function",
        ));
    }

    Ok(FfiLibraryMetadata {
        name: library_name.to_string(),
        convention: convention.to_string(),
        functions,
        source_name: context.source_name.to_string(),
        source: context.source.to_string(),
        span: call.span,
    })
}

fn extract_ffi_function_declaration(
    context: &FfiSourceContext<'_>,
    library_name: &str,
    function_name: &str,
    library_convention: &str,
    call: &CallExpression<'_>,
    bindings: &FfiImportBindings,
) -> Result<FfiFunctionMetadata, Diagnostic> {
    let Some(return_expression) = call.arguments.first().and_then(Argument::as_expression) else {
        return Err(ffi_dynamic_declaration_diag(
            context.path,
            call.span,
            "unsafeFfi.fn requires a static return type",
        ));
    };
    let (return_type, return_descriptor) =
        ffi_type_descriptor_from_expression(context.path, return_expression, bindings)?;
    if ffi_return_type_unsupported(&return_type) {
        return Err(Diagnostic::new(
            "SLOPPY_E_FFI_UNSUPPORTED_TYPE",
            format!("unsupported FFI return type \"{return_type}\""),
        )
        .with_path(context.path)
        .with_span(return_expression.span())
        .with_hint("Return cstring, utf16, bytes, and mutBytes through caller-owned buffers."));
    }
    let Some(Argument::ArrayExpression(parameters)) = call.arguments.get(1) else {
        return Err(ffi_dynamic_declaration_diag(
            context.path,
            call.span,
            "unsafeFfi.fn requires a static array of parameter types",
        ));
    };
    let mut parameter_types = Vec::new();
    let mut parameter_descriptors = Vec::new();
    for element in &parameters.elements {
        let Some(expression) = element.as_expression() else {
            return Err(ffi_dynamic_declaration_diag(
                context.path,
                oxc_span::GetSpan::span(element),
                "unsafeFfi.fn parameter types must be static type aliases",
            ));
        };
        let (parameter_type, parameter_descriptor) =
            ffi_type_descriptor_from_expression(context.path, expression, bindings)?;
        if parameter_type == "void" {
            return Err(Diagnostic::new(
                "SLOPPY_E_FFI_UNSUPPORTED_TYPE",
                "FFI parameters cannot use void",
            )
            .with_path(context.path)
            .with_span(expression.span()));
        }
        parameter_types.push(parameter_type);
        parameter_descriptors.push(parameter_descriptor.unwrap_or(serde_json::Value::Null));
    }
    let options = ffi_options_object(context, call, 2, "unsafeFfi.fn")?;
    if options
        .map(|object| ffi_bool_option(context, call.span, object, "callback"))
        .transpose()?
        .flatten()
        .unwrap_or(false)
    {
        return Err(Diagnostic::new(
            "SLOPPY_E_FFI_UNSUPPORTED_CALLBACK",
            "FFI callbacks are not supported",
        )
        .with_path(context.path)
        .with_span(call.span));
    }
    if options
        .map(|object| ffi_bool_option(context, call.span, object, "variadic"))
        .transpose()?
        .flatten()
        .unwrap_or(false)
    {
        return Err(Diagnostic::new(
            "SLOPPY_E_FFI_UNSUPPORTED_VARIADIC",
            "FFI variadic functions are not supported",
        )
        .with_path(context.path)
        .with_span(call.span));
    }
    let symbol = match options {
        Some(object) => {
            ffi_string_option(context, call.span, object, "symbol")?.unwrap_or(function_name)
        }
        None => function_name,
    };
    if symbol.is_empty() {
        return Err(ffi_dynamic_declaration_diag(
            context.path,
            call.span,
            "unsafeFfi.fn symbol must be a non-empty string",
        ));
    }
    let convention = match options {
        Some(object) => ffi_string_option(context, call.span, object, "convention")?
            .unwrap_or(library_convention),
        None => library_convention,
    };
    validate_ffi_convention(context.path, call.span, convention)?;
    let dispose = match options {
        Some(object) => {
            ffi_string_option(context, call.span, object, "dispose")?.map(str::to_string)
        }
        None => None,
    };

    Ok(FfiFunctionMetadata {
        id: format!("ffi:{library_name}:{function_name}"),
        name: function_name.to_string(),
        symbol: symbol.to_string(),
        convention: convention.to_string(),
        return_type,
        parameters: parameter_types,
        return_descriptor,
        parameter_descriptors,
        dispose,
        source_name: context.source_name.to_string(),
        source: context.source.to_string(),
        span: call.span,
    })
}

fn extract_ffi_struct_declaration(
    context: &FfiSourceContext<'_>,
    call: &CallExpression<'_>,
    bindings: &FfiImportBindings,
) -> Result<FfiStructMetadata, Diagnostic> {
    let Some(struct_name) = call.arguments.first().and_then(string_argument) else {
        return Err(ffi_dynamic_declaration_diag(
            context.path,
            call.span,
            "unsafeFfi.struct requires a static non-empty struct name",
        ));
    };
    if struct_name.is_empty() {
        return Err(ffi_dynamic_declaration_diag(
            context.path,
            call.span,
            "unsafeFfi.struct requires a non-empty struct name",
        ));
    }
    let Some(fields_object) = call.arguments.get(1).and_then(object_argument) else {
        return Err(ffi_dynamic_declaration_diag(
            context.path,
            call.span,
            "unsafeFfi.struct requires a static object literal of fields",
        ));
    };
    let mut fields = Vec::new();
    for property in &fields_object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(ffi_dynamic_declaration_diag(
                context.path,
                call.span,
                "unsafeFfi.struct field declarations do not support spreads",
            ));
        };
        if property.computed {
            return Err(ffi_dynamic_declaration_diag(
                context.path,
                property.span,
                "unsafeFfi.struct field names must be static properties",
            ));
        }
        let Some(field_name) = property_key_name(&property.key) else {
            return Err(ffi_dynamic_declaration_diag(
                context.path,
                property.span,
                "unsafeFfi.struct field names must be static properties",
            ));
        };
        let (type_name, descriptor) =
            ffi_struct_field_descriptor_from_expression(context.path, &property.value, bindings)?;
        if descriptor.is_none() && !ffi_struct_field_type_supported(&type_name) {
            return Err(Diagnostic::new(
                "SLOPPY_E_FFI_UNSUPPORTED_TYPE",
                format!("unsupported FFI struct field type \"{type_name}\""),
            )
            .with_path(context.path)
            .with_span(property.value.span())
            .with_hint(
                "Struct layouts support fixed-size primitive fields; pass structs by pointer.",
            ));
        }
        fields.push(FfiStructFieldMetadata {
            name: field_name.to_string(),
            type_name,
            descriptor,
        });
    }
    if fields.is_empty() {
        return Err(ffi_dynamic_declaration_diag(
            context.path,
            call.span,
            "unsafeFfi.struct requires at least one field",
        ));
    }
    let (layout, pack) = extract_ffi_struct_options(context.path, call)?;
    Ok(FfiStructMetadata {
        name: struct_name.to_string(),
        layout,
        pack,
        fields,
        source_name: context.source_name.to_string(),
        source: context.source.to_string(),
        span: call.span,
    })
}

fn extract_ffi_struct_options(
    path: &Path,
    call: &CallExpression<'_>,
) -> Result<(String, Option<u32>), Diagnostic> {
    let Some(options) = call.arguments.get(2).and_then(object_argument) else {
        return Ok(("sequential".to_string(), None));
    };
    let mut layout = "sequential".to_string();
    let mut pack = None;
    for property in &options.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(ffi_dynamic_declaration_diag(
                path,
                call.span,
                "unsafeFfi.struct options do not support spreads",
            ));
        };
        if property.computed {
            return Err(ffi_dynamic_declaration_diag(
                path,
                property.span,
                "unsafeFfi.struct option names must be static properties",
            ));
        }
        let Some(name) = property_key_name(&property.key) else {
            return Err(ffi_dynamic_declaration_diag(
                path,
                property.span,
                "unsafeFfi.struct option names must be static properties",
            ));
        };
        match name {
            "layout" => {
                let Expression::StringLiteral(value) = &property.value else {
                    return Err(ffi_dynamic_declaration_diag(
                        path,
                        property.span,
                        "unsafeFfi.struct layout must be a string literal",
                    ));
                };
                if value.value.as_str() != "sequential" {
                    return Err(Diagnostic::new(
                        "SLOPPY_E_FFI_UNSUPPORTED_TYPE",
                        "FFI structs only support sequential layout",
                    )
                    .with_path(path)
                    .with_span(property.value.span())
                    .with_hint(
                        "Unions, bitfields, and struct-by-value layouts are not supported.",
                    ));
                }
                layout = value.value.as_str().to_string();
            }
            "pack" => {
                let Expression::NumericLiteral(value) = &property.value else {
                    return Err(ffi_dynamic_declaration_diag(
                        path,
                        property.span,
                        "unsafeFfi.struct pack must be a numeric literal",
                    ));
                };
                if value.value.fract() != 0.0
                    || !matches!(value.value as u32, 1 | 2 | 4 | 8 | 16)
                    || value.value < 1.0
                    || value.value > 16.0
                {
                    return Err(Diagnostic::new(
                        "SLOPPY_E_FFI_UNSUPPORTED_TYPE",
                        "FFI struct pack must be 1, 2, 4, 8, or 16",
                    )
                    .with_path(path)
                    .with_span(property.value.span()));
                }
                pack = Some(value.value as u32);
            }
            _ => {
                return Err(ffi_dynamic_declaration_diag(
                    path,
                    property.span,
                    format!("unsupported unsafeFfi.struct option \"{name}\""),
                ));
            }
        }
    }
    Ok((layout, pack))
}

fn extract_ffi_handle_declaration(
    context: &FfiSourceContext<'_>,
    call: &CallExpression<'_>,
) -> Result<FfiHandleMetadata, Diagnostic> {
    let Some(name) = call.arguments.first().and_then(string_argument) else {
        return Err(ffi_dynamic_declaration_diag(
            context.path,
            call.span,
            "unsafeFfi.handle requires a static non-empty handle name",
        ));
    };
    if name.is_empty() {
        return Err(ffi_dynamic_declaration_diag(
            context.path,
            call.span,
            "unsafeFfi.handle requires a non-empty handle name",
        ));
    }
    Ok(FfiHandleMetadata {
        name: name.to_string(),
        source_name: context.source_name.to_string(),
        source: context.source.to_string(),
        span: call.span,
    })
}

fn extract_ffi_callback_declaration(
    context: &FfiSourceContext<'_>,
    call: &CallExpression<'_>,
    bindings: &FfiImportBindings,
) -> Result<FfiCallbackMetadata, Diagnostic> {
    let Some(return_expression) = call.arguments.first().and_then(Argument::as_expression) else {
        return Err(ffi_dynamic_declaration_diag(
            context.path,
            call.span,
            "unsafeFfi.callback requires a static return type",
        ));
    };
    let return_type = ffi_type_name_from_expression(context.path, return_expression, bindings)?;
    if !ffi_callback_return_type_supported(&return_type) {
        return Err(Diagnostic::new(
            "SLOPPY_E_FFI_UNSUPPORTED_CALLBACK",
            format!("unsupported FFI callback return type \"{return_type}\""),
        )
        .with_path(context.path)
        .with_span(return_expression.span())
        .with_hint("Callbacks currently support void, i32, and u32 returns."));
    }
    let Some(Argument::ArrayExpression(parameters)) = call.arguments.get(1) else {
        return Err(ffi_dynamic_declaration_diag(
            context.path,
            call.span,
            "unsafeFfi.callback requires a static parameter array",
        ));
    };
    let mut parameter_types = Vec::new();
    for element in &parameters.elements {
        let Some(expression) = element.as_expression() else {
            return Err(ffi_dynamic_declaration_diag(
                context.path,
                oxc_span::GetSpan::span(element),
                "unsafeFfi.callback parameter types must be static",
            ));
        };
        let parameter_type = ffi_type_name_from_expression(context.path, expression, bindings)?;
        if !ffi_callback_parameter_type_supported(&parameter_type) {
            return Err(Diagnostic::new(
                "SLOPPY_E_FFI_UNSUPPORTED_CALLBACK",
                format!("unsupported FFI callback parameter type \"{parameter_type}\""),
            )
            .with_path(context.path)
            .with_span(expression.span())
            .with_hint("Callbacks currently support i32 and u32 parameters."));
        }
        parameter_types.push(parameter_type);
    }
    Ok(FfiCallbackMetadata {
        id: format!("ffi:callback:{}:{}", context.source_name, call.span.start),
        return_type,
        parameters: parameter_types,
        thread: "runtime".to_string(),
        source_name: context.source_name.to_string(),
        source: context.source.to_string(),
        span: call.span,
    })
}

fn extract_ffi_dispatch_table_declaration(
    context: &FfiSourceContext<'_>,
    call: &CallExpression<'_>,
    bindings: &FfiImportBindings,
) -> Result<FfiDispatchTableMetadata, Diagnostic> {
    let Some(name) = call.arguments.first().and_then(string_argument) else {
        return Err(ffi_dynamic_declaration_diag(
            context.path,
            call.span,
            "unsafeFfi.dispatchTable requires a static name",
        ));
    };
    let Some(descriptor) = call.arguments.get(1).and_then(object_argument) else {
        return Err(ffi_dynamic_declaration_diag(
            context.path,
            call.span,
            "unsafeFfi.dispatchTable requires a static descriptor",
        ));
    };
    let mut resolver = None;
    let mut symbols = Vec::new();
    for property in &descriptor.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(ffi_dynamic_declaration_diag(
                context.path,
                call.span,
                "unsafeFfi.dispatchTable descriptor does not support spreads",
            ));
        };
        let Some(property_name) = property_key_name(&property.key) else {
            continue;
        };
        match property_name {
            "resolver" => {
                if let Expression::StaticMemberExpression(member) = &property.value {
                    if let Expression::Identifier(object) = &member.object {
                        resolver = Some(format!(
                            "{}.{}",
                            object.name.as_str(),
                            member.property.name.as_str()
                        ));
                    }
                }
                if resolver.is_none() {
                    return Err(ffi_dynamic_declaration_diag(
                        context.path,
                        property.span,
                        "unsafeFfi.dispatchTable resolver must be a static library member",
                    ));
                }
            }
            "symbols" => {
                let Expression::ObjectExpression(symbol_object) = &property.value else {
                    return Err(ffi_dynamic_declaration_diag(
                        context.path,
                        property.span,
                        "unsafeFfi.dispatchTable symbols must be a static object",
                    ));
                };
                for symbol_property in &symbol_object.properties {
                    let ObjectPropertyKind::ObjectProperty(symbol_property) = symbol_property
                    else {
                        return Err(ffi_dynamic_declaration_diag(
                            context.path,
                            property.span,
                            "unsafeFfi.dispatchTable symbols do not support spreads",
                        ));
                    };
                    let Some(symbol_name) = property_key_name(&symbol_property.key) else {
                        return Err(ffi_dynamic_declaration_diag(
                            context.path,
                            symbol_property.span,
                            "unsafeFfi.dispatchTable symbol names must be static",
                        ));
                    };
                    let Some(fn_call) =
                        ffi_function_descriptor_call(&symbol_property.value, bindings)
                    else {
                        return Err(ffi_dynamic_declaration_diag(
                            context.path,
                            symbol_property.span,
                            "unsafeFfi.dispatchTable symbols must be unsafeFfi.fn descriptors",
                        ));
                    };
                    let symbol = extract_ffi_function_declaration(
                        context,
                        name,
                        symbol_name,
                        "system",
                        fn_call,
                        bindings,
                    )?;
                    validate_ffi_dispatch_symbol(context, &symbol, symbol_property.span)?;
                    symbols.push(symbol);
                }
            }
            _ => {}
        }
    }
    let Some(resolver) = resolver else {
        return Err(ffi_dynamic_declaration_diag(
            context.path,
            call.span,
            "unsafeFfi.dispatchTable requires a static resolver",
        ));
    };
    if symbols.is_empty() {
        return Err(ffi_dynamic_declaration_diag(
            context.path,
            call.span,
            "unsafeFfi.dispatchTable requires at least one symbol",
        ));
    }
    Ok(FfiDispatchTableMetadata {
        name: name.to_string(),
        resolver,
        symbols,
        source_name: context.source_name.to_string(),
        source: context.source.to_string(),
        span: call.span,
    })
}

fn ffi_type_name_from_expression(
    path: &Path,
    expression: &Expression<'_>,
    bindings: &FfiImportBindings,
) -> Result<String, Diagnostic> {
    let Expression::StaticMemberExpression(member) = expression else {
        return Err(Diagnostic::new(
            "SLOPPY_E_FFI_INVALID_DECLARATION",
            "FFI types must be static properties from the t namespace",
        )
        .with_path(path)
        .with_span(expression.span()));
    };
    let Expression::Identifier(object) = &member.object else {
        return Err(Diagnostic::new(
            "SLOPPY_E_FFI_INVALID_DECLARATION",
            "FFI types must be static properties from the t namespace",
        )
        .with_path(path)
        .with_span(expression.span()));
    };
    if !bindings.type_names.contains(object.name.as_str()) {
        return Err(Diagnostic::new(
            "SLOPPY_E_FFI_INVALID_DECLARATION",
            "FFI type declarations must use the imported t namespace",
        )
        .with_path(path)
        .with_span(expression.span()));
    }
    let name = member.property.name.as_str();
    if !ffi_type_supported(name) {
        return Err(Diagnostic::new(
            "SLOPPY_E_FFI_INVALID_DECLARATION",
            format!("unsupported FFI type \"{name}\""),
        )
        .with_path(path)
        .with_span(expression.span()));
    }
    Ok(name.to_string())
}

fn ffi_type_descriptor_from_expression(
    path: &Path,
    expression: &Expression<'_>,
    bindings: &FfiImportBindings,
) -> Result<(String, Option<serde_json::Value>), Diagnostic> {
    if let Expression::StaticMemberExpression(member) = expression {
        if let Expression::Identifier(object) = &member.object {
            if bindings.type_names.contains(object.name.as_str()) {
                let type_name = ffi_type_name_from_expression(path, expression, bindings)?;
                return Ok((type_name, None));
            }
        }
    }
    if let Expression::StaticMemberExpression(member) = expression {
        if member.property.name.as_str() == "owned" {
            if let Expression::Identifier(object) = &member.object {
                let Some(handle_name) = bindings.handle_names.get(object.name.as_str()) else {
                    return Err(Diagnostic::new(
                        "SLOPPY_E_FFI_INVALID_DECLARATION",
                        "owned FFI handle descriptors must reference a static unsafeFfi.handle declaration",
                    )
                    .with_path(path)
                    .with_span(expression.span()));
                };
                return Ok((
                    "ptr".to_string(),
                    Some(json!({
                        "kind": "handle",
                        "name": handle_name,
                        "owned": true,
                        "type": "ptr"
                    })),
                ));
            }
        }
    }
    if let Expression::Identifier(identifier) = expression {
        if let Some(handle_name) = bindings.handle_names.get(identifier.name.as_str()) {
            return Ok((
                "ptr".to_string(),
                Some(json!({
                    "kind": "handle",
                    "name": handle_name,
                    "owned": false,
                    "type": "ptr"
                })),
            ));
        }
        if let Some(callback_name) = bindings.callback_names.get(identifier.name.as_str()) {
            return Ok((
                "ptr".to_string(),
                Some(json!({
                    "kind": "callback",
                    "name": callback_name,
                    "type": "ptr"
                })),
            ));
        }
        return Err(Diagnostic::new(
            "SLOPPY_E_FFI_INVALID_DECLARATION",
            "FFI identifier descriptors must reference static unsafeFfi.handle or unsafeFfi.callback declarations",
        )
        .with_path(path)
        .with_span(expression.span()));
    }
    Err(Diagnostic::new(
        "SLOPPY_E_FFI_INVALID_DECLARATION",
        "FFI declarations must use static t aliases, handle descriptors, or callback descriptors",
    )
    .with_path(path)
    .with_span(expression.span()))
}

fn ffi_struct_field_descriptor_from_expression(
    path: &Path,
    expression: &Expression<'_>,
    bindings: &FfiImportBindings,
) -> Result<(String, Option<serde_json::Value>), Diagnostic> {
    if let Expression::CallExpression(call) = expression {
        if let Some((member, _)) = ffi_static_member_call(expression, &bindings.ffi_names) {
            if member == "array" {
                let Some(element) = call.arguments.first().and_then(Argument::as_expression) else {
                    return Err(ffi_dynamic_declaration_diag(
                        path,
                        call.span,
                        "unsafeFfi.array requires a static element type",
                    ));
                };
                let element_type = ffi_type_name_from_expression(path, element, bindings)?;
                let Some(length_expression) =
                    call.arguments.get(1).and_then(Argument::as_expression)
                else {
                    return Err(ffi_dynamic_declaration_diag(
                        path,
                        call.span,
                        "unsafeFfi.array requires a static positive length",
                    ));
                };
                let Expression::NumericLiteral(length) = length_expression else {
                    return Err(ffi_dynamic_declaration_diag(
                        path,
                        call.span,
                        "unsafeFfi.array requires a static positive length",
                    ));
                };
                if length.value.fract() != 0.0 || length.value <= 0.0 {
                    return Err(ffi_dynamic_declaration_diag(
                        path,
                        call.span,
                        "unsafeFfi.array length must be a positive integer",
                    ));
                }
                return Ok((
                    format!("{element_type}[{}]", length.value as u32),
                    Some(json!({
                        "kind": "array",
                        "element": element_type,
                        "length": length.value as u32
                    })),
                ));
            }
        }
    }
    if let Expression::Identifier(identifier) = expression {
        let Some(struct_name) = bindings.struct_names.get(identifier.name.as_str()) else {
            return Err(Diagnostic::new(
                "SLOPPY_E_FFI_INVALID_DECLARATION",
                "nested FFI struct fields must reference a static unsafeFfi.struct declaration",
            )
            .with_path(path)
            .with_span(expression.span()));
        };
        return Ok((
            struct_name.clone(),
            Some(json!({
                "kind": "struct",
                "name": struct_name
            })),
        ));
    }
    ffi_type_descriptor_from_expression(path, expression, bindings)
}

fn ffi_type_supported(name: &str) -> bool {
    matches!(
        name,
        "void"
            | "bool"
            | "bool32"
            | "i8"
            | "u8"
            | "i16"
            | "u16"
            | "i32"
            | "u32"
            | "i64"
            | "u64"
            | "isize"
            | "usize"
            | "f32"
            | "f64"
            | "ptr"
            | "handle"
            | "hwnd"
            | "hmodule"
            | "ntstatus"
            | "cstring"
            | "lpcstr"
            | "utf16"
            | "lpcwstr"
            | "bytes"
            | "mutBytes"
    )
}

fn ffi_return_type_unsupported(name: &str) -> bool {
    matches!(
        name,
        "cstring" | "lpcstr" | "utf16" | "lpcwstr" | "bytes" | "mutBytes"
    )
}

fn ffi_struct_field_type_supported(name: &str) -> bool {
    matches!(
        name,
        "bool"
            | "bool32"
            | "i8"
            | "u8"
            | "i16"
            | "u16"
            | "i32"
            | "u32"
            | "i64"
            | "u64"
            | "isize"
            | "usize"
            | "f32"
            | "f64"
            | "ptr"
            | "handle"
            | "hwnd"
            | "hmodule"
            | "ntstatus"
    )
}

fn ffi_callback_return_type_supported(name: &str) -> bool {
    matches!(name, "void" | "i32" | "u32")
}

fn ffi_callback_parameter_type_supported(name: &str) -> bool {
    matches!(name, "i32" | "u32")
}

fn ffi_dispatch_return_type_supported(name: &str) -> bool {
    matches!(name, "void" | "i32" | "u32" | "ptr")
}

fn ffi_dispatch_parameter_type_supported(name: &str) -> bool {
    matches!(name, "i32" | "u32" | "ptr")
}

fn validate_ffi_dispatch_symbol(
    context: &FfiSourceContext<'_>,
    symbol: &FfiFunctionMetadata,
    span: Span,
) -> Result<(), Diagnostic> {
    if !ffi_dispatch_return_type_supported(&symbol.return_type) {
        return Err(Diagnostic::new(
            "SLOPPY_E_FFI_UNSUPPORTED_TYPE",
            format!(
                "unsupported FFI dispatch-table return type \"{}\"",
                symbol.return_type
            ),
        )
        .with_path(context.path)
        .with_span(span)
        .with_hint("Dispatch-table symbols currently support void, i32, u32, and ptr returns."));
    }
    for parameter in &symbol.parameters {
        if !ffi_dispatch_parameter_type_supported(parameter) {
            return Err(Diagnostic::new(
                "SLOPPY_E_FFI_UNSUPPORTED_TYPE",
                format!("unsupported FFI dispatch-table parameter type \"{parameter}\""),
            )
            .with_path(context.path)
            .with_span(span)
            .with_hint("Dispatch-table symbols currently support i32, u32, and ptr parameters."));
        }
    }
    Ok(())
}

fn validate_ffi_convention(path: &Path, span: Span, convention: &str) -> Result<(), Diagnostic> {
    if matches!(convention, "system" | "cdecl" | "stdcall") {
        return Ok(());
    }
    Err(Diagnostic::new(
        "SLOPPY_E_FFI_UNSUPPORTED_CALLING_CONVENTION",
        format!("unsupported FFI calling convention \"{convention}\""),
    )
    .with_path(path)
    .with_span(span)
    .with_hint("Supported FFI conventions are system, cdecl, and stdcall."))
}

fn ffi_options_object<'a>(
    context: &FfiSourceContext<'_>,
    call: &'a CallExpression<'a>,
    index: usize,
    operation: &str,
) -> Result<Option<&'a oxc_ast::ast::ObjectExpression<'a>>, Diagnostic> {
    let Some(argument) = call.arguments.get(index) else {
        return Ok(None);
    };
    object_argument(argument).map(Some).ok_or_else(|| {
        ffi_dynamic_declaration_diag(
            context.path,
            call.span,
            format!("{operation} options must be a static object literal"),
        )
    })
}

fn ffi_string_option<'a>(
    context: &FfiSourceContext<'_>,
    span: Span,
    object: &'a oxc_ast::ast::ObjectExpression<'a>,
    name: &str,
) -> Result<Option<&'a str>, Diagnostic> {
    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(ffi_dynamic_declaration_diag(
                context.path,
                span,
                "FFI options do not support spreads",
            ));
        };
        if property.computed || property_key_name(&property.key) != Some(name) {
            continue;
        }
        let Expression::StringLiteral(value) = &property.value else {
            return Err(ffi_dynamic_declaration_diag(
                context.path,
                property.span,
                format!("FFI option \"{name}\" must be a string literal"),
            ));
        };
        return Ok(Some(value.value.as_str()));
    }
    Ok(None)
}

fn ffi_bool_option(
    context: &FfiSourceContext<'_>,
    span: Span,
    object: &oxc_ast::ast::ObjectExpression<'_>,
    name: &str,
) -> Result<Option<bool>, Diagnostic> {
    for property in &object.properties {
        let ObjectPropertyKind::ObjectProperty(property) = property else {
            return Err(ffi_dynamic_declaration_diag(
                context.path,
                span,
                "FFI options do not support spreads",
            ));
        };
        if property.computed || property_key_name(&property.key) != Some(name) {
            continue;
        }
        let Expression::BooleanLiteral(value) = &property.value else {
            return Err(ffi_dynamic_declaration_diag(
                context.path,
                property.span,
                format!("FFI option \"{name}\" must be a boolean literal"),
            ));
        };
        return Ok(Some(value.value));
    }
    Ok(None)
}

fn ffi_dynamic_declaration_diag(path: &Path, span: Span, message: impl Into<String>) -> Diagnostic {
    Diagnostic::new("SLOPPY_E_FFI_INVALID_DECLARATION", message)
        .with_path(path)
        .with_span(span)
        .with_hint("FFI declarations must be static so the compiler can emit Plan metadata.")
}

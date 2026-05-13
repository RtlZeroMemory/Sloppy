// Relative helper and function-module extraction.
use super::*;

#[derive(Debug, Clone)]
pub(super) struct ImportedHelper {
    pub(super) name: String,
    pub(super) source: String,
    pub(super) summary: FunctionEffectSummary,
}

pub(super) fn extract_relative_helper_import(
    graph: &mut ModuleGraph,
    imported: &ImportedModule,
) -> Result<Vec<ImportedHelper>, Diagnostic> {
    if graph.visiting.contains(&imported.path) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_CIRCULAR_IMPORT",
            "circular relative imports are not supported",
        )
        .with_path(&imported.path)
        .with_span(imported.span)
        .with_hint("Keep relative helper modules acyclic."));
    }

    graph.visiting.insert(imported.path.clone());
    let source = fs::read_to_string(&imported.path).map_err(|error| {
        Diagnostic::new(
            "SLOPPYC_E_INPUT",
            format!("failed to read imported module: {error}"),
        )
        .with_path(&imported.path)
    })?;
    let source_name = graph.record_source(&imported.path, &source);
    graph.noncrypto_hash_security_context_visible |=
        noncrypto_hash_security_context_visible(&source);
    graph.checksum_security_context_visible |= checksum_security_context_visible(&source);
    let source_type = source_type_for_path(&imported.path, ParseContext::Module)?;
    let allocator = Allocator::default();
    let parsed = Parser::new(&allocator, &source, source_type).parse();
    if let Some(error) = parsed.errors.into_iter().next() {
        graph.visiting.remove(&imported.path);
        return Err(Diagnostic::new(
            "SLOPPYC_E_PARSE",
            format!("failed to parse module: {error}"),
        )
        .with_path(&imported.path));
    }

    let mut helpers = Vec::<ImportedHelper>::new();
    let mut helper_sources = BTreeMap::<String, String>::new();
    let mut helper_effects = BTreeMap::<String, FunctionEffectSummary>::new();

    for statement in &parsed.program.body {
        let Statement::ImportDeclaration(import) = statement else {
            continue;
        };
        let import_source = import.source.value.as_str();
        if import_source == "sloppy" {
            validate_module_sloppy_root_import(&imported.path, import)?;
            mark_sloppy_root_runtime_usage(graph, import);
            continue;
        }
        if import_source == "sloppy/time" {
            validate_module_sloppy_time_import(&imported.path, import)?;
            if import_has_runtime_value_specifier(import) {
                graph.uses_time_runtime = true;
            }
            continue;
        }
        if import_source == "sloppy/data" {
            validate_module_sloppy_data_import(&imported.path, import)?;
            if import_has_runtime_value_specifier(import) {
                mark_sloppy_data_runtime_usage(graph, import);
            }
            continue;
        }
        if import_source == "sloppy/fs" {
            validate_module_sloppy_fs_import(&imported.path, import)?;
            if import_has_runtime_value_specifier(import) {
                graph.uses_fs_runtime = true;
            }
            continue;
        }
        if import_source == "sloppy/crypto" {
            validate_module_sloppy_crypto_import(&imported.path, import)?;
            if import_has_runtime_value_specifier(import) {
                graph.uses_crypto_runtime = true;
            }
            continue;
        }
        if import_source == "sloppy/codec" {
            validate_module_sloppy_codec_import(&imported.path, import)?;
            if import_has_runtime_value_specifier(import) {
                graph.uses_codec_runtime = true;
            }
            continue;
        }
        if import_source == "sloppy/cache" {
            validate_module_sloppy_cache_import(&imported.path, import)?;
            if import_has_runtime_value_specifier(import) {
                graph.uses_cache_runtime = true;
            }
            continue;
        }
        if import_source == "sloppy/redis" {
            validate_module_sloppy_redis_import(&imported.path, import)?;
            if import_has_runtime_value_specifier(import) {
                graph.uses_redis_runtime = true;
                graph.uses_net_runtime = true;
            }
            continue;
        }
        if import_source == "sloppy/net" {
            validate_module_sloppy_net_import(&imported.path, import)?;
            if let Some(specifiers) = &import.specifiers {
                for specifier in specifiers {
                    if let ImportDeclarationSpecifier::ImportSpecifier(specifier) = specifier {
                        if import_specifier_is_runtime_value(import, specifier) {
                            mark_sloppy_net_runtime_usage(
                                &mut graph.uses_net_runtime,
                                &mut graph.uses_http_client_runtime,
                                specifier.imported.name().as_str(),
                            );
                        }
                    }
                }
            }
            continue;
        }
        if import_source == "sloppy/http" {
            validate_module_sloppy_http_import(&imported.path, import)?;
            if import_has_runtime_value_specifier(import) {
                graph.uses_http_client_runtime = true;
            }
            continue;
        }
        if import_source == "sloppy/os" {
            validate_module_sloppy_os_import(&imported.path, import)?;
            if import_has_runtime_value_specifier(import) {
                graph.uses_os_runtime = true;
            }
            continue;
        }
        if import_source == "sloppy/workers" {
            validate_module_sloppy_workers_import(&imported.path, import)?;
            if import_has_runtime_value_specifier(import) {
                graph.uses_workers_runtime = true;
            }
            continue;
        }
        if import_source == "sloppy/ffi" {
            validate_module_sloppy_ffi_import(&imported.path, import)?;
            if import_has_runtime_value_specifier(import) {
                graph.uses_ffi_runtime = true;
            }
            continue;
        }
        if import_source == "sloppy/providers/sqlite" {
            continue;
        }
        if import_source.starts_with("./") || import_source.starts_with("../") {
            let nested =
                resolve_relative_import(&imported.path, import_source).ok_or_else(|| {
                    Diagnostic::new(
                        "SLOPPYC_E_MISSING_RELATIVE_IMPORT",
                        format!("relative import \"{import_source}\" could not be resolved"),
                    )
                    .with_path(&imported.path)
                    .with_span(import.source.span)
                })?;
            if !resolver::stays_within_source_root(&nested, &graph.entry_dir) {
                graph.visiting.remove(&imported.path);
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_RELATIVE_IMPORT",
                    "relative imports must stay within the source root",
                )
                .with_path(&imported.path)
                .with_span(import.source.span));
            }
            graph.add_relative_dependency_import(&imported.path, import_source, &nested);
            if let Some(specifiers) = &import.specifiers {
                for specifier in specifiers {
                    let ImportDeclarationSpecifier::ImportSpecifier(specifier) = specifier else {
                        graph.visiting.remove(&imported.path);
                        return Err(Diagnostic::new(
                            "SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER",
                            format!("unsupported import specifier \"{import_source}\""),
                        )
                        .with_path(&imported.path)
                        .with_span(import.source.span));
                    };
                    let nested_import = ImportedModule {
                        local_name: specifier.local.name.as_str().to_string(),
                        export_name: specifier.imported.name().as_str().to_string(),
                        path: nested.clone(),
                        span: specifier.span,
                    };
                    for helper in extract_relative_helper_import(graph, &nested_import)? {
                        helper_sources
                            .entry(helper.name.clone())
                            .or_insert(helper.source.clone());
                        helper_effects
                            .entry(helper.name.clone())
                            .or_insert(helper.summary.clone());
                        helpers.push(helper);
                    }
                    resolve_helper_effect_callgraph(&mut helper_effects);
                }
            }
            continue;
        }
        if let resolver::ImportKind::Package(package_resolution) =
            resolver::classify_import(&imported.path, import_source)
        {
            graph.add_package_record(&package_resolution);
            let package_id = program_module_id(
                graph,
                &package_resolution.entry,
                Some(&package_resolution.name),
            );
            graph.add_dependency_module(
                package_id.clone(),
                package_id.clone(),
                package_resolution.format,
                Some(package_resolution.name.clone()),
                Some(resolver::normalized_artifact_id(
                    &imported.path,
                    &graph.entry_dir,
                )),
            );
            graph.add_package_dependency_import(&imported.path, import_source, &package_id);
            if let Some(specifiers) = &import.specifiers {
                for specifier in specifiers {
                    let ImportDeclarationSpecifier::ImportSpecifier(specifier) = specifier else {
                        graph.visiting.remove(&imported.path);
                        return Err(Diagnostic::new(
                            "SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER",
                            format!("unsupported import specifier \"{import_source}\""),
                        )
                        .with_path(&imported.path)
                        .with_span(import.source.span));
                    };
                    let package_import = ImportedModule {
                        local_name: specifier.local.name.as_str().to_string(),
                        export_name: specifier.imported.name().as_str().to_string(),
                        path: package_resolution.entry.clone(),
                        span: specifier.span,
                    };
                    for helper in extract_relative_helper_import(graph, &package_import)? {
                        helper_sources
                            .entry(helper.name.clone())
                            .or_insert(helper.source.clone());
                        helper_effects
                            .entry(helper.name.clone())
                            .or_insert(helper.summary.clone());
                        helpers.push(helper);
                    }
                    resolve_helper_effect_callgraph(&mut helper_effects);
                }
            }
            continue;
        }
        graph.visiting.remove(&imported.path);
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER",
            format!("unsupported import specifier \"{import_source}\""),
        )
        .with_path(&imported.path)
        .with_span(import.source.span));
    }

    let mut found = None::<ImportedHelper>;
    for statement in &parsed.program.body {
        let Statement::ExportNamedDeclaration(export) = statement else {
            continue;
        };
        match &export.declaration {
            Some(Declaration::FunctionDeclaration(function)) => {
                let Some(identifier) = &function.id else {
                    continue;
                };
                let export_name = identifier.name.as_str();
                if export_name != imported.export_name {
                    continue;
                }
                let Some(source_text) = source_slice(&source, function.span) else {
                    graph.visiting.remove(&imported.path);
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_HELPER",
                        "helper source could not be extracted",
                    )
                    .with_path(&imported.path)
                    .with_span(function.span));
                };
                let summary = helper_effects_from_function(
                    function,
                    &BTreeMap::new(),
                    &helper_effects,
                    &source,
                    &source_name,
                );
                found = Some(imported_helper_with_alias(
                    imported,
                    export_name,
                    source_text,
                    summary,
                ));
                break;
            }
            Some(Declaration::VariableDeclaration(declaration)) => {
                for declarator in &declaration.declarations {
                    let Some(export_name) = binding_identifier(&declarator.id) else {
                        continue;
                    };
                    if export_name != imported.export_name {
                        continue;
                    }
                    let Some(init) = &declarator.init else {
                        continue;
                    };
                    let Some(init_source) = source_slice(&source, init.span()) else {
                        graph.visiting.remove(&imported.path);
                        return Err(Diagnostic::new(
                            "SLOPPYC_E_UNSUPPORTED_HELPER",
                            "helper source could not be extracted",
                        )
                        .with_path(&imported.path)
                        .with_span(init.span()));
                    };
                    let source_text = format!("const {export_name} = {init_source};");
                    let summary = helper_effects_from_initializer(
                        init,
                        &BTreeMap::new(),
                        &helper_effects,
                        &source,
                        &source_name,
                    );
                    found = Some(imported_helper_with_alias(
                        imported,
                        export_name,
                        source_text,
                        summary,
                    ));
                    break;
                }
            }
            _ => {}
        }
        if found.is_some() {
            break;
        }
    }

    graph.visiting.remove(&imported.path);
    let Some(helper) = found else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_MISSING_EXPORT",
            format!(
                "imported module does not export \"{}\"",
                imported.export_name
            ),
        )
        .with_path(&imported.path)
        .with_span(imported.span));
    };
    helpers.push(helper);
    Ok(helpers)
}

fn imported_helper_with_alias(
    imported: &ImportedModule,
    export_name: &str,
    mut source: String,
    summary: FunctionEffectSummary,
) -> ImportedHelper {
    if imported.local_name != export_name {
        source.push_str(&format!("\nconst {} = {export_name};", imported.local_name));
    }
    ImportedHelper {
        name: imported.local_name.clone(),
        source,
        summary,
    }
}

pub(super) fn extract_relative_module(
    graph: &mut ModuleGraph,
    imported: &ImportedModule,
    mut metrics: Option<&mut CompileMetrics>,
) -> Result<CachedModuleExport, Diagnostic> {
    if graph.visiting.contains(&imported.path) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_CIRCULAR_IMPORT",
            "circular relative imports are not supported",
        )
        .with_path(&imported.path)
        .with_span(imported.span)
        .with_hint(
            "Keep function modules acyclic for the current function-module compiler contract.",
        ));
    }
    if let Some(module) = graph.modules.get(&imported.path) {
        return cached_module_routes(module, imported);
    }
    graph.visiting.insert(imported.path.clone());

    let read_start = Instant::now();
    let source = fs::read_to_string(&imported.path).map_err(|error| {
        Diagnostic::new(
            "SLOPPYC_E_INPUT",
            format!("failed to read imported module: {error}"),
        )
        .with_path(&imported.path)
    })?;
    if let Some(metrics) = metrics.as_mut() {
        metrics.add_phase("readInputMs", read_start.elapsed());
    }
    let source_name = graph.record_source(&imported.path, &source);
    graph.noncrypto_hash_security_context_visible |=
        noncrypto_hash_security_context_visible(&source);
    graph.checksum_security_context_visible |= checksum_security_context_visible(&source);
    let source_type = source_type_for_path(&imported.path, ParseContext::Module)?;
    let parse_start = Instant::now();
    let allocator = Allocator::default();
    let parsed = Parser::new(&allocator, &source, source_type).parse();
    if let Some(metrics) = metrics.as_mut() {
        metrics.add_phase("parseModulesMs", parse_start.elapsed());
    }
    if let Some(error) = parsed.errors.into_iter().next() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_PARSE",
            format!("failed to parse module: {error}"),
        )
        .with_path(&imported.path));
    }
    extract_ffi_declarations_from_statements(
        &imported.path,
        &source,
        &source_name,
        &parsed.program.body,
        &mut graph.ffi_libraries,
        &mut graph.ffi_structs,
    )?;

    let mut exports = BTreeMap::<String, CachedModuleExport>::new();
    let mut duplicate_exports = BTreeSet::<String>::new();
    let extract_start = Instant::now();
    let mut module_results_imported = false;
    let mut imported_helper_sources = BTreeMap::<String, String>::new();
    let mut imported_helper_effects = BTreeMap::<String, FunctionEffectSummary>::new();
    for statement in &parsed.program.body {
        let Statement::ImportDeclaration(import) = statement else {
            continue;
        };
        let import_source = import.source.value.as_str();
        if import_source == "sloppy" {
            module_results_imported |= validate_module_sloppy_root_import(&imported.path, import)?;
            mark_sloppy_root_runtime_usage(graph, import);
            continue;
        }
        if import_source == "sloppy/time" {
            validate_module_sloppy_time_import(&imported.path, import)?;
            if import_has_runtime_value_specifier(import) {
                graph.uses_time_runtime = true;
            }
            continue;
        }
        if import_source == "sloppy/data" {
            validate_module_sloppy_data_import(&imported.path, import)?;
            if import_has_runtime_value_specifier(import) {
                mark_sloppy_data_runtime_usage(graph, import);
            }
            continue;
        }
        if import_source == "sloppy/crypto" {
            validate_module_sloppy_crypto_import(&imported.path, import)?;
            if import_has_runtime_value_specifier(import) {
                graph.uses_crypto_runtime = true;
            }
            continue;
        }
        if import_source == "sloppy/codec" {
            validate_module_sloppy_codec_import(&imported.path, import)?;
            if import_has_runtime_value_specifier(import) {
                graph.uses_codec_runtime = true;
            }
            continue;
        }
        if import_source == "sloppy/cache" {
            validate_module_sloppy_cache_import(&imported.path, import)?;
            if import_has_runtime_value_specifier(import) {
                graph.uses_cache_runtime = true;
            }
            continue;
        }
        if import_source == "sloppy/redis" {
            validate_module_sloppy_redis_import(&imported.path, import)?;
            if import_has_runtime_value_specifier(import) {
                graph.uses_redis_runtime = true;
                graph.uses_net_runtime = true;
            }
            continue;
        }
        if import_source == "sloppy/net" {
            validate_module_sloppy_net_import(&imported.path, import)?;
            if let Some(specifiers) = &import.specifiers {
                for specifier in specifiers {
                    if let ImportDeclarationSpecifier::ImportSpecifier(specifier) = specifier {
                        if import_specifier_is_runtime_value(import, specifier) {
                            let imported_name = specifier.imported.name().as_str();
                            mark_sloppy_net_runtime_usage(
                                &mut graph.uses_net_runtime,
                                &mut graph.uses_http_client_runtime,
                                imported_name,
                            );
                        }
                    }
                }
            }
            continue;
        }
        if import_source == "sloppy/http" {
            validate_module_sloppy_http_import(&imported.path, import)?;
            if import_has_runtime_value_specifier(import) {
                graph.uses_http_client_runtime = true;
            }
            continue;
        }
        if import_source == "sloppy/os" {
            validate_module_sloppy_os_import(&imported.path, import)?;
            if import_has_runtime_value_specifier(import) {
                graph.uses_os_runtime = true;
            }
            continue;
        }
        if import_source == "sloppy/workers" {
            validate_module_sloppy_workers_import(&imported.path, import)?;
            if import_has_runtime_value_specifier(import) {
                graph.uses_workers_runtime = true;
            }
            continue;
        }
        if import_source == "sloppy/ffi" {
            validate_module_sloppy_ffi_import(&imported.path, import)?;
            if import_has_runtime_value_specifier(import) {
                graph.uses_ffi_runtime = true;
            }
            continue;
        }
        if import_source.starts_with("./") || import_source.starts_with("../") {
            let nested =
                resolve_relative_import(&imported.path, import_source).ok_or_else(|| {
                    Diagnostic::new(
                        "SLOPPYC_E_MISSING_RELATIVE_IMPORT",
                        format!("relative import \"{import_source}\" could not be resolved"),
                    )
                    .with_path(&imported.path)
                    .with_span(import.source.span)
                })?;
            if graph.visiting.contains(&nested) {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_CIRCULAR_IMPORT",
                    "circular relative imports are not supported",
                )
                .with_path(&imported.path)
                .with_span(import.source.span));
            }
            if !resolver::stays_within_source_root(&nested, &graph.entry_dir) {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_RELATIVE_IMPORT",
                    "relative imports must stay within the source root",
                )
                .with_path(&imported.path)
                .with_span(import.source.span));
            }
            graph.add_relative_dependency_import(&imported.path, import_source, &nested);
            if let Some(specifiers) = &import.specifiers {
                for specifier in specifiers {
                    let ImportDeclarationSpecifier::ImportSpecifier(specifier) = specifier else {
                        return Err(Diagnostic::new(
                            "SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER",
                            format!("unsupported import specifier \"{import_source}\""),
                        )
                        .with_path(&imported.path)
                        .with_span(import.source.span));
                    };
                    let nested_import = ImportedModule {
                        local_name: specifier.local.name.as_str().to_string(),
                        export_name: specifier.imported.name().as_str().to_string(),
                        path: nested.clone(),
                        span: specifier.span,
                    };
                    for helper in extract_relative_helper_import(graph, &nested_import)? {
                        imported_helper_sources
                            .entry(helper.name.clone())
                            .or_insert(helper.source);
                        imported_helper_effects
                            .entry(helper.name)
                            .or_insert(helper.summary);
                    }
                    resolve_helper_effect_callgraph(&mut imported_helper_effects);
                }
            }
            continue;
        }
        match resolver::classify_import(&imported.path, import_source) {
            resolver::ImportKind::Package(package_resolution) => {
                graph.add_package_record(&package_resolution);
                let package_id = program_module_id(
                    graph,
                    &package_resolution.entry,
                    Some(&package_resolution.name),
                );
                graph.add_dependency_module(
                    package_id.clone(),
                    package_id.clone(),
                    package_resolution.format,
                    Some(package_resolution.name.clone()),
                    Some(resolver::normalized_artifact_id(
                        &imported.path,
                        &graph.entry_dir,
                    )),
                );
                graph.add_package_dependency_import(&imported.path, import_source, &package_id);
                if let Some(specifiers) = &import.specifiers {
                    for specifier in specifiers {
                        let ImportDeclarationSpecifier::ImportSpecifier(specifier) = specifier
                        else {
                            return Err(Diagnostic::new(
                                "SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER",
                                format!("unsupported import specifier \"{import_source}\""),
                            )
                            .with_path(&imported.path)
                            .with_span(import.source.span));
                        };
                        let package_import = ImportedModule {
                            local_name: specifier.local.name.as_str().to_string(),
                            export_name: specifier.imported.name().as_str().to_string(),
                            path: package_resolution.entry.clone(),
                            span: specifier.span,
                        };
                        for helper in extract_relative_helper_import(graph, &package_import)? {
                            imported_helper_sources
                                .entry(helper.name.clone())
                                .or_insert(helper.source);
                            imported_helper_effects
                                .entry(helper.name)
                                .or_insert(helper.summary);
                        }
                        resolve_helper_effect_callgraph(&mut imported_helper_effects);
                    }
                }
                continue;
            }
            resolver::ImportKind::NativeAddonUnsupported(package_resolution) => {
                graph.add_package_record(&package_resolution);
            }
            _ => {}
        }
        if import_source != "sloppy/providers/sqlite" {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER",
                format!("unsupported import specifier \"{import_source}\""),
            )
            .with_path(&imported.path)
            .with_span(import.source.span));
        }
    }

    for statement in &parsed.program.body {
        match statement {
            Statement::FunctionDeclaration(function) => {
                let Some(identifier) = &function.id else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
                        "function module helper declarations must be named",
                    )
                    .with_path(&imported.path)
                    .with_span(function.span));
                };
                let Some(helper_source) = source_slice(&source, function.span) else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_HELPER",
                        "helper source could not be extracted",
                    )
                    .with_path(&imported.path)
                    .with_span(function.span));
                };
                let name = identifier.name.as_str().to_string();
                let summary = helper_effects_from_function(
                    function,
                    &BTreeMap::new(),
                    &imported_helper_effects,
                    &source,
                    &source_name,
                );
                imported_helper_sources.insert(name.clone(), helper_source);
                imported_helper_effects.insert(name, summary);
                resolve_helper_effect_callgraph(&mut imported_helper_effects);
            }
            Statement::VariableDeclaration(declaration) => {
                for declarator in &declaration.declarations {
                    let Some(init) = &declarator.init else {
                        continue;
                    };
                    if helper_initializer(init).is_none() {
                        continue;
                    }
                    let Some(name) = binding_identifier(&declarator.id) else {
                        return Err(Diagnostic::new(
                            "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
                            "function module helper declarations must use simple identifiers",
                        )
                        .with_path(&imported.path)
                        .with_span(declarator.span));
                    };
                    let Some(init_source) = source_slice(&source, init.span()) else {
                        return Err(Diagnostic::new(
                            "SLOPPYC_E_UNSUPPORTED_HELPER",
                            "helper source could not be extracted",
                        )
                        .with_path(&imported.path)
                        .with_span(init.span()));
                    };
                    let helper_source = format!("const {name} = {init_source};");
                    let summary = helper_effects_from_initializer(
                        init,
                        &BTreeMap::new(),
                        &imported_helper_effects,
                        &source,
                        &source_name,
                    );
                    imported_helper_sources.insert(name.to_string(), helper_source);
                    imported_helper_effects.insert(name.to_string(), summary);
                    resolve_helper_effect_callgraph(&mut imported_helper_effects);
                }
            }
            _ => {}
        }
    }

    for statement in &parsed.program.body {
        match statement {
            Statement::ImportDeclaration(_) => {}
            Statement::FunctionDeclaration(_) => {}
            Statement::VariableDeclaration(declaration)
                if module_variable_declaration_is_helper(declaration) => {}
            Statement::ExportNamedDeclaration(export) => {
                let Some(Declaration::FunctionDeclaration(function)) = &export.declaration else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
                        "function modules must export a named function declaration",
                    )
                    .with_path(&imported.path)
                    .with_span(export.span));
                };
                let Some(identifier) = &function.id else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
                        "function module export must be named",
                    )
                    .with_path(&imported.path)
                    .with_span(function.span));
                };
                let export_name = identifier.name.as_str();
                let module_export = extract_module_function_routes(
                    &imported.path,
                    &source,
                    &source_name,
                    export_name,
                    function,
                    &imported_helper_sources,
                    &imported_helper_effects,
                )?;
                if !module_results_imported {
                    if let Some(route) = module_export
                        .routes
                        .iter()
                        .find(|route| route.handler.requires_results_import)
                    {
                        return Err(missing_results_import_diagnostic(
                            &imported.path,
                            route.handler.span,
                        ));
                    }
                }
                if exports
                    .insert(export_name.to_string(), module_export)
                    .is_some()
                {
                    duplicate_exports.insert(export_name.to_string());
                }
            }
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
                    "function module files support imports and named function exports",
                )
                .with_path(&imported.path)
                .with_span(statement.span()));
            }
        }
    }
    if let Some(metrics) = metrics.as_mut() {
        metrics.add_phase("extractMs", extract_start.elapsed());
    }

    graph.visiting.remove(&imported.path);
    let module = CachedModule {
        exports,
        duplicate_exports,
    };
    let routes = cached_module_routes(&module, imported)?;
    graph.modules.insert(imported.path.clone(), module);
    Ok(routes)
}

fn module_variable_declaration_is_helper(declaration: &VariableDeclaration<'_>) -> bool {
    !declaration.declarations.is_empty()
        && declaration.declarations.iter().all(|declarator| {
            binding_identifier(&declarator.id).is_some()
                && declarator
                    .init
                    .as_ref()
                    .is_some_and(|init| helper_initializer(init).is_some())
        })
}

fn cached_module_routes(
    module: &CachedModule,
    imported: &ImportedModule,
) -> Result<CachedModuleExport, Diagnostic> {
    if module.duplicate_exports.contains(&imported.export_name) {
        return Err(Diagnostic::new(
            "SLOPPYC_E_DUPLICATE_EXPORT",
            format!("module exports \"{}\" more than once", imported.export_name),
        )
        .with_path(&imported.path)
        .with_span(imported.span));
    }
    let Some(module_export) = module.exports.get(&imported.export_name) else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_MISSING_EXPORT",
            format!(
                "imported module does not export \"{}\"",
                imported.export_name
            ),
        )
        .with_path(&imported.path)
        .with_span(imported.span));
    };
    Ok(module_export.clone())
}

fn extract_module_function_routes(
    path: &Path,
    source: &str,
    source_name: &str,
    module_name: &str,
    function: &oxc_ast::ast::Function<'_>,
    imported_helper_sources: &BTreeMap<String, String>,
    imported_helper_effects: &BTreeMap<String, FunctionEffectSummary>,
) -> Result<CachedModuleExport, Diagnostic> {
    if function.params.items.len() != 1 || function.params.rest.is_some() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
            "function modules must declare exactly one app parameter",
        )
        .with_path(path)
        .with_span(function.span));
    }
    let Some(app_name) = function
        .params
        .items
        .first()
        .and_then(|parameter| binding_identifier(&parameter.pattern))
    else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
            "function module app parameter must be a simple identifier",
        )
        .with_path(path)
        .with_span(function.span));
    };
    let Some(body) = &function.body else {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
            "function module must have a body",
        )
        .with_path(path)
        .with_span(function.span));
    };

    let mut groups = BTreeMap::<String, RouteGroupState>::new();
    let mut providers = BTreeMap::<String, ProviderBinding>::new();
    let mut helper_sources = imported_helper_sources.clone();
    let mut helper_effects = imported_helper_effects.clone();
    let schema_names = collect_schema_declaration_names(&body.statements);
    let mut schemas = Vec::new();
    let mut routes = Vec::new();

    for statement in &body.statements {
        match statement {
            Statement::FunctionDeclaration(function) => {
                let Some(identifier) = &function.id else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
                        "function module helper declarations must be named",
                    )
                    .with_path(path)
                    .with_span(function.span));
                };
                let Some(helper_source) = source_slice(source, function.span) else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
                        "function module helper source could not be extracted",
                    )
                    .with_path(path)
                    .with_span(function.span));
                };
                let name = identifier.name.as_str().to_string();
                let summary = helper_effects_from_function(
                    function,
                    &providers,
                    &helper_effects,
                    source,
                    source_name,
                );
                helper_sources.insert(name.clone(), helper_source);
                helper_effects.insert(name, summary);
                resolve_helper_effect_callgraph(&mut helper_effects);
            }
            Statement::VariableDeclaration(declaration) => {
                for declarator in &declaration.declarations {
                    let Some(name) = binding_identifier(&declarator.id) else {
                        return Err(Diagnostic::new(
                            "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
                            "function module declarations must use simple identifiers",
                        )
                        .with_path(path)
                        .with_span(declarator.span));
                    };
                    let Some(init) = &declarator.init else {
                        continue;
                    };
                    if let Some(binding) = app_provider_call(init, app_name) {
                        providers.insert(name.to_string(), binding);
                    } else if schema_names.contains(name) {
                        if let Some(schema) = schema_declaration(
                            path,
                            source,
                            source_name,
                            name,
                            init,
                            &schema_names,
                        )? {
                            if let Some(init_source) = source_slice(source, init.span()) {
                                let helper_source = format!("const {name} = {init_source};");
                                helper_sources.insert(name.to_string(), helper_source);
                                helper_effects
                                    .entry(name.to_string())
                                    .or_insert_with(FunctionEffectSummary::default);
                            }
                            schemas.push(schema);
                        }
                    } else if let Some((receiver, prefix, metadata)) = app_group_call(init)? {
                        let full_prefix = if receiver == app_name {
                            prefix.to_string()
                        } else if let Some(parent) = groups.get(receiver) {
                            join_route_patterns(&parent.prefix, prefix)
                        } else {
                            return Err(Diagnostic::new(
                                "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
                                "function module route groups must be created from the module app parameter or another module route group",
                            )
                            .with_path(path)
                            .with_span(init.span()));
                        };
                        let mut tags = if receiver == app_name {
                            Vec::new()
                        } else {
                            groups
                                .get(receiver)
                                .map(|parent| parent.tags.clone())
                                .unwrap_or_default()
                        };
                        let auth = metadata.auth.or_else(|| {
                            groups.get(receiver).and_then(|parent| parent.auth.clone())
                        });
                        tags.extend(metadata.tags);
                        groups.insert(
                            name.to_string(),
                            RouteGroupState {
                                prefix: full_prefix,
                                tags,
                                auth,
                                middleware: Vec::new(),
                            },
                        );
                    } else if helper_initializer(init).is_some() {
                        let Some(init_source) = source_slice(source, init.span()) else {
                            return Err(Diagnostic::new(
                                "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
                                "function module helper source could not be extracted",
                            )
                            .with_path(path)
                            .with_span(init.span()));
                        };
                        let helper_source = format!("const {name} = {init_source};");
                        let summary = helper_effects_from_initializer(
                            init,
                            &providers,
                            &helper_effects,
                            source,
                            source_name,
                        );
                        helper_sources.insert(name.to_string(), helper_source);
                        helper_effects.insert(name.to_string(), summary);
                        resolve_helper_effect_callgraph(&mut helper_effects);
                    }
                }
            }
            Statement::ExpressionStatement(statement) => {
                if let Expression::CallExpression(call) = &statement.expression {
                    if let Some((receiver, property)) = static_member_name(&call.callee) {
                        if groups.contains_key(receiver) {
                            let requirement = match property {
                                "requireAuth" | "requiresAuth" => Some(
                                    auth_requirement_from_call(call)
                                        .map_err(|diagnostic| diagnostic.with_path(path))?,
                                ),
                                "allowAnonymous" => Some(anonymous_auth_requirement()),
                                _ => None,
                            };
                            if let Some(requirement) = requirement {
                                if let Some(group) = groups.get_mut(receiver) {
                                    group.auth = Some(requirement);
                                }
                                continue;
                            }
                        }
                    }
                }
                let mut module_app_state = AppState::new();
                module_app_state.app_vars.insert(app_name.to_string());
                if let Some(mut health_routes) = app_map_health_checks_call(
                    path,
                    source,
                    source_name,
                    &statement.expression,
                    &module_app_state,
                )? {
                    for route in &mut health_routes {
                        route.module = Some(module_name.to_string());
                    }
                    routes.extend(health_routes);
                    continue;
                }
                if let Some(mut health_routes) = app_health_expose_call(
                    path,
                    source,
                    source_name,
                    &statement.expression,
                    &module_app_state,
                )? {
                    for route in &mut health_routes {
                        route.module = Some(module_name.to_string());
                    }
                    routes.extend(health_routes);
                    continue;
                }
                if let Some(mut management_routes) = app_management_call(
                    path,
                    source,
                    source_name,
                    &statement.expression,
                    &module_app_state,
                )? {
                    for route in &mut management_routes {
                        route.module = Some(module_name.to_string());
                    }
                    routes.extend(management_routes);
                    continue;
                }
                let (route_expr, fluent_metadata) = route_metadata_chain(&statement.expression)
                    .map_err(|diagnostic| diagnostic.with_path(path))?;
                let static_strings = StaticStringEnv::default();
                let Some((receiver, method, kind, pattern, route_metadata, handler_arg)) =
                    route_call_parts(route_expr, source, &static_strings)
                        .map_err(|diagnostic| diagnostic.with_path(path))?
                else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
                        "function modules support provider lookup, route groups, and literal routes only",
                    )
                    .with_path(path)
                    .with_span(statement.span));
                };
                let (full_pattern, mut tags, inherited_auth) = if receiver == app_name {
                    (pattern.clone(), Vec::new(), None)
                } else if let Some(group) = groups.get(receiver) {
                    (
                        join_route_patterns(&group.prefix, &pattern),
                        group.tags.clone(),
                        group.auth.clone(),
                    )
                } else {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
                        "module route must be registered on the module app parameter or a group created from it",
                    )
                    .with_path(path)
                    .with_span(statement.span));
                };
                let normalized_pattern = normalize_framework_route_pattern(&full_pattern);
                if !route_pattern_supported(&normalized_pattern) {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_UNSUPPORTED_ROUTE_PATTERN",
                        "route pattern is outside the Plan v1 alpha route syntax",
                    )
                    .with_path(path)
                    .with_span(statement.span)
                    .with_hint("Use '/', static segments, {name}, {name:str}, or {name:int}."));
                }

                let handler_context = HandlerExtractionContext {
                    route_pattern: &full_pattern,
                    route_kind: kind,
                    source,
                    source_name,
                    allow_data_handler_body: !providers.is_empty(),
                    schema_names: &schema_names,
                    provider_bindings: &providers,
                    helper_effects: &helper_effects,
                };
                validate_handler_body_validate_schema_references(path, handler_arg, &schema_names)?;
                let Some(mut handler) = handler_from_argument(handler_arg, &handler_context) else {
                    return Err(handler_diagnostic(
                        path,
                        handler_arg,
                        &full_pattern,
                        &schema_names,
                        statement.span,
                    ));
                };
                wrap_realtime_handler(&mut handler, kind, &route_metadata);

                let referenced_helper_sources =
                    helper_sources_referenced_by_handler(&handler.emitted_source, &helper_sources);
                if !handler.effects.is_empty()
                    || !handler_context.helper_effects.is_empty()
                    || !referenced_helper_sources.is_empty()
                {
                    let used_providers = providers_used_by_effects(&providers, &handler.effects);
                    if let Some((name, binding)) = used_providers
                        .iter()
                        .find(|(_, binding)| !provider_has_generated_runtime_bridge(binding))
                    {
                        return Err(Diagnostic::new(
                            "SLOPPYC_E_UNSUPPORTED_PROVIDER_BRIDGE",
                            format!(
                                "{} provider-backed handlers are not executable by the compiler-generated runtime bridge yet",
                                binding.provider
                            ),
                        )
                        .with_path(path)
                        .with_span(statement.span)
                        .with_hint(format!(
                            "Provider handle '{name}' is recognized for Plan metadata, but only the SQLite generated bridge is executable by the current compiler/runtime contract."
                        )));
                    }
                    handler.emitted_source = wrap_handler_with_providers_and_helpers(
                        &handler.emitted_source,
                        &used_providers,
                        &referenced_helper_sources,
                        handler.is_async,
                    );
                }
                apply_route_schema_metadata(
                    path,
                    statement.span,
                    &schema_names,
                    &fluent_metadata,
                    &mut handler,
                )?;
                let contract_metadata = merged_route_metadata(&route_metadata, &fluent_metadata);
                tags.extend(contract_metadata.tags.clone());
                let auth = contract_metadata.auth.clone().or(inherited_auth);
                if let Some(requirement) = &auth {
                    if requirement.required {
                        force_auth_required_route_v8_dispatch(&mut handler);
                        ensure_auth_required_route_request_context(&mut handler);
                    }
                    handler.emitted_source =
                        wrap_handler_with_auth(&handler.emitted_source, requirement);
                    handler.is_async = true;
                }
                routes.push(Route {
                    method,
                    kind,
                    websocket: contract_metadata.websocket.clone(),
                    realtime: realtime_route_metadata_from_contract(&contract_metadata),
                    framework_path: (normalized_pattern != full_pattern).then_some(full_pattern),
                    pattern: normalized_pattern,
                    name: contract_metadata.name.clone(),
                    tags,
                    summary: contract_metadata.summary.clone(),
                    description: contract_metadata.description.clone(),
                    deprecated: contract_metadata.deprecated.clone(),
                    consumes: contract_metadata.consumes.clone(),
                    produces: contract_metadata.produces.clone(),
                    headers: contract_metadata.headers.clone(),
                    query_schema: contract_metadata.query_schema.clone(),
                    params_schema: contract_metadata.params_schema.clone(),
                    openapi_override: contract_metadata.openapi_override.clone(),
                    output_cache: contract_metadata.output_cache.clone(),
                    cache_headers: contract_metadata.cache_headers.clone(),
                    rate_limits: contract_metadata.rate_limits.clone(),
                    docs: None,
                    health: None,
                    middleware: Vec::new(),
                    auth,
                    cors: None,
                    cors_preflight: false,
                    span: statement.span,
                    source_path: path.to_path_buf(),
                    source_name: source_name.to_string(),
                    source: source.to_string(),
                    module: Some(module_name.to_string()),
                    handler,
                });
            }
            _ => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_MODULE_SHAPE",
                    "unsupported statement in function module",
                )
                .with_path(path)
                .with_span(statement.span()));
            }
        }
    }
    Ok(CachedModuleExport { routes, schemas })
}

fn app_provider_call(expression: &Expression<'_>, app_name: &str) -> Option<ProviderBinding> {
    let Expression::CallExpression(call) = expression else {
        return None;
    };
    let (receiver, property) = static_member_name(&call.callee)?;
    if receiver != app_name || property != "provider" || call.arguments.len() != 1 {
        return None;
    }
    let token = string_argument(call.arguments.first()?)?;
    database_provider_binding_from_token(token)
}

pub(super) fn provider_has_generated_runtime_bridge(binding: &ProviderBinding) -> bool {
    binding.capability_kind == "database" && binding.provider == "sqlite"
}

pub(super) fn helper_sources_referenced_by_handler(
    handler_source: &str,
    helper_sources: &BTreeMap<String, String>,
) -> Vec<String> {
    let mut selected = BTreeSet::<String>::new();
    let mut visiting = BTreeSet::<String>::new();
    let mut ordered = Vec::<String>::new();
    for name in helper_sources.keys() {
        if source_contains_identifier(handler_source, name) {
            push_helper_source_with_dependencies(
                name,
                helper_sources,
                &mut selected,
                &mut visiting,
                &mut ordered,
            );
        }
    }
    ordered
}

pub(super) fn helper_sources_in_dependency_order(
    helper_sources: &BTreeMap<String, String>,
) -> Vec<String> {
    let mut selected = BTreeSet::<String>::new();
    let mut visiting = BTreeSet::<String>::new();
    let mut ordered = Vec::<String>::new();
    for name in helper_sources.keys() {
        push_helper_source_with_dependencies(
            name,
            helper_sources,
            &mut selected,
            &mut visiting,
            &mut ordered,
        );
    }
    ordered
}

fn push_helper_source_with_dependencies(
    name: &str,
    helper_sources: &BTreeMap<String, String>,
    selected: &mut BTreeSet<String>,
    visiting: &mut BTreeSet<String>,
    ordered: &mut Vec<String>,
) {
    if selected.contains(name) || !visiting.insert(name.to_string()) {
        return;
    }
    let Some(source) = helper_sources.get(name) else {
        visiting.remove(name);
        return;
    };
    for dependency in helper_sources.keys() {
        if dependency == name || !source_contains_identifier(source, dependency) {
            continue;
        }
        push_helper_source_with_dependencies(
            dependency,
            helper_sources,
            selected,
            visiting,
            ordered,
        );
    }
    visiting.remove(name);
    selected.insert(name.to_string());
    ordered.push(source.clone());
}

pub(super) fn source_contains_identifier(source: &str, identifier: &str) -> bool {
    if identifier.is_empty() {
        return false;
    }
    let source_bytes = source.as_bytes();
    let identifier_bytes = identifier.as_bytes();
    if identifier_bytes.len() > source_bytes.len() {
        return false;
    }
    let mut index = 0usize;
    while index <= source_bytes.len() - identifier_bytes.len() {
        match source_bytes[index] {
            b'\'' | b'"' | b'`' => {
                index = skip_js_quoted_literal(source_bytes, index);
                continue;
            }
            b'/' if source_bytes.get(index + 1) == Some(&b'/') => {
                index = skip_js_line_comment(source_bytes, index + 2);
                continue;
            }
            b'/' if source_bytes.get(index + 1) == Some(&b'*') => {
                index = skip_js_block_comment(source_bytes, index + 2);
                continue;
            }
            _ => {}
        }
        if &source_bytes[index..index + identifier_bytes.len()] != identifier_bytes {
            index += 1;
            continue;
        }
        let before = index
            .checked_sub(1)
            .and_then(|before| source_bytes.get(before));
        let after = source_bytes.get(index + identifier_bytes.len());
        if before.is_none_or(|byte| !is_js_identifier_byte(*byte))
            && after.is_none_or(|byte| !is_js_identifier_byte(*byte))
            && !identifier_match_is_object_key(source_bytes, index + identifier_bytes.len())
        {
            return true;
        }
        index += 1;
    }
    false
}

pub(super) fn skip_js_quoted_literal(source: &[u8], start: usize) -> usize {
    let quote = source[start];
    let mut index = start + 1;
    while index < source.len() {
        if source[index] == b'\\' {
            index = (index + 2).min(source.len());
            continue;
        }
        if source[index] == quote {
            return index + 1;
        }
        index += 1;
    }
    source.len()
}

pub(super) fn skip_js_line_comment(source: &[u8], mut index: usize) -> usize {
    while index < source.len() && source[index] != b'\n' && source[index] != b'\r' {
        index += 1;
    }
    index
}

pub(super) fn skip_js_block_comment(source: &[u8], mut index: usize) -> usize {
    while index + 1 < source.len() {
        if source[index] == b'*' && source[index + 1] == b'/' {
            return index + 2;
        }
        index += 1;
    }
    source.len()
}

fn identifier_match_is_object_key(source: &[u8], mut index: usize) -> bool {
    while let Some(byte) = source.get(index) {
        if !byte.is_ascii_whitespace() {
            return *byte == b':';
        }
        index += 1;
    }
    false
}

fn is_js_identifier_byte(byte: u8) -> bool {
    byte == b'$' || byte == b'_' || byte.is_ascii_alphanumeric()
}

pub(super) fn wrap_handler_with_providers_and_helpers(
    handler_source: &str,
    providers: &BTreeMap<String, ProviderBinding>,
    helper_sources: &[String],
    is_async: bool,
) -> String {
    if providers.is_empty() && helper_sources.is_empty() {
        return handler_source.to_string();
    }
    let provider_names = providers
        .keys()
        .map(|name| format!("let {name};"))
        .collect::<Vec<_>>()
        .join(" ");
    let provider_prefix = providers
        .iter()
        .map(|(name, binding)| {
            format!(
                "{name} = __sloppy_open_data_provider({}, {}); __sloppy_opened_providers.push({name});",
                serde_json::to_string(&binding.provider)
                    .unwrap_or_else(|_| "\"sqlite\"".to_string()),
                serde_json::to_string(&binding.token)
                    .unwrap_or_else(|_| "\"data.main\"".to_string())
            )
        })
        .collect::<Vec<_>>()
        .join(" ");
    let helper_prefix = if helper_sources.is_empty() {
        String::new()
    } else {
        format!("{} ", helper_sources.join(" "))
    };
    let close_loop =
        "while (__sloppy_opened_providers.length > 0) { const __sloppy_provider = __sloppy_opened_providers.pop(); try { __sloppy_provider.close(); } catch (_) {} }";
    if is_async {
        return format!(
            "async function(ctx) {{ const __sloppy_opened_providers = []; {provider_names} try {{ {provider_prefix} {helper_prefix}return await ({handler_source})(ctx); }} finally {{ {close_loop} }} }}"
        );
    }

    format!(
        "function(ctx) {{ const __sloppy_opened_providers = []; {provider_names} try {{ {provider_prefix} {helper_prefix}return ({handler_source})(ctx); }} finally {{ {close_loop} }} }}"
    )
}

pub(super) fn wrap_handler_with_auth(
    handler_source: &str,
    requirement: &AuthRequirementMetadata,
) -> String {
    let requirement_json = serde_json::to_string(&json!({
        "required": requirement.required,
        "allowAnonymous": requirement.allow_anonymous,
        "schemes": requirement.schemes,
        "scopes": requirement.scopes,
        "roles": requirement.roles,
        "claims": requirement.claims,
        "policy": requirement.policy
    }))
    .unwrap_or_else(|_| "{\"required\":true}".to_string());
    format!(
        "async function(ctx) {{ return await __sloppy_require_auth(ctx, {requirement_json}, () => ({handler_source})(ctx)); }}"
    )
}

pub(super) fn force_auth_required_route_v8_dispatch(handler: &mut Handler) {
    if let Some(response) = &mut handler.response {
        response.native_body = None;
    }
    for response in &mut handler.responses {
        response.native_body = None;
    }
}

pub(super) fn ensure_auth_required_route_request_context(handler: &mut Handler) {
    if handler
        .bindings
        .iter()
        .any(|binding| binding.kind == "context")
    {
        return;
    }
    handler.bindings.push(context_request_binding());
}

pub(super) fn providers_used_by_effects(
    provider_bindings: &BTreeMap<String, ProviderBinding>,
    effects: &[EffectMetadata],
) -> BTreeMap<String, ProviderBinding> {
    let tokens = effects
        .iter()
        .map(|effect| effect.provider.as_str())
        .collect::<BTreeSet<_>>();
    provider_bindings
        .iter()
        .filter(|(_, binding)| tokens.contains(binding.token.as_str()))
        .map(|(name, binding)| (name.clone(), binding.clone()))
        .collect()
}

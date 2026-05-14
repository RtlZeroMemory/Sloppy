// Program Mode extraction, dependency traversal, and module rewriting.
use super::*;

pub(super) fn source_has_sloppy_web_import(path: &Path, source: &str) -> Result<bool, Diagnostic> {
    let source_type = source_type_for_path(path, ParseContext::Entry)?;
    let allocator = Allocator::default();
    let parsed = Parser::new(&allocator, source, source_type).parse();
    if let Some(error) = parsed.errors.into_iter().next() {
        return Err(
            Diagnostic::new("SLOPPYC_E_PARSE", format!("failed to parse input: {error}"))
                .with_path(path),
        );
    }
    Ok(parsed.program.body.iter().any(|statement| {
        matches!(
            statement,
            Statement::ImportDeclaration(import) if import.source.value.as_str() == "sloppy"
        )
    }))
}

pub(super) fn extract_program_with_metrics(
    path: &Path,
    source: &str,
    options: &CompileOptions,
    mut metrics: Option<&mut CompileMetrics>,
) -> Result<ExtractedApp, Diagnostic> {
    let parse_start = Instant::now();
    let source_type = source_type_for_path(path, ParseContext::Entry)?;
    let allocator = Allocator::default();
    let parsed = Parser::new(&allocator, source, source_type).parse();
    if let Some(metrics) = metrics.as_mut() {
        metrics.add_phase("parseEntryMs", parse_start.elapsed());
    }
    if let Some(error) = parsed.errors.into_iter().next() {
        return Err(
            Diagnostic::new("SLOPPYC_E_PARSE", format!("failed to parse input: {error}"))
                .with_path(path),
        );
    }
    let extract_start = Instant::now();
    let mut graph = ModuleGraph::new(path, options.config_dir.as_deref());
    let mut modules = Vec::new();
    let mut visiting = BTreeSet::new();
    extract_program_module(path, source, &mut graph, &mut visiting, &mut modules)?;
    apply_dependency_includes(path, &mut graph, &mut visiting, &mut modules, options)?;
    ensure_package_global_shims(path, &mut graph, &mut modules);
    if let Some(metrics) = metrics.as_mut() {
        metrics.add_phase("extractMs", extract_start.elapsed());
    }
    let source_files = graph.source_files.clone();
    let capabilities = program_inferred_capabilities(&graph);
    let dependency_graph = graph.dependency_graph.clone();
    let program_entry = program_module_id(&graph, path, None);
    Ok(ExtractedApp {
        kind: ProjectKind::Program,
        program_entry: Some(program_entry),
        program_modules: modules,
        uses_data_runtime: graph.uses_data_runtime,
        uses_sql_runtime: graph.uses_sql_runtime,
        uses_orm_runtime: graph.uses_orm_runtime,
        orm_tables: Vec::new(),
        orm_relations: Vec::new(),
        orm_extraction_partial: graph.uses_orm_runtime,
        uses_migrations_runtime: graph.uses_migrations_runtime,
        uses_provider_health_runtime: graph.uses_provider_health_runtime,
        source_files,
        routes: Vec::new(),
        dynamic_routes: Vec::new(),
        dynamic_entry_source: None,
        service_registrations: Vec::new(),
        modules: Vec::new(),
        helper_sources: Vec::new(),
        capabilities,
        configuration: None,
        schemas: Vec::new(),
        config_reads: Vec::new(),
        uses_time_runtime: graph.uses_time_runtime,
        uses_fs_runtime: graph.uses_fs_runtime,
        uses_crypto_runtime: graph.uses_crypto_runtime,
        noncrypto_hash_security_context_visible: graph.noncrypto_hash_security_context_visible,
        uses_codec_runtime: graph.uses_codec_runtime,
        checksum_security_context_visible: graph.checksum_security_context_visible,
        uses_cache_runtime: graph.uses_cache_runtime,
        uses_net_runtime: graph.uses_net_runtime,
        uses_os_runtime: graph.uses_os_runtime,
        uses_http_client_runtime: graph.uses_http_client_runtime,
        uses_webhooks_runtime: graph.uses_webhooks_runtime,
        uses_redis_runtime: graph.uses_redis_runtime,
        uses_realtime_runtime: graph.uses_realtime_runtime,
        uses_workers_runtime: graph.uses_workers_runtime,
        uses_ffi_runtime: graph.uses_ffi_runtime,
        ffi: graph.ffi_libraries,
        ffi_structs: graph.ffi_structs,
        ffi_handles: graph.ffi_handles,
        ffi_callbacks: graph.ffi_callbacks,
        ffi_dispatch_tables: graph.ffi_dispatch_tables,
        ffi_adoptions: graph.ffi_adoptions,
        uses_health: false,
        auth: AuthMetadata::default(),
        problem_details: None,
        dependency_graph,
    })
}

fn ensure_package_global_shims(
    path: &Path,
    graph: &mut ModuleGraph,
    modules: &mut Vec<ProgramModule>,
) {
    if graph.dependency_graph.packages.is_empty() {
        return;
    }
    for specifier in ["node:process", "node:buffer"] {
        if let Some(builtin) = resolver::resolve_node_builtin(specifier) {
            graph.add_node_builtin(&builtin, path);
            if let Some(backing) = builtin.backing {
                ensure_node_compat_module(backing, graph, modules);
            }
        }
    }
}

fn program_inferred_capabilities(graph: &ModuleGraph) -> Vec<DatabaseCapability> {
    let mut capabilities = Vec::new();
    if graph.uses_fs_runtime {
        push_program_inferred_capability(&mut capabilities, "fs");
    }
    if graph.uses_net_runtime || graph.uses_http_client_runtime {
        push_program_inferred_capability(&mut capabilities, "net");
    }
    if graph.uses_cache_runtime {
        push_program_inferred_capability(&mut capabilities, "cache");
    }
    if graph.uses_redis_runtime {
        push_program_inferred_capability(&mut capabilities, "redis");
    }
    if graph.uses_os_runtime {
        push_program_inferred_capability(&mut capabilities, "os");
    }
    if graph.uses_time_runtime {
        push_program_inferred_capability(&mut capabilities, "time");
    }
    if graph.uses_crypto_runtime {
        push_program_inferred_capability(&mut capabilities, "crypto");
    }
    if graph.uses_codec_runtime {
        push_program_inferred_capability(&mut capabilities, "codec");
    }
    if graph.uses_workers_runtime {
        push_program_inferred_capability(&mut capabilities, "workers");
    }
    if graph.uses_ffi_runtime
        || !graph.ffi_libraries.is_empty()
        || !graph.ffi_structs.is_empty()
        || !graph.ffi_handles.is_empty()
        || !graph.ffi_callbacks.is_empty()
        || !graph.ffi_dispatch_tables.is_empty()
        || !graph.ffi_adoptions.is_empty()
    {
        push_program_inferred_capability(&mut capabilities, "ffi");
    }
    capabilities
}

fn push_program_inferred_capability(capabilities: &mut Vec<DatabaseCapability>, token: &str) {
    let Some((kind, access)) = declared_capability_shape(token) else {
        return;
    };
    capabilities.push(DatabaseCapability {
        token: token.to_string(),
        capability_kind: kind.to_string(),
        provider: String::new(),
        config_name: None,
        config_key: None,
        access: access.to_string(),
        database: None,
        config_source: None,
        source_name: "program import".to_string(),
        source: format!("import:{token}"),
        span: Span::new(0, 0),
        from_provider_use: false,
    });
}

pub(super) fn include_pattern_is_safe(pattern: &str) -> bool {
    let pattern = pattern.trim();
    if pattern.is_empty() || pattern.as_bytes().contains(&0) {
        return false;
    }
    let normalized = pattern.replace('\\', "/");
    if normalized.starts_with('/') || normalized.starts_with("//") {
        return false;
    }
    if normalized.len() >= 2 && normalized.as_bytes()[1] == b':' {
        return false;
    }
    !normalized
        .split('/')
        .any(|segment| segment == ".." || segment.is_empty())
}

fn apply_dependency_includes(
    entry_path: &Path,
    graph: &mut ModuleGraph,
    visiting: &mut BTreeSet<PathBuf>,
    modules: &mut Vec<ProgramModule>,
    options: &CompileOptions,
) -> Result<(), Diagnostic> {
    for pattern in &options.module_include {
        let matches = collect_include_matches(&graph.entry_dir, pattern, IncludeKind::Module)
            .map_err(|error| {
                Diagnostic::new(
                    "SLOPPYC_E_INCLUDE",
                    format!("failed to evaluate moduleInclude pattern '{pattern}': {error}"),
                )
                .with_path(entry_path)
            })?;
        if matches.is_empty() {
            graph
                .dependency_graph
                .compatibility_findings
                .push(CompatibilityFinding {
                    code: "SLOPPYC_W_MODULE_INCLUDE_EMPTY".to_string(),
                    severity: "warning".to_string(),
                    message: format!("moduleInclude pattern '{pattern}' matched no modules."),
                    source: Some("sloppy.json".to_string()),
                    package: None,
                    specifier: Some(pattern.clone()),
                    hint: Some(
                        "Check the project-relative glob or remove the include.".to_string(),
                    ),
                });
            continue;
        }
        for module_path in matches {
            let source = fs::read_to_string(&module_path).map_err(|error| {
                Diagnostic::new(
                    "SLOPPYC_E_INPUT",
                    format!("failed to read moduleInclude file: {error}"),
                )
                .with_path(&module_path)
            })?;
            extract_program_module_with_context(
                &module_path,
                &source,
                graph,
                visiting,
                modules,
                None,
                Some(format!("moduleInclude:{pattern}")),
            )?;
        }
    }

    for pattern in &options.asset_include {
        let matches = collect_include_matches(&graph.entry_dir, pattern, IncludeKind::Asset)
            .map_err(|error| {
                Diagnostic::new(
                    "SLOPPYC_E_INCLUDE",
                    format!("failed to evaluate assetInclude pattern '{pattern}': {error}"),
                )
                .with_path(entry_path)
            })?;
        if matches.is_empty() {
            graph
                .dependency_graph
                .compatibility_findings
                .push(CompatibilityFinding {
                    code: "SLOPPYC_W_ASSET_INCLUDE_EMPTY".to_string(),
                    severity: "warning".to_string(),
                    message: format!("assetInclude pattern '{pattern}' matched no assets."),
                    source: Some("sloppy.json".to_string()),
                    package: None,
                    specifier: Some(pattern.clone()),
                    hint: Some(
                        "Check the project-relative glob or remove the include.".to_string(),
                    ),
                });
            continue;
        }
        for asset_path in matches {
            graph.add_dependency_asset(&asset_path, format!("assetInclude:{pattern}"));
        }
    }

    Ok(())
}

#[derive(Debug, Clone, Copy, Eq, PartialEq)]
enum IncludeKind {
    Module,
    Asset,
}

fn collect_include_matches(
    root: &Path,
    pattern: &str,
    kind: IncludeKind,
) -> Result<Vec<PathBuf>, std::io::Error> {
    let normalized_pattern = pattern.replace('\\', "/");
    let include_node_modules =
        normalized_pattern == "node_modules" || normalized_pattern.starts_with("node_modules/");
    let mut files = Vec::new();
    collect_include_files(root, root, include_node_modules, &mut files)?;
    let mut matches = files
        .into_iter()
        .filter(|path| {
            if kind == IncludeKind::Module && !module_include_extension_supported(path) {
                return false;
            }
            let relative = resolver::normalized_artifact_id(path, root);
            glob_match(&normalized_pattern, &relative)
        })
        .collect::<Vec<_>>();
    matches.sort_by_key(|path| resolver::normalized_artifact_id(path, root));
    Ok(matches)
}

fn collect_include_files(
    root: &Path,
    dir: &Path,
    include_node_modules: bool,
    files: &mut Vec<PathBuf>,
) -> Result<(), std::io::Error> {
    let mut entries = fs::read_dir(dir)?.collect::<Result<Vec<_>, _>>()?;
    entries.sort_by_key(|entry| entry.file_name());
    for entry in entries {
        let path = entry.path();
        let file_type = entry.file_type()?;
        if file_type.is_dir() {
            let name = entry.file_name();
            let name = name.to_string_lossy();
            if matches!(name.as_ref(), ".git" | ".sloppy" | "target")
                || (name == "node_modules" && !include_node_modules)
            {
                continue;
            }
            if resolver::stays_within_source_root(&path, root) {
                collect_include_files(root, &path, include_node_modules, files)?;
            }
        } else if file_type.is_file() && resolver::stays_within_source_root(&path, root) {
            files.push(path);
        }
    }
    Ok(())
}

fn module_include_extension_supported(path: &Path) -> bool {
    matches!(
        path.extension()
            .and_then(|extension| extension.to_str())
            .unwrap_or(""),
        "js" | "mjs" | "cjs" | "ts" | "tsx" | "json"
    )
}

fn glob_match(pattern: &str, value: &str) -> bool {
    let pattern = pattern.trim_matches('/');
    let value = value.trim_matches('/');
    let pattern_segments = pattern.split('/').collect::<Vec<_>>();
    let value_segments = value.split('/').collect::<Vec<_>>();
    glob_segments_match(&pattern_segments, &value_segments)
}

fn glob_segments_match(pattern: &[&str], value: &[&str]) -> bool {
    if pattern.is_empty() {
        return value.is_empty();
    }
    if pattern[0] == "**" {
        return glob_segments_match(&pattern[1..], value)
            || (!value.is_empty() && glob_segments_match(pattern, &value[1..]));
    }
    !value.is_empty()
        && glob_segment_match(pattern[0], value[0])
        && glob_segments_match(&pattern[1..], &value[1..])
}

fn glob_segment_match(pattern: &str, value: &str) -> bool {
    fn inner(pattern: &[u8], value: &[u8]) -> bool {
        match pattern.split_first() {
            None => value.is_empty(),
            Some((&b'*', rest)) => {
                inner(rest, value) || (!value.is_empty() && inner(pattern, &value[1..]))
            }
            Some((&b'?', rest)) => !value.is_empty() && inner(rest, &value[1..]),
            Some((&literal, rest)) => {
                value.first().is_some_and(|candidate| *candidate == literal)
                    && inner(rest, &value[1..])
            }
        }
    }
    inner(pattern.as_bytes(), value.as_bytes())
}

fn extract_program_module(
    path: &Path,
    source: &str,
    graph: &mut ModuleGraph,
    visiting: &mut BTreeSet<PathBuf>,
    modules: &mut Vec<ProgramModule>,
) -> Result<(), Diagnostic> {
    extract_program_module_with_context(path, source, graph, visiting, modules, None, None)
}

fn extract_program_module_with_context(
    path: &Path,
    source: &str,
    graph: &mut ModuleGraph,
    visiting: &mut BTreeSet<PathBuf>,
    modules: &mut Vec<ProgramModule>,
    package: Option<String>,
    included_by: Option<String>,
) -> Result<(), Diagnostic> {
    let canonical = fs::canonicalize(path).unwrap_or_else(|_| path.to_path_buf());
    let package_type = if package.is_some() {
        resolver::package_type_for_path(&canonical)
    } else {
        "module".to_string()
    };
    let format = resolver::module_format_for_path(&canonical, &package_type);
    let id = if included_by.is_some() && package.is_none() {
        resolver::normalized_artifact_id(&canonical, &graph.entry_dir)
    } else {
        program_module_id(graph, &canonical, package.as_deref())
    };
    if modules.iter().any(|module| module.id == id) {
        return Ok(());
    }
    if !visiting.insert(canonical.clone()) {
        graph.dependency_graph.ensure_defaults();
        graph
            .dependency_graph
            .compatibility_findings
            .push(CompatibilityFinding {
                code: "SLOPPYC_W_MODULE_CYCLE_BASIC".to_string(),
                severity: "warning".to_string(),
                message:
                    "module cycle uses Sloppy's basic module-cache semantics; exact ESM temporal-dead-zone behavior is not guaranteed yet"
                        .to_string(),
                source: Some(id),
                package: package.clone(),
                specifier: None,
                hint: Some("Avoid depending on uninitialized ESM bindings across cycles.".to_string()),
            });
        return Ok(());
    }
    let source_name = graph.record_source(&canonical, source);
    let transformed = transform_program_source(
        &canonical,
        source,
        graph,
        visiting,
        modules,
        package.clone(),
        format,
    )?;
    visiting.remove(&canonical);
    if graph.dependency_graph.has_entries()
        || package.is_some()
        || included_by.is_some()
        || format != ModuleFormat::Esm
    {
        graph.add_dependency_module(
            id.clone(),
            resolver::normalized_artifact_id(&canonical, &graph.entry_dir),
            format,
            package.clone(),
            included_by,
        );
    }
    modules.push(ProgramModule {
        id,
        source_name,
        source: source.to_string(),
        emitted_source: transformed,
        format,
    });
    Ok(())
}

pub(super) fn program_module_id(
    graph: &ModuleGraph,
    path: &Path,
    _package: Option<&str>,
) -> String {
    resolver::normalized_artifact_id(path, &graph.entry_dir)
}

fn validate_program_relative_module(
    importer: &Path,
    resolved: &Path,
    graph: &ModuleGraph,
    package: Option<&str>,
    span: Span,
) -> Result<PathBuf, Diagnostic> {
    let allowed = if package.is_some() {
        resolver::stays_within_nearest_package_scope(importer, resolved)
    } else {
        resolver::stays_within_source_root(resolved, &graph.entry_dir)
    };
    if allowed {
        return Ok(resolved.to_path_buf());
    }
    Err(Diagnostic::new(
        "SLOPPYC_E_SOURCE_ESCAPE",
        "program relative imports must stay inside the source root or package root",
    )
    .with_path(resolved)
    .with_span(span)
    .with_hint(
        "Move the imported module under the entry directory or expose it through a package-internal path.",
    ))
}

fn transform_program_source(
    path: &Path,
    source: &str,
    graph: &mut ModuleGraph,
    visiting: &mut BTreeSet<PathBuf>,
    modules: &mut Vec<ProgramModule>,
    package: Option<String>,
    format: ModuleFormat,
) -> Result<String, Diagnostic> {
    if format == ModuleFormat::Json {
        let value: Value = serde_json::from_str(source).map_err(|error| {
            Diagnostic::new(
                "SLOPPYC_E_PARSE",
                format!("failed to parse JSON module: {error}"),
            )
            .with_path(path)
        })?;
        return Ok(format!(
            "module.exports = {};\n",
            serde_json::to_string(&value).unwrap_or_else(|_| "null".to_string())
        ));
    }
    if format == ModuleFormat::CommonJs {
        analyze_commonjs_requires(path, source, graph, visiting, modules, package.clone())?;
        return Ok(source.to_string());
    }
    let source_type = source_type_for_path(path, ParseContext::Module)?;
    let allocator = Allocator::default();
    let parsed = Parser::new(&allocator, source, source_type).parse();
    if let Some(error) = parsed.errors.into_iter().next() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_PARSE",
            format!("failed to parse program module: {error}"),
        )
        .with_path(path));
    }

    let mut program = parsed.program;
    let source_name = graph.record_source(path, source);
    extract_ffi_declarations_from_statements(
        path,
        source,
        &source_name,
        &program.body,
        FfiMetadataSink {
            libraries: &mut graph.ffi_libraries,
            structs: &mut graph.ffi_structs,
            handles: &mut graph.ffi_handles,
            callbacks: &mut graph.ffi_callbacks,
            dispatch_tables: &mut graph.ffi_dispatch_tables,
            adoptions: &mut graph.ffi_adoptions,
        },
    )?;
    analyze_program_imports(
        path,
        &program.body,
        graph,
        visiting,
        modules,
        package.clone(),
    )?;
    let transformed = transpile_program_typescript(path, &allocator, &mut program)?;
    rewrite_program_module_exports(path, &transformed, graph, package)
}

fn analyze_program_imports(
    path: &Path,
    statements: &[Statement<'_>],
    graph: &mut ModuleGraph,
    visiting: &mut BTreeSet<PathBuf>,
    modules: &mut Vec<ProgramModule>,
    package: Option<String>,
) -> Result<(), Diagnostic> {
    for statement in statements {
        analyze_statement_dynamic_imports(
            path,
            statement,
            graph,
            visiting,
            modules,
            package.clone(),
        )?;
        if let Statement::ImportDeclaration(import) = statement {
            analyze_program_import(path, import, graph, visiting, modules, package.clone())?;
        } else if let Statement::ExportNamedDeclaration(export) = statement {
            if export.export_kind != ImportOrExportKind::Type {
                if let Some(source) = &export.source {
                    let has_runtime_reexport = export
                        .specifiers
                        .iter()
                        .any(|specifier| specifier.export_kind != ImportOrExportKind::Type);
                    if !has_runtime_reexport {
                        continue;
                    }
                    resolve_program_dependency(
                        ProgramDependencyRequest {
                            path,
                            specifier: source.value.as_str(),
                            package: package.clone(),
                            import_mode: true,
                            span: source.span,
                        },
                        graph,
                        visiting,
                        modules,
                    )?;
                }
            }
        } else if let Statement::ExportAllDeclaration(export) = statement {
            if export.export_kind != ImportOrExportKind::Type {
                resolve_program_dependency(
                    ProgramDependencyRequest {
                        path,
                        specifier: export.source.value.as_str(),
                        package: package.clone(),
                        import_mode: true,
                        span: export.source.span,
                    },
                    graph,
                    visiting,
                    modules,
                )?;
            }
        }
    }
    Ok(())
}

fn analyze_statement_dynamic_imports(
    path: &Path,
    statement: &Statement<'_>,
    graph: &mut ModuleGraph,
    visiting: &mut BTreeSet<PathBuf>,
    modules: &mut Vec<ProgramModule>,
    package: Option<String>,
) -> Result<(), Diagnostic> {
    match statement {
        Statement::VariableDeclaration(declaration) => {
            for declarator in &declaration.declarations {
                if let Some(init) = &declarator.init {
                    analyze_expression_dynamic_imports(
                        path,
                        init,
                        graph,
                        visiting,
                        modules,
                        package.clone(),
                    )?;
                }
            }
        }
        Statement::ExpressionStatement(statement) => analyze_expression_dynamic_imports(
            path,
            &statement.expression,
            graph,
            visiting,
            modules,
            package,
        )?,
        Statement::ReturnStatement(statement) => {
            if let Some(argument) = &statement.argument {
                analyze_expression_dynamic_imports(
                    path, argument, graph, visiting, modules, package,
                )?;
            }
        }
        Statement::BlockStatement(block) => {
            for statement in &block.body {
                analyze_statement_dynamic_imports(
                    path,
                    statement,
                    graph,
                    visiting,
                    modules,
                    package.clone(),
                )?;
            }
        }
        Statement::FunctionDeclaration(function) => {
            if let Some(body) = &function.body {
                for statement in &body.statements {
                    analyze_statement_dynamic_imports(
                        path,
                        statement,
                        graph,
                        visiting,
                        modules,
                        package.clone(),
                    )?;
                }
            }
        }
        Statement::ExportNamedDeclaration(export) => {
            if let Some(declaration) = &export.declaration {
                analyze_declaration_dynamic_imports(
                    path,
                    declaration,
                    graph,
                    visiting,
                    modules,
                    package,
                )?;
            }
        }
        _ => {}
    }
    Ok(())
}

fn analyze_declaration_dynamic_imports(
    path: &Path,
    declaration: &Declaration<'_>,
    graph: &mut ModuleGraph,
    visiting: &mut BTreeSet<PathBuf>,
    modules: &mut Vec<ProgramModule>,
    package: Option<String>,
) -> Result<(), Diagnostic> {
    match declaration {
        Declaration::FunctionDeclaration(function) => {
            if let Some(body) = &function.body {
                for statement in &body.statements {
                    analyze_statement_dynamic_imports(
                        path,
                        statement,
                        graph,
                        visiting,
                        modules,
                        package.clone(),
                    )?;
                }
            }
        }
        Declaration::VariableDeclaration(declaration) => {
            for declarator in &declaration.declarations {
                if let Some(init) = &declarator.init {
                    analyze_expression_dynamic_imports(
                        path,
                        init,
                        graph,
                        visiting,
                        modules,
                        package.clone(),
                    )?;
                }
            }
        }
        _ => {}
    }
    Ok(())
}

fn analyze_expression_dynamic_imports(
    path: &Path,
    expression: &Expression<'_>,
    graph: &mut ModuleGraph,
    visiting: &mut BTreeSet<PathBuf>,
    modules: &mut Vec<ProgramModule>,
    package: Option<String>,
) -> Result<(), Diagnostic> {
    match expression {
        Expression::ImportExpression(import) => {
            let from_id = program_module_id(graph, path, package.as_deref());
            if let Some(specifier) = expression_string_literal(&import.source) {
                let resolved_id = resolve_program_dependency(
                    ProgramDependencyRequest {
                        path,
                        specifier,
                        package,
                        import_mode: true,
                        span: import.span,
                    },
                    graph,
                    visiting,
                    modules,
                )?;
                graph.add_dynamic_import(
                    &from_id,
                    Some(specifier.to_string()),
                    resolved_id,
                    "string-literal",
                );
            } else {
                graph.add_dynamic_import(&from_id, None, None, "computed");
            }
        }
        Expression::AwaitExpression(await_expression) => analyze_expression_dynamic_imports(
            path,
            &await_expression.argument,
            graph,
            visiting,
            modules,
            package,
        )?,
        Expression::CallExpression(call) => {
            analyze_expression_dynamic_imports(
                path,
                &call.callee,
                graph,
                visiting,
                modules,
                package.clone(),
            )?;
            for argument in &call.arguments {
                analyze_argument_dynamic_imports(
                    path,
                    argument,
                    graph,
                    visiting,
                    modules,
                    package.clone(),
                )?;
            }
        }
        Expression::ParenthesizedExpression(parenthesized) => analyze_expression_dynamic_imports(
            path,
            &parenthesized.expression,
            graph,
            visiting,
            modules,
            package,
        )?,
        Expression::ArrowFunctionExpression(function) => {
            for statement in &function.body.statements {
                analyze_statement_dynamic_imports(
                    path,
                    statement,
                    graph,
                    visiting,
                    modules,
                    package.clone(),
                )?;
            }
        }
        _ => {}
    }
    Ok(())
}

fn analyze_argument_dynamic_imports(
    path: &Path,
    argument: &Argument<'_>,
    graph: &mut ModuleGraph,
    visiting: &mut BTreeSet<PathBuf>,
    modules: &mut Vec<ProgramModule>,
    package: Option<String>,
) -> Result<(), Diagnostic> {
    match argument {
        Argument::ImportExpression(import) => {
            let from_id = program_module_id(graph, path, package.as_deref());
            if let Some(specifier) = expression_string_literal(&import.source) {
                let resolved_id = resolve_program_dependency(
                    ProgramDependencyRequest {
                        path,
                        specifier,
                        package,
                        import_mode: true,
                        span: import.span,
                    },
                    graph,
                    visiting,
                    modules,
                )?;
                graph.add_dynamic_import(
                    &from_id,
                    Some(specifier.to_string()),
                    resolved_id,
                    "string-literal",
                );
            } else {
                graph.add_dynamic_import(&from_id, None, None, "computed");
            }
        }
        Argument::CallExpression(call) => {
            for argument in &call.arguments {
                analyze_argument_dynamic_imports(
                    path,
                    argument,
                    graph,
                    visiting,
                    modules,
                    package.clone(),
                )?;
            }
        }
        Argument::ParenthesizedExpression(parenthesized) => analyze_expression_dynamic_imports(
            path,
            &parenthesized.expression,
            graph,
            visiting,
            modules,
            package,
        )?,
        _ => {}
    }
    Ok(())
}

fn analyze_program_import(
    path: &Path,
    import: &ImportDeclaration<'_>,
    graph: &mut ModuleGraph,
    visiting: &mut BTreeSet<PathBuf>,
    modules: &mut Vec<ProgramModule>,
    package: Option<String>,
) -> Result<(), Diagnostic> {
    let specifier = import.source.value.as_str();
    if !program_import_has_runtime_value(import) {
        return Ok(());
    }
    let import_kind = resolver::classify_import(path, specifier);
    let from_id = program_module_id(graph, path, package.as_deref());
    match import_kind {
        resolver::ImportKind::Relative(resolved) => {
            let resolved = validate_program_relative_module(
                path,
                &resolved,
                graph,
                package.as_deref(),
                import.source.span,
            )?;
            let source = fs::read_to_string(&resolved).map_err(|error| {
                Diagnostic::new(
                    "SLOPPYC_E_INPUT",
                    format!("failed to read program module: {error}"),
                )
                .with_path(&resolved)
            })?;
            let resolved_package = package.clone();
            let resolved_id = program_module_id(graph, &resolved, resolved_package.as_deref());
            graph.add_dependency_import(&from_id, specifier, &resolved_id, "relative");
            extract_program_module_with_context(
                &resolved,
                &source,
                graph,
                visiting,
                modules,
                resolved_package,
                Some(from_id),
            )?;
            Ok(())
        }
        resolver::ImportKind::SlopStdlib
        | resolver::ImportKind::SlopData
        | resolver::ImportKind::SlopTime
        | resolver::ImportKind::SlopFilesystem
        | resolver::ImportKind::SlopCrypto
        | resolver::ImportKind::SlopCodec
        | resolver::ImportKind::SlopCache
        | resolver::ImportKind::SlopNet
        | resolver::ImportKind::SlopHttp
        | resolver::ImportKind::SlopWebhooks
        | resolver::ImportKind::SlopRedis
        | resolver::ImportKind::SlopOs
        | resolver::ImportKind::SlopOrm
        | resolver::ImportKind::SlopWorkers
        | resolver::ImportKind::SlopFfi
        | resolver::ImportKind::SqliteProvider => {
            validate_program_stdlib_import(path, import, &import_kind)?;
            mark_program_import(graph, &import_kind, import);
            if matches!(import_kind, resolver::ImportKind::SqliteProvider) {
                ensure_sloppy_provider_module("sloppy/providers/sqlite", graph, modules);
                graph.add_dependency_import(
                    &from_id,
                    specifier,
                    "sloppy/providers/sqlite",
                    "sloppy-provider",
                );
            }
            Ok(())
        }
        resolver::ImportKind::NodeBuiltin(builtin) => {
            if builtin.status == "unsupported" {
                graph.add_node_builtin(&builtin, path);
                return Err(Diagnostic::new(
                    "SLOPPYC_E_UNSUPPORTED_NODE_BUILTIN",
                    format!(
                        "{} is not supported by Sloppy's Node compatibility registry yet.",
                        builtin.specifier
                    ),
                )
                .with_path(path)
                .with_span(import.source.span)
                .with_hint(
                    "Use a Sloppy stdlib API or a package path that avoids this Node builtin.",
                ));
            }
            graph.add_node_builtin(&builtin, path);
            if let Some(backing) = builtin.backing {
                ensure_node_compat_module(backing, graph, modules);
                graph.add_dependency_import(&from_id, specifier, backing, "node-compat-shim");
            }
            Ok(())
        }
        resolver::ImportKind::Package(package_resolution) => {
            graph.add_package_record(&package_resolution);
            let source = fs::read_to_string(&package_resolution.entry).map_err(|error| {
                Diagnostic::new(
                    "SLOPPYC_E_INPUT",
                    format!("failed to read package module: {error}"),
                )
                .with_path(&package_resolution.entry)
            })?;
            let resolved_id = program_module_id(
                graph,
                &package_resolution.entry,
                Some(&package_resolution.name),
            );
            graph.add_dependency_import(&from_id, specifier, &resolved_id, "package");
            extract_program_module_with_context(
                &package_resolution.entry,
                &source,
                graph,
                visiting,
                modules,
                Some(package_resolution.name),
                Some(from_id),
            )
        }
        resolver::ImportKind::NativeAddonUnsupported(package_resolution) => {
            graph.add_package_record(&package_resolution);
            Err(Diagnostic::new(
                "SLOPPYC_E_NATIVE_ADDON_UNSUPPORTED",
                format!(
                    "Package \"{}\" requires a native Node addon. Sloppy does not support Node native addons yet.",
                    package_resolution.name
                ),
            )
            .with_path(path)
            .with_span(import.source.span)
            .with_hint("Use a pure-JavaScript package entry or remove the native addon dependency."))
        }
        resolver::ImportKind::PackageExportUnsupported(failure) => Err(
            package_export_unsupported_diagnostic(path, import.source.span, &failure),
        ),
        resolver::ImportKind::UnsupportedBare(specifier) => Err(Diagnostic::new(
            "SLOPPYC_E_PACKAGE_NOT_FOUND",
            format!(
                "Package \"{specifier}\" was not found from {}.",
                source_map_source_name(path)
            ),
        )
        .with_path(path)
        .with_span(import.source.span)
        .with_hint(format!(
            "Install it with your package manager, for example:\n  npm install {specifier}"
        ))),
        resolver::ImportKind::UnresolvedRelative(specifier) => Err(Diagnostic::new(
            "SLOPPYC_E_MISSING_RELATIVE_IMPORT",
            format!("relative import \"{specifier}\" could not be resolved"),
        )
        .with_path(path)
        .with_span(import.source.span)
        .with_hint("Use a relative .js/.mjs/.cjs/.ts/.tsx/.json module inside the source root.")),
        resolver::ImportKind::Remote(specifier) => Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_IMPORT",
            format!("remote import \"{specifier}\" is outside the sealed Sloppy artifact graph"),
        )
        .with_path(path)
        .with_span(import.source.span)
        .with_hint("Use installed packages, Sloppy stdlib imports, or relative modules.")),
    }
}

fn analyze_commonjs_requires(
    path: &Path,
    source: &str,
    graph: &mut ModuleGraph,
    visiting: &mut BTreeSet<PathBuf>,
    modules: &mut Vec<ProgramModule>,
    package: Option<String>,
) -> Result<(), Diagnostic> {
    let source_type = SourceType::from_path(path).unwrap_or_else(|_| SourceType::mjs());
    let allocator = Allocator::default();
    let parsed = Parser::new(&allocator, source, source_type).parse();
    if let Some(error) = parsed.errors.into_iter().next() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_PARSE",
            format!("failed to parse CommonJS module: {error}"),
        )
        .with_path(path));
    }
    for statement in &parsed.program.body {
        analyze_commonjs_statement_requires(
            path,
            statement,
            graph,
            visiting,
            modules,
            package.clone(),
        )?;
    }
    Ok(())
}

fn analyze_commonjs_statement_requires(
    path: &Path,
    statement: &Statement<'_>,
    graph: &mut ModuleGraph,
    visiting: &mut BTreeSet<PathBuf>,
    modules: &mut Vec<ProgramModule>,
    package: Option<String>,
) -> Result<(), Diagnostic> {
    match statement {
        Statement::VariableDeclaration(declaration) => {
            for declarator in &declaration.declarations {
                if let Some(init) = &declarator.init {
                    analyze_commonjs_expression_requires(
                        path,
                        init,
                        graph,
                        visiting,
                        modules,
                        package.clone(),
                    )?;
                }
            }
        }
        Statement::ExpressionStatement(statement) => analyze_commonjs_expression_requires(
            path,
            &statement.expression,
            graph,
            visiting,
            modules,
            package,
        )?,
        Statement::BlockStatement(block) => {
            for statement in &block.body {
                analyze_commonjs_statement_requires(
                    path,
                    statement,
                    graph,
                    visiting,
                    modules,
                    package.clone(),
                )?;
            }
        }
        Statement::IfStatement(statement) => {
            analyze_commonjs_expression_requires(
                path,
                &statement.test,
                graph,
                visiting,
                modules,
                package.clone(),
            )?;
            analyze_commonjs_statement_requires(
                path,
                &statement.consequent,
                graph,
                visiting,
                modules,
                package.clone(),
            )?;
            if let Some(alternate) = &statement.alternate {
                analyze_commonjs_statement_requires(
                    path, alternate, graph, visiting, modules, package,
                )?;
            }
        }
        _ => {}
    }
    Ok(())
}

fn analyze_commonjs_expression_requires(
    path: &Path,
    expression: &Expression<'_>,
    graph: &mut ModuleGraph,
    visiting: &mut BTreeSet<PathBuf>,
    modules: &mut Vec<ProgramModule>,
    package: Option<String>,
) -> Result<(), Diagnostic> {
    match expression {
        Expression::CallExpression(call) => {
            if let Some(specifier) = call_static_require_specifier(call) {
                resolve_program_dependency(
                    ProgramDependencyRequest {
                        path,
                        specifier,
                        package: package.clone(),
                        import_mode: false,
                        span: call.span,
                    },
                    graph,
                    visiting,
                    modules,
                )?;
            }
            for argument in &call.arguments {
                analyze_commonjs_argument_requires(
                    path,
                    argument,
                    graph,
                    visiting,
                    modules,
                    package.clone(),
                )?;
            }
        }
        Expression::ParenthesizedExpression(parenthesized) => analyze_commonjs_expression_requires(
            path,
            &parenthesized.expression,
            graph,
            visiting,
            modules,
            package,
        )?,
        Expression::ArrayExpression(array) => {
            for element in &array.elements {
                if let Some(expression) = element.as_expression() {
                    analyze_commonjs_expression_requires(
                        path,
                        expression,
                        graph,
                        visiting,
                        modules,
                        package.clone(),
                    )?;
                }
            }
        }
        Expression::ObjectExpression(object) => {
            for property in &object.properties {
                if let ObjectPropertyKind::ObjectProperty(property) = property {
                    analyze_commonjs_expression_requires(
                        path,
                        &property.value,
                        graph,
                        visiting,
                        modules,
                        package.clone(),
                    )?;
                }
            }
        }
        Expression::AssignmentExpression(assignment) => analyze_commonjs_expression_requires(
            path,
            &assignment.right,
            graph,
            visiting,
            modules,
            package,
        )?,
        _ => {}
    }
    Ok(())
}

fn analyze_commonjs_argument_requires(
    path: &Path,
    argument: &Argument<'_>,
    graph: &mut ModuleGraph,
    visiting: &mut BTreeSet<PathBuf>,
    modules: &mut Vec<ProgramModule>,
    package: Option<String>,
) -> Result<(), Diagnostic> {
    match argument {
        Argument::CallExpression(call) => {
            if let Some(specifier) = call_static_require_specifier(call) {
                resolve_program_dependency(
                    ProgramDependencyRequest {
                        path,
                        specifier,
                        package: package.clone(),
                        import_mode: false,
                        span: call.span,
                    },
                    graph,
                    visiting,
                    modules,
                )?;
            }
            for nested in &call.arguments {
                analyze_commonjs_argument_requires(
                    path,
                    nested,
                    graph,
                    visiting,
                    modules,
                    package.clone(),
                )?;
            }
        }
        Argument::ParenthesizedExpression(parenthesized) => analyze_commonjs_expression_requires(
            path,
            &parenthesized.expression,
            graph,
            visiting,
            modules,
            package,
        )?,
        _ => {}
    }
    Ok(())
}

fn call_static_require_specifier<'a>(call: &'a CallExpression<'a>) -> Option<&'a str> {
    let Expression::Identifier(callee) = &call.callee else {
        return None;
    };
    if callee.name.as_str() != "require" || call.arguments.len() != 1 {
        return None;
    }
    call.arguments.first().and_then(string_argument)
}

fn ensure_node_compat_module(
    backing: &str,
    graph: &mut ModuleGraph,
    modules: &mut Vec<ProgramModule>,
) {
    if backing == "sloppy/node/fs" {
        ensure_node_compat_module("sloppy/node/fs/promises", graph, modules);
    }
    if backing == "sloppy/node/stream" {
        ensure_node_compat_module("sloppy/node/events", graph, modules);
    }
    if backing == "sloppy/node/diagnostics_channel" {
        ensure_node_compat_module("sloppy/node/events", graph, modules);
    }
    if backing == "sloppy/node/stream/promises" {
        ensure_node_compat_module("sloppy/node/stream", graph, modules);
    }
    if backing == "sloppy/node/assert/strict" {
        ensure_node_compat_module("sloppy/node/assert", graph, modules);
    }
    if modules.iter().any(|module| module.id == backing) {
        return;
    }
    let Some(source) = node_compat_module_source(backing) else {
        return;
    };
    graph.dependency_graph.ensure_defaults();
    graph.add_dependency_module(
        backing.to_string(),
        backing.to_string(),
        ModuleFormat::CommonJs,
        None,
        Some("node-compat-registry".to_string()),
    );
    modules.push(ProgramModule {
        id: backing.to_string(),
        source_name: backing.to_string(),
        source: source.to_string(),
        emitted_source: source.to_string(),
        format: ModuleFormat::CommonJs,
    });
}

const SLOPPY_SQLITE_PROVIDER_PROGRAM_MODULE: &str = r#"function validateSqliteProviderName(name){if(typeof name!=="string"||name.length===0){throw new TypeError("Sloppy sqlite provider name must be a non-empty string.");}if(name.trim()!==name||!/^[A-Za-z0-9_.-]+$/u.test(name)){throw new TypeError("Sloppy sqlite provider name must contain only letters, digits, dots, underscores, or hyphens.");}}function validateSqliteProviderOptions(options){if(options===undefined){return Object.freeze({});}if(options===null||typeof options!=="object"||Array.isArray(options)){throw new TypeError("Sloppy sqlite provider options must be a plain object.");}if(Object.prototype.hasOwnProperty.call(options,"database")&&typeof options.database!=="string"){throw new TypeError("Sloppy sqlite provider database option must be a string.");}return Object.freeze({...options});}function sqlite(name,options){validateSqliteProviderName(name);return Object.freeze({__sloppyProvider:true,kind:"sqlite",name,token:name.includes(".")?name:`data.${name}`,options:validateSqliteProviderOptions(options)});}module.exports={sqlite,Sqlite:sqlite,default:null};module.exports.default=module.exports;"#;

fn ensure_sloppy_provider_module(
    backing: &str,
    graph: &mut ModuleGraph,
    modules: &mut Vec<ProgramModule>,
) {
    if modules.iter().any(|module| module.id == backing) {
        return;
    }
    let source = match backing {
        "sloppy/providers/sqlite" => SLOPPY_SQLITE_PROVIDER_PROGRAM_MODULE,
        _ => return,
    };
    graph.dependency_graph.ensure_defaults();
    graph.add_dependency_module(
        backing.to_string(),
        backing.to_string(),
        ModuleFormat::CommonJs,
        None,
        Some("sloppy-stdlib-provider".to_string()),
    );
    modules.push(ProgramModule {
        id: backing.to_string(),
        source_name: backing.to_string(),
        source: source.to_string(),
        emitted_source: source.to_string(),
        format: ModuleFormat::CommonJs,
    });
}

fn node_compat_module_source(backing: &str) -> Option<&'static str> {
    match backing {
        "sloppy/node/path" => Some(NODE_PATH_SHIM),
        "sloppy/node/events" => Some(NODE_EVENTS_SHIM),
        "sloppy/node/url" => Some(NODE_URL_SHIM),
        "sloppy/node/querystring" => Some(NODE_QUERYSTRING_SHIM),
        "sloppy/node/buffer" => Some(NODE_BUFFER_SHIM),
        "sloppy/node/console" => Some(NODE_CONSOLE_SHIM),
        "sloppy/node/constants" => Some(NODE_CONSTANTS_SHIM),
        "sloppy/node/util" => Some(NODE_UTIL_SHIM),
        "sloppy/node/timers" => Some(NODE_TIMERS_SHIM),
        "sloppy/node/fs" => Some(NODE_FS_SHIM),
        "sloppy/node/fs/promises" => Some(NODE_FS_PROMISES_SHIM),
        "sloppy/node/os" => Some(NODE_OS_SHIM),
        "sloppy/node/process" => Some(NODE_PROCESS_SHIM),
        "sloppy/node/crypto" => Some(NODE_CRYPTO_SHIM),
        "sloppy/node/diagnostics_channel" => Some(NODE_DIAGNOSTICS_CHANNEL_SHIM),
        "sloppy/node/http" => Some(NODE_HTTP_SHIM),
        "sloppy/node/https" => Some(NODE_HTTPS_SHIM),
        "sloppy/node/module" => Some(NODE_MODULE_SHIM),
        "sloppy/node/perf_hooks" => Some(NODE_PERF_HOOKS_SHIM),
        "sloppy/node/assert" => Some(NODE_ASSERT_SHIM),
        "sloppy/node/assert/strict" => Some(NODE_ASSERT_STRICT_SHIM),
        "sloppy/node/stream" => Some(NODE_STREAM_SHIM),
        "sloppy/node/stream/promises" => Some(NODE_STREAM_PROMISES_SHIM),
        "sloppy/node/string_decoder" => Some(NODE_STRING_DECODER_SHIM),
        "sloppy/node/tty" => Some(NODE_TTY_SHIM),
        "sloppy/node/zlib" => Some(NODE_ZLIB_SHIM),
        _ => None,
    }
}

const NODE_PATH_SHIM: &str = r#"function parts(values){return values.filter((value)=>value!==undefined&&value!==null).map(String).filter(Boolean);}
function normalize(path){const input=String(path||"");const absolute=input.startsWith("/");const out=[];for(const part of input.replace(/\\/g,"/").split("/")){if(!part||part===".")continue;if(part===".."){if(out.length&&out[out.length-1]!==".."){out.pop();}else if(!absolute){out.push("..");}}else{out.push(part);}}return `${absolute?"/":""}${out.join("/")}`||".";}
function join(){return normalize(parts(Array.prototype.slice.call(arguments)).join("/"));}
function resolve(){let output="";for(const part of parts(Array.prototype.slice.call(arguments))){output=part.startsWith("/")?part:`${output}/${part}`;}return normalize(output||".");}
function basename(path,ext){let base=String(path).replace(/\\/g,"/").split("/").filter(Boolean).pop()||"";if(ext&&base.endsWith(ext)){base=base.slice(0,-String(ext).length);}return base;}
function dirname(path){const text=normalize(path);const index=text.lastIndexOf("/");if(index<=0)return text.startsWith("/")?"/":".";return text.slice(0,index);}
function extname(path){const base=basename(path);const index=base.lastIndexOf(".");return index>0?base.slice(index):"";}
function isAbsolute(path){return String(path).startsWith("/")||/^[A-Za-z]:[\\/]/.test(String(path));}
const posix=Object.freeze({join,resolve,basename,dirname,extname,normalize,isAbsolute,sep:"/",delimiter:":"});
const win32=Object.freeze({...posix,sep:"\\",delimiter:";"});
module.exports={join,resolve,basename,dirname,extname,normalize,isAbsolute,posix,win32,sep:"/",delimiter:":",default:null};module.exports.default=module.exports;"#;

const NODE_EVENTS_SHIM: &str = r#"const onceOriginal=Symbol("onceOriginal");class EventEmitter{constructor(){this._events=Object.create(null);}on(name,fn){if(typeof fn!=="function")throw new TypeError("listener must be a function");(this._events[name]??=[]).push(fn);return this;}addListener(name,fn){return this.on(name,fn);}off(name,fn){return this.removeListener(name,fn);}removeListener(name,fn){const list=this._events[name];if(!list)return this;this._events[name]=list.filter((item)=>item!==fn&&item[onceOriginal]!==fn);return this;}once(name,fn){if(typeof fn!=="function")throw new TypeError("listener must be a function");const wrapped=(...args)=>{this.removeListener(name,wrapped);return fn.apply(this,args);};wrapped[onceOriginal]=fn;return this.on(name,wrapped);}emit(name,...args){const list=(this._events[name]||[]).slice();for(const fn of list){fn.apply(this,args);}return list.length>0;}listenerCount(name){return (this._events[name]||[]).length;}removeAllListeners(name){if(name===undefined){this._events=Object.create(null);}else{delete this._events[name];}return this;}}
module.exports={EventEmitter,default:EventEmitter};"#;

const NODE_URL_SHIM: &str = r#"module.exports={URL:globalThis.URL,URLSearchParams:globalThis.URLSearchParams,pathToFileURL(path){return new URL(`file://${String(path).replace(/\\/g,"/")}`);},fileURLToPath(value){return new URL(value).pathname;},default:null};module.exports.default=module.exports;"#;

const NODE_QUERYSTRING_SHIM: &str = r#"function parse(text){const out=Object.create(null);for(const part of String(text||"").split("&")){if(!part)continue;const [k,v=""]=part.split("=");out[decodeURIComponent(k.replace(/\+/g," "))]=decodeURIComponent(v.replace(/\+/g," "));}return out;}function stringify(value){return Object.entries(value||{}).map(([k,v])=>`${encodeURIComponent(k)}=${encodeURIComponent(String(v))}`).join("&");}module.exports={parse,stringify,escape:encodeURIComponent,unescape:decodeURIComponent,default:null};module.exports.default=module.exports;"#;

pub(super) const NODE_BUFFER_SHIM: &str = r#"const rt=globalThis.__sloppy_runtime||{};function enc(e="utf8"){const n=String(e).toLowerCase();if(n==="utf-8")return"utf8";if(["utf8","hex","base64","base64url"].includes(n))return n;throw new TypeError(`Sloppy Buffer encoding is not supported: ${e}`);}function fromString(value,e){switch(enc(e)){case"utf8":return rt.Text.utf8.encode(value);case"hex":return rt.Hex.decode(value);case"base64":return rt.Base64.decode(value);case"base64url":return rt.Base64Url.decode(value,{padding:"optional"});}}function toStringValue(value,e){switch(enc(e)){case"utf8":return rt.Text.utf8.decode(value);case"hex":return rt.Hex.encode(value);case"base64":return rt.Base64.encode(value);case"base64url":return rt.Base64Url.encode(value);}}class Buffer extends Uint8Array{static from(value,encoding="utf8"){if(typeof value==="string")return new Buffer(fromString(value,encoding));if(value instanceof ArrayBuffer||ArrayBuffer.isView(value)||Array.isArray(value))return new Buffer(value);throw new TypeError("Buffer.from only supports string, ArrayBuffer, Uint8Array, or byte arrays in Sloppy.");}static alloc(size,fill=0){const buffer=new Buffer(size);buffer.fill(fill);return buffer;}static allocUnsafe(size){return Buffer.alloc(size);}static isBuffer(value){return value instanceof Buffer;}static isEncoding(value){if(value===undefined||value===null)return false;try{enc(value);return true;}catch{return false;}}static byteLength(value,encoding="utf8"){return Buffer.from(value,encoding).byteLength;}static compare(left,right){left=Buffer.from(left);right=Buffer.from(right);const length=Math.min(left.byteLength,right.byteLength);for(let i=0;i<length;i+=1){if(left[i]!==right[i])return left[i]<right[i]?-1:1;}return left.byteLength===right.byteLength?0:left.byteLength<right.byteLength?-1:1;}static concat(list,totalLength=undefined){if(!Array.isArray(list))throw new TypeError("Sloppy Buffer.concat expects an array.");const buffers=list.map((entry)=>Buffer.from(entry));const length=totalLength===undefined?buffers.reduce((sum,entry)=>sum+entry.byteLength,0):Number(totalLength);if(!Number.isInteger(length)||length<0)throw new TypeError("Sloppy Buffer.concat totalLength must be a non-negative integer.");const output=new Buffer(length);let offset=0;for(const buffer of buffers){output.set(buffer.subarray(0,Math.max(0,length-offset)),offset);offset+=buffer.byteLength;if(offset>=length)break;}return output;}equals(other){return Buffer.compare(this,other)===0;}compare(other){return Buffer.compare(this,other);}subarray(start=0,end=undefined){const view=super.subarray(start,end);Object.setPrototypeOf(view,Buffer.prototype);return view;}slice(start=0,end=undefined){return this.subarray(start,end);}toString(encoding="utf8"){return toStringValue(this,encoding);}write(value,offset=0,length=undefined,encoding="utf8"){if(typeof offset==="string"){encoding=offset;offset=0;length=undefined;}else if(typeof length==="string"){encoding=length;length=undefined;}const bytes=Buffer.from(String(value),encoding);const start=Number(offset);const count=Math.min(length===undefined?bytes.byteLength:Number(length),this.byteLength-start);if(!Number.isInteger(start)||start<0||!Number.isInteger(count)||count<0)throw new RangeError("Buffer.write offset and length are out of range.");this.set(bytes.subarray(0,count),start);return count;}readUInt8(offset=0){return this._read(offset,1,true);}readUInt16LE(offset=0){return this._read(offset,2,true);}readUInt16BE(offset=0){return this._read(offset,2,false);}readUInt32LE(offset=0){return this._read(offset,4,true);}readUInt32BE(offset=0){return this._read(offset,4,false);}writeUInt8(value,offset=0){return this._write(value,offset,1,true);}writeUInt16LE(value,offset=0){return this._write(value,offset,2,true);}writeUInt16BE(value,offset=0){return this._write(value,offset,2,false);}writeUInt32LE(value,offset=0){return this._write(value,offset,4,true);}writeUInt32BE(value,offset=0){return this._write(value,offset,4,false);}_bounds(offset,width){offset=Number(offset);if(!Number.isInteger(offset)||offset<0||offset+width>this.byteLength)throw new RangeError("Buffer offset is outside bounds.");return offset;}_read(offset,width,little){offset=this._bounds(offset,width);let value=0;for(let i=0;i<width;i+=1){value+=this[offset+(little?i:width-1-i)]*2**(8*i);}return value;}_write(value,offset,width,little){offset=this._bounds(offset,width);value=Number(value);const max=2**(8*width);if(!Number.isInteger(value)||value<0||value>=max)throw new RangeError("Buffer unsigned integer value is outside range.");for(let i=0;i<width;i+=1){this[offset+(little?i:width-1-i)]=Math.floor(value/2**(8*i))&255;}return offset+width;}}module.exports={Buffer,default:Buffer};"#;

const NODE_UTIL_SHIM: &str = r#"function inspect(value){if(typeof value==="string")return value;if(typeof value==="function")return value.name?`[Function: ${value.name}]`:"[Function]";try{return JSON.stringify(value,null,2);}catch(_){return String(value);}}function format(first="",...rest){if(typeof first!=="string")return[first,...rest].map(inspect).join(" ");let index=0;const text=first.replace(/%[sdjifoO%]/g,(token)=>{if(token==="%%")return"%";if(index>=rest.length)return token;const value=rest[index++];if(token==="%s")return String(value);if(token==="%d")return String(Number(value));if(token==="%i")return String(Number.parseInt(value,10));if(token==="%f")return String(Number.parseFloat(value));if(token==="%j"){try{return JSON.stringify(value);}catch{return"[Circular]";}}return inspect(value);});return[text,...rest.slice(index).map(inspect)].join(" ");}function promisify(fn){if(typeof fn!=="function")throw new TypeError("promisify expects a function");return function(){const args=Array.prototype.slice.call(arguments);return new Promise((resolve,reject)=>fn.call(this,...args,(error,value)=>error?reject(error):resolve(value)));};}function callbackify(fn){if(typeof fn!=="function")throw new TypeError("callbackify expects a function");return function(){const args=Array.prototype.slice.call(arguments);const cb=args.pop();Promise.resolve().then(()=>fn.apply(this,args)).then((value)=>cb(null,value),(error)=>cb(error));};}function inherits(ctor,superCtor){if(typeof ctor!=="function"||typeof superCtor!=="function")throw new TypeError("inherits expects constructor functions.");Object.setPrototypeOf(ctor.prototype,superCtor.prototype);Object.defineProperty(ctor.prototype,"constructor",{configurable:true,enumerable:false,value:ctor,writable:true});}module.exports={callbackify,format,inherits,inspect,promisify,types:{isArrayBufferView:ArrayBuffer.isView,isDate:(value)=>value instanceof Date,isRegExp:(value)=>value instanceof RegExp,isUint8Array(value){return value instanceof Uint8Array;}},default:null};module.exports.default=module.exports;"#;

const NODE_TIMERS_SHIM: &str = r#"function unsupported(name){return function(){throw new Error(`node:timers.${name} is not implemented by Sloppy's node:timers compatibility shim.`);};}module.exports={setTimeout:globalThis.setTimeout||unsupported("setTimeout"),clearTimeout:globalThis.clearTimeout||unsupported("clearTimeout"),setInterval:globalThis.setInterval||unsupported("setInterval"),clearInterval:globalThis.clearInterval||unsupported("clearInterval"),setImmediate:unsupported("setImmediate"),clearImmediate:unsupported("clearImmediate"),default:null};module.exports.default=module.exports;"#;

const NODE_FS_SHIM: &str = r#"const promises=require("sloppy/node/fs/promises");function unsupported(name){return function(){throw new Error(`node:fs.${name} is not implemented by Sloppy's node:fs compatibility shim. Use sloppy/fs when available, or avoid this package path.`);};}function callbackify(method,name){return function(){const args=Array.prototype.slice.call(arguments);const callback=args.pop();if(typeof callback!=="function")throw new TypeError(`node:fs.${name} requires a callback.`);Promise.resolve().then(()=>method(...args)).then((value)=>callback(null,value),(error)=>callback(error));};}const api={access:callbackify(promises.access,"access"),appendFile:callbackify(promises.appendFile,"appendFile"),copyFile:callbackify(promises.copyFile,"copyFile"),existsSync:unsupported("existsSync"),lstat:callbackify(promises.lstat,"lstat"),mkdir:callbackify(promises.mkdir,"mkdir"),mkdtemp:callbackify(promises.mkdtemp,"mkdtemp"),promises,readdir:callbackify(promises.readdir,"readdir"),readFile:callbackify(promises.readFile,"readFile"),readlink:callbackify(promises.readlink,"readlink"),realpath:callbackify(promises.realpath,"realpath"),rename:callbackify(promises.rename,"rename"),rm:callbackify(promises.rm,"rm"),stat:callbackify(promises.stat,"stat"),symlink:callbackify(promises.symlink,"symlink"),unlink:callbackify(promises.unlink,"unlink"),watch:unsupported("watch"),writeFile:callbackify(promises.writeFile,"writeFile"),default:null};module.exports=api;module.exports.default=api;"#;

const NODE_FS_PROMISES_SHIM: &str = r#"const rt=globalThis.__sloppy_runtime||{};function encoding(options,op){const e=typeof options==="string"?options:options?.encoding;if(e===undefined||e===null)return undefined;const n=String(e).toLowerCase();if(n==="utf8"||n==="utf-8")return"utf8";throw new TypeError(`SLOPPY_E_NODE_FS_UNSUPPORTED_ENCODING: ${op} only supports utf8 text encoding.`);}function recursiveOption(options,op){const value=options?.recursive;if(value===undefined)return false;if(typeof value!=="boolean")throw new TypeError(`${op} recursive option must be a boolean.`);return value;}function sloppyPath(path){if(typeof path!=="string")return path;const value=path.replace(/\\/g,"/");if(value.startsWith("/")||/^[A-Za-z]:\//.test(value)||value.startsWith("./")||value.startsWith("../")||value.includes("://"))return value;return `./${value}`;}function stats(stat){return Object.freeze({...stat,isFile(){return stat.kind==="file"||(stat.exists===true&&stat.kind!=="directory");},isDirectory(){return stat.kind==="directory";},isSymbolicLink(){return stat.kind==="symlink"||stat.symlink===true;}});}async function unsupported(name){throw new Error(`SLOPPY_E_NODE_FS_UNSUPPORTED: node:fs.promises.${name} is not implemented by Sloppy's Node compatibility shim.`);}async function readFile(path,options=undefined){const e=encoding(options,"node:fs.readFile");const target=sloppyPath(path);return e==="utf8"?rt.File.readText(target):rt.File.readBytes(target);}async function writeFile(path,data,options=undefined){encoding(options,"node:fs.writeFile");const target=sloppyPath(path);if(typeof data==="string")return rt.File.writeText(target,data);return rt.File.writeBytes(target,data);}async function appendFile(path,data,options=undefined){encoding(options,"node:fs.appendFile");const target=sloppyPath(path);if(typeof data==="string")return rt.File.appendText(target,data);return rt.File.appendBytes(target,data);}async function access(path){const stat=await rt.File.stat(sloppyPath(path));if(!stat.exists)throw new Error("SLOPPY_E_NODE_FS_ACCESS: path is not accessible.");}async function readdir(path){return(await rt.Directory.list(sloppyPath(path))).map((entry)=>entry.name);}async function rm(path,options=undefined){const target=sloppyPath(path);const stat=await rt.File.stat(target);if(stat.kind==="directory")return rt.Directory.delete(target,{recursive:recursiveOption(options,"node:fs.rm")});return rt.File.delete(target);}async function unlink(path){return rt.File.delete(sloppyPath(path));}async function copyFile(fromPath,toPath){return rt.File.copy(sloppyPath(fromPath),sloppyPath(toPath));}async function rename(fromPath,toPath){return rt.File.move(sloppyPath(fromPath),sloppyPath(toPath));}async function readlink(path){return rt.File.readLink(sloppyPath(path));}async function symlink(targetPath,linkPath,type=undefined){return rt.File.createSymlink(sloppyPath(targetPath),sloppyPath(linkPath),{directory:type==="dir"||type==="junction"});}const api={access,appendFile,copyFile,lstat:()=>unsupported("lstat"),mkdir:(path,options)=>rt.Directory.create(sloppyPath(path),{recursive:recursiveOption(options,"node:fs.mkdir")}),mkdtemp:()=>unsupported("mkdtemp"),readdir,readFile,readlink,realpath:()=>unsupported("realpath"),rename,rm,stat:async(path)=>stats(await rt.File.stat(sloppyPath(path))),symlink,unlink,writeFile};module.exports={...api,default:api};"#;

const NODE_OS_SHIM: &str = r#"function runtime(){return globalThis.__sloppy_runtime||{};}function platform(){return runtime().System?.platform||"sloppy";}function tmpdir(){return ".";}function homedir(){return ".";}module.exports={platform,tmpdir,homedir,EOL:"\n",default:null};module.exports.default=module.exports;"#;

const NODE_PROCESS_SHIM: &str = r#"const rt=globalThis.__sloppy_runtime||{};function monotonicMs(){const bridge=globalThis.__sloppy?.time;if(bridge&&typeof bridge.monotonicMs==="function")return bridge.monotonicMs();if(globalThis.performance&&typeof globalThis.performance.now==="function")return globalThis.performance.now();return Date.now();}const startedAt=monotonicMs();const events=Object.create(null);const env=new Proxy(Object.create(null),{get(target,key){if(typeof key!=="string")return undefined;return Object.prototype.hasOwnProperty.call(target,key)?target[key]:rt.Environment?.get(key);},has(target,key){return typeof key==="string"&&(Object.prototype.hasOwnProperty.call(target,key)||rt.Environment?.has?.(key)===true);},set(target,key,value){if(typeof key!=="string")return false;target[key]=String(value);return true;},ownKeys(target){return[...new Set([...Object.keys(target),...(rt.Environment?.list?.()??[])])];},getOwnPropertyDescriptor(target,key){const value=typeof key==="string"?(Object.prototype.hasOwnProperty.call(target,key)?target[key]:rt.Environment?.get(key)):undefined;return value===undefined?undefined:{enumerable:true,configurable:true,value};}});const sloppyVersion=String(rt.System?.version??"0.1.0");const version=sloppyVersion.startsWith("sloppy-")?sloppyVersion:`sloppy-${sloppyVersion}`;let exitCode=undefined;function on(name,fn){if(typeof fn!=="function")throw new TypeError("process listener must be a function");(events[name]??=[]).push(fn);return process;}function off(name,fn){events[name]=(events[name]||[]).filter((item)=>item!==fn);return process;}const addListener=on;const removeListener=off;function emit(name,...args){const list=(events[name]||[]).slice();for(const fn of list)fn(...args);return list.length>0;}function once(name,fn){const wrapped=(...args)=>{off(name,wrapped);fn(...args);};return on(name,wrapped);}function stream(name){return Object.freeze({isTTY:false,write(){throw new Error(`SLOPPY_E_PROCESS_STREAM_WRITE_UNSUPPORTED: process.${name}.write is not available in Sloppy's Node compatibility shim.`);}});}function memoryUsage(){return Object.freeze({rss:0,heapTotal:0,heapUsed:0,external:0,arrayBuffers:0});}function uptime(){return Math.max(0,(monotonicMs()-startedAt)/1000);}function hrtimeNanos(){return BigInt(Math.max(0,Math.floor(monotonicMs()*1000000)));}function hrtime(previous=undefined){const now=hrtimeNanos();const sec=Number(now/1000000000n);const ns=Number(now%1000000000n);if(previous===undefined)return[sec,ns];const prev=BigInt(previous[0])*1000000000n+BigInt(previous[1]);const diff=now-prev;return[Number(diff/1000000000n),Number(diff%1000000000n)];}hrtime.bigint=hrtimeNanos;const process={env,get argv(){return Array.isArray(globalThis.__sloppy_program_args)?globalThis.__sloppy_program_args.slice():[];},browser:false,cwd(){return globalThis.__sloppy_program_context?.cwd??".";},get platform(){return rt.System?.platform??"sloppy";},get arch(){return rt.System?.arch??"unknown";},version,versions:Object.freeze({sloppy:version.replace(/^sloppy-/,"")}),release:Object.freeze({name:"sloppy"}),stdin:Object.freeze({isTTY:false}),stdout:stream("stdout"),stderr:stream("stderr"),memoryUsage,uptime,hrtime,on,off,addListener,removeListener,emit,once,get exitCode(){return exitCode;},set exitCode(value){exitCode=value;},nextTick(fn,...args){Promise.resolve().then(()=>fn(...args));},exit(code=0){throw new Error(`SLOPPY_E_PROCESS_EXIT_UNSUPPORTED: process.exit(${code}) is not supported.`);}};module.exports=process;module.exports.default=process;"#;

const NODE_CRYPTO_SHIM: &str = r#"const rt=globalThis.__sloppy_runtime||{};function alg(value,op){const normalized=String(value).toLowerCase().replace(/-/g,"");if(["sha256","sha384","sha512"].includes(normalized))return normalized;throw new TypeError(`SLOPPY_E_NODE_CRYPTO_UNSUPPORTED: node:crypto.${op} only supports sha256, sha384, and sha512.`);}function randomBytes(size){return rt.Random.bytes(size);}function randomUUID(){return rt.Random.uuid();}function createHash(algorithm){return rt.Hash.create(alg(algorithm,"createHash"));}function bytes(value,op){if(typeof value==="string")return rt.Text.utf8.encode(value);if(value instanceof ArrayBuffer)return new Uint8Array(value).slice();if(ArrayBuffer.isView(value))return new Uint8Array(value.buffer,value.byteOffset,value.byteLength).slice();throw new TypeError(`${op} requires a string, ArrayBuffer, or typed-array bytes.`);}function secret(value){return typeof value==="string"?rt.Secret.fromUtf8(value):rt.Secret.fromBytes(bytes(value,"node:crypto.createHmac key"));}function createHmac(algorithm,key){const normalized=alg(algorithm,"createHmac");if(normalized!=="sha256")throw new TypeError("SLOPPY_E_NODE_CRYPTO_UNSUPPORTED: node:crypto.createHmac currently supports sha256.");const keySecret=secret(key);const chunks=[];return{update(value){chunks.push(bytes(value,"node:crypto.Hmac.update"));return this;},async digest(encoding=undefined){const input=new Uint8Array(chunks.reduce((total,chunk)=>total+chunk.byteLength,0));let offset=0;for(const chunk of chunks){input.set(chunk,offset);offset+=chunk.byteLength;}const out=await rt.Hmac.sha256(keySecret,input);if(encoding==="hex")return rt.Hex.encode(out);if(encoding!==undefined&&encoding!=="bytes")throw new TypeError("Sloppy node:crypto Hmac digest encoding must be bytes or hex.");return out;}};}function timingSafeEqual(left,right){if(left.byteLength!==right.byteLength)throw new RangeError("Input buffers must have the same byte length.");return rt.ConstantTime.equals(left,right);}module.exports={createHash,createHmac,randomBytes,randomUUID,timingSafeEqual,default:null};module.exports.default=module.exports;"#;

const NODE_ASSERT_SHIM: &str = r#"class AssertionError extends Error{constructor(options={}){super(options.message??"Assertion failed");this.name="AssertionError";this.actual=options.actual;this.expected=options.expected;this.operator=options.operator;this.code="ERR_ASSERTION";}}function internalFail(options){throw new AssertionError(options);}function fail(actual=undefined,expected=undefined,message=undefined,operator="!="){if(arguments.length===0)internalFail({message:"Failed",operator:"fail"});if(arguments.length===1){if(actual instanceof Error)throw actual;internalFail({message:actual===undefined?"Failed":actual,operator:"fail"});}internalFail({actual,expected,message:message??`${actual} ${operator} ${expected}`,operator});}function ok(value,message=undefined){if(!value)internalFail({actual:value,expected:true,operator:"ok",message});}function equal(actual,expected,message=undefined){if(actual!=expected)internalFail({actual,expected,operator:"==",message});}function strictEqual(actual,expected,message=undefined){if(!Object.is(actual,expected))internalFail({actual,expected,operator:"strictEqual",message});}function notStrictEqual(actual,expected,message=undefined){if(Object.is(actual,expected))internalFail({actual,expected,operator:"notStrictEqual",message});}function stable(value){if(value===null||typeof value!=="object")return JSON.stringify(value);if(Array.isArray(value))return`[${value.map(stable).join(",")}]`;return`{${Object.keys(value).sort().map((key)=>`${JSON.stringify(key)}:${stable(value[key])}`).join(",")}}`;}function deepStrictEqual(actual,expected,message=undefined){if(stable(actual)!==stable(expected))internalFail({actual,expected,operator:"deepStrictEqual",message});}const deepEqual=deepStrictEqual;function matchExpected(error,expected){if(expected===undefined)return true;if(typeof expected==="function"){try{if(error instanceof expected)return true;}catch{}try{return expected(error)===true;}catch{return false;}}if(expected instanceof RegExp)return expected.test(String(error?.message??error));if(expected!==null&&typeof expected==="object")return Object.entries(expected).every(([key,value])=>Object.is(error?.[key],value));return false;}function mismatch(error,expected,operator,message){internalFail({actual:error,expected,operator,message:message??`${operator} validation failed.`});}function isPromiseLike(value){return value!==null&&(typeof value==="object"||typeof value==="function")&&typeof value.then==="function";}function requirePromiseLike(value,operator){if(!isPromiseLike(value))throw new TypeError(`${operator} expects a Promise or a function returning a Promise.`);return value;}function throws(fn,expected=undefined,message=undefined){if(typeof fn!=="function")throw new TypeError("assert.throws expects a function.");try{fn();}catch(error){if(!matchExpected(error,expected))mismatch(error,expected,"throws",message);return error;}internalFail({actual:undefined,expected,operator:"throws",message});}function doesNotThrow(fn,expected=undefined,message=undefined){if(typeof fn!=="function")throw new TypeError("assert.doesNotThrow expects a function.");if(typeof expected==="string"&&message===undefined){message=expected;expected=undefined;}try{fn();}catch(error){if(expected===undefined||matchExpected(error,expected))mismatch(error,expected,"doesNotThrow",message);throw error;}}async function rejects(fn,expected=undefined,message=undefined){const promise=requirePromiseLike(typeof fn==="function"?fn():fn,"assert.rejects");try{await promise;}catch(error){if(!matchExpected(error,expected))mismatch(error,expected,"rejects",message);return error;}internalFail({actual:undefined,expected,operator:"rejects",message});}async function doesNotReject(fn,expected=undefined,message=undefined){if(typeof expected==="string"&&message===undefined){message=expected;expected=undefined;}const promise=requirePromiseLike(typeof fn==="function"?fn():fn,"assert.doesNotReject");try{await promise;}catch(error){if(expected===undefined||matchExpected(error,expected))mismatch(error,expected,"doesNotReject",message);throw error;}}function ifError(value){if(value!==null&&value!==undefined)internalFail({actual:value,expected:null,operator:"ifError",message:value?.message});}function assert(value,message=undefined){return ok(value,message);}Object.assign(assert,{AssertionError,deepEqual,deepStrictEqual,doesNotReject,doesNotThrow,equal,fail,ifError,notStrictEqual,ok,rejects,strictEqual,throws});module.exports=assert;module.exports.default=assert;"#;
const NODE_ASSERT_STRICT_SHIM: &str = r#"const base=require("sloppy/node/assert");function strict(value,message=undefined){return base.ok(value,message);}Object.assign(strict,base,{equal:base.strictEqual});module.exports=strict;module.exports.default=strict;"#;

const NODE_STREAM_SHIM: &str = r#"const {EventEmitter}=require("sloppy/node/events");class Stream extends EventEmitter{}class Readable extends Stream{static from(iterable){const readable=new Readable();readable._iterable=iterable;return readable;}async *[Symbol.asyncIterator](){for await(const chunk of this._iterable??[])yield chunk;}}class Writable extends Stream{constructor(options={}){super();this._write=typeof options.write==="function"?options.write:undefined;this.chunks=[];this._pendingWrites=new Set();this._finished=Promise.resolve(this);}_invokeWrite(chunk){return new Promise((resolve,reject)=>{try{if(this._write){if(this._write.length>=3)this._write(chunk,undefined,(error)=>error?reject(error):resolve());else Promise.resolve(this._write(chunk)).then(resolve,reject);}else{this.chunks.push(chunk);resolve();}}catch(error){reject(error);}});}_trackWrite(promise){const tracked=Promise.resolve(promise).finally(()=>this._pendingWrites.delete(tracked));this._pendingWrites.add(tracked);return tracked;}write(chunk,encodingOrCallback=undefined,callback=undefined){const done=typeof encodingOrCallback==="function"?encodingOrCallback:callback;this._trackWrite(this._invokeWrite(chunk)).then(()=>{if(typeof done==="function")done();this.emit("drain");},(error)=>{if(typeof done==="function")done(error);this.emit("error",error);});return true;}end(chunk=undefined,encodingOrCallback=undefined,callback=undefined){if(typeof chunk==="function"){callback=chunk;chunk=undefined;}else if(typeof encodingOrCallback==="function")callback=encodingOrCallback;if(chunk!==undefined)this.write(chunk);if(typeof callback==="function")this.once("finish",callback);this._finished=Promise.all([...this._pendingWrites]).then(()=>{this.emit("finish");return this;},(error)=>{this.emit("error",error);throw error;});return this;}get finished(){return this._finished;}}class PassThrough extends Writable{write(chunk,encodingOrCallback=undefined,callback=undefined){super.write(chunk,encodingOrCallback,callback);this.emit("data",chunk);return true;}}async function pipeline(source,destination,callback=undefined){try{for await(const chunk of source)destination.write(chunk);destination.end();await destination.finished;if(typeof callback==="function")callback();return destination;}catch(error){if(typeof callback==="function"){callback(error);return destination;}throw error;}}module.exports={PassThrough,Readable,Stream,Writable,pipeline,default:null};module.exports.default=module.exports;"#;
const NODE_STREAM_PROMISES_SHIM: &str = r#"const stream=require("sloppy/node/stream");module.exports={pipeline:stream.pipeline,default:{pipeline:stream.pipeline}};"#;

const NODE_CONSOLE_SHIM: &str = r#"module.exports=globalThis.console||{log(){},error(){},warn(){},info(){},debug(){}};function Console(){};Console.prototype.log=module.exports.log;Console.prototype.error=module.exports.error;Console.prototype.warn=module.exports.warn;Console.prototype.info=module.exports.info;Console.prototype.debug=module.exports.debug;module.exports.Console=module.exports.Console||Console;module.exports.default=module.exports;"#;
const NODE_CONSTANTS_SHIM: &str = r#"module.exports={O_RDONLY:0,O_WRONLY:1,O_RDWR:2,S_IFMT:61440,S_IFREG:32768,S_IFDIR:16384,default:null};module.exports.default=module.exports;"#;
const NODE_STRING_DECODER_SHIM: &str = r#"const rt=globalThis.__sloppy_runtime||{};class StringDecoder{constructor(encoding="utf8"){const n=String(encoding).toLowerCase();if(n!=="utf8"&&n!=="utf-8")throw new TypeError("Sloppy string_decoder only supports utf8.");this._decoder=rt.Text.utf8.decoder();}write(buffer){const bytes=buffer instanceof Uint8Array?buffer:new Uint8Array(buffer);return this._decoder.decode(bytes,{stream:true});}end(buffer=undefined){const text=buffer===undefined?"":this.write(buffer);return text+this._decoder.finish();}}module.exports={StringDecoder,default:null};module.exports.default=module.exports;"#;
const NODE_MODULE_SHIM: &str = r#"const builtinModules=Object.freeze(["assert","assert/strict","buffer","console","constants","crypto","diagnostics_channel","events","fs","fs/promises","http","https","module","os","path","perf_hooks","process","querystring","stream","stream/promises","string_decoder","timers","tty","url","util","zlib"]);function createRequire(source){if(typeof globalThis.__sloppy_program_create_require==="function")return globalThis.__sloppy_program_create_require(source);throw new Error("SLOPPY_E_MODULE_REQUIRE_UNAVAILABLE: createRequire is only available in bundled Sloppy programs.");}const Module=Object.freeze({builtinModules,createRequire});module.exports={Module,builtinModules,createRequire,default:null};module.exports.default=module.exports;"#;
const NODE_PERF_HOOKS_SHIM: &str = r#"const fallbackTimeOrigin=Date.now()-(typeof process!=="undefined"&&typeof process.uptime==="function"?process.uptime()*1000:0);const performance=globalThis.performance||Object.freeze({timeOrigin:fallbackTimeOrigin,now:()=>Date.now()-fallbackTimeOrigin});module.exports={performance,default:null};module.exports.default=module.exports;"#;
const NODE_DIAGNOSTICS_CHANNEL_SHIM: &str = r#"const {EventEmitter}=require("sloppy/node/events");const channels=new Map();class Channel extends EventEmitter{constructor(name){super();this.name=name;}publish(message){this.emit("message",message,this.name);}subscribe(listener){this.on("message",listener);}unsubscribe(listener){this.removeListener("message",listener);}hasSubscribers(){return this.listenerCount("message")>0;}}function channel(name){name=String(name);if(!channels.has(name))channels.set(name,new Channel(name));return channels.get(name);}function hasSubscribers(name){return channels.get(String(name))?.hasSubscribers()??false;}module.exports={channel,hasSubscribers,default:null};module.exports.default=module.exports;"#;
const NODE_TTY_SHIM: &str = r#"function isatty(){return false;}class ReadStream{constructor(){this.isTTY=false;}}class WriteStream{constructor(){this.isTTY=false;}}module.exports={isatty,ReadStream,WriteStream,default:null};module.exports.default=module.exports;"#;
const NODE_HTTP_SHIM: &str = r#"function unsupported(name){return function(){throw new Error(`SLOPPY_E_NODE_HTTP_UNSUPPORTED: node:http.${name} is not implemented by Sloppy's Node compatibility shim.`);};}module.exports={request:unsupported("request"),get:unsupported("get"),default:null};module.exports.default=module.exports;"#;
const NODE_HTTPS_SHIM: &str = r#"function unsupported(name){return function(){throw new Error(`SLOPPY_E_NODE_HTTPS_UNSUPPORTED: node:https.${name} is not implemented by Sloppy's Node compatibility shim.`);};}module.exports={request:unsupported("request"),get:unsupported("get"),default:null};module.exports.default=module.exports;"#;
const NODE_ZLIB_SHIM: &str = r#"const rt=globalThis.__sloppy_runtime||{};function bytes(value){if(typeof value==="string")return rt.Text.utf8.encode(value);if(value instanceof Uint8Array)return value;if(value instanceof ArrayBuffer)return new Uint8Array(value);if(ArrayBuffer.isView(value))return new Uint8Array(value.buffer,value.byteOffset,value.byteLength);throw new TypeError("Sloppy node:zlib input must be a string or bytes.");}function cb(factory,callback){if(typeof callback!=="function")throw new TypeError("Sloppy node:zlib callback API requires a callback.");factory().then((value)=>callback(null,value),(error)=>callback(error));}function gzip(input,options,callback){if(typeof options==="function"){callback=options;options=undefined;}cb(()=>rt.Compression.gzip(bytes(input),options),callback);}function gunzip(input,options,callback){if(typeof options==="function"){callback=options;options=undefined;}cb(()=>rt.Compression.gunzip(bytes(input),options),callback);}function unsupportedAsync(name,input,options,callback){if(typeof options==="function"){callback=options;options=undefined;}cb(()=>Promise.reject(new Error(`SLOPPY_E_NODE_ZLIB_UNSUPPORTED: node:zlib.${name} is not implemented by Sloppy's Node compatibility shim.`)),callback);}function deflate(input,options,callback){unsupportedAsync("deflate",input,options,callback);}function inflate(input,options,callback){unsupportedAsync("inflate",input,options,callback);}function unsupported(name){return function(){throw new Error(`SLOPPY_E_NODE_ZLIB_SYNC_UNSUPPORTED: node:zlib.${name} is not available because Sloppy compression is async.`);};}module.exports={gzip,gunzip,deflate,inflate,gzipSync:unsupported("gzipSync"),gunzipSync:unsupported("gunzipSync"),deflateSync:unsupported("deflateSync"),inflateSync:unsupported("inflateSync"),default:null};module.exports.default=module.exports;"#;

struct ProgramDependencyRequest<'a> {
    path: &'a Path,
    specifier: &'a str,
    package: Option<String>,
    import_mode: bool,
    span: Span,
}

fn package_export_unsupported_diagnostic(
    path: &Path,
    span: Span,
    failure: &resolver::PackageExportFailure,
) -> Diagnostic {
    Diagnostic::new(
        "SLOPPYC_E_PACKAGE_EXPORT_UNSUPPORTED",
        format!(
            "Package \"{}\" {} entry at \"{}\" is not supported: {}.",
            failure.subject, failure.field, failure.subpath, failure.reason
        ),
    )
    .with_path(path)
    .with_span(span)
    .with_hint(
        "Use a supported package.json exports/imports condition or fall back to a main entry.",
    )
}

fn resolve_program_dependency(
    request: ProgramDependencyRequest<'_>,
    graph: &mut ModuleGraph,
    visiting: &mut BTreeSet<PathBuf>,
    modules: &mut Vec<ProgramModule>,
) -> Result<Option<String>, Diagnostic> {
    let path = request.path;
    let specifier = request.specifier;
    let package = request.package;
    let span = request.span;
    let from_id = program_module_id(graph, path, package.as_deref());
    match resolver::classify_import_with_mode(path, specifier, request.import_mode) {
        resolver::ImportKind::Relative(resolved) => {
            let resolved =
                validate_program_relative_module(path, &resolved, graph, package.as_deref(), span)?;
            let source = fs::read_to_string(&resolved).map_err(|error| {
                Diagnostic::new(
                    "SLOPPYC_E_INPUT",
                    format!("failed to read module: {error}"),
                )
                .with_path(&resolved)
            })?;
            let resolved_id = program_module_id(graph, &resolved, package.as_deref());
            graph.add_dependency_import(&from_id, specifier, &resolved_id, "relative");
            extract_program_module_with_context(
                &resolved,
                &source,
                graph,
                visiting,
                modules,
                package,
                Some(from_id),
            )?;
            Ok(Some(resolved_id))
        }
        resolver::ImportKind::Package(package_resolution) => {
            graph.add_package_record(&package_resolution);
            let source = fs::read_to_string(&package_resolution.entry).map_err(|error| {
                Diagnostic::new(
                    "SLOPPYC_E_INPUT",
                    format!("failed to read package module: {error}"),
                )
                .with_path(&package_resolution.entry)
            })?;
            let resolved_id =
                program_module_id(graph, &package_resolution.entry, Some(&package_resolution.name));
            graph.add_dependency_import(&from_id, specifier, &resolved_id, "package");
            extract_program_module_with_context(
                &package_resolution.entry,
                &source,
                graph,
                visiting,
                modules,
                Some(package_resolution.name),
                Some(from_id),
            )?;
            Ok(Some(resolved_id))
        }
        resolver::ImportKind::NodeBuiltin(builtin) if builtin.status != "unsupported" => {
            graph.add_node_builtin(&builtin, path);
            if let Some(backing) = builtin.backing {
                ensure_node_compat_module(backing, graph, modules);
                graph.add_dependency_import(&from_id, specifier, backing, "node-compat-shim");
                Ok(Some(backing.to_string()))
            } else {
                Ok(None)
            }
        }
        resolver::ImportKind::SqliteProvider => {
            ensure_sloppy_provider_module("sloppy/providers/sqlite", graph, modules);
            graph.add_dependency_import(
                &from_id,
                specifier,
                "sloppy/providers/sqlite",
                "sloppy-provider",
            );
            Ok(Some("sloppy/providers/sqlite".to_string()))
        }
        resolver::ImportKind::NodeBuiltin(builtin) => Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_NODE_BUILTIN",
            format!(
                "{} is not supported by Sloppy's Node compatibility registry yet.",
                builtin.specifier
            ),
        )
        .with_path(path)
        .with_span(span)
        .with_hint("Use a Sloppy stdlib API or a package path that avoids this Node builtin.")),
        resolver::ImportKind::NativeAddonUnsupported(package_resolution) => Err(Diagnostic::new(
            "SLOPPYC_E_NATIVE_ADDON_UNSUPPORTED",
            format!(
                "Package \"{}\" requires a native Node addon. Sloppy does not support Node native addons yet.",
                package_resolution.name
            ),
        )
        .with_path(path)
        .with_span(span)
        .with_hint("Use a pure-JavaScript package entry or remove the native addon dependency.")),
        resolver::ImportKind::PackageExportUnsupported(failure) => {
            Err(package_export_unsupported_diagnostic(path, span, &failure))
        }
        resolver::ImportKind::UnsupportedBare(specifier) => Err(Diagnostic::new(
            "SLOPPYC_E_PACKAGE_NOT_FOUND",
            format!("Package \"{specifier}\" was not found from {}.", source_map_source_name(path)),
        )
        .with_path(path)
        .with_span(span)
        .with_hint(format!(
            "Install it with your package manager, for example:\n  npm install {specifier}"
        ))),
        resolver::ImportKind::UnresolvedRelative(specifier) => Err(Diagnostic::new(
            "SLOPPYC_E_MISSING_RELATIVE_IMPORT",
            format!("relative import \"{specifier}\" could not be resolved"),
        )
        .with_path(path)
        .with_span(span)
        .with_hint("Use a relative .js/.mjs/.cjs/.ts/.tsx/.json module inside the source root.")),
        resolver::ImportKind::Remote(specifier) => Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_IMPORT",
            format!("remote import \"{specifier}\" is outside the sealed Sloppy artifact graph"),
        )
        .with_path(path)
        .with_span(span)),
        _ => Ok(None),
    }
}

fn program_import_has_runtime_value(import: &ImportDeclaration<'_>) -> bool {
    if import.import_kind == ImportOrExportKind::Type {
        return false;
    }
    let Some(specifiers) = &import.specifiers else {
        return true;
    };
    specifiers.iter().any(|specifier| match specifier {
        ImportDeclarationSpecifier::ImportSpecifier(specifier) => {
            specifier.import_kind != ImportOrExportKind::Type
        }
        ImportDeclarationSpecifier::ImportDefaultSpecifier(_)
        | ImportDeclarationSpecifier::ImportNamespaceSpecifier(_) => true,
    })
}

fn validate_program_stdlib_import(
    path: &Path,
    import: &ImportDeclaration<'_>,
    import_kind: &resolver::ImportKind,
) -> Result<(), Diagnostic> {
    match import_kind {
        resolver::ImportKind::SlopFilesystem => validate_module_sloppy_fs_import(path, import),
        resolver::ImportKind::SlopData => validate_module_sloppy_data_import(path, import),
        resolver::ImportKind::SlopTime => validate_module_sloppy_time_import(path, import),
        resolver::ImportKind::SlopCrypto => validate_module_sloppy_crypto_import(path, import),
        resolver::ImportKind::SlopCodec => validate_module_sloppy_codec_import(path, import),
        resolver::ImportKind::SlopCache => validate_module_sloppy_cache_import(path, import),
        resolver::ImportKind::SlopNet => validate_module_sloppy_net_import(path, import),
        resolver::ImportKind::SlopHttp => validate_module_sloppy_http_import(path, import),
        resolver::ImportKind::SlopWebhooks => validate_module_sloppy_webhooks_import(path, import),
        resolver::ImportKind::SlopRedis => validate_module_sloppy_redis_import(path, import),
        resolver::ImportKind::SlopOs => validate_module_sloppy_os_import(path, import),
        resolver::ImportKind::SlopOrm => validate_module_sloppy_orm_import(path, import),
        resolver::ImportKind::SlopWorkers => validate_module_sloppy_workers_import(path, import),
        resolver::ImportKind::SlopFfi => validate_module_sloppy_ffi_import(path, import),
        resolver::ImportKind::SqliteProvider => {
            validate_module_sloppy_sqlite_provider_import(path, import)
        }
        resolver::ImportKind::SlopStdlib => {
            validate_module_sloppy_root_import(path, import).map(|_| ())
        }
        _ => Ok(()),
    }
}

fn transpile_program_typescript<'a>(
    path: &Path,
    allocator: &'a Allocator,
    program: &mut oxc_ast::ast::Program<'a>,
) -> Result<String, Diagnostic> {
    let semantic = SemanticBuilder::new()
        .with_excess_capacity(2.0)
        .with_enum_eval(true)
        .build(program);
    if let Some(error) = semantic.errors.into_iter().next() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_TYPESCRIPT",
            format!("unsupported Program Mode syntax: {error}"),
        )
        .with_path(path)
        .with_hint(
            "Program Mode uses the Oxc TypeScript transform and does not type-check code.",
        ));
    }
    let mut options = TransformOptions::default();
    options.env.module = Module::Preserve;
    let transform = Transformer::new(allocator, path, &options)
        .build_with_scoping(semantic.semantic.into_scoping(), program);
    if let Some(error) = transform.errors.into_iter().next() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_TYPESCRIPT",
            format!("unsupported Program Mode transform: {error}"),
        )
        .with_path(path));
    }
    Ok(Codegen::new().build(program).code)
}

fn rewrite_program_module_exports(
    path: &Path,
    source: &str,
    graph: &ModuleGraph,
    package: Option<String>,
) -> Result<String, Diagnostic> {
    let allocator = Allocator::default();
    let parsed = Parser::new(&allocator, source, SourceType::mjs()).parse();
    if let Some(error) = parsed.errors.into_iter().next() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_TRANSFORM",
            format!("failed to parse transformed Program Mode JavaScript: {error}"),
        )
        .with_path(path));
    }

    let mut replacements = Vec::<ProgramReplacement>::new();
    let mut is_esm = false;
    for statement in &parsed.program.body {
        match statement {
            Statement::ImportDeclaration(import) => {
                is_esm = true;
                replacements.push(ProgramReplacement {
                    span: import.span,
                    text: program_import_replacement(path, import, graph, package.as_deref())?,
                });
            }
            Statement::ExportNamedDeclaration(export) => {
                is_esm = true;
                replacements.push(ProgramReplacement {
                    span: export.span,
                    text: program_named_export_replacement(
                        path,
                        source,
                        export,
                        graph,
                        package.as_deref(),
                    )?,
                });
            }
            Statement::ExportDefaultDeclaration(export) => {
                is_esm = true;
                replacements.push(ProgramReplacement {
                    span: export.span,
                    text: program_default_export_replacement(source, export),
                });
            }
            Statement::ExportAllDeclaration(export) => {
                is_esm = true;
                replacements.push(ProgramReplacement {
                    span: export.span,
                    text: program_export_all_replacement(path, export, graph, package.as_deref())?,
                });
            }
            _ => {}
        }
        if !matches!(statement, Statement::ExportNamedDeclaration(_)) {
            collect_dynamic_import_replacements(
                path,
                source,
                statement,
                graph,
                package.as_deref(),
                &mut replacements,
            );
        }
    }
    let output = apply_program_replacements(source, &replacements);
    if is_esm {
        Ok(format!(
            "Object.defineProperty(exports,\"__esModule\",{{value:true}});\n{output}"
        ))
    } else {
        Ok(output)
    }
}

fn collect_dynamic_import_replacements(
    path: &Path,
    source: &str,
    statement: &Statement<'_>,
    graph: &ModuleGraph,
    package: Option<&str>,
    replacements: &mut Vec<ProgramReplacement>,
) {
    match statement {
        Statement::VariableDeclaration(declaration) => {
            for declarator in &declaration.declarations {
                if let Some(init) = &declarator.init {
                    collect_dynamic_import_expression_replacements(
                        path,
                        source,
                        init,
                        graph,
                        package,
                        replacements,
                    );
                }
            }
        }
        Statement::ExpressionStatement(statement) => {
            collect_dynamic_import_expression_replacements(
                path,
                source,
                &statement.expression,
                graph,
                package,
                replacements,
            )
        }
        Statement::ReturnStatement(statement) => {
            if let Some(argument) = &statement.argument {
                collect_dynamic_import_expression_replacements(
                    path,
                    source,
                    argument,
                    graph,
                    package,
                    replacements,
                );
            }
        }
        Statement::BlockStatement(block) => {
            for statement in &block.body {
                collect_dynamic_import_replacements(
                    path,
                    source,
                    statement,
                    graph,
                    package,
                    replacements,
                );
            }
        }
        Statement::FunctionDeclaration(function) => {
            if let Some(body) = &function.body {
                for statement in &body.statements {
                    collect_dynamic_import_replacements(
                        path,
                        source,
                        statement,
                        graph,
                        package,
                        replacements,
                    );
                }
            }
        }
        Statement::ExportNamedDeclaration(export) => {
            if let Some(declaration) = &export.declaration {
                collect_dynamic_import_declaration_replacements(
                    path,
                    source,
                    declaration,
                    graph,
                    package,
                    replacements,
                );
            }
        }
        _ => {}
    }
}

fn collect_dynamic_import_declaration_replacements(
    path: &Path,
    source: &str,
    declaration: &Declaration<'_>,
    graph: &ModuleGraph,
    package: Option<&str>,
    replacements: &mut Vec<ProgramReplacement>,
) {
    match declaration {
        Declaration::FunctionDeclaration(function) => {
            if let Some(body) = &function.body {
                for statement in &body.statements {
                    collect_dynamic_import_replacements(
                        path,
                        source,
                        statement,
                        graph,
                        package,
                        replacements,
                    );
                }
            }
        }
        Declaration::VariableDeclaration(declaration) => {
            for declarator in &declaration.declarations {
                if let Some(init) = &declarator.init {
                    collect_dynamic_import_expression_replacements(
                        path,
                        source,
                        init,
                        graph,
                        package,
                        replacements,
                    );
                }
            }
        }
        _ => {}
    }
}

fn collect_dynamic_import_expression_replacements(
    path: &Path,
    source: &str,
    expression: &Expression<'_>,
    graph: &ModuleGraph,
    package: Option<&str>,
    replacements: &mut Vec<ProgramReplacement>,
) {
    match expression {
        Expression::ImportExpression(import) => {
            let argument =
                source_slice(source, import.source.span()).unwrap_or_else(|| "\"\"".to_string());
            replacements.push(ProgramReplacement {
                span: import.span,
                text: format!(
                    "__sloppy_program_import_from({}, {argument})",
                    json_string(&program_module_id(graph, path, package))
                ),
            });
        }
        Expression::AwaitExpression(await_expression) => {
            collect_dynamic_import_expression_replacements(
                path,
                source,
                &await_expression.argument,
                graph,
                package,
                replacements,
            )
        }
        Expression::CallExpression(call) => {
            collect_dynamic_import_expression_replacements(
                path,
                source,
                &call.callee,
                graph,
                package,
                replacements,
            );
            for argument in &call.arguments {
                collect_dynamic_import_argument_replacements(
                    path,
                    source,
                    argument,
                    graph,
                    package,
                    replacements,
                );
            }
        }
        Expression::ParenthesizedExpression(parenthesized) => {
            collect_dynamic_import_expression_replacements(
                path,
                source,
                &parenthesized.expression,
                graph,
                package,
                replacements,
            )
        }
        Expression::ArrowFunctionExpression(function) => {
            for statement in &function.body.statements {
                collect_dynamic_import_replacements(
                    path,
                    source,
                    statement,
                    graph,
                    package,
                    replacements,
                );
            }
        }
        _ => {}
    }
}

fn collect_dynamic_import_argument_replacements(
    path: &Path,
    source: &str,
    argument: &Argument<'_>,
    graph: &ModuleGraph,
    package: Option<&str>,
    replacements: &mut Vec<ProgramReplacement>,
) {
    match argument {
        Argument::ImportExpression(import) => {
            let argument =
                source_slice(source, import.source.span()).unwrap_or_else(|| "\"\"".to_string());
            replacements.push(ProgramReplacement {
                span: import.span,
                text: format!(
                    "__sloppy_program_import_from({}, {argument})",
                    json_string(&program_module_id(graph, path, package))
                ),
            });
        }
        Argument::CallExpression(call) => {
            for argument in &call.arguments {
                collect_dynamic_import_argument_replacements(
                    path,
                    source,
                    argument,
                    graph,
                    package,
                    replacements,
                );
            }
        }
        Argument::ParenthesizedExpression(parenthesized) => {
            collect_dynamic_import_expression_replacements(
                path,
                source,
                &parenthesized.expression,
                graph,
                package,
                replacements,
            )
        }
        _ => {}
    }
}

struct ProgramReplacement {
    span: Span,
    text: String,
}

fn apply_program_replacements(source: &str, replacements: &[ProgramReplacement]) -> String {
    let mut output = String::with_capacity(source.len() + replacements.len() * 32);
    let mut cursor = 0usize;
    let mut sorted = replacements.iter().collect::<Vec<_>>();
    sorted.sort_by_key(|replacement| (replacement.span.start, replacement.span.end));
    for replacement in sorted {
        let start = replacement.span.start as usize;
        let end = replacement.span.end as usize;
        if start < cursor {
            continue;
        }
        output.push_str(&source[cursor..start]);
        output.push_str(&replacement.text);
        cursor = end;
    }
    output.push_str(&source[cursor..]);
    if !output.ends_with('\n') {
        output.push('\n');
    }
    output
}

fn apply_program_replacements_in_span(
    source: &str,
    span: Span,
    replacements: &[ProgramReplacement],
) -> String {
    let mut output = String::new();
    let mut cursor = span.start as usize;
    let end = span.end as usize;
    let mut sorted = replacements.iter().collect::<Vec<_>>();
    sorted.sort_by_key(|replacement| (replacement.span.start, replacement.span.end));
    for replacement in sorted {
        let start = replacement.span.start as usize;
        let replacement_end = replacement.span.end as usize;
        if start < cursor || replacement_end > end {
            continue;
        }
        output.push_str(&source[cursor..start]);
        output.push_str(&replacement.text);
        cursor = replacement_end;
    }
    output.push_str(&source[cursor..end]);
    output
}

pub(super) fn transform_dynamic_web_entry(path: &Path, source: &str) -> Result<String, Diagnostic> {
    let source_type = source_type_for_path(path, ParseContext::Entry)?;
    let allocator = Allocator::default();
    let parsed = Parser::new(&allocator, source, source_type).parse();
    if let Some(error) = parsed.errors.into_iter().next() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_PARSE",
            format!("failed to parse dynamic web input: {error}"),
        )
        .with_path(path));
    }
    let mut program = parsed.program;
    let transformed = transpile_program_typescript(path, &allocator, &mut program)?;
    rewrite_dynamic_web_entry(path, &transformed)
}

fn rewrite_dynamic_web_entry(path: &Path, source: &str) -> Result<String, Diagnostic> {
    let allocator = Allocator::default();
    let parsed = Parser::new(&allocator, source, SourceType::mjs()).parse();
    if let Some(error) = parsed.errors.into_iter().next() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_TRANSFORM",
            format!("failed to parse transformed dynamic web JavaScript: {error}"),
        )
        .with_path(path));
    }

    let mut replacements = Vec::<ProgramReplacement>::new();
    for statement in &parsed.program.body {
        match statement {
            Statement::ImportDeclaration(import) => {
                replacements.push(ProgramReplacement {
                    span: import.span,
                    text: String::new(),
                });
            }
            Statement::ExportDefaultDeclaration(export) => {
                let value = source_slice(source, export.declaration.span()).unwrap_or_default();
                replacements.push(ProgramReplacement {
                    span: export.span,
                    text: format!("globalThis.__sloppy_dynamic_default_app = {value};"),
                });
            }
            _ => {}
        }
    }

    Ok(apply_program_replacements(source, &replacements))
}

fn program_import_replacement(
    path: &Path,
    import: &ImportDeclaration<'_>,
    graph: &ModuleGraph,
    package: Option<&str>,
) -> Result<String, Diagnostic> {
    if import.import_kind == ImportOrExportKind::Type {
        return Ok(String::new());
    }
    let specifier = import.source.value.as_str();
    let require_expr = match resolver::classify_import(path, specifier) {
        resolver::ImportKind::Relative(resolved) => {
            format!(
                "__sloppy_program_require({})",
                json_string(&program_module_id(graph, &resolved, package))
            )
        }
        resolver::ImportKind::SlopStdlib
        | resolver::ImportKind::SlopData
        | resolver::ImportKind::SlopTime
        | resolver::ImportKind::SlopFilesystem
        | resolver::ImportKind::SlopCrypto
        | resolver::ImportKind::SlopCodec
        | resolver::ImportKind::SlopCache
        | resolver::ImportKind::SlopNet
        | resolver::ImportKind::SlopHttp
        | resolver::ImportKind::SlopWebhooks
        | resolver::ImportKind::SlopRedis
        | resolver::ImportKind::SlopOs
        | resolver::ImportKind::SlopOrm
        | resolver::ImportKind::SlopWorkers
        | resolver::ImportKind::SlopFfi => "globalThis.__sloppy_runtime".to_string(),
        resolver::ImportKind::SqliteProvider => {
            "__sloppy_program_require(\"sloppy/providers/sqlite\")".to_string()
        }
        resolver::ImportKind::NodeBuiltin(builtin) if builtin.status != "unsupported" => {
            let backing = builtin.backing.unwrap_or("sloppy/node/unsupported");
            format!("__sloppy_program_require({})", json_string(backing))
        }
        resolver::ImportKind::Package(package) => {
            let entry_id = program_module_id(graph, &package.entry, Some(&package.name));
            format!("__sloppy_program_require({})", json_string(&entry_id))
        }
        resolver::ImportKind::NativeAddonUnsupported(package) => {
            return Err(Diagnostic::new(
                "SLOPPYC_E_NATIVE_ADDON_UNSUPPORTED",
                format!(
                    "Package \"{}\" requires a native Node addon. Sloppy does not support Node native addons yet.",
                    package.name
                ),
            )
            .with_path(path)
            .with_span(import.source.span)
            .with_hint("Use a pure-JavaScript package entry or remove the native addon dependency."));
        }
        resolver::ImportKind::PackageExportUnsupported(failure) => {
            return Err(package_export_unsupported_diagnostic(
                path,
                import.source.span,
                &failure,
            ));
        }
        resolver::ImportKind::UnsupportedBare(specifier) => {
            return Err(Diagnostic::new(
                "SLOPPYC_E_PACKAGE_NOT_FOUND",
                format!(
                    "Package \"{specifier}\" was not found from {}.",
                    source_map_source_name(path)
                ),
            )
            .with_path(path)
            .with_span(import.source.span)
            .with_hint(format!(
                "Install it with your package manager, for example:\n  npm install {specifier}"
            )));
        }
        resolver::ImportKind::NodeBuiltin(builtin) => {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_NODE_BUILTIN",
                format!(
                    "{} is not supported by Sloppy's Node compatibility registry yet.",
                    builtin.specifier
                ),
            )
            .with_path(path)
            .with_span(import.source.span)
            .with_hint(
                "Use a Sloppy stdlib API or a package path that avoids this Node builtin.",
            ));
        }
        _ => {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_IMPORT",
                format!("Program Mode cannot resolve import \"{specifier}\""),
            )
            .with_path(path)
            .with_span(import.source.span)
            .with_hint(
                "Use Sloppy stdlib imports, relative modules, or installed compatible packages.",
            ));
        }
    };
    let Some(specifiers) = &import.specifiers else {
        return Ok(format!("{require_expr};"));
    };
    if specifiers.is_empty() {
        return Ok(format!("{require_expr};"));
    }
    let mut defaults = Vec::new();
    let mut namespaces = Vec::new();
    let mut named = Vec::new();
    for specifier in specifiers {
        match specifier {
            ImportDeclarationSpecifier::ImportDefaultSpecifier(specifier) => {
                defaults.push(specifier.local.name.as_str().to_string());
            }
            ImportDeclarationSpecifier::ImportNamespaceSpecifier(specifier) => {
                namespaces.push(specifier.local.name.as_str().to_string());
            }
            ImportDeclarationSpecifier::ImportSpecifier(specifier) => {
                if specifier.import_kind == ImportOrExportKind::Type {
                    continue;
                }
                let imported = module_export_name_text(&specifier.imported);
                let local = specifier.local.name.as_str();
                if imported == local {
                    named.push(imported);
                } else {
                    named.push(format!("{imported}: {local}"));
                }
            }
        }
    }
    let mut output = String::new();
    if !named.is_empty() {
        output.push_str("const { ");
        output.push_str(&named.join(", "));
        output.push_str(" } = ");
        output.push_str(&require_expr);
        output.push(';');
    }
    for default in defaults {
        if !output.is_empty() {
            output.push(' ');
        }
        output.push_str("const ");
        output.push_str(&default);
        output.push_str(" = ");
        output.push_str("(()=>{const __sloppy_default_module=");
        output.push_str(&require_expr);
        output.push_str(";return __sloppy_default_module&&__sloppy_default_module.__esModule===true&&Object.prototype.hasOwnProperty.call(__sloppy_default_module,\"default\")?__sloppy_default_module.default:__sloppy_default_module;})();");
    }
    for namespace in namespaces {
        if !output.is_empty() {
            output.push(' ');
        }
        output.push_str("const ");
        output.push_str(&namespace);
        output.push_str(" = ");
        output.push_str(&require_expr);
        output.push(';');
    }
    Ok(output)
}

fn program_named_export_replacement(
    path: &Path,
    source: &str,
    export: &ExportNamedDeclaration<'_>,
    graph: &ModuleGraph,
    package: Option<&str>,
) -> Result<String, Diagnostic> {
    if export.export_kind == ImportOrExportKind::Type {
        return Ok(String::new());
    }
    if let Some(export_source) = &export.source {
        let has_runtime_reexport = export
            .specifiers
            .iter()
            .any(|specifier| specifier.export_kind != ImportOrExportKind::Type);
        if !has_runtime_reexport {
            return Ok(String::new());
        }
        let require_expr = program_reexport_require_expr(
            path,
            export_source.value.as_str(),
            export_source.span,
            graph,
            package,
        )?;
        let module_var = format!("__sloppy_reexport_{}", export.span.start);
        let mut output = format!("const {module_var} = {require_expr};\n");
        for specifier in &export.specifiers {
            if specifier.export_kind == ImportOrExportKind::Type {
                continue;
            }
            let local = module_export_name_text(&specifier.local);
            let exported = module_export_name_text(&specifier.exported);
            output.push_str(&export_assignment(
                &exported,
                &member_access_expr(&module_var, &local),
            ));
            output.push('\n');
        }
        return Ok(output);
    }
    let mut output = String::new();
    if let Some(declaration) = &export.declaration {
        let mut dynamic_replacements = Vec::new();
        collect_dynamic_import_declaration_replacements(
            path,
            source,
            declaration,
            graph,
            package,
            &mut dynamic_replacements,
        );
        output.push_str(&apply_program_replacements_in_span(
            source,
            declaration.span(),
            &dynamic_replacements,
        ));
        for name in declaration_export_names(declaration) {
            output.push('\n');
            output.push_str(&export_assignment(&name, &name));
        }
        return Ok(output);
    }
    for specifier in &export.specifiers {
        if specifier.export_kind == ImportOrExportKind::Type {
            continue;
        }
        let local = module_export_name_text(&specifier.local);
        let exported = module_export_name_text(&specifier.exported);
        if !identifier_like(&local) {
            return Err(Diagnostic::new(
                "SLOPPYC_E_UNSUPPORTED_EXPORT",
                "program mode named exports must reference local identifiers",
            )
            .with_path(path)
            .with_span(specifier.span));
        }
        output.push_str(&export_assignment(&exported, &local));
        output.push('\n');
    }
    Ok(output)
}

fn program_export_all_replacement(
    path: &Path,
    export: &ExportAllDeclaration<'_>,
    graph: &ModuleGraph,
    package: Option<&str>,
) -> Result<String, Diagnostic> {
    if export.export_kind == ImportOrExportKind::Type {
        return Ok(String::new());
    }
    let require_expr = program_reexport_require_expr(
        path,
        export.source.value.as_str(),
        export.source.span,
        graph,
        package,
    )?;
    let module_var = format!("__sloppy_reexport_{}", export.span.start);
    if let Some(exported) = &export.exported {
        return Ok(export_assignment(
            &module_export_name_text(exported),
            &require_expr,
        ));
    }
    Ok(format!(
        "const {module_var} = {require_expr};\nfor (const __sloppy_key of Object.keys({module_var})) {{ if (__sloppy_key !== \"default\" && __sloppy_key !== \"__esModule\" && !Object.prototype.hasOwnProperty.call(exports, __sloppy_key)) {{ exports[__sloppy_key] = {module_var}[__sloppy_key]; }} }}"
    ))
}

fn program_reexport_require_expr(
    path: &Path,
    specifier: &str,
    span: Span,
    graph: &ModuleGraph,
    package: Option<&str>,
) -> Result<String, Diagnostic> {
    match resolver::classify_import(path, specifier) {
        resolver::ImportKind::Relative(resolved) => Ok(format!(
            "__sloppy_program_require({})",
            json_string(&program_module_id(graph, &resolved, package))
        )),
        resolver::ImportKind::SlopStdlib
        | resolver::ImportKind::SlopData
        | resolver::ImportKind::SlopTime
        | resolver::ImportKind::SlopFilesystem
        | resolver::ImportKind::SlopCrypto
        | resolver::ImportKind::SlopCodec
        | resolver::ImportKind::SlopCache
        | resolver::ImportKind::SlopNet
        | resolver::ImportKind::SlopHttp
        | resolver::ImportKind::SlopWebhooks
        | resolver::ImportKind::SlopRedis
        | resolver::ImportKind::SlopOs
        | resolver::ImportKind::SlopOrm
        | resolver::ImportKind::SlopWorkers
        | resolver::ImportKind::SlopFfi => Ok("globalThis.__sloppy_runtime".to_string()),
        resolver::ImportKind::SqliteProvider => {
            Ok("__sloppy_program_require(\"sloppy/providers/sqlite\")".to_string())
        }
        resolver::ImportKind::NodeBuiltin(builtin) if builtin.status != "unsupported" => {
            let backing = builtin.backing.unwrap_or("sloppy/node/unsupported");
            Ok(format!("__sloppy_program_require({})", json_string(backing)))
        }
        resolver::ImportKind::Package(package) => {
            let entry_id = program_module_id(graph, &package.entry, Some(&package.name));
            Ok(format!("__sloppy_program_require({})", json_string(&entry_id)))
        }
        resolver::ImportKind::NativeAddonUnsupported(package) => Err(Diagnostic::new(
            "SLOPPYC_E_NATIVE_ADDON_UNSUPPORTED",
            format!(
                "Package \"{}\" requires a native Node addon. Sloppy does not support Node native addons yet.",
                package.name
            ),
        )
        .with_path(path)
        .with_span(span)
        .with_hint("Use a pure-JavaScript package entry or remove the native addon dependency.")),
        resolver::ImportKind::PackageExportUnsupported(failure) => {
            Err(package_export_unsupported_diagnostic(path, span, &failure))
        }
        resolver::ImportKind::UnsupportedBare(specifier) => Err(Diagnostic::new(
            "SLOPPYC_E_PACKAGE_NOT_FOUND",
            format!(
                "Package \"{specifier}\" was not found from {}.",
                source_map_source_name(path)
            ),
        )
        .with_path(path)
        .with_span(span)
        .with_hint(format!(
            "Install it with your package manager, for example:\n  npm install {specifier}"
        ))),
        resolver::ImportKind::UnresolvedRelative(specifier) => Err(Diagnostic::new(
            "SLOPPYC_E_MISSING_RELATIVE_IMPORT",
            format!("relative import \"{specifier}\" could not be resolved"),
        )
        .with_path(path)
        .with_span(span)
        .with_hint("Use a relative .js/.mjs/.cjs/.ts/.tsx/.json module inside the source root.")),
        resolver::ImportKind::Remote(specifier) => Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_IMPORT",
            format!("remote import \"{specifier}\" is outside the sealed Sloppy artifact graph"),
        )
        .with_path(path)
        .with_span(span)
        .with_hint("Use installed packages, Sloppy stdlib imports, or relative modules.")),
        resolver::ImportKind::NodeBuiltin(builtin) => Err(Diagnostic::new(
            "SLOPPYC_E_UNSUPPORTED_NODE_BUILTIN",
            format!(
                "{} is not supported by Sloppy's Node compatibility registry yet.",
                builtin.specifier
            ),
        )
        .with_path(path)
        .with_span(span)
        .with_hint("Use a Sloppy stdlib API or a package path that avoids this Node builtin.")),
    }
}

fn program_default_export_replacement(
    source: &str,
    export: &ExportDefaultDeclaration<'_>,
) -> String {
    let declaration = span_source(source, export.declaration.span()).trim_end_matches(';');
    format!("exports.default = {declaration};")
}

fn declaration_export_names(declaration: &Declaration<'_>) -> Vec<String> {
    match declaration {
        Declaration::FunctionDeclaration(function) => function
            .id
            .as_ref()
            .map(|id| vec![id.name.as_str().to_string()])
            .unwrap_or_default(),
        Declaration::ClassDeclaration(class) => class
            .id
            .as_ref()
            .map(|id| vec![id.name.as_str().to_string()])
            .unwrap_or_default(),
        Declaration::VariableDeclaration(declaration) => variable_declaration_names(declaration),
        _ => Vec::new(),
    }
}

fn variable_declaration_names(declaration: &VariableDeclaration<'_>) -> Vec<String> {
    let mut names = Vec::new();
    for declarator in &declaration.declarations {
        binding_pattern_names(&declarator.id, &mut names);
    }
    names
}

fn binding_pattern_names(pattern: &BindingPattern<'_>, names: &mut Vec<String>) {
    match pattern {
        BindingPattern::BindingIdentifier(identifier) => {
            names.push(identifier.name.as_str().to_string());
        }
        BindingPattern::ObjectPattern(pattern) => {
            for property in &pattern.properties {
                binding_pattern_names(&property.value, names);
            }
            if let Some(rest) = &pattern.rest {
                binding_pattern_names(&rest.argument, names);
            }
        }
        BindingPattern::ArrayPattern(pattern) => {
            for element in pattern.elements.iter().flatten() {
                binding_pattern_names(element, names);
            }
            if let Some(rest) = &pattern.rest {
                binding_pattern_names(&rest.argument, names);
            }
        }
        BindingPattern::AssignmentPattern(pattern) => {
            binding_pattern_names(&pattern.left, names);
        }
    }
}

fn module_export_name_text(name: &ModuleExportName<'_>) -> String {
    name.name().as_str().to_string()
}

fn export_assignment(exported: &str, local: &str) -> String {
    if identifier_like(exported) {
        format!("exports.{exported} = {local};")
    } else {
        format!("exports[{}] = {local};", json_string(exported))
    }
}

fn member_access_expr(object: &str, property: &str) -> String {
    if identifier_like(property) {
        format!("{object}.{property}")
    } else {
        format!("{object}[{}]", json_string(property))
    }
}

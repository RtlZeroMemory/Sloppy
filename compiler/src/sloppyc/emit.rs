// Artifact writing, generated JavaScript emission, and source-map emission.
use super::*;

pub(super) fn write_artifacts(
    out_dir: &Path,
    app: &ExtractedApp,
    mut metrics: Option<&mut CompileMetrics>,
) -> Result<(), Diagnostic> {
    validate_output_dir(out_dir)?;
    fs::create_dir_all(out_dir).map_err(|error| {
        Diagnostic::new(
            "SLOPPYC_E_OUTPUT",
            format!("failed to create output directory: {error}"),
        )
        .with_path(out_dir)
    })?;

    let bundle_start = Instant::now();
    let app_js = emit_app_js(app);
    if let Some(metrics) = metrics.as_mut() {
        metrics.add_phase("bundleEmitMs", bundle_start.elapsed());
        metrics.set_artifact_bytes("appJsBytes", app_js.source.len());
    }
    let source_map_start = Instant::now();
    let source_map = emit_source_map(app, &app_js);
    if let Some(metrics) = metrics.as_mut() {
        metrics.add_phase("sourceMapMs", source_map_start.elapsed());
        metrics.set_artifact_bytes("sourceMapBytes", source_map.len());
    }
    let route_artifact_start = Instant::now();
    let route_artifact = emit_route_artifact(app)?;
    let route_artifact_metadata =
        route_artifact.as_ref().map(
            |bytes| crate::route_artifact::RouteDispatchArtifactMetadata {
                path: ROUTE_ARTIFACT_PATH.to_string(),
                hash: sha256_bytes_hex(bytes),
            },
        );
    if let Some(metrics) = metrics.as_mut() {
        metrics.add_phase("routeArtifactEmitMs", route_artifact_start.elapsed());
        if let Some(route_artifact) = &route_artifact {
            metrics.set_artifact_bytes("routeArtifactBytes", route_artifact.len());
        }
    }
    let plan_start = Instant::now();
    let plan = emit_plan_with_route_artifact(
        app,
        &sha256_hex(&app_js.source),
        &sha256_hex(&source_map),
        route_artifact_metadata.as_ref(),
    )?;
    if let Some(metrics) = metrics.as_mut() {
        metrics.add_phase("planEmitMs", plan_start.elapsed());
        metrics.set_artifact_bytes("planBytes", plan.len());
    }
    let dependency_graph = if app.dependency_graph.has_entries() {
        let dependency_graph = emit_dependency_graph(&app.dependency_graph)?;
        if let Some(metrics) = metrics.as_mut() {
            metrics.set_artifact_bytes("dependencyGraphBytes", dependency_graph.len());
        }
        Some(dependency_graph)
    } else {
        None
    };

    let write_start = Instant::now();
    write_artifact(out_dir, "app.js", &app_js.source)?;
    write_artifact(out_dir, "app.js.map", &source_map)?;
    if let Some(route_artifact) = &route_artifact {
        write_binary_artifact(out_dir, ROUTE_ARTIFACT_PATH, route_artifact)?;
    }
    write_artifact(out_dir, "app.plan.json", &plan)?;
    if let Some(dependency_graph) = &dependency_graph {
        write_artifact(out_dir, "deps.graph.json", dependency_graph)?;
    }
    if let Some(metrics) = metrics.as_mut() {
        metrics.add_phase("writeMs", write_start.elapsed());
    }
    Ok(())
}

fn write_artifact(out_dir: &Path, name: &str, contents: &str) -> Result<(), Diagnostic> {
    let temp_name = format!("{name}.tmp");
    let temp_path = out_dir.join(&temp_name);
    let final_path = out_dir.join(name);
    fs::write(&temp_path, contents).map_err(|error| {
        Diagnostic::new(
            "SLOPPYC_E_OUTPUT",
            format!("failed to write {temp_name}: {error}"),
        )
        .with_path(out_dir)
    })?;
    if final_path.exists() {
        fs::remove_file(&final_path).map_err(|error| {
            Diagnostic::new(
                "SLOPPYC_E_OUTPUT",
                format!("failed to replace {name}: {error}"),
            )
            .with_path(out_dir)
        })?;
    }
    fs::rename(&temp_path, &final_path).map_err(|error| {
        Diagnostic::new(
            "SLOPPYC_E_OUTPUT",
            format!("failed to finalize {name}: {error}"),
        )
        .with_path(out_dir)
    })
}

fn write_binary_artifact(out_dir: &Path, name: &str, contents: &[u8]) -> Result<(), Diagnostic> {
    let temp_name = format!("{name}.tmp");
    let temp_path = out_dir.join(&temp_name);
    let final_path = out_dir.join(name);
    fs::write(&temp_path, contents).map_err(|error| {
        Diagnostic::new(
            "SLOPPYC_E_OUTPUT",
            format!("failed to write {temp_name}: {error}"),
        )
        .with_path(out_dir)
    })?;
    if final_path.exists() {
        fs::remove_file(&final_path).map_err(|error| {
            Diagnostic::new(
                "SLOPPYC_E_OUTPUT",
                format!("failed to replace {name}: {error}"),
            )
            .with_path(out_dir)
        })?;
    }
    fs::rename(&temp_path, &final_path).map_err(|error| {
        Diagnostic::new(
            "SLOPPYC_E_OUTPUT",
            format!("failed to finalize {name}: {error}"),
        )
        .with_path(out_dir)
    })
}

fn validate_output_dir(out_dir: &Path) -> Result<(), Diagnostic> {
    if out_dir.as_os_str().is_empty() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_INVALID_OUTPUT",
            "output directory must not be empty",
        ));
    }

    for component in out_dir.components() {
        if matches!(component, std::path::Component::ParentDir) {
            return Err(Diagnostic::new(
                "SLOPPYC_E_INVALID_OUTPUT",
                "output directory must not contain '..'",
            )
            .with_path(out_dir));
        }
    }
    Ok(())
}

fn source_location_json(source_name: &str, source: &str, span: Span) -> Value {
    let (line, column) = line_column(source, span.start);
    json!({
        "path": source_name,
        "line": line,
        "column": column
    })
}

fn config_source_env_name(source: &str) -> Option<String> {
    if let Some(env) = source.strip_prefix("env:") {
        return (!env.is_empty()).then(|| env.to_string());
    }
    let start = source.find("${")?;
    let rest = &source[start + 2..];
    let end = rest.find('}')?;
    let env = &rest[..end];
    (!env.is_empty()).then(|| env.to_string())
}

fn framework_provider_connection_string_env(app: &ExtractedApp, key: &str) -> String {
    app.configuration
        .as_ref()
        .and_then(|configuration| {
            configuration
                .requirements
                .iter()
                .find(|requirement| {
                    normalize_config_key(&requirement.key) == normalize_config_key(key)
                })
                .and_then(|requirement| requirement.source.as_deref())
                .and_then(config_source_env_name)
        })
        .unwrap_or_else(|| config_key_to_env_name(key))
}

fn framework_provider_config_entries(app: &ExtractedApp) -> String {
    let entries = app
        .capabilities
        .iter()
        .filter(|capability| capability.capability_kind == "database")
        .map(|capability| {
            let connection_string_key = capability.config_key.clone().or_else(|| {
                if matches!(capability.provider.as_str(), "postgres" | "sqlserver") {
                    Some(format!(
                        "Sloppy:Providers:{}:{}:connectionString",
                        capability.provider,
                        provider_config_name(capability)
                    ))
                } else {
                    None
                }
            });
            let connection_string_env = connection_string_key
                .as_deref()
                .map(|key| framework_provider_connection_string_env(app, key));
            let value = json!({
                "providerKind": capability.provider,
                "access": capability.access,
                "connectionStringKey": connection_string_key,
                "connectionStringEnv": connection_string_env
            });
            format!(
                "[{}, {}]",
                serde_json::to_string(&capability.token).unwrap_or_else(|_| "\"\"".to_string()),
                serde_json::to_string(&value).unwrap_or_else(|_| "{}".to_string())
            )
        })
        .collect::<Vec<_>>()
        .join(", ");
    format!("[{entries}]")
}

fn framework_config_default_entries(app: &ExtractedApp) -> String {
    let mut entries = Vec::new();
    let mut seen = BTreeSet::new();
    for read in &app.config_reads {
        let Some(default_value) = &read.default_value else {
            continue;
        };
        if !seen.insert(normalize_config_key(&read.key)) {
            continue;
        }
        entries.push(format!(
            "[{}, {}]",
            serde_json::to_string(&read.key).unwrap_or_else(|_| "\"\"".to_string()),
            serde_json::to_string(default_value).unwrap_or_else(|_| "null".to_string())
        ));
    }
    format!("[{}]", entries.join(", "))
}

fn framework_queue_service_entries(app: &ExtractedApp) -> Vec<(String, String)> {
    let explicit_services = app
        .service_registrations
        .iter()
        .map(|registration| registration.token.clone())
        .collect::<BTreeSet<_>>();
    app.capabilities
        .iter()
        .filter(|capability| capability.capability_kind == "queue")
        .filter(|capability| !explicit_services.contains(&capability.token))
        .map(|capability| {
            let name = capability.config_name.clone().unwrap_or_else(|| {
                capability
                    .token
                    .strip_prefix("queue.")
                    .unwrap_or(&capability.token)
                    .to_string()
            });
            (capability.token.clone(), name)
        })
        .collect()
}

pub(super) fn emit_app_js(app: &ExtractedApp) -> EmittedAppJs {
    if app.kind == ProjectKind::Program {
        return emit_program_app_js(app);
    }
    if let Some(source) = &app.dynamic_entry_source {
        return emit_dynamic_web_app_js(source, app);
    }
    let mut output = String::with_capacity(estimate_app_js_capacity(app));
    let mut mappings = Vec::with_capacity(app.routes.len());
    let mut handler_generated_starts = Vec::with_capacity(app.routes.len());
    let mut generated_line = 0usize;
    let needs_provider_open_helper = app.routes.iter().any(|route| {
        route
            .handler
            .emitted_source
            .contains("__sloppy_open_data_provider")
    });
    let needs_framework_arg_helper = app.routes.iter().any(|route| {
        route
            .handler
            .emitted_source
            .contains("__sloppy_framework_arg")
    });
    let needs_framework_config_bindings = app.routes.iter().any(|route| {
        route
            .handler
            .bindings
            .iter()
            .any(|binding| binding.kind == "config")
    });
    let queue_service_entries = framework_queue_service_entries(app);
    let needs_output_cache_runtime = app.routes.iter().any(|route| route.output_cache.is_some());
    let needs_framework_services = needs_framework_arg_helper
        || needs_output_cache_runtime
        || !app.service_registrations.is_empty()
        || !queue_service_entries.is_empty()
        || app.routes.iter().any(|route| {
            route
                .handler
                .emitted_source
                .contains("__sloppy_framework_services")
        });
    let needs_framework_pipeline = app.routes.iter().any(|route| {
        route
            .handler
            .emitted_source
            .contains("__sloppy_run_middleware")
    });
    let needs_request_id_helper = app
        .routes
        .iter()
        .any(|route| route.handler.emitted_source.contains("__sloppy_request_id"));
    let needs_request_logging_helper = app.routes.iter().any(|route| {
        route
            .handler
            .emitted_source
            .contains("__sloppy_request_logging")
    });
    let needs_cors_helper = app.routes.iter().any(|route| {
        route
            .handler
            .emitted_source
            .contains("__sloppy_finish_cors")
            || route
                .handler
                .emitted_source
                .contains("__sloppy_cors_preflight")
    });
    let needs_static_asset_helper = app.routes.iter().any(|route| {
        route
            .handler
            .emitted_source
            .contains("__sloppyStaticAssetResponse")
    });
    let needs_auth_helper = app.routes.iter().any(|route| {
        route
            .handler
            .emitted_source
            .contains("__sloppy_require_auth")
    }) || !app.auth.schemes.is_empty();
    let needs_framework_environment = app.routes.iter().any(|route| {
        route.handler.bindings.iter().any(|binding| {
            binding.kind == "config"
                || matches!(
                    binding.provider_kind.as_deref(),
                    Some("postgres") | Some("sqlserver")
                )
        })
    });

    push_generated_line(
        &mut output,
        &mut generated_line,
        "const __sloppyRuntime = globalThis.__sloppy_runtime;",
    );
    push_generated_line(
        &mut output,
        &mut generated_line,
        "if (__sloppyRuntime === undefined) {",
    );
    push_generated_line(
        &mut output,
        &mut generated_line,
        "  throw new Error(\"Sloppy bootstrap runtime was not loaded\");",
    );
    push_generated_line(&mut output, &mut generated_line, "}");
    let mut runtime_exports = vec!["Results"];
    if app.uses_realtime_runtime {
        runtime_exports.push("Realtime");
        runtime_exports.push("SloppyRealtimeError");
        runtime_exports.push("schema");
        runtime_exports.push("Schema");
    }
    if app.problem_details.is_some() {
        runtime_exports.push("ProblemDetails");
    }
    if app.uses_data_runtime {
        runtime_exports.push("data");
    }
    if app.uses_migrations_runtime {
        runtime_exports.push("Migrations");
    }
    if app.uses_provider_health_runtime {
        runtime_exports.push("ProviderHealth");
    }
    if app.uses_sql_runtime {
        runtime_exports.push("sql");
    }
    if app.uses_orm_runtime {
        runtime_exports.extend([
            "orm",
            "table",
            "column",
            "relation",
            "SloppyOrmError",
            "SloppyOrmConcurrencyError",
        ]);
    }
    if app.uses_time_runtime {
        runtime_exports.extend([
            "Time",
            "Deadline",
            "CancellationController",
            "TimeoutError",
            "CancelledError",
            "InvalidDeadlineError",
            "TimerDisposedError",
        ]);
    }
    if app.uses_fs_runtime {
        runtime_exports.extend(["File", "Directory", "Path", "FileHandle", "FileWatcher"]);
    }
    if app.uses_crypto_runtime {
        runtime_exports.extend([
            "Random",
            "Hash",
            "Hmac",
            "Password",
            "ConstantTime",
            "Secret",
            "NonCryptoHash",
        ]);
    }
    if app.uses_codec_runtime {
        runtime_exports.extend(CODEC_EXPORTS.iter().copied());
    }
    if needs_output_cache_runtime && !app.uses_codec_runtime {
        runtime_exports.push("Text");
    }
    if app.uses_cache_runtime || needs_output_cache_runtime {
        runtime_exports.extend(["Cache", "SloppyCacheError"]);
    }
    if app.uses_redis_runtime {
        runtime_exports.extend(["Redis", "SloppyRedisError"]);
    }
    if app.uses_net_runtime {
        runtime_exports.extend([
            "TcpClient",
            "TcpListener",
            "TcpConnection",
            "LocalEndpoint",
            "UnixSocket",
            "NamedPipe",
            "NetworkAddress",
            "SloppyNetError",
        ]);
    }
    if app.uses_os_runtime {
        runtime_exports.extend([
            "System",
            "Environment",
            "Process",
            "ProcessHandle",
            "Signals",
            "OsError",
        ]);
    } else if needs_framework_environment {
        runtime_exports.push("Environment");
    }
    if app.uses_http_client_runtime {
        runtime_exports.extend([
            "HttpClient",
            "Http",
            "HttpClientFactory",
            "HttpError",
            "SloppyHttpClientError",
            "TestHttp",
        ]);
    }
    if app.uses_workers_runtime {
        runtime_exports.extend(WORKER_EXPORTS.iter().copied());
    }
    if app.uses_webhooks_runtime {
        runtime_exports.extend(["Webhooks", "SloppyWebhookError"]);
    }
    if app.uses_ffi_runtime || !app.ffi.is_empty() || !app.ffi_structs.is_empty() {
        runtime_exports.extend(["unsafeFfi", "t"]);
    }
    if needs_framework_services {
        runtime_exports.push("__createFrameworkServiceProvider");
    }
    push_generated_line(
        &mut output,
        &mut generated_line,
        &format!(
            "const {{ {} }} = __sloppyRuntime;",
            runtime_exports.join(", ")
        ),
    );
    if needs_framework_services {
        push_generated_line(
            &mut output,
            &mut generated_line,
            "const __sloppy_framework_services = __createFrameworkServiceProvider();",
        );
        for (token, name) in &queue_service_entries {
            let token = serde_json::to_string(token).unwrap_or_else(|_| "\"\"".to_string());
            let name = serde_json::to_string(name).unwrap_or_else(|_| "\"\"".to_string());
            push_generated_line(
                &mut output,
                &mut generated_line,
                &format!("__sloppy_framework_services.addSingleton({token}, () => WorkQueue.create({name}));"),
            );
        }
        for registration in &app.service_registrations {
            let token =
                serde_json::to_string(&registration.token).unwrap_or_else(|_| "\"\"".to_string());
            let method = match registration.lifetime {
                "singleton" => "addSingleton",
                "scoped" => "addScoped",
                "transient" => "addTransient",
                _ => "addTransient",
            };
            push_generated_line(
                &mut output,
                &mut generated_line,
                &format!(
                    "__sloppy_framework_services.{method}({token}, {});",
                    registration.factory_source
                ),
            );
        }
    }
    if needs_framework_config_bindings {
        let provider_configs = framework_provider_config_entries(app);
        let config_defaults = framework_config_default_entries(app);
        push_generated_line(
            &mut output,
            &mut generated_line,
            &format!("const __sloppy_framework_provider_configs = new Map({provider_configs});"),
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            &format!("const __sloppy_framework_config_defaults = new Map({config_defaults});"),
        );
    } else if needs_framework_arg_helper {
        let provider_configs = framework_provider_config_entries(app);
        push_generated_line(
            &mut output,
            &mut generated_line,
            &format!("const __sloppy_framework_provider_configs = new Map({provider_configs});"),
        );
    }
    if app.uses_data_runtime && needs_provider_open_helper {
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_open_data_provider(kind, token) {",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (kind === \"sqlite\") { return data.sqlite(token); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  throw new Error(`sloppy: ${kind} provider bridge unavailable`);",
        );
        push_generated_line(&mut output, &mut generated_line, "}");
    }
    if needs_framework_arg_helper {
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_framework_arg(ctx, scope, binding) {",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (binding.kind === \"body.json\") { return ctx.body.json(); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (binding.kind === \"body.form\") { return ctx.request.form(); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (binding.kind === \"body.multipart\") { return ctx.request.multipart(); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (binding.kind === \"context\") { return ctx; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (binding.kind === \"injection\") { return __sloppy_framework_injection(scope, binding); }",
        );
        if needs_framework_config_bindings {
            push_generated_line(
                &mut output,
                &mut generated_line,
                "  if (binding.kind === \"config\") { const value = Environment.get(binding.name); if (value !== undefined) { return value; } if (__sloppy_framework_config_defaults.has(binding.name)) { return __sloppy_framework_config_defaults.get(binding.name); } throw new Error(`sloppy: Config injection for '${binding.name}' requires an environment value.`); }",
            );
        } else {
            push_generated_line(
                &mut output,
                &mut generated_line,
                "  if (binding.kind === \"config\") { const value = Environment.get(binding.name); if (value === undefined) { throw new Error(`sloppy: Config injection for '${binding.name}' requires an environment value.`); } return value; }",
            );
        }
        push_generated_line(&mut output, &mut generated_line, "  let value;");
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (binding.kind === \"route\") { value = ctx.route[binding.name]; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  else if (binding.kind === \"query\") { value = ctx.query[binding.name]; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  else if (binding.kind === \"header\") { value = ctx.header[__sloppy_framework_header_property(binding.name)]; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  else { throw new TypeError(`Sloppy Framework binding kind '${binding.kind}' is not supported.`); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  return __sloppy_framework_coerce(value, binding);",
        );
        push_generated_line(&mut output, &mut generated_line, "}");
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_framework_header_property(name) {",
        );
        push_generated_line(&mut output, &mut generated_line, "  let output = \"\";");
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  let uppercaseNext = false;",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  for (const ch of String(name)) {",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "    if (ch === \"-\") { uppercaseNext = output.length !== 0; continue; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "    output += uppercaseNext ? ch.toUpperCase() : ch;",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "    uppercaseNext = false;",
        );
        push_generated_line(&mut output, &mut generated_line, "  }");
        push_generated_line(&mut output, &mut generated_line, "  return output;");
        push_generated_line(&mut output, &mut generated_line, "}");
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_framework_coerce(value, binding) {",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (value === null || value === undefined) { return value; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  const type = String(binding.type || binding.schema || \"\");",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (type.includes(\"boolean\")) {",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "    const normalized = String(value).toLowerCase();",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "    if (normalized === \"true\") { return true; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "    if (normalized === \"false\") { return false; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "    throw new TypeError(`Sloppy Framework binding '${binding.parameter || binding.name}' expected a boolean value.`);",
        );
        push_generated_line(&mut output, &mut generated_line, "  }");
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (type.includes(\"number\") || type.includes(\"PositiveInt\") || type === \"int\") {",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "    const parsed = Number(value);",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "    if (!Number.isFinite(parsed)) { throw new TypeError(`Sloppy Framework binding '${binding.parameter || binding.name}' expected a numeric value.`); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "    if ((type.includes(\"PositiveInt\") || type === \"int\") && !Number.isInteger(parsed)) { throw new TypeError(`Sloppy Framework binding '${binding.parameter || binding.name}' expected an integer value.`); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "    if (type.includes(\"PositiveInt\") && parsed <= 0) { throw new TypeError(`Sloppy Framework binding '${binding.parameter || binding.name}' expected a positive integer value.`); }",
        );
        push_generated_line(&mut output, &mut generated_line, "    return parsed;");
        push_generated_line(&mut output, &mut generated_line, "  }");
        push_generated_line(&mut output, &mut generated_line, "  return value;");
        push_generated_line(&mut output, &mut generated_line, "}");
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_framework_injection(scope, binding) {",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  const dependencyName = binding.capability || (binding.name && binding.name.includes(\".\") ? binding.name : `data.${binding.name}`);",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (binding.injectionKind === \"service\") { return scope.get(binding.name); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (binding.injectionKind === \"queue\") { return scope.get(dependencyName); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (binding.injectionKind === \"provider\" && binding.providerKind === \"sqlite\" && typeof data.sqlite === \"function\") { return scope.track(data.sqlite(dependencyName)); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (binding.injectionKind === \"provider\" && binding.providerKind === \"postgres\" && data.postgres !== undefined && typeof data.postgres.open === \"function\") { return scope.track(data.postgres.open(__sloppy_framework_provider_open_options(binding, dependencyName))); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (binding.injectionKind === \"provider\" && binding.providerKind === \"sqlserver\" && data.sqlserver !== undefined && typeof data.sqlserver.open === \"function\") { return scope.track(data.sqlserver.open(__sloppy_framework_provider_open_options(binding, dependencyName))); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  throw new Error(`sloppy: ${binding.injectionKind} injection for '${binding.name}' is unavailable in this runtime lane.`);",
        );
        push_generated_line(&mut output, &mut generated_line, "}");
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_framework_provider_open_options(binding, token) {",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  const config = __sloppy_framework_provider_configs.get(token);",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (config === undefined) { throw new Error(`sloppy: provider '${token}' is not configured for Framework injection.`); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (config.providerKind !== binding.providerKind) { throw new Error(`sloppy: provider '${token}' is configured as ${config.providerKind}, not ${binding.providerKind}.`); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  const key = config.connectionStringKey;",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  const env = config.connectionStringEnv;",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (typeof key !== \"string\" || key.length === 0 || typeof env !== \"string\" || env.length === 0) { throw new Error(`sloppy: provider '${token}' does not declare a connection string config key for Framework injection.`); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  const connectionString = Environment.get(env);",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  if (typeof connectionString !== \"string\" || connectionString.length === 0) { throw new Error(`sloppy: provider '${token}' requires config '${key}' from environment '${env}'.`); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "  return { connectionString, capability: token, access: config.access === \"read\" ? \"read\" : \"readwrite\" };",
        );
        push_generated_line(&mut output, &mut generated_line, "}");
    }
    if needs_framework_pipeline
        || needs_request_id_helper
        || needs_request_logging_helper
        || needs_cors_helper
        || needs_auth_helper
    {
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_is_plain_object(value) { return value !== null && typeof value === \"object\" && !Array.isArray(value); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_request_header(ctx, name) { const headers = ctx?.request?.headers; if (headers === undefined || headers === null) { return undefined; } const lower = String(name).toLowerCase(); if (typeof headers.get === \"function\") { const direct = headers.get(name) ?? headers.get(lower); if (direct !== undefined && direct !== null) { return direct; } if (typeof headers.entries === \"function\") { for (const [key, value] of headers.entries()) { if (String(key).toLowerCase() === lower) { return value; } } } return undefined; } if (__sloppy_is_plain_object(headers)) { for (const [key, value] of Object.entries(headers)) { if (key.toLowerCase() === lower) { return value; } } } return undefined; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_merge_headers(result, headers) { if (result === null || typeof result !== \"object\") { return result; } return Object.freeze({ ...result, headers: Object.freeze({ ...(__sloppy_is_plain_object(result.headers) ? result.headers : {}), ...headers }) }); }",
        );
    }
    if needs_framework_pipeline {
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_run_middleware(ctx, middleware, terminal) { let index = -1; function dispatch(nextIndex) { if (nextIndex <= index) { throw new Error(\"Sloppy middleware next() must not be called more than once.\"); } index = nextIndex; const current = middleware[nextIndex]; if (current === undefined) { return terminal(); } let nextCalled = false; let downstreamPromise; function next() { if (nextCalled) { throw new Error(\"Sloppy middleware next() must not be called more than once.\"); } nextCalled = true; const downstream = dispatch(nextIndex + 1); downstreamPromise = Promise.resolve(downstream); return downstream; } const value = current(ctx, next); if (!nextCalled) { return value; } return Promise.resolve(value).then((returned) => downstreamPromise.then(() => returned), (error) => downstreamPromise.then(() => { throw error; }, () => { throw error; })); } return dispatch(0); }",
        );
    }
    if needs_auth_helper {
        push_generated_line(
            &mut output,
            &mut generated_line,
            &format!(
                "const __sloppy_auth_schemes = {};",
                auth_schemes_json(&app.auth)
            ),
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_auth_policy(configure) { const requirements = []; const builder = { requireAuthenticated() { requirements.push([\"authenticated\"]); return builder; }, requireScope(...scopes) { requirements.push([\"scope\", scopes.flat()]); return builder; }, requireRole(...roles) { requirements.push([\"role\", roles.flat()]); return builder; }, requireClaim(name, value) { requirements.push([\"claim\", name, value]); return builder; }, custom(predicate) { requirements.push([\"custom\", predicate]); return builder; } }; configure(builder); return { async evaluate(user, ctx, resource) { for (const requirement of requirements) { if (requirement[0] === \"authenticated\" && user?.authenticated !== true) { return false; } if (requirement[0] === \"scope\" && !requirement[1].every((scope) => user?.hasScope(scope))) { return false; } if (requirement[0] === \"role\" && !requirement[1].some((role) => user?.hasRole(role))) { return false; } if (requirement[0] === \"claim\" && !user?.hasClaim(requirement[1], requirement[2])) { return false; } if (requirement[0] === \"custom\" && await requirement[1](user, ctx, resource) !== true) { return false; } } return true; } }; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_auth_policy_function(policy) { if (typeof policy === \"function\") { return policy; } if (policy && typeof policy.evaluate === \"function\") { return (user, ctx, resource) => policy.evaluate(user, ctx, resource); } return () => false; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            &format!(
                "const __sloppy_auth_policies = {};",
                auth_policies_js(&app.auth)
            ),
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "const __sloppy_auth_default_scheme = __sloppy_auth_schemes[0]?.name;",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "const __sloppy_auth_memory_sessions = new Map(); let __sloppy_auth_session_counter = 0;",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_auth_user(claims, scheme, authScheme = scheme) { const roles = Array.isArray(claims.roles) ? claims.roles.filter((role) => typeof role === \"string\") : (typeof claims.role === \"string\" ? [claims.role] : []); const scopes = []; if (typeof claims.scope === \"string\") { scopes.push(...claims.scope.split(/\\s+/).filter((scope) => scope.length !== 0)); } if (typeof claims.scp === \"string\") { scopes.push(...claims.scp.split(/\\s+/).filter((scope) => scope.length !== 0)); } if (Array.isArray(claims.scopes)) { scopes.push(...claims.scopes.filter((scope) => typeof scope === \"string\")); } if (Array.isArray(claims.scp)) { scopes.push(...claims.scp.filter((scope) => typeof scope === \"string\")); } const user = { authenticated: true, sub: typeof claims.sub === \"string\" ? claims.sub : \"\", name: typeof claims.name === \"string\" ? claims.name : \"\", roles: Object.freeze([...new Set(roles)]), scopes: Object.freeze([...new Set(scopes)]), claims: Object.freeze({ ...claims }), scheme, authScheme, hasRole(role) { return typeof role === \"string\" && user.roles.includes(role); }, hasScope(scope) { return typeof scope === \"string\" && user.scopes.includes(scope); }, hasClaim(name, value) { return typeof name === \"string\" && Object.prototype.hasOwnProperty.call(user.claims, name) && (value === undefined || Object.is(user.claims[name], value)); } }; return Object.freeze(user); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_auth_anonymous() { return Object.freeze({ authenticated: false, sub: \"\", name: \"\", roles: Object.freeze([]), scopes: Object.freeze([]), claims: Object.freeze({}), scheme: \"\", authScheme: \"\", hasRole() { return false; }, hasScope() { return false; }, hasClaim() { return false; } }); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_auth_attach_context(ctx) { ctx.user ??= __sloppy_auth_anonymous(); if (ctx.auth === undefined) { Object.defineProperties(ctx, { auth: { value: Object.freeze({ get user() { return ctx.user; } }), enumerable: true }, claims: { get() { return ctx.user?.claims ?? {}; }, enumerable: true }, requireUser: { value() { if (ctx.user?.authenticated !== true) { throw new Error(\"SLOPPY_E_AUTH_MISSING_CREDENTIALS: authenticated user is required.\"); } return ctx.user; }, enumerable: true }, hasScope: { value(scope) { return ctx.user?.hasScope(scope) === true; }, enumerable: true }, hasRole: { value(role) { return ctx.user?.hasRole(role) === true; }, enumerable: true }, hasClaim: { value(name, value) { return ctx.user?.hasClaim(name, value) === true; }, enumerable: true }, authorize: { async value(policy, resource) { const fn = __sloppy_auth_policies.get(policy); return typeof fn === \"function\" && (await fn(ctx.user, ctx, resource)) === true; }, enumerable: true } }); } return ctx; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_auth_problem(status, title, code) { return Results.problem(Object.freeze({ status, title, code }), Object.freeze({ status })); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_ct_bytes(left, right) { if (!(left instanceof Uint8Array) || !(right instanceof Uint8Array)) { return false; } if (left.byteLength === 0 || right.byteLength === 0) { return left.byteLength === right.byteLength; } let diff = left.byteLength ^ right.byteLength; const length = Math.max(left.byteLength, right.byteLength); for (let index = 0; index < length; index += 1) { diff |= left[index % left.byteLength] ^ right[index % right.byteLength]; } return diff === 0; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_ct_string(left, right) { left = String(left); right = String(right); if (left.length === 0 || right.length === 0) { return left.length === right.length; } let diff = left.length ^ right.length; const length = Math.max(left.length, right.length); for (let index = 0; index < length; index += 1) { diff |= left.charCodeAt(index % left.length) ^ right.charCodeAt(index % right.length); } return diff === 0; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_auth_json(part) { return JSON.parse(Text.utf8.decode(Base64Url.decode(part, { padding: \"optional\" }))); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_auth_config_value(configKey, envKey) { if (typeof configKey !== \"string\" || configKey.length === 0) { return undefined; } const key = typeof envKey === \"string\" && envKey.length !== 0 ? envKey : configKey.split(\":\").join(\"__\"); return Environment.get(key); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "async function __sloppy_auth_jwt(token, scheme) { const parts = String(token).split(\".\"); if (parts.length !== 3 || parts.some((part) => part.length === 0)) { return undefined; } const header = __sloppy_auth_json(parts[0]); if (header.alg === \"none\" || header.alg !== \"HS256\") { return undefined; } if (typeof scheme.secretConfigKey !== \"string\" || scheme.secretConfigKey.length === 0) { return undefined; } const secret = __sloppy_auth_config_value(scheme.secretConfigKey, scheme.secretEnvKey); if (typeof secret !== \"string\" || secret.length === 0) { throw new Error(`sloppy: auth secret config '${scheme.secretConfigKey}' is required.`); } const expected = await Hmac.sha256(Secret.fromUtf8(secret), `${parts[0]}.${parts[1]}`); const actual = Base64Url.decode(parts[2], { padding: \"optional\" }); if (!__sloppy_ct_bytes(expected, actual)) { return undefined; } const claims = __sloppy_auth_json(parts[1]); if (claims === null || typeof claims !== \"object\" || Array.isArray(claims)) { return undefined; } const now = Math.floor(Date.now() / 1000); const skew = Number.isInteger(scheme.clockSkewSeconds) ? scheme.clockSkewSeconds : 0; if (scheme.issuer && claims.iss !== scheme.issuer) { return undefined; } if (scheme.audience && (Array.isArray(claims.aud) ? !claims.aud.includes(scheme.audience) : claims.aud !== scheme.audience)) { return undefined; } if (claims.exp !== undefined && (!Number.isFinite(claims.exp) || claims.exp <= now - skew)) { return undefined; } if (claims.nbf !== undefined && (!Number.isFinite(claims.nbf) || claims.nbf > now + skew)) { return undefined; } if (claims.iat !== undefined && (!Number.isFinite(claims.iat) || claims.iat > now + skew)) { return undefined; } if (claims.sub !== undefined && typeof claims.sub !== \"string\") { return undefined; } return __sloppy_auth_user(claims, \"jwtBearer\", scheme.name); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_auth_cookie(ctx, name) { const cookies = ctx && ctx.cookies; if (cookies && typeof cookies.get === \"function\") { return cookies.get(name) ?? undefined; } const value = __sloppy_request_header(ctx, \"cookie\"); if (typeof value !== \"string\") { return undefined; } for (const pair of value.split(\";\")) { const equals = pair.indexOf(\"=\"); if (equals <= 0) { continue; } if (pair.slice(0, equals).trim() === name) { try { return decodeURIComponent(pair.slice(equals + 1).trim()); } catch { return undefined; } } } return undefined; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "async function __sloppy_auth_session(ctx, scheme) { const value = __sloppy_auth_cookie(ctx, scheme.cookie); if (typeof value !== \"string\") { return undefined; } const parts = value.split(\".\"); if (parts.length !== 2 || parts.some((part) => part.length === 0)) { return false; } if (typeof scheme.secretConfigKey !== \"string\" || scheme.secretConfigKey.length === 0) { return false; } const secret = __sloppy_auth_config_value(scheme.secretConfigKey, scheme.secretEnvKey); if (typeof secret !== \"string\" || secret.length === 0) { throw new Error(`sloppy: auth secret config '${scheme.secretConfigKey}' is required.`); } const expected = await Hmac.sha256(Secret.fromUtf8(secret), parts[0]); let actual; try { actual = Base64Url.decode(parts[1], { padding: \"optional\" }); } catch { return false; } if (!__sloppy_ct_bytes(expected, actual)) { return false; } const payload = __sloppy_auth_json(parts[0]); if (payload === null || typeof payload !== \"object\" || Array.isArray(payload)) { return false; } const nowMs = Date.now(); const now = Math.floor(nowMs / 1000); if (scheme.store === \"memory\") { if (typeof payload.sid !== \"string\" || payload.sid.length === 0) { return false; } const record = __sloppy_auth_memory_sessions.get(payload.sid); if (record === undefined || record.revokedAt !== undefined || (record.expiresAt !== undefined && record.expiresAt <= nowMs) || (record.idleExpiresAt !== undefined && record.idleExpiresAt <= nowMs)) { if (record !== undefined) { __sloppy_auth_memory_sessions.delete(payload.sid); } return false; } record.lastSeenAt = nowMs; if (Number.isInteger(scheme.idleTimeoutMs)) { record.idleExpiresAt = nowMs + scheme.idleTimeoutMs; } ctx.session = Object.freeze({ id: payload.sid, scheme: scheme.name, issuedAt: Math.floor(record.createdAt / 1000), expiresAt: Math.floor(record.expiresAt / 1000), revoke() { __sloppy_auth_memory_sessions.delete(payload.sid); } }); return __sloppy_auth_user(record.claims, \"cookieSession\", scheme.name); } if (payload.claims === null || typeof payload.claims !== \"object\" || Array.isArray(payload.claims)) { return false; } if (payload.exp !== undefined && (!Number.isFinite(payload.exp) || payload.exp <= now)) { return false; } ctx.session = Object.freeze({ scheme: scheme.name, issuedAt: payload.iat, expiresAt: payload.exp }); return __sloppy_auth_user(payload.claims, \"cookieSession\", scheme.name); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_auth_session_scheme() { const scheme = __sloppy_auth_schemes.find((entry) => entry.kind === \"cookieSession\"); if (scheme === undefined) { throw new TypeError(\"Sloppy Auth.signIn/signOut requires Auth.cookieSession middleware.\"); } return scheme; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "async function __sloppy_auth_sign_in(ctx, claims) { const scheme = __sloppy_auth_session_scheme(); if (claims === null || typeof claims !== \"object\" || Array.isArray(claims)) { throw new TypeError(\"Sloppy Auth.signIn claims must be a plain object.\"); } const copied = { ...claims }; if (copied.claims !== undefined) { if (copied.claims === null || typeof copied.claims !== \"object\" || Array.isArray(copied.claims)) { throw new TypeError(\"Sloppy Auth.signIn claims.claims must be a plain object when provided.\"); } Object.assign(copied, copied.claims); delete copied.claims; } if (typeof scheme.secretConfigKey !== \"string\" || scheme.secretConfigKey.length === 0) { throw new Error(\"sloppy: auth session secret config is required.\"); } const secret = __sloppy_auth_config_value(scheme.secretConfigKey, scheme.secretEnvKey); if (typeof secret !== \"string\" || secret.length === 0) { throw new Error(`sloppy: auth secret config '${scheme.secretConfigKey}' is required.`); } const nowMs = Date.now(); const current = Math.floor(nowMs / 1000); let payload; let maxAgeSeconds = scheme.maxAgeSeconds; if (scheme.store === \"memory\") { const sid = `session-${nowMs}-${++__sloppy_auth_session_counter}`; const absolute = nowMs + (Number.isInteger(scheme.absoluteTimeoutMs) ? scheme.absoluteTimeoutMs : 86400000); const idle = Number.isInteger(scheme.idleTimeoutMs) ? nowMs + scheme.idleTimeoutMs : undefined; __sloppy_auth_memory_sessions.set(sid, { id: sid, claims: copied, createdAt: nowMs, lastSeenAt: nowMs, expiresAt: absolute, idleExpiresAt: idle }); payload = { sid }; maxAgeSeconds = Math.ceil((absolute - nowMs) / 1000); ctx.session = Object.freeze({ id: sid, scheme: scheme.name, issuedAt: current, expiresAt: Math.floor(absolute / 1000), revoke() { __sloppy_auth_memory_sessions.delete(sid); } }); } else { payload = { iat: current, claims: copied }; if (Number.isInteger(scheme.maxAgeSeconds)) { payload.exp = current + scheme.maxAgeSeconds; } } const body = Base64Url.encode(Text.utf8.encode(JSON.stringify(payload))); const signature = await Hmac.sha256(Secret.fromUtf8(secret), body); const value = `${body}.${Base64Url.encode(signature)}`; ctx.user = __sloppy_auth_user(copied, \"cookieSession\", scheme.name); const options = { path: scheme.path ?? \"/\", secure: scheme.secure !== false, httpOnly: scheme.httpOnly !== false, sameSite: scheme.sameSite ?? \"lax\" }; if (Number.isInteger(maxAgeSeconds)) { options.maxAgeSeconds = maxAgeSeconds; } return Results.ok({ ok: true }).cookie(scheme.cookie, value, options); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_auth_sign_out(ctx) { const scheme = __sloppy_auth_session_scheme(); if (typeof ctx?.session?.id === \"string\") { __sloppy_auth_memory_sessions.delete(ctx.session.id); } ctx.user = __sloppy_auth_anonymous(); return Results.status(204).cookie(scheme.cookie, \"\", { path: scheme.path ?? \"/\", secure: scheme.secure !== false, httpOnly: scheme.httpOnly !== false, sameSite: scheme.sameSite ?? \"lax\", maxAgeSeconds: 0, expires: new Date(0) }); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "async function __sloppy_auth_rotate_session(ctx, result) { if (result === null || typeof result !== \"object\" || typeof result.cookie !== \"function\" || typeof ctx?.session?.id !== \"string\") { return result; } const scheme = __sloppy_auth_schemes.find((entry) => entry.kind === \"cookieSession\" && entry.store === \"memory\" && entry.rotation === true && entry.name === ctx.session.scheme); if (scheme === undefined) { return result; } const previous = __sloppy_auth_memory_sessions.get(ctx.session.id); if (previous === undefined || previous.revokedAt !== undefined) { return result; } if (typeof scheme.secretConfigKey !== \"string\" || scheme.secretConfigKey.length === 0) { throw new Error(\"sloppy: auth session secret config is required.\"); } const secret = __sloppy_auth_config_value(scheme.secretConfigKey, scheme.secretEnvKey); if (typeof secret !== \"string\" || secret.length === 0) { throw new Error(`sloppy: auth secret config '${scheme.secretConfigKey}' is required.`); } const nowMs = Date.now(); __sloppy_auth_memory_sessions.delete(ctx.session.id); const sid = `session-${nowMs}-${++__sloppy_auth_session_counter}`; const absolute = Number.isInteger(previous.expiresAt) ? previous.expiresAt : nowMs + (Number.isInteger(scheme.absoluteTimeoutMs) ? scheme.absoluteTimeoutMs : 86400000); const idle = Number.isInteger(scheme.idleTimeoutMs) ? nowMs + scheme.idleTimeoutMs : undefined; const record = { id: sid, claims: previous.claims, createdAt: previous.createdAt, lastSeenAt: nowMs, expiresAt: absolute, idleExpiresAt: idle }; __sloppy_auth_memory_sessions.set(sid, record); const body = Base64Url.encode(Text.utf8.encode(JSON.stringify({ sid }))); const signature = await Hmac.sha256(Secret.fromUtf8(secret), body); const value = `${body}.${Base64Url.encode(signature)}`; ctx.session = Object.freeze({ id: sid, scheme: scheme.name, issuedAt: Math.floor(record.createdAt / 1000), expiresAt: Math.floor(record.expiresAt / 1000), revoke() { __sloppy_auth_memory_sessions.delete(sid); } }); return result.cookie(scheme.cookie, value, { path: scheme.path ?? \"/\", secure: scheme.secure !== false, httpOnly: scheme.httpOnly !== false, sameSite: scheme.sameSite ?? \"lax\", maxAgeSeconds: Math.ceil((absolute - nowMs) / 1000) }); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "const Auth = Object.freeze({ signIn: __sloppy_auth_sign_in, signOut: __sloppy_auth_sign_out });",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "async function __sloppy_authenticate(ctx) { ctx.user ??= __sloppy_auth_anonymous(); for (const scheme of __sloppy_auth_schemes) { if (scheme.kind === \"jwtBearer\") { const value = __sloppy_request_header(ctx, \"authorization\"); if (typeof value !== \"string\") { continue; } const match = value.match(/^Bearer\\s+(.+)$/i); if (match === null) { return false; } const user = await __sloppy_auth_jwt(match[1], scheme); if (user === undefined) { return false; } ctx.user = user; return true; } if (scheme.kind === \"cookieSession\") { const user = await __sloppy_auth_session(ctx, scheme); if (user === undefined) { continue; } if (user === false) { return false; } ctx.user = user; return true; } if (scheme.kind === \"apiKey\") { const key = __sloppy_request_header(ctx, scheme.header); if (typeof key !== \"string\") { continue; } if (typeof scheme.configKey !== \"string\" || scheme.configKey.length === 0) { return false; } const expected = __sloppy_auth_config_value(scheme.configKey, scheme.configEnvKey); if (typeof expected !== \"string\" || expected.length === 0 || !__sloppy_ct_string(key, expected)) { return false; } ctx.user = __sloppy_auth_user({ sub: \"api-key\" }, \"apiKey\", scheme.name); return true; } } return false; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "async function __sloppy_require_auth(ctx, requirement, terminal) { __sloppy_auth_attach_context(ctx); if (requirement && requirement.required === false) { return await terminal(); } if (ctx.user === undefined || ctx.user.authenticated !== true) { const ok = await __sloppy_authenticate(ctx); if (!ok) { return __sloppy_auth_problem(401, \"Unauthorized\", \"SLOPPY_E_AUTH_UNAUTHORIZED\"); } __sloppy_auth_attach_context(ctx); } if (Array.isArray(requirement.schemes) && requirement.schemes.length !== 0 && !requirement.schemes.includes(ctx.user.scheme) && !requirement.schemes.includes(ctx.user.authScheme)) { return __sloppy_auth_problem(401, \"Unauthorized\", \"SLOPPY_E_AUTH_UNAUTHORIZED\"); } if ((!Array.isArray(requirement.schemes) || requirement.schemes.length === 0) && typeof __sloppy_auth_default_scheme === \"string\" && ctx.user.scheme !== __sloppy_auth_default_scheme && ctx.user.authScheme !== __sloppy_auth_default_scheme) { return __sloppy_auth_problem(401, \"Unauthorized\", \"SLOPPY_E_AUTH_UNAUTHORIZED\"); } if (Array.isArray(requirement.scopes) && requirement.scopes.length !== 0 && !requirement.scopes.every((scope) => ctx.user.hasScope(scope))) { return __sloppy_auth_problem(403, \"Forbidden\", \"SLOPPY_E_AUTH_FORBIDDEN\"); } if (Array.isArray(requirement.roles) && requirement.roles.length !== 0 && !requirement.roles.some((role) => ctx.user.hasRole(role))) { return __sloppy_auth_problem(403, \"Forbidden\", \"SLOPPY_E_AUTH_FORBIDDEN\"); } if (Array.isArray(requirement.claims) && requirement.claims.length !== 0 && !requirement.claims.every((claim) => ctx.user.hasClaim(claim))) { return __sloppy_auth_problem(403, \"Forbidden\", \"SLOPPY_E_AUTH_FORBIDDEN\"); } if (typeof requirement.policy === \"string\" && requirement.policy.length !== 0) { const policy = __sloppy_auth_policies.get(requirement.policy); if (typeof policy !== \"function\" || (await policy(ctx.user, ctx)) !== true) { return __sloppy_auth_problem(403, \"Forbidden\", \"SLOPPY_E_AUTH_FORBIDDEN\"); } } return await __sloppy_auth_rotate_session(ctx, await terminal()); }",
        );
    }
    if needs_request_id_helper {
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_header_value_safe(value) { if (typeof value !== \"string\" || value.trim().length === 0) { return false; } for (let index = 0; index < value.length; index += 1) { const code = value.charCodeAt(index); if ((code < 0x20 && code !== 0x09) || code === 0x7f) { return false; } } return true; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_request_id(options) { let counter = 0; return async function(ctx, next) { const incoming = options.trustIncoming ? __sloppy_request_header(ctx, options.header) : undefined; const requestId = __sloppy_header_value_safe(incoming) ? incoming : `req-${++counter}`; if (!__sloppy_header_value_safe(requestId)) { throw new TypeError(\"Sloppy RequestId generator must return a safe non-empty value.\"); } Object.defineProperty(ctx, \"requestId\", { value: requestId, enumerable: true, writable: true, configurable: true }); const result = await next(); return options.responseHeader ? __sloppy_merge_headers(result, { [options.header]: requestId }) : result; }; }",
        );
    }
    if needs_request_logging_helper {
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_request_method(ctx) { return String(ctx?.request?.method ?? ctx?.method ?? \"GET\").toUpperCase(); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_request_path(ctx) { const request = ctx?.request; return String(request?.rawTarget ?? request?.target ?? request?.path ?? ctx?.path ?? ctx?.routePattern ?? \"\"); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_response_status(result) { return Number.isInteger(result?.status) ? result.status : 200; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_write_request_log(ctx, options, startedAt, status) { if (ctx?.log === undefined || typeof ctx.log.info !== \"function\") { return; } const fields = { method: __sloppy_request_method(ctx), path: __sloppy_request_path(ctx), status }; if (options.includeRoute) { if (typeof ctx.routePattern === \"string\" && ctx.routePattern.length !== 0) { fields.route = ctx.routePattern; fields.routePattern = ctx.routePattern; } if (typeof ctx.routeName === \"string\" && ctx.routeName.length !== 0) { fields.routeName = ctx.routeName; } } if (options.includeRequestId && typeof ctx.requestId === \"string\") { fields.requestId = ctx.requestId; } if (options.includeDuration) { fields.durationMs = Math.max(0, Date.now() - startedAt); } ctx.log.info(\"request completed\", fields); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_request_logging(options) { return async function(ctx, next) { const startedAt = options.includeDuration ? Date.now() : 0; try { const result = await next(); __sloppy_write_request_log(ctx, options, startedAt, __sloppy_response_status(result)); return result; } catch (error) { __sloppy_write_request_log(ctx, options, startedAt, 500); throw error; } }; }",
        );
    }
    if needs_cors_helper {
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_cors_allowed_origin(policy, origin) { if (typeof origin !== \"string\" || origin.length === 0) { return undefined; } if (policy.origins.includes(\"*\")) { return \"*\"; } return policy.origins.includes(origin) ? origin : undefined; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_cors_vary(existing, value) { if (existing === undefined || existing.length === 0) { return value; } return existing.split(\",\").map((token) => token.trim().toLowerCase()).includes(value.toLowerCase()) ? existing : `${existing}, ${value}`; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_finish_cors(result, policy, ctx) { const allowed = __sloppy_cors_allowed_origin(policy, __sloppy_request_header(ctx, \"origin\")); if (allowed === undefined) { return result; } const headers = { ...(__sloppy_is_plain_object(result?.headers) ? result.headers : {}), \"Access-Control-Allow-Origin\": allowed }; if (!policy.origins.includes(\"*\")) { headers.Vary = __sloppy_cors_vary(headers.Vary, \"Origin\"); } if (policy.credentials) { headers[\"Access-Control-Allow-Credentials\"] = \"true\"; } if (policy.exposedHeaders.length !== 0) { headers[\"Access-Control-Expose-Headers\"] = policy.exposedHeaders.join(\", \"); } return __sloppy_merge_headers(result, headers); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_cors_requested_headers_allowed(policy, requestedHeaders) { if (typeof requestedHeaders !== \"string\" || requestedHeaders.length === 0) { return true; } const requested = requestedHeaders.split(\",\").map((header) => header.trim().toLowerCase()).filter((header) => header.length !== 0); return requested.every((header) => policy.headers.includes(header)); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_cors_preflight(policy, routeMethods, ctx) { const allowed = __sloppy_cors_allowed_origin(policy, __sloppy_request_header(ctx, \"origin\")); const requestedMethod = (__sloppy_request_header(ctx, \"access-control-request-method\") ?? \"\").toUpperCase(); const requestedHeaders = __sloppy_request_header(ctx, \"access-control-request-headers\"); const methods = policy.methods.length === 0 ? routeMethods : policy.methods; if (allowed === undefined || !methods.includes(requestedMethod) || !__sloppy_cors_requested_headers_allowed(policy, requestedHeaders)) { return Results.status(403); } const headers = { \"Access-Control-Allow-Origin\": allowed, \"Access-Control-Allow-Methods\": methods.join(\", \") }; if (!policy.origins.includes(\"*\")) { headers.Vary = \"Origin, Access-Control-Request-Method, Access-Control-Request-Headers\"; } if (policy.credentials) { headers[\"Access-Control-Allow-Credentials\"] = \"true\"; } if (policy.headers.length !== 0) { headers[\"Access-Control-Allow-Headers\"] = policy.headers.join(\", \"); } if (policy.maxAgeSeconds !== null && policy.maxAgeSeconds !== undefined) { headers[\"Access-Control-Max-Age\"] = String(policy.maxAgeSeconds); } return Results.status(204, undefined, { headers }); }",
        );
    }
    if needs_output_cache_runtime {
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_output_cache_plain_object(value) { return value !== null && typeof value === \"object\" && !Array.isArray(value); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_output_cache_header(result, value) { return { ...result, headers: { ...(__sloppy_output_cache_plain_object(result?.headers) ? result.headers : {}), \"X-Sloppy-Output-Cache\": value } }; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_output_cache_request_value(value) { return value === undefined || value === null ? \"\" : String(value); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_output_cache_request_header(ctx, name) { const headers = ctx?.request?.headers; if (headers === undefined || headers === null) { return undefined; } const lower = String(name).toLowerCase(); if (typeof headers.get === \"function\") { const direct = headers.get(name) ?? headers.get(lower); if (direct !== undefined && direct !== null) { return direct; } if (typeof headers.entries === \"function\") { for (const [key, value] of headers.entries()) { if (String(key).toLowerCase() === lower) { return value; } } } return undefined; } if (__sloppy_output_cache_plain_object(headers)) { for (const [key, value] of Object.entries(headers)) { if (key.toLowerCase() === lower) { return value; } } } return undefined; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_output_cache_body_size(result) { if (result?.body === undefined) { return 0; } if (typeof result.body === \"string\") { return Text.utf8.encode(result.body).byteLength; } if (result.body instanceof Uint8Array) { return result.body.byteLength; } const serialized = JSON.stringify(result.body); return serialized === undefined ? 0 : Text.utf8.encode(serialized).byteLength; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_output_cache_non_json(value, seen = new Set()) { if (value === null || typeof value === \"string\" || typeof value === \"number\" || typeof value === \"boolean\") { return false; } if (typeof value === \"function\" || typeof value === \"symbol\" || typeof value === \"undefined\" || typeof value === \"bigint\") { return true; } if (typeof value !== \"object\" || seen.has(value)) { return true; } seen.add(value); if (Array.isArray(value)) { return value.some((item) => __sloppy_output_cache_non_json(item, seen)); } if (!__sloppy_output_cache_plain_object(value)) { return true; } return Object.values(value).some((item) => __sloppy_output_cache_non_json(item, seen)); }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_output_cache_unsupported_reason(result) { if (result?.__sloppyResult !== true) { return \"unsupported-result\"; } if (result.kind === \"stream\") { return \"streaming\"; } if (result.kind === \"json\") { return __sloppy_output_cache_non_json(result.body) ? \"unsupported-body\" : undefined; } if (result.kind === \"text\") { return typeof result.body === \"string\" ? undefined : \"unsupported-body\"; } if (result.kind === \"bytes\") { return result.body instanceof Uint8Array ? undefined : \"unsupported-body\"; } if (result.kind === \"empty\") { return result.body === undefined ? undefined : \"unsupported-body\"; } return \"unsupported-result-kind\"; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_output_cache_key(ctx, routeKey, options, authRequired) { const query = __sloppy_output_cache_plain_object(ctx?.query) ? ctx.query : {}; const queryParts = options.varyByQuery === \"all\" ? Object.entries(query).sort(([left], [right]) => String(left).localeCompare(String(right))) : (Array.isArray(options.varyByQuery) ? options.varyByQuery : []).map((name) => [name, __sloppy_output_cache_request_value(query[name])]); const headerParts = (Array.isArray(options.varyByHeader) ? options.varyByHeader : []).map((name) => [String(name).toLowerCase(), __sloppy_output_cache_request_value(__sloppy_output_cache_request_header(ctx, name))]); const route = __sloppy_output_cache_plain_object(ctx?.route) ? ctx.route : {}; const routeParts = (Array.isArray(options.varyByRouteParams) ? options.varyByRouteParams : []).map((name) => [name, __sloppy_output_cache_request_value(route[name])]); const partition = []; if (authRequired || options.allowAuthenticated === true) { if (options.varyByUser === true) { const sub = ctx?.user?.sub ?? ctx?.user?.id; if (typeof sub !== \"string\" || sub.length === 0) { return undefined; } partition.push([\"user\", sub]); } else if (options.allowSharedAuthenticated === true) { if (options.varyByRole === true) { const roles = Array.isArray(ctx?.user?.roles) ? ctx.user.roles.map(String).sort().join(\",\") : \"\"; partition.push([\"roles\", roles]); } if (Array.isArray(options.varyByClaim)) { for (const claim of options.varyByClaim) { const value = typeof ctx?.user?.claim === \"function\" ? ctx.user.claim(claim) : ctx?.user?.claims?.[claim]; partition.push([`claim:${claim}`, __sloppy_output_cache_request_value(value)]); } } if (partition.length === 0) { return undefined; } } else { return undefined; } } return `output:${JSON.stringify({ routeKey, query: queryParts, headers: headerParts, params: routeParts, partition })}`; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_output_cache_bypass_reason(ctx, result, options, authRequired) { if ((ctx?.request?.method ?? \"GET\") !== \"GET\" && (ctx?.request?.method ?? \"GET\") !== \"HEAD\") { return \"method\"; } if (authRequired && options.allowAuthenticated !== true) { return \"auth-unsafe\"; } const unsupported = __sloppy_output_cache_unsupported_reason(result); if (unsupported !== undefined) { return unsupported; } const statusCodes = Array.isArray(options.statusCodes) ? options.statusCodes : [200, 203, 204]; if (!statusCodes.includes(result.status)) { return \"status\"; } if (Array.isArray(result.setCookies) && result.setCookies.length !== 0 && options.allowSetCookie !== true) { return \"set-cookie\"; } const headers = __sloppy_output_cache_plain_object(result.headers) ? result.headers : {}; if (Object.keys(headers).some((name) => name.toLowerCase() === \"set-cookie\") && options.allowSetCookie !== true) { return \"set-cookie\"; } const maxBodyBytes = Number.isInteger(options.maxBodyBytes) ? options.maxBodyBytes : 1048576; if (__sloppy_output_cache_body_size(result) > maxBodyBytes) { return \"body-too-large\"; } return undefined; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "async function __sloppy_output_cache(ctx, routeKey, options, authRequired, terminal) { const scope = __sloppy_framework_services.createScope(ctx); ctx.services = scope; try { const cache = scope.get(Cache.token(options.cacheName ?? \"default\")); const key = __sloppy_output_cache_key(ctx, routeKey, options, authRequired); if (key !== undefined) { const cached = await cache.get(key); if (cached !== undefined) { return __sloppy_output_cache_header(cached, \"HIT\"); } } const result = await terminal(ctx); const reason = key === undefined ? \"auth-unsafe\" : __sloppy_output_cache_bypass_reason(ctx, result, options, authRequired); if (reason !== undefined) { return __sloppy_output_cache_header(result, \"BYPASS\"); } await cache.set(key, result, { ttlMs: options.ttlMs, tags: Array.isArray(options.tags) ? options.tags : [`route:${routeKey}`] }); return __sloppy_output_cache_header(result, \"MISS\"); } finally { await scope.dispose(); } }",
        );
    }
    if needs_static_asset_helper {
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_static_header(ctx, name) { const headers = ctx?.request?.headers; if (headers && typeof headers.get === \"function\") { return headers.get(name); } return undefined; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_static_encoding_q(value, encoding) { let best = 0; for (const part of String(value ?? \"\").split(\",\")) { const pieces = part.trim().toLowerCase().split(\";\").map((entry) => entry.trim()).filter((entry) => entry.length !== 0); const token = pieces.shift(); if (token !== encoding && token !== \"*\") continue; let q = 1; for (const param of pieces) { const match = /^q=([0-9.]+)$/u.exec(param); if (match !== null) { q = Number(match[1]); } } if (Number.isFinite(q) && q >= 0 && q <= 1) best = Math.max(best, q); } return best; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_static_select_variant(ctx, asset) { const accept = __sloppy_static_header(ctx, \"accept-encoding\") ?? \"\"; let selected = asset; let bestQ = 0; for (const variant of asset.variants ?? []) { const q = __sloppy_static_encoding_q(accept, variant.contentEncoding); if (q > 0 && (selected === asset || q > bestQ)) { selected = { ...asset, bytes: variant.bytes, contentEncoding: variant.contentEncoding, contentHash: variant.contentHash }; bestQ = q; } } return selected; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppy_static_range(value, size) { if (value === undefined) return undefined; const match = /^bytes=(\\d*)-(\\d*)$/u.exec(String(value).trim()); if (match === null || (match[1].length === 0 && match[2].length === 0)) return null; let start; let end; if (match[1].length === 0) { const suffix = Number(match[2]); if (!Number.isSafeInteger(suffix) || suffix <= 0) return null; start = Math.max(0, size - suffix); end = size - 1; } else { start = Number(match[1]); end = match[2].length === 0 ? size - 1 : Number(match[2]); } if (!Number.isSafeInteger(start) || !Number.isSafeInteger(end) || start < 0 || end < start || start >= size) return null; return { start, end: Math.min(end, size - 1) }; }",
        );
        push_generated_line(
            &mut output,
            &mut generated_line,
            "function __sloppyStaticAssetResponse(ctx, asset) { const selected = __sloppy_static_select_variant(ctx, asset); const hash = selected.contentHash ?? asset.contentHash; const etag = `W/\"${hash}-${selected.contentEncoding ?? \"identity\"}\"`; const headers = { ETag: etag, \"Accept-Ranges\": asset.range === false || selected.contentEncoding !== undefined ? \"none\" : \"bytes\", \"X-Content-Type-Options\": \"nosniff\" }; if (asset.cacheControl !== undefined) headers[\"Cache-Control\"] = asset.cacheControl; if (selected.contentEncoding !== undefined) { headers[\"Content-Encoding\"] = selected.contentEncoding; headers.Vary = \"Accept-Encoding\"; } const inm = __sloppy_static_header(ctx, \"if-none-match\"); if (inm !== undefined && inm.split(\",\").map((entry) => entry.trim()).some((entry) => entry === \"*\" || entry === etag)) return Results.status(304, undefined, { headers }); if (asset.range !== false && selected.contentEncoding === undefined) { const range = __sloppy_static_range(__sloppy_static_header(ctx, \"range\"), selected.bytes.byteLength); if (range === null) return Results.status(416, undefined, { headers: { ...headers, \"Content-Range\": `bytes */${selected.bytes.byteLength}` } }); if (range !== undefined) return Results.bytes(selected.bytes.slice(range.start, range.end + 1), { status: 206, contentType: asset.contentType, headers: { ...headers, \"Content-Range\": `bytes ${range.start}-${range.end}/${selected.bytes.byteLength}` } }); } return Results.bytes(selected.bytes, { contentType: asset.contentType, headers }); }",
        );
    }
    push_generated_line(&mut output, &mut generated_line, "");

    for helper_source in &app.helper_sources {
        push_generated_source(&mut output, &mut generated_line, helper_source);
    }
    if !app.helper_sources.is_empty() {
        push_generated_line(&mut output, &mut generated_line, "");
    }

    let source_indices = source_indices_by_name(app);
    for (index, route) in app.routes.iter().enumerate() {
        let id = index + 1;
        let prefix = format!("globalThis.__sloppy_handler_{id} = ");
        let handler_start_line = generated_line;
        let handler_start_column = prefix.len();
        let source_map_start_line = handler_start_line + route.handler.source_map_line_offset;
        let source_map_start_column = if route.handler.source_map_line_offset == 0 {
            handler_start_column + route.handler.source_map_column_offset
        } else {
            route.handler.source_map_column_offset
        };
        handler_generated_starts.push(HandlerGeneratedStart {
            generated_line: handler_start_line,
            generated_column: handler_start_column,
        });
        mappings.extend(handler_source_mappings(
            &route.handler.source_text,
            route.handler.span,
            &route.handler.source,
            source_map_start_line,
            source_map_start_column,
            source_index_for(&source_indices, &route.handler.source_name),
        ));

        output.push_str(&prefix);
        if let Some(output_cache) = &route.output_cache {
            let route_key =
                serde_json::to_string(&format!("output:{}:{}", route.method, route.pattern))
                    .unwrap_or_else(|_| "\"\"".to_string());
            let options_json =
                serde_json::to_string(output_cache).unwrap_or_else(|_| "{}".to_string());
            let auth_required = route.auth.is_some();
            let wrapped = format!(
                "async function(ctx) {{ return await __sloppy_output_cache(ctx, {route_key}, {options_json}, {auth_required}, ({})); }}",
                route.handler.emitted_source
            );
            output.push_str(&wrapped);
            output.push_str(";\n");
            generated_line += wrapped.matches('\n').count() + 1;
        } else {
            output.push_str(&route.handler.emitted_source);
            output.push_str(";\n");
            generated_line += route.handler.emitted_source.matches('\n').count() + 1;
        }
        push_generated_line(
            &mut output,
            &mut generated_line,
            &format!(
                "globalThis.__sloppy_register_handler({id}, globalThis.__sloppy_handler_{id});"
            ),
        );
    }

    EmittedAppJs {
        source: output,
        mappings,
        handler_generated_starts,
    }
}

fn source_indices_by_name(app: &ExtractedApp) -> BTreeMap<&str, usize> {
    app.source_files
        .iter()
        .enumerate()
        .map(|(index, file)| (file.name.as_str(), index))
        .collect()
}

fn estimate_app_js_capacity(app: &ExtractedApp) -> usize {
    let handler_bytes = app
        .routes
        .iter()
        .map(|route| route.handler.emitted_source.len() + 96)
        .sum::<usize>();
    let helper_bytes = app.helper_sources.iter().map(String::len).sum::<usize>();
    handler_bytes + helper_bytes + 8192
}

fn emit_dynamic_web_app_js(source: &str, app: &ExtractedApp) -> EmittedAppJs {
    let mut output = String::with_capacity(source.len() + 8192);
    output.push_str("const __sloppyRuntime = globalThis.__sloppy_runtime;\n");
    output.push_str("if (__sloppyRuntime === undefined) { throw new Error(\"Sloppy bootstrap runtime was not loaded\"); }\n");
    output.push_str("const { Results, RateLimit, Realtime, SloppyRealtimeError, schema, Schema, Environment, data, sql, orm, table, column, relation, SloppyOrmError, SloppyOrmConcurrencyError, Cache, SloppyCacheError, Redis, SloppyRedisError, Time, File, Directory, Path, Random, Hash, Hmac, Password, ConstantTime, Secret, NonCryptoHash, Base64, Base64Url, Hex, Text, Binary, Compression, Checksums, TcpClient, TcpListener, TcpConnection, NetworkAddress, HttpClient, Http, HttpClientFactory, HttpError, SloppyHttpClientError, TestHttp, System, Process, Signals, OsError, BackgroundService, WorkQueue, WorkerPool, Worker, WorkerCancellationController, WorkerCancellationSignal, SloppyWorkerError, __createFrameworkServiceProvider } = __sloppyRuntime;\n");
    output.push_str(
        r#"const __sloppy_framework_services = __createFrameworkServiceProvider();
const __sloppy_framework_provider_configs = new Map([]);
function __sloppy_framework_arg(ctx, scope, binding) {
  if (binding.kind === "body.json") { return ctx.request.json(); }
  if (binding.kind === "body.form") { return ctx.request.form(); }
  if (binding.kind === "body.multipart") { return ctx.request.multipart(); }
  if (binding.kind === "context") { return ctx; }
  if (binding.kind === "injection") { return __sloppy_framework_injection(scope, binding); }
  if (binding.kind === "config") { const value = Environment.get(binding.name); if (value === undefined) { throw new Error(`sloppy: Config injection for '${binding.name}' requires an environment value.`); } return value; }
  let value;
  if (binding.kind === "route") { value = ctx.route[binding.name]; }
  else if (binding.kind === "query") { value = ctx.query[binding.name]; }
  else if (binding.kind === "header") { value = ctx.request.headers.get(binding.name); }
  else { throw new TypeError(`Sloppy Framework binding kind '${binding.kind}' is not supported.`); }
  return __sloppy_framework_coerce(value, binding);
}
function __sloppy_framework_coerce(value, binding) {
  if (value === null || value === undefined) { return value; }
  const type = String(binding.type || binding.schema || "");
  if (type.includes("boolean")) {
    const normalized = String(value).toLowerCase();
    if (normalized === "true") { return true; }
    if (normalized === "false") { return false; }
    throw new TypeError(`Sloppy Framework binding '${binding.parameter || binding.name}' expected a boolean value.`);
  }
  if (type.includes("number") || type.includes("PositiveInt") || type === "int") {
    const parsed = Number(value);
    if (!Number.isFinite(parsed)) { throw new TypeError(`Sloppy Framework binding '${binding.parameter || binding.name}' expected a numeric value.`); }
    if ((type.includes("PositiveInt") || type === "int") && !Number.isInteger(parsed)) { throw new TypeError(`Sloppy Framework binding '${binding.parameter || binding.name}' expected an integer value.`); }
    if (type.includes("PositiveInt") && parsed <= 0) { throw new TypeError(`Sloppy Framework binding '${binding.parameter || binding.name}' expected a positive integer value.`); }
    return parsed;
  }
  return value;
}
function __sloppy_framework_injection(scope, binding) {
  const dependencyName = binding.capability || (binding.name && binding.name.includes(".") ? binding.name : `data.${binding.name}`);
  if (binding.injectionKind === "service") { return scope.get(binding.name); }
  if (binding.injectionKind === "queue") { return scope.get(dependencyName); }
  if (binding.injectionKind === "provider" && binding.providerKind === "sqlite" && typeof data.sqlite === "function") { return scope.track(data.sqlite(dependencyName)); }
  throw new Error(`sloppy: ${binding.injectionKind} injection for '${binding.name}' is unavailable in this runtime lane.`);
}
"#,
    );
    output.push_str(
        r#"function __sloppy_create_dynamic_app() {
  const routes = [];
  let frozen = false;
  function assertOpen() { if (frozen) { throw new Error("Sloppy app is frozen"); } }
  function register(method, pattern, handler) {
    assertOpen();
    if (typeof pattern !== "string" || pattern.length === 0 || pattern[0] !== "/") { throw new TypeError("Sloppy app route pattern must be an absolute path string."); }
    if (typeof handler !== "function") { throw new TypeError("Sloppy app route handler must be callable."); }
    const route = { method, pattern, handler, name: undefined, metadata: Object.freeze({}) };
    routes.push(route);
    function metadataForSchema(schema) {
      if (schema === undefined || schema === null || typeof schema !== "object" || schema.metadata === undefined) { throw new TypeError("Sloppy endpoint schema metadata is required."); }
      return schema.metadata;
    }
    function setMetadata(key, value) {
      route.metadata = Object.freeze({ ...route.metadata, [key]: Object.freeze(value) });
    }
    return Object.freeze({
      withName(name) { route.name = String(name); return this; },
      accepts(schema) { setMetadata("accepts", { contentType: "application/json", schema: metadataForSchema(schema) }); return this; },
      returns(schema, options = undefined) { setMetadata("returns", { status: options?.status ?? 200, contentType: options?.contentType ?? "application/json", schema: metadataForSchema(schema) }); return this; },
      withTags() { return this; }
    });
  }
  const app = {
    services: __sloppy_framework_services,
    get(pattern, handler) { return register("GET", pattern, handler); },
    post(pattern, handler) { return register("POST", pattern, handler); },
    put(pattern, handler) { return register("PUT", pattern, handler); },
    patch(pattern, handler) { return register("PATCH", pattern, handler); },
    delete(pattern, handler) { return register("DELETE", pattern, handler); },
    mapGet(pattern, handler) { return register("GET", pattern, handler); },
    mapPost(pattern, handler) { return register("POST", pattern, handler); },
    mapPut(pattern, handler) { return register("PUT", pattern, handler); },
    mapPatch(pattern, handler) { return register("PATCH", pattern, handler); },
    mapDelete(pattern, handler) { return register("DELETE", pattern, handler); },
    useStaticFiles(options) {
      assertOpen();
      if (options === null || typeof options !== "object" || typeof options.requestPath !== "string" || typeof options.root !== "string") { throw new TypeError("Sloppy app.useStaticFiles requires literal requestPath and root options."); }
      throw new TypeError("Sloppy app.useStaticFiles is not supported for dynamic fallback routes yet.");
    },
    staticFiles(mount, options) {
      assertOpen();
      if (typeof mount !== "string" || options === null || typeof options !== "object" || typeof options.root !== "string") { throw new TypeError("Sloppy app.staticFiles requires a literal mount path and root option."); }
      throw new TypeError("Sloppy app.staticFiles is not supported for dynamic fallback routes yet.");
    },
    spa(mount, options) {
      assertOpen();
      if (typeof mount !== "string" || options === null || typeof options !== "object" || typeof options.root !== "string") { throw new TypeError("Sloppy app.spa requires a literal mount path and root option."); }
      throw new TypeError("Sloppy app.spa is not supported for dynamic fallback routes yet.");
    },
    freeze() { frozen = true; return app; },
    __getRoutes() { return routes.slice(); }
  };
  return app;
}
const Sloppy = Object.freeze({ create: __sloppy_create_dynamic_app, createBuilder() { return { build: __sloppy_create_dynamic_app }; } });
"#,
    );
    output.push_str(source);
    output.push_str(
        r#"
function __sloppy_dynamic_match(pattern, path) {
  const patternParts = pattern.split("/").filter(Boolean);
  const pathParts = path.split("?")[0].split("/").filter(Boolean);
  if (patternParts.length !== pathParts.length) { return null; }
  const route = Object.create(null);
  for (let index = 0; index < patternParts.length; index += 1) {
    const segment = patternParts[index];
    const value = decodeURIComponent(pathParts[index] ?? "");
    if (segment.startsWith("{") && segment.endsWith("}")) {
      const parts = segment.slice(1, -1).split(":");
      if (parts.length > 2) { return null; }
      const [name, kind = "str"] = parts;
      if (value.length === 0) { return null; }
      if (kind !== "str" && kind !== "int" && kind !== "uuid" && kind !== "alpha" && kind !== "float") { return null; }
      if (kind === "int" && !/^[0-9]+$/u.test(value)) { return null; }
      if (kind === "uuid" && !/^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$/u.test(value)) { return null; }
      if (kind === "alpha" && !/^[A-Za-z]+$/u.test(value)) { return null; }
      if (kind === "float" && !(/^[0-9]*\.[0-9]+$/u.test(value) || /^[0-9]+\.[0-9]*$/u.test(value))) { return null; }
      route[name] = value;
    } else if (segment.startsWith(":")) {
      route[segment.slice(1).split(":")[0]] = value;
    } else if (segment !== value) {
      return null;
    }
  }
  return route;
}
function __sloppy_dynamic_response(result) {
  if (typeof result === "string") { return { __sloppyResult: true, kind: "text", status: 200, contentType: "text/plain; charset=utf-8", body: result }; }
  if (result !== null && typeof result === "object" && result.__sloppyResult === true) { return result; }
  if (result !== null && typeof result === "object" && result.kind === undefined && result.status === undefined && result.body === undefined) { return { __sloppyResult: true, kind: "json", status: 200, contentType: "application/json; charset=utf-8", body: result }; }
  const status = Number.isInteger(result?.status) ? result.status : 200;
  const kind = result?.kind ?? "text";
  if (kind === "empty") { return { __sloppyResult: true, kind: "empty", status, contentType: "text/plain; charset=utf-8", body: "" }; }
  if (kind === "json" || kind === "problem") { return { __sloppyResult: true, kind, status, contentType: result.contentType ?? "application/json; charset=utf-8", body: result.body ?? null }; }
  return { __sloppyResult: true, kind: "text", status, contentType: result?.contentType ?? "text/plain; charset=utf-8", body: String(result?.body ?? "") };
}
function __sloppy_dynamic_body_text(response) {
  if (response.kind === "json" || response.kind === "problem") { return response.__sloppyJsonText ?? JSON.stringify(response.body ?? null); }
  return String(response.body ?? "");
}
function __sloppy_dynamic_http_text(response) {
  const body = __sloppy_dynamic_body_text(response);
  return `HTTP/1.1 ${response.status} OK\r\ncontent-type: ${response.contentType}\r\ncontent-length: ${body.length}\r\n\r\n${body}`;
}
globalThis.__sloppy_dispatch_dynamic_result = async function(method, target) {
  const app = globalThis.__sloppy_dynamic_default_app;
  if (app === undefined || typeof app.__getRoutes !== "function") { return { __sloppyResult: true, kind: "text", status: 500, contentType: "text/plain; charset=utf-8", body: "Dynamic Sloppy app was not initialized\n" }; }
  app.freeze();
  const path = String(target).split("?")[0];
  for (const route of app.__getRoutes()) {
    if (route.method !== String(method).toUpperCase()) { continue; }
    const routeValues = __sloppy_dynamic_match(route.pattern, path);
    if (routeValues === null) { continue; }
    const result = await route.handler({ route: routeValues, query: Object.freeze({}), request: Object.freeze({ method, path }) });
    return __sloppy_dynamic_response(result);
  }
  return { __sloppyResult: true, kind: "text", status: 404, contentType: "text/plain; charset=utf-8", body: "Not Found\n" };
};
globalThis.__sloppy_dispatch_dynamic = async function(method, target) {
  return __sloppy_dynamic_http_text(await globalThis.__sloppy_dispatch_dynamic_result(method, target));
};
"#,
    );
    let mut mappings = Vec::with_capacity(app.routes.len());
    let mut handler_generated_starts = Vec::with_capacity(app.routes.len());
    let mut generated_line = output.matches('\n').count();
    let source_indices = source_indices_by_name(app);
    for (index, route) in app.routes.iter().enumerate() {
        let id = index + 1;
        let prefix = format!("globalThis.__sloppy_handler_{id} = ");
        let handler_start_line = generated_line;
        let handler_start_column = prefix.len();
        let source_map_start_line = handler_start_line + route.handler.source_map_line_offset;
        let source_map_start_column = if route.handler.source_map_line_offset == 0 {
            handler_start_column + route.handler.source_map_column_offset
        } else {
            route.handler.source_map_column_offset
        };
        handler_generated_starts.push(HandlerGeneratedStart {
            generated_line: handler_start_line,
            generated_column: handler_start_column,
        });
        mappings.extend(handler_source_mappings(
            &route.handler.source_text,
            route.handler.span,
            &route.handler.source,
            source_map_start_line,
            source_map_start_column,
            source_index_for(&source_indices, &route.handler.source_name),
        ));

        output.push_str(&prefix);
        output.push_str(&route.handler.emitted_source);
        output.push_str(";\n");
        generated_line += route.handler.emitted_source.matches('\n').count() + 1;
        push_generated_line(
            &mut output,
            &mut generated_line,
            &format!(
                "globalThis.__sloppy_register_handler({id}, globalThis.__sloppy_handler_{id});"
            ),
        );
    }
    EmittedAppJs {
        source: output,
        mappings,
        handler_generated_starts,
    }
}

fn emit_program_app_js(app: &ExtractedApp) -> EmittedAppJs {
    let mut output = String::with_capacity(
        app.program_modules
            .iter()
            .map(|module| module.emitted_source.len())
            .sum::<usize>()
            + 8192,
    );
    output.push_str("(function() {\n");
    output.push_str("  if (!globalThis.__sloppy_runtime) {\n");
    output.push_str("    throw new Error(\"Sloppy bootstrap runtime was not loaded\");\n");
    output.push_str("  }\n");
    output.push_str("  const __sloppy_program_modules = Object.create(null);\n");
    output.push_str("  const __sloppy_program_cache = Object.create(null);\n");
    output.push_str("  const __sloppy_program_resolutions = Object.create(null);\n");
    output.push_str("  const __sloppy_program_package_roots = Object.create(null);\n");
    for package in &app.dependency_graph.packages {
        output.push_str("  __sloppy_program_package_roots[");
        output.push_str(&json_string(&package.name));
        output.push_str("] = ");
        output.push_str(&json_string(&package.root));
        output.push_str(";\n");
    }
    for module in &app.dependency_graph.modules {
        for resolved in &module.resolved_imports {
            output.push_str("  __sloppy_program_resolutions[");
            output.push_str(&json_string(&format!(
                "{}\0{}",
                module.id, resolved.specifier
            )));
            output.push_str("] = ");
            output.push_str(&json_string(&resolved.resolved_id));
            output.push_str(";\n");
        }
    }
    output.push_str("  function __sloppy_program_dirname(id) { const index = String(id).lastIndexOf('/'); return index < 0 ? '.' : String(id).slice(0, index); }\n");
    output.push_str("  function __sloppy_program_resolve(fromId, specifier) {\n");
    output.push_str("    if (__sloppy_program_modules[specifier]) { return specifier; }\n");
    output.push_str(
        "    const mapped = __sloppy_program_resolutions[`${fromId}\\u0000${specifier}`];\n",
    );
    output.push_str("    if (mapped) { return mapped; }\n");
    output.push_str(
        "    if (String(specifier).startsWith('./') || String(specifier).startsWith('../')) {\n",
    );
    output.push_str(
        "      const base = __sloppy_program_dirname(fromId).split('/').filter(Boolean);\n",
    );
    output.push_str("      for (const part of String(specifier).split('/')) { if (!part || part === '.') { continue; } if (part === '..') { base.pop(); } else { base.push(part); } }\n");
    output.push_str("      const raw = base.join('/');\n");
    output.push_str("      for (const candidate of [raw, `${raw}.js`, `${raw}.mjs`, `${raw}.cjs`, `${raw}.ts`, `${raw}.json`, `${raw}/index.js`, `${raw}/index.mjs`, `${raw}/index.cjs`, `${raw}/index.ts`]) { if (__sloppy_program_modules[candidate]) { return candidate; } }\n");
    output.push_str("    }\n");
    output.push_str("    throw new Error(`SLOPPY_E_MODULE_NOT_FOUND: Dynamic import or require resolved to '${specifier}', but that module was not included in the Sloppy artifact graph. Add a moduleInclude pattern for computed imports.`);\n");
    output.push_str("  }\n");
    output.push_str("  function __sloppy_program_require_from(fromId, specifier) { return __sloppy_program_require(__sloppy_program_resolve(fromId, specifier)); }\n");
    output.push_str("  function __sloppy_program_import_from(fromId, specifier) { return Promise.resolve(__sloppy_program_require_from(fromId, specifier)); }\n");
    output.push_str("  function __sloppy_program_canonical_from_id(fromId) {\n");
    output.push_str("    let baseId = String(fromId || \"\").replace(/^file:\\/\\//, \"\").replace(/\\\\/g, \"/\");\n");
    output.push_str("    const packageMatch = baseId.match(/(?:^|\\/)node_modules\\/(@[^\\/]+\\/[^\\/]+|[^\\/]+)(\\/.*)?$/);\n");
    output.push_str("    if (packageMatch && __sloppy_program_package_roots[packageMatch[1]]) { baseId = `${__sloppy_program_package_roots[packageMatch[1]]}${packageMatch[2] || \"/index.js\"}`; }\n");
    output.push_str("    if (__sloppy_program_modules[baseId]) { return baseId; }\n");
    output.push_str("    const suffix = baseId.replace(/^\\/+/, \"\");\n");
    output.push_str("    for (const id of Object.keys(__sloppy_program_modules)) { const normalized = String(id).replace(/\\\\/g, \"/\"); if (normalized === suffix || normalized.endsWith(`/${suffix}`)) { return id; } }\n");
    output.push_str("    return baseId;\n");
    output.push_str("  }\n");
    output.push_str("  function __sloppy_program_require(id) {\n");
    output.push_str(
        "    if (__sloppy_program_cache[id]) { return __sloppy_program_cache[id].exports; }\n",
    );
    output.push_str("    const factory = __sloppy_program_modules[id];\n");
    output.push_str("    if (typeof factory !== \"function\") { throw new Error(`Sloppy program module '${id}' was not found`); }\n");
    output.push_str("    const module = { exports: {} };\n");
    output.push_str("    __sloppy_program_cache[id] = module;\n");
    output.push_str("    const localRequire = function(specifier) { return __sloppy_program_require_from(id, specifier); };\n");
    output.push_str("    localRequire.resolve = function(specifier) { return __sloppy_program_resolve(id, specifier); };\n");
    output.push_str("    localRequire.cache = __sloppy_program_cache;\n");
    output.push_str(
        "    factory(module.exports, module, localRequire, id, __sloppy_program_dirname(id));\n",
    );
    output.push_str("    return module.exports;\n");
    output.push_str("  }\n");
    output.push_str("  globalThis.__sloppy_program_create_require = function(fromId) { const baseId = __sloppy_program_canonical_from_id(fromId); const req = function(specifier) { return __sloppy_program_require_from(baseId, specifier); }; req.resolve = function(specifier) { return __sloppy_program_resolve(baseId, specifier); }; req.cache = __sloppy_program_cache; return req; };\n");
    for module in &app.program_modules {
        output.push_str("  __sloppy_program_modules[");
        output.push_str(&json_string(&module.id));
        output.push_str("] = function(exports, module, require, __filename, __dirname) {\n");
        for line in module.emitted_source.lines() {
            output.push_str("    ");
            output.push_str(line);
            output.push('\n');
        }
        output.push_str("  };\n");
    }
    output.push_str("  function __sloppy_program_format(value) {\n");
    output.push_str("    if (typeof value === \"string\") { return value; }\n");
    output.push_str("    if (typeof value === \"number\" || typeof value === \"boolean\" || typeof value === \"bigint\") { return String(value); }\n");
    output.push_str("    if (value === undefined) { return \"undefined\"; }\n");
    output.push_str("    if (value === null) { return \"null\"; }\n");
    output.push_str("    if (typeof value === \"symbol\") { return String(value); }\n");
    output.push_str("    if (typeof value === \"function\") { return value.name ? `[Function: ${value.name}]` : \"[Function]\"; }\n");
    output.push_str("    if (value instanceof Error) {\n");
    output.push_str(
        "      let text = value.stack || `${value.name || \"Error\"}: ${value.message || \"\"}`;\n",
    );
    output.push_str("      if (typeof AggregateError !== \"undefined\" && value instanceof AggregateError && Array.isArray(value.errors)) {\n");
    output.push_str(
        "        const errors = value.errors.map(__sloppy_program_format).join(\"; \");\n",
    );
    output.push_str("        text = `${text}\\nErrors: ${errors}`;\n");
    output.push_str("      }\n");
    output.push_str("      return text;\n");
    output.push_str("    }\n");
    output.push_str("    const seen = [];\n");
    output.push_str("    try {\n");
    output.push_str("      const text = JSON.stringify(value, function(_key, nested) {\n");
    output.push_str("        if (typeof nested === \"bigint\") { return String(nested); }\n");
    output.push_str("        if (nested !== null && typeof nested === \"object\") {\n");
    output.push_str("          if (seen.indexOf(nested) !== -1) { return \"[Circular]\"; }\n");
    output.push_str("          seen.push(nested);\n");
    output.push_str("        }\n");
    output.push_str("        return nested;\n");
    output.push_str("      });\n");
    output.push_str("      return text === undefined ? String(value) : text;\n");
    output.push_str("    } catch (_error) {\n");
    output.push_str("      return \"[Circular]\";\n");
    output.push_str("    }\n");
    output.push_str("  }\n");
    output.push_str("  function __sloppy_program_console(events, stream, values) {\n");
    output.push_str(
        "    const parts = Array.prototype.slice.call(values).map(__sloppy_program_format);\n",
    );
    output.push_str("    events.push({ stream, text: `${parts.join(\" \")}\\n` });\n");
    output.push_str("  }\n");
    output.push_str("  function __sloppy_program_is_result(value) {\n");
    output.push_str("    return value !== null && typeof value === \"object\" && value.__sloppyResult === true && typeof value.kind === \"string\";\n");
    output.push_str("  }\n");
    output.push_str("  function __sloppy_program_has_exit_code(value) {\n");
    output.push_str("    return value !== null && typeof value === \"object\" && Object.prototype.hasOwnProperty.call(value, \"exitCode\");\n");
    output.push_str("  }\n");
    output.push_str("  function __sloppy_program_exit_code(value) {\n");
    output.push_str("    let code = 0;\n");
    output.push_str("    if (typeof value === \"number\") { code = value; }\n");
    output.push_str(
        "    else if (__sloppy_program_has_exit_code(value)) { code = value.exitCode; }\n",
    );
    output.push_str("    else { return 0; }\n");
    output.push_str("    if (!Number.isInteger(code) || code < 0 || code > 255) {\n");
    output.push_str(
        "      throw new Error(\"Sloppy program exit code must be an integer from 0 to 255\");\n",
    );
    output.push_str("    }\n");
    output.push_str("    return code;\n");
    output.push_str("  }\n");
    output.push_str("  function __sloppy_program_result(value, events) {\n");
    output.push_str("    const exitCode = __sloppy_program_exit_code(value);\n");
    output.push_str("    if (events.length === 0 && (typeof value === \"string\" || __sloppy_program_is_result(value))) { return value; }\n");
    output.push_str("    return { __sloppyResult: true, kind: \"json\", status: 200, contentType: \"application/json\", body: { __sloppyProgramResult: true, exitCode, events } };\n");
    output.push_str("  }\n");
    let entry = app
        .program_entry
        .as_deref()
        .or_else(|| app.program_modules.last().map(|module| module.id.as_str()))
        .unwrap_or("");
    output.push_str(
        "  globalThis.__sloppy_program_main = async function __sloppy_program_main() {\n",
    );
    output.push_str("    const events = [];\n");
    output.push_str("    const args = Array.isArray(globalThis.__sloppy_program_args) ? globalThis.__sloppy_program_args.slice() : [];\n");
    output.push_str("    const ctx = globalThis.__sloppy_program_context && typeof globalThis.__sloppy_program_context === \"object\" ? globalThis.__sloppy_program_context : { kind: \"program\", args, cwd: \"\", environment: \"Development\", plan: { kind: \"program\", metadataCompleteness: \"opaque\" } };\n");
    output.push_str("    const previousConsole = globalThis.console;\n");
    output.push_str("    const previousGlobal = globalThis.global;\n");
    output.push_str("    const previousProcess = globalThis.process;\n");
    output.push_str("    const previousBuffer = globalThis.Buffer;\n");
    output.push_str("    const programConsole = Object.create(previousConsole || null);\n");
    output.push_str(
        "    programConsole.log = function() { __sloppy_program_console(events, \"stdout\", arguments); };\n",
    );
    output.push_str(
        "    programConsole.info = function() { __sloppy_program_console(events, \"stdout\", arguments); };\n",
    );
    output.push_str(
        "    programConsole.debug = function() { __sloppy_program_console(events, \"stdout\", arguments); };\n",
    );
    output.push_str(
        "    programConsole.warn = function() { __sloppy_program_console(events, \"stderr\", arguments); };\n",
    );
    output.push_str(
        "    programConsole.error = function() { __sloppy_program_console(events, \"stderr\", arguments); };\n",
    );
    output.push_str("    Object.freeze(programConsole);\n");
    output.push_str("    let value;\n");
    output.push_str("    let failed = false;\n");
    output.push_str("    try {\n");
    output.push_str("    globalThis.console = programConsole;\n");
    output.push_str("    if (previousGlobal === undefined) { globalThis.global = globalThis; }\n");
    output.push_str("    if (__sloppy_program_modules[\"sloppy/node/process\"]) { globalThis.process = __sloppy_program_require(\"sloppy/node/process\"); }\n");
    output.push_str("    if (__sloppy_program_modules[\"sloppy/node/buffer\"]) { globalThis.Buffer = __sloppy_program_require(\"sloppy/node/buffer\").Buffer; }\n");
    output.push_str("    const entry = __sloppy_program_require(");
    output.push_str(&json_string(entry));
    output.push_str(");\n");
    output.push_str(
        "    if (typeof entry.main === \"function\") { value = await entry.main(args, ctx); }\n",
    );
    output.push_str(
        "    else if (typeof entry.default === \"function\") { value = await entry.default(args, ctx); }\n",
    );
    output.push_str("    } catch (error) {\n");
    output.push_str("      failed = true;\n");
    output.push_str("      __sloppy_program_console(events, \"stderr\", [error]);\n");
    output.push_str("      value = { exitCode: 1 };\n");
    output.push_str("    } finally {\n");
    output.push_str("      if (previousConsole === undefined) {\n");
    output.push_str("        try { delete globalThis.console; } catch (_error) { globalThis.console = undefined; }\n");
    output.push_str("      } else {\n");
    output.push_str("        globalThis.console = previousConsole;\n");
    output.push_str("      }\n");
    output.push_str("      if (previousGlobal === undefined) { try { delete globalThis.global; } catch (_error) { globalThis.global = undefined; } } else { globalThis.global = previousGlobal; }\n");
    output.push_str("      if (previousProcess === undefined) { try { delete globalThis.process; } catch (_error) { globalThis.process = undefined; } } else { globalThis.process = previousProcess; }\n");
    output.push_str("      if (previousBuffer === undefined) { try { delete globalThis.Buffer; } catch (_error) { globalThis.Buffer = undefined; } } else { globalThis.Buffer = previousBuffer; }\n");
    output.push_str("    }\n");
    output.push_str("    if (!failed) {\n");
    output.push_str("      try { return __sloppy_program_result(value, events); }\n");
    output.push_str("      catch (error) { __sloppy_program_console(events, \"stderr\", [error]); value = { exitCode: 1 }; }\n");
    output.push_str("    }\n");
    output.push_str("    return __sloppy_program_result(value, events);\n");
    output.push_str("  };\n");
    output.push_str("})();\n");
    EmittedAppJs {
        source: output,
        mappings: Vec::new(),
        handler_generated_starts: Vec::new(),
    }
}

pub(super) fn emit_source_map(app: &ExtractedApp, emitted_js: &EmittedAppJs) -> String {
    let mappings = &emitted_js.mappings;
    let sources = app
        .source_files
        .iter()
        .map(|file| file.name.clone())
        .collect::<Vec<_>>();
    let sources_content = app
        .source_files
        .iter()
        .map(|file| file.source.clone())
        .collect::<Vec<_>>();
    let source_files = app
        .source_files
        .iter()
        .map(|file| {
            json!({
                "path": file.name,
                "hash": sha256_hex(&file.source)
            })
        })
        .collect::<Vec<_>>();
    let handlers = app
        .routes
        .iter()
        .enumerate()
        .map(|(index, route)| {
            let id = index + 1;
            let generated = emitted_js
                .handler_generated_starts
                .get(index)
                .map(|start| generated_location_json(start.generated_line, start.generated_column))
                .unwrap_or_else(|| json!(null));
            json!({
                "id": id,
                "generated": generated,
                "source": source_location_json(
                    &route.handler.source_name,
                    &route.handler.source_text,
                    route.handler.span
                )
            })
        })
        .collect::<Vec<_>>();
    let routes = app
        .routes
        .iter()
        .enumerate()
        .map(|(index, route)| {
            let mut route_json = json!({
                "handlerId": index + 1,
                "method": route.method,
                "pattern": route.pattern,
                "module": route.module,
                "source": source_location_json(&route.source_name, &route.source, route.span)
            });
            if route.kind != "http" {
                route_json["kind"] = json!(route.kind);
            }
            if let Some(websocket) = &route.websocket {
                route_json["websocket"] = websocket_options_json(websocket);
            }
            if let Some(realtime) = &route.realtime {
                route_json["realtime"] = json!({
                    "kind": "framework",
                    "transport": "websocket",
                    "metadataStatus": "partial",
                    "partialReason": "channel and option expressions are preserved; static event and schema extraction is not complete in this alpha",
                    "channelExpression": realtime.channel_source,
                    "optionsExpression": realtime.options_source
                });
            }
            if let Some(framework_path) = &route.framework_path {
                route_json["frameworkPath"] = json!(framework_path);
            }
            if !route.tags.is_empty() {
                route_json["tags"] = json!(route.tags);
            }
            if let Some(health) = &route.health {
                route_json["health"] = json!({
                    "kind": health.kind,
                    "checks": health.checks
                });
            }
            if let Some(output_cache) = &route.output_cache {
                route_json["outputCache"] = output_cache.clone();
            }
            if let Some(cache_headers) = &route.cache_headers {
                route_json["cacheHeaders"] = cache_headers.clone();
            }
            if !route.middleware.is_empty() {
                route_json["middleware"] = json!(route
                    .middleware
                    .iter()
                    .map(|middleware| {
                        json!({
                            "kind": middleware.kind,
                            "sequence": middleware.sequence,
                            "source": source_location_json(
                                &middleware.source_name,
                                &middleware.source_text,
                                middleware.span
                            )
                        })
                    })
                    .collect::<Vec<_>>());
            }
            if let Some(cors) = &route.cors {
                route_json["cors"] = json!({
                    "origins": cors.origins,
                    "methods": cors.methods,
                    "headers": cors.headers,
                    "exposedHeaders": cors.exposed_headers,
                    "credentials": cors.credentials,
                    "maxAgeSeconds": cors.max_age_seconds,
                    "preflight": route.cors_preflight
                });
            }
            route_json
        })
        .collect::<Vec<_>>();
    let modules = app
        .modules
        .iter()
        .map(|module| {
            json!({
                "name": module.name,
                "source": {
                    "path": module.source_name
                }
            })
        })
        .collect::<Vec<_>>();
    let schemas = app
        .schemas
        .iter()
        .map(|schema| {
            json!({
                "name": schema.name,
                "source": source_location_json(&schema.source_name, &schema.source, schema.span)
            })
        })
        .collect::<Vec<_>>();
    let providers = app
        .capabilities
        .iter()
        .map(|capability| {
            json!({
                "token": capability.token,
                "capabilityKind": capability.capability_kind,
                "providerKind": capability.provider,
                "source": source_location_json(
                    &capability.source_name,
                    &capability.source,
                    capability.span
                )
            })
        })
        .collect::<Vec<_>>();
    let capabilities = app
        .capabilities
        .iter()
        .map(|capability| {
            json!({
                "token": capability.token,
                "kind": capability.capability_kind,
                "access": capability.access,
                "source": source_location_json(
                    &capability.source_name,
                    &capability.source,
                    capability.span
                )
            })
        })
        .collect::<Vec<_>>();
    let effects = app
        .routes
        .iter()
        .enumerate()
        .flat_map(|(index, route)| {
            route.handler.effects.iter().map(move |effect| {
                json!({
                    "handlerId": index + 1,
                    "provider": effect.provider,
                    "capabilityKind": effect.capability_kind,
                    "providerKind": effect.provider_kind,
                    "access": effect.access,
                    "operation": effect.operation,
                    "reason": effect.reason,
                    "source": source_location_json(
                        &effect.source_name,
                        &effect.source_text,
                        effect.span
                    )
                })
            })
        })
        .collect::<Vec<_>>();
    let mut x_sloppy = json!({
        "version": 1,
        "sourceFiles": source_files,
        "handlers": handlers,
        "routes": routes,
        "modules": modules,
        "schemas": schemas,
        "providers": providers,
        "capabilities": capabilities,
        "effects": effects
    });
    if app.kind == ProjectKind::Program {
        let program_modules = app
            .program_modules
            .iter()
            .map(|module| {
                json!({
                    "id": module.id,
                    "source": {
                        "path": module.source_name
                    },
                    "format": module.format.as_str(),
                    "hash": sha256_hex(&module.source)
                })
            })
            .collect::<Vec<_>>();
        x_sloppy["kind"] = json!(app.kind.as_str());
        x_sloppy["programModules"] = json!(program_modules);
        if app.dependency_graph.has_entries() {
            x_sloppy["dependencyGraph"] = dependency_graph_json(&app.dependency_graph);
        }
    }
    let value = json!({
        "version": 3,
        "file": "app.js",
        "sources": sources,
        "sourcesContent": sources_content,
        "names": [],
        "mappings": encode_source_map_mappings(mappings),
        "x_sloppy": x_sloppy
    });

    let json = serde_json::to_string_pretty(&value).unwrap_or_else(|_| "{}".to_string());
    format!("{json}\n")
}

fn websocket_options_json(options: &WebSocketRouteOptionsMetadata) -> Value {
    let origins = match &options.origins {
        Some(WebSocketOriginsMetadata::Any) => json!("*"),
        Some(WebSocketOriginsMetadata::List(origins)) => json!(origins),
        None => Value::Null,
    };
    json!({
        "protocols": &options.protocols,
        "origins": origins,
        "maxMessageBytes": options.max_message_bytes,
        "maxSendQueueBytes": options.max_send_queue_bytes,
        "heartbeatMs": options.heartbeat_ms,
        "idleTimeoutMs": options.idle_timeout_ms,
        "closeTimeoutMs": options.close_timeout_ms,
        "slowClientPolicy": options.slow_client_policy.as_ref(),
        "compression": options.compression.unwrap_or(false)
    })
}

fn generated_location_json(generated_line: usize, generated_column: usize) -> Value {
    json!({
        "line": generated_line + 1,
        "column": generated_column + 1
    })
}

fn push_generated_line(output: &mut String, generated_line: &mut usize, line: &str) {
    output.push_str(line);
    output.push('\n');
    *generated_line += 1;
}

fn push_generated_source(output: &mut String, generated_line: &mut usize, source: &str) {
    output.push_str(source);
    output.push('\n');
    *generated_line += source.split('\n').count();
}

fn handler_source_mappings(
    source: &str,
    span: Span,
    handler_source: &str,
    generated_start_line: usize,
    generated_start_column: usize,
    source_index: usize,
) -> Vec<SourceMapMapping> {
    let Some(source_start) = usize::try_from(span.start).ok() else {
        return Vec::new();
    };
    let mut mappings = Vec::new();
    let mut relative_start = 0usize;
    let mut generated_line_offset = 0usize;

    loop {
        let original_offset = source_start.saturating_add(relative_start);
        let (original_line, original_column) = line_column(
            source,
            span.start
                .saturating_add(u32::try_from(relative_start).unwrap_or(u32::MAX)),
        );
        mappings.push(SourceMapMapping {
            generated_line: generated_start_line + generated_line_offset,
            generated_column: if generated_line_offset == 0 {
                generated_start_column
            } else {
                0
            },
            source_index,
            original_line: original_line.saturating_sub(1),
            original_column: original_column.saturating_sub(1),
        });

        let Some(next_newline) = handler_source[relative_start..].find('\n') else {
            break;
        };
        relative_start += next_newline + 1;
        generated_line_offset += 1;
        if relative_start >= handler_source.len() || original_offset >= source.len() {
            break;
        }
    }

    mappings
}

fn source_index_for(source_indices: &BTreeMap<&str, usize>, source_name: &str) -> usize {
    source_indices.get(source_name).copied().unwrap_or(0)
}

fn encode_source_map_mappings(mappings: &[SourceMapMapping]) -> String {
    if mappings.is_empty() {
        return String::new();
    }

    let mut sorted = mappings.to_vec();
    sorted.sort_by_key(|mapping| (mapping.generated_line, mapping.generated_column));
    let Some(max_line) = sorted.last().map(|mapping| mapping.generated_line) else {
        return String::new();
    };

    let mut output = String::new();
    let mut mapping_index = 0usize;
    let mut previous_source = 0i64;
    let mut previous_original_line = 0i64;
    let mut previous_original_column = 0i64;

    for line in 0..=max_line {
        if line > 0 {
            output.push(';');
        }

        let mut previous_generated_column = 0i64;
        let mut first_segment = true;
        while mapping_index < sorted.len() && sorted[mapping_index].generated_line == line {
            let mapping = &sorted[mapping_index];
            if !first_segment {
                output.push(',');
            }
            first_segment = false;

            let generated_column = usize_to_i64(mapping.generated_column);
            let source_index = usize_to_i64(mapping.source_index);
            let original_line = usize_to_i64(mapping.original_line);
            let original_column = usize_to_i64(mapping.original_column);
            output.push_str(&encode_vlq(generated_column - previous_generated_column));
            output.push_str(&encode_vlq(source_index - previous_source));
            output.push_str(&encode_vlq(original_line - previous_original_line));
            output.push_str(&encode_vlq(original_column - previous_original_column));

            previous_generated_column = generated_column;
            previous_source = source_index;
            previous_original_line = original_line;
            previous_original_column = original_column;
            mapping_index += 1;
        }
    }

    output
}

fn usize_to_i64(value: usize) -> i64 {
    i64::try_from(value).unwrap_or(i64::MAX)
}

fn encode_vlq(value: i64) -> String {
    const BASE64: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut vlq = if value < 0 {
        ((value.saturating_abs() as u64) << 1) | 1
    } else {
        (value as u64) << 1
    };
    let mut output = String::new();

    loop {
        let mut digit = (vlq & 31) as usize;
        vlq >>= 5;
        if vlq > 0 {
            digit |= 32;
        }
        output.push(char::from(BASE64[digit]));
        if vlq == 0 {
            break;
        }
    }

    output
}

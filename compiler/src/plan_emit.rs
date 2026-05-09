//! Deterministic `app.plan.json` emission from the internal AppGraph.

use oxc_span::Span;
use serde_json::{json, Value};

use crate::diagnostic::Diagnostic;
use crate::graph::{route_parameter_names, ConfigurationPackageEntry, ExtractedApp};
use crate::hash::sha256_hex;
use crate::source::line_column;
use crate::validation::{
    plan_completeness, route_completeness, Completeness, RouteCompletenessInput,
};
use crate::version::{COMPILER_VERSION, RUNTIME_MINIMUM_VERSION, STDLIB_VERSION};

fn completeness_json(completeness: &Completeness) -> Value {
    json!({
        "status": completeness.status.as_str(),
        "reasons": completeness
            .reasons
            .iter()
            .map(|reason| {
                json!({
                    "code": reason.code,
                    "message": reason.message
                })
            })
            .collect::<Vec<_>>()
    })
}

pub(crate) fn emit_plan(
    app: &ExtractedApp,
    bundle_hash: &str,
    source_map_hash: &str,
) -> Result<String, Diagnostic> {
    let has_async_handlers = app.routes.iter().any(|route| route.handler.is_async);
    let emits_app_metadata =
        !app.schemas.is_empty() || !app.config_reads.is_empty() || app.uses_health;
    let route_completeness_values = app
        .routes
        .iter()
        .map(|route| {
            route_completeness(&RouteCompletenessInput {
                has_response_metadata: route.handler.response.is_some(),
                body_json_without_schema: route
                    .handler
                    .bindings
                    .iter()
                    .any(|binding| binding.kind == "body.json" && binding.schema.is_none()),
                missing_provider_registration: route.handler.effects.iter().any(|effect| {
                    !app.capabilities.iter().any(|capability| {
                        capability.token == effect.provider
                            && capability.capability_kind == effect.capability_kind
                            && capability.provider == effect.provider_kind
                    })
                }),
                runtime_only: route.handler.runtime_deferred,
            })
        })
        .collect::<Vec<_>>();
    let app_completeness = plan_completeness(&route_completeness_values);
    let emits_binding_metadata = |index: usize, route: &crate::graph::Route| {
        !route.handler.bindings.is_empty()
            || route_completeness_values[index].status.as_str() == "complete"
    };
    let emits_metadata = emits_app_metadata
        || app.routes.iter().enumerate().any(|(index, route)| {
            emits_binding_metadata(index, route)
                || route.handler.response.is_some()
                || !route.handler.responses.is_empty()
                || route.handler.runtime_deferred
                || !route.handler.effects.is_empty()
                || route.health.is_some()
                || !route.middleware.is_empty()
                || route.cors.is_some()
        });
    let handlers = app
        .routes
        .iter()
        .enumerate()
        .map(|(index, route)| {
            let id = index + 1;
            let (line, column) = line_column(&route.handler.source_text, route.handler.span.start);
            let mut handler = json!({
                "async": route.handler.is_async,
                "id": id,
                "exportName": format!("__sloppy_handler_{id}"),
                "displayName": route.name.clone().unwrap_or_else(|| format!("{} {}", route.method, route.pattern)),
                "source": {
                    "path": route.handler.source_name,
                    "line": line,
                    "column": column
                }
            });
            if route.handler.runtime_deferred {
                handler["runtimeDispatch"] = json!("deferred");
            }
            handler
        })
        .collect::<Vec<_>>();

    let routes = app
        .routes
        .iter()
        .enumerate()
        .map(|(index, route)| {
            let id = index + 1;
            let (line, column) = line_column(&route.source, route.span.start);
            let completeness = &route_completeness_values[index];
            let mut route_json = json!({
                "method": route.method,
                "pattern": route.pattern,
                "handlerId": id,
                "name": route.name,
                "source": {
                    "path": route.source_name,
                    "line": line,
                    "column": column
                }
            });
            if let Some(module) = &route.module {
                route_json["module"] = json!(module);
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
            if let Some(framework_path) = &route.framework_path {
                route_json["frameworkPath"] = json!(framework_path);
            }
            if route.handler.runtime_deferred {
                let route_params = route_parameter_names(
                    route.framework_path.as_deref().unwrap_or(&route.pattern),
                )
                .into_iter()
                .map(|name| json!({ "name": name }))
                .collect::<Vec<_>>();
                if !route_params.is_empty() {
                    route_json["routeParams"] = json!(route_params);
                }
            }
            let emits_route_metadata = emits_app_metadata
                || emits_binding_metadata(index, route)
                || route.handler.response.is_some()
                || !route.handler.responses.is_empty()
                || !route.handler.effects.is_empty()
                || route.health.is_some()
                || !route.middleware.is_empty()
                || route.cors.is_some();
            if emits_binding_metadata(index, route) {
                route_json["bindings"] = json!(route
                    .handler
                    .bindings
                    .iter()
                    .map(|binding| {
                        let mut binding_json = json!({
                            "kind": binding.kind,
                            "name": binding.name,
                            "schema": binding.schema
                        });
                        if let Some(parameter) = &binding.parameter {
                            binding_json["parameter"] = json!(parameter);
                        }
                        if let Some(type_name) = &binding.type_name {
                            binding_json["type"] = json!(type_name);
                        }
                        if let Some(wrapper) = &binding.wrapper {
                            binding_json["wrapper"] = json!(wrapper);
                        }
                        if let Some(kind) = &binding.injection_kind {
                            binding_json["injectionKind"] = json!(kind);
                        }
                        if let Some(provider_kind) = &binding.provider_kind {
                            binding_json["providerKind"] = json!(provider_kind);
                        }
                        if let Some(capability) = &binding.capability {
                            binding_json["capability"] = json!(capability);
                        }
                        if let Some(semantic) = &binding.semantic {
                            binding_json["semantic"] = json!(semantic);
                        }
                        if binding.redacted {
                            binding_json["secret"] = json!(true);
                            binding_json["redaction"] = json!("secret");
                        }
                        if let Some(span) = binding.span {
                            binding_json["source"] = source_location_json(
                                binding
                                    .source_name
                                    .as_deref()
                                    .unwrap_or(&route.handler.source_name),
                                binding
                                    .source_text
                                    .as_deref()
                                    .unwrap_or(&route.handler.source_text),
                                span,
                            );
                        }
                        binding_json
                    })
                    .collect::<Vec<_>>());
                let injections = route
                    .handler
                    .bindings
                    .iter()
                    .filter(|binding| binding.injection_kind.is_some())
                    .map(|binding| {
                        json!({
                            "kind": binding.injection_kind,
                            "providerKind": binding.provider_kind,
                            "name": binding.name,
                            "parameter": binding.parameter,
                            "capability": binding.capability
                        })
                    })
                    .collect::<Vec<_>>();
                if !injections.is_empty() {
                    route_json["injections"] = json!(injections);
                }
            }
            if emits_route_metadata {
                if let Some(response) = &route.handler.response {
                    let mut response_json = json!({
                        "helper": response.helper,
                        "status": response.status,
                        "kind": response.kind
                    });
                    if response.body_schema.is_some() {
                        response_json["bodySchema"] = json!(response.body_schema);
                    }
                    if response.partial {
                        response_json["partial"] = json!(true);
                    }
                    route_json["response"] = response_json;
                }
                if route.handler.responses.len() > 1 || route.handler.runtime_deferred {
                    route_json["responses"] = json!(route
                        .handler
                        .responses
                        .iter()
                        .map(|response| {
                            let mut value = json!({
                                "helper": response.helper,
                                "status": response.status,
                                "kind": response.kind,
                                "bodySchema": response.body_schema,
                                "partial": response.partial
                            });
                            if let (Some(source_name), Some(source_text), Some(span)) =
                                (&response.source_name, &response.source_text, response.span)
                            {
                                value["source"] =
                                    source_location_json(source_name, source_text, span);
                            }
                            value
                        })
                        .collect::<Vec<_>>());
                }
            }
            if !route.handler.effects.is_empty() {
                route_json["effects"] = json!(route
                    .handler
                    .effects
                    .iter()
                    .map(|effect| {
                        json!({
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
                    .collect::<Vec<_>>());
            }
            route_json["completeness"] = completeness_json(completeness);
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

    let source_files = app
        .source_files
        .iter()
        .map(|source_file| {
            json!({
                "path": source_file.name,
                "hash": sha256_hex(&source_file.source)
            })
        })
        .collect::<Vec<_>>();

    let data_provider_capabilities = app
        .capabilities
        .iter()
        .filter(|capability| capability.capability_kind == "database")
        .collect::<Vec<_>>();

    let data_providers = data_provider_capabilities
        .iter()
        .map(|capability| {
            let mut provider = json!({
                "token": capability.token,
                "capabilityKind": capability.capability_kind,
                "providerKind": capability.provider,
                "provider": capability.provider,
                "capability": capability.token,
                "service": null,
                "source": source_location_json(
                    &capability.source_name,
                    &capability.source,
                    capability.span
                )
            });
            if let Some(database) = &capability.database {
                provider["database"] = json!(database);
            }
            if let Some(config_key) = &capability.config_key {
                provider["configKey"] = json!(config_key);
            }
            provider
        })
        .collect::<Vec<_>>();

    let capabilities = app
        .capabilities
        .iter()
        .map(|capability| {
            let mut value = json!({
                "token": capability.token,
                "kind": capability.capability_kind,
                "access": capability.access,
                "source": source_location_json(
                    &capability.source_name,
                    &capability.source,
                    capability.span
                )
            });
            if capability.capability_kind == "database" {
                value["provider"] = json!(capability.token);
            }
            if let Some(config_key) = &capability.config_key {
                value["configKey"] = json!(config_key);
            }
            value
        })
        .collect::<Vec<_>>();

    let configuration = app.configuration.as_ref().map(|configuration| {
        let keys = configuration
            .keys
            .iter()
            .map(|key| {
                json!({
                    "key": key.key,
                    "source": key.source,
                    "value": key.value,
                    "sensitive": key.sensitive
                })
            })
            .collect::<Vec<_>>();
        let providers = configuration
            .providers
            .iter()
            .map(|provider| {
                json!({
                    "provider": provider.provider,
                    "name": provider.name,
                    "prefix": provider.prefix,
                    "source": provider.source
                })
            })
            .collect::<Vec<_>>();
        let requirements = configuration
            .requirements
            .iter()
            .map(|requirement| {
                let mut value = json!({
                    "key": requirement.key,
                    "type": requirement.value_type,
                    "required": requirement.required,
                    "status": requirement.status,
                    "source": requirement.source,
                    "requiredBy": requirement.required_by,
                    "redaction": if requirement.sensitive { "secret" } else { "none" },
                    "secret": requirement.sensitive
                });
                if let Some(default_value) = &requirement.default_value {
                    value["default"] = if requirement.sensitive {
                        json!("<redacted>")
                    } else {
                        default_value.clone()
                    };
                }
                value
            })
            .collect::<Vec<_>>();
        let package_required = configuration
            .package_manifest
            .required
            .iter()
            .map(package_manifest_entry_json)
            .collect::<Vec<_>>();
        let package_optional = configuration
            .package_manifest
            .optional
            .iter()
            .map(package_manifest_entry_json)
            .collect::<Vec<_>>();
        let mut configuration_json = json!({
            "environment": configuration.environment,
            "keys": keys,
            "providers": providers
        });
        if !requirements.is_empty() {
            configuration_json["requirements"] = json!(requirements);
        }
        if !package_required.is_empty() || !package_optional.is_empty() {
            configuration_json["packageManifest"] = json!({
                "required": package_required,
                "optional": package_optional
            });
        }
        configuration_json
    });

    let schemas = app
        .schemas
        .iter()
        .map(|schema| {
            let (line, column) = line_column(&schema.source, schema.span.start);
            json!({
                "name": schema.name,
                "definition": schema.definition,
                "source": {
                    "path": schema.source_name,
                    "line": line,
                    "column": column
                }
            })
        })
        .collect::<Vec<_>>();

    let config_reads = app
        .config_reads
        .iter()
        .map(|read| {
            let (line, column) = line_column(&read.source, read.span.start);
            json!({
                "key": read.key,
                "type": read.value_type,
                "hasDefault": read.has_default,
                "required": read.required,
                "secret": read.sensitive,
                "source": {
                    "path": read.source_name,
                    "line": line,
                    "column": column
                }
            })
        })
        .collect::<Vec<_>>();

    let mut value = json!({
        "schemaVersion": 1,
        "compilerVersion": COMPILER_VERSION,
        "runtimeMinimumVersion": RUNTIME_MINIMUM_VERSION,
        "stdlibVersion": STDLIB_VERSION,
        "target": {
            "platform": "windows-x64",
            "engine": "v8"
        },
        "bundle": {
            "path": "app.js",
            "id": "sloppyc-app-js",
            "hash": bundle_hash
        },
        "sourceMap": {
            "path": "app.js.map",
            "id": "sloppyc-app-js-map",
            "hash": source_map_hash
        },
        "handlers": handlers,
        "routes": routes,
        "modules": modules,
        "sourceFiles": source_files,
        "dataProviders": data_providers,
        "capabilities": capabilities,
        "completeness": completeness_json(&app_completeness),
        "strongPlan": {
            "version": 1,
            "profile": "compiler-30-strong-plan",
            "compatibility": {
                "schemaVersion": 1,
                "unknownOptionalFields": "ignored",
                "unknownRequiredFeatures": "rejected"
            },
            "evidence": {
                "sourceGraph": true,
                "routes": true,
                "providers": !data_providers.is_empty(),
                "bindings": app.routes.iter().any(|route| !route.handler.bindings.is_empty()),
                "effects": app.routes.iter().any(|route| !route.handler.effects.is_empty()),
                "capabilities": !app.capabilities.is_empty()
            }
        },
        "features": {
            "asyncHandlers": has_async_handlers,
            "dataProviders": !data_providers.is_empty(),
            "capabilities": !app.capabilities.is_empty(),
            "strongPlanMetadata": true,
            "sourceMaps": true
        }
    });
    let mut required_features = Vec::new();
    let mut doctor_checks = Vec::new();
    if app.uses_time_runtime {
        required_features.push("stdlib.time");
        value["strongPlan"]["evidence"]["time"] = json!(true);
        value["features"]["time"] = json!(true);
    }
    if app.uses_fs_runtime {
        required_features.push("stdlib.fs");
        value["strongPlan"]["evidence"]["filesystem"] = json!(true);
        value["features"]["fileSystem"] = json!(true);
    }
    if app.uses_crypto_runtime {
        required_features.push("stdlib.crypto");
        value["strongPlan"]["evidence"]["crypto"] = json!(true);
        value["features"]["crypto"] = json!(true);
    }
    if app.uses_codec_runtime {
        required_features.push("stdlib.codec");
        value["strongPlan"]["evidence"]["codec"] = json!(true);
        value["features"]["codec"] = json!(true);
    }
    if app.uses_crypto_runtime && app.noncrypto_hash_security_context_visible {
        value["strongPlan"]["evidence"]["nonCryptoHashSecurityContext"] = json!(true);
        doctor_checks.push(json!({
            "id": "stdlib.crypto.noncrypto_hash.security_context",
            "status": "warn",
            "message": "NonCryptoHash.xxHash64 is visible in a security-looking context; use Password, Hash, or Hmac for security or attacker-resistance"
        }));
    }
    if app.uses_codec_runtime && app.checksum_security_context_visible {
        value["strongPlan"]["evidence"]["checksumSecurityContext"] = json!(true);
        doctor_checks.push(json!({
            "id": "stdlib.codec.checksum.security_context",
            "status": "warn",
            "message": "Checksums.crc32 is visible in a security-looking context; use Hash or Hmac for security or attacker-resistance"
        }));
    }
    if app.uses_net_runtime {
        required_features.push("stdlib.net");
        value["strongPlan"]["evidence"]["network"] = json!(true);
        value["features"]["network"] = json!(true);
    }
    if app.uses_os_runtime {
        required_features.push("stdlib.os");
        value["strongPlan"]["evidence"]["os"] = json!(true);
        value["features"]["os"] = json!(true);
    }
    if app.uses_http_client_runtime {
        required_features.push("stdlib.httpclient");
        value["strongPlan"]["evidence"]["httpClient"] = json!(true);
        value["features"]["httpClient"] = json!(true);
        doctor_checks.push(json!({
            "id": "stdlib.httpclient.contract",
            "status": "warn",
            "message": "HttpClient is Plan-visible; default compiler metadata does not infer static outbound targets yet"
        }));
    }
    if app.uses_workers_runtime {
        required_features.push("stdlib.workers");
        value["strongPlan"]["evidence"]["workers"] = json!(true);
        value["features"]["workers"] = json!(true);
    }
    if app.uses_health {
        value["strongPlan"]["evidence"]["health"] = json!(true);
        value["features"]["health"] = json!(true);
    }
    if !required_features.is_empty() {
        value["requiredFeatures"] = json!(required_features);
    }
    if !doctor_checks.is_empty() {
        value["doctorChecks"] = json!(doctor_checks);
    }
    if let Some(configuration) = configuration {
        value["configuration"] = configuration;
    }
    if !schemas.is_empty() {
        value["schemas"] = json!(schemas);
    }
    if !config_reads.is_empty() {
        value["configReads"] = json!(config_reads);
    }
    if emits_metadata {
        value["features"]["metadataInference"] = json!(true);
    }

    serde_json::to_string_pretty(&value)
        .map(|json| format!("{json}\n"))
        .map_err(|error| {
            Diagnostic::new(
                "SLOPPYC_E_EMIT",
                format!("failed to emit app.plan.json: {error}"),
            )
        })
}

fn source_location_json(source_name: &str, source: &str, span: Span) -> Value {
    let (line, column) = line_column(source, span.start);
    json!({
        "path": source_name,
        "line": line,
        "column": column
    })
}

fn package_manifest_entry_json(entry: &ConfigurationPackageEntry) -> Value {
    let mut value = json!({
        "key": entry.key,
        "env": entry.env,
        "type": entry.value_type,
        "secret": entry.sensitive
    });
    if let Some(default_value) = &entry.default_value {
        value["default"] = if entry.sensitive {
            json!("<redacted>")
        } else {
            default_value.clone()
        };
    }
    value
}

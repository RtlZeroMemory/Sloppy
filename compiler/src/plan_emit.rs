//! Deterministic `app.plan.json` emission from the internal AppGraph.

use std::collections::{BTreeMap, BTreeSet};

use oxc_span::Span;
use serde_json::{json, Map, Value};

use crate::diagnostic::Diagnostic;
use crate::graph::{
    route_parameter_names, route_pattern_has_params, AuthSchemeMetadata, ConfigurationPackageEntry,
    DependencyGraph, ExtractedApp, ProjectKind, RequestBinding, ResponseMetadata,
    WebSocketOriginsMetadata, WebSocketRouteOptionsMetadata,
};
use crate::hash::sha256_hex;
use crate::route_artifact::{
    route_execution_kind, RouteDispatchArtifactMetadata, RouteExecutionKind,
};
use crate::source::line_column;
use crate::validation::{
    plan_completeness, route_completeness, Completeness, CompletenessReason, RouteCompletenessInput,
};
use crate::version::{COMPILER_VERSION, RUNTIME_MINIMUM_VERSION, STDLIB_VERSION};

mod plan_helpers;
use plan_helpers::*;
pub(crate) use plan_helpers::{
    parse_property_name, parse_reference, parse_relation_definition, relation_object_source,
    split_top_level_properties,
};

#[cfg(test)]
pub(crate) fn emit_plan(
    app: &ExtractedApp,
    bundle_hash: &str,
    source_map_hash: &str,
) -> Result<String, Diagnostic> {
    let route_artifact = crate::route_artifact::emit_route_artifact(app)?;
    let route_artifact_metadata =
        route_artifact
            .as_ref()
            .map(|bytes| RouteDispatchArtifactMetadata {
                path: crate::route_artifact::ROUTE_ARTIFACT_PATH.to_string(),
                hash: crate::hash::sha256_bytes_hex(bytes),
            });
    emit_plan_with_route_artifact(
        app,
        bundle_hash,
        source_map_hash,
        route_artifact_metadata.as_ref(),
    )
}

pub(crate) fn emit_plan_with_route_artifact(
    app: &ExtractedApp,
    bundle_hash: &str,
    source_map_hash: &str,
    route_artifact: Option<&RouteDispatchArtifactMetadata>,
) -> Result<String, Diagnostic> {
    if app.kind == ProjectKind::Program && !app.routes.is_empty() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_PROGRAM_PLAN_SHAPE",
            "program plans must not contain web routes or handlers",
        ));
    }

    let has_async_handlers = app.routes.iter().any(|route| route.handler.is_async);
    let provider_capabilities = app
        .capabilities
        .iter()
        .map(|capability| {
            (
                capability.token.as_str(),
                capability.capability_kind.as_str(),
                capability.provider.as_str(),
            )
        })
        .collect::<BTreeSet<_>>();
    let emits_app_metadata = !app.schemas.is_empty()
        || !app.config_reads.is_empty()
        || app.uses_health
        || !app.auth.schemes.is_empty()
        || !app.auth.policies.is_empty();
    let route_completeness_values = app
        .routes
        .iter()
        .map(|route| {
            route_completeness(&RouteCompletenessInput {
                has_response_metadata: route.handler.response.is_some(),
                partial_response_metadata: route
                    .handler
                    .response
                    .as_ref()
                    .is_some_and(|response| response.partial),
                body_json_without_schema: route
                    .handler
                    .bindings
                    .iter()
                    .any(|binding| binding.kind == "body.json" && binding.schema.is_none()),
                missing_provider_registration: route.handler.effects.iter().any(|effect| {
                    !provider_capabilities.contains(&(
                        effect.provider.as_str(),
                        effect.capability_kind.as_str(),
                        effect.provider_kind.as_str(),
                    ))
                }),
                runtime_only: route.handler.runtime_deferred,
                schema_metadata_conflict: route.handler.schema_metadata_conflict,
            })
        })
        .collect::<Vec<_>>();
    let mut all_route_completeness_values = route_completeness_values.clone();
    all_route_completeness_values.extend(app.dynamic_routes.iter().map(|route| {
        Completeness::dynamic(vec![CompletenessReason::new("dynamic-route", route.reason)])
    }));
    let app_completeness = if app.kind == ProjectKind::Program {
        Completeness::opaque(vec![CompletenessReason::new(
            "program-mode",
            "program mode does not require static web route metadata",
        )])
    } else {
        plan_completeness(&all_route_completeness_values)
    };
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
                || route.auth.is_some()
                || route.realtime.is_some()
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

    let schema_definitions = schema_definition_map(app);
    let mut schema_reference_resolution = SchemaReferenceResolution::default();
    let resolved_schema_definitions = app
        .schemas
        .iter()
        .map(|schema| {
            let mut stack = BTreeSet::from([schema.name.clone()]);
            let definition = resolve_schema_references(
                &schema.name,
                &schema.definition,
                &schema_definitions,
                &mut stack,
                &mut schema_reference_resolution,
            );
            (schema.name.clone(), definition)
        })
        .collect::<BTreeMap<_, _>>();

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
            if let Some(module) = &route.module {
                route_json["module"] = json!(module);
            }
            if !route.tags.is_empty() {
                route_json["tags"] = json!(route.tags);
            }
            if let Some(summary) = &route.summary {
                route_json["summary"] = json!(summary);
            }
            if let Some(description) = &route.description {
                route_json["description"] = json!(description);
            }
            if let Some(deprecated) = &route.deprecated {
                route_json["deprecated"] = json!(deprecated != "false");
                if deprecated != "true" && deprecated != "false" {
                    route_json["deprecatedReason"] = json!(deprecated);
                }
            }
            if !route.consumes.is_empty() {
                route_json["consumes"] = json!(route.consumes);
            }
            if !route.produces.is_empty() {
                route_json["produces"] = json!(route.produces);
            }
            if !route.headers.is_empty() {
                route_json["headers"] = json!(route
                    .headers
                    .iter()
                    .map(|header| {
                        json!({
                            "name": header.name,
                            "schema": header.schema,
                            "required": header.required,
                            "description": header.description
                        })
                    })
                    .collect::<Vec<_>>());
            }
            if let Some(schema) = &route.query_schema {
                route_json["querySchema"] = json!(schema);
            }
            if let Some(schema) = &route.params_schema {
                route_json["paramsSchema"] = json!(schema);
            }
            if let Some(override_value) = &route.openapi_override {
                route_json["openapi"] = override_value.clone();
            }
            if let Some(output_cache) = &route.output_cache {
                route_json["outputCache"] = output_cache.clone();
            }
            if let Some(cache_headers) = &route.cache_headers {
                route_json["cacheHeaders"] = cache_headers.clone();
            }
            if !route.rate_limits.is_empty() {
                route_json["rateLimit"] = json!(route
                    .rate_limits
                    .iter()
                    .map(|policy| {
                        json!({
                            "name": policy.name,
                            "algorithm": policy.algorithm,
                            "store": policy.store,
                            "partition": policy.partition,
                            "partial": policy.partial
                        })
                    })
                    .collect::<Vec<_>>());
            }
            if let Some(docs) = &route.docs {
                route_json["docsInternal"] = json!(true);
                route_json["docs"] = json!({
                    "kind": docs.kind,
                    "strict": docs.strict
                });
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
            if let Some(auth) = &route.auth {
                route_json["auth"] = json!({
                    "required": auth.required,
                    "allowAnonymous": auth.allow_anonymous,
                    "schemes": auth.schemes,
                    "scopes": auth.scopes,
                    "roles": auth.roles,
                    "claims": auth.claims,
                    "policy": auth.policy
                });
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
                || route.auth.is_some()
                || route.websocket.is_some()
                || route.realtime.is_some()
                || route.cors.is_some()
                || route.summary.is_some()
                || route.description.is_some()
                || route.deprecated.is_some()
                || !route.consumes.is_empty()
                || !route.produces.is_empty()
                || !route.headers.is_empty()
                || route.query_schema.is_some()
                || route.params_schema.is_some()
                || route.openapi_override.is_some()
                || !route.rate_limits.is_empty()
                || route.docs.is_some();
            let response_kind = route
                .handler
                .response
                .as_ref()
                .map(|response| response.kind.as_str());
            let native_body = route
                .handler
                .response
                .as_ref()
                .and_then(|response| response.native_body.as_deref());
            let execution_kind =
                route_execution_kind(response_kind, native_body, !route.handler.effects.is_empty());
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
                    if let Some(native_body) = &response.native_body {
                        response_json["nativeBody"] = json!(native_body);
                        if execution_kind != RouteExecutionKind::V8Handler {
                            route_json["nativeResponse"] = json!({
                                "kind": response.kind,
                                "status": response.status,
                                "body": native_body,
                                "contentType": match response.kind.as_str() {
                                    "json" => "application/json",
                                    "problem" => "application/problem+json",
                                    "empty" => "text/plain; charset=utf-8",
                                    _ => "text/plain; charset=utf-8",
                                }
                            });
                        }
                    }
                    route_json["response"] = response_json;
                }
                route_json["jsonRequest"] = route_json_request_plan(&route.handler.bindings);
                route_json["jsonResponse"] = route_json_response_plan(
                    route.handler.response.as_ref(),
                    &route.handler.responses,
                    &resolved_schema_definitions,
                    execution_kind != RouteExecutionKind::V8Handler,
                );
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
                                "nativeBody": response.native_body,
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
            route_json["dispatch"] = json!({
                "endpointId": id,
                "mode": if route_artifact.is_some() { "native-compiled" } else { "native-compiled-in-memory" },
                "strategy": if route_pattern_has_params(&route.pattern) {
                    "segment-trie"
                } else {
                    "exact-static-hash"
                },
                "specializable": true,
                "executionKind": execution_kind.as_plan_str()
            });
            route_json["completeness"] = completeness_json(completeness);
            route_json
        })
        .collect::<Vec<_>>();

    let dynamic_routes = app
        .dynamic_routes
        .iter()
        .map(|route| {
            let (line, column) = line_column(&route.source, route.span.start);
            json!({
                "kind": "dynamicRoute",
                "method": {
                    "known": route.method.is_some(),
                    "value": route.method
                },
                "pattern": {
                    "known": route.pattern.is_some(),
                    "value": route.pattern,
                    "reason": route.pattern_reason
                },
                "handler": {
                    "known": route.handler_known
                },
                "source": {
                    "path": route.source_name,
                    "line": line,
                    "column": column
                },
                "metadata": {
                    "completeness": "dynamic",
                    "unknown": if route.pattern.is_some() {
                        json!(["response"])
                    } else {
                        json!(["pattern", "response"])
                    },
                    "reason": route.reason
                }
            })
        })
        .collect::<Vec<_>>();

    let dynamic_findings = app
        .dynamic_routes
        .iter()
        .map(|route| {
            let (line, column) = line_column(&route.source, route.span.start);
            json!({
                "code": "SLOPPYC_W_DYNAMIC_ROUTE",
                "severity": "warning",
                "message": route.reason,
                "source": {
                    "path": route.source_name,
                    "line": line,
                    "column": column
                },
                "hint": "The app can build, but native dispatch and OpenAPI only use statically known route metadata."
            })
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
            let definition = resolved_schema_definitions
                .get(&schema.name)
                .cloned()
                .unwrap_or_else(|| schema.definition.clone());
            json!({
                "name": schema.name,
                "definition": definition,
                "source": {
                    "path": schema.source_name,
                    "line": line,
                    "column": column
                }
            })
        })
        .collect::<Vec<_>>();

    let ffi_libraries = app
        .ffi
        .iter()
        .map(|library| {
            json!({
                "name": library.name,
                "convention": library.convention,
                "source": source_location_json(&library.source_name, &library.source, library.span),
                "functions": library
                    .functions
                    .iter()
                    .map(|function| {
                        let mut value = json!({
                            "id": function.id,
                            "name": function.name,
                            "symbol": function.symbol,
                            "convention": function.convention,
                            "return": function.return_type,
                            "parameters": function.parameters,
                            "marshaling": {
                                "arguments": ffi_argument_marshaling(&function.parameters),
                                "return": "direct"
                            },
                            "safety": "unsafe",
                            "source": source_location_json(
                                &function.source_name,
                                &function.source,
                                function.span
                            )
                        });
                        if let Some(descriptor) = &function.return_descriptor {
                            value["returnDescriptor"] = descriptor.clone();
                        }
                        if function
                            .parameter_descriptors
                            .iter()
                            .any(|descriptor| !descriptor.is_null())
                        {
                            value["parameterDescriptors"] = json!(function.parameter_descriptors);
                        }
                        if let Some(dispose) = &function.dispose {
                            value["dispose"] = json!(dispose);
                        }
                        value
                    })
                    .collect::<Vec<_>>()
            })
        })
        .collect::<Vec<_>>();

    let ffi_structs = app
        .ffi_structs
        .iter()
        .map(|layout| {
            let mut value = json!({
                "name": layout.name,
                "layout": layout.layout,
                "fields": layout
                    .fields
                    .iter()
                    .map(|field| {
                        let mut value = json!({
                            "name": field.name,
                            "type": field.type_name
                        });
                        if let Some(descriptor) = &field.descriptor {
                            value["descriptor"] = descriptor.clone();
                        }
                        value
                    })
                    .collect::<Vec<_>>(),
                "source": source_location_json(&layout.source_name, &layout.source, layout.span)
            });
            if let Some(pack) = layout.pack {
                value["pack"] = json!(pack);
            }
            value
        })
        .collect::<Vec<_>>();

    let ffi_handles = app
        .ffi_handles
        .iter()
        .map(|handle| {
            json!({
                "name": handle.name,
                "type": "ptr",
                "ownership": "typed-explicit",
                "source": source_location_json(&handle.source_name, &handle.source, handle.span)
            })
        })
        .collect::<Vec<_>>();
    let ffi_callbacks = app
        .ffi_callbacks
        .iter()
        .map(|callback| {
            json!({
                "id": callback.id,
                "return": callback.return_type,
                "parameters": callback.parameters,
                "thread": callback.thread,
                "lifetime": "explicit-dispose",
                "source": source_location_json(&callback.source_name, &callback.source, callback.span)
            })
        })
        .collect::<Vec<_>>();
    let ffi_dispatch_tables = app
        .ffi_dispatch_tables
        .iter()
        .map(|table| {
            json!({
                "name": table.name,
                "resolver": table.resolver,
                "symbols": table.symbols.iter().map(|symbol| {
                    let mut value = json!({
                        "id": symbol.id,
                        "name": symbol.name,
                        "symbol": symbol.symbol,
                        "convention": symbol.convention,
                        "return": symbol.return_type,
                        "parameters": symbol.parameters,
                        "marshaling": {
                            "arguments": ffi_argument_marshaling(&symbol.parameters),
                            "return": "direct"
                        },
                        "safety": "unsafe",
                        "source": source_location_json(
                            &symbol.source_name,
                            &symbol.source,
                            symbol.span
                        )
                    });
                    if let Some(descriptor) = &symbol.return_descriptor {
                        value["returnDescriptor"] = descriptor.clone();
                    }
                    if symbol
                        .parameter_descriptors
                        .iter()
                        .any(|descriptor| !descriptor.is_null())
                    {
                        value["parameterDescriptors"] = json!(symbol.parameter_descriptors);
                    }
                    if let Some(dispose) = &symbol.dispose {
                        value["dispose"] = json!(dispose);
                    }
                    value
                }).collect::<Vec<_>>(),
                "source": source_location_json(&table.source_name, &table.source, table.span)
            })
        })
        .collect::<Vec<_>>();
    let ffi_adoptions = app
        .ffi_adoptions
        .iter()
        .map(|adoption| {
            let mut value = json!({
                "handle": adoption.handle,
                "ownership": adoption.ownership,
                "source": source_location_json(
                    &adoption.source_name,
                    &adoption.source,
                    adoption.span
                )
            });
            if let Some(disposer) = &adoption.disposer {
                value["disposer"] = json!(disposer);
            }
            value
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

    let auth_schemes = app
        .auth
        .schemes
        .iter()
        .map(|scheme| match scheme {
            AuthSchemeMetadata::JwtBearer {
                name,
                issuer,
                audience,
                clock_skew_seconds,
                secret_config_key,
            } => json!({
                "kind": "jwtBearer",
                "name": name,
                "scheme": "bearer",
                "bearerFormat": "JWT",
                "algorithm": "HS256",
                "issuer": issuer,
                "audience": audience,
                "clockSkewSeconds": clock_skew_seconds,
                "secretConfigKey": secret_config_key,
                "secret": "<redacted>"
            }),
            AuthSchemeMetadata::ApiKey {
                name,
                header,
                config_key,
            } => json!({
                "kind": "apiKey",
                "name": name,
                "in": "header",
                "header": header,
                "configKey": config_key,
                "secret": "<redacted>"
            }),
            AuthSchemeMetadata::CookieSession {
                name,
                cookie,
                secure,
                http_only,
                same_site,
                path,
                max_age_seconds,
                store,
                idle_timeout_ms,
                absolute_timeout_ms,
                rotation,
                csrf,
                secret_config_key,
            } => json!({
                "kind": "cookieSession",
                "name": name,
                "in": "cookie",
                "cookie": cookie,
                "secure": secure,
                "httpOnly": http_only,
                "sameSite": same_site,
                "path": path,
                "maxAgeSeconds": max_age_seconds,
                "store": store,
                "idleTimeoutMs": idle_timeout_ms,
                "absoluteTimeoutMs": absolute_timeout_ms,
                "rotation": rotation,
                "csrf": csrf,
                "configKey": secret_config_key,
                "secret": "<redacted>"
            }),
        })
        .collect::<Vec<_>>();
    let auth_policies = app
        .auth
        .policies
        .iter()
        .map(|policy| policy.name.clone())
        .collect::<Vec<_>>();

    let dependency_graph = if app.dependency_graph.has_entries() {
        Some((
            dependency_graph_json(&app.dependency_graph),
            emit_dependency_graph(&app.dependency_graph)?,
        ))
    } else {
        None
    };

    let mut value = json!({
        "schemaVersion": 1,
        "kind": app.kind.as_str(),
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
            "capabilities": !app.capabilities.is_empty(),
            "ffi": !ffi_libraries.is_empty() || !ffi_structs.is_empty() || !ffi_handles.is_empty() || !ffi_callbacks.is_empty() || !ffi_dispatch_tables.is_empty() || !ffi_adoptions.is_empty()
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
    if !dynamic_routes.is_empty() {
        value["dynamicRoutes"] = json!(dynamic_routes);
    }
    if !dynamic_findings.is_empty() {
        value["findings"] = json!(dynamic_findings);
    }
    if app.kind == ProjectKind::Web {
        value["routeDispatch"] = route_dispatch_json(
            app,
            &route_completeness_values,
            route_artifact,
            &resolved_schema_definitions,
        );
        value["features"]["nativeEndpointDispatch"] = json!(true);
        value["features"]["nativeJsonDispatch"] = json!(true);
        value["strongPlan"]["evidence"]["routeDispatch"] = json!(true);
        value["strongPlan"]["evidence"]["nativeJsonDispatch"] = json!(true);
    }
    if app.kind == ProjectKind::Web && !app.dynamic_routes.is_empty() {
        let complete_count = route_completeness_values
            .iter()
            .filter(|route| route.status.as_str() == "complete")
            .count();
        let partial_count = route_completeness_values
            .iter()
            .filter(|route| route.status.as_str() == "partial")
            .count();
        value["metadata"] = json!({
            "completeness": completeness_json(&app_completeness),
            "routes": {
                "complete": complete_count,
                "partial": partial_count,
                "dynamic": app.dynamic_routes.len()
            }
        });
    }
    if app.kind == ProjectKind::Program {
        value["metadata"] = json!({
            "completeness": completeness_json(&app_completeness),
            "program": {
                "entry": app.program_entry
            }
        });
        value["strongPlan"]["evidence"]["routes"] = json!(false);
        value["strongPlan"]["evidence"]["program"] = json!(true);
        value["features"]["program"] = json!(true);
    }
    if let Some((graph_json, graph_text)) = &dependency_graph {
        value["dependencyGraph"] = graph_json.clone();
        value["dependencyGraphArtifact"] = json!({
            "path": "deps.graph.json",
            "id": "sloppyc-deps-graph",
            "hash": sha256_hex(graph_text)
        });
        value["features"]["dependencyGraph"] = json!(true);
        value["strongPlan"]["evidence"]["dependencyGraph"] = json!(true);
    }

    let mut required_features = Vec::new();
    let mut doctor_checks = Vec::new();
    doctor_checks.extend(schema_reference_resolution.doctor_checks());
    let health_endpoints = app
        .routes
        .iter()
        .filter_map(|route| {
            route.health.as_ref().map(|health| {
                json!({
                    "method": route.method,
                    "path": route.pattern,
                    "name": route.name,
                    "kind": health.kind,
                    "checks": health.checks
                })
            })
        })
        .collect::<Vec<_>>();
    let management_endpoints = app
        .routes
        .iter()
        .filter(|route| {
            route
                .name
                .as_deref()
                .is_some_and(|name| name.starts_with("Management."))
        })
        .map(|route| {
            json!({
                "method": route.method,
                "path": route.pattern,
                "name": route.name
            })
        })
        .collect::<Vec<_>>();
    if !health_endpoints.is_empty() || !management_endpoints.is_empty() {
        value["ops"] = json!({
            "health": {
                "enabled": !health_endpoints.is_empty(),
                "endpoints": health_endpoints
            },
            "management": {
                "enabled": !management_endpoints.is_empty(),
                "endpoints": management_endpoints,
                "requiresProtection": !management_endpoints.is_empty()
            },
            "metrics": {
                "prometheus": app.routes.iter().any(|route| route.name.as_deref() == Some("Management.Metrics")),
                "json": app.routes.iter().any(|route| route.name.as_deref() == Some("Management.MetricsJson"))
            }
        });
        value["strongPlan"]["evidence"]["ops"] = json!(true);
        value["features"]["ops"] = json!(true);
    }
    if app.routes.iter().any(|route| {
        route
            .name
            .as_deref()
            .is_some_and(|name| name.starts_with("Management."))
    }) {
        value["features"]["management"] = json!(true);
        doctor_checks.push(json!({
            "id": "ops.management.protection",
            "status": "warn",
            "message": "management endpoints are exposed; protect detailed health, metrics, info, and runtime routes before exposing them outside a trusted network"
        }));
    }
    if app
        .routes
        .iter()
        .any(|route| route.name.as_deref() == Some("Management.Metrics"))
    {
        value["features"]["metrics"] = json!(true);
        doctor_checks.push(json!({
            "id": "ops.metrics.prometheus",
            "status": "info",
            "message": "Prometheus metrics endpoint is Plan-visible and package-safe"
        }));
    }
    if app.kind == ProjectKind::Program {
        doctor_checks.push(json!({
            "id": "program.metadata",
            "status": "info",
            "message": "program Plan has no route metadata by design"
        }));
    }
    if app.uses_time_runtime {
        required_features.push("stdlib.time".to_string());
        value["strongPlan"]["evidence"]["time"] = json!(true);
        value["features"]["time"] = json!(true);
    }
    if app.uses_fs_runtime {
        required_features.push("stdlib.fs".to_string());
        value["strongPlan"]["evidence"]["filesystem"] = json!(true);
        value["features"]["fileSystem"] = json!(true);
    }
    if app.uses_crypto_runtime {
        required_features.push("stdlib.crypto".to_string());
        value["strongPlan"]["evidence"]["crypto"] = json!(true);
        value["features"]["crypto"] = json!(true);
    }
    if app.uses_codec_runtime {
        required_features.push("stdlib.codec".to_string());
        value["strongPlan"]["evidence"]["codec"] = json!(true);
        value["features"]["codec"] = json!(true);
    }
    let output_cache_routes = app
        .routes
        .iter()
        .filter(|route| route.output_cache.is_some())
        .map(|route| {
            json!({
                "method": route.method,
                "pattern": route.pattern,
                "cache": route.output_cache
            })
        })
        .collect::<Vec<_>>();
    if app.uses_cache_runtime || !output_cache_routes.is_empty() {
        required_features.push("stdlib.cache".to_string());
        value["strongPlan"]["evidence"]["cache"] = json!(true);
        value["features"]["cache"] = json!(true);
        value["cache"] = json!({
            "enabled": true,
            "staticMetadata": if output_cache_routes.is_empty() { "import-visible" } else { "route-visible" },
            "outputCacheRoutes": output_cache_routes
        });
        doctor_checks.push(json!({
            "id": "stdlib.cache.contract",
            "status": "info",
            "message": "Cache is Plan-visible; output cache route metadata is emitted when statically available"
        }));
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
    if app.uses_cache_runtime {
        required_features.push("stdlib.cache".to_string());
        value["strongPlan"]["evidence"]["cache"] = json!(true);
        value["features"]["cache"] = json!(true);
    }
    if app.uses_redis_runtime {
        required_features.push("stdlib.redis".to_string());
        required_features.push("stdlib.net".to_string());
        value["strongPlan"]["evidence"]["redis"] = json!(true);
        value["features"]["redis"] = json!(true);
        value["redis"] = json!({
            "enabled": true,
            "clients": []
        });
        doctor_checks.push(json!({
            "id": "stdlib.redis.contract",
            "status": "warn",
            "message": "Redis is Plan-visible; verify URL, timeout, pool, and health configuration at runtime without exposing secrets"
        }));
    }
    if app.uses_net_runtime {
        required_features.push("stdlib.net".to_string());
        value["strongPlan"]["evidence"]["network"] = json!(true);
        value["features"]["network"] = json!(true);
    }
    if app.uses_os_runtime {
        required_features.push("stdlib.os".to_string());
        value["strongPlan"]["evidence"]["os"] = json!(true);
        value["features"]["os"] = json!(true);
    }
    if app.uses_http_client_runtime {
        required_features.push("stdlib.httpclient".to_string());
        value["strongPlan"]["evidence"]["httpClient"] = json!(true);
        value["features"]["httpClient"] = json!(true);
        let http_clients = http_clients_from_sources(app);
        if !http_clients.is_empty() {
            let static_http_clients = http_clients
                .iter()
                .all(|client| client["target"] == "static");
            value["httpClients"] = json!(http_clients);
            value["strongPlan"]["evidence"]["httpClients"] = json!(static_http_clients);
        }
        doctor_checks.push(json!({
            "id": "stdlib.httpclient.contract",
            "status": "ok",
            "message": "HttpClient feature metadata is Plan-visible"
        }));
        if value["httpClients"]
            .as_array()
            .is_some_and(|clients| clients.iter().any(|client| client["target"] == "static"))
        {
            doctor_checks.push(json!({
                "id": "stdlib.httpclient.static-targets",
                "status": "ok",
                "message": "static outbound HTTP client metadata is visible without secret values"
            }));
        }
        if value["httpClients"]
            .as_array()
            .is_some_and(|clients| clients.iter().any(|client| client["target"] == "dynamic"))
        {
            doctor_checks.push(json!({
                "id": "stdlib.httpclient.dynamic-targets",
                "status": "warn",
                "message": "dynamic outbound HTTP client metadata is partial and must be checked at runtime"
            }));
        }
    }
    if app.uses_orm_runtime {
        let orm_tables = app.orm_tables.clone();
        let orm_relations = app.orm_relations.clone();
        let (extraction_status, extraction_reason, doctor_message) = if app.orm_extraction_partial {
            (
                "partial",
                "runtime ORM is available; dynamic table or relation shapes compile and run while static metadata remains partial",
                "sloppy/orm is Plan-visible; runtime ORM works dynamically while some table or relation metadata is partial",
            )
        } else if !orm_tables.is_empty() || !orm_relations.is_empty() {
            (
                "static",
                "runtime ORM is available; static table and relation metadata was extracted from AST call expressions",
                "sloppy/orm is Plan-visible; static table and relation metadata was extracted from AST call expressions",
            )
        } else {
            (
                "runtime-dynamic",
                "runtime ORM is available; no static table or relation metadata was extracted, so ORM shape metadata remains runtime-dynamic",
                "sloppy/orm is Plan-visible; runtime ORM works dynamically and no static table or relation metadata was extracted",
            )
        };
        value["strongPlan"]["evidence"]["orm"] = json!(
            !app.orm_extraction_partial && (!orm_tables.is_empty() || !orm_relations.is_empty())
        );
        value["features"]["orm"] = json!(true);
        value["orm"] = json!({
            "mode": "runtime-dynamic",
            "tables": orm_tables,
            "relations": orm_relations,
            "migrationSnapshots": [],
            "extraction": {
                "status": extraction_status,
                "reason": extraction_reason
            }
        });
        doctor_checks.push(json!({
            "id": "stdlib.orm.dynamic_metadata",
            "status": "info",
            "message": doctor_message
        }));
    }
    if app.uses_workers_runtime {
        required_features.push("stdlib.workers".to_string());
        value["strongPlan"]["evidence"]["workers"] = json!(true);
        value["features"]["workers"] = json!(true);
    }
    if app.uses_webhooks_runtime {
        required_features.push("stdlib.webhooks".to_string());
        value["strongPlan"]["evidence"]["webhooks"] = json!(true);
        value["features"]["webhooks"] = json!(true);
        value["webhooks"] = json!({
            "enabled": true,
            "events": [],
            "outbox": {
                "provider": "dynamic"
            }
        });
        doctor_checks.push(json!({
            "id": "stdlib.webhooks.contract",
            "status": "warn",
            "message": "Webhook metadata is Plan-visible; verify signing secret, delivery worker, retry, dead-letter, and private-network policy in app configuration"
        }));
    }
    if app.uses_ffi_runtime
        || !ffi_libraries.is_empty()
        || !ffi_structs.is_empty()
        || !ffi_handles.is_empty()
        || !ffi_callbacks.is_empty()
        || !ffi_dispatch_tables.is_empty()
        || !ffi_adoptions.is_empty()
    {
        required_features.push("stdlib.ffi".to_string());
        value["strongPlan"]["evidence"]["ffi"] = json!(true);
        value["features"]["ffi"] = json!(true);
        value["native"] = json!({
            "ffi": ffi_libraries,
            "ffiStructs": ffi_structs,
            "ffiHandles": ffi_handles,
            "ffiCallbacks": ffi_callbacks,
            "ffiDispatchTables": ffi_dispatch_tables,
            "ffiAdoptions": ffi_adoptions
        });
        doctor_checks.push(json!({
            "id": "stdlib.ffi.unsafe_boundary",
            "status": "warn",
            "message": "unsafeFfi is Plan-visible; native libraries execute in-process and must be trusted"
        }));
    }
    if app.uses_health {
        value["strongPlan"]["evidence"]["health"] = json!(true);
        value["features"]["health"] = json!(true);
    }
    if app.uses_realtime_runtime {
        required_features.push("runtime.realtime".to_string());
        value["strongPlan"]["evidence"]["realtime"] = json!(true);
        value["features"]["realtime"] = json!(true);
        doctor_checks.push(json!({
            "id": "app.realtime.metadata",
            "status": "warn",
            "message": "realtime route metadata is Plan-visible; WebSocket upgrade execution is partial in this alpha"
        }));
    }
    if !auth_schemes.is_empty() || !auth_policies.is_empty() {
        value["auth"] = json!({
            "schemes": auth_schemes,
            "policies": auth_policies
        });
        value["strongPlan"]["evidence"]["auth"] = json!(true);
        value["features"]["auth"] = json!(true);
        doctor_checks.push(json!({
            "id": "app.auth.metadata",
            "status": "info",
            "message": "auth schemes and route authorization requirements are Plan-visible without secret values"
        }));
    }
    if app.dependency_graph.has_entries() {
        for builtin in &app.dependency_graph.node_builtins {
            if builtin.status != "unsupported" {
                let feature_name = match builtin.specifier.as_str() {
                    "node:assert/strict" => "assert".to_string(),
                    "node:stream/promises" => "stream".to_string(),
                    _ => builtin
                        .specifier
                        .strip_prefix("node:")
                        .unwrap_or(&builtin.specifier)
                        .replace('/', "."),
                };
                required_features.push(format!("node.compat.{feature_name}"));
            }
        }
        doctor_checks.push(json!({
            "id": "dependency.graph",
            "status": "info",
            "message": format!(
                "Dependency graph emitted with {} package(s), {} module(s), and {} Node compatibility binding(s).",
                app.dependency_graph.packages.len(),
                app.dependency_graph.modules.len(),
                app.dependency_graph.node_builtins.len()
            )
        }));
        for finding in &app.dependency_graph.compatibility_findings {
            doctor_checks.push(json!({
                "id": finding.code,
                "status": if finding.severity == "error" { "error" } else { "warn" },
                "message": finding.message,
                "package": finding.package,
                "specifier": finding.specifier,
                "hint": finding.hint
            }));
        }
        if !doctor_checks.is_empty() {
            value["doctorChecks"] = json!(doctor_checks);
        }
    }
    if !required_features.is_empty() {
        required_features.sort();
        required_features.dedup();
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

mod dependency_graph;
use dependency_graph::*;
pub(crate) use dependency_graph::{dependency_graph_json, emit_dependency_graph};

#[cfg(test)]
mod tests {
    use super::*;

    fn json_response(schema: Option<&str>) -> ResponseMetadata {
        ResponseMetadata {
            helper: "ok".to_string(),
            status: 200,
            kind: "json".to_string(),
            body_schema: schema.map(str::to_string),
            native_body: None,
            source_name: None,
            source_text: None,
            span: None,
            partial: false,
        }
    }

    fn text_response() -> ResponseMetadata {
        ResponseMetadata {
            helper: "text".to_string(),
            status: 200,
            kind: "text".to_string(),
            body_schema: None,
            native_body: None,
            source_name: None,
            source_text: None,
            span: None,
            partial: false,
        }
    }

    #[test]
    fn object_schema_without_properties_is_an_explicit_response_fallback() {
        let schema = json!({ "kind": "object" });

        assert_eq!(
            schema_response_native_fallback_reason(&schema),
            Some("object-properties-missing")
        );
    }

    #[test]
    fn route_json_response_plan_considers_all_declared_json_responses() {
        let primary = json_response(Some("UserDto"));
        let responses = vec![primary.clone(), json_response(Some("OrderDto"))];
        let mut resolved = BTreeMap::new();
        resolved.insert(
            "UserDto".to_string(),
            json!({ "kind": "object", "properties": { "id": { "kind": "int" } } }),
        );
        resolved.insert(
            "OrderDto".to_string(),
            json!({ "kind": "object", "properties": { "id": { "kind": "int" } } }),
        );

        let plan = route_json_response_plan(Some(&primary), &responses, &resolved, true);

        assert_eq!(plan["mode"], "fallback");
        assert_eq!(plan["writer"], "none");
        assert_eq!(plan["fallbackReason"], "multiple-json-response-schemas");
    }

    #[test]
    fn route_json_response_plan_uses_declared_json_responses_when_primary_is_non_json() {
        let primary = text_response();
        let responses = vec![primary.clone(), json_response(Some("UserDto"))];
        let mut resolved = BTreeMap::new();
        resolved.insert(
            "UserDto".to_string(),
            json!({ "kind": "object", "properties": { "id": { "kind": "int" } } }),
        );

        let plan = route_json_response_plan(Some(&primary), &responses, &resolved, true);

        assert_eq!(plan["mode"], "native-schema");
        assert_eq!(plan["writer"], "bounded");
        assert_eq!(plan["schema"], "UserDto");
    }
}

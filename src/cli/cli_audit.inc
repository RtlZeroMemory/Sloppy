/*
 * src/cli/cli_audit.inc
 *
 * Private audit command emitters.
 * Included only by src/main.c to preserve existing static helper visibility.
 */
static void sl_cli_audit_text(SlCliSpan severity, const char* code, const char* message,
                              const char* path)
{
    (void)printf("[%.*s] %s %s (%s)\n", (int)severity.length, severity.ptr, code, message, path);
}

static void sl_cli_audit_json(SlCliSpan severity, const char* code, const char* message,
                              const char* path, bool comma)
{
    (void)printf("%s\n    { \"severity\": ", comma ? "," : "");
    sl_cli_json_escape(stdout, severity);
    (void)printf(", \"code\": \"%s\", \"message\": ", code);
    sl_cli_json_escape(stdout, sl_cli_span_cstr(message));
    (void)printf(", \"path\": \"%s\" }", path);
}

static void sl_cli_audit_emit(const SlCliOptions* options, const char* severity, const char* code,
                              const char* message, const char* path, size_t* findings,
                              size_t* errors)
{
    if (options->format == SL_CLI_FORMAT_JSON) {
        sl_cli_audit_json(sl_cli_span_cstr(severity), code, message, path, *findings > 0U);
    }
    else {
        sl_cli_audit_text(sl_cli_span_cstr(severity), code, message, path);
    }
    *findings += 1U;
    if (errors != NULL && strcmp(severity, "error") == 0) {
        *errors += 1U;
    }
}

static void sl_cli_audit_routes(const SlCliOptions* options, const SlCliMetadata* metadata,
                                size_t* findings, size_t* errors)
{
    size_t outer = 0U;
    size_t inner = 0U;

    for (outer = 0U; outer < metadata->route_count; outer += 1U) {
        if (sl_cli_find_handler(metadata, metadata->routes[outer].handler_id) == NULL) {
            sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_MISSING_HANDLER",
                              "route references a missing handler id", "routes", findings, errors);
        }
        if (!sl_cli_span_empty(metadata->routes[outer].capability) &&
            sl_cli_find_capability(metadata, metadata->routes[outer].capability) == NULL)
        {
            sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_ROUTE_CAPABILITY_MISSING",
                              "route references an undeclared capability", "routes", findings,
                              errors);
        }
        for (inner = outer + 1U; inner < metadata->route_count; inner += 1U) {
            if (!sl_cli_span_empty(metadata->routes[outer].name) &&
                sl_cli_span_equal(metadata->routes[outer].name, metadata->routes[inner].name))
            {
                sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_DUPLICATE_ROUTE_NAME",
                                  "duplicate route name", "routes", findings, errors);
            }
            if (sl_cli_span_equal(metadata->routes[outer].method, metadata->routes[inner].method) &&
                sl_cli_span_equal(metadata->routes[outer].pattern, metadata->routes[inner].pattern))
            {
                sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_DUPLICATE_ROUTE",
                                  "duplicate route method and pattern", "routes", findings, errors);
            }
        }
        if (!sl_cli_span_empty(metadata->routes[outer].completeness) &&
            !sl_cli_span_equal_cstr(metadata->routes[outer].completeness, "complete"))
        {
            sl_cli_audit_emit(
                options,
                sl_cli_span_equal_cstr(metadata->routes[outer].completeness, "invalid") ? "error"
                                                                                        : "warn",
                "SLOPPY_AUDIT_ROUTE_COMPLETENESS", "route Plan completeness is not complete",
                "routes", findings, errors);
        }
        if (metadata->routes[outer].binding_count > 0U) {
            size_t binding = 0U;
            for (binding = 0U; binding < metadata->routes[outer].binding_count; binding += 1U) {
                if (sl_cli_span_equal_cstr(metadata->routes[outer].binding_kinds[binding],
                                           "body.json") &&
                    sl_cli_span_empty(metadata->routes[outer].binding_schemas[binding]))
                {
                    sl_cli_audit_emit(options, "warn", "SLOPPY_AUDIT_BODY_SCHEMA_MISSING",
                                      "JSON body binding has no schema metadata", "validation",
                                      findings, errors);
                }
            }
        }
        if (!sl_cli_span_empty(metadata->routes[outer].completeness) &&
            sl_cli_span_empty(metadata->routes[outer].response_kind))
        {
            sl_cli_audit_emit(options, "warn", "SLOPPY_AUDIT_RESPONSE_UNKNOWN",
                              "route response metadata is unknown", "response", findings, errors);
        }
        if (sl_cli_span_equal_cstr(metadata->routes[outer].response_kind, "json")) {
            sl_cli_audit_emit(
                options, "note", "SLOPPY_AUDIT_OPT_NATIVE_JSON_CANDIDATE",
                "route response metadata is available for future native JSON analysis",
                "optimization", findings, errors);
        }
        if (metadata->routes[outer].effect_count > 0U) {
            sl_cli_audit_emit(
                options, "note", "SLOPPY_AUDIT_OPT_PROVIDER_ROUTE_CANDIDATE",
                "route provider/effect metadata is available for future route analysis",
                "optimization", findings, errors);
        }
    }
}

static void sl_cli_audit_modules(const SlCliOptions* options, const SlCliMetadata* metadata,
                                 size_t* findings, size_t* errors)
{
    size_t outer = 0U;
    size_t inner = 0U;

    for (outer = 0U; outer < metadata->module_count; outer += 1U) {
        for (inner = 0U; inner < metadata->modules[outer].dependency_count; inner += 1U) {
            const SlCliModule* dep =
                sl_cli_find_module(metadata, metadata->modules[outer].dependencies[inner]);
            if (dep == NULL) {
                sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_MISSING_MODULE_DEPENDENCY",
                                  "module dependency is missing", "modules", findings, errors);
            }
            else {
                size_t dep_index = 0U;
                for (dep_index = 0U; dep_index < dep->dependency_count; dep_index += 1U) {
                    if (sl_cli_span_equal(dep->dependencies[dep_index],
                                          metadata->modules[outer].name))
                    {
                        sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_MODULE_CYCLE",
                                          "module dependency cycle detected", "modules", findings,
                                          errors);
                    }
                }
            }
        }
    }
}

static void sl_cli_audit_providers(const SlCliOptions* options, const SlCliMetadata* metadata,
                                   size_t* findings, size_t* errors)
{
    size_t index = 0U;

    for (index = 0U; index < metadata->provider_count; index += 1U) {
        if (sl_cli_span_empty(metadata->providers[index].token) ||
            sl_cli_span_empty(metadata->providers[index].provider) ||
            sl_cli_span_empty(metadata->providers[index].service))
        {
            sl_cli_audit_emit(options, "warn", "SLOPPY_AUDIT_PROVIDER_INCOMPLETE",
                              "provider metadata is missing token, provider, or service",
                              "dataProviders", findings, errors);
        }
        if (!sl_cli_span_empty(metadata->providers[index].capability)) {
            const SlCliCapability* capability =
                sl_cli_find_capability(metadata, metadata->providers[index].capability);
            if (capability == NULL) {
                sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_PROVIDER_CAPABILITY_MISSING",
                                  "data provider references an undeclared capability",
                                  "dataProviders", findings, errors);
            }
            else if (!sl_cli_span_equal_cstr(capability->kind, "database")) {
                sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_PROVIDER_CAPABILITY_KIND",
                                  "data provider capability is not a database capability",
                                  "dataProviders", findings, errors);
            }
            else {
                if (!sl_cli_span_empty(capability->provider) &&
                    !sl_cli_span_equal(capability->provider, metadata->providers[index].token))
                {
                    sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_PROVIDER_MISMATCH",
                                      "capability provider reference does not match data provider",
                                      "capabilities", findings, errors);
                }
                if (!sl_cli_span_empty(metadata->providers[index].required_access) &&
                    !sl_cli_capability_allows(capability->access,
                                              metadata->providers[index].required_access))
                {
                    sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_CAPABILITY_INSUFFICIENT",
                                      "capability access is insufficient for provider operation",
                                      "capabilities", findings, errors);
                }
            }
        }
    }
}

static void sl_cli_audit_capabilities(const SlCliOptions* options, const SlCliMetadata* metadata,
                                      size_t* findings, size_t* errors)
{
    size_t outer = 0U;
    size_t inner = 0U;
    bool has_filesystem = false;
    bool has_network = false;

    for (outer = 0U; outer < metadata->capability_count; outer += 1U) {
        if (sl_cli_span_empty(metadata->capabilities[outer].token) ||
            sl_cli_span_empty(metadata->capabilities[outer].kind) ||
            sl_cli_span_empty(metadata->capabilities[outer].access))
        {
            sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_CAPABILITY_INCOMPLETE",
                              "capability metadata is missing token, kind, or access",
                              "capabilities", findings, errors);
        }
        for (inner = outer + 1U; inner < metadata->capability_count; inner += 1U) {
            if (!sl_cli_span_empty(metadata->capabilities[outer].token) &&
                sl_cli_span_equal(metadata->capabilities[outer].token,
                                  metadata->capabilities[inner].token))
            {
                sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_DUPLICATE_CAPABILITY",
                                  "duplicate capability token", "capabilities", findings, errors);
            }
        }
        if (sl_cli_span_equal_cstr(metadata->capabilities[outer].kind, "database")) {
            if (sl_cli_span_empty(metadata->capabilities[outer].provider)) {
                sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_CAPABILITY_PROVIDER_REQUIRED",
                                  "database capability is missing required provider reference",
                                  "capabilities", findings, errors);
            }
            else if (sl_cli_find_provider(metadata, metadata->capabilities[outer].provider) == NULL)
            {
                sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_CAPABILITY_PROVIDER_MISSING",
                                  "database capability references an undeclared provider",
                                  "capabilities", findings, errors);
            }
        }
        else if ((sl_cli_span_equal_cstr(metadata->capabilities[outer].kind, "filesystem") ||
                  sl_cli_span_equal_cstr(metadata->capabilities[outer].kind, "network")) &&
                 !sl_cli_span_empty(metadata->capabilities[outer].provider))
        {
            sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_CAPABILITY_PROVIDER_FORBIDDEN",
                              "filesystem/network capabilities must not declare providers",
                              "capabilities", findings, errors);
        }
        if (sl_cli_span_equal_cstr(metadata->capabilities[outer].kind, "filesystem")) {
            has_filesystem = true;
        }
        if (sl_cli_span_equal_cstr(metadata->capabilities[outer].kind, "network")) {
            has_network = true;
        }
    }
    if (has_filesystem) {
        sl_cli_audit_emit(options, "note", "SLOPPY_AUDIT_FILESYSTEM_POLICY_VISIBLE",
                          "filesystem capabilities are policy-visible for sloppy/fs; no OS "
                          "sandbox is implemented",
                          "capabilities", findings, errors);
    }
    if (has_network) {
        sl_cli_audit_emit(options, "note", "SLOPPY_AUDIT_NETWORK_POLICY_VISIBLE",
                          "network capabilities are policy-visible for sloppy/net, including "
                          "LocalEndpoint metadata; no OS sandbox or external live-network "
                          "evidence is implemented",
                          "capabilities", findings, errors);
    }
}

static void sl_cli_audit_config_requirements(const SlCliOptions* options,
                                             const SlCliMetadata* metadata, size_t* findings,
                                             size_t* errors)
{
    char message_buffer[640];
    bool has_secret = false;
    size_t index = 0U;

    if (metadata->config_requirement_count == 0U) {
        return;
    }

    sl_cli_audit_emit(options, "note", "SLOPPY_AUDIT_CONFIG_PLAN_VISIBLE",
                      "configuration requirements are Plan-visible without values",
                      "configuration.requirements", findings, errors);

    for (index = 0U; index < metadata->config_requirement_count; index += 1U) {
        const SlCliConfigRequirement* requirement = &metadata->config_requirements[index];
        SlStringBuilder message_builder = {0};
        SlStr message = {0};
        const char* redaction = requirement->secret ? "secret" : "none";
        const char* severity = NULL;
        const char* code = NULL;
        const char* missing_kind = NULL;

        if (requirement->secret) {
            has_secret = true;
        }
        if (!sl_cli_span_equal_cstr(requirement->status, "missing")) {
            continue;
        }

        if (requirement->required) {
            severity = "error";
            code = "SLOPPY_AUDIT_CONFIG_REQUIRED_MISSING";
            missing_kind = requirement->secret ? "required secret config " : "required config ";
        }
        else {
            severity = "warn";
            code = "SLOPPY_AUDIT_CONFIG_OPTIONAL_MISSING";
            missing_kind = requirement->secret ? "optional secret config " : "optional config ";
        }

        if (!sl_status_is_ok(
                sl_string_builder_init_fixed(&message_builder, message_buffer,
                                             sizeof(message_buffer))) ||
            !sl_status_is_ok(sl_string_builder_append_cstr(&message_builder, "Missing ")) ||
            !sl_status_is_ok(sl_string_builder_append_cstr(&message_builder, missing_kind)) ||
            !sl_cli_string_builder_append_span(&message_builder, requirement->key) ||
            !sl_status_is_ok(
                sl_string_builder_append_cstr(&message_builder, " required by ")) ||
            !sl_cli_string_builder_append_span(&message_builder, requirement->required_by) ||
            !sl_status_is_ok(sl_string_builder_append_cstr(&message_builder, " (redaction: ")) ||
            !sl_status_is_ok(sl_string_builder_append_cstr(&message_builder, redaction)) ||
            !sl_status_is_ok(sl_string_builder_append_char(&message_builder, ')')))
        {
            sl_cli_audit_emit(options, "warn", "SLOPPY_AUDIT_CONFIG_METADATA_TRUNCATED",
                              "configuration requirement metadata is too long",
                              "configuration.requirements", findings, errors);
            continue;
        }

        message = sl_string_builder_view(&message_builder);
        if (message.length >= sizeof(message_buffer)) {
            sl_cli_audit_emit(options, "warn", "SLOPPY_AUDIT_CONFIG_METADATA_TRUNCATED",
                              "configuration requirement metadata is too long",
                              "configuration.requirements", findings, errors);
            continue;
        }
        message_buffer[message.length] = '\0';
        sl_cli_audit_emit(options, severity, code, message_buffer, "configuration.requirements",
                          findings, errors);
    }

    if (has_secret) {
        sl_cli_audit_emit(options, "note", "SLOPPY_AUDIT_CONFIG_SECRET_REDACTED",
                          "secret configuration requirements report key, status, source, and owner "
                          "metadata without values",
                          "configuration.requirements", findings, errors);
    }
}

static void sl_cli_audit_os_capabilities(const SlCliOptions* options, const SlCliMetadata* metadata,
                                         size_t* findings, size_t* errors)
{
    bool has_os = false;
    bool has_env = false;
    bool has_process = false;
    bool has_shell = false;
    bool has_signals = false;

    for (size_t index = 0U; index < metadata->capability_count; index += 1U) {
        if (sl_cli_span_equal_cstr(metadata->capabilities[index].kind, "os")) {
            has_os = true;
        }
        if (sl_cli_span_equal_cstr(metadata->capabilities[index].kind, "env")) {
            has_env = true;
        }
        if (sl_cli_span_equal_cstr(metadata->capabilities[index].kind, "process")) {
            has_process = true;
            if (sl_cli_span_equal_cstr(metadata->capabilities[index].access, "shell")) {
                has_shell = true;
            }
        }
        if (sl_cli_span_equal_cstr(metadata->capabilities[index].kind, "signals")) {
            has_signals = true;
        }
    }

    if (has_os || has_env || has_process || has_signals) {
        sl_cli_audit_emit(options, "note", "SLOPPY_AUDIT_OS_POLICY_VISIBLE",
                          "OS capabilities are policy-visible for sloppy/os; no OS sandbox, "
                          "environment values, native handles, or raw PIDs are exposed",
                          "capabilities", findings, errors);
    }
    if (has_env) {
        sl_cli_audit_emit(options, "note", "SLOPPY_AUDIT_OS_ENV_REDACTED",
                          "environment metadata names access only and omits variable values",
                          "capabilities", findings, errors);
    }
    if (has_process) {
        sl_cli_audit_emit(options, "note", "SLOPPY_AUDIT_OS_PROCESS_EXPLICIT_ARGV",
                          "process metadata is explicit-argv and does not imply shell execution",
                          "capabilities", findings, errors);
    }
    if (has_shell) {
        sl_cli_audit_emit(options, "warn", "SLOPPY_AUDIT_OS_SHELL_DENIED",
                          "shell capability metadata is visible, but shell execution is denied "
                          "unless a future explicit gate enables it",
                          "capabilities", findings, errors);
    }
}

static void sl_cli_audit_os_features(const SlCliOptions* options, const SlCliMetadata* metadata,
                                     size_t* findings, size_t* errors)
{
    if (sl_cli_metadata_requires_feature(metadata, "stdlib.os")) {
        sl_cli_audit_emit(options, "note", "SLOPPY_AUDIT_OS_PLAN_VISIBLE",
                          "sloppy/os is Plan-visible as stdlib.os", "requiredFeatures", findings,
                          errors);
    }
}

static void sl_cli_audit_http_clients(const SlCliOptions* options, const SlCliMetadata* metadata,
                                      size_t* findings, size_t* errors)
{
    bool has_http_client_feature = sl_cli_metadata_requires_feature(metadata, "stdlib.httpclient");
    bool has_static_target = false;
    bool has_dynamic_target = false;
    size_t index = 0U;

    for (index = 0U; index < metadata->http_client_count; index += 1U) {
        const SlCliHttpClient* client = &metadata->http_clients[index];
        if (sl_cli_span_equal_cstr(client->target, "static") ||
            !sl_cli_span_empty(client->base_url))
        {
            has_static_target = true;
        }
        if (sl_cli_span_equal_cstr(client->target, "dynamic") ||
            sl_cli_span_equal_cstr(client->target, "partial"))
        {
            has_dynamic_target = true;
        }
    }

    if (has_http_client_feature) {
        sl_cli_audit_emit(options, "note", "SLOPPY_AUDIT_HTTP_CLIENT_PLAN_VISIBLE",
                          "HttpClient is Plan-visible as stdlib.httpclient", "requiredFeatures",
                          findings, errors);
    }
    if (metadata->http_client_count > 0U) {
        sl_cli_audit_emit(options, "note", "SLOPPY_AUDIT_HTTP_CLIENTS_VISIBLE",
                          "HTTP client named-client metadata is visible without secret values",
                          "httpClients", findings, errors);
    }
    if (has_static_target) {
        sl_cli_audit_emit(
            options, "note", "SLOPPY_AUDIT_HTTP_CLIENT_STATIC_TARGET",
            "static outbound HTTP target metadata is visible without leaking URLs or headers",
            "httpClients", findings, errors);
    }
    if (has_dynamic_target) {
        sl_cli_audit_emit(
            options, "warn", "SLOPPY_AUDIT_HTTP_CLIENT_DYNAMIC_TARGET",
            "dynamic outbound HTTP target metadata is partial and must be checked at runtime",
            "httpClients", findings, errors);
    }
}

static void sl_cli_audit_workers(const SlCliOptions* options, const SlCliMetadata* metadata,
                                 size_t* findings, size_t* errors)
{
    if (!sl_cli_metadata_requires_feature(metadata, "stdlib.workers")) {
        return;
    }

    sl_cli_audit_emit(options, "note", "SLOPPY_AUDIT_WORKERS_PLAN_VISIBLE",
                      "sloppy/workers is Plan-visible as stdlib.workers", "requiredFeatures",
                      findings, errors);
    sl_cli_audit_emit(options, "note", "SLOPPY_AUDIT_WORKERS_BOUNDED",
                      "worker queues, pools, services, and JS workers must declare bounded "
                      "lifecycle policy before runtime admission",
                      "workers", findings, errors);
    sl_cli_audit_emit(options, "note", "SLOPPY_AUDIT_WORKERS_PAYLOAD_REDACTED",
                      "worker audit output reports policy metadata without payload values",
                      "workers", findings, errors);
}

static int sl_cli_command_audit(const SlCliOptions* options)
{
    unsigned char json_storage[SL_CLI_FILE_MAX_BYTES];
    yyjson_doc* doc = NULL;
    SlCliMetadata metadata = {0};
    size_t findings = 0U;
    size_t errors = 0U;

    if (options->plan_path == NULL) {
        sl_cli_write_cstr(stderr, "sloppy audit: --plan <path> is required\n");
        return 1;
    }
    if (sl_cli_load_metadata(options->plan_path, json_storage, NULL, false, &doc, &metadata) != 0) {
        if (doc != NULL) {
            yyjson_doc_free(doc);
        }
        return 1;
    }

    if (options->format == SL_CLI_FORMAT_JSON) {
        (void)printf("{\n  \"findings\": [");
    }
    else {
        (void)printf("Sloppy Audit\n\n");
    }

    if (!sl_cli_span_empty(metadata.completeness) &&
        sl_cli_span_equal_cstr(metadata.completeness, "invalid"))
    {
        sl_cli_audit_emit(options, "error", "SLOPPY_AUDIT_PLAN_INVALID",
                          "Plan completeness is invalid", "completeness", &findings, &errors);
    }
    sl_cli_audit_routes(options, &metadata, &findings, &errors);
    sl_cli_audit_modules(options, &metadata, &findings, &errors);
    sl_cli_audit_providers(options, &metadata, &findings, &errors);
    sl_cli_audit_capabilities(options, &metadata, &findings, &errors);
    sl_cli_audit_config_requirements(options, &metadata, &findings, &errors);
    sl_cli_audit_os_capabilities(options, &metadata, &findings, &errors);
    sl_cli_audit_os_features(options, &metadata, &findings, &errors);
    sl_cli_audit_http_clients(options, &metadata, &findings, &errors);
    sl_cli_audit_workers(options, &metadata, &findings, &errors);

    if (findings == 0U && options->format == SL_CLI_FORMAT_TEXT) {
        (void)printf("No findings.\n");
    }
    if (options->format == SL_CLI_FORMAT_JSON) {
        (void)printf("\n  ]\n}\n");
    }

    yyjson_doc_free(doc);
    return errors > 0U ? 1 : 0;
}

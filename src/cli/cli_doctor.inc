/*
 * src/cli/cli_doctor.inc
 *
 * Private doctor command emitters.
 * Included only by src/main.c to preserve existing static helper visibility.
 */
static void sl_cli_doctor_emit_text(SlArena* arena, SlCliSpan id, SlCliSpan status,
                                    SlCliSpan message)
{
    printf("[");
    sl_cli_write_span(stdout, status);
    printf("] ");
    sl_cli_write_span(stdout, id);
    if (!sl_cli_span_empty(message)) {
        printf(": ");
        sl_cli_write_redacted(stdout, arena, message);
    }
    printf("\n");
}

static void sl_cli_doctor_emit_json(SlArena* arena, SlCliSpan id, SlCliSpan status,
                                    SlCliSpan message, bool comma)
{
    printf("%s\n    { \"id\": ", comma ? "," : "");
    sl_cli_json_escape(stdout, id);
    printf(", \"status\": ");
    sl_cli_json_escape(stdout, status);
    printf(", \"message\": ");
    sl_cli_json_redacted(stdout, arena, message);
    printf(" }");
}

static bool sl_cli_path_exists(const char* path)
{
    SlFsStat stat = {0};

    if (path == NULL || path[0] == '\0') {
        return false;
    }
    return sl_status_is_ok(sl_fs_stat(sl_str_from_cstr(path), &stat, NULL)) && stat.exists &&
           stat.kind == SL_FS_NODE_FILE;
}

static void sl_cli_doctor_emit(const SlCliOptions* options, SlArena* arena, SlCliSpan id,
                               SlCliSpan status, SlCliSpan message, bool* emitted)
{
    if (options->format == SL_CLI_FORMAT_JSON) {
        sl_cli_doctor_emit_json(arena, id, status, message, *emitted);
        *emitted = true;
        return;
    }

    sl_cli_doctor_emit_text(arena, id, status, message);
}

static SlCliSpan sl_cli_doctor_route_identity(const SlCliRoute* route)
{
    if (!sl_cli_span_empty(route->name)) {
        return route->name;
    }
    if (!sl_cli_span_empty(route->pattern)) {
        return route->pattern;
    }
    return sl_cli_span_cstr("<unknown>");
}

static SlCliSpan sl_cli_doctor_route_message(SlArena* arena, const char* prefix,
                                             SlCliSpan route_identity)
{
    SlStringBuilder builder = {0};
    SlStr message = sl_str_empty();

    if (!sl_status_is_ok(sl_string_builder_init_arena(&builder, arena, 128U, 512U)) ||
        !sl_status_is_ok(sl_string_builder_append_cstr(&builder, prefix)) ||
        !sl_status_is_ok(sl_string_builder_append_cstr(&builder, " (route: ")) ||
        !sl_status_is_ok(sl_string_builder_append_str(
            &builder, sl_str_from_parts(route_identity.ptr, route_identity.length))) ||
        !sl_status_is_ok(sl_string_builder_append_char(&builder, ')'))) {
        return sl_cli_span_cstr(prefix);
    }

    message = sl_string_builder_view(&builder);
    return sl_cli_span_from_str(message);
}

static void sl_cli_doctor_emit_fs_capabilities(const SlCliOptions* options, SlArena* arena,
                                               const SlCliMetadata* metadata, bool* emitted)
{
    bool has_filesystem_capability = false;
    bool has_watch_capability = false;

    for (size_t index = 0U; index < metadata->capability_count; index += 1U) {
        if (sl_cli_span_equal_cstr(metadata->capabilities[index].kind, "filesystem")) {
            has_filesystem_capability = true;
            if (sl_cli_span_equal_cstr(metadata->capabilities[index].access, "watch")) {
                has_watch_capability = true;
            }
        }
    }
    if (has_filesystem_capability) {
        sl_cli_doctor_emit(
            options, arena, sl_cli_span_cstr("stdlib.fs.capabilities"), sl_cli_span_cstr("ok"),
            sl_cli_span_cstr(
                "filesystem capability metadata is visible to sloppy/fs policy checks"),
            emitted);
    }
    if (has_watch_capability) {
        sl_cli_doctor_emit(options, arena, sl_cli_span_cstr("stdlib.fs.watch"),
                           sl_cli_span_cstr("ok"),
                           sl_cli_span_cstr("fs.watch capability metadata is present"), emitted);
    }
}

static void sl_cli_doctor_emit_network_capabilities(const SlCliOptions* options, SlArena* arena,
                                                    const SlCliMetadata* metadata, bool* emitted)
{
    bool has_network_capability = false;
    bool has_connect_capability = false;
    bool has_listen_capability = false;

    for (size_t index = 0U; index < metadata->capability_count; index += 1U) {
        if (sl_cli_span_equal_cstr(metadata->capabilities[index].kind, "network")) {
            has_network_capability = true;
            if (sl_cli_span_equal_cstr(metadata->capabilities[index].access, "connect") ||
                sl_cli_span_equal_cstr(metadata->capabilities[index].access, "connect-listen"))
            {
                has_connect_capability = true;
            }
            if (sl_cli_span_equal_cstr(metadata->capabilities[index].access, "listen") ||
                sl_cli_span_equal_cstr(metadata->capabilities[index].access, "connect-listen"))
            {
                has_listen_capability = true;
            }
        }
    }
    if (has_network_capability) {
        sl_cli_doctor_emit(
            options, arena, sl_cli_span_cstr("stdlib.net.capabilities"), sl_cli_span_cstr("ok"),
            sl_cli_span_cstr("network capability metadata is visible to sloppy/net policy checks"),
            emitted);
    }
    if (has_connect_capability) {
        sl_cli_doctor_emit(
            options, arena, sl_cli_span_cstr("stdlib.net.connect"), sl_cli_span_cstr("ok"),
            sl_cli_span_cstr("network connect capability metadata is present"), emitted);
    }
    if (has_listen_capability) {
        sl_cli_doctor_emit(
            options, arena, sl_cli_span_cstr("stdlib.net.listen"), sl_cli_span_cstr("ok"),
            sl_cli_span_cstr("network listen capability metadata is present"), emitted);
    }
}

static bool sl_cli_metadata_requires_feature(const SlCliMetadata* metadata, const char* feature)
{
    size_t index = 0U;

    for (index = 0U; index < metadata->required_feature_count; index += 1U) {
        if (sl_cli_span_equal_cstr(metadata->required_features[index], feature)) {
            return true;
        }
    }
    return false;
}

static void sl_cli_doctor_emit_os_capabilities(const SlCliOptions* options, SlArena* arena,
                                               const SlCliMetadata* metadata, bool* emitted)
{
    bool has_os_feature = sl_cli_metadata_requires_feature(metadata, "stdlib.os");
    bool has_os_capability = false;
    bool has_env_capability = false;
    bool has_process_capability = false;
    bool has_shell_capability = false;
    bool has_signals_capability = false;

    for (size_t index = 0U; index < metadata->capability_count; index += 1U) {
        if (sl_cli_span_equal_cstr(metadata->capabilities[index].kind, "os")) {
            has_os_capability = true;
        }
        if (sl_cli_span_equal_cstr(metadata->capabilities[index].kind, "env")) {
            has_env_capability = true;
        }
        if (sl_cli_span_equal_cstr(metadata->capabilities[index].kind, "process")) {
            has_process_capability = true;
            if (sl_cli_span_equal_cstr(metadata->capabilities[index].access, "shell")) {
                has_shell_capability = true;
            }
        }
        if (sl_cli_span_equal_cstr(metadata->capabilities[index].kind, "signals")) {
            has_signals_capability = true;
        }
    }

    if (has_os_feature) {
        sl_cli_doctor_emit(
            options, arena, sl_cli_span_cstr("stdlib.os.required"), sl_cli_span_cstr("ok"),
            sl_cli_span_cstr("OS runtime feature metadata is Plan-visible"), emitted);
    }
    if (has_os_capability || has_env_capability || has_process_capability || has_signals_capability)
    {
        sl_cli_doctor_emit(
            options, arena, sl_cli_span_cstr("stdlib.os.capabilities"), sl_cli_span_cstr("ok"),
            sl_cli_span_cstr("OS capability metadata is visible without values or native handles"),
            emitted);
    }
    if (has_env_capability) {
        sl_cli_doctor_emit(
            options, arena, sl_cli_span_cstr("stdlib.os.environment"), sl_cli_span_cstr("ok"),
            sl_cli_span_cstr("environment access metadata omits environment values"), emitted);
    }
    if (has_process_capability) {
        sl_cli_doctor_emit(
            options, arena, sl_cli_span_cstr("stdlib.os.process"), sl_cli_span_cstr("ok"),
            sl_cli_span_cstr(
                "process metadata includes current-process snapshots and explicit argv only"),
            emitted);
    }
    if (has_shell_capability) {
        sl_cli_doctor_emit(
            options, arena, sl_cli_span_cstr("stdlib.os.shell"), sl_cli_span_cstr("warn"),
            sl_cli_span_cstr("shell capability metadata is visible but shell execution is denied"),
            emitted);
    }
    if (has_signals_capability) {
        sl_cli_doctor_emit(
            options, arena, sl_cli_span_cstr("stdlib.os.signals"), sl_cli_span_cstr("ok"),
            sl_cli_span_cstr("shutdown signal metadata is visible for the current platform"),
            emitted);
    }
}

static void sl_cli_doctor_emit_http_clients(const SlCliOptions* options, SlArena* arena,
                                            const SlCliMetadata* metadata, bool* emitted)
{
    bool has_http_client_feature = sl_cli_metadata_requires_feature(metadata, "stdlib.httpclient");
    bool has_static_target = false;
    bool has_dynamic_target = false;
    bool has_strict_network = false;
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
        if (client->strict_network) {
            has_strict_network = true;
        }
    }

    if (has_http_client_feature) {
        sl_cli_doctor_emit(
            options, arena, sl_cli_span_cstr("stdlib.httpclient.required"), sl_cli_span_cstr("ok"),
            sl_cli_span_cstr("HttpClient feature metadata is Plan-visible"), emitted);
    }
    if (metadata->http_client_count > 0U) {
        sl_cli_doctor_emit(
            options, arena, sl_cli_span_cstr("stdlib.httpclient.clients"), sl_cli_span_cstr("ok"),
            sl_cli_span_cstr("HTTP client named-client metadata is visible"), emitted);
    }
    if (has_static_target) {
        sl_cli_doctor_emit(
            options, arena, sl_cli_span_cstr("stdlib.httpclient.static-targets"),
            sl_cli_span_cstr("ok"),
            sl_cli_span_cstr("static outbound HTTP targets are visible without secret values"),
            emitted);
    }
    if (has_dynamic_target) {
        sl_cli_doctor_emit(
            options, arena, sl_cli_span_cstr("stdlib.httpclient.dynamic-targets"),
            sl_cli_span_cstr("warn"),
            sl_cli_span_cstr("dynamic outbound HTTP targets are marked partial, not guessed"),
            emitted);
    }
    if (has_strict_network) {
        sl_cli_doctor_emit(options, arena, sl_cli_span_cstr("stdlib.httpclient.strict-network"),
                           sl_cli_span_cstr("ok"),
                           sl_cli_span_cstr("strict HTTP network policy metadata is present"),
                           emitted);
    }
}

static void sl_cli_doctor_emit_workers(const SlCliOptions* options, SlArena* arena,
                                       const SlCliMetadata* metadata, bool* emitted)
{
    if (!sl_cli_metadata_requires_feature(metadata, "stdlib.workers")) {
        return;
    }

    sl_cli_doctor_emit(options, arena, sl_cli_span_cstr("stdlib.workers.required"),
                       sl_cli_span_cstr("ok"),
                       sl_cli_span_cstr("Workers feature metadata is Plan-visible"), emitted);
    sl_cli_doctor_emit(
        options, arena, sl_cli_span_cstr("stdlib.workers.lifecycle"), sl_cli_span_cstr("ok"),
        sl_cli_span_cstr(
            "workers are runtime resources with explicit bounded lifecycle policy metadata"),
        emitted);
    sl_cli_doctor_emit(
        options, arena, sl_cli_span_cstr("stdlib.workers.redaction"), sl_cli_span_cstr("ok"),
        sl_cli_span_cstr("worker doctor metadata omits payload and secret values"), emitted);
}

static void sl_cli_doctor_emit_cache(const SlCliOptions* options, SlArena* arena,
                                     const SlCliMetadata* metadata, bool* emitted)
{
    bool has_cache_feature = sl_cli_metadata_requires_feature(metadata, "stdlib.cache");
    bool has_output_cache = false;
    bool has_cache_headers = false;
    bool has_shared_auth_output_cache = false;

    for (size_t index = 0U; index < metadata->route_count; index += 1U) {
        const SlCliRoute* route = &metadata->routes[index];
        has_output_cache = has_output_cache || route->output_cache_value != NULL;
        has_cache_headers = has_cache_headers || route->cache_headers_value != NULL;
        if (route->auth_required && route->output_cache_value != NULL) {
            yyjson_val* shared = yyjson_obj_get(route->output_cache_value, "allowSharedAuthenticated");
            has_shared_auth_output_cache = has_shared_auth_output_cache ||
                                           (shared != NULL && yyjson_is_bool(shared) &&
                                            yyjson_get_bool(shared));
        }
    }

    if (has_cache_feature) {
        sl_cli_doctor_emit(options, arena, sl_cli_span_cstr("stdlib.cache.required"),
                           sl_cli_span_cstr("ok"),
                           sl_cli_span_cstr("Cache feature metadata is Plan-visible"), emitted);
    }
    if (has_output_cache) {
        sl_cli_doctor_emit(
            options, arena, sl_cli_span_cstr("route.output-cache.metadata"), sl_cli_span_cstr("ok"),
            sl_cli_span_cstr("OutputCache route metadata is visible without raw cache keys"),
            emitted);
    }
    if (has_cache_headers) {
        sl_cli_doctor_emit(
            options, arena, sl_cli_span_cstr("route.cache-headers.metadata"), sl_cli_span_cstr("ok"),
            sl_cli_span_cstr("cache header route metadata is visible"), emitted);
    }
    if (has_shared_auth_output_cache) {
        sl_cli_doctor_emit(
            options, arena, sl_cli_span_cstr("route.output-cache.shared-auth"),
            sl_cli_span_cstr("warn"),
            sl_cli_span_cstr(
                "shared authenticated OutputCache routes require application data isolation review"),
            emitted);
    }
}

static void sl_cli_doctor_emit_ffi(const SlCliOptions* options, SlArena* arena,
                                   const SlCliMetadata* metadata, bool* emitted)
{
    if (!sl_cli_metadata_requires_feature(metadata, "stdlib.ffi") &&
        metadata->ffi_library_count == 0U && metadata->ffi_struct_count == 0U)
    {
        return;
    }

    if (sl_cli_metadata_requires_feature(metadata, "stdlib.ffi")) {
        sl_cli_doctor_emit(
            options, arena, sl_cli_span_cstr("stdlib.ffi.required"), sl_cli_span_cstr("ok"),
            sl_cli_span_cstr("native FFI runtime feature metadata is Plan-visible"), emitted);
    }
    if (metadata->ffi_library_count > 0U || metadata->ffi_struct_count > 0U) {
        sl_cli_doctor_emit(
            options, arena, sl_cli_span_cstr("stdlib.ffi.metadata"), sl_cli_span_cstr("ok"),
            sl_cli_span_cstr("native FFI library and struct metadata is Plan-visible"), emitted);
    }
    for (size_t library_index = 0U; library_index < metadata->ffi_library_count;
         library_index += 1U)
    {
        const SlCliFfiLibrary* library = &metadata->ffi_libraries[library_index];
        SlPlatformDynlib handle = {0};
        SlStatus status = sl_platform_dynlib_open(sl_cli_span_str(library->name), &handle, NULL);
        if (!sl_status_is_ok(status)) {
            sl_cli_doctor_emit(options, arena, sl_cli_span_cstr("stdlib.ffi.library"),
                               sl_cli_span_cstr("error"),
                               sl_cli_span_cstr("native FFI library could not be loaded"), emitted);
            continue;
        }
        sl_cli_doctor_emit(options, arena, sl_cli_span_cstr("stdlib.ffi.library"),
                           sl_cli_span_cstr("ok"), sl_cli_span_cstr("native FFI library loads"),
                           emitted);
        for (size_t index = 0U; index < library->function_count; index += 1U) {
            const SlCliFfiFunction* function =
                &metadata->ffi_functions[library->first_function + index];
            void* symbol = NULL;
            status = sl_platform_dynlib_symbol(&handle, sl_cli_span_str(function->symbol), &symbol,
                                               NULL);
            sl_cli_doctor_emit(options, arena, sl_cli_span_cstr("stdlib.ffi.symbol"),
                               sl_status_is_ok(status) ? sl_cli_span_cstr("ok")
                                                       : sl_cli_span_cstr("error"),
                               sl_status_is_ok(status)
                                   ? sl_cli_span_cstr("native FFI symbol resolves")
                                   : sl_cli_span_cstr("native FFI symbol could not be resolved"),
                               emitted);
        }
        sl_platform_dynlib_close(&handle);
    }
}

static void sl_cli_doctor_emit_config_requirements(const SlCliOptions* options, SlArena* arena,
                                                   const SlCliMetadata* metadata, bool* emitted)
{
    char id_buffer[320];
    char message_buffer[640];
    size_t index = 0U;

    if (metadata->config_requirement_count == 0U) {
        return;
    }

    sl_cli_doctor_emit(options, arena, sl_cli_span_cstr("app.plan.config"), sl_cli_span_cstr("ok"),
                       sl_cli_span_cstr("configuration requirements are Plan-visible"), emitted);
    for (index = 0U; index < metadata->config_requirement_count; index += 1U) {
        const SlCliConfigRequirement* requirement = &metadata->config_requirements[index];
        SlStringBuilder id_builder = {0};
        SlStringBuilder message_builder = {0};
        SlStr id = {0};
        SlStr message = {0};
        SlCliSpan status = sl_cli_span_cstr("ok");
        const char* redaction = requirement->secret ? "secret" : "none";

        if (!sl_status_is_ok(
                sl_string_builder_init_fixed(&id_builder, id_buffer, sizeof(id_buffer))) ||
            !sl_status_is_ok(sl_string_builder_init_fixed(&message_builder, message_buffer,
                                                          sizeof(message_buffer))) ||
            !sl_status_is_ok(sl_string_builder_append_cstr(&id_builder, "config.")) ||
            !sl_cli_string_builder_append_span(&id_builder, requirement->key))
        {
            sl_cli_doctor_emit(
                options, arena, sl_cli_span_cstr("config.requirement"), sl_cli_span_cstr("warn"),
                sl_cli_span_cstr("configuration requirement metadata is too long"), emitted);
            continue;
        }

        if (sl_cli_span_equal_cstr(requirement->status, "missing")) {
            const char* missing_kind = "optional ";
            if (requirement->required && requirement->secret) {
                missing_kind = "required secret ";
            }
            else if (requirement->required) {
                missing_kind = "required ";
            }
            else if (requirement->secret) {
                missing_kind = "optional secret ";
            }
            status = requirement->required ? sl_cli_span_cstr("error") : sl_cli_span_cstr("warn");
            if (!sl_status_is_ok(sl_string_builder_append_cstr(&message_builder, "Missing ")) ||
                !sl_status_is_ok(sl_string_builder_append_cstr(&message_builder, missing_kind)) ||
                !sl_status_is_ok(sl_string_builder_append_cstr(&message_builder, "config ")) ||
                !sl_cli_string_builder_append_span(&message_builder, requirement->key) ||
                !sl_status_is_ok(
                    sl_string_builder_append_cstr(&message_builder, " required by ")) ||
                !sl_cli_string_builder_append_span(&message_builder, requirement->required_by) ||
                !sl_status_is_ok(
                    sl_string_builder_append_cstr(&message_builder, " (redaction: ")) ||
                !sl_status_is_ok(sl_string_builder_append_cstr(&message_builder, redaction)) ||
                !sl_status_is_ok(sl_string_builder_append_char(&message_builder, ')')))
            {
                sl_cli_doctor_emit(
                    options, arena, sl_cli_span_cstr("config.requirement"),
                    sl_cli_span_cstr("warn"),
                    sl_cli_span_cstr("configuration requirement message is too long"), emitted);
                continue;
            }
        }
        else if (sl_cli_span_equal_cstr(requirement->status, "defaulted")) {
            if (!sl_status_is_ok(sl_string_builder_append_cstr(&message_builder, "Config ")) ||
                !sl_cli_string_builder_append_span(&message_builder, requirement->key) ||
                !sl_status_is_ok(sl_string_builder_append_cstr(
                    &message_builder, " uses its declared default required by ")) ||
                !sl_cli_string_builder_append_span(&message_builder, requirement->required_by) ||
                !sl_status_is_ok(
                    sl_string_builder_append_cstr(&message_builder, " (redaction: ")) ||
                !sl_status_is_ok(sl_string_builder_append_cstr(&message_builder, redaction)) ||
                !sl_status_is_ok(sl_string_builder_append_char(&message_builder, ')')))
            {
                sl_cli_doctor_emit(
                    options, arena, sl_cli_span_cstr("config.requirement"),
                    sl_cli_span_cstr("warn"),
                    sl_cli_span_cstr("configuration requirement message is too long"), emitted);
                continue;
            }
        }
        else {
            if (!sl_status_is_ok(sl_string_builder_append_cstr(&message_builder, "Config ")) ||
                !sl_cli_string_builder_append_span(&message_builder, requirement->key) ||
                !sl_status_is_ok(
                    sl_string_builder_append_cstr(&message_builder, " is present from ")) ||
                !sl_cli_string_builder_append_span(&message_builder, requirement->source) ||
                !sl_status_is_ok(
                    sl_string_builder_append_cstr(&message_builder, " required by ")) ||
                !sl_cli_string_builder_append_span(&message_builder, requirement->required_by) ||
                !sl_status_is_ok(
                    sl_string_builder_append_cstr(&message_builder, " (redaction: ")) ||
                !sl_status_is_ok(sl_string_builder_append_cstr(&message_builder, redaction)) ||
                !sl_status_is_ok(sl_string_builder_append_char(&message_builder, ')')))
            {
                sl_cli_doctor_emit(
                    options, arena, sl_cli_span_cstr("config.requirement"),
                    sl_cli_span_cstr("warn"),
                    sl_cli_span_cstr("configuration requirement message is too long"), emitted);
                continue;
            }
        }

        id = sl_string_builder_view(&id_builder);
        message = sl_string_builder_view(&message_builder);
        sl_cli_doctor_emit(options, arena, sl_cli_span_from_str(id), status,
                           sl_cli_span_from_str(message), emitted);
    }
}

static void sl_cli_doctor_emit_auth(const SlCliOptions* options, SlArena* arena,
                                    const SlCliMetadata* metadata, bool* emitted)
{
    bool has_protected_routes = false;
    bool has_anonymous_routes = false;
    size_t route_index = 0U;

    for (route_index = 0U; route_index < metadata->route_count; route_index += 1U) {
        const SlCliRoute* route = &metadata->routes[route_index];
        if (route->auth_required) {
            has_protected_routes = true;
        }
        if (route->auth_allow_anonymous) {
            has_anonymous_routes = true;
        }
    }
    if (!has_protected_routes && !has_anonymous_routes && metadata->auth_scheme_count == 0U &&
        metadata->auth_policy_count == 0U)
    {
        return;
    }
    if (metadata->auth_scheme_count > 0U) {
        sl_cli_doctor_emit(
            options, arena, sl_cli_span_cstr("app.auth.schemes"), sl_cli_span_cstr("ok"),
            sl_cli_span_cstr("auth scheme metadata is Plan-visible without secret values"),
            emitted);
    }
    if (metadata->auth_policy_count > 0U) {
        sl_cli_doctor_emit(options, arena, sl_cli_span_cstr("app.auth.policies"),
                           sl_cli_span_cstr("ok"),
                           sl_cli_span_cstr("auth policy names are Plan-visible"), emitted);
    }
    for (size_t auth_index = 0U; auth_index < metadata->auth_scheme_count; auth_index += 1U) {
        const SlCliAuthScheme* scheme = &metadata->auth_schemes[auth_index];
        if (sl_cli_span_equal_cstr(scheme->kind, "cookieSession") &&
            sl_cli_span_equal_cstr(scheme->store, "memory"))
        {
            sl_cli_doctor_emit(
                options, arena, sl_cli_span_cstr("app.auth.session-store"),
                sl_cli_span_cstr("warn"),
                sl_cli_span_cstr("cookie session uses in-memory session store metadata"),
                emitted);
        }
    }
    if (has_protected_routes) {
        sl_cli_doctor_emit(
            options, arena, sl_cli_span_cstr("app.auth.protected-routes"),
            metadata->auth_scheme_count > 0U ? sl_cli_span_cstr("ok") : sl_cli_span_cstr("warn"),
            metadata->auth_scheme_count > 0U
                ? sl_cli_span_cstr("protected route auth metadata is Plan-visible")
                : sl_cli_span_cstr("protected routes have no auth scheme metadata"),
            emitted);
    }
    if (has_anonymous_routes) {
        sl_cli_doctor_emit(options, arena, sl_cli_span_cstr("app.auth.anonymous-routes"),
                           sl_cli_span_cstr("ok"),
                           sl_cli_span_cstr("anonymous route auth metadata is Plan-visible"),
                           emitted);
    }
    for (route_index = 0U; route_index < metadata->route_count; route_index += 1U) {
        const SlCliRoute* route = &metadata->routes[route_index];
        for (size_t scheme_index = 0U; scheme_index < route->auth_scheme_count; scheme_index += 1U)
        {
            bool found = false;
            for (size_t auth_index = 0U; auth_index < metadata->auth_scheme_count; auth_index += 1U)
            {
                if (sl_cli_span_equal(route->auth_schemes[scheme_index],
                                      metadata->auth_schemes[auth_index].name))
                {
                    found = true;
                    break;
                }
            }
            if (!found) {
                sl_cli_doctor_emit(
                    options, arena, sl_cli_span_cstr("app.auth.route-scheme"),
                    sl_cli_span_cstr("error"),
                    sl_cli_doctor_route_message(
                        arena, "route references an auth scheme that is not configured",
                        sl_cli_doctor_route_identity(route)),
                    emitted);
            }
        }
        if (!sl_cli_span_empty(route->auth_policy)) {
            bool found = false;
            for (size_t policy_index = 0U; policy_index < metadata->auth_policy_count;
                 policy_index += 1U)
            {
                if (sl_cli_span_equal(route->auth_policy, metadata->auth_policies[policy_index])) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                sl_cli_doctor_emit(
                    options, arena, sl_cli_span_cstr("app.auth.route-policy"),
                    sl_cli_span_cstr("error"),
                    sl_cli_doctor_route_message(
                        arena, "route references an auth policy that is not configured",
                        sl_cli_doctor_route_identity(route)),
                    emitted);
            }
        }
    }
}

static void sl_cli_doctor_emit_route_dispatch(const SlCliOptions* options, SlArena* arena,
                                              const SlCliMetadata* metadata, bool* emitted)
{
    const SlCliRouteDispatch* dispatch = &metadata->route_dispatch;

    if (!options->dispatch_details) {
        return;
    }
    if (!dispatch->present) {
        sl_cli_doctor_emit(options, arena, sl_cli_span_cstr("route.dispatch.metadata"),
                           sl_cli_span_cstr("warn"),
                           sl_cli_span_cstr("routeDispatch Plan metadata is not present"), emitted);
        return;
    }

    sl_cli_doctor_emit(options, arena, sl_cli_span_cstr("route.dispatch.mode"),
                       sl_cli_span_cstr("ok"), dispatch->mode, emitted);
    sl_cli_doctor_emit(
        options, arena, sl_cli_span_cstr("route.dispatch.contract"), sl_cli_span_cstr("ok"),
        sl_cli_span_cstr("Plan-backed native dispatch with routes.slrt integrity validation"),
        emitted);
    sl_cli_doctor_emit(options, arena, sl_cli_span_cstr("route.dispatch.artifact"),
                       sl_cli_span_equal_cstr(dispatch->artifact_kind, "none")
                           ? sl_cli_span_cstr("warn")
                           : sl_cli_span_cstr("ok"),
                       !sl_cli_span_empty(dispatch->artifact_hash)    ? dispatch->artifact_hash
                       : sl_cli_span_empty(dispatch->artifact_reason) ? dispatch->artifact_kind
                                                                      : dispatch->artifact_reason,
                       emitted);
    sl_cli_doctor_emit(options, arena, sl_cli_span_cstr("route.dispatch.fallback"),
                       dispatch->classic_fallback_available ? sl_cli_span_cstr("ok")
                                                            : sl_cli_span_cstr("warn"),
                       dispatch->classic_fallback_available
                           ? sl_cli_span_cstr("classic dispatch fallback is available")
                           : sl_cli_span_cstr("classic dispatch fallback is not advertised"),
                       emitted);
    sl_cli_doctor_emit(
        options, arena, sl_cli_span_cstr("route.dispatch.json.request"),
        dispatch->json_request_native_routes > 0U ? sl_cli_span_cstr("ok")
                                                  : sl_cli_span_cstr("info"),
        dispatch->json_request_native_routes > 0U
            ? sl_cli_span_cstr("native schema-backed JSON request validation is advertised")
            : sl_cli_span_cstr("no native schema-backed JSON request routes are advertised"),
        emitted);
    sl_cli_doctor_emit(options, arena, sl_cli_span_cstr("route.dispatch.json.response"),
                       dispatch->json_response_native_routes > 0U ? sl_cli_span_cstr("ok")
                                                                  : sl_cli_span_cstr("info"),
                       dispatch->json_response_native_routes > 0U
                           ? sl_cli_span_cstr("native JSON response writers are advertised")
                           : sl_cli_span_cstr("no native JSON response writers are advertised"),
                       emitted);
    if (dispatch->json_request_fallback_routes > 0U || dispatch->json_response_fallback_routes > 0U)
    {
        sl_cli_doctor_emit(options, arena, sl_cli_span_cstr("route.dispatch.json.fallback"),
                           sl_cli_span_cstr("warn"),
                           sl_cli_span_cstr("some JSON routes advertise fallback reasons"),
                           emitted);
    }
    if (dispatch->parameter_routes > 0U) {
        sl_cli_doctor_emit(
            options, arena, sl_cli_span_cstr("route.dispatch.segment-trie"),
            dispatch->segment_trie_nodes == 0U ? sl_cli_span_cstr("warn") : sl_cli_span_cstr("ok"),
            dispatch->segment_trie_nodes == 0U
                ? sl_cli_span_cstr("parameter routes do not advertise segment-trie nodes")
                : sl_cli_span_cstr("parameter routes advertise native segment-trie nodes"),
            emitted);
    }
    if (dispatch->native_no_js_endpoints == 0U) {
        sl_cli_doctor_emit(
            options, arena, sl_cli_span_cstr("route.dispatch.no-js"), sl_cli_span_cstr("warn"),
            sl_cli_span_cstr("native no-JS endpoint execution is not advertised"), emitted);
    }
}

static bool sl_cli_provider_is_kind(const SlCliProvider* provider, const char* kind)
{
    if (provider == NULL || kind == NULL) {
        return false;
    }
    return sl_cli_span_equal_cstr(provider->provider_kind, kind) ||
           sl_cli_span_equal_cstr(provider->provider, kind);
}

static bool sl_cli_doctor_span_starts_with(SlCliSpan span, const char* prefix)
{
    size_t index = 0U;

    if (prefix == NULL) {
        return false;
    }
    while (prefix[index] != '\0') {
        if (index >= span.length || span.ptr[index] != prefix[index]) {
            return false;
        }
        index += 1U;
    }
    return true;
}

static void sl_cli_doctor_emit_database_provider_dependencies(const SlCliOptions* options,
                                                              SlArena* arena,
                                                              const SlCliMetadata* metadata,
                                                              bool* emitted)
{
    bool has_sqlite = false;
    bool has_postgres = false;
    bool has_sqlserver = false;

    for (size_t index = 0U; index < metadata->provider_count; index += 1U) {
        const SlCliProvider* provider = &metadata->providers[index];

        has_sqlite = has_sqlite || sl_cli_provider_is_kind(provider, "sqlite");
        has_postgres = has_postgres || sl_cli_provider_is_kind(provider, "postgres");
        has_sqlserver = has_sqlserver || sl_cli_provider_is_kind(provider, "sqlserver");
    }

    if (has_sqlite) {
        sl_cli_doctor_emit(options, arena, sl_cli_span_cstr("sqlite.provider"),
                           sl_cli_span_cstr("ok"),
                           sl_cli_span_cstr("SQLite is embedded; SQLite providers do not require "
                                            "PostgreSQL, SQL Server, libpq, or ODBC"),
                           emitted);
    }
    if (has_postgres) {
        sl_cli_doctor_emit(
            options, arena, sl_cli_span_cstr("postgres.provider"), sl_cli_span_cstr("warn"),
            sl_cli_span_cstr(
                "PostgreSQL is optional; apps that use it need a connection string and libpq from "
                "a bundled provider package when available or from the system"),
            emitted);
    }
    if (has_sqlserver) {
        sl_cli_doctor_emit(
            options, arena, sl_cli_span_cstr("sqlserver.provider"), sl_cli_span_cstr("warn"),
            sl_cli_span_cstr("SQL Server is optional; apps that use it need Microsoft ODBC Driver "
                             "17 or 18 visible to the driver manager"),
            emitted);
    }
}

static bool sl_cli_doctor_span_contains_ascii_ci(SlCliSpan span, const char* needle)
{
    size_t needle_length = 0U;

    if (span.ptr == NULL || needle == NULL) {
        return false;
    }
    while (needle[needle_length] != '\0') {
        needle_length += 1U;
    }
    if (needle_length == 0U || span.length < needle_length) {
        return false;
    }
    for (size_t index = 0U; index + needle_length <= span.length; index += 1U) {
        size_t offset = 0U;
        for (offset = 0U; offset < needle_length; offset += 1U) {
            unsigned char left = (unsigned char)span.ptr[index + offset];
            unsigned char right = (unsigned char)needle[offset];
            if (left >= 'A' && left <= 'Z') {
                left = (unsigned char)(left + ('a' - 'A'));
            }
            if (right >= 'A' && right <= 'Z') {
                right = (unsigned char)(right + ('a' - 'A'));
            }
            if (left != right) {
                break;
            }
        }
        if (offset == needle_length) {
            return true;
        }
    }
    return false;
}

static void sl_cli_doctor_emit_plan_metadata(const SlCliOptions* options, SlArena* arena,
                                             const SlCliMetadata* metadata, bool* emitted)
{
    size_t health_routes = 0U;
    size_t management_routes = 0U;
    size_t realtime_routes = 0U;
    size_t static_routes = 0U;
    size_t rate_limited_routes = 0U;
    bool prometheus_metrics = false;
    bool json_metrics = false;

    for (size_t index = 0U; index < metadata->route_count; index += 1U) {
        const SlCliRoute* route = &metadata->routes[index];

        if (!sl_cli_span_empty(route->health_kind)) {
            health_routes += 1U;
        }
        if (!sl_cli_span_empty(route->realtime_kind)) {
            realtime_routes += 1U;
        }
        for (size_t tag = 0U; tag < route->tag_count; tag += 1U) {
            if (sl_cli_span_equal_cstr(route->tags[tag], "static")) {
                static_routes += 1U;
            }
        }
        if (route->rate_limit_count > 0U) {
            rate_limited_routes += 1U;
        }
        if (sl_cli_doctor_span_starts_with(route->name, "Management.")) {
            management_routes += 1U;
        }
        prometheus_metrics =
            prometheus_metrics || sl_cli_span_equal_cstr(route->name, "Management.Metrics");
        json_metrics =
            json_metrics || sl_cli_span_equal_cstr(route->name, "Management.MetricsJson");
    }

    sl_cli_doctor_emit(
        options, arena, sl_cli_span_cstr("app.plan.routes"),
        (metadata->route_count > 0U || sl_cli_span_equal_cstr(metadata->kind, "program"))
            ? sl_cli_span_cstr("ok")
            : sl_cli_span_cstr("warn"),
        metadata->route_count > 0U
            ? sl_cli_span_cstr("route metadata present")
            : (sl_cli_span_equal_cstr(metadata->kind, "program")
                   ? sl_cli_span_cstr("program Plan has no route metadata by design")
                   : sl_cli_span_cstr("no route metadata present")),
        emitted);
    sl_cli_doctor_emit(
        options, arena, sl_cli_span_cstr("app.plan.providers"),
        metadata->provider_count > 0U ? sl_cli_span_cstr("ok") : sl_cli_span_cstr("warn"),
        metadata->provider_count > 0U ? sl_cli_span_cstr("provider metadata present")
                                      : sl_cli_span_cstr("provider metadata not present"),
        emitted);
    sl_cli_doctor_emit(
        options, arena, sl_cli_span_cstr("app.plan.capabilities"),
        metadata->capability_count > 0U ? sl_cli_span_cstr("ok") : sl_cli_span_cstr("warn"),
        metadata->capability_count > 0U ? sl_cli_span_cstr("capability metadata present")
                                        : sl_cli_span_cstr("capability metadata not present"),
        emitted);
    if (static_routes > 0U) {
        sl_cli_doctor_emit(options, arena, sl_cli_span_cstr("app.plan.static-assets"),
                           sl_cli_span_cstr("ok"),
                           sl_cli_span_cstr("static asset route metadata present"), emitted);
    }
    if (rate_limited_routes > 0U) {
        sl_cli_doctor_emit(options, arena, sl_cli_span_cstr("app.rate-limit.routes"),
                           sl_cli_span_cstr("ok"),
                           sl_cli_span_cstr("rate-limit metadata present on Plan routes"),
                           emitted);
    }
    if (health_routes > 0U || management_routes > 0U || prometheus_metrics || json_metrics) {
        sl_cli_doctor_emit(
            options, arena, sl_cli_span_cstr("ops.health"),
            health_routes > 0U ? sl_cli_span_cstr("ok") : sl_cli_span_cstr("warn"),
            health_routes > 0U ? sl_cli_span_cstr("health endpoint metadata present")
                               : sl_cli_span_cstr("health endpoint metadata not present"),
            emitted);
        sl_cli_doctor_emit(
            options, arena, sl_cli_span_cstr("ops.management"),
            management_routes > 0U ? sl_cli_span_cstr("warn") : sl_cli_span_cstr("info"),
            management_routes > 0U
                ? sl_cli_span_cstr(
                      "management endpoints are exposed; protect detailed endpoints in production")
                : sl_cli_span_cstr("management endpoints are not exposed"),
            emitted);
        sl_cli_doctor_emit(
            options, arena, sl_cli_span_cstr("ops.metrics"),
            (prometheus_metrics && json_metrics) ? sl_cli_span_cstr("ok")
                                                 : sl_cli_span_cstr("warn"),
            (prometheus_metrics && json_metrics)
                ? sl_cli_span_cstr("Prometheus and JSON metrics endpoint metadata present")
                : sl_cli_span_cstr("Prometheus or JSON metrics endpoint metadata missing"),
            emitted);
    }
    if (realtime_routes > 0U) {
        sl_cli_doctor_emit(
            options, arena, sl_cli_span_cstr("app.realtime.framework"), sl_cli_span_cstr("warn"),
            sl_cli_span_cstr(
                "Realtime framework metadata is present; WebSocket runtime support remains alpha"),
            emitted);
    }
    sl_cli_doctor_emit_fs_capabilities(options, arena, metadata, emitted);
    sl_cli_doctor_emit_network_capabilities(options, arena, metadata, emitted);
    sl_cli_doctor_emit_os_capabilities(options, arena, metadata, emitted);
    sl_cli_doctor_emit_http_clients(options, arena, metadata, emitted);
    sl_cli_doctor_emit_workers(options, arena, metadata, emitted);
    sl_cli_doctor_emit_cache(options, arena, metadata, emitted);
    sl_cli_doctor_emit_ffi(options, arena, metadata, emitted);
    sl_cli_doctor_emit_config_requirements(options, arena, metadata, emitted);
    sl_cli_doctor_emit_auth(options, arena, metadata, emitted);
    sl_cli_doctor_emit_route_dispatch(options, arena, metadata, emitted);
    sl_cli_doctor_emit_database_provider_dependencies(options, arena, metadata, emitted);
    if (!sl_cli_span_empty(metadata->completeness)) {
        SlCliSpan status = sl_cli_span_cstr("warn");
        SlCliSpan message =
            sl_cli_span_cstr("Plan completeness is partial, runtime-only, or invalid");

        if (sl_cli_span_equal_cstr(metadata->completeness, "complete")) {
            status = sl_cli_span_cstr("ok");
            message = sl_cli_span_cstr("Plan completeness is complete");
        }
        else if (sl_cli_span_equal_cstr(metadata->completeness, "invalid")) {
            status = sl_cli_span_cstr("error");
        }
        sl_cli_doctor_emit(options, arena, sl_cli_span_cstr("app.plan.completeness"), status,
                           message, emitted);
    }
    for (size_t index = 0U; index < metadata->route_count; index += 1U) {
        const SlCliRoute* route = &metadata->routes[index];
        if (sl_cli_span_equal_cstr(route->name, "Docs.Ui") ||
            sl_cli_span_equal_cstr(route->name, "Docs.OpenApi"))
        {
            sl_cli_doctor_emit(
                options, arena, sl_cli_span_cstr("app.openapi.docs"),
                route->auth_required ? sl_cli_span_cstr("ok") : sl_cli_span_cstr("warn"),
                route->auth_required
                    ? sl_cli_span_cstr("OpenAPI docs route is Plan-visible and protected")
                    : sl_cli_span_cstr("OpenAPI docs route is Plan-visible and public"),
                emitted);
        }
        if (!sl_cli_span_empty(route->completeness) &&
            !sl_cli_span_equal_cstr(route->completeness, "complete"))
        {
            sl_cli_doctor_emit(
                options, arena, sl_cli_span_cstr("route.completeness"),
                sl_cli_span_equal_cstr(route->completeness, "invalid") ? sl_cli_span_cstr("error")
                                                                       : sl_cli_span_cstr("warn"),
                sl_cli_span_cstr("route has partial/runtime-only/invalid Plan metadata"), emitted);
        }
        if (sl_cli_span_empty(route->response_kind)) {
            sl_cli_doctor_emit(options, arena, sl_cli_span_cstr("route.response"),
                               sl_cli_span_cstr("warn"),
                               sl_cli_span_cstr("route response metadata is unknown"), emitted);
        }
        if (!route->auth_required && route->rate_limit_count == 0U &&
            (sl_cli_doctor_span_contains_ascii_ci(route->name, "login") ||
             sl_cli_doctor_span_contains_ascii_ci(route->pattern, "login")))
        {
            sl_cli_doctor_emit(options, arena, sl_cli_span_cstr("route.rate-limit.login"),
                               sl_cli_span_cstr("warn"),
                               sl_cli_doctor_route_message(
                                   arena, "public login route has no rate-limit metadata",
                                   sl_cli_doctor_route_identity(route)),
                               emitted);
        }
        for (size_t rate_index = 0U; rate_index < route->rate_limit_count; rate_index += 1U) {
            if (route->rate_limit_partials[rate_index]) {
                sl_cli_doctor_emit(options, arena, sl_cli_span_cstr("route.rate-limit.partial"),
                                   sl_cli_span_cstr("warn"),
                                   sl_cli_doctor_route_message(
                                       arena, "route rate-limit metadata is partial",
                                       sl_cli_doctor_route_identity(route)),
                                   emitted);
                break;
            }
        }
        for (size_t binding = 0U; binding < route->binding_count; binding += 1U) {
            if (sl_cli_span_equal_cstr(route->binding_kinds[binding], "body.json") &&
                sl_cli_span_empty(route->binding_schemas[binding]))
            {
                sl_cli_doctor_emit(
                    options, arena, sl_cli_span_cstr("route.validation"), sl_cli_span_cstr("warn"),
                    sl_cli_span_cstr("JSON body binding has no schema metadata"), emitted);
            }
        }
        if (!sl_cli_span_empty(route->query_schema) &&
            sl_cli_find_schema(metadata, route->query_schema) == NULL)
        {
            sl_cli_doctor_emit(options, arena, sl_cli_span_cstr("route.openapi.query"),
                               sl_cli_span_cstr("warn"),
                               sl_cli_span_cstr("route query contract references a missing schema"),
                               emitted);
        }
        if (!sl_cli_span_empty(route->params_schema) &&
            sl_cli_find_schema(metadata, route->params_schema) == NULL)
        {
            sl_cli_doctor_emit(
                options, arena, sl_cli_span_cstr("route.openapi.params"), sl_cli_span_cstr("warn"),
                sl_cli_span_cstr("route parameter contract references a missing schema"), emitted);
        }
        if (!sl_cli_span_empty(route->json_request_fallback)) {
            char message_buffer[640];
            SlStringBuilder message_builder = {0};
            SlCliSpan identity =
                !sl_cli_span_empty(route->name) ? route->name : route->pattern;
            SlCliSpan message = route->json_request_fallback;

            if (sl_status_is_ok(sl_string_builder_init_fixed(&message_builder, message_buffer,
                                                             sizeof(message_buffer))) &&
                sl_status_is_ok(sl_string_builder_append_cstr(&message_builder, "route ")) &&
                sl_cli_string_builder_append_span(&message_builder, identity) &&
                sl_status_is_ok(
                    sl_string_builder_append_cstr(&message_builder, " JSON request fallback: ")) &&
                sl_cli_string_builder_append_span(&message_builder, route->json_request_fallback))
            {
                message = sl_cli_span_from_str(sl_string_builder_view(&message_builder));
            }
            sl_cli_doctor_emit(options, arena, sl_cli_span_cstr("route.json.request"),
                               sl_cli_span_cstr("warn"), message, emitted);
        }
        if (!sl_cli_span_empty(route->json_response_fallback)) {
            char message_buffer[640];
            SlStringBuilder message_builder = {0};
            SlCliSpan identity =
                !sl_cli_span_empty(route->name) ? route->name : route->pattern;
            SlCliSpan message = route->json_response_fallback;

            if (sl_status_is_ok(sl_string_builder_init_fixed(&message_builder, message_buffer,
                                                             sizeof(message_buffer))) &&
                sl_status_is_ok(sl_string_builder_append_cstr(&message_builder, "route ")) &&
                sl_cli_string_builder_append_span(&message_builder, identity) &&
                sl_status_is_ok(
                    sl_string_builder_append_cstr(&message_builder, " JSON response fallback: ")) &&
                sl_cli_string_builder_append_span(&message_builder, route->json_response_fallback))
            {
                message = sl_cli_span_from_str(sl_string_builder_view(&message_builder));
            }
            sl_cli_doctor_emit(options, arena, sl_cli_span_cstr("route.json.response"),
                               sl_cli_span_cstr("warn"), message, emitted);
        }
    }
    {
        bool emitted_response_candidate = false;
        bool emitted_provider_candidate = false;

        for (size_t index = 0U; index < metadata->route_count; index += 1U) {
            const SlCliRoute* route = &metadata->routes[index];
            if (!emitted_response_candidate && sl_cli_span_equal_cstr(route->response_kind, "json"))
            {
                sl_cli_doctor_emit(
                    options, arena, sl_cli_span_cstr("optimization.native-json-candidate"),
                    sl_cli_span_cstr("ok"),
                    sl_cli_span_cstr("route has response metadata for future native JSON analysis"),
                    emitted);
                emitted_response_candidate = true;
            }
            if (!emitted_provider_candidate && route->effect_count > 0U) {
                sl_cli_doctor_emit(
                    options, arena, sl_cli_span_cstr("optimization.provider-route-candidate"),
                    sl_cli_span_cstr("ok"),
                    sl_cli_span_cstr(
                        "route has provider/effect metadata for future route analysis"),
                    emitted);
                emitted_provider_candidate = true;
            }
        }
    }
}

static void sl_cli_doctor_emit_environment(const SlCliOptions* options, SlArena* arena,
                                           bool* emitted)
{
    const char* bootstrap_asset = SLOPPY_BOOTSTRAP_BUILD_DIR "/internal/runtime-classic.js";

    sl_cli_doctor_emit(options, arena, sl_cli_span_cstr("bootstrap.assets"),
                       sl_cli_path_exists(bootstrap_asset) ? sl_cli_span_cstr("ok")
                                                           : sl_cli_span_cstr("warn"),
                       sl_cli_path_exists(bootstrap_asset)
                           ? sl_cli_span_cstr("bootstrap runtime asset found")
                           : sl_cli_span_cstr("bootstrap runtime asset not found in build layout"),
                       emitted);
#ifdef SLOPPY_ENABLE_V8_BRIDGE
    sl_cli_doctor_emit(
        options, arena, sl_cli_span_cstr("engine.v8"), sl_cli_span_cstr("ok"),
        sl_cli_span_cstr("V8 bridge compiled; runtime success still requires V8 tests"), emitted);
#else
    sl_cli_doctor_emit(
        options, arena, sl_cli_span_cstr("engine.v8"), sl_cli_span_cstr("warn"),
        sl_cli_span_cstr("V8 bridge disabled in this build; V8 runtime tests not run"), emitted);
#endif
    sl_cli_doctor_emit(
        options, arena, sl_cli_span_cstr("providers.live"), sl_cli_span_cstr("warn"),
        sl_cli_span_cstr(
            "live provider checks are not configured by default and no live DB was contacted"),
        emitted);
    sl_cli_doctor_emit(options, arena, sl_cli_span_cstr("package.runtime"),
                       sl_cli_span_cstr("warn"),
                       sl_cli_span_cstr("package release status requires package smoke"), emitted);
}

static void sl_cli_doctor_write_report(const SlCliOptions* options, bool emitted)
{
    unsigned char arena_storage[SL_CLI_DIAG_ARENA_BYTES];
    SlArena arena = {0};
    SlDiagBuilder builder = {0};
    SlDiag diag = {0};
    SlDiagReportContext context = {0};

    if (!sl_status_is_ok(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)))) {
        return;
    }
    if (!sl_status_is_ok(sl_diag_builder_init(
            &builder, &arena, SL_DIAG_SEVERITY_NOTE, SL_DIAG_NONE,
            sl_str_from_cstr("sloppy doctor report written"))))
    {
        return;
    }
    if (!sl_status_is_ok(sl_diag_builder_add_hint(
            &builder, emitted ? sl_str_from_cstr("doctor checks emitted")
                              : sl_str_from_cstr("no doctor checks emitted"))))
    {
        return;
    }
    if (!sl_status_is_ok(sl_diag_builder_finish(&builder, &diag))) {
        return;
    }
    context.command = sl_str_from_cstr("doctor");
    context.runtime_version = sl_str_from_cstr(SL_VERSION_STRING);
    context.plan_path =
        options != NULL && options->plan_path != NULL ? sl_str_from_cstr(options->plan_path)
                                                      : sl_str_empty();
    context.cause_message = sl_str_from_cstr("local doctor report");
    sl_breadcrumb_global_record(SL_DIAG_SUBSYSTEM_CLI, SL_BREADCRUMB_EVENT_DOCTOR_REPORT,
                                SL_STATUS_OK, 0U, 0U, 0U, 0U, context.cause_message);
    sl_cli_write_diagnostic_report_file("doctor-report.json", &diag, &context,
                                        sl_breadcrumb_global_ring());
}

static int sl_cli_command_doctor(const SlCliOptions* options)
{
    unsigned char* json_storage = sl_cli_scratch_json_storage();
    unsigned char* plan_arena_storage = sl_cli_scratch_plan_arena_storage();
    unsigned char diag_arena_storage[SL_CLI_DIAG_ARENA_BYTES];
    SlArena plan_arena;
    SlArena diag_arena;
    yyjson_doc* doc = NULL;
    SlCliMetadata* metadata = sl_cli_scratch_metadata();
    size_t index = 0U;
    bool emitted = false;

    if (!sl_status_is_ok(sl_arena_init(&plan_arena, plan_arena_storage, SL_CLI_ARENA_BYTES)) ||
        !sl_status_is_ok(
            sl_arena_init(&diag_arena, diag_arena_storage, sizeof(diag_arena_storage))))
    {
        sl_cli_write_cstr(stderr, "sloppy doctor: failed to initialize validation arena\n");
        return 1;
    }

    if (options->plan_path != NULL && sl_cli_load_metadata(options->plan_path, json_storage,
                                                           &plan_arena, true, &doc, metadata) != 0)
    {
        if (doc != NULL) {
            yyjson_doc_free(doc);
        }
        return 1;
    }

    if (options->format == SL_CLI_FORMAT_JSON) {
        printf("{\n  \"checks\": [");
        sl_cli_doctor_emit_environment(options, &diag_arena, &emitted);
        if (options->plan_path != NULL) {
            sl_cli_doctor_emit(
                options, &diag_arena, sl_cli_span_cstr("app.plan.parse"), sl_cli_span_cstr("ok"),
                sl_cli_span_cstr("app plan parsed by native Plan v1 parser"), &emitted);
            sl_cli_doctor_emit_plan_metadata(options, &diag_arena, metadata, &emitted);
        }
        for (index = 0U; index < metadata->doctor_check_count; index += 1U) {
            sl_cli_doctor_emit(options, &diag_arena, metadata->doctor_checks[index].id,
                               metadata->doctor_checks[index].status,
                               metadata->doctor_checks[index].message, &emitted);
        }
        printf("\n  ]\n}\n");
    }
    else {
        printf("Sloppy Doctor\n\n");
        sl_cli_doctor_emit_environment(options, &diag_arena, &emitted);
        if (options->plan_path != NULL) {
            sl_cli_doctor_emit(
                options, &diag_arena, sl_cli_span_cstr("app.plan.parse"), sl_cli_span_cstr("ok"),
                sl_cli_span_cstr("app plan parsed by native Plan v1 parser"), &emitted);
            sl_cli_doctor_emit_plan_metadata(options, &diag_arena, metadata, &emitted);
        }
        for (index = 0U; index < metadata->doctor_check_count; index += 1U) {
            sl_cli_doctor_emit(options, &diag_arena, metadata->doctor_checks[index].id,
                               metadata->doctor_checks[index].status,
                               metadata->doctor_checks[index].message, &emitted);
        }
    }

    if (options->doctor_report) {
        sl_cli_doctor_write_report(options, emitted);
    }

    if (doc != NULL) {
        yyjson_doc_free(doc);
    }
    return 0;
}

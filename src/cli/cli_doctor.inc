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
            sl_cli_span_cstr("process metadata is explicit-argv and exposes no native handles"),
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
    size_t index = 0U;

    for (index = 0U; index < metadata->route_count; index += 1U) {
        if (metadata->routes[index].auth_required) {
            has_protected_routes = true;
            break;
        }
    }
    if (!has_protected_routes && metadata->auth_scheme_count == 0U) {
        return;
    }
    sl_cli_doctor_emit(
        options, arena, sl_cli_span_cstr("app.auth.metadata"),
        metadata->auth_scheme_count > 0U ? sl_cli_span_cstr("ok") : sl_cli_span_cstr("warn"),
        metadata->auth_scheme_count > 0U
            ? sl_cli_span_cstr("auth scheme metadata is Plan-visible")
            : sl_cli_span_cstr("protected routes have no auth scheme metadata"),
        emitted);
}

static void sl_cli_doctor_emit_plan_metadata(const SlCliOptions* options, SlArena* arena,
                                             const SlCliMetadata* metadata, bool* emitted)
{
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
    sl_cli_doctor_emit_fs_capabilities(options, arena, metadata, emitted);
    sl_cli_doctor_emit_network_capabilities(options, arena, metadata, emitted);
    sl_cli_doctor_emit_os_capabilities(options, arena, metadata, emitted);
    sl_cli_doctor_emit_http_clients(options, arena, metadata, emitted);
    sl_cli_doctor_emit_workers(options, arena, metadata, emitted);
    sl_cli_doctor_emit_ffi(options, arena, metadata, emitted);
    sl_cli_doctor_emit_config_requirements(options, arena, metadata, emitted);
    sl_cli_doctor_emit_auth(options, arena, metadata, emitted);
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
        for (size_t binding = 0U; binding < route->binding_count; binding += 1U) {
            if (sl_cli_span_equal_cstr(route->binding_kinds[binding], "body.json") &&
                sl_cli_span_empty(route->binding_schemas[binding]))
            {
                sl_cli_doctor_emit(
                    options, arena, sl_cli_span_cstr("route.validation"), sl_cli_span_cstr("warn"),
                    sl_cli_span_cstr("JSON body binding has no schema metadata"), emitted);
            }
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

    if (doc != NULL) {
        yyjson_doc_free(doc);
    }
    return 0;
}

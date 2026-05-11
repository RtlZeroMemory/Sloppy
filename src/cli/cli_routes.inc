/*
 * src/cli/cli_routes.inc
 *
 * Private routes and capabilities command emitters.
 * Included only by src/main.c to preserve existing static helper visibility.
 */
static void sl_cli_routes_emit_json_bindings(const SlCliRoute* route)
{
    size_t binding = 0U;

    printf(", \"bindings\": [");
    for (binding = 0U; binding < route->binding_count; binding += 1U) {
        printf("%s{ \"kind\": ", binding == 0U ? "" : ", ");
        sl_cli_json_escape(stdout, route->binding_kinds[binding]);
        printf(", \"name\": ");
        sl_cli_json_escape(stdout, route->binding_names[binding]);
        printf(", \"schema\": ");
        sl_cli_json_escape(stdout, route->binding_schemas[binding]);
        printf(" }");
    }
    printf("]");
}

static void sl_cli_routes_emit_json_response_value(SlCliSpan kind, SlCliSpan helper,
                                                   uint64_t status, SlCliSpan body_schema)
{
    printf("{ \"kind\": ");
    sl_cli_json_escape(stdout, kind);
    printf(", \"helper\": ");
    sl_cli_json_escape(stdout, helper);
    printf(", \"status\": %llu", (unsigned long long)status);
    if (!sl_cli_span_empty(body_schema)) {
        printf(", \"bodySchema\": ");
        sl_cli_json_escape(stdout, body_schema);
    }
    printf(" }");
}

static void sl_cli_routes_emit_json_route(const SlCliRoute* route, size_t index)
{
    size_t response = 0U;

    printf("%s\n    { \"method\": ", index == 0U ? "" : ",");
    sl_cli_json_escape(stdout, route->method);
    printf(", \"kind\": ");
    sl_cli_json_escape(stdout, route->kind);
    printf(", \"pattern\": ");
    sl_cli_json_escape(stdout, route->pattern);
    printf(", \"handlerId\": %u, \"name\": ", route->handler_id);
    sl_cli_json_escape(stdout, route->name);
    printf(", \"module\": ");
    sl_cli_json_escape(stdout, route->module);
    printf(", \"sourceOrder\": %zu", route->source_order);
    printf(", \"source\": { \"path\": ");
    sl_cli_json_escape(stdout, route->source_path);
    printf(", \"line\": %llu, \"column\": %llu }", (unsigned long long)route->source_line,
           (unsigned long long)route->source_column);
    printf(", \"completeness\": ");
    sl_cli_json_escape(stdout, route->completeness);
    if (route->auth_required) {
        size_t item = 0U;
        printf(", \"auth\": { \"required\": true");
        if (route->auth_role_count > 0U) {
            printf(", \"roles\": [");
            for (item = 0U; item < route->auth_role_count; item += 1U) {
                printf("%s", item == 0U ? "" : ", ");
                sl_cli_json_escape(stdout, route->auth_roles[item]);
            }
            printf("]");
        }
        if (route->auth_claim_count > 0U) {
            printf(", \"claims\": [");
            for (item = 0U; item < route->auth_claim_count; item += 1U) {
                printf("%s", item == 0U ? "" : ", ");
                sl_cli_json_escape(stdout, route->auth_claims[item]);
            }
            printf("]");
        }
        if (!sl_cli_span_empty(route->auth_policy)) {
            printf(", \"policy\": ");
            sl_cli_json_escape(stdout, route->auth_policy);
        }
        printf(" }");
    }
    if (route->dynamic_metadata) {
        printf(", \"metadata\": { \"dynamic\": true, \"reason\": ");
        sl_cli_json_escape(stdout, route->dynamic_reason);
        printf(" }");
    }
    sl_cli_routes_emit_json_bindings(route);
    printf(", \"response\": ");
    sl_cli_routes_emit_json_response_value(route->response_kind, route->response_helper,
                                           route->response_status, route->response_body_schemas[0]);
    if (route->response_count > 1U) {
        printf(", \"responses\": [");
        for (response = 0U; response < route->response_count; response += 1U) {
            printf("%s", response == 0U ? "" : ", ");
            sl_cli_routes_emit_json_response_value(route->response_kinds[response],
                                                   route->response_helpers[response],
                                                   route->response_statuses[response],
                                                   route->response_body_schemas[response]);
        }
        printf("]");
    }
    printf(" }");
}

static void sl_cli_routes_emit_text_bindings(const SlCliRoute* route)
{
    size_t binding = 0U;

    if (route->binding_count == 0U) {
        printf("-");
        return;
    }
    for (binding = 0U; binding < route->binding_count; binding += 1U) {
        printf("%s%.*s", binding == 0U ? "" : ",", (int)route->binding_kinds[binding].length,
               route->binding_kinds[binding].ptr);
        if (!sl_cli_span_empty(route->binding_names[binding])) {
            printf(":%.*s", (int)route->binding_names[binding].length,
                   route->binding_names[binding].ptr);
        }
        if (!sl_cli_span_empty(route->binding_schemas[binding])) {
            printf("(%.*s)", (int)route->binding_schemas[binding].length,
                   route->binding_schemas[binding].ptr);
        }
    }
}

static void sl_cli_routes_emit_text_response(const SlCliRoute* route)
{
    if (!sl_cli_span_empty(route->response_kind)) {
        printf("%llu/%.*s/%.*s", (unsigned long long)route->response_status,
               (int)route->response_kind.length, route->response_kind.ptr,
               (int)route->response_helper.length, route->response_helper.ptr);
        return;
    }
    printf("unknown");
}

static void sl_cli_routes_emit_text_auth(const SlCliRoute* route)
{
    if (!route->auth_required) {
        printf("-");
        return;
    }
    printf("auth");
    if (!sl_cli_span_empty(route->auth_policy)) {
        printf(":policy:%.*s", (int)route->auth_policy.length, route->auth_policy.ptr);
    }
    else if (route->auth_role_count > 0U) {
        printf(":role:%.*s", (int)route->auth_roles[0].length, route->auth_roles[0].ptr);
    }
}

static bool sl_cli_routes_have_auth(const SlCliMetadata* metadata)
{
    size_t index = 0U;

    for (index = 0U; index < metadata->route_count; index += 1U) {
        if (metadata->routes[index].auth_required) {
            return true;
        }
    }
    return false;
}

static void sl_cli_routes_emit_text_route(const SlCliRoute* route, bool include_auth)
{
    printf("%-5zu  %-6.*s  %-11.*s  %-19.*s  %-7u  %-8.*s  %-6.*s  ", route->source_order,
           (int)route->method.length, route->method.ptr, (int)route->kind.length,
           route->kind.ptr, (int)route->pattern.length, route->pattern.ptr, route->handler_id,
           (int)route->completeness.length,
           route->completeness.ptr, (int)route->module.length, route->module.ptr);
    if (!sl_cli_span_empty(route->source_path)) {
        printf("%.*s:%llu:%llu", (int)route->source_path.length, route->source_path.ptr,
               (unsigned long long)route->source_line, (unsigned long long)route->source_column);
    }
    if (route->dynamic_metadata && !sl_cli_span_empty(route->dynamic_reason)) {
        printf(" (%.*s)", (int)route->dynamic_reason.length, route->dynamic_reason.ptr);
    }
    printf("  ");
    sl_cli_routes_emit_text_bindings(route);
    printf("  ");
    sl_cli_routes_emit_text_response(route);
    if (include_auth) {
        printf("  ");
        sl_cli_routes_emit_text_auth(route);
    }
    printf("  %.*s\n", (int)route->name.length, route->name.ptr);
}

static int sl_cli_command_routes(const SlCliOptions* options)
{
    unsigned char* json_storage = sl_cli_scratch_json_storage();
    unsigned char* plan_arena_storage = sl_cli_scratch_plan_arena_storage();
    SlArena plan_arena;
    yyjson_doc* doc = NULL;
    SlCliMetadata* metadata = sl_cli_scratch_metadata();
    size_t index = 0U;
    bool include_auth = false;

    if (options->plan_path == NULL) {
        sl_cli_write_cstr(stderr, "sloppy routes: --plan <path> is required\n");
        return 1;
    }
    if (!sl_status_is_ok(sl_arena_init(&plan_arena, plan_arena_storage, SL_CLI_ARENA_BYTES))) {
        sl_cli_write_cstr(stderr, "sloppy routes: failed to initialize plan validation arena\n");
        return 1;
    }
    if (sl_cli_load_metadata(options->plan_path, json_storage, &plan_arena, true, &doc, metadata) !=
        0)
    {
        if (doc != NULL) {
            yyjson_doc_free(doc);
        }
        return 1;
    }
    sl_cli_sort_routes(metadata);
    include_auth = sl_cli_routes_have_auth(metadata);

    if (options->format == SL_CLI_FORMAT_JSON) {
        printf("{\n  \"kind\": ");
        sl_cli_json_escape(stdout, metadata->kind);
        printf(",\n  \"routes\": [");
        for (index = 0U; index < metadata->route_count; index += 1U) {
            sl_cli_routes_emit_json_route(&metadata->routes[index], index);
        }
        printf("\n  ]\n}\n");
    }
    else {
        if (include_auth) {
            printf("ORDER  METHOD  KIND         PATTERN              HANDLER  COMPLETE  MODULE  SOURCE  "
                   "BINDINGS  RESPONSE  AUTH  NAME\n");
        }
        else {
            printf("ORDER  METHOD  KIND         PATTERN              HANDLER  COMPLETE  MODULE  SOURCE  "
                   "BINDINGS  RESPONSE  NAME\n");
        }
        if (metadata->route_count == 0U) {
            printf(sl_cli_span_equal_cstr(metadata->kind, "program")
                       ? "Program Plan: no route metadata expected.\n"
                       : "No routes.\n");
        }
        for (index = 0U; index < metadata->route_count; index += 1U) {
            sl_cli_routes_emit_text_route(&metadata->routes[index], include_auth);
        }
    }

    yyjson_doc_free(doc);
    return 0;
}

static void sl_cli_capabilities_emit_json_effect(const SlCliRoute* route, size_t effect_index,
                                                 size_t finding_count)
{
    printf("%s\n    { \"route\": { \"method\": ", finding_count == 0U ? "" : ",");
    sl_cli_json_escape(stdout, route->method);
    printf(", \"pattern\": ");
    sl_cli_json_escape(stdout, route->pattern);
    printf(" }, \"provider\": ");
    sl_cli_json_escape(stdout, route->effect_providers[effect_index]);
    printf(", \"providerKind\": ");
    sl_cli_json_escape(stdout, route->effect_provider_kinds[effect_index]);
    printf(", \"capabilityKind\": ");
    sl_cli_json_escape(stdout, route->effect_capability_kinds[effect_index]);
    printf(", \"access\": ");
    sl_cli_json_escape(stdout, route->effect_accesses[effect_index]);
    printf(", \"inference\": \"generated\", \"reason\": ");
    sl_cli_json_escape(stdout, route->effect_reasons[effect_index]);
    printf(", \"operation\": ");
    sl_cli_json_escape(stdout, route->effect_operations[effect_index]);
    printf(", \"source\": { \"path\": ");
    sl_cli_json_escape(stdout, route->source_path);
    printf(", \"line\": %llu, \"column\": %llu } }", (unsigned long long)route->source_line,
           (unsigned long long)route->source_column);
}

static void sl_cli_capabilities_emit_text_effect(const SlCliRoute* route, size_t effect_index)
{
    printf(
        "%.*s %.*s  %.*s  %.*s  %.*s  generated:%.*s  ", (int)route->method.length,
        route->method.ptr, (int)route->pattern.length, route->pattern.ptr,
        (int)route->effect_providers[effect_index].length,
        route->effect_providers[effect_index].ptr,
        (int)route->effect_provider_kinds[effect_index].length,
        route->effect_provider_kinds[effect_index].ptr,
        (int)route->effect_accesses[effect_index].length, route->effect_accesses[effect_index].ptr,
        (int)route->effect_reasons[effect_index].length, route->effect_reasons[effect_index].ptr);
    if (!sl_cli_span_empty(route->source_path)) {
        printf("%.*s:%llu:%llu", (int)route->source_path.length, route->source_path.ptr,
               (unsigned long long)route->source_line, (unsigned long long)route->source_column);
    }
    printf("\n");
}

static void sl_cli_capabilities_emit_json_declared(const SlCliCapability* capability,
                                                   size_t finding_count)
{
    printf("%s\n    { \"token\": ", finding_count == 0U ? "" : ",");
    sl_cli_json_escape(stdout, capability->token);
    printf(", \"kind\": ");
    sl_cli_json_escape(stdout, capability->kind);
    printf(", \"access\": ");
    sl_cli_json_escape(stdout, capability->access);
    printf(", \"inference\": \"declared\"");
    if (!sl_cli_span_empty(capability->provider)) {
        printf(", \"provider\": ");
        sl_cli_json_escape(stdout, capability->provider);
    }
    printf(" }");
}

static void sl_cli_capabilities_emit_text_declared(const SlCliCapability* capability)
{
    printf("PLAN  %.*s  %.*s  %.*s  declared", (int)capability->token.length, capability->token.ptr,
           (int)capability->kind.length, capability->kind.ptr, (int)capability->access.length,
           capability->access.ptr);
    if (!sl_cli_span_empty(capability->provider)) {
        printf("  %.*s", (int)capability->provider.length, capability->provider.ptr);
    }
    printf("\n");
}

static void sl_cli_capabilities_emit_json_ffi(const SlCliFfiFunction* function,
                                              size_t finding_count)
{
    printf("%s\n    { \"token\": ", finding_count == 0U ? "" : ",");
    sl_cli_json_escape(stdout, function->id);
    printf(
        ", \"kind\": \"ffi\", \"access\": \"use\", \"inference\": \"native-ffi\", \"library\": ");
    sl_cli_json_escape(stdout, function->library);
    printf(", \"symbol\": ");
    sl_cli_json_escape(stdout, function->symbol);
    printf(" }");
}

static void sl_cli_capabilities_emit_text_ffi(const SlCliFfiFunction* function)
{
    printf("PLAN  ");
    sl_cli_write_span(stdout, function->id);
    printf("  ffi  use  native-ffi  ");
    sl_cli_write_span(stdout, function->library);
    printf(":");
    sl_cli_write_span(stdout, function->symbol);
    printf("\n");
}

static void sl_cli_capabilities_emit_route_effects(const SlCliOptions* options,
                                                   const SlCliRoute* route, size_t* finding_count)
{
    size_t effect_index = 0U;

    for (effect_index = 0U; effect_index < route->effect_count; effect_index += 1U) {
        if (options->format == SL_CLI_FORMAT_JSON) {
            sl_cli_capabilities_emit_json_effect(route, effect_index, *finding_count);
        }
        else {
            sl_cli_capabilities_emit_text_effect(route, effect_index);
        }
        *finding_count += 1U;
    }
}

static int sl_cli_command_capabilities(const SlCliOptions* options)
{
    unsigned char* json_storage = sl_cli_scratch_json_storage();
    unsigned char* plan_arena_storage = sl_cli_scratch_plan_arena_storage();
    SlArena plan_arena;
    yyjson_doc* doc = NULL;
    SlCliMetadata* metadata = sl_cli_scratch_metadata();
    size_t route_index = 0U;
    size_t capability_index = 0U;
    size_t ffi_index = 0U;
    size_t finding_count = 0U;

    if (options->plan_path == NULL) {
        sl_cli_write_cstr(stderr, "sloppy capabilities: --plan <path> is required\n");
        return 1;
    }
    if (!sl_status_is_ok(sl_arena_init(&plan_arena, plan_arena_storage, SL_CLI_ARENA_BYTES))) {
        sl_cli_write_cstr(stderr,
                          "sloppy capabilities: failed to initialize plan validation arena\n");
        return 1;
    }
    if (sl_cli_load_metadata(options->plan_path, json_storage, &plan_arena, true, &doc, metadata) !=
        0)
    {
        if (doc != NULL) {
            yyjson_doc_free(doc);
        }
        return 1;
    }
    sl_cli_sort_routes(metadata);

    if (options->format == SL_CLI_FORMAT_JSON) {
        printf("{\n  \"capabilities\": [");
    }
    else {
        printf("SCOPE  TOKEN/ROUTE  KIND  ACCESS  REASON  SOURCE\n");
    }
    for (capability_index = 0U; capability_index < metadata->capability_count;
         capability_index += 1U)
    {
        if (options->format == SL_CLI_FORMAT_JSON) {
            sl_cli_capabilities_emit_json_declared(&metadata->capabilities[capability_index],
                                                   finding_count);
        }
        else {
            sl_cli_capabilities_emit_text_declared(&metadata->capabilities[capability_index]);
        }
        finding_count += 1U;
    }
    for (ffi_index = 0U; ffi_index < metadata->ffi_function_count; ffi_index += 1U) {
        if (options->format == SL_CLI_FORMAT_JSON) {
            sl_cli_capabilities_emit_json_ffi(&metadata->ffi_functions[ffi_index], finding_count);
        }
        else {
            sl_cli_capabilities_emit_text_ffi(&metadata->ffi_functions[ffi_index]);
        }
        finding_count += 1U;
    }
    for (route_index = 0U; route_index < metadata->route_count; route_index += 1U) {
        sl_cli_capabilities_emit_route_effects(options, &metadata->routes[route_index],
                                               &finding_count);
    }
    if (finding_count == 0U && options->format == SL_CLI_FORMAT_TEXT) {
        printf("No inferred route capabilities.\n");
    }
    if (options->format == SL_CLI_FORMAT_JSON) {
        printf("\n  ]\n}\n");
    }

    yyjson_doc_free(doc);
    return 0;
}

/*
 * src/cli/cli_metadata.inc
 *
 * Private app-plan metadata loading and validation helpers for CLI commands.
 * Included only by src/main.c to preserve existing static helper visibility.
 */
static int sl_cli_parse_handlers(yyjson_val* root, SlCliMetadata* metadata)
{
    yyjson_val* handlers = yyjson_obj_get(root, "handlers");
    yyjson_val* value = NULL;
    yyjson_arr_iter iter;

    if (handlers == NULL || !yyjson_is_arr(handlers)) {
        return 0;
    }

    yyjson_arr_iter_init(handlers, &iter);
    while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
        yyjson_val* id_value = yyjson_obj_get(value, "id");
        uint64_t id = 0U;

        if (!yyjson_is_obj(value) || id_value == NULL || !yyjson_is_uint(id_value)) {
            continue;
        }
        if (metadata->handler_count >= SL_CLI_MAX_HANDLERS) {
            sl_cli_write_cstr(stderr, "sloppy: too many handlers in metadata\n");
            return 1;
        }
        id = yyjson_get_uint(id_value);
        if (id > UINT32_MAX) {
            continue;
        }
        metadata->handlers[metadata->handler_count].id = (SlHandlerId)id;
        metadata->handlers[metadata->handler_count].display_name =
            sl_cli_json_span(value, "displayName");
        metadata->handler_count += 1U;
    }

    return 0;
}

static void sl_cli_parse_route_bindings(yyjson_val* value, SlCliRoute* route)
{
    yyjson_val* bindings = yyjson_obj_get(value, "bindings");
    yyjson_val* entry = NULL;
    yyjson_arr_iter iter;

    if (bindings == NULL || !yyjson_is_arr(bindings)) {
        return;
    }

    yyjson_arr_iter_init(bindings, &iter);
    while ((entry = yyjson_arr_iter_next(&iter)) != NULL) {
        if (yyjson_is_obj(entry) && route->binding_count < SL_CLI_MAX_ROUTE_BINDINGS) {
            route->binding_kinds[route->binding_count] = sl_cli_json_span(entry, "kind");
            route->binding_names[route->binding_count] = sl_cli_json_span(entry, "name");
            route->binding_schemas[route->binding_count] = sl_cli_json_span(entry, "schema");
            route->binding_count += 1U;
        }
    }
}

static void sl_cli_parse_route_json_plans(yyjson_val* value, SlCliRoute* route)
{
    yyjson_val* request = yyjson_obj_get(value, "jsonRequest");
    yyjson_val* response = yyjson_obj_get(value, "jsonResponse");

    if (request != NULL && yyjson_is_obj(request)) {
        route->json_request_mode = sl_cli_json_span(request, "mode");
        route->json_request_materialization = sl_cli_json_span(request, "materialization");
        route->json_request_schema = sl_cli_json_span(request, "schema");
        route->json_request_fallback = sl_cli_json_span(request, "fallbackReason");
    }
    if (response != NULL && yyjson_is_obj(response)) {
        route->json_response_mode = sl_cli_json_span(response, "mode");
        route->json_response_writer = sl_cli_json_span(response, "writer");
        route->json_response_schema = sl_cli_json_span(response, "schema");
        route->json_response_fallback = sl_cli_json_span(response, "fallbackReason");
    }
    if (sl_cli_span_empty(route->json_request_mode)) {
        route->json_request_mode = sl_cli_span_cstr("none");
    }
    if (sl_cli_span_empty(route->json_request_materialization)) {
        route->json_request_materialization = sl_cli_span_cstr("none");
    }
    if (sl_cli_span_empty(route->json_response_mode)) {
        route->json_response_mode = sl_cli_span_cstr("none");
    }
    if (sl_cli_span_empty(route->json_response_writer)) {
        route->json_response_writer = sl_cli_span_cstr("none");
    }
}

static void sl_cli_parse_route_effects(yyjson_val* value, SlCliRoute* route)
{
    yyjson_val* effects = yyjson_obj_get(value, "effects");
    yyjson_val* entry = NULL;
    yyjson_arr_iter iter;

    if (effects == NULL || !yyjson_is_arr(effects)) {
        return;
    }

    yyjson_arr_iter_init(effects, &iter);
    while ((entry = yyjson_arr_iter_next(&iter)) != NULL) {
        if (yyjson_is_obj(entry) && route->effect_count < SL_CLI_MAX_ROUTE_EFFECTS) {
            route->effect_providers[route->effect_count] = sl_cli_json_span(entry, "provider");
            route->effect_provider_kinds[route->effect_count] =
                sl_cli_json_span(entry, "providerKind");
            route->effect_capability_kinds[route->effect_count] =
                sl_cli_json_span(entry, "capabilityKind");
            route->effect_accesses[route->effect_count] = sl_cli_json_span(entry, "access");
            route->effect_operations[route->effect_count] = sl_cli_json_span(entry, "operation");
            route->effect_reasons[route->effect_count] = sl_cli_json_span(entry, "reason");
            route->effect_count += 1U;
        }
    }
}

static int sl_cli_parse_route_tags(yyjson_val* value, SlCliRoute* route)
{
    yyjson_val* tags = yyjson_obj_get(value, "tags");
    yyjson_val* entry = NULL;
    yyjson_arr_iter iter;

    if (tags == NULL || !yyjson_is_arr(tags)) {
        return 0;
    }

    yyjson_arr_iter_init(tags, &iter);
    while ((entry = yyjson_arr_iter_next(&iter)) != NULL) {
        if (!yyjson_is_str(entry)) {
            continue;
        }
        if (route->tag_count >= SL_CLI_MAX_ROUTE_TAGS) {
            sl_cli_write_cstr(stderr, "sloppy: too many route tags in metadata for ");
            sl_cli_write_span(stderr, route->method);
            sl_cli_write_cstr(stderr, " ");
            sl_cli_write_span(stderr, route->pattern);
            sl_cli_write_cstr(stderr, "\n");
            return 1;
        }
        route->tags[route->tag_count] = (SlCliSpan){yyjson_get_str(entry), yyjson_get_len(entry)};
        route->tag_count += 1U;
    }
    return 0;
}

static int sl_cli_parse_route_span_array(yyjson_val* value, const char* field, SlCliSpan* values,
                                         size_t* count, size_t capacity)
{
    yyjson_val* array = yyjson_obj_get(value, field);
    yyjson_val* entry = NULL;
    yyjson_arr_iter iter;

    if (array == NULL || !yyjson_is_arr(array)) {
        return 0;
    }
    yyjson_arr_iter_init(array, &iter);
    while ((entry = yyjson_arr_iter_next(&iter)) != NULL) {
        if (!yyjson_is_str(entry)) {
            continue;
        }
        if (*count >= capacity) {
            sl_cli_write_cstr(stderr, "sloppy: too many route metadata entries\n");
            return 1;
        }
        values[*count] = (SlCliSpan){yyjson_get_str(entry), yyjson_get_len(entry)};
        *count += 1U;
    }
    return 0;
}

static int sl_cli_parse_route_contract_headers(yyjson_val* value, SlCliRoute* route)
{
    yyjson_val* headers = yyjson_obj_get(value, "headers");
    yyjson_val* entry = NULL;
    yyjson_arr_iter iter;

    if (headers == NULL || !yyjson_is_arr(headers)) {
        return 0;
    }
    yyjson_arr_iter_init(headers, &iter);
    while ((entry = yyjson_arr_iter_next(&iter)) != NULL) {
        yyjson_val* required = NULL;
        if (!yyjson_is_obj(entry)) {
            continue;
        }
        if (route->header_count >= SL_CLI_MAX_ROUTE_BINDINGS) {
            sl_cli_write_cstr(stderr, "sloppy: too many route header metadata entries\n");
            return 1;
        }
        route->header_names[route->header_count] = sl_cli_json_span(entry, "name");
        route->header_schemas[route->header_count] = sl_cli_json_span(entry, "schema");
        route->header_descriptions[route->header_count] = sl_cli_json_span(entry, "description");
        required = yyjson_obj_get(entry, "required");
        route->header_required[route->header_count] =
            required != NULL && yyjson_is_bool(required) && yyjson_get_bool(required);
        route->header_count += 1U;
    }
    return 0;
}

static int sl_cli_parse_route_auth_array(yyjson_val* array, SlCliSpan* values, size_t* count,
                                         size_t capacity)
{
    yyjson_val* entry = NULL;
    yyjson_arr_iter iter;

    if (array == NULL || !yyjson_is_arr(array)) {
        return 0;
    }
    yyjson_arr_iter_init(array, &iter);
    while ((entry = yyjson_arr_iter_next(&iter)) != NULL) {
        if (!yyjson_is_str(entry)) {
            continue;
        }
        if (*count >= capacity) {
            sl_cli_write_cstr(stderr, "sloppy: too many route auth entries in metadata\n");
            return 1;
        }
        values[*count] = (SlCliSpan){yyjson_get_str(entry), yyjson_get_len(entry)};
        *count += 1U;
    }
    return 0;
}

static int sl_cli_parse_route_auth(yyjson_val* value, SlCliRoute* route)
{
    yyjson_val* auth = yyjson_obj_get(value, "auth");
    yyjson_val* required = NULL;
    yyjson_val* allow_anonymous = NULL;

    if (auth == NULL) {
        return 0;
    }
    if (!yyjson_is_obj(auth)) {
        sl_cli_write_cstr(stderr, "sloppy: malformed metadata: route auth must be an object\n");
        return 1;
    }
    allow_anonymous = yyjson_obj_get(auth, "allowAnonymous");
    required = yyjson_obj_get(auth, "required");
    if (required != NULL && !yyjson_is_bool(required)) {
        sl_cli_write_cstr(stderr,
                          "sloppy: malformed metadata: route auth.required must be a boolean\n");
        return 1;
    }
    if (allow_anonymous != NULL && !yyjson_is_bool(allow_anonymous)) {
        sl_cli_write_cstr(stderr,
                          "sloppy: malformed metadata: route auth.allowAnonymous must be a boolean\n");
        return 1;
    }
    route->auth_allow_anonymous = allow_anonymous != NULL && yyjson_get_bool(allow_anonymous);
    route->auth_required = required == NULL ? !route->auth_allow_anonymous : yyjson_get_bool(required);
    if (route->auth_allow_anonymous) {
        route->auth_required = false;
    }
    route->auth_policy = sl_cli_json_span(auth, "policy");
    if (sl_cli_parse_route_auth_array(yyjson_obj_get(auth, "schemes"), route->auth_schemes,
                                      &route->auth_scheme_count, SL_CLI_MAX_ROUTE_AUTH_SCHEMES) !=
        0)
    {
        return 1;
    }
    if (sl_cli_parse_route_auth_array(yyjson_obj_get(auth, "scopes"), route->auth_scopes,
                                      &route->auth_scope_count, SL_CLI_MAX_ROUTE_AUTH_SCOPES) != 0)
    {
        return 1;
    }
    if (sl_cli_parse_route_auth_array(yyjson_obj_get(auth, "roles"), route->auth_roles,
                                      &route->auth_role_count, SL_CLI_MAX_ROUTE_AUTH_ROLES) != 0)
    {
        return 1;
    }
    return sl_cli_parse_route_auth_array(yyjson_obj_get(auth, "claims"), route->auth_claims,
                                         &route->auth_claim_count, SL_CLI_MAX_ROUTE_AUTH_CLAIMS);
}

static int sl_cli_parse_route_response_entry(yyjson_val* value, SlCliRoute* route)
{
    if (!yyjson_is_obj(value)) {
        return 0;
    }
    if (route->response_count >= SL_CLI_MAX_ROUTE_RESPONSES) {
        sl_cli_write_cstr(stderr, "sloppy: too many route responses in metadata\n");
        return 1;
    }
    route->response_kinds[route->response_count] = sl_cli_json_span(value, "kind");
    route->response_helpers[route->response_count] = sl_cli_json_span(value, "helper");
    route->response_body_schemas[route->response_count] = sl_cli_json_span(value, "bodySchema");
    route->response_statuses[route->response_count] = sl_cli_json_uint_or_zero(value, "status");
    route->response_count += 1U;
    return 0;
}

static int sl_cli_parse_route_responses(yyjson_val* value, SlCliRoute* route)
{
    yyjson_val* responses = yyjson_obj_get(value, "responses");
    yyjson_val* entry = NULL;
    yyjson_arr_iter iter;

    if (responses == NULL) {
        return 0;
    }
    if (!yyjson_is_arr(responses)) {
        return 0;
    }

    yyjson_arr_iter_init(responses, &iter);
    while ((entry = yyjson_arr_iter_next(&iter)) != NULL) {
        if (sl_cli_parse_route_response_entry(entry, route) != 0) {
            return 1;
        }
    }
    return 0;
}

static int sl_cli_parse_route_rate_limits(yyjson_val* value, SlCliRoute* route)
{
    yyjson_val* rate_limits = yyjson_obj_get(value, "rateLimit");
    yyjson_val* entry = NULL;
    yyjson_arr_iter iter;

    if (rate_limits == NULL) {
        return 0;
    }
    if (!yyjson_is_arr(rate_limits)) {
        sl_cli_write_cstr(stderr, "sloppy: malformed metadata: route rateLimit must be an array\n");
        return 1;
    }

    yyjson_arr_iter_init(rate_limits, &iter);
    while ((entry = yyjson_arr_iter_next(&iter)) != NULL) {
        yyjson_val* partial = NULL;
        if (!yyjson_is_obj(entry)) {
            sl_cli_write_cstr(
                stderr, "sloppy: malformed metadata: route rateLimit entries must be objects\n");
            return 1;
        }
        if (route->rate_limit_count >= SL_CLI_MAX_ROUTE_RATE_LIMITS) {
            sl_cli_write_cstr(stderr, "sloppy: too many route rateLimit entries in metadata\n");
            return 1;
        }
        route->rate_limit_names[route->rate_limit_count] = sl_cli_json_span(entry, "name");
        route->rate_limit_algorithms[route->rate_limit_count] =
            sl_cli_json_span(entry, "algorithm");
        route->rate_limit_stores[route->rate_limit_count] = sl_cli_json_span(entry, "store");
        route->rate_limit_partitions[route->rate_limit_count] =
            sl_cli_json_span(entry, "partition");
        partial = yyjson_obj_get(entry, "partial");
        route->rate_limit_partials[route->rate_limit_count] =
            partial != NULL && yyjson_is_bool(partial) && yyjson_get_bool(partial);
        route->rate_limit_count += 1U;
    }
    return 0;
}

static int sl_cli_parse_route_health(yyjson_val* value, SlCliRoute* route)
{
    yyjson_val* health = yyjson_obj_get(value, "health");
    yyjson_val* kind = NULL;
    yyjson_val* checks = NULL;
    yyjson_val* entry = NULL;
    yyjson_arr_iter iter;

    if (health == NULL) {
        return 0;
    }
    if (!yyjson_is_obj(health)) {
        sl_cli_write_cstr(stderr, "sloppy: malformed metadata: route health must be an object\n");
        return 1;
    }
    kind = yyjson_obj_get(health, "kind");
    if (kind != NULL && !yyjson_is_str(kind)) {
        sl_cli_write_cstr(stderr,
                          "sloppy: malformed metadata: route health kind must be a string\n");
        return 1;
    }
    route->health_kind = sl_cli_json_span(health, "kind");
    checks = yyjson_obj_get(health, "checks");
    if (checks == NULL) {
        return 0;
    }
    if (!yyjson_is_arr(checks)) {
        sl_cli_write_cstr(stderr,
                          "sloppy: malformed metadata: route health checks must be an array\n");
        return 1;
    }
    if (yyjson_arr_size(checks) == 0U) {
        sl_cli_write_cstr(stderr,
                          "sloppy: malformed metadata: route health checks must not be empty\n");
        return 1;
    }
    yyjson_arr_iter_init(checks, &iter);
    while ((entry = yyjson_arr_iter_next(&iter)) != NULL) {
        if (!yyjson_is_str(entry)) {
            sl_cli_write_cstr(
                stderr,
                "sloppy: malformed metadata: route health check entries must be strings\n");
            return 1;
        }
        if (route->health_check_count >= SL_CLI_MAX_ROUTE_HEALTH_CHECKS) {
            sl_cli_write_cstr(stderr, "sloppy: too many route health checks in metadata\n");
            return 1;
        }
        route->health_checks[route->health_check_count] =
            (SlCliSpan){yyjson_get_str(entry), yyjson_get_len(entry)};
        route->health_check_count += 1U;
    }
    return 0;
}

static int sl_cli_parse_route_strong_metadata(yyjson_val* value, SlCliRoute* route)
{
    yyjson_val* completeness = yyjson_obj_get(value, "completeness");
    yyjson_val* response = yyjson_obj_get(value, "response");

    if (completeness != NULL && yyjson_is_obj(completeness)) {
        route->completeness = sl_cli_json_span(completeness, "status");
    }
    if (response != NULL && yyjson_is_obj(response)) {
        route->response_kind = sl_cli_json_span(response, "kind");
        route->response_helper = sl_cli_json_span(response, "helper");
        route->response_status = sl_cli_json_uint_or_zero(response, "status");
    }
    if (sl_cli_parse_route_responses(value, route) != 0) {
        return 1;
    }
    if (sl_cli_parse_route_rate_limits(value, route) != 0) {
        return 1;
    }
    if (sl_cli_parse_route_health(value, route) != 0) {
        return 1;
    }
    if (route->response_count == 0U && !sl_cli_span_empty(route->response_kind)) {
        route->response_kinds[0] = route->response_kind;
        route->response_helpers[0] = route->response_helper;
        route->response_body_schemas[0] = sl_cli_json_span(response, "bodySchema");
        route->response_statuses[0] = route->response_status;
        route->response_count = 1U;
    }
    if (route->response_count > 0U) {
        route->response_kind = route->response_kinds[0];
        route->response_helper = route->response_helpers[0];
        route->response_status = route->response_statuses[0];
    }
    if (sl_cli_parse_route_tags(value, route) != 0) {
        return 1;
    }
    if (sl_cli_parse_route_auth(value, route) != 0) {
        return 1;
    }
    sl_cli_parse_route_bindings(value, route);
    sl_cli_parse_route_json_plans(value, route);
    sl_cli_parse_route_effects(value, route);
    return 0;
}

static int sl_cli_parse_routes(yyjson_val* root, SlCliMetadata* metadata)
{
    yyjson_val* routes = yyjson_obj_get(root, "routes");
    yyjson_val* value = NULL;
    yyjson_arr_iter iter;

    if (routes == NULL) {
        return 0;
    }
    if (!yyjson_is_arr(routes)) {
        sl_cli_write_cstr(stderr, "sloppy: malformed metadata: routes must be an array\n");
        return 1;
    }

    yyjson_arr_iter_init(routes, &iter);
    while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
        SlCliRoute route = {0};

        if (!yyjson_is_obj(value)) {
            sl_cli_write_cstr(stderr,
                              "sloppy: malformed metadata: route entries must be objects\n");
            return 1;
        }
        if (metadata->route_count >= SL_CLI_MAX_ROUTES) {
            sl_cli_write_cstr(stderr, "sloppy: too many routes in metadata\n");
            return 1;
        }

        route.method = sl_cli_json_span(value, "method");
        route.kind = sl_cli_json_span(value, "kind");
        if (sl_cli_span_empty(route.kind)) {
            route.kind = sl_cli_span_cstr("http");
        }
        route.pattern = sl_cli_json_span(value, "pattern");
        route.name = sl_cli_json_span(value, "name");
        route.module = sl_cli_json_span(value, "module");
        route.capability = sl_cli_json_span(value, "capability");
        route.summary = sl_cli_json_span(value, "summary");
        route.description = sl_cli_json_span(value, "description");
        route.deprecated_reason = sl_cli_json_span(value, "deprecatedReason");
        route.query_schema = sl_cli_json_span(value, "querySchema");
        route.params_schema = sl_cli_json_span(value, "paramsSchema");
        route.openapi_override = sl_cli_json_span(value, "openapi");
        route.openapi_override_value = yyjson_obj_get(value, "openapi");
        route.output_cache = sl_cli_json_span(value, "outputCache");
        route.output_cache_value = yyjson_obj_get(value, "outputCache");
        route.cache_headers = sl_cli_json_span(value, "cacheHeaders");
        route.cache_headers_value = yyjson_obj_get(value, "cacheHeaders");
        {
            yyjson_val* docs_internal = yyjson_obj_get(value, "docsInternal");
            yyjson_val* docs = yyjson_obj_get(value, "docs");
            route.docs_internal =
                docs_internal != NULL && yyjson_is_bool(docs_internal) &&
                yyjson_get_bool(docs_internal);
            if (docs != NULL && yyjson_is_obj(docs)) {
                yyjson_val* strict = yyjson_obj_get(docs, "strict");
                route.docs_kind = sl_cli_json_span(docs, "kind");
                route.docs_strict =
                    strict != NULL && yyjson_is_bool(strict) && yyjson_get_bool(strict);
            }
        }
        {
            yyjson_val* deprecated = yyjson_obj_get(value, "deprecated");
            route.deprecated =
                deprecated != NULL && yyjson_is_bool(deprecated) && yyjson_get_bool(deprecated);
        }
        {
            yyjson_val* realtime = yyjson_obj_get(value, "realtime");
            if (realtime != NULL && yyjson_is_obj(realtime)) {
                route.realtime_kind = sl_cli_json_span(realtime, "kind");
                route.realtime_transport = sl_cli_json_span(realtime, "transport");
                route.realtime_channel = sl_cli_json_span(realtime, "channelExpression");
            }
        }
        if (sl_cli_parse_route_span_array(value, "consumes", route.consumes,
                                          &route.consume_count, SL_CLI_MAX_ROUTE_TAGS) != 0 ||
            sl_cli_parse_route_span_array(value, "produces", route.produces,
                                          &route.produce_count, SL_CLI_MAX_ROUTE_TAGS) != 0 ||
            sl_cli_parse_route_contract_headers(value, &route) != 0)
        {
            return 1;
        }
        if (sl_cli_parse_route_strong_metadata(value, &route) != 0) {
            return 1;
        }
        route.handler_id = sl_cli_json_handler_id(value);
        sl_cli_parse_source(value, &route.source_path, &route.source_line, &route.source_column);
        route.source_order = metadata->route_count;

        if (sl_cli_span_empty(route.method) || sl_cli_span_empty(route.pattern) ||
            route.handler_id == SL_HANDLER_ID_INVALID)
        {
            sl_cli_write_cstr(stderr, "sloppy: malformed metadata: route requires method, pattern, "
                                      "and handlerId\n");
            return 1;
        }

        metadata->routes[metadata->route_count] = route;
        metadata->route_count += 1U;
    }

    return 0;
}

static int sl_cli_parse_dynamic_routes(yyjson_val* root, SlCliMetadata* metadata)
{
    yyjson_val* routes = yyjson_obj_get(root, "dynamicRoutes");
    yyjson_val* value = NULL;
    yyjson_arr_iter iter;

    if (routes == NULL) {
        return 0;
    }
    if (!yyjson_is_arr(routes)) {
        sl_cli_write_cstr(stderr, "sloppy: malformed metadata: dynamicRoutes must be an array\n");
        return 1;
    }

    yyjson_arr_iter_init(routes, &iter);
    while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
        yyjson_val* method = NULL;
        yyjson_val* pattern = NULL;
        yyjson_val* metadata_value = NULL;
        SlCliRoute route = {0};

        if (!yyjson_is_obj(value)) {
            sl_cli_write_cstr(
                stderr, "sloppy: malformed metadata: dynamic route entries must be objects\n");
            return 1;
        }
        if (metadata->route_count >= SL_CLI_MAX_ROUTES) {
            sl_cli_write_cstr(stderr, "sloppy: too many routes in metadata\n");
            return 1;
        }

        method = yyjson_obj_get(value, "method");
        if (method != NULL && yyjson_is_obj(method)) {
            route.method = sl_cli_json_span(method, "value");
        }
        if (sl_cli_span_empty(route.method)) {
            route.method = sl_cli_span_cstr("<dynamic>");
        }
        pattern = yyjson_obj_get(value, "pattern");
        if (pattern != NULL && yyjson_is_obj(pattern)) {
            route.pattern = sl_cli_json_span(pattern, "value");
        }
        if (sl_cli_span_empty(route.pattern)) {
            route.pattern = sl_cli_span_cstr("<dynamic>");
        }
        route.kind = sl_cli_span_cstr("-");
        route.completeness = sl_cli_span_cstr("dynamic");
        route.dynamic_metadata = true;
        metadata_value = yyjson_obj_get(value, "metadata");
        if (metadata_value != NULL && yyjson_is_obj(metadata_value)) {
            route.dynamic_reason = sl_cli_json_span(metadata_value, "reason");
        }
        sl_cli_parse_source(value, &route.source_path, &route.source_line, &route.source_column);
        route.source_order = metadata->route_count;
        metadata->routes[metadata->route_count] = route;
        metadata->route_count += 1U;
    }

    return 0;
}

static int sl_cli_parse_modules(yyjson_val* root, SlCliMetadata* metadata)
{
    yyjson_val* modules = yyjson_obj_get(root, "modules");
    yyjson_val* value = NULL;
    yyjson_arr_iter iter;

    if (modules == NULL) {
        return 0;
    }
    if (!yyjson_is_arr(modules)) {
        sl_cli_write_cstr(stderr, "sloppy: malformed metadata: modules must be an array\n");
        return 1;
    }

    yyjson_arr_iter_init(modules, &iter);
    while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
        SlCliModule module = {0};
        yyjson_val* deps = NULL;
        yyjson_val* dep = NULL;
        yyjson_arr_iter dep_iter;

        if (!yyjson_is_obj(value)) {
            sl_cli_write_cstr(stderr, "sloppy: malformed module in metadata\n");
            return 1;
        }
        if (metadata->module_count >= SL_CLI_MAX_MODULES) {
            sl_cli_write_cstr(stderr, "sloppy: too many modules in metadata\n");
            return 1;
        }
        module.name = sl_cli_json_span(value, "name");
        deps = yyjson_obj_get(value, "dependencies");
        if (deps != NULL && yyjson_is_arr(deps)) {
            yyjson_arr_iter_init(deps, &dep_iter);
            while ((dep = yyjson_arr_iter_next(&dep_iter)) != NULL) {
                if (yyjson_is_str(dep) && module.dependency_count < SL_CLI_MAX_DEPS) {
                    module.dependencies[module.dependency_count] =
                        (SlCliSpan){yyjson_get_str(dep), yyjson_get_len(dep)};
                    module.dependency_count += 1U;
                }
            }
        }
        metadata->modules[metadata->module_count] = module;
        metadata->module_count += 1U;
    }

    return 0;
}

static int sl_cli_parse_providers(yyjson_val* root, SlCliMetadata* metadata)
{
    yyjson_val* providers = yyjson_obj_get(root, "dataProviders");
    yyjson_val* value = NULL;
    yyjson_arr_iter iter;

    if (providers == NULL) {
        return 0;
    }
    if (!yyjson_is_arr(providers)) {
        sl_cli_write_cstr(stderr, "sloppy: malformed metadata: dataProviders must be an array\n");
        return 1;
    }

    yyjson_arr_iter_init(providers, &iter);
    while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
        SlCliProvider provider = {0};

        if (!yyjson_is_obj(value)) {
            sl_cli_write_cstr(stderr, "sloppy: malformed data provider in metadata\n");
            return 1;
        }
        if (metadata->provider_count >= SL_CLI_MAX_PROVIDERS) {
            sl_cli_write_cstr(stderr, "sloppy: too many data providers in metadata\n");
            return 1;
        }
        provider.token = sl_cli_json_span(value, "token");
        provider.provider = sl_cli_json_span(value, "provider");
        provider.provider_kind = sl_cli_json_span(value, "providerKind");
        provider.capability = sl_cli_json_span(value, "capability");
        provider.required_access = sl_cli_json_span(value, "requiredAccess");
        provider.service = sl_cli_json_span(value, "service");
        provider.database = sl_cli_json_span(value, "database");
        provider.config_key = sl_cli_json_span(value, "configKey");
        metadata->providers[metadata->provider_count] = provider;
        metadata->provider_count += 1U;
    }

    return 0;
}

static int sl_cli_parse_capabilities(yyjson_val* root, SlCliMetadata* metadata)
{
    yyjson_val* capabilities = yyjson_obj_get(root, "capabilities");
    yyjson_val* value = NULL;
    yyjson_arr_iter iter;

    if (capabilities == NULL) {
        return 0;
    }
    if (!yyjson_is_arr(capabilities)) {
        sl_cli_write_cstr(stderr, "sloppy: malformed metadata: capabilities must be an array\n");
        return 1;
    }

    yyjson_arr_iter_init(capabilities, &iter);
    while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
        SlCliCapability capability = {0};

        if (!yyjson_is_obj(value)) {
            sl_cli_write_cstr(stderr, "sloppy: malformed capability in metadata\n");
            return 1;
        }
        if (metadata->capability_count >= SL_CLI_MAX_CAPABILITIES) {
            sl_cli_write_cstr(stderr, "sloppy: too many capabilities in metadata\n");
            return 1;
        }
        capability.token = sl_cli_json_span(value, "token");
        capability.kind = sl_cli_json_span(value, "kind");
        capability.access = sl_cli_json_span(value, "access");
        capability.provider = sl_cli_json_span(value, "provider");
        metadata->capabilities[metadata->capability_count] = capability;
        metadata->capability_count += 1U;
    }

    return 0;
}

static void sl_cli_parse_schema_property(yyjson_val* property_value, SlCliSpan name,
                                         SlCliSchema* schema)
{
    SlCliSchemaProperty property = {0};
    yyjson_val* optional = NULL;
    yyjson_val* items = NULL;

    if (!yyjson_is_obj(property_value) || schema->property_count >= SL_CLI_MAX_SCHEMA_PROPERTIES) {
        return;
    }
    property.name = name;
    property.kind = sl_cli_json_span(property_value, "kind");
    property.format = sl_cli_json_span(property_value, "format");
    optional = yyjson_obj_get(property_value, "optional");
    property.optional = optional != NULL && yyjson_is_bool(optional) && yyjson_get_bool(optional);
    {
        yyjson_val* nullable = yyjson_obj_get(property_value, "nullable");
        yyjson_val* min = yyjson_obj_get(property_value, "min");
        yyjson_val* max = yyjson_obj_get(property_value, "max");
        property.literal_value = yyjson_obj_get(property_value, "value");
        property.nullable =
            nullable != NULL && yyjson_is_bool(nullable) && yyjson_get_bool(nullable);
        property.has_min = min != NULL && yyjson_is_num(min);
        property.has_max = max != NULL && yyjson_is_num(max);
        property.min_value = property.has_min ? yyjson_get_num(min) : 0.0;
        property.max_value = property.has_max ? yyjson_get_num(max) : 0.0;
    }
    items = yyjson_obj_get(property_value, "items");
    if (items != NULL && yyjson_is_obj(items)) {
        property.item_kind = sl_cli_json_span(items, "kind");
    }
    schema->properties[schema->property_count] = property;
    schema->property_count += 1U;
}

static void sl_cli_parse_schema_properties(yyjson_val* schema_value, SlCliSchema* schema)
{
    yyjson_val* definition = yyjson_obj_get(schema_value, "definition");
    yyjson_val* properties = NULL;
    yyjson_val* items = NULL;
    yyjson_obj_iter iter;
    yyjson_val* key = NULL;

    if (definition == NULL || !yyjson_is_obj(definition)) {
        return;
    }
    schema->kind = sl_cli_json_span(definition, "kind");
    {
        yyjson_val* nullable = yyjson_obj_get(definition, "nullable");
        yyjson_val* min = yyjson_obj_get(definition, "min");
        yyjson_val* max = yyjson_obj_get(definition, "max");
        schema->literal_value = yyjson_obj_get(definition, "value");
        schema->nullable =
            nullable != NULL && yyjson_is_bool(nullable) && yyjson_get_bool(nullable);
        schema->has_min = min != NULL && yyjson_is_num(min);
        schema->has_max = max != NULL && yyjson_is_num(max);
        schema->min_value = schema->has_min ? yyjson_get_num(min) : 0.0;
        schema->max_value = schema->has_max ? yyjson_get_num(max) : 0.0;
    }
    items = yyjson_obj_get(definition, "items");
    if (items != NULL && yyjson_is_obj(items)) {
        schema->item_kind = sl_cli_json_span(items, "kind");
    }
    properties = yyjson_obj_get(definition, "properties");
    if (properties == NULL || !yyjson_is_obj(properties)) {
        return;
    }
    iter = yyjson_obj_iter_with(properties);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL) {
        const char* property_name = yyjson_get_str(key);

        if (property_name == NULL) {
            continue;
        }
        sl_cli_parse_schema_property(yyjson_obj_iter_get_val(key),
                                     (SlCliSpan){property_name, yyjson_get_len(key)}, schema);
    }
}

static int sl_cli_parse_schemas(yyjson_val* root, SlCliMetadata* metadata)
{
    yyjson_val* schemas = yyjson_obj_get(root, "schemas");
    yyjson_val* value = NULL;
    yyjson_arr_iter iter;

    if (schemas == NULL) {
        return 0;
    }
    if (!yyjson_is_arr(schemas)) {
        sl_cli_write_cstr(stderr, "sloppy: malformed metadata: schemas must be an array\n");
        return 1;
    }

    yyjson_arr_iter_init(schemas, &iter);
    while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
        SlCliSchema schema = {0};

        if (!yyjson_is_obj(value)) {
            sl_cli_write_cstr(stderr, "sloppy: malformed schema in metadata\n");
            return 1;
        }
        if (metadata->schema_count >= SL_CLI_MAX_SCHEMAS) {
            sl_cli_write_cstr(stderr, "sloppy: too many schemas in metadata\n");
            return 1;
        }
        schema.name = sl_cli_json_span(value, "name");
        sl_cli_parse_source(value, &schema.source_path, &schema.source_line, &schema.source_column);
        sl_cli_parse_schema_properties(value, &schema);
        metadata->schemas[metadata->schema_count] = schema;
        metadata->schema_count += 1U;
    }

    return 0;
}

static int sl_cli_parse_doctor_checks(yyjson_val* root, SlCliMetadata* metadata)
{
    yyjson_val* checks = yyjson_obj_get(root, "doctorChecks");
    yyjson_val* value = NULL;
    yyjson_arr_iter iter;

    if (checks == NULL) {
        return 0;
    }
    if (!yyjson_is_arr(checks)) {
        sl_cli_write_cstr(stderr, "sloppy: malformed metadata: doctorChecks must be an array\n");
        return 1;
    }

    yyjson_arr_iter_init(checks, &iter);
    while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
        SlCliDoctorCheck check = {0};

        if (!yyjson_is_obj(value)) {
            sl_cli_write_cstr(stderr, "sloppy: malformed doctor check in metadata\n");
            return 1;
        }
        if (metadata->doctor_check_count >= SL_CLI_MAX_DOCTOR_CHECKS) {
            sl_cli_write_cstr(stderr, "sloppy: too many doctor checks in metadata\n");
            return 1;
        }
        check.id = sl_cli_json_span(value, "id");
        check.status = sl_cli_json_span(value, "status");
        check.message = sl_cli_json_span(value, "message");
        if (sl_cli_span_empty(check.id) || sl_cli_span_empty(check.status)) {
            sl_cli_write_cstr(stderr, "sloppy: doctor check is missing id or status\n");
            return 1;
        }
        metadata->doctor_checks[metadata->doctor_check_count] = check;
        metadata->doctor_check_count += 1U;
    }

    return 0;
}

static int sl_cli_parse_required_features(yyjson_val* root, SlCliMetadata* metadata)
{
    yyjson_val* features = yyjson_obj_get(root, "requiredFeatures");
    yyjson_val* value = NULL;
    yyjson_arr_iter iter;

    if (features == NULL) {
        return 0;
    }
    if (!yyjson_is_arr(features)) {
        sl_cli_write_cstr(stderr,
                          "sloppy: malformed metadata: requiredFeatures must be an array\n");
        return 1;
    }

    yyjson_arr_iter_init(features, &iter);
    while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
        if (!yyjson_is_str(value)) {
            sl_cli_write_cstr(
                stderr, "sloppy: malformed metadata: requiredFeatures entries must be strings\n");
            return 1;
        }
        if (metadata->required_feature_count >= SL_CLI_MAX_REQUIRED_FEATURES) {
            sl_cli_write_cstr(stderr, "sloppy: too many required features in metadata\n");
            return 1;
        }
        metadata->required_features[metadata->required_feature_count] =
            (SlCliSpan){yyjson_get_str(value), yyjson_get_len(value)};
        metadata->required_feature_count += 1U;
    }

    return 0;
}

static int sl_cli_parse_http_clients(yyjson_val* root, SlCliMetadata* metadata)
{
    yyjson_val* clients = yyjson_obj_get(root, "httpClients");
    yyjson_val* value = NULL;
    yyjson_arr_iter iter;

    if (clients == NULL) {
        return 0;
    }
    if (!yyjson_is_arr(clients)) {
        sl_cli_write_cstr(stderr, "sloppy: malformed metadata: httpClients must be an array\n");
        return 1;
    }

    yyjson_arr_iter_init(clients, &iter);
    while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
        SlCliHttpClient client = {0};
        yyjson_val* strict = NULL;

        if (!yyjson_is_obj(value)) {
            sl_cli_write_cstr(stderr, "sloppy: malformed HTTP client metadata\n");
            return 1;
        }
        if (metadata->http_client_count >= SL_CLI_MAX_HTTP_CLIENTS) {
            sl_cli_write_cstr(stderr, "sloppy: too many HTTP clients in metadata\n");
            return 1;
        }
        client.name = sl_cli_json_span(value, "name");
        client.target = sl_cli_json_span(value, "target");
        if (sl_cli_span_empty(client.target)) {
            client.target = sl_cli_json_span(value, "targetKind");
        }
        client.base_url = sl_cli_json_span(value, "baseUrl");
        strict = yyjson_obj_get(value, "strictNetwork");
        client.strict_network = strict != NULL && yyjson_is_bool(strict) && yyjson_get_bool(strict);
        metadata->http_clients[metadata->http_client_count] = client;
        metadata->http_client_count += 1U;
    }

    return 0;
}

static int sl_cli_parse_config_requirements(yyjson_val* root, SlCliMetadata* metadata)
{
    yyjson_val* configuration = yyjson_obj_get(root, "configuration");
    yyjson_val* requirements = NULL;
    yyjson_val* value = NULL;
    yyjson_arr_iter iter;

    if (configuration == NULL) {
        return 0;
    }
    if (!yyjson_is_obj(configuration)) {
        sl_cli_write_cstr(stderr, "sloppy: malformed metadata: configuration must be an object\n");
        return 1;
    }

    requirements = yyjson_obj_get(configuration, "requirements");
    if (requirements == NULL) {
        return 0;
    }
    if (!yyjson_is_arr(requirements)) {
        sl_cli_write_cstr(
            stderr, "sloppy: malformed metadata: configuration.requirements must be an array\n");
        return 1;
    }

    yyjson_arr_iter_init(requirements, &iter);
    while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
        SlCliConfigRequirement requirement = {0};
        yyjson_val* required = NULL;
        yyjson_val* secret = NULL;

        if (!yyjson_is_obj(value)) {
            sl_cli_write_cstr(stderr, "sloppy: malformed config requirement metadata\n");
            return 1;
        }
        if (metadata->config_requirement_count >= SL_CLI_MAX_CONFIG_REQUIREMENTS) {
            sl_cli_write_cstr(stderr, "sloppy: too many config requirements in metadata\n");
            return 1;
        }

        requirement.key = sl_cli_json_span(value, "key");
        requirement.type = sl_cli_json_span(value, "type");
        requirement.status = sl_cli_json_span(value, "status");
        requirement.source = sl_cli_json_span(value, "source");
        requirement.required_by = sl_cli_json_span(value, "requiredBy");
        requirement.redaction = sl_cli_json_span(value, "redaction");
        required = yyjson_obj_get(value, "required");
        secret = yyjson_obj_get(value, "secret");
        requirement.required =
            required != NULL && yyjson_is_bool(required) && yyjson_get_bool(required);
        requirement.secret = secret != NULL && yyjson_is_bool(secret) && yyjson_get_bool(secret);
        if (sl_cli_span_empty(requirement.key) || sl_cli_span_empty(requirement.status)) {
            sl_cli_write_cstr(stderr, "sloppy: config requirement is missing key or status\n");
            return 1;
        }
        metadata->config_requirements[metadata->config_requirement_count] = requirement;
        metadata->config_requirement_count += 1U;
    }

    return 0;
}

static int sl_cli_parse_auth_metadata(yyjson_val* root, SlCliMetadata* metadata)
{
    yyjson_val* auth = yyjson_obj_get(root, "auth");
    yyjson_val* schemes = NULL;
    yyjson_val* policies = NULL;
    yyjson_val* value = NULL;
    yyjson_arr_iter iter;

    if (auth == NULL) {
        return 0;
    }
    if (!yyjson_is_obj(auth)) {
        sl_cli_write_cstr(stderr, "sloppy: malformed metadata: auth must be an object\n");
        return 1;
    }
    schemes = yyjson_obj_get(auth, "schemes");
    if (schemes != NULL) {
        if (!yyjson_is_arr(schemes)) {
            sl_cli_write_cstr(stderr,
                              "sloppy: malformed metadata: auth.schemes must be an array\n");
            return 1;
        }
        yyjson_arr_iter_init(schemes, &iter);
        while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
            SlCliAuthScheme scheme = {0};
            if (!yyjson_is_obj(value)) {
                continue;
            }
            if (metadata->auth_scheme_count >= SL_CLI_MAX_AUTH_SCHEMES) {
                sl_cli_write_cstr(stderr, "sloppy: too many auth schemes in metadata\n");
                return 1;
            }
            scheme.kind = sl_cli_json_span(value, "kind");
            scheme.name = sl_cli_json_span(value, "name");
            scheme.header = sl_cli_json_span(value, "header");
            scheme.cookie = sl_cli_json_span(value, "cookie");
            scheme.store = sl_cli_json_span(value, "store");
            metadata->auth_schemes[metadata->auth_scheme_count] = scheme;
            metadata->auth_scheme_count += 1U;
        }
    }
    policies = yyjson_obj_get(auth, "policies");
    if (policies != NULL) {
        if (!yyjson_is_arr(policies)) {
            sl_cli_write_cstr(stderr,
                              "sloppy: malformed metadata: auth.policies must be an array\n");
            return 1;
        }
        yyjson_arr_iter_init(policies, &iter);
        while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
            if (!yyjson_is_str(value)) {
                continue;
            }
            if (metadata->auth_policy_count >= SL_CLI_MAX_AUTH_POLICIES) {
                sl_cli_write_cstr(stderr, "sloppy: too many auth policies in metadata\n");
                return 1;
            }
            metadata->auth_policies[metadata->auth_policy_count] =
                (SlCliSpan){yyjson_get_str(value), yyjson_get_len(value)};
            metadata->auth_policy_count += 1U;
        }
    }
    return 0;
}

static int sl_cli_parse_ffi_parameters(yyjson_val* function_value, SlCliFfiFunction* function)
{
    yyjson_val* parameters = yyjson_obj_get(function_value, "parameters");
    yyjson_val* value = NULL;
    yyjson_arr_iter iter;

    if (parameters == NULL) {
        return 0;
    }
    if (!yyjson_is_arr(parameters)) {
        sl_cli_write_cstr(stderr,
                          "sloppy: malformed metadata: native.ffi parameters must be an array\n");
        return 1;
    }
    yyjson_arr_iter_init(parameters, &iter);
    while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
        if (!yyjson_is_str(value)) {
            sl_cli_write_cstr(
                stderr,
                "sloppy: malformed metadata: native.ffi parameter entries must be strings\n");
            return 1;
        }
        if (function->parameter_count >= SL_CLI_MAX_FFI_PARAMETERS) {
            sl_cli_write_cstr(stderr, "sloppy: too many native FFI parameters in metadata\n");
            return 1;
        }
        function->parameters[function->parameter_count] =
            (SlCliSpan){yyjson_get_str(value), yyjson_get_len(value)};
        function->parameter_count += 1U;
    }
    return 0;
}

static int sl_cli_parse_ffi_functions(yyjson_val* library_value, SlCliMetadata* metadata,
                                      SlCliFfiLibrary* library)
{
    yyjson_val* functions = yyjson_obj_get(library_value, "functions");
    yyjson_val* value = NULL;
    yyjson_arr_iter iter;

    if (functions == NULL) {
        return 0;
    }
    if (!yyjson_is_arr(functions)) {
        sl_cli_write_cstr(stderr,
                          "sloppy: malformed metadata: native.ffi functions must be an array\n");
        return 1;
    }

    yyjson_arr_iter_init(functions, &iter);
    while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
        SlCliFfiFunction function = {0};

        if (!yyjson_is_obj(value)) {
            sl_cli_write_cstr(
                stderr, "sloppy: malformed metadata: native.ffi function must be an object\n");
            return 1;
        }
        if (metadata->ffi_function_count >= SL_CLI_MAX_FFI_FUNCTIONS) {
            sl_cli_write_cstr(stderr, "sloppy: too many native FFI functions in metadata\n");
            return 1;
        }
        function.library = library->name;
        function.id = sl_cli_json_span(value, "id");
        function.name = sl_cli_json_span(value, "name");
        function.symbol = sl_cli_json_span(value, "symbol");
        function.convention = sl_cli_json_span(value, "convention");
        function.return_type = sl_cli_json_span(value, "return");
        if (sl_cli_parse_ffi_parameters(value, &function) != 0) {
            return 1;
        }
        metadata->ffi_functions[metadata->ffi_function_count] = function;
        metadata->ffi_function_count += 1U;
        library->function_count += 1U;
    }
    return 0;
}

static int sl_cli_parse_ffi_structs(yyjson_val* native, SlCliMetadata* metadata)
{
    yyjson_val* structs = yyjson_obj_get(native, "ffiStructs");
    yyjson_val* value = NULL;
    yyjson_arr_iter iter;

    if (structs == NULL) {
        return 0;
    }
    if (!yyjson_is_arr(structs)) {
        sl_cli_write_cstr(stderr,
                          "sloppy: malformed metadata: native.ffiStructs must be an array\n");
        return 1;
    }
    yyjson_arr_iter_init(structs, &iter);
    while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
        SlCliFfiStruct layout = {0};
        yyjson_val* fields = NULL;
        yyjson_val* field = NULL;
        yyjson_arr_iter field_iter;

        if (!yyjson_is_obj(value)) {
            sl_cli_write_cstr(
                stderr, "sloppy: malformed metadata: native.ffiStructs entry must be an object\n");
            return 1;
        }
        if (metadata->ffi_struct_count >= SL_CLI_MAX_FFI_STRUCTS) {
            sl_cli_write_cstr(stderr, "sloppy: too many native FFI structs in metadata\n");
            return 1;
        }
        layout.name = sl_cli_json_span(value, "name");
        fields = yyjson_obj_get(value, "fields");
        if (fields == NULL || !yyjson_is_arr(fields)) {
            sl_cli_write_cstr(
                stderr, "sloppy: malformed metadata: native.ffiStructs fields must be an array\n");
            return 1;
        }
        yyjson_arr_iter_init(fields, &field_iter);
        while ((field = yyjson_arr_iter_next(&field_iter)) != NULL) {
            if (!yyjson_is_obj(field)) {
                sl_cli_write_cstr(
                    stderr, "sloppy: malformed metadata: native FFI field must be an object\n");
                return 1;
            }
            if (layout.field_count >= SL_CLI_MAX_FFI_STRUCT_FIELDS) {
                sl_cli_write_cstr(stderr,
                                  "sloppy: too many native FFI struct fields in metadata\n");
                return 1;
            }
            layout.fields[layout.field_count].name = sl_cli_json_span(field, "name");
            layout.fields[layout.field_count].type = sl_cli_json_span(field, "type");
            layout.field_count += 1U;
        }
        metadata->ffi_structs[metadata->ffi_struct_count] = layout;
        metadata->ffi_struct_count += 1U;
    }
    return 0;
}

static int sl_cli_parse_ffi_metadata(yyjson_val* root, SlCliMetadata* metadata)
{
    yyjson_val* native = yyjson_obj_get(root, "native");
    yyjson_val* ffi = NULL;
    yyjson_val* value = NULL;
    yyjson_arr_iter iter;

    if (native == NULL) {
        return 0;
    }
    if (!yyjson_is_obj(native)) {
        sl_cli_write_cstr(stderr, "sloppy: malformed metadata: native must be an object\n");
        return 1;
    }

    ffi = yyjson_obj_get(native, "ffi");
    if (ffi != NULL) {
        if (!yyjson_is_arr(ffi)) {
            sl_cli_write_cstr(stderr, "sloppy: malformed metadata: native.ffi must be an array\n");
            return 1;
        }
        yyjson_arr_iter_init(ffi, &iter);
        while ((value = yyjson_arr_iter_next(&iter)) != NULL) {
            SlCliFfiLibrary library = {0};

            if (!yyjson_is_obj(value)) {
                sl_cli_write_cstr(
                    stderr, "sloppy: malformed metadata: native.ffi entry must be an object\n");
                return 1;
            }
            if (metadata->ffi_library_count >= SL_CLI_MAX_FFI_LIBRARIES) {
                sl_cli_write_cstr(stderr, "sloppy: too many native FFI libraries in metadata\n");
                return 1;
            }
            library.name = sl_cli_json_span(value, "name");
            library.convention = sl_cli_json_span(value, "convention");
            library.first_function = metadata->ffi_function_count;
            if (sl_cli_parse_ffi_functions(value, metadata, &library) != 0) {
                return 1;
            }
            metadata->ffi_libraries[metadata->ffi_library_count] = library;
            metadata->ffi_library_count += 1U;
        }
    }

    return sl_cli_parse_ffi_structs(native, metadata);
}

static void sl_cli_parse_route_dispatch(yyjson_val* root, SlCliMetadata* metadata)
{
    yyjson_val* dispatch = yyjson_obj_get(root, "routeDispatch");
    yyjson_val* artifact = NULL;
    yyjson_val* stats = NULL;
    yyjson_val* fallback = NULL;
    yyjson_val* json_metadata = NULL;
    yyjson_val* json_request = NULL;
    yyjson_val* json_response = NULL;
    yyjson_val* url_generation = NULL;
    yyjson_val* classic_available = NULL;

    if (dispatch == NULL || !yyjson_is_obj(dispatch)) {
        return;
    }

    metadata->route_dispatch.present = true;
    metadata->route_dispatch.mode = sl_cli_json_span(dispatch, "mode");
    metadata->route_dispatch.route_count = sl_cli_json_uint_or_zero(dispatch, "routeCount");
    metadata->route_dispatch.endpoint_count = sl_cli_json_uint_or_zero(dispatch, "endpointCount");
    metadata->route_dispatch.static_routes = sl_cli_json_uint_or_zero(dispatch, "staticRoutes");
    metadata->route_dispatch.parameter_routes =
        sl_cli_json_uint_or_zero(dispatch, "parameterRoutes");
    metadata->route_dispatch.catch_all_routes =
        sl_cli_json_uint_or_zero(dispatch, "catchAllRoutes");
    metadata->route_dispatch.native_no_js_endpoints =
        sl_cli_json_uint_or_zero(dispatch, "nativeNoJsEndpoints");
    metadata->route_dispatch.url_writers = sl_cli_json_uint_or_zero(dispatch, "urlWriters");

    url_generation = yyjson_obj_get(dispatch, "urlGeneration");
    metadata->route_dispatch.url_generation =
        url_generation != NULL && yyjson_is_bool(url_generation) && yyjson_get_bool(url_generation);

    artifact = yyjson_obj_get(dispatch, "artifact");
    if (artifact != NULL && yyjson_is_obj(artifact)) {
        metadata->route_dispatch.artifact_kind = sl_cli_json_span(artifact, "kind");
        metadata->route_dispatch.artifact_path = sl_cli_json_span(artifact, "path");
        metadata->route_dispatch.artifact_hash = sl_cli_json_span(artifact, "hash");
        metadata->route_dispatch.artifact_reason = sl_cli_json_span(artifact, "reason");
    }

    stats = yyjson_obj_get(dispatch, "dispatchStats");
    if (stats != NULL && yyjson_is_obj(stats)) {
        metadata->route_dispatch.exact_static_paths =
            sl_cli_json_uint_or_zero(stats, "exactStaticPaths");
        metadata->route_dispatch.parameter_candidate_buckets =
            sl_cli_json_uint_or_zero(stats, "parameterCandidateBuckets");
        metadata->route_dispatch.segment_trie_nodes =
            sl_cli_json_uint_or_zero(stats, "segmentTrieNodes");
    }

    fallback = yyjson_obj_get(dispatch, "fallback");
    if (fallback != NULL && yyjson_is_obj(fallback)) {
        classic_available = yyjson_obj_get(fallback, "classicAvailable");
        metadata->route_dispatch.classic_fallback_available = classic_available != NULL &&
                                                              yyjson_is_bool(classic_available) &&
                                                              yyjson_get_bool(classic_available);
        metadata->route_dispatch.dynamic_fallback_routes =
            sl_cli_json_uint_or_zero(fallback, "dynamicRoutes");
        metadata->route_dispatch.partial_fallback_routes =
            sl_cli_json_uint_or_zero(fallback, "partialRoutes");
    }

    json_metadata = yyjson_obj_get(dispatch, "json");
    if (json_metadata != NULL && yyjson_is_obj(json_metadata)) {
        json_request = yyjson_obj_get(json_metadata, "request");
        if (json_request != NULL && yyjson_is_obj(json_request)) {
            metadata->route_dispatch.json_request_native_routes =
                sl_cli_json_uint_or_zero(json_request, "native");
            metadata->route_dispatch.json_request_generic_routes =
                sl_cli_json_uint_or_zero(json_request, "generic");
            metadata->route_dispatch.json_request_fallback_routes =
                sl_cli_json_uint_or_zero(json_request, "fallback");
            metadata->route_dispatch.json_request_materialized_routes =
                sl_cli_json_uint_or_zero(json_request, "materialized");
        }
        json_response = yyjson_obj_get(json_metadata, "response");
        if (json_response != NULL && yyjson_is_obj(json_response)) {
            metadata->route_dispatch.json_response_native_routes =
                sl_cli_json_uint_or_zero(json_response, "native");
            metadata->route_dispatch.json_response_generic_routes =
                sl_cli_json_uint_or_zero(json_response, "generic");
            metadata->route_dispatch.json_response_fallback_routes =
                sl_cli_json_uint_or_zero(json_response, "fallback");
        }
    }
}

static int sl_cli_parse_plan_kind(yyjson_val* root, SlCliMetadata* metadata)
{
    yyjson_val* kind = yyjson_obj_get(root, "kind");

    if (kind == NULL || yyjson_is_null(kind)) {
        metadata->kind = sl_cli_span_cstr("web");
        return 0;
    }
    if (!yyjson_is_str(kind)) {
        sl_cli_write_cstr(stderr, "sloppy: malformed metadata: kind must be web or program\n");
        return 1;
    }

    metadata->kind = (SlCliSpan){yyjson_get_str(kind), yyjson_get_len(kind)};
    if (sl_cli_span_equal_cstr(metadata->kind, "web") ||
        sl_cli_span_equal_cstr(metadata->kind, "program"))
    {
        return 0;
    }

    sl_cli_write_cstr(stderr, "sloppy: malformed metadata: kind must be web or program\n");
    return 1;
}

static int sl_cli_validate_native_plan(const char* path, SlBytes json, SlArena* arena)
{
    SlPlan plan = {0};
    SlDiag diag = {0};
    SlPlanParseOptions options = {sl_str_from_cstr(path)};
    SlStatus status = sl_plan_parse_json(arena, json, &options, &plan, &diag);

    if (!sl_status_is_ok(status)) {
        sl_cli_write_cstr(stderr, "sloppy: invalid app plan metadata: ");
        if (!sl_str_is_empty(diag.message)) {
            sl_cli_write_span(stderr, sl_cli_span_from_str(diag.message));
        }
        else {
            sl_cli_write_cstr(stderr, "native Plan v1 validation failed");
        }
        for (size_t index = 0U; index < diag.hint_count; index += 1U) {
            if (!sl_str_is_empty(diag.hints[index])) {
                sl_cli_write_cstr(stderr, "\n");
                sl_cli_write_span(stderr, sl_cli_span_from_str(diag.hints[index]));
            }
        }
        sl_cli_write_cstr(stderr, "\n");
        return 1;
    }

    return 0;
}

static int sl_cli_load_metadata(const char* path, unsigned char* json_storage, SlArena* plan_arena,
                                bool validate_native_plan, yyjson_doc** out_doc,
                                SlCliMetadata* out_metadata)
{
    SlBytes json = {0};
    yyjson_read_err error = {0};
    yyjson_val* root = NULL;

    if (sl_cli_read_file(path, json_storage, SL_CLI_FILE_MAX_BYTES, &json) != 0) {
        return 1;
    }

    if (validate_native_plan && sl_cli_validate_native_plan(path, json, plan_arena) != 0) {
        return 1;
    }

    *out_doc = yyjson_read_opts((char*)json.ptr, json.length, 0U, NULL, &error);
    if (*out_doc == NULL) {
        sl_cli_write_error_with_value("sloppy: malformed metadata JSON: ", error.msg, "\n");
        return 1;
    }

    root = yyjson_doc_get_root(*out_doc);
    if (!yyjson_is_obj(root)) {
        sl_cli_write_cstr(stderr, "sloppy: malformed metadata: root must be an object\n");
        return 1;
    }

    *out_metadata = (SlCliMetadata){0};
    if (sl_cli_parse_plan_kind(root, out_metadata) != 0) {
        return 1;
    }
    {
        yyjson_val* completeness = yyjson_obj_get(root, "completeness");
        if (completeness != NULL && yyjson_is_obj(completeness)) {
            out_metadata->completeness = sl_cli_json_span(completeness, "status");
        }
    }
    if (sl_cli_parse_handlers(root, out_metadata) != 0 ||
        sl_cli_parse_routes(root, out_metadata) != 0 ||
        sl_cli_parse_dynamic_routes(root, out_metadata) != 0 ||
        sl_cli_parse_modules(root, out_metadata) != 0 ||
        sl_cli_parse_providers(root, out_metadata) != 0 ||
        sl_cli_parse_capabilities(root, out_metadata) != 0 ||
        sl_cli_parse_schemas(root, out_metadata) != 0 ||
        sl_cli_parse_doctor_checks(root, out_metadata) != 0 ||
        sl_cli_parse_required_features(root, out_metadata) != 0 ||
        sl_cli_parse_http_clients(root, out_metadata) != 0 ||
        sl_cli_parse_config_requirements(root, out_metadata) != 0 ||
        sl_cli_parse_auth_metadata(root, out_metadata) != 0 ||
        sl_cli_parse_ffi_metadata(root, out_metadata) != 0)
    {
        return 1;
    }
    sl_cli_parse_route_dispatch(root, out_metadata);

    return 0;
}

static const SlCliHandler* sl_cli_find_handler(const SlCliMetadata* metadata, SlHandlerId id)
{
    size_t index = 0U;

    for (index = 0U; index < metadata->handler_count; index += 1U) {
        if (metadata->handlers[index].id == id) {
            return &metadata->handlers[index];
        }
    }

    return NULL;
}

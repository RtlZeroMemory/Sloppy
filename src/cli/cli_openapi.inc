/*
 * src/cli/cli_openapi.inc
 *
 * Private OpenAPI command emitter.
 * Included only by src/main.c to preserve existing static helper visibility.
 */
static bool sl_cli_openapi_path(char* buffer, size_t capacity, SlCliSpan pattern,
                                SlCliSpan* openapi_path)
{
    SlStringBuilder builder = {0};
    SlStr view = {0};
    size_t index = 0U;
    SlStatus status;

    if (buffer == NULL || capacity == 0U || openapi_path == NULL ||
        (pattern.ptr == NULL && pattern.length != 0U))
    {
        return false;
    }

    status = sl_string_builder_init_fixed(&builder, buffer, capacity);
    if (!sl_status_is_ok(status)) {
        return false;
    }

    while (index < pattern.length) {
        if (pattern.ptr[index] == '{') {
            status = sl_string_builder_append_char(&builder, pattern.ptr[index]);
            if (!sl_status_is_ok(status)) {
                return false;
            }
            index += 1U;

            while (index < pattern.length && pattern.ptr[index] != '}' && pattern.ptr[index] != ':')
            {
                status = sl_string_builder_append_char(&builder, pattern.ptr[index]);
                if (!sl_status_is_ok(status)) {
                    return false;
                }
                index += 1U;
            }

            while (index < pattern.length && pattern.ptr[index] != '}') {
                index += 1U;
            }
            if (index >= pattern.length) {
                return false;
            }
        }
        status = sl_string_builder_append_char(&builder, pattern.ptr[index]);
        if (!sl_status_is_ok(status)) {
            return false;
        }
        index += 1U;
    }

    status = sl_string_builder_view_with_nul(&builder, &view);
    if (!sl_status_is_ok(status)) {
        return false;
    }

    *openapi_path = sl_cli_span_from_str(view);
    return true;
}

static SlCliSpan sl_cli_openapi_type_for_kind(SlCliSpan kind)
{
    if (sl_cli_span_equal_cstr(kind, "int") || sl_cli_span_equal_cstr(kind, "integer")) {
        return sl_cli_span_cstr("integer");
    }
    if (sl_cli_span_equal_cstr(kind, "number") || sl_cli_span_equal_cstr(kind, "float")) {
        return sl_cli_span_cstr("number");
    }
    if (sl_cli_span_equal_cstr(kind, "boolean") || sl_cli_span_equal_cstr(kind, "bool")) {
        return sl_cli_span_cstr("boolean");
    }
    if (sl_cli_span_equal_cstr(kind, "array")) {
        return sl_cli_span_cstr("array");
    }
    if (sl_cli_span_equal_cstr(kind, "object")) {
        return sl_cli_span_cstr("object");
    }
    return sl_cli_span_cstr("string");
}

typedef struct SlCliOpenApiPolicy
{
    size_t routes_total;
    size_t routes_included;
    size_t routes_omitted;
    size_t operations_complete;
    size_t operations_partial;
    size_t missing_count;
} SlCliOpenApiPolicy;

static void sl_cli_openapi_emit_parameter(FILE* file, SlCliSpan name, SlCliSpan location,
                                          SlCliSpan type, bool required,
                                          SlCliSpan description, bool* first)
{
    if (!*first) {
        sl_cli_write_cstr(file, ",");
    }
    sl_cli_write_cstr(file, "\n          { \"name\": ");
    sl_cli_json_escape(file, name);
    sl_cli_write_cstr(file, ", \"in\": ");
    sl_cli_json_escape(file, location);
    sl_cli_write_cstr(file, ", \"required\": ");
    sl_cli_write_cstr(file, required ? "true" : "false");
    sl_cli_write_cstr(file, ", \"schema\": { \"type\": ");
    sl_cli_json_escape(file, sl_cli_openapi_type_for_kind(type));
    if (sl_cli_span_equal_cstr(type, "uuid")) {
        sl_cli_write_cstr(file, ", \"format\": \"uuid\"");
    }
    else if (sl_cli_span_equal_cstr(type, "alpha")) {
        sl_cli_write_cstr(file, ", \"pattern\": \"^[A-Za-z]+$\"");
    }
    sl_cli_write_cstr(file, " }");
    if (sl_cli_span_equal_cstr(location, "path") && !sl_cli_span_empty(type)) {
        sl_cli_write_cstr(file, ", \"x-slop-constraint\": ");
        sl_cli_json_escape(file, type);
    }
    if (!sl_cli_span_empty(description)) {
        sl_cli_write_cstr(file, ", \"description\": ");
        sl_cli_json_escape(file, description);
    }
    sl_cli_write_cstr(file, " }");
    *first = false;
}

typedef struct SlCliOpenApiParameterKey
{
    SlCliSpan name;
    SlCliSpan location;
} SlCliOpenApiParameterKey;

static bool sl_cli_openapi_parameter_seen(const SlCliOpenApiParameterKey* emitted,
                                          size_t emitted_count, SlCliSpan name,
                                          SlCliSpan location)
{
    size_t index = 0U;

    for (index = 0U; index < emitted_count; index += 1U) {
        if (sl_cli_span_equal(emitted[index].name, name) &&
            sl_cli_span_equal(emitted[index].location, location))
        {
            return true;
        }
    }
    return false;
}

static void sl_cli_openapi_emit_unique_parameter(FILE* file,
                                                 SlCliOpenApiParameterKey* emitted,
                                                 size_t* emitted_count, SlCliSpan name,
                                                 SlCliSpan location, SlCliSpan type,
                                                 bool required, SlCliSpan description,
                                                 bool* first)
{
    if (emitted == NULL || emitted_count == NULL ||
        sl_cli_openapi_parameter_seen(emitted, *emitted_count, name, location))
    {
        return;
    }
    sl_cli_openapi_emit_parameter(file, name, location, type, required, description, first);
    if (*emitted_count < ((size_t)SL_CLI_MAX_ROUTE_BINDINGS * 4U)) {
        emitted[*emitted_count] = (SlCliOpenApiParameterKey){name, location};
        *emitted_count += 1U;
    }
}

static void sl_cli_openapi_emit_field_prefix(FILE* out, bool* first)
{
    sl_cli_write_cstr(out, *first ? "" : ",");
    sl_cli_write_cstr(out, "\n        ");
    *first = false;
}

static const SlCliSchema* sl_cli_openapi_find_schema(const SlCliMetadata* metadata, SlCliSpan name)
{
    size_t index = 0U;

    if (sl_cli_span_empty(name)) {
        return NULL;
    }
    for (index = 0U; index < metadata->schema_count; index += 1U) {
        if (sl_cli_span_equal(metadata->schemas[index].name, name)) {
            return &metadata->schemas[index];
        }
    }
    return NULL;
}

static const SlCliSchemaProperty* sl_cli_openapi_find_schema_property(
    const SlCliSchema* schema, SlCliSpan name)
{
    size_t index = 0U;

    if (schema == NULL || sl_cli_span_empty(name)) {
        return NULL;
    }
    for (index = 0U; index < schema->property_count; index += 1U) {
        if (sl_cli_span_equal(schema->properties[index].name, name)) {
            return &schema->properties[index];
        }
    }
    return NULL;
}

static bool sl_cli_openapi_pattern_has_typed_parameter(SlCliSpan pattern)
{
    size_t index = 0U;

    while (index < pattern.length) {
        if (pattern.ptr[index] == '{') {
            while (index < pattern.length && pattern.ptr[index] != '}') {
                if (pattern.ptr[index] == ':') {
                    return true;
                }
                index += 1U;
            }
        }
        index += 1U;
    }

    return false;
}

static bool sl_cli_openapi_route_has_validation(const SlCliRoute* route)
{
    size_t index = 0U;

    if (sl_cli_openapi_pattern_has_typed_parameter(route->pattern)) {
        return true;
    }
    for (index = 0U; index < route->binding_count; index += 1U) {
        if (sl_cli_span_equal_cstr(route->binding_kinds[index], "body.json") ||
            sl_cli_span_equal_cstr(route->binding_kinds[index], "body.form") ||
            sl_cli_span_equal_cstr(route->binding_kinds[index], "body.multipart"))
        {
            return true;
        }
        if ((sl_cli_span_equal_cstr(route->binding_kinds[index], "query") ||
             sl_cli_span_equal_cstr(route->binding_kinds[index], "header")) &&
            !sl_cli_span_empty(route->binding_schemas[index]))
        {
            return true;
        }
    }

    return false;
}

static bool sl_cli_openapi_route_has_known_json_body_schema(const SlCliMetadata* metadata,
                                                           const SlCliRoute* route)
{
    size_t index = 0U;

    for (index = 0U; index < route->binding_count; index += 1U) {
        if (sl_cli_span_equal_cstr(route->binding_kinds[index], "body.json")) {
            return sl_cli_openapi_find_schema(metadata, route->binding_schemas[index]) != NULL;
        }
    }
    return true;
}

static bool sl_cli_openapi_route_has_known_json_response_schema(const SlCliMetadata* metadata,
                                                               const SlCliRoute* route)
{
    size_t index = 0U;

    if (route->response_count == 0U && sl_cli_span_equal_cstr(route->response_kind, "json")) {
        return sl_cli_openapi_find_schema(metadata, route->response_body_schemas[0]) != NULL;
    }
    for (index = 0U; index < route->response_count; index += 1U) {
        if (sl_cli_span_equal_cstr(route->response_kinds[index], "json") &&
            sl_cli_openapi_find_schema(metadata, route->response_body_schemas[index]) == NULL)
        {
            return false;
        }
    }
    return true;
}

static bool sl_cli_openapi_schema_reference_known(const SlCliMetadata* metadata, SlCliSpan schema)
{
    return sl_cli_span_empty(schema) || sl_cli_openapi_find_schema(metadata, schema) != NULL;
}

static bool sl_cli_openapi_override_complete(const SlCliRoute* route)
{
    yyjson_val* responses = route != NULL && route->openapi_override_value != NULL &&
                                    yyjson_is_obj(route->openapi_override_value)
                                ? yyjson_obj_get(route->openapi_override_value, "responses")
                                : NULL;

    return responses != NULL && yyjson_is_obj(responses) && yyjson_obj_size(responses) > 0U;
}

static bool sl_cli_openapi_route_complete(const SlCliMetadata* metadata, const SlCliRoute* route)
{
    size_t index = 0U;

    if (sl_cli_openapi_override_complete(route)) {
        return true;
    }
    if (route->dynamic_metadata || sl_cli_span_empty(route->response_kind) ||
        !sl_cli_span_equal_cstr(route->completeness, "complete") ||
        !sl_cli_openapi_route_has_known_json_body_schema(metadata, route) ||
        !sl_cli_openapi_route_has_known_json_response_schema(metadata, route) ||
        (route->auth_required && metadata->auth_scheme_count == 0U) ||
        !sl_cli_openapi_schema_reference_known(metadata, route->query_schema) ||
        !sl_cli_openapi_schema_reference_known(metadata, route->params_schema))
    {
        return false;
    }
    for (index = 0U; index < route->header_count; index += 1U) {
        if (!sl_cli_openapi_schema_reference_known(metadata, route->header_schemas[index])) {
            return false;
        }
    }
    return true;
}

static void sl_cli_openapi_parameters(FILE* file, const SlCliMetadata* metadata,
                                      const SlCliRoute* route)
{
    size_t index = 0U;
    bool first = true;
    SlCliOpenApiParameterKey emitted[SL_CLI_MAX_ROUTE_BINDINGS * 4U] = {0};
    size_t emitted_count = 0U;
    const SlCliSchema* query_schema = sl_cli_openapi_find_schema(metadata, route->query_schema);
    const SlCliSchema* params_schema = sl_cli_openapi_find_schema(metadata, route->params_schema);

    sl_cli_write_cstr(file, "[");
    while (index < route->pattern.length) {
        if (route->pattern.ptr[index] == '{') {
            size_t name_start = index + 1U;
            size_t name_end = name_start;
            size_t type_start = 0U;
            size_t type_end = 0U;
            SlCliSpan type = {0};
            SlCliSpan name = {0};
            const SlCliSchemaProperty* property = NULL;

            while (name_end < route->pattern.length && route->pattern.ptr[name_end] != '}' &&
                   route->pattern.ptr[name_end] != ':')
            {
                name_end += 1U;
            }
            if (name_end < route->pattern.length && route->pattern.ptr[name_end] == ':') {
                type_start = name_end + 1U;
                type_end = type_start;
                while (type_end < route->pattern.length && route->pattern.ptr[type_end] != '}') {
                    type_end += 1U;
                }
                type = (SlCliSpan){&route->pattern.ptr[type_start], type_end - type_start};
            }
            name = (SlCliSpan){&route->pattern.ptr[name_start], name_end - name_start};
            property = sl_cli_openapi_find_schema_property(params_schema, name);
            if (property != NULL && !sl_cli_span_empty(property->kind)) {
                type = property->kind;
            }
            sl_cli_openapi_emit_unique_parameter(file, emitted, &emitted_count, name,
                                                 sl_cli_span_cstr("path"), type, true,
                                                 (SlCliSpan){0}, &first);
        }
        index += 1U;
    }
    if (params_schema != NULL) {
        for (index = 0U; index < params_schema->property_count; index += 1U) {
            const SlCliSchemaProperty* property = &params_schema->properties[index];
            sl_cli_openapi_emit_unique_parameter(file, emitted, &emitted_count, property->name,
                                                 sl_cli_span_cstr("path"), property->kind, true,
                                                 (SlCliSpan){0}, &first);
        }
    }
    if (query_schema != NULL) {
        for (index = 0U; index < query_schema->property_count; index += 1U) {
            const SlCliSchemaProperty* property = &query_schema->properties[index];
            sl_cli_openapi_emit_unique_parameter(file, emitted, &emitted_count, property->name,
                                                 sl_cli_span_cstr("query"), property->kind,
                                                 !property->optional, (SlCliSpan){0}, &first);
        }
    }
    for (index = 0U; index < route->header_count; index += 1U) {
        const SlCliSchema* header_schema =
            sl_cli_openapi_find_schema(metadata, route->header_schemas[index]);
        SlCliSpan kind = header_schema != NULL ? header_schema->kind : sl_cli_span_cstr("string");
        sl_cli_openapi_emit_unique_parameter(file, emitted, &emitted_count,
                                             route->header_names[index],
                                             sl_cli_span_cstr("header"), kind,
                                             route->header_required[index],
                                             route->header_descriptions[index], &first);
    }
    for (index = 0U; index < route->binding_count; index += 1U) {
        if (sl_cli_span_equal_cstr(route->binding_kinds[index], "query")) {
            sl_cli_openapi_emit_unique_parameter(file, emitted, &emitted_count,
                                                 route->binding_names[index],
                                                 sl_cli_span_cstr("query"),
                                                 route->binding_schemas[index], false,
                                                 (SlCliSpan){0}, &first);
        }
        else if (sl_cli_span_equal_cstr(route->binding_kinds[index], "header")) {
            sl_cli_openapi_emit_unique_parameter(file, emitted, &emitted_count,
                                                 route->binding_names[index],
                                                 sl_cli_span_cstr("header"),
                                                 route->binding_schemas[index], false,
                                                 (SlCliSpan){0}, &first);
        }
        else if (sl_cli_span_equal_cstr(route->binding_kinds[index], "cookie") &&
                 !sl_cli_span_empty(route->binding_names[index]))
        {
            sl_cli_openapi_emit_unique_parameter(file, emitted, &emitted_count,
                                                 route->binding_names[index],
                                                 sl_cli_span_cstr("cookie"),
                                                 sl_cli_span_cstr("string"), false,
                                                 (SlCliSpan){0}, &first);
        }
    }
    if (!first) {
        sl_cli_write_cstr(file, "\n        ");
    }
    sl_cli_write_cstr(file, "]");
}

static int sl_cli_openapi_prepare_paths(const SlCliMetadata* metadata,
                                        char path_buffers[SL_CLI_MAX_ROUTES][512],
                                        SlCliSpan openapi_paths[SL_CLI_MAX_ROUTES])
{
    size_t index = 0U;

    for (index = 0U; index < metadata->route_count; index += 1U) {
        if (metadata->routes[index].dynamic_metadata || metadata->routes[index].docs_internal) {
            openapi_paths[index] = (SlCliSpan){0};
            continue;
        }
        if (!sl_cli_openapi_path(path_buffers[index], sizeof(path_buffers[index]),
                                 metadata->routes[index].pattern, &openapi_paths[index]))
        {
            sl_cli_write_cstr(stderr, "sloppy openapi: malformed route pattern in metadata: ");
            sl_cli_write_span(stderr, metadata->routes[index].pattern);
            sl_cli_write_cstr(stderr, "\n");
            return 1;
        }
    }

    return 0;
}

static bool sl_cli_openapi_path_seen(const SlCliSpan openapi_paths[SL_CLI_MAX_ROUTES],
                                     size_t current)
{
    size_t index = 0U;

    for (index = 0U; index < current; index += 1U) {
        if (sl_cli_span_empty(openapi_paths[index])) {
            continue;
        }
        if (sl_cli_span_equal(openapi_paths[index], openapi_paths[current])) {
            return true;
        }
    }

    return false;
}

static void sl_cli_openapi_emit_literal_value(FILE* out, yyjson_val* value)
{
    if (value == NULL || yyjson_is_null(value)) {
        sl_cli_write_cstr(out, "null");
    }
    else if (yyjson_is_str(value)) {
        sl_cli_json_escape(out, (SlCliSpan){yyjson_get_str(value), yyjson_get_len(value)});
    }
    else if (yyjson_is_bool(value)) {
        sl_cli_write_cstr(out, yyjson_get_bool(value) ? "true" : "false");
    }
    else if (yyjson_is_num(value)) {
        sl_cli_write_cstr(out, yyjson_get_raw(value));
    }
    else {
        sl_cli_write_cstr(out, "null");
    }
}

static void sl_cli_openapi_emit_capabilities(FILE* out, const SlCliRoute* route)
{
    size_t index = 0U;

    sl_cli_write_cstr(out, "[");
    for (index = 0U; index < route->effect_count; index += 1U) {
        sl_cli_write_cstr(out, index == 0U ? "" : ", ");
        sl_cli_write_cstr(out, "{ \"provider\": ");
        sl_cli_json_escape(out, route->effect_providers[index]);
        sl_cli_write_cstr(out, ", \"providerKind\": ");
        sl_cli_json_escape(out, route->effect_provider_kinds[index]);
        sl_cli_write_cstr(out, ", \"access\": ");
        sl_cli_json_escape(out, route->effect_accesses[index]);
        sl_cli_write_cstr(out, ", \"operation\": ");
        sl_cli_json_escape(out, route->effect_operations[index]);
        sl_cli_write_cstr(out, " }");
    }
    sl_cli_write_cstr(out, "]");
}

static void sl_cli_openapi_emit_request_body(FILE* out, const SlCliMetadata* metadata,
                                             const SlCliRoute* route)
{
    size_t index = 0U;

    for (index = 0U; index < route->binding_count; index += 1U) {
        if (sl_cli_span_equal_cstr(route->binding_kinds[index], "body.form")) {
            sl_cli_write_cstr(out, ",\n        \"requestBody\": {\n"
                                   "          \"required\": true,\n"
                                   "          \"content\": { "
                                   "\"application/x-www-form-urlencoded\": { \"schema\": "
                                   "{ \"type\": \"object\", \"additionalProperties\": "
                                   "{ \"type\": \"string\" } } } }\n        }");
            return;
        }
        if (sl_cli_span_equal_cstr(route->binding_kinds[index], "body.multipart")) {
            sl_cli_write_cstr(out, ",\n        \"requestBody\": {\n"
                                   "          \"required\": true,\n"
                                   "          \"content\": { "
                                   "\"multipart/form-data\": { \"schema\": "
                                   "{ \"type\": \"object\", \"additionalProperties\": true } } }"
                                   "\n        }");
            return;
        }
        if (!sl_cli_span_equal_cstr(route->binding_kinds[index], "body.json")) {
            continue;
        }
        sl_cli_write_cstr(out, ",\n        \"requestBody\": {\n"
                               "          \"required\": true,\n"
                               "          \"content\": { ");
        sl_cli_json_escape(out, route->consume_count > 0U ? route->consumes[0]
                                                          : sl_cli_span_cstr("application/json"));
        sl_cli_write_cstr(out, ": { \"schema\": ");
        if (sl_cli_openapi_find_schema(metadata, route->binding_schemas[index]) != NULL) {
            sl_cli_write_cstr(out, "{ \"$ref\": \"#/components/schemas/");
            sl_cli_write_span(out, route->binding_schemas[index]);
            sl_cli_write_cstr(out, "\" }");
        }
        else {
            sl_cli_write_cstr(out, "{ \"x-slop-partial\": \"request body schema unknown\" }");
        }
        sl_cli_write_cstr(out, " } }\n        }");
        return;
    }
}

static SlCliSpan sl_cli_openapi_content_type_for_response_kind(SlCliSpan kind)
{
    if (sl_cli_span_equal_cstr(kind, "json")) {
        return sl_cli_span_cstr("application/json");
    }
    if (sl_cli_span_equal_cstr(kind, "problem")) {
        return sl_cli_span_cstr("application/problem+json");
    }
    if (sl_cli_span_equal_cstr(kind, "text")) {
        return sl_cli_span_cstr("text/plain");
    }
    if (sl_cli_span_equal_cstr(kind, "html")) {
        return sl_cli_span_cstr("text/html");
    }
    if (sl_cli_span_equal_cstr(kind, "bytes")) {
        return sl_cli_span_cstr("application/octet-stream");
    }
    if (sl_cli_span_equal_cstr(kind, "stream")) {
        return sl_cli_span_cstr("application/octet-stream");
    }
    return (SlCliSpan){0};
}

static void sl_cli_openapi_emit_problem_schema(FILE* out)
{
    sl_cli_write_cstr(out, "{ \"type\": \"object\", \"properties\": { "
                           "\"status\": { \"type\": \"integer\" }, "
                           "\"title\": { \"type\": \"string\" }, "
                           "\"detail\": { \"type\": \"string\" } } }");
}

static void sl_cli_openapi_emit_response_schema(FILE* out, const SlCliMetadata* metadata,
                                                SlCliSpan kind, SlCliSpan body_schema)
{
    if (sl_cli_span_equal_cstr(kind, "json")) {
        if (sl_cli_openapi_find_schema(metadata, body_schema) != NULL) {
            sl_cli_write_cstr(out, "{ \"$ref\": \"#/components/schemas/");
            sl_cli_write_span(out, body_schema);
            sl_cli_write_cstr(out, "\" }");
        }
        else {
            sl_cli_write_cstr(out,
                              "{ \"x-slop-partial\": \"response schema shape not declared\" }");
        }
        return;
    }
    if (sl_cli_span_equal_cstr(kind, "problem")) {
        sl_cli_openapi_emit_problem_schema(out);
        return;
    }
    if (sl_cli_span_equal_cstr(kind, "text") || sl_cli_span_equal_cstr(kind, "html")) {
        sl_cli_write_cstr(out, "{ \"type\": \"string\" }");
        return;
    }
    if (sl_cli_span_equal_cstr(kind, "bytes") || sl_cli_span_equal_cstr(kind, "stream")) {
        sl_cli_write_cstr(out, "{ \"type\": \"string\", \"format\": \"binary\" }");
        return;
    }
    sl_cli_write_cstr(out, "{ \"x-slop-partial\": \"response schema shape not declared\" }");
}

static void sl_cli_openapi_emit_response_content(FILE* out, const SlCliMetadata* metadata,
                                                 const SlCliRoute* route, SlCliSpan kind,
                                                 SlCliSpan body_schema)
{
    SlCliSpan content_type = route->produce_count > 0U
                                  ? route->produces[0]
                                  : sl_cli_openapi_content_type_for_response_kind(kind);

    if (sl_cli_span_empty(content_type)) {
        return;
    }

    sl_cli_write_cstr(out, ", \"content\": { ");
    sl_cli_json_escape(out, content_type);
    sl_cli_write_cstr(out, ": { \"schema\": ");
    sl_cli_openapi_emit_response_schema(out, metadata, kind, body_schema);
    sl_cli_write_cstr(out, " } }");
}

static bool sl_cli_openapi_response_status_seen(const SlCliRoute* route, size_t limit,
                                                uint64_t status)
{
    size_t index = 0U;

    for (index = 0U; index < limit && index < route->response_count; index += 1U) {
        uint64_t existing_status =
            route->response_statuses[index] == 0U ? 200U : route->response_statuses[index];
        if (existing_status == status) {
            return true;
        }
    }
    return false;
}

static bool sl_cli_openapi_route_has_response_status(const SlCliRoute* route, uint64_t status)
{
    return sl_cli_openapi_response_status_seen(route, route->response_count, status);
}

static void sl_cli_openapi_emit_problem_response(FILE* out, uint64_t status, const char* description)
{
    sl_cli_write_cstr(out, "          \"");
    sl_cli_write_u64(out, status);
    sl_cli_write_cstr(out, "\": { \"description\": ");
    sl_cli_json_escape(out, sl_cli_span_cstr(description));
    sl_cli_write_cstr(out,
                      ", \"content\": { \"application/problem+json\": { \"schema\": { \"$ref\": "
                      "\"#/components/schemas/SlopValidationProblem\" } } } }");
}

static void sl_cli_openapi_emit_response(FILE* out, const SlCliMetadata* metadata,
                                         const SlCliRoute* route, uint64_t status, SlCliSpan kind,
                                         SlCliSpan helper, SlCliSpan body_schema)
{
    sl_cli_write_cstr(out, "          \"");
    sl_cli_write_u64(out, status == 0U ? 200U : status);
    sl_cli_write_cstr(out, "\": { \"description\": ");
    if (sl_cli_span_empty(kind)) {
        sl_cli_json_escape(out, sl_cli_span_cstr("response metadata unknown"));
        sl_cli_write_cstr(out, ", \"x-slop-partial\": \"response metadata unknown\" }");
        return;
    }

    sl_cli_json_escape(out, helper);
    if (!sl_cli_span_equal_cstr(kind, "empty")) {
        sl_cli_openapi_emit_response_content(out, metadata, route, kind, body_schema);
    }
    sl_cli_write_cstr(out, " }");
}

static void sl_cli_openapi_emit_responses(FILE* out, const SlCliMetadata* metadata,
                                          const SlCliRoute* route)
{
    size_t response = 0U;
    bool first = true;

    sl_cli_write_cstr(out, ",\n        \"responses\": {\n");
    if (route->response_count == 0U) {
        sl_cli_openapi_emit_response(out, metadata, route, route->response_status,
                                     route->response_kind, route->response_helper,
                                     (SlCliSpan){0});
        first = false;
    }
    else {
        for (response = 0U; response < route->response_count; response += 1U) {
            uint64_t status = route->response_statuses[response] == 0U
                                  ? 200U
                                  : route->response_statuses[response];
            if (sl_cli_openapi_response_status_seen(route, response, status)) {
                continue;
            }
            sl_cli_write_cstr(out, first ? "" : ",\n");
            sl_cli_openapi_emit_response(out, metadata, route, status,
                                         route->response_kinds[response],
                                         route->response_helpers[response],
                                         route->response_body_schemas[response]);
            first = false;
        }
    }
    if (sl_cli_openapi_route_has_validation(route) &&
        !sl_cli_openapi_route_has_response_status(route, 400U))
    {
        sl_cli_write_cstr(out, first ? "" : ",\n");
        sl_cli_write_cstr(out, "          \"400\": { \"$ref\": "
                               "\"#/components/responses/SlopValidationProblem\" }");
        first = false;
    }
    if (route->auth_required && !sl_cli_openapi_route_has_response_status(route, 401U)) {
        sl_cli_write_cstr(out, first ? "" : ",\n");
        sl_cli_openapi_emit_problem_response(out, 401U, "authentication failed");
        first = false;
    }
    if (route->auth_required && !sl_cli_openapi_route_has_response_status(route, 403U)) {
        sl_cli_write_cstr(out, first ? "" : ",\n");
        sl_cli_openapi_emit_problem_response(out, 403U, "authorization failed");
        first = false;
    }
    sl_cli_write_cstr(out, "\n");
    sl_cli_write_cstr(out, "        }");
}

static void sl_cli_openapi_emit_candidates(FILE* out, const SlCliMetadata* metadata,
                                           const SlCliRoute* route)
{
    bool first = true;

    sl_cli_write_cstr(out, "[");
    if (sl_cli_span_equal_cstr(route->response_kind, "json")) {
        sl_cli_write_cstr(out, "\"native-json-serialization\"");
        first = false;
    }
    for (size_t index = 0U; index < route->binding_count; index += 1U) {
        if (sl_cli_span_equal_cstr(route->binding_kinds[index], "body.json") &&
            sl_cli_openapi_find_schema(metadata, route->binding_schemas[index]) != NULL)
        {
            sl_cli_write_cstr(out, first ? "" : ", ");
            sl_cli_write_cstr(out, "\"native-body-validation\"");
            first = false;
        }
    }
    if (route->effect_count > 0U) {
        sl_cli_write_cstr(out, first ? "" : ", ");
        sl_cli_write_cstr(out, "\"provider-aware-route\"");
        first = false;
    }
    if (sl_cli_span_equal_cstr(route->completeness, "complete")) {
        sl_cli_write_cstr(out, first ? "" : ", ");
        sl_cli_write_cstr(out, "\"static-route-dispatch\"");
    }
    sl_cli_write_cstr(out, "]");
}

static bool sl_cli_openapi_auth_scheme_known(const SlCliMetadata* metadata, SlCliSpan name)
{
    size_t index = 0U;

    for (index = 0U; index < metadata->auth_scheme_count; index += 1U) {
        if (sl_cli_span_equal(metadata->auth_schemes[index].name, name)) {
            return true;
        }
    }
    return false;
}

static bool sl_cli_openapi_route_has_unknown_auth_scheme(const SlCliMetadata* metadata,
                                                        const SlCliRoute* route)
{
    size_t index = 0U;

    for (index = 0U; index < route->auth_scheme_count; index += 1U) {
        if (!sl_cli_openapi_auth_scheme_known(metadata, route->auth_schemes[index])) {
            return true;
        }
    }
    return false;
}

static void sl_cli_openapi_emit_string_array(FILE* out, const SlCliSpan* values, size_t count)
{
    size_t index = 0U;

    sl_cli_write_cstr(out, "[");
    for (index = 0U; index < count; index += 1U) {
        sl_cli_write_cstr(out, index == 0U ? "" : ", ");
        sl_cli_json_escape(out, values[index]);
    }
    sl_cli_write_cstr(out, "]");
}

static void sl_cli_openapi_emit_auth_metadata(FILE* out, const SlCliRoute* route, bool* first)
{
    if (!route->auth_required && !route->auth_allow_anonymous) {
        return;
    }

    sl_cli_openapi_emit_field_prefix(out, first);
    sl_cli_write_cstr(out, "\"x-slop-auth\": {\n          \"required\": ");
    sl_cli_write_cstr(out, route->auth_required ? "true" : "false");
    if (route->auth_allow_anonymous) {
        sl_cli_write_cstr(out, ",\n          \"allowAnonymous\": true");
    }
    if (route->auth_scheme_count > 0U) {
        sl_cli_write_cstr(out, ",\n          \"schemes\": ");
        sl_cli_openapi_emit_string_array(out, route->auth_schemes, route->auth_scheme_count);
    }
    if (route->auth_scope_count > 0U) {
        sl_cli_write_cstr(out, ",\n          \"scopes\": ");
        sl_cli_openapi_emit_string_array(out, route->auth_scopes, route->auth_scope_count);
    }
    if (route->auth_role_count > 0U) {
        sl_cli_write_cstr(out, ",\n          \"roles\": ");
        sl_cli_openapi_emit_string_array(out, route->auth_roles, route->auth_role_count);
    }
    if (route->auth_claim_count > 0U) {
        sl_cli_write_cstr(out, ",\n          \"claims\": ");
        sl_cli_openapi_emit_string_array(out, route->auth_claims, route->auth_claim_count);
    }
    if (!sl_cli_span_empty(route->auth_policy)) {
        sl_cli_write_cstr(out, ",\n          \"policy\": ");
        sl_cli_json_escape(out, route->auth_policy);
    }
    sl_cli_write_cstr(out, "\n        }");
}

static void sl_cli_openapi_emit_security(FILE* out, const SlCliMetadata* metadata,
                                         const SlCliRoute* route, bool* first)
{
    size_t index = 0U;
    size_t scheme_count = route->auth_scheme_count;

    if (!route->auth_required) {
        return;
    }
    if (metadata->auth_scheme_count == 0U) {
        sl_cli_openapi_emit_field_prefix(out, first);
        sl_cli_write_cstr(
            out, "\"x-slop-partial\": \"auth required but auth scheme metadata is missing\"");
        return;
    }
    if (scheme_count > 0U && sl_cli_openapi_route_has_unknown_auth_scheme(metadata, route)) {
        sl_cli_openapi_emit_field_prefix(out, first);
        sl_cli_write_cstr(
            out, "\"x-slop-partial\": \"auth route references an unknown auth scheme\"");
        return;
    }
    if (scheme_count == 0U) {
        scheme_count = metadata->auth_scheme_count;
    }
    sl_cli_openapi_emit_field_prefix(out, first);
    sl_cli_write_cstr(out, "\"security\": [");
    for (index = 0U; index < scheme_count; index += 1U) {
        sl_cli_write_cstr(out, index == 0U ? "" : ", ");
        sl_cli_write_cstr(out, "{ ");
        sl_cli_json_escape(
            out, route->auth_scheme_count == 0U ? metadata->auth_schemes[index].name
                                                : route->auth_schemes[index]);
        sl_cli_write_cstr(out, ": [] }");
    }
    sl_cli_write_cstr(out, "]");
}

static void sl_cli_openapi_emit_json_value(FILE* out, yyjson_val* value)
{
    yyjson_arr_iter arr_iter;
    yyjson_obj_iter obj_iter;
    yyjson_val* item = NULL;
    yyjson_val* key = NULL;
    bool first = true;

    if (value == NULL || yyjson_is_null(value)) {
        sl_cli_write_cstr(out, "null");
        return;
    }
    if (yyjson_is_str(value)) {
        sl_cli_json_escape(out, (SlCliSpan){yyjson_get_str(value), yyjson_get_len(value)});
        return;
    }
    if (yyjson_is_bool(value)) {
        sl_cli_write_cstr(out, yyjson_get_bool(value) ? "true" : "false");
        return;
    }
    if (yyjson_is_num(value)) {
        fprintf(out, "%.17g", yyjson_get_num(value));
        return;
    }
    if (yyjson_is_arr(value)) {
        sl_cli_write_cstr(out, "[");
        yyjson_arr_iter_init(value, &arr_iter);
        while ((item = yyjson_arr_iter_next(&arr_iter)) != NULL) {
            sl_cli_write_cstr(out, first ? "" : ", ");
            first = false;
            sl_cli_openapi_emit_json_value(out, item);
        }
        sl_cli_write_cstr(out, "]");
        return;
    }
    if (yyjson_is_obj(value)) {
        sl_cli_write_cstr(out, "{");
        yyjson_obj_iter_init(value, &obj_iter);
        while ((key = yyjson_obj_iter_next(&obj_iter)) != NULL) {
            item = yyjson_obj_iter_get_val(key);
            if (!yyjson_is_str(key)) {
                continue;
            }
            sl_cli_write_cstr(out, first ? " " : ", ");
            first = false;
            sl_cli_json_escape(out, (SlCliSpan){yyjson_get_str(key), yyjson_get_len(key)});
            sl_cli_write_cstr(out, ": ");
            sl_cli_openapi_emit_json_value(out, item);
        }
        sl_cli_write_cstr(out, first ? "}" : " }");
        return;
    }

    sl_cli_write_cstr(out, "null");
}

static void sl_cli_openapi_emit_operation(FILE* out, const SlCliMetadata* metadata,
                                          const SlCliRoute* route)
{
    bool first = true;
    size_t tag_index = 0U;
    bool complete = sl_cli_openapi_route_complete(metadata, route);

    sl_cli_json_escape_lower(out, route->method);
    if (route->openapi_override_value != NULL && yyjson_is_obj(route->openapi_override_value)) {
        sl_cli_write_cstr(out, ": ");
        sl_cli_openapi_emit_json_value(out, route->openapi_override_value);
        return;
    }
    sl_cli_write_cstr(out, ": {");
    if (!sl_cli_span_empty(route->name)) {
        sl_cli_openapi_emit_field_prefix(out, &first);
        sl_cli_write_cstr(out, "\"operationId\": ");
        sl_cli_json_escape(out, route->name);
    }
    if (!sl_cli_span_empty(route->summary)) {
        sl_cli_openapi_emit_field_prefix(out, &first);
        sl_cli_write_cstr(out, "\"summary\": ");
        sl_cli_json_escape(out, route->summary);
    }
    if (!sl_cli_span_empty(route->description)) {
        sl_cli_openapi_emit_field_prefix(out, &first);
        sl_cli_write_cstr(out, "\"description\": ");
        sl_cli_json_escape(out, route->description);
    }
    if (route->deprecated) {
        sl_cli_openapi_emit_field_prefix(out, &first);
        sl_cli_write_cstr(out, "\"deprecated\": true");
        if (!sl_cli_span_empty(route->deprecated_reason)) {
            sl_cli_openapi_emit_field_prefix(out, &first);
            sl_cli_write_cstr(out, "\"x-slop-deprecated-reason\": ");
            sl_cli_json_escape(out, route->deprecated_reason);
        }
    }
    if (route->tag_count > 0U) {
        sl_cli_openapi_emit_field_prefix(out, &first);
        sl_cli_write_cstr(out, "\"tags\": [");
        for (tag_index = 0U; tag_index < route->tag_count; tag_index += 1U) {
            sl_cli_write_cstr(out, tag_index == 0U ? "" : ", ");
            sl_cli_json_escape(out, route->tags[tag_index]);
        }
        sl_cli_write_cstr(out, "]");
    }
    sl_cli_openapi_emit_security(out, metadata, route, &first);
    sl_cli_openapi_emit_auth_metadata(out, route, &first);
    if (!sl_cli_span_empty(route->kind) && !sl_cli_span_equal_cstr(route->kind, "http")) {
        sl_cli_openapi_emit_field_prefix(out, &first);
        sl_cli_write_cstr(out, "\"x-slop-realtime\": { \"kind\": ");
        sl_cli_json_escape(out, route->kind);
        sl_cli_write_cstr(out, " }");
        sl_cli_openapi_emit_field_prefix(out, &first);
        sl_cli_write_cstr(out, "\"x-slop-transport\": ");
        sl_cli_json_escape(out, route->kind);
    }
    if (!sl_cli_span_empty(route->source_path)) {
        sl_cli_openapi_emit_field_prefix(out, &first);
        sl_cli_write_cstr(out, "\"x-slop-source\": { \"path\": ");
        sl_cli_json_escape(out, route->source_path);
        sl_cli_write_cstr(out, ", \"line\": ");
        sl_cli_write_u64(out, route->source_line);
        sl_cli_write_cstr(out, ", \"column\": ");
        sl_cli_write_u64(out, route->source_column);
        sl_cli_write_cstr(out, " }");
    }
    sl_cli_openapi_emit_field_prefix(out, &first);
    sl_cli_write_cstr(out, "\"x-slop-completeness\": ");
    sl_cli_json_escape(out, complete ? sl_cli_span_cstr("complete") : sl_cli_span_cstr("partial"));
    if (!complete) {
        sl_cli_write_cstr(out, ",\n        \"x-slop-missing\": [");
        first = true;
        if (route->dynamic_metadata) {
            sl_cli_openapi_emit_field_prefix(out, &first);
            sl_cli_json_escape(out, sl_cli_span_cstr("route path metadata is dynamic"));
        }
        if (!sl_cli_span_equal_cstr(route->completeness, "complete")) {
            sl_cli_openapi_emit_field_prefix(out, &first);
            sl_cli_json_escape(out, sl_cli_span_cstr("route Plan completeness is not complete"));
        }
        if (sl_cli_span_empty(route->response_kind)) {
            sl_cli_openapi_emit_field_prefix(out, &first);
            sl_cli_json_escape(out, sl_cli_span_cstr("response metadata is unknown"));
        }
        if (!sl_cli_openapi_route_has_known_json_body_schema(metadata, route)) {
            sl_cli_openapi_emit_field_prefix(out, &first);
            sl_cli_json_escape(out, sl_cli_span_cstr("request body schema is missing"));
        }
        if (!sl_cli_openapi_route_has_known_json_response_schema(metadata, route)) {
            sl_cli_openapi_emit_field_prefix(out, &first);
            sl_cli_json_escape(out, sl_cli_span_cstr("response schema is missing"));
        }
        if (route->auth_required && metadata->auth_scheme_count == 0U) {
            sl_cli_openapi_emit_field_prefix(out, &first);
            sl_cli_json_escape(out, sl_cli_span_cstr("auth scheme metadata is missing"));
        }
        if (!sl_cli_openapi_schema_reference_known(metadata, route->query_schema)) {
            sl_cli_openapi_emit_field_prefix(out, &first);
            sl_cli_json_escape(out, sl_cli_span_cstr("query schema is missing"));
        }
        if (!sl_cli_openapi_schema_reference_known(metadata, route->params_schema)) {
            sl_cli_openapi_emit_field_prefix(out, &first);
            sl_cli_json_escape(out, sl_cli_span_cstr("route parameter schema is missing"));
        }
        for (size_t header_index = 0U; header_index < route->header_count; header_index += 1U) {
            if (!sl_cli_openapi_schema_reference_known(metadata,
                                                       route->header_schemas[header_index]))
            {
                sl_cli_openapi_emit_field_prefix(out, &first);
                sl_cli_json_escape(out, sl_cli_span_cstr("header schema is missing"));
            }
        }
        if (!first) {
            sl_cli_write_cstr(out, "\n        ");
        }
        sl_cli_write_cstr(out, "]");
    }
    sl_cli_write_cstr(out, ",\n        \"x-slop-capabilities\": ");
    sl_cli_openapi_emit_capabilities(out, route);
    sl_cli_write_cstr(out, ",\n        \"x-slop-optimization-candidates\": ");
    sl_cli_openapi_emit_candidates(out, metadata, route);
    sl_cli_write_cstr(out, ",\n        \"parameters\": ");
    sl_cli_openapi_parameters(out, metadata, route);
    sl_cli_openapi_emit_request_body(out, metadata, route);
    sl_cli_openapi_emit_responses(out, metadata, route);
    sl_cli_write_cstr(out, "\n      }");
}

static void sl_cli_openapi_emit_path(FILE* out, const SlCliMetadata* metadata,
                                     const SlCliSpan openapi_paths[SL_CLI_MAX_ROUTES],
                                     size_t path_index)
{
    size_t operation = 0U;
    bool first_method = true;

    sl_cli_write_cstr(out, "\n    ");
    sl_cli_json_escape(out, openapi_paths[path_index]);
    sl_cli_write_cstr(out, ": {");
    for (operation = 0U; operation < metadata->route_count; operation += 1U) {
        if (!sl_cli_span_equal(openapi_paths[operation], openapi_paths[path_index])) {
            continue;
        }
        if (metadata->routes[operation].dynamic_metadata) {
            continue;
        }
        if (metadata->routes[operation].docs_internal) {
            continue;
        }
        if (!first_method) {
            sl_cli_write_cstr(out, ",");
        }
        first_method = false;
        sl_cli_write_cstr(out, "\n      ");
        sl_cli_openapi_emit_operation(out, metadata, &metadata->routes[operation]);
    }
    sl_cli_write_cstr(out, "\n    }");
}

static void sl_cli_openapi_emit_property_schema(FILE* out, const SlCliSchemaProperty* property)
{
    sl_cli_write_cstr(out, "{ \"type\": ");
    sl_cli_json_escape(out, sl_cli_openapi_type_for_kind(property->kind));
    if (!sl_cli_span_empty(property->format)) {
        sl_cli_write_cstr(out, ", \"format\": ");
        sl_cli_json_escape(out, property->format);
    }
    if (sl_cli_span_equal_cstr(property->kind, "array")) {
        if (sl_cli_span_empty(property->item_kind)) {
            sl_cli_write_cstr(out, ", \"items\": { \"x-slop-partial\": "
                                   "\"array item schema unknown\" }");
        }
        else {
            sl_cli_write_cstr(out, ", \"items\": { \"type\": ");
            sl_cli_json_escape(out, sl_cli_openapi_type_for_kind(property->item_kind));
            sl_cli_write_cstr(out, " }");
        }
    }
    if (sl_cli_span_equal_cstr(property->kind, "literal")) {
        sl_cli_write_cstr(out, ", \"enum\": [");
        sl_cli_openapi_emit_literal_value(out, property->literal_value);
        sl_cli_write_cstr(out, "]");
    }
    if (property->nullable) {
        sl_cli_write_cstr(out, ", \"nullable\": true");
    }
    if (property->has_min) {
        fprintf(out, sl_cli_span_equal_cstr(property->kind, "string") ? ", \"minLength\": %.17g"
                                                                      : ", \"minimum\": %.17g",
                property->min_value);
    }
    if (property->has_max) {
        fprintf(out, sl_cli_span_equal_cstr(property->kind, "string") ? ", \"maxLength\": %.17g"
                                                                      : ", \"maximum\": %.17g",
                property->max_value);
    }
    sl_cli_write_cstr(out, " }");
}

static void sl_cli_openapi_emit_schema(FILE* out, const SlCliSchema* schema, bool comma)
{
    size_t index = 0U;
    bool first_required = true;
    bool is_object =
        sl_cli_span_empty(schema->kind) || sl_cli_span_equal_cstr(schema->kind, "object");

    sl_cli_write_cstr(out, comma ? ",\n      " : "\n      ");
    sl_cli_json_escape(out, schema->name);
    sl_cli_write_cstr(out, ": {\n        \"type\": ");
    sl_cli_json_escape(out, is_object ? sl_cli_span_cstr("object")
                                      : sl_cli_openapi_type_for_kind(schema->kind));
    if (sl_cli_span_equal_cstr(schema->kind, "array")) {
        if (sl_cli_span_empty(schema->item_kind)) {
            sl_cli_write_cstr(out, ",\n        \"items\": { \"x-slop-partial\": "
                                   "\"array item schema unknown\" }");
        }
        else {
            sl_cli_write_cstr(out, ",\n        \"items\": { \"type\": ");
            sl_cli_json_escape(out, sl_cli_openapi_type_for_kind(schema->item_kind));
            sl_cli_write_cstr(out, " }");
        }
    }
    if (sl_cli_span_equal_cstr(schema->kind, "literal")) {
        sl_cli_write_cstr(out, ",\n        \"enum\": [");
        sl_cli_openapi_emit_literal_value(out, schema->literal_value);
        sl_cli_write_cstr(out, "]");
    }
    if (schema->nullable) {
        sl_cli_write_cstr(out, ",\n        \"nullable\": true");
    }
    if (schema->has_min) {
        fprintf(out, sl_cli_span_equal_cstr(schema->kind, "string") ? ",\n        \"minLength\": %.17g"
                                                                    : ",\n        \"minimum\": %.17g",
                schema->min_value);
    }
    if (schema->has_max) {
        fprintf(out, sl_cli_span_equal_cstr(schema->kind, "string") ? ",\n        \"maxLength\": %.17g"
                                                                    : ",\n        \"maximum\": %.17g",
                schema->max_value);
    }
    if (!sl_cli_span_empty(schema->source_path)) {
        sl_cli_write_cstr(out, ",\n        \"x-slop-source\": { \"path\": ");
        sl_cli_json_escape(out, schema->source_path);
        sl_cli_write_cstr(out, ", \"line\": ");
        sl_cli_write_u64(out, schema->source_line);
        sl_cli_write_cstr(out, ", \"column\": ");
        sl_cli_write_u64(out, schema->source_column);
        sl_cli_write_cstr(out, " }");
    }
    if (!is_object) {
        sl_cli_write_cstr(out, "\n      }");
        return;
    }
    sl_cli_write_cstr(out, ",\n        \"properties\": {");
    for (index = 0U; index < schema->property_count; index += 1U) {
        sl_cli_write_cstr(out, index == 0U ? "\n          " : ",\n          ");
        sl_cli_json_escape(out, schema->properties[index].name);
        sl_cli_write_cstr(out, ": ");
        sl_cli_openapi_emit_property_schema(out, &schema->properties[index]);
    }
    if (schema->property_count > 0U) {
        sl_cli_write_cstr(out, "\n        ");
    }
    sl_cli_write_cstr(out, "}");
    for (index = 0U; index < schema->property_count; index += 1U) {
        if (schema->properties[index].optional) {
            continue;
        }
        if (first_required) {
            sl_cli_write_cstr(out, ",\n        \"required\": [");
        }
        else {
            sl_cli_write_cstr(out, ", ");
        }
        sl_cli_json_escape(out, schema->properties[index].name);
        first_required = false;
    }
    if (!first_required) {
        sl_cli_write_cstr(out, "]");
    }
    sl_cli_write_cstr(out, "\n      }");
}

static void sl_cli_openapi_emit_components(FILE* out, const SlCliMetadata* metadata)
{
    size_t index = 0U;

    sl_cli_write_cstr(out, ",\n  \"components\": {\n"
                           "    \"responses\": {\n"
                           "      \"SlopValidationProblem\": {\n"
                           "        \"description\": \"Slop validation problem response\",\n"
                           "        \"content\": { \"application/problem+json\": { \"schema\": { "
                           "\"$ref\": \"#/components/schemas/SlopValidationProblem\" } } }\n"
                           "      }\n"
                           "    },\n"
                           "    \"schemas\": {\n"
                           "      \"SlopValidationProblem\": {\n"
                           "        \"type\": \"object\",\n"
                           "        \"properties\": {\n"
                           "          \"status\": { \"type\": \"integer\" },\n"
                           "          \"title\": { \"type\": \"string\" },\n"
                           "          \"detail\": { \"type\": \"string\" }\n"
                           "        }\n"
                           "      }");
    for (index = 0U; index < metadata->schema_count; index += 1U) {
        sl_cli_openapi_emit_schema(out, &metadata->schemas[index], true);
    }
    sl_cli_write_cstr(out, "\n    }");
    if (metadata->auth_scheme_count > 0U) {
        sl_cli_write_cstr(out, ",\n    \"securitySchemes\": {");
        for (index = 0U; index < metadata->auth_scheme_count; index += 1U) {
            const SlCliAuthScheme* scheme = &metadata->auth_schemes[index];
            sl_cli_write_cstr(out, index == 0U ? "\n      " : ",\n      ");
            sl_cli_json_escape(out, scheme->name);
            if (sl_cli_span_equal_cstr(scheme->kind, "jwtBearer")) {
                sl_cli_write_cstr(out, ": { \"type\": \"http\", \"scheme\": \"bearer\", "
                                       "\"bearerFormat\": \"JWT\" }");
            }
            else if (sl_cli_span_equal_cstr(scheme->kind, "cookieSession")) {
                sl_cli_write_cstr(out, ": { \"type\": \"apiKey\", \"in\": \"cookie\", \"name\": ");
                sl_cli_json_escape(out, scheme->cookie);
                sl_cli_write_cstr(out, " }");
            }
            else {
                sl_cli_write_cstr(out, ": { \"type\": \"apiKey\", \"in\": \"header\", \"name\": ");
                sl_cli_json_escape(out, scheme->header);
                sl_cli_write_cstr(out, " }");
            }
        }
        sl_cli_write_cstr(out, "\n    }");
    }
    sl_cli_write_cstr(out, "\n  }");
}

static SlCliOpenApiPolicy sl_cli_openapi_compute_policy(
    const SlCliMetadata* metadata, const SlCliSpan openapi_paths[SL_CLI_MAX_ROUTES])
{
    SlCliOpenApiPolicy policy = {0};
    size_t index = 0U;

    for (index = 0U; index < metadata->route_count; index += 1U) {
        const SlCliRoute* route = &metadata->routes[index];
        if (route->docs_internal) {
            continue;
        }
        policy.routes_total += 1U;
        if (route->dynamic_metadata || sl_cli_span_empty(openapi_paths[index])) {
            policy.routes_omitted += 1U;
            policy.operations_partial += 1U;
            policy.missing_count += 1U;
            continue;
        }
        policy.routes_included += 1U;
        if (sl_cli_openapi_route_complete(metadata, route)) {
            policy.operations_complete += 1U;
        }
        else {
            policy.operations_partial += 1U;
            policy.missing_count += 1U;
        }
    }
    return policy;
}

static void sl_cli_openapi_emit_policy_missing(FILE* out, const SlCliMetadata* metadata,
                                               const SlCliSpan openapi_paths[SL_CLI_MAX_ROUTES])
{
    size_t index = 0U;
    bool first = true;

    sl_cli_write_cstr(out, "[");
    for (index = 0U; index < metadata->route_count; index += 1U) {
        const SlCliRoute* route = &metadata->routes[index];
        if (route->docs_internal) {
            continue;
        }
        if (!route->dynamic_metadata && !sl_cli_span_empty(openapi_paths[index]) &&
            sl_cli_openapi_route_complete(metadata, route))
        {
            continue;
        }
        sl_cli_write_cstr(out, first ? "\n      " : ",\n      ");
        sl_cli_write_cstr(out, "{ \"method\": ");
        sl_cli_json_escape(out, route->method);
        sl_cli_write_cstr(out, ", \"path\": ");
        sl_cli_json_escape(out, route->pattern);
        sl_cli_write_cstr(out, ", \"reason\": ");
        if (route->dynamic_metadata) {
            sl_cli_json_escape(out, route->dynamic_reason);
        }
        else if (sl_cli_span_empty(route->response_kind)) {
            sl_cli_json_escape(out, sl_cli_span_cstr("response metadata is unknown"));
        }
        else if (!sl_cli_openapi_route_has_known_json_body_schema(metadata, route)) {
            sl_cli_json_escape(out, sl_cli_span_cstr("request body schema is missing"));
        }
        else if (!sl_cli_openapi_route_has_known_json_response_schema(metadata, route)) {
            sl_cli_json_escape(out, sl_cli_span_cstr("response schema is missing"));
        }
        else if (route->auth_required && metadata->auth_scheme_count == 0U) {
            sl_cli_json_escape(out, sl_cli_span_cstr("auth scheme metadata is missing"));
        }
        else if (!sl_cli_span_equal_cstr(route->completeness, "complete")) {
            sl_cli_json_escape(out, sl_cli_span_cstr("route Plan completeness is not complete"));
        }
        else {
            sl_cli_json_escape(out, sl_cli_span_cstr("schema metadata is incomplete"));
        }
        sl_cli_write_cstr(out, " }");
        first = false;
    }
    if (!first) {
        sl_cli_write_cstr(out, "\n    ");
    }
    sl_cli_write_cstr(out, "]");
}

static void sl_cli_openapi_emit_summary(FILE* out, const SlCliMetadata* metadata,
                                        const SlCliSpan openapi_paths[SL_CLI_MAX_ROUTES])
{
    SlCliOpenApiPolicy policy = sl_cli_openapi_compute_policy(metadata, openapi_paths);

    sl_cli_write_cstr(out, "Sloppy OpenAPI\n\n");
    fprintf(out, "routes total: %zu\n", policy.routes_total);
    fprintf(out, "routes included: %zu\n", policy.routes_included);
    fprintf(out, "routes omitted: %zu\n", policy.routes_omitted);
    fprintf(out, "operations complete: %zu\n", policy.operations_complete);
    fprintf(out, "operations partial: %zu\n", policy.operations_partial);
}

static void sl_cli_openapi_emit_document(FILE* out, const SlCliMetadata* metadata,
                                         const SlCliSpan openapi_paths[SL_CLI_MAX_ROUTES])
{
    SlCliOpenApiPolicy policy = sl_cli_openapi_compute_policy(metadata, openapi_paths);
    size_t index = 0U;
    bool first_path = true;

    sl_cli_write_cstr(out, "{\n"
                           "  \"openapi\": \"3.0.3\",\n"
                           "  \"info\": {\n"
                           "    \"title\": \"Sloppy API\",\n"
                           "    \"version\": \"0.0.0\"\n"
                           "  },\n"
                           "  \"x-slop-openapi-policy\": {\n"
                           "    \"mode\": ");
    sl_cli_json_escape(out,
                       policy.operations_partial == 0U ? sl_cli_span_cstr("complete")
                                                       : sl_cli_span_cstr("partial"));
    fprintf(out, ",\n"
                 "    \"routesTotal\": %zu,\n"
                 "    \"routesIncluded\": %zu,\n"
                 "    \"routesOmitted\": %zu,\n"
                 "    \"operationsComplete\": %zu,\n"
                 "    \"operationsPartial\": %zu,\n"
                 "    \"missing\": ",
            policy.routes_total, policy.routes_included, policy.routes_omitted,
            policy.operations_complete, policy.operations_partial);
    sl_cli_openapi_emit_policy_missing(out, metadata, openapi_paths);
    sl_cli_write_cstr(out, ",\n"
                           "    \"optimizations\": \"reported-only\"\n"
                           "  },\n"
                           "  \"paths\": {");
    for (index = 0U; index < metadata->route_count; index += 1U) {
        if (metadata->routes[index].dynamic_metadata || metadata->routes[index].docs_internal ||
            sl_cli_span_empty(openapi_paths[index]))
        {
            continue;
        }
        if (sl_cli_openapi_path_seen(openapi_paths, index)) {
            continue;
        }
        if (!first_path) {
            sl_cli_write_cstr(out, ",");
        }
        first_path = false;
        sl_cli_openapi_emit_path(out, metadata, openapi_paths, index);
    }
    sl_cli_write_cstr(out, "\n  }");
    sl_cli_openapi_emit_components(out, metadata);
    sl_cli_write_cstr(out, "\n}\n");
}

static int sl_cli_command_openapi(const SlCliOptions* options)
{
    unsigned char* json_storage = sl_cli_scratch_json_storage();
    unsigned char* plan_arena_storage = sl_cli_scratch_plan_arena_storage();
    SlArena plan_arena;
    yyjson_doc* doc = NULL;
    SlCliMetadata* metadata = sl_cli_scratch_metadata();
    static char path_buffers[SL_CLI_MAX_ROUTES][512];
    static SlCliSpan openapi_paths[SL_CLI_MAX_ROUTES];
    FILE* out = stdout;
    int result = 1;

    if (options->plan_path == NULL) {
        sl_cli_write_cstr(stderr, "sloppy openapi: --plan <path> is required\n");
        return 1;
    }
    if (!sl_status_is_ok(sl_arena_init(&plan_arena, plan_arena_storage, SL_CLI_ARENA_BYTES))) {
        sl_cli_write_cstr(stderr, "sloppy openapi: failed to initialize plan validation arena\n");
        return 1;
    }
    if (options->output_path != NULL) {
#ifdef _MSC_VER
        if (fopen_s(&out, options->output_path, "wb") != 0) {
            out = NULL;
        }
#else
        out = fopen(options->output_path, "wb");
#endif
        if (out == NULL) {
            sl_cli_write_error_with_value(
                "sloppy openapi: failed to open output path: ", options->output_path, "\n");
            return 1;
        }
    }
    if (sl_cli_load_metadata(options->plan_path, json_storage, &plan_arena, true, &doc, metadata) !=
        0)
    {
        goto cleanup;
    }
    if (sl_cli_span_equal_cstr(metadata->kind, "program")) {
        sl_cli_write_cstr(
            stderr,
            "sloppy openapi: OpenAPI is only available for web Plans with route metadata.\n");
        goto cleanup;
    }
    sl_cli_sort_routes(metadata);
    if (sl_cli_openapi_prepare_paths(metadata, path_buffers, openapi_paths) != 0) {
        goto cleanup;
    }
    {
        SlCliOpenApiPolicy policy = sl_cli_openapi_compute_policy(metadata, openapi_paths);
        if ((options->openapi_strict || options->openapi_fail_on_partial) &&
            policy.operations_partial > 0U)
        {
            sl_cli_write_cstr(stderr,
                              options->openapi_strict
                                  ? "sloppy openapi: --strict requires complete route contracts\n"
                                  : "sloppy openapi: partial OpenAPI contract detected\n");
            sl_cli_openapi_emit_policy_missing(stderr, metadata, openapi_paths);
            sl_cli_write_cstr(stderr, "\n");
            goto cleanup;
        }
        if (options->openapi_summary) {
            sl_cli_openapi_emit_summary(out, metadata, openapi_paths);
        }
        else {
            sl_cli_openapi_emit_document(out, metadata, openapi_paths);
        }
    }
    result = 0;

cleanup:
    if (doc != NULL) {
        yyjson_doc_free(doc);
    }
    if (out != stdout) {
        if (fclose(out) != 0 && result == 0) {
            sl_cli_write_error_with_value(
                "sloppy openapi: failed to write output path: ", options->output_path, "\n");
            result = 1;
        }
    }
    return result;
}

static int sl_cli_openapi_artifact_request_from_plan(const char* plan_path, bool* out_requested,
                                                     bool* out_strict)
{
    unsigned char* json_storage = sl_cli_scratch_json_storage();
    unsigned char* plan_arena_storage = sl_cli_scratch_plan_arena_storage();
    SlArena plan_arena;
    yyjson_doc* doc = NULL;
    SlCliMetadata* metadata = sl_cli_scratch_metadata();
    size_t index = 0U;
    int result = 1;

    if (out_requested != NULL) {
        *out_requested = false;
    }
    if (out_strict != NULL) {
        *out_strict = false;
    }
    if (plan_path == NULL || out_requested == NULL || out_strict == NULL ||
        !sl_status_is_ok(sl_arena_init(&plan_arena, plan_arena_storage, SL_CLI_ARENA_BYTES)))
    {
        return 1;
    }
    if (sl_cli_load_metadata(plan_path, json_storage, &plan_arena, true, &doc, metadata) != 0) {
        goto cleanup;
    }
    for (index = 0U; index < metadata->route_count; index += 1U) {
        if (!metadata->routes[index].docs_internal) {
            continue;
        }
        if (sl_cli_span_equal_cstr(metadata->routes[index].docs_kind, "openapi")) {
            *out_requested = true;
            if (metadata->routes[index].docs_strict) {
                *out_strict = true;
            }
        }
    }
    result = 0;

cleanup:
    if (doc != NULL) {
        yyjson_doc_free(doc);
    }
    return result;
}

static int sl_cli_openapi_refresh_docs_artifact(const char* artifacts_path,
                                                const char* command_name)
{
    SlCliOptions openapi_options = {0};
    char plan_path[SL_RUN_PATH_MAX_BYTES];
    char output_path[SL_RUN_PATH_MAX_BYTES];
    bool requested = false;
    bool strict = false;

    if (artifacts_path == NULL ||
        !sl_cli_join_leaf(plan_path, sizeof(plan_path), artifacts_path, "app.plan.json") ||
        !sl_cli_join_leaf(output_path, sizeof(output_path), artifacts_path, "openapi.json"))
    {
        sl_cli_write_error_with_value(command_name, ": OpenAPI artifact path is too long\n", "");
        return 1;
    }
    if (sl_cli_openapi_artifact_request_from_plan(plan_path, &requested, &strict) != 0) {
        return 1;
    }
    if (!requested) {
        return 0;
    }
    openapi_options.command = "openapi";
    openapi_options.plan_path = plan_path;
    openapi_options.output_path = output_path;
    openapi_options.openapi_strict = strict;
    return sl_cli_command_openapi(&openapi_options);
}

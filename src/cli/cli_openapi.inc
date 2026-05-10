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
    if (sl_cli_span_equal_cstr(kind, "number")) {
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

static void sl_cli_openapi_emit_parameter(FILE* file, SlCliSpan name, SlCliSpan location,
                                          SlCliSpan type, bool required, bool* first)
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
    sl_cli_write_cstr(file, " } }");
    *first = false;
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
        if (sl_cli_span_equal_cstr(route->binding_kinds[index], "body.json")) {
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

static void sl_cli_openapi_parameters(FILE* file, const SlCliRoute* route)
{
    size_t index = 0U;
    bool first = true;

    sl_cli_write_cstr(file, "[");
    while (index < route->pattern.length) {
        if (route->pattern.ptr[index] == '{') {
            size_t name_start = index + 1U;
            size_t name_end = name_start;
            size_t type_start = 0U;
            size_t type_end = 0U;
            SlCliSpan type = {0};

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
            sl_cli_openapi_emit_parameter(
                file, (SlCliSpan){&route->pattern.ptr[name_start], name_end - name_start},
                sl_cli_span_cstr("path"), type, true, &first);
        }
        index += 1U;
    }
    for (index = 0U; index < route->binding_count; index += 1U) {
        if (sl_cli_span_equal_cstr(route->binding_kinds[index], "query")) {
            sl_cli_openapi_emit_parameter(file, route->binding_names[index],
                                          sl_cli_span_cstr("query"), route->binding_schemas[index],
                                          false, &first);
        }
        else if (sl_cli_span_equal_cstr(route->binding_kinds[index], "header")) {
            sl_cli_openapi_emit_parameter(file, route->binding_names[index],
                                          sl_cli_span_cstr("header"), route->binding_schemas[index],
                                          false, &first);
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
        if (metadata->routes[index].dynamic_metadata) {
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
        if (!sl_cli_span_equal_cstr(route->binding_kinds[index], "body.json")) {
            continue;
        }
        sl_cli_write_cstr(out, ",\n        \"requestBody\": {\n"
                               "          \"required\": true,\n"
                               "          \"content\": { \"application/json\": { \"schema\": ");
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
    return (SlCliSpan){0};
}

static void sl_cli_openapi_emit_problem_schema(FILE* out)
{
    sl_cli_write_cstr(out,
                      "{ \"type\": \"object\", \"properties\": { "
                      "\"status\": { \"type\": \"integer\" }, "
                      "\"title\": { \"type\": \"string\" }, "
                      "\"detail\": { \"type\": \"string\" } } }");
}

static void sl_cli_openapi_emit_response_schema(FILE* out, SlCliSpan kind)
{
    if (sl_cli_span_equal_cstr(kind, "json")) {
        sl_cli_write_cstr(out, "{ \"x-slop-partial\": \"response schema shape not declared\" }");
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
    if (sl_cli_span_equal_cstr(kind, "bytes")) {
        sl_cli_write_cstr(out, "{ \"type\": \"string\", \"format\": \"binary\" }");
        return;
    }
    sl_cli_write_cstr(out, "{ \"x-slop-partial\": \"response schema shape not declared\" }");
}

static void sl_cli_openapi_emit_response_content(FILE* out, SlCliSpan kind)
{
    SlCliSpan content_type = sl_cli_openapi_content_type_for_response_kind(kind);

    if (sl_cli_span_empty(content_type)) {
        return;
    }

    sl_cli_write_cstr(out, ", \"content\": { ");
    sl_cli_json_escape(out, content_type);
    sl_cli_write_cstr(out, ": { \"schema\": ");
    sl_cli_openapi_emit_response_schema(out, kind);
    sl_cli_write_cstr(out, " } }");
}

static void sl_cli_openapi_emit_responses(FILE* out, const SlCliRoute* route)
{
    uint64_t status = route->response_status == 0U ? 200U : route->response_status;

    sl_cli_write_cstr(out, ",\n        \"responses\": {\n          \"");
    sl_cli_write_u64(out, status);
    sl_cli_write_cstr(out, "\": { \"description\": ");
    if (sl_cli_span_empty(route->response_kind)) {
        sl_cli_json_escape(out, sl_cli_span_cstr("response metadata unknown"));
        sl_cli_write_cstr(out, ", \"x-slop-partial\": \"response metadata unknown\" }");
    }
    else {
        sl_cli_json_escape(out, route->response_helper);
        if (!sl_cli_span_equal_cstr(route->response_kind, "empty")) {
            sl_cli_openapi_emit_response_content(out, route->response_kind);
        }
        sl_cli_write_cstr(out, " }");
    }
    if (sl_cli_openapi_route_has_validation(route)) {
        sl_cli_write_cstr(out, ",\n"
                               "          \"400\": { \"$ref\": "
                               "\"#/components/responses/SlopValidationProblem\" }\n");
    }
    else {
        sl_cli_write_cstr(out, "\n");
    }
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

static void sl_cli_openapi_emit_operation(FILE* out, const SlCliMetadata* metadata,
                                          const SlCliRoute* route)
{
    bool first = true;
    size_t tag_index = 0U;

    sl_cli_json_escape_lower(out, route->method);
    sl_cli_write_cstr(out, ": {");
    if (!sl_cli_span_empty(route->name)) {
        sl_cli_openapi_emit_field_prefix(out, &first);
        sl_cli_write_cstr(out, "\"operationId\": ");
        sl_cli_json_escape(out, route->name);
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
    sl_cli_json_escape(out, route->completeness);
    sl_cli_write_cstr(out, ",\n        \"x-slop-capabilities\": ");
    sl_cli_openapi_emit_capabilities(out, route);
    sl_cli_write_cstr(out, ",\n        \"x-slop-optimization-candidates\": ");
    sl_cli_openapi_emit_candidates(out, metadata, route);
    sl_cli_write_cstr(out, ",\n        \"parameters\": ");
    sl_cli_openapi_parameters(out, route);
    sl_cli_openapi_emit_request_body(out, metadata, route);
    sl_cli_openapi_emit_responses(out, route);
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
    sl_cli_write_cstr(out, "\n    }\n  }");
}

static void sl_cli_openapi_emit_document(FILE* out, const SlCliMetadata* metadata,
                                         const SlCliSpan openapi_paths[SL_CLI_MAX_ROUTES])
{
    size_t index = 0U;
    bool first_path = true;
    bool has_dynamic_routes = false;

    for (index = 0U; index < metadata->route_count; index += 1U) {
        if (metadata->routes[index].dynamic_metadata) {
            has_dynamic_routes = true;
            break;
        }
    }

    sl_cli_write_cstr(out, "{\n"
                           "  \"openapi\": \"3.0.3\",\n"
                           "  \"info\": {\n"
                           "    \"title\": \"Sloppy API\",\n"
                           "    \"version\": \"0.0.0\"\n"
                           "  },\n"
                           "  \"x-slop-openapi-policy\": {\n"
                           "    \"status\": \"plan-supported-subset\",\n"
                           "    \"unknownMetadata\": \"explicit-partial-markers\"");
    if (has_dynamic_routes) {
        sl_cli_write_cstr(out,
                          ",\n"
                          "    \"dynamicRoutes\": \"omitted-when-path-is-unknown\"");
    }
    sl_cli_write_cstr(out,
                           ",\n"
                           "    \"optimizations\": \"reported-only\"\n"
                           "  },\n"
                           "  \"paths\": {");
    for (index = 0U; index < metadata->route_count; index += 1U) {
        if (metadata->routes[index].dynamic_metadata) {
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

    if (options->plan_path == NULL) {
        sl_cli_write_cstr(stderr, "sloppy openapi: --plan <path> is required\n");
        return 1;
    }
    if (!sl_status_is_ok(
            sl_arena_init(&plan_arena, plan_arena_storage, SL_CLI_ARENA_BYTES)))
    {
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
    if (sl_cli_load_metadata(options->plan_path, json_storage, &plan_arena, true, &doc,
                             metadata) != 0)
    {
        if (doc != NULL) {
            yyjson_doc_free(doc);
        }
        if (out != stdout) {
            (void)fclose(out);
        }
        return 1;
    }
    if (sl_cli_span_equal_cstr(metadata->kind, "program")) {
        sl_cli_write_cstr(stderr,
                          "sloppy openapi: OpenAPI is only available for web Plans with route metadata.\n");
        yyjson_doc_free(doc);
        if (out != stdout) {
            (void)fclose(out);
        }
        return 1;
    }
    sl_cli_sort_routes(metadata);
    if (sl_cli_openapi_prepare_paths(metadata, path_buffers, openapi_paths) != 0) {
        yyjson_doc_free(doc);
        if (out != stdout) {
            (void)fclose(out);
        }
        return 1;
    }
    sl_cli_openapi_emit_document(out, metadata, openapi_paths);

    yyjson_doc_free(doc);
    if (out != stdout) {
        (void)fclose(out);
    }
    return 0;
}

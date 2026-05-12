#include "bench_internal.h"

#include "sloppy/arena.h"
#include "sloppy/builder.h"
#include "sloppy/engine.h"
#include "sloppy/http.h"
#include "sloppy/http_context.h"
#include "sloppy/http_dispatch.h"
#include "sloppy/http_response.h"
#include "sloppy/request_validation.h"

#include <yyjson.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t sl_bench_json_checksum(SlBytes bytes)
{
    uint64_t checksum = 1469598103934665603ULL;
    size_t index = 0U;

    for (index = 0U; index < bytes.length; index += 1U) {
        checksum ^= (uint64_t)bytes.ptr[index];
        checksum *= 1099511628211ULL;
    }

    return checksum;
}

static SlHttpRequestContext sl_bench_json_context(SlHttpRequestHead* request)
{
    SlHttpRequestContext context = {0};

    context.request = request;
    context.scheme = sl_str_from_cstr("http");
    context.protocol = sl_str_from_cstr("http/1.1");
    context.content_type = sl_str_from_cstr("application/json");
    context.content_length = request->body.length;
    context.has_content_length = true;
    context.body_kind = SL_HTTP_REQUEST_BODY_JSON;
    return context;
}

static SlPlan sl_bench_json_plan(const SlPlanSchema* schemas, size_t schema_count)
{
    SlPlan plan = {0};

    plan.version = SL_PLAN_CURRENT_VERSION;
    plan.compiler_version = sl_str_from_cstr("bench");
    plan.runtime_min_version = sl_str_from_cstr(SL_PLAN_RUNTIME_MIN_VERSION_0_1_0);
    plan.stdlib_version = sl_str_from_cstr(SL_PLAN_STDLIB_VERSION_0_1_0);
    plan.schemas = schemas;
    plan.schema_count = schema_count;
    return plan;
}

static SlEngineOptions sl_bench_json_noop_options(void)
{
    SlEngineOptions options = {0};

    options.kind = SL_ENGINE_KIND_NONE;
    options.runtime_name = sl_str_from_cstr("sloppy-json-bench");
    options.runtime_version = sl_str_from_cstr("0.0.0");
    options.target_platform = sl_str_from_cstr(SL_PLAN_TARGET_PLATFORM_WINDOWS_X64);
    options.target_engine = sl_str_from_cstr("none");
    return options;
}

static SlStatus sl_bench_json_create_noop_engine(SlArena* arena, SlEngine** out_engine)
{
    SlEngineOptions options = sl_bench_json_noop_options();
    return sl_engine_create(&options, arena, out_engine);
}

static void sl_bench_json_schema_fixture(SlPlanSchemaProperty properties[3U],
                                         SlPlanSchemaNode* name_schema,
                                         SlPlanSchemaNode* age_schema,
                                         SlPlanSchemaNode* active_schema, SlPlanSchema* schema)
{
    *name_schema =
        (SlPlanSchemaNode){.kind = SL_PLAN_SCHEMA_STRING, .has_max = true, .max_value = 32};
    *age_schema = (SlPlanSchemaNode){.kind = SL_PLAN_SCHEMA_INT,
                                     .has_min = true,
                                     .min_value = 0,
                                     .has_max = true,
                                     .max_value = 120};
    *active_schema = (SlPlanSchemaNode){.kind = SL_PLAN_SCHEMA_BOOLEAN, .optional = true};
    properties[0] = (SlPlanSchemaProperty){.name = sl_str_from_cstr("name"), .schema = name_schema};
    properties[1] = (SlPlanSchemaProperty){.name = sl_str_from_cstr("age"), .schema = age_schema};
    properties[2] =
        (SlPlanSchemaProperty){.name = sl_str_from_cstr("active"), .schema = active_schema};
    *schema = (SlPlanSchema){.name = sl_str_from_cstr("CreateUser"),
                             .definition = {.kind = SL_PLAN_SCHEMA_OBJECT,
                                            .properties = properties,
                                            .property_count = 3U}};
}

static SlPlanRoute sl_bench_json_route(const SlPlanRequestBinding* binding)
{
    SlPlanRoute route = {0};

    route.method = sl_str_from_cstr("POST");
    route.pattern = sl_str_from_cstr("/users");
    route.handler_id = 1U;
    route.bindings = binding;
    route.binding_count = 1U;
    route.json_request.mode = SL_PLAN_JSON_REQUEST_NATIVE_SCHEMA;
    route.json_request.materialization = SL_PLAN_JSON_MATERIALIZATION_MATERIALIZE_ONCE;
    route.json_request.unknown_fields = SL_PLAN_JSON_UNKNOWN_FIELDS_REJECT;
    route.json_request.schema = sl_str_from_cstr("CreateUser");
    route.json_request.max_body_bytes = SL_HTTP_DEFAULT_MAX_BODY_LENGTH;
    route.json_request.max_depth = 50U;
    route.json_request.max_string_bytes = 4096U;
    route.json_request.max_array_length = 1024U;
    return route;
}

static SlStatus bench_json_request_native_schema_valid(const SlBenchContext* context,
                                                       uint64_t iterations, uint64_t* out_checksum)
{
    unsigned char arena_storage[8192];
    static const unsigned char body[] = "{\"name\":\"Ada\",\"age\":42,\"active\":true}";
    SlPlanSchemaProperty properties[3];
    SlPlanSchemaNode name_schema = {0};
    SlPlanSchemaNode age_schema = {0};
    SlPlanSchemaNode active_schema = {0};
    SlPlanSchema schema = {0};
    SlPlanRequestBinding binding = {.kind = SL_PLAN_REQUEST_BINDING_BODY_JSON,
                                    .schema = sl_str_from_cstr("CreateUser")};
    SlPlanRoute route = sl_bench_json_route(&binding);
    SlPlan plan;
    SlArena arena;
    SlHttpRequestHead request = {0};
    SlHttpRequestContext request_context = {0};
    uint64_t checksum = 0U;
    uint64_t index = 0U;
    SlStatus status;

    (void)context;

    if (out_checksum == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    sl_bench_json_schema_fixture(properties, &name_schema, &age_schema, &active_schema, &schema);
    plan = sl_bench_json_plan(&schema, 1U);
    request.body = sl_bytes_from_parts(body, sizeof(body) - 1U);
    request_context = sl_bench_json_context(&request);
    status = sl_arena_init(&arena, arena_storage, sizeof(arena_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }

    for (index = 0U; index < iterations; index += 1U) {
        SlEngineResult result = {0};
        SlDiag diag = {0};

        sl_arena_reset(&arena);
        status =
            sl_request_validation_validate(&arena, &plan, &route, &request_context, &result, &diag);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        if (result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_NONE) {
            return sl_status_from_code(SL_STATUS_INVALID_STATE);
        }
        checksum += (uint64_t)result.kind;
        checksum += (uint64_t)diag.code;
        checksum += (uint64_t)request.body.length;
    }

    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus bench_json_request_native_schema_reject(const SlBenchContext* context,
                                                        uint64_t iterations, uint64_t* out_checksum)
{
    unsigned char arena_storage[16384];
    static const unsigned char body[] = "{\"name\":\"NameThatIsTooLongForTheSchemaLimit\","
                                        "\"age\":121,\"active\":true,\"extra\":true}";
    SlPlanSchemaProperty properties[3];
    SlPlanSchemaNode name_schema = {0};
    SlPlanSchemaNode age_schema = {0};
    SlPlanSchemaNode active_schema = {0};
    SlPlanSchema schema = {0};
    SlPlanRequestBinding binding = {.kind = SL_PLAN_REQUEST_BINDING_BODY_JSON,
                                    .schema = sl_str_from_cstr("CreateUser")};
    SlPlanRoute route = sl_bench_json_route(&binding);
    SlPlan plan;
    SlArena arena;
    SlHttpRequestHead request = {0};
    SlHttpRequestContext request_context = {0};
    uint64_t checksum = 0U;
    uint64_t index = 0U;
    SlStatus status;

    (void)context;

    if (out_checksum == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    sl_bench_json_schema_fixture(properties, &name_schema, &age_schema, &active_schema, &schema);
    plan = sl_bench_json_plan(&schema, 1U);
    request.body = sl_bytes_from_parts(body, sizeof(body) - 1U);
    request_context = sl_bench_json_context(&request);
    status = sl_arena_init(&arena, arena_storage, sizeof(arena_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }

    for (index = 0U; index < iterations; index += 1U) {
        SlEngineResult result = {0};
        SlDiag diag = {0};

        sl_arena_reset(&arena);
        status =
            sl_request_validation_validate(&arena, &plan, &route, &request_context, &result, &diag);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        if (result.kind != SL_ENGINE_RESULT_ERROR || result.response.status != 400U ||
            diag.code != SL_DIAG_REQUEST_VALIDATION_FAILED)
        {
            return sl_status_from_code(SL_STATUS_INVALID_STATE);
        }
        checksum += (uint64_t)result.response.status;
        checksum += sl_bench_json_checksum(result.response.body);
    }

    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus bench_json_request_generic_parse_valid(const SlBenchContext* context,
                                                       uint64_t iterations, uint64_t* out_checksum)
{
    static const unsigned char body[] = "{\"username\":\"ada\",\"password\":\"longpass\"}";
    uint64_t checksum = 0U;
    uint64_t index = 0U;

    (void)context;

    if (out_checksum == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    for (index = 0U; index < iterations; index += 1U) {
        yyjson_doc* doc = yyjson_read((char*)body, sizeof(body) - 1U, 0U);

        if (doc == NULL) {
            return sl_status_from_code(SL_STATUS_INVALID_STATE);
        }
        checksum += sl_bench_json_checksum(sl_bytes_from_parts(body, sizeof(body) - 1U));
        yyjson_doc_free(doc);
    }

    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus bench_json_request_generic_parse_reject(const SlBenchContext* context,
                                                        uint64_t iterations, uint64_t* out_checksum)
{
    static const unsigned char body[] = "{\"username\":\"ada\",\"password\":";
    uint64_t checksum = 0U;
    uint64_t index = 0U;

    (void)context;

    if (out_checksum == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    for (index = 0U; index < iterations; index += 1U) {
        yyjson_doc* doc = yyjson_read((char*)body, sizeof(body) - 1U, 0U);

        if (doc != NULL) {
            yyjson_doc_free(doc);
            return sl_status_from_code(SL_STATUS_INVALID_STATE);
        }
        checksum += sizeof(body) - 1U;
    }

    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus bench_json_request_native_materialize_once(const SlBenchContext* context,
                                                           uint64_t iterations,
                                                           uint64_t* out_checksum)
{
    unsigned char arena_storage[8192];
    static const unsigned char body[] = "{\"name\":\"Ada\",\"age\":42,\"active\":true}";
    SlPlanSchemaProperty properties[3];
    SlPlanSchemaNode name_schema = {0};
    SlPlanSchemaNode age_schema = {0};
    SlPlanSchemaNode active_schema = {0};
    SlPlanSchema schema = {0};
    SlPlanRequestBinding binding = {.kind = SL_PLAN_REQUEST_BINDING_BODY_JSON,
                                    .schema = sl_str_from_cstr("CreateUser")};
    SlPlanRoute route = sl_bench_json_route(&binding);
    SlPlan plan;
    SlArena arena;
    SlHttpRequestHead request = {0};
    SlHttpRequestContext request_context = {0};
    uint64_t checksum = 0U;
    uint64_t index = 0U;
    SlStatus status;

    (void)context;

    if (out_checksum == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    sl_bench_json_schema_fixture(properties, &name_schema, &age_schema, &active_schema, &schema);
    plan = sl_bench_json_plan(&schema, 1U);
    request.body = sl_bytes_from_parts(body, sizeof(body) - 1U);
    request_context = sl_bench_json_context(&request);
    request_context.native_json_validated = true;
    status = sl_arena_init(&arena, arena_storage, sizeof(arena_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }

    for (index = 0U; index < iterations; index += 1U) {
        SlEngineResult result = {0};
        SlDiag diag = {0};
        yyjson_doc* doc = NULL;

        sl_arena_reset(&arena);
        status =
            sl_request_validation_validate(&arena, &plan, &route, &request_context, &result, &diag);
        if (!sl_status_is_ok(status) || result.kind != SL_ENGINE_RESULT_NONE ||
            diag.code != SL_DIAG_NONE)
        {
            return sl_status_from_code(SL_STATUS_INVALID_STATE);
        }
        doc = yyjson_read((char*)body, sizeof(body) - 1U, 0U);
        if (doc == NULL) {
            return sl_status_from_code(SL_STATUS_INVALID_STATE);
        }
        checksum += sl_bench_json_checksum(sl_bytes_from_parts(body, sizeof(body) - 1U));
        yyjson_doc_free(doc);
    }

    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus bench_json_request_generic_parse_medium_body(const SlBenchContext* context,
                                                             uint64_t iterations,
                                                             uint64_t* out_checksum)
{
    char body[4096];
    uint64_t checksum = 0U;
    uint64_t index = 0U;
    size_t payload_length = 0U;
    size_t body_length = 0U;
    int written = 0;

    (void)context;

    if (out_checksum == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    memset(body, 'a', sizeof(body));
    written = snprintf(body, sizeof(body), "{\"name\":\"Ada\",\"age\":42,\"payload\":\"");
    if (written < 0 || (size_t)written >= sizeof(body)) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    payload_length = sizeof(body) - 1U - (size_t)written - (sizeof("\"}") - 1U);
    memset(&body[written], 'a', payload_length);
    memcpy(&body[(size_t)written + payload_length], "\"}", sizeof("\"}") - 1U);
    body[(size_t)written + payload_length + sizeof("\"}") - 1U] = '\0';
    body_length = (size_t)written + payload_length + (sizeof("\"}") - 1U);

    for (index = 0U; index < iterations; index += 1U) {
        yyjson_doc* doc = yyjson_read(body, body_length, 0U);

        if (doc == NULL) {
            return sl_status_from_code(SL_STATUS_INVALID_STATE);
        }
        checksum +=
            sl_bench_json_checksum(sl_bytes_from_parts((const unsigned char*)body, body_length));
        yyjson_doc_free(doc);
    }

    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus bench_json_append_escaped_string(SlStringBuilder* builder, SlStr value)
{
    static const char hex[] = "0123456789abcdef";
    size_t index = 0U;
    bool needs_escape = false;
    SlStatus status;

    for (index = 0U; index < value.length; index += 1U) {
        unsigned char ch = (unsigned char)value.ptr[index];
        if (ch < 0x20U || ch == '"' || ch == '\\') {
            needs_escape = true;
            break;
        }
    }

    status = sl_string_builder_append_char(builder, '"');
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (!needs_escape) {
        status = sl_string_builder_append_str(builder, value);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        return sl_string_builder_append_char(builder, '"');
    }
    for (index = 0U; index < value.length; index += 1U) {
        unsigned char ch = (unsigned char)value.ptr[index];
        char escaped[7] = {'\\', 'u', '0', '0', '0', '0', '\0'};

        switch (ch) {
        case '"':
            status = sl_string_builder_append_cstr(builder, "\\\"");
            break;
        case '\\':
            status = sl_string_builder_append_cstr(builder, "\\\\");
            break;
        case '\b':
            status = sl_string_builder_append_cstr(builder, "\\b");
            break;
        case '\f':
            status = sl_string_builder_append_cstr(builder, "\\f");
            break;
        case '\n':
            status = sl_string_builder_append_cstr(builder, "\\n");
            break;
        case '\r':
            status = sl_string_builder_append_cstr(builder, "\\r");
            break;
        case '\t':
            status = sl_string_builder_append_cstr(builder, "\\t");
            break;
        default:
            if (ch < 0x20U) {
                escaped[4] = hex[(ch >> 4U) & 0x0FU];
                escaped[5] = hex[ch & 0x0FU];
                status = sl_string_builder_append_cstr(builder, escaped);
            }
            else {
                status = sl_string_builder_append_char(builder, (char)ch);
            }
            break;
        }
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    return sl_string_builder_append_char(builder, '"');
}

static SlStatus bench_json_response_native_schema_payload(SlStringBuilder* builder)
{
    SlStatus status = sl_string_builder_append_cstr(builder, "{\"name\":");

    if (sl_status_is_ok(status)) {
        status = bench_json_append_escaped_string(builder, sl_str_from_cstr("Ada"));
    }
    if (sl_status_is_ok(status)) {
        status = sl_string_builder_append_cstr(builder, ",\"escaped\":");
    }
    if (sl_status_is_ok(status)) {
        status =
            bench_json_append_escaped_string(builder, sl_str_from_cstr("line\nquote\"slash\\"));
    }
    if (sl_status_is_ok(status)) {
        status = sl_string_builder_append_cstr(builder, ",\"count\":");
    }
    if (sl_status_is_ok(status)) {
        status = sl_string_builder_append_i64(builder, 7);
    }
    if (sl_status_is_ok(status)) {
        status = sl_string_builder_append_cstr(builder, ",\"score\":");
    }
    if (sl_status_is_ok(status)) {
        status = sl_string_builder_append_f64(builder, 1.5);
    }
    if (sl_status_is_ok(status)) {
        status = sl_string_builder_append_cstr(builder,
                                               ",\"ok\":true,\"note\":null,\"nested\":{\"label\":");
    }
    if (sl_status_is_ok(status)) {
        status = bench_json_append_escaped_string(builder, sl_str_from_cstr("inner"));
    }
    if (sl_status_is_ok(status)) {
        status = sl_string_builder_append_cstr(builder, "},\"tags\":[");
    }
    if (sl_status_is_ok(status)) {
        status = bench_json_append_escaped_string(builder, sl_str_from_cstr("a"));
    }
    if (sl_status_is_ok(status)) {
        status = sl_string_builder_append_char(builder, ',');
    }
    if (sl_status_is_ok(status)) {
        status = bench_json_append_escaped_string(builder, sl_str_from_cstr("b"));
    }
    if (sl_status_is_ok(status)) {
        status = sl_string_builder_append_cstr(builder, "]}");
    }
    return status;
}

static SlStatus bench_json_response_generic_serialize_payload_only(const SlBenchContext* context,
                                                                   uint64_t iterations,
                                                                   uint64_t* out_checksum)
{
    uint64_t checksum = 0U;
    uint64_t index = 0U;

    (void)context;

    if (out_checksum == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    for (index = 0U; index < iterations; index += 1U) {
        yyjson_mut_doc* doc = yyjson_mut_doc_new(NULL);
        yyjson_mut_val* root = NULL;
        yyjson_mut_val* tags = NULL;
        yyjson_mut_val* nested = NULL;
        yyjson_mut_val* note = NULL;
        char* json = NULL;
        size_t length = 0U;

        if (doc == NULL) {
            return sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
        }
        root = yyjson_mut_obj(doc);
        tags = yyjson_mut_arr(doc);
        nested = yyjson_mut_obj(doc);
        note = yyjson_mut_null(doc);
        if (root == NULL || tags == NULL || nested == NULL || note == NULL ||
            !yyjson_mut_obj_add_str(doc, root, "name", "Ada") ||
            !yyjson_mut_obj_add_str(doc, root, "escaped", "line\nquote\"slash\\") ||
            !yyjson_mut_obj_add_int(doc, root, "count", 7) ||
            !yyjson_mut_obj_add_real(doc, root, "score", 1.5) ||
            !yyjson_mut_obj_add_bool(doc, root, "ok", true) ||
            !yyjson_mut_obj_add_val(doc, root, "note", note) ||
            !yyjson_mut_obj_add_str(doc, nested, "label", "inner") ||
            !yyjson_mut_obj_add_val(doc, root, "nested", nested) ||
            !yyjson_mut_arr_add_str(doc, tags, "a") || !yyjson_mut_arr_add_str(doc, tags, "b") ||
            !yyjson_mut_obj_add_val(doc, root, "tags", tags))
        {
            yyjson_mut_doc_free(doc);
            return sl_status_from_code(SL_STATUS_INVALID_STATE);
        }
        yyjson_mut_doc_set_root(doc, root);
        json = yyjson_mut_write(doc, 0U, &length);
        if (json == NULL) {
            yyjson_mut_doc_free(doc);
            return sl_status_from_code(SL_STATUS_INVALID_STATE);
        }
        checksum += sl_bench_json_checksum(sl_bytes_from_parts((const unsigned char*)json, length));
        free(json);
        yyjson_mut_doc_free(doc);
    }

    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus bench_json_response_generic_http_response_write(const SlBenchContext* context,
                                                                uint64_t iterations,
                                                                uint64_t* out_checksum)
{
    unsigned char output[1024];
    static const unsigned char body[] =
        "{\"name\":\"Ada\",\"escaped\":\"line\\nquote\\\"slash\\\\\",\"count\":7,\"score\":1.5,"
        "\"ok\":true,\"note\":null,\"nested\":{\"label\":\"inner\"},\"tags\":[\"a\",\"b\"]}";
    SlHttpResponse response =
        sl_http_response_json(200U, sl_bytes_from_parts(body, sizeof(body) - 1U));
    uint64_t checksum = 0U;
    uint64_t index = 0U;

    (void)context;

    if (out_checksum == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    for (index = 0U; index < iterations; index += 1U) {
        SlBytes bytes = {0};
        SlStatus status = sl_http_response_write(&response, output, sizeof(output), &bytes);

        if (!sl_status_is_ok(status)) {
            return status;
        }
        checksum += sl_bench_json_checksum(bytes);
        checksum += (uint64_t)bytes.length;
    }

    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus bench_json_response_native_schema_serialize_payload_only(
    const SlBenchContext* context, uint64_t iterations, uint64_t* out_checksum)
{
    uint64_t checksum = 0U;
    uint64_t index = 0U;

    (void)context;

    if (out_checksum == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    for (index = 0U; index < iterations; index += 1U) {
        char output[512];
        SlStringBuilder builder = {0};
        SlStr json = sl_str_empty();
        SlStatus status = sl_string_builder_init_fixed(&builder, output, sizeof(output));

        if (sl_status_is_ok(status)) {
            status = bench_json_response_native_schema_payload(&builder);
        }
        if (!sl_status_is_ok(status)) {
            return status;
        }
        json = sl_string_builder_view(&builder);
        checksum += sl_bench_json_checksum(
            sl_bytes_from_parts((const unsigned char*)json.ptr, json.length));
    }

    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus bench_json_response_native_schema_http_response_write(const SlBenchContext* context,
                                                                      uint64_t iterations,
                                                                      uint64_t* out_checksum)
{
    unsigned char output[1024];
    uint64_t checksum = 0U;
    uint64_t index = 0U;

    (void)context;

    if (out_checksum == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    for (index = 0U; index < iterations; index += 1U) {
        char body_storage[512];
        SlStringBuilder builder = {0};
        SlStr json = sl_str_empty();
        SlBytes bytes = {0};
        SlHttpResponse response;
        SlStatus status =
            sl_string_builder_init_fixed(&builder, body_storage, sizeof(body_storage));

        if (sl_status_is_ok(status)) {
            status = bench_json_response_native_schema_payload(&builder);
        }
        if (!sl_status_is_ok(status)) {
            return status;
        }
        json = sl_string_builder_view(&builder);
        response = sl_http_response_json(
            200U, sl_bytes_from_parts((const unsigned char*)json.ptr, json.length));
        status = sl_http_response_write(&response, output, sizeof(output), &bytes);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        checksum += sl_bench_json_checksum(bytes);
        checksum += (uint64_t)bytes.length;
    }

    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus bench_json_response_native_static_write(const SlBenchContext* context,
                                                        uint64_t iterations, uint64_t* out_checksum)
{
    unsigned char output[1024];
    static const unsigned char body[] = "{\"ok\":true,\"items\":[1,2,3]}";
    SlHttpResponse response =
        sl_http_response_json(200U, sl_bytes_from_parts(body, sizeof(body) - 1U));
    uint64_t checksum = 0U;
    uint64_t index = 0U;

    (void)context;

    if (out_checksum == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    for (index = 0U; index < iterations; index += 1U) {
        SlBytes bytes = {0};
        SlStatus status = sl_http_response_write(&response, output, sizeof(output), &bytes);

        if (!sl_status_is_ok(status)) {
            return status;
        }
        checksum += sl_bench_json_checksum(bytes);
        checksum += (uint64_t)bytes.length;
    }

    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus bench_json_response_native_static_head_write(const SlBenchContext* context,
                                                             uint64_t iterations,
                                                             uint64_t* out_checksum)
{
    unsigned char output[1024];
    static const unsigned char body[] = "{\"ok\":true,\"items\":[1,2,3]}";
    SlHttpResponseWriteOptions options = {.suppress_body = true};
    SlHttpResponse response =
        sl_http_response_json(200U, sl_bytes_from_parts(body, sizeof(body) - 1U));
    uint64_t checksum = 0U;
    uint64_t index = 0U;

    (void)context;

    if (out_checksum == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    for (index = 0U; index < iterations; index += 1U) {
        SlBytes bytes = {0};
        SlStatus status = sl_http_response_write_with_options(&response, &options, output,
                                                              sizeof(output), &bytes);

        if (!sl_status_is_ok(status)) {
            return status;
        }
        checksum += sl_bench_json_checksum(bytes);
        checksum += (uint64_t)bytes.length;
    }

    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus bench_json_response_large_list_write(const SlBenchContext* context,
                                                     uint64_t iterations, uint64_t* out_checksum)
{
    static unsigned char body[32768];
    static bool initialized = false;
    unsigned char output[65536];
    SlHttpResponse response;
    uint64_t checksum = 0U;
    uint64_t index = 0U;
    size_t cursor = 0U;

    (void)context;

    if (out_checksum == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (!initialized) {
        size_t item = 0U;

        body[cursor++] = '[';
        for (item = 0U; item < 2048U; item += 1U) {
            int written = 0;

            if (item != 0U) {
                body[cursor++] = ',';
            }
            written = snprintf((char*)&body[cursor], sizeof(body) - cursor, "%zu", item);
            if (written < 0 || (size_t)written >= sizeof(body) - cursor) {
                return sl_status_from_code(SL_STATUS_INVALID_STATE);
            }
            cursor += (size_t)written;
        }
        body[cursor++] = ']';
        initialized = true;
    }
    else {
        while (cursor < sizeof(body) && body[cursor] != '\0') {
            cursor += 1U;
        }
    }

    response = sl_http_response_json(200U, sl_bytes_from_parts(body, cursor));
    for (index = 0U; index < iterations; index += 1U) {
        SlBytes bytes = {0};
        SlStatus status = sl_http_response_write(&response, output, sizeof(output), &bytes);

        if (!sl_status_is_ok(status)) {
            return status;
        }
        checksum += sl_bench_json_checksum(bytes);
        checksum += (uint64_t)bytes.length;
    }

    *out_checksum = checksum;
    return sl_status_ok();
}

typedef enum SlBenchJsonRouteScenario
{
    SL_BENCH_JSON_ROUTE_TABLE_BUILD = 0,
    SL_BENCH_JSON_ROUTE_DISPATCH_ONLY = 1,
    SL_BENCH_JSON_ROUTE_FULL_INPROCESS = 2
} SlBenchJsonRouteScenario;

typedef struct SlBenchJsonRouteCache
{
    bool initialized;
    size_t route_count;
    SlPlanJsonRequestMode mode;
    SlBenchJsonRouteScenario scenario;
    SlArena setup_arena;
    SlArena table_arena;
    SlArena engine_arena;
    SlArena dispatch_arena;
    SlPlanRoute* routes;
    SlPlanHandler* handlers;
    char* path_storage;
    SlPlanSchemaProperty properties[3U];
    SlPlanSchemaNode name_schema;
    SlPlanSchemaNode age_schema;
    SlPlanSchemaNode active_schema;
    SlPlanSchema schema;
    SlPlanRequestBinding binding;
    SlPlan plan;
    SlHttpRouteTable table;
    SlEngine* engine;
    SlHttpRequestHead request;
    SlHttpHeader headers[2];
} SlBenchJsonRouteCache;

static bool bench_json_dispatch_route_cache_matches(const SlBenchJsonRouteCache* cache,
                                                    size_t route_count, SlPlanJsonRequestMode mode,
                                                    SlBenchJsonRouteScenario scenario)
{
    return cache != NULL && cache->initialized && cache->route_count == route_count &&
           cache->mode == mode && cache->scenario == scenario;
}

static SlStatus bench_json_dispatch_prepare_route_cache(size_t route_count,
                                                        SlPlanJsonRequestMode mode,
                                                        SlBenchJsonRouteScenario scenario,
                                                        SlBenchJsonRouteCache** out_cache)
{
    static unsigned char setup_storage[16U * 1024U * 1024U];
    static unsigned char table_storage[16U * 1024U * 1024U];
    static unsigned char engine_storage[1024];
    static unsigned char dispatch_storage[65536];
    static const unsigned char request_body[] = "{\"name\":\"Ada\",\"age\":42,\"active\":true}";
    static const char response_body[] = "{\"ok\":true}";
    static SlBenchJsonRouteCache cache;
    size_t route_index = 0U;
    size_t target_route_index = 0U;
    SlStatus status;

    if (out_cache == NULL || route_count == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_cache = NULL;
    if (bench_json_dispatch_route_cache_matches(&cache, route_count, mode, scenario)) {
        *out_cache = &cache;
        return sl_status_ok();
    }

    if (cache.engine != NULL) {
        sl_engine_destroy(cache.engine);
    }
    cache = (SlBenchJsonRouteCache){0};
    cache.route_count = route_count;
    cache.mode = mode;
    cache.scenario = scenario;

    status = sl_arena_init(&cache.setup_arena, setup_storage, sizeof(setup_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_init(&cache.table_arena, table_storage, sizeof(table_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_init(&cache.engine_arena, engine_storage, sizeof(engine_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_init(&cache.dispatch_arena, dispatch_storage, sizeof(dispatch_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(&cache.setup_arena, sizeof(SlPlanRoute) * route_count,
                            _Alignof(SlPlanRoute), (void**)&cache.routes);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(&cache.setup_arena, sizeof(SlPlanHandler) * route_count,
                            _Alignof(SlPlanHandler), (void**)&cache.handlers);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(&cache.setup_arena, 32U * route_count, _Alignof(char),
                            (void**)&cache.path_storage);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    memset(cache.routes, 0, sizeof(SlPlanRoute) * route_count);
    memset(cache.handlers, 0, sizeof(SlPlanHandler) * route_count);
    for (route_index = 0U; route_index < route_count; route_index += 1U) {
        char* path = &cache.path_storage[route_index * 32U];
        int written = snprintf(path, 32U, "/json/%zu", route_index);

        if (written < 0 || written >= 32) {
            return sl_status_from_code(SL_STATUS_INVALID_STATE);
        }
        cache.handlers[route_index] =
            (SlPlanHandler){(SlHandlerId)(route_index + 1U), sl_str_from_cstr("handler"),
                            sl_str_from_cstr("json dispatch handler")};
        cache.routes[route_index].method = sl_str_from_cstr("POST");
        cache.routes[route_index].pattern = sl_str_from_cstr(path);
        cache.routes[route_index].handler_id = (SlHandlerId)(route_index + 1U);
    }

    sl_bench_json_schema_fixture(cache.properties, &cache.name_schema, &cache.age_schema,
                                 &cache.active_schema, &cache.schema);
    cache.binding = (SlPlanRequestBinding){.kind = SL_PLAN_REQUEST_BINDING_BODY_JSON,
                                           .schema = sl_str_from_cstr("CreateUser")};
    cache.routes[target_route_index].json_request.mode = mode;
    cache.routes[target_route_index].json_request.materialization =
        mode == SL_PLAN_JSON_REQUEST_NATIVE_SCHEMA ? SL_PLAN_JSON_MATERIALIZATION_MATERIALIZE_ONCE
                                                   : SL_PLAN_JSON_MATERIALIZATION_NONE;
    cache.routes[target_route_index].json_request.unknown_fields =
        SL_PLAN_JSON_UNKNOWN_FIELDS_REJECT;
    cache.routes[target_route_index].json_request.schema = sl_str_from_cstr("CreateUser");
    cache.routes[target_route_index].json_request.max_body_bytes = SL_HTTP_DEFAULT_MAX_BODY_LENGTH;
    cache.routes[target_route_index].json_request.max_depth = 50U;
    cache.routes[target_route_index].json_request.max_string_bytes = 4096U;
    cache.routes[target_route_index].json_request.max_array_length = 1024U;
    cache.routes[target_route_index].native_response_kind = sl_str_from_cstr("json");
    cache.routes[target_route_index].native_response_status = 200U;
    cache.routes[target_route_index].native_response_body = sl_str_from_cstr(response_body);
    cache.routes[target_route_index].native_response_content_type =
        sl_str_from_cstr("application/json");
    if (scenario == SL_BENCH_JSON_ROUTE_FULL_INPROCESS) {
        cache.routes[target_route_index].bindings = &cache.binding;
        cache.routes[target_route_index].binding_count = 1U;
    }

    cache.plan = sl_bench_json_plan(&cache.schema, 1U);
    cache.plan.target.platform = sl_str_from_cstr(SL_PLAN_TARGET_PLATFORM_WINDOWS_X64);
    cache.plan.target.engine = sl_str_from_cstr("none");
    cache.plan.handlers = cache.handlers;
    cache.plan.handler_count = route_count;
    cache.plan.routes = cache.routes;
    cache.plan.route_count = route_count;

    cache.headers[0] = (SlHttpHeader){.name = sl_str_from_cstr("content-type"),
                                      .value = sl_str_from_cstr("application/json")};
    cache.headers[1] =
        (SlHttpHeader){.name = sl_str_from_cstr("content-length"), .value = sl_str_from_cstr("37")};
    cache.request.method = SL_HTTP_METHOD_POST;
    cache.request.path = cache.routes[target_route_index].pattern;
    cache.request.raw_target = cache.request.path;
    cache.request.headers = cache.headers;
    cache.request.header_count = sizeof(cache.headers) / sizeof(cache.headers[0]);
    cache.request.body = sl_bytes_from_parts(request_body, sizeof(request_body) - 1U);

    if (scenario != SL_BENCH_JSON_ROUTE_TABLE_BUILD) {
        status = sl_bench_json_create_noop_engine(&cache.engine_arena, &cache.engine);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_http_route_table_build(&cache.table_arena, &cache.plan, &cache.table, NULL);
        if (!sl_status_is_ok(status)) {
            sl_engine_destroy(cache.engine);
            cache.engine = NULL;
            return status;
        }
    }

    cache.initialized = true;
    *out_cache = &cache;
    return sl_status_ok();
}

static SlStatus bench_json_dispatch_route_count(size_t route_count, SlPlanJsonRequestMode mode,
                                                SlBenchJsonRouteScenario scenario,
                                                uint64_t iterations, uint64_t* out_checksum)
{
    SlBenchJsonRouteCache* cache = NULL;
    uint64_t checksum = 0U;
    uint64_t index = 0U;
    SlStatus status;

    if (out_checksum == NULL || route_count == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = bench_json_dispatch_prepare_route_cache(route_count, mode, scenario, &cache);
    if (!sl_status_is_ok(status) || cache == NULL) {
        return sl_status_is_ok(status) ? sl_status_from_code(SL_STATUS_INVALID_STATE) : status;
    }

    if (scenario == SL_BENCH_JSON_ROUTE_TABLE_BUILD) {
        for (index = 0U; index < iterations; index += 1U) {
            SlHttpRouteTable table = {0};

            sl_arena_reset(&cache->table_arena);
            status = sl_http_route_table_build(&cache->table_arena, &cache->plan, &table, NULL);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            checksum += (uint64_t)table.route_count;
            checksum += (uint64_t)table.dispatch.route_count;
            checksum += (uint64_t)table.dispatch.exact_route_bucket_count;
            checksum += (uint64_t)table.dispatch.param_route_count;
            checksum += (uint64_t)table.dispatch.dispatch_mode;
        }

        *out_checksum = checksum;
        return sl_status_ok();
    }

    for (index = 0U; index < iterations; index += 1U) {
        SlEngineResult result = {0};
        SlDiag diag = {0};

        sl_arena_reset(&cache->dispatch_arena);
        status =
            sl_http_dispatch_request_head(&cache->dispatch_arena, cache->engine, &cache->plan,
                                          &cache->table.dispatch, &cache->request, &result, &diag);
        if (!sl_status_is_ok(status) || result.kind != SL_ENGINE_RESULT_JSON ||
            result.response.status != 200U)
        {
            return sl_status_is_ok(status) ? sl_status_from_code(SL_STATUS_INVALID_STATE) : status;
        }
        checksum += (uint64_t)result.response.status;
        checksum += sl_bench_json_checksum(result.response.body);
    }

    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus bench_json_dispatch_generic_json(const SlBenchContext* context, uint64_t iterations,
                                                 uint64_t* out_checksum)
{
    (void)context;
    return bench_json_dispatch_route_count(1U, SL_PLAN_JSON_REQUEST_GENERIC,
                                           SL_BENCH_JSON_ROUTE_FULL_INPROCESS, iterations,
                                           out_checksum);
}

static SlStatus bench_json_dispatch_native_json(const SlBenchContext* context, uint64_t iterations,
                                                uint64_t* out_checksum)
{
    (void)context;
    return bench_json_dispatch_route_count(1U, SL_PLAN_JSON_REQUEST_NATIVE_SCHEMA,
                                           SL_BENCH_JSON_ROUTE_FULL_INPROCESS, iterations,
                                           out_checksum);
}

static SlStatus bench_json_dispatch_1k_route_table_build(const SlBenchContext* context,
                                                         uint64_t iterations,
                                                         uint64_t* out_checksum)
{
    (void)context;
    return bench_json_dispatch_route_count(1000U, SL_PLAN_JSON_REQUEST_NATIVE_SCHEMA,
                                           SL_BENCH_JSON_ROUTE_TABLE_BUILD, iterations,
                                           out_checksum);
}

static SlStatus bench_json_dispatch_10k_route_table_build(const SlBenchContext* context,
                                                          uint64_t iterations,
                                                          uint64_t* out_checksum)
{
    (void)context;
    return bench_json_dispatch_route_count(10000U, SL_PLAN_JSON_REQUEST_NATIVE_SCHEMA,
                                           SL_BENCH_JSON_ROUTE_TABLE_BUILD, iterations,
                                           out_checksum);
}

static SlStatus
bench_json_dispatch_1k_route_native_json_dispatch_only(const SlBenchContext* context,
                                                       uint64_t iterations, uint64_t* out_checksum)
{
    (void)context;
    return bench_json_dispatch_route_count(1000U, SL_PLAN_JSON_REQUEST_NATIVE_SCHEMA,
                                           SL_BENCH_JSON_ROUTE_DISPATCH_ONLY, iterations,
                                           out_checksum);
}

static SlStatus
bench_json_dispatch_10k_route_native_json_dispatch_only(const SlBenchContext* context,
                                                        uint64_t iterations, uint64_t* out_checksum)
{
    (void)context;
    return bench_json_dispatch_route_count(10000U, SL_PLAN_JSON_REQUEST_NATIVE_SCHEMA,
                                           SL_BENCH_JSON_ROUTE_DISPATCH_ONLY, iterations,
                                           out_checksum);
}

static SlStatus
bench_json_dispatch_1k_route_generic_json_dispatch_only(const SlBenchContext* context,
                                                        uint64_t iterations, uint64_t* out_checksum)
{
    (void)context;
    return bench_json_dispatch_route_count(1000U, SL_PLAN_JSON_REQUEST_GENERIC,
                                           SL_BENCH_JSON_ROUTE_DISPATCH_ONLY, iterations,
                                           out_checksum);
}

static SlStatus bench_json_dispatch_10k_route_generic_json_dispatch_only(
    const SlBenchContext* context, uint64_t iterations, uint64_t* out_checksum)
{
    (void)context;
    return bench_json_dispatch_route_count(10000U, SL_PLAN_JSON_REQUEST_GENERIC,
                                           SL_BENCH_JSON_ROUTE_DISPATCH_ONLY, iterations,
                                           out_checksum);
}

static SlStatus
bench_json_dispatch_1k_route_native_json_full_inprocess(const SlBenchContext* context,
                                                        uint64_t iterations, uint64_t* out_checksum)
{
    (void)context;
    return bench_json_dispatch_route_count(1000U, SL_PLAN_JSON_REQUEST_NATIVE_SCHEMA,
                                           SL_BENCH_JSON_ROUTE_FULL_INPROCESS, iterations,
                                           out_checksum);
}

static SlStatus bench_json_dispatch_10k_route_native_json_full_inprocess(
    const SlBenchContext* context, uint64_t iterations, uint64_t* out_checksum)
{
    (void)context;
    return bench_json_dispatch_route_count(10000U, SL_PLAN_JSON_REQUEST_NATIVE_SCHEMA,
                                           SL_BENCH_JSON_ROUTE_FULL_INPROCESS, iterations,
                                           out_checksum);
}

static SlStatus bench_json_dispatch_1k_route_generic_json_full_inprocess(
    const SlBenchContext* context, uint64_t iterations, uint64_t* out_checksum)
{
    (void)context;
    return bench_json_dispatch_route_count(1000U, SL_PLAN_JSON_REQUEST_GENERIC,
                                           SL_BENCH_JSON_ROUTE_FULL_INPROCESS, iterations,
                                           out_checksum);
}

static SlStatus bench_json_dispatch_10k_route_generic_json_full_inprocess(
    const SlBenchContext* context, uint64_t iterations, uint64_t* out_checksum)
{
    (void)context;
    return bench_json_dispatch_route_count(10000U, SL_PLAN_JSON_REQUEST_GENERIC,
                                           SL_BENCH_JSON_ROUTE_FULL_INPROCESS, iterations,
                                           out_checksum);
}

static SlStatus bench_json_dispatch_validate_then_static_response(const SlBenchContext* context,
                                                                  uint64_t iterations,
                                                                  uint64_t* out_checksum)
{
    unsigned char arena_storage[8192];
    unsigned char output[1024];
    static const unsigned char request_body[] = "{\"name\":\"Ada\",\"age\":42,\"active\":true}";
    static const unsigned char response_body[] = "{\"ok\":true}";
    SlPlanSchemaProperty properties[3];
    SlPlanSchemaNode name_schema = {0};
    SlPlanSchemaNode age_schema = {0};
    SlPlanSchemaNode active_schema = {0};
    SlPlanSchema schema = {0};
    SlPlanRequestBinding binding = {.kind = SL_PLAN_REQUEST_BINDING_BODY_JSON,
                                    .schema = sl_str_from_cstr("CreateUser")};
    SlPlanRoute route = sl_bench_json_route(&binding);
    SlPlan plan;
    SlArena arena;
    SlHttpRequestHead request = {0};
    SlHttpRequestContext request_context = {0};
    SlHttpResponse response =
        sl_http_response_json(200U, sl_bytes_from_parts(response_body, sizeof(response_body) - 1U));
    uint64_t checksum = 0U;
    uint64_t index = 0U;
    SlStatus status;

    (void)context;

    if (out_checksum == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    sl_bench_json_schema_fixture(properties, &name_schema, &age_schema, &active_schema, &schema);
    plan = sl_bench_json_plan(&schema, 1U);
    request.body = sl_bytes_from_parts(request_body, sizeof(request_body) - 1U);
    request_context = sl_bench_json_context(&request);
    status = sl_arena_init(&arena, arena_storage, sizeof(arena_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }

    for (index = 0U; index < iterations; index += 1U) {
        SlEngineResult result = {0};
        SlDiag diag = {0};
        SlBytes bytes = {0};

        sl_arena_reset(&arena);
        status =
            sl_request_validation_validate(&arena, &plan, &route, &request_context, &result, &diag);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        if (result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_NONE) {
            return sl_status_from_code(SL_STATUS_INVALID_STATE);
        }
        status = sl_http_response_write(&response, output, sizeof(output), &bytes);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        checksum += (uint64_t)request.body.length;
        checksum += sl_bench_json_checksum(bytes);
    }

    *out_checksum = checksum;
    return sl_status_ok();
}

static const SlBenchDefinition json_dispatch_definitions[] = {
    {"json.request.generic_parse.small_login.payload_only", "json",
     "generic baseline parse for a small JSON login body", 1000U, 100000U,
     bench_json_request_generic_parse_valid,
     "baseline JSON parse only; no schema validation, handler, socket, or response writer", false,
     sizeof("{\"username\":\"ada\",\"password\":\"longpass\"}") - 1U, 1U, 0U},
    {"json.request.native_schema.valid.payload_validate_only", "json",
     "validate a schema-backed JSON request body without entering JavaScript", 1000U, 100000U,
     bench_json_request_native_schema_valid,
     "parses and validates the request body; no sockets, response writer, or JavaScript handler",
     false, sizeof("{\"name\":\"Ada\",\"age\":42,\"active\":true}") - 1U, 1U, 0U},
    {"json.request.native_materialize_once.small_login.payload_validate_materialize", "json",
     "validate a schema-backed JSON body and materialize one cached JSON value", 1000U, 50000U,
     bench_json_request_native_materialize_once,
     "models the native-validated materialize-once handoff without entering V8", false,
     sizeof("{\"name\":\"Ada\",\"age\":42,\"active\":true}") - 1U, 1U, 0U},
    {"json.request.generic_parse.medium_body.payload_only", "json",
     "generic baseline parse for a medium JSON body", 100U, 20000U,
     bench_json_request_generic_parse_medium_body,
     "medium body parser row; no schema validation, handler, socket, or response writer", false,
     4095U, 1U, 0U},
    {"json.request.generic_parse.malformed.payload_only", "json",
     "generic baseline malformed JSON reject", 100U, 20000U,
     bench_json_request_generic_parse_reject,
     "baseline malformed JSON parser rejection; no schema problem serialization", false,
     sizeof("{\"username\":\"ada\",\"password\":") - 1U, 1U, 0U},
    {"json.request.native_schema.reject.problem_details", "json",
     "reject a schema-backed JSON request body and build validation problem details", 100U, 20000U,
     bench_json_request_native_schema_reject,
     "negative-path validation with max-bound and unknown-field diagnostics", false,
     sizeof("{\"name\":\"NameThatIsTooLongForTheSchemaLimit\",\"age\":121,\"active\":true,"
            "\"extra\":true}") -
         1U,
     1U, 0U},
    {"json.response.generic.serialize.payload_only", "json",
     "serialize a dynamic JSON response through yyjson", 1000U, 100000U,
     bench_json_response_generic_serialize_payload_only,
     "generic response serialization baseline; no HTTP headers, schema-backed native writer, or "
     "sockets",
     false,
     sizeof("{\"name\":\"Ada\",\"escaped\":\"line\\nquote\\\"slash\\\\\",\"count\":7,\"score\":1.5,"
            "\"ok\":true,\"note\":null,\"nested\":{\"label\":\"inner\"},\"tags\":[\"a\",\"b\"]}") -
         1U,
     1U, 0U},
    {"json.response.native_schema.serialize.payload_only", "json",
     "serialize a supported schema-backed dynamic JSON response in stable field order", 1000U,
     100000U, bench_json_response_native_schema_serialize_payload_only,
     "native schema response payload writer shape; no HTTP headers, sockets, or JavaScript handler",
     false,
     sizeof("{\"name\":\"Ada\",\"escaped\":\"line\\nquote\\\"slash\\\\\",\"count\":7,\"score\":1.5,"
            "\"ok\":true,\"note\":null,\"nested\":{\"label\":\"inner\"},\"tags\":[\"a\",\"b\"]}") -
         1U,
     1U, 0U},
    {"json.response.generic.http_response_write", "json",
     "write a generic serialized JSON payload through the HTTP response writer", 1000U, 100000U,
     bench_json_response_generic_http_response_write,
     "body is already serialized JSON bytes; includes HTTP status/header/body framing but no "
     "sockets",
     false,
     sizeof("{\"name\":\"Ada\",\"escaped\":\"line\\nquote\\\"slash\\\\\",\"count\":7,\"score\":1.5,"
            "\"ok\":true,\"note\":null,\"nested\":{\"label\":\"inner\"},\"tags\":[\"a\",\"b\"]}") -
         1U,
     1U, 0U},
    {"json.response.native_schema.http_response_write", "json",
     "serialize a supported schema-backed JSON payload and write an HTTP response", 1000U, 100000U,
     bench_json_response_native_schema_http_response_write,
     "includes native schema payload serialization plus HTTP status/header/body framing; no "
     "sockets",
     false,
     sizeof("{\"name\":\"Ada\",\"escaped\":\"line\\nquote\\\"slash\\\\\",\"count\":7,\"score\":1.5,"
            "\"ok\":true,\"note\":null,\"nested\":{\"label\":\"inner\"},\"tags\":[\"a\",\"b\"]}") -
         1U,
     1U, 0U},
    {"json.response.native_static.http_response_write", "json",
     "write a preencoded native Results.json response through the HTTP response writer", 1000U,
     100000U, bench_json_response_native_static_write,
     "response body is already JSON bytes; includes HTTP status/header/body framing but no schema "
     "serialization or sockets",
     false, sizeof("{\"ok\":true,\"items\":[1,2,3]}") - 1U, 1U, 0U},
    {"json.response.native_static.head_http_response_write", "json",
     "write preencoded native Results.json metadata while suppressing the body", 1000U, 100000U,
     bench_json_response_native_static_head_write,
     "models HEAD response writing after GET dispatch; body bytes are omitted on the wire", false,
     0U, 1U, 0U},
    {"json.response.large_list.http_response_write", "json",
     "write a large JSON list response body through the HTTP response writer", 100U, 10000U,
     bench_json_response_large_list_write,
     "large response/list writer row; body is preencoded JSON bytes", false, 9120U, 1U, 0U},
    {"json.dispatch.full_inprocess.generic_json", "json",
     "route a POST request, generically parse JSON, and return a native JSON response", 1000U,
     50000U, bench_json_dispatch_generic_json,
     "full in-process dispatch path without sockets; handler is bypassed by native response", false,
     (sizeof("{\"name\":\"Ada\",\"age\":42,\"active\":true}") - 1U) +
         (sizeof("{\"ok\":true}") - 1U),
     2U, 0U},
    {"json.dispatch.full_inprocess.native_json", "json",
     "route a POST request, natively validate JSON, and return a native JSON response", 1000U,
     50000U, bench_json_dispatch_native_json,
     "full in-process dispatch path without sockets; handler is bypassed by native response", false,
     (sizeof("{\"name\":\"Ada\",\"age\":42,\"active\":true}") - 1U) +
         (sizeof("{\"ok\":true}") - 1U),
     2U, 0U},
    {"json.dispatch.routes_1k.table_build", "json", "build a 1000-route dispatch table", 10U, 1000U,
     bench_json_dispatch_1k_route_table_build,
     "setup/build row only; no request dispatch, JSON parsing, validation, response, or sockets",
     false, 1000U, 1000U, 0U},
    {"json.dispatch.routes_10k.table_build", "json", "build a 10000-route dispatch table", 1U, 100U,
     bench_json_dispatch_10k_route_table_build,
     "setup/build row only; no request dispatch, JSON parsing, validation, response, or sockets",
     false, 10000U, 10000U, 0U},
    {"json.dispatch.routes_1k.native_json.dispatch_only", "json",
     "route a POST request through a 1000-route table with native JSON dispatch selected", 100U,
     10000U, bench_json_dispatch_1k_route_native_json_dispatch_only,
     "route table is built once before timing; excludes schema validation, handler execution, "
     "response writing, and sockets",
     false,
     (sizeof("{\"name\":\"Ada\",\"age\":42,\"active\":true}") - 1U) +
         (sizeof("{\"ok\":true}") - 1U),
     2U, 0U},
    {"json.dispatch.routes_10k.native_json.dispatch_only", "json",
     "route a POST request through a 10000-route table with native JSON dispatch selected", 10U,
     1000U, bench_json_dispatch_10k_route_native_json_dispatch_only,
     "route table is built once before timing; excludes schema validation, handler execution, "
     "response writing, and sockets",
     false,
     (sizeof("{\"name\":\"Ada\",\"age\":42,\"active\":true}") - 1U) +
         (sizeof("{\"ok\":true}") - 1U),
     2U, 0U},
    {"json.dispatch.routes_1k.generic_json.dispatch_only", "json",
     "route a POST request through a 1000-route table with generic JSON dispatch selected", 100U,
     10000U, bench_json_dispatch_1k_route_generic_json_dispatch_only,
     "route table is built once before timing; excludes schema validation, handler execution, "
     "response writing, and sockets",
     false,
     (sizeof("{\"name\":\"Ada\",\"age\":42,\"active\":true}") - 1U) +
         (sizeof("{\"ok\":true}") - 1U),
     2U, 0U},
    {"json.dispatch.routes_10k.generic_json.dispatch_only", "json",
     "route a POST request through a 10000-route table with generic JSON dispatch selected", 10U,
     1000U, bench_json_dispatch_10k_route_generic_json_dispatch_only,
     "route table is built once before timing; excludes schema validation, handler execution, "
     "response writing, and sockets",
     false,
     (sizeof("{\"name\":\"Ada\",\"age\":42,\"active\":true}") - 1U) +
         (sizeof("{\"ok\":true}") - 1U),
     2U, 0U},
    {"json.dispatch.routes_1k.native_json.full_inprocess", "json",
     "route through a 1000-route table with schema-backed JSON validation", 100U, 10000U,
     bench_json_dispatch_1k_route_native_json_full_inprocess,
     "route table is built once before timing; includes body policy, native schema validation, "
     "and native response materialization; no sockets or JavaScript handler",
     false,
     (sizeof("{\"name\":\"Ada\",\"age\":42,\"active\":true}") - 1U) +
         (sizeof("{\"ok\":true}") - 1U),
     2U, 0U},
    {"json.dispatch.routes_10k.native_json.full_inprocess", "json",
     "route through a 10000-route table with schema-backed JSON validation", 10U, 1000U,
     bench_json_dispatch_10k_route_native_json_full_inprocess,
     "route table is built once before timing; includes body policy, native schema validation, "
     "and native response materialization; no sockets or JavaScript handler",
     false,
     (sizeof("{\"name\":\"Ada\",\"age\":42,\"active\":true}") - 1U) +
         (sizeof("{\"ok\":true}") - 1U),
     2U, 0U},
    {"json.dispatch.routes_1k.generic_json.full_inprocess", "json",
     "route through a 1000-route table with generic JSON parsing", 100U, 10000U,
     bench_json_dispatch_1k_route_generic_json_full_inprocess,
     "route table is built once before timing; includes body policy, generic JSON parse, "
     "request validation metadata walk, and native response materialization; no sockets or "
     "JavaScript handler",
     false,
     (sizeof("{\"name\":\"Ada\",\"age\":42,\"active\":true}") - 1U) +
         (sizeof("{\"ok\":true}") - 1U),
     2U, 0U},
    {"json.dispatch.routes_10k.generic_json.full_inprocess", "json",
     "route through a 10000-route table with generic JSON parsing", 10U, 1000U,
     bench_json_dispatch_10k_route_generic_json_full_inprocess,
     "route table is built once before timing; includes body policy, generic JSON parse, "
     "request validation metadata walk, and native response materialization; no sockets or "
     "JavaScript handler",
     false,
     (sizeof("{\"name\":\"Ada\",\"age\":42,\"active\":true}") - 1U) +
         (sizeof("{\"ok\":true}") - 1U),
     2U, 0U},
    {"json.dispatch.native_schema_static_response.full_inprocess", "json",
     "validate a schema-backed JSON request then write a preencoded JSON response", 1000U, 50000U,
     bench_json_dispatch_validate_then_static_response,
     "bounded native request/response path only; no user handler or socket transport", false,
     (sizeof("{\"name\":\"Ada\",\"age\":42,\"active\":true}") - 1U) +
         (sizeof("{\"ok\":true}") - 1U),
     2U, 0U},
};

const SlBenchDefinition* sl_bench_json_dispatch_definitions(size_t* out_count)
{
    if (out_count != NULL) {
        *out_count = sizeof(json_dispatch_definitions) / sizeof(json_dispatch_definitions[0]);
    }

    return json_dispatch_definitions;
}

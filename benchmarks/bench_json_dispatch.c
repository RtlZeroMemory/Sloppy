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

    for (index = 0U; index < iterations; index += 1U) {
        yyjson_doc* doc = yyjson_read(body, strlen(body), 0U);

        if (doc == NULL) {
            return sl_status_from_code(SL_STATUS_INVALID_STATE);
        }
        checksum +=
            sl_bench_json_checksum(sl_bytes_from_parts((const unsigned char*)body, strlen(body)));
        yyjson_doc_free(doc);
    }

    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus bench_json_response_generic_serialize(const SlBenchContext* context,
                                                      uint64_t iterations, uint64_t* out_checksum)
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
        char* json = NULL;
        size_t length = 0U;

        if (doc == NULL) {
            return sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
        }
        root = yyjson_mut_obj(doc);
        tags = yyjson_mut_arr(doc);
        if (root == NULL || tags == NULL || !yyjson_mut_obj_add_str(doc, root, "name", "Ada") ||
            !yyjson_mut_obj_add_int(doc, root, "count", 7) ||
            !yyjson_mut_obj_add_real(doc, root, "score", 1.5) ||
            !yyjson_mut_obj_add_bool(doc, root, "ok", true) ||
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

static SlStatus bench_json_response_native_schema_write(const SlBenchContext* context,
                                                        uint64_t iterations, uint64_t* out_checksum)
{
    unsigned char arena_storage[8192];
    SlArena arena = {0};
    uint64_t checksum = 0U;
    uint64_t index = 0U;
    SlStatus status;

    (void)context;

    if (out_checksum == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_arena_init(&arena, arena_storage, sizeof(arena_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }

    for (index = 0U; index < iterations; index += 1U) {
        SlStringBuilder builder = {0};
        SlStr json = sl_str_empty();

        sl_arena_reset(&arena);
        status = sl_string_builder_init_arena(&builder, &arena, 128U, 4096U);
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_cstr(
                &builder,
                "{\"name\":\"Ada\",\"escaped\":\"line\\nquote\\\"slash\\\\\",\"count\":7,"
                "\"score\":1.5,\"ok\":true,\"note\":null,\"nested\":{\"label\":\"inner\"},"
                "\"tags\":[\"a\",\"b\"]}");
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

static SlStatus bench_json_dispatch_route_count(size_t route_count, SlPlanJsonRequestMode mode,
                                                uint64_t iterations, uint64_t* out_checksum)
{
    static unsigned char setup_storage[16U * 1024U * 1024U];
    unsigned char engine_storage[1024];
    unsigned char dispatch_storage[65536];
    SlArena setup_arena = {0};
    SlArena engine_arena = {0};
    SlArena dispatch_arena = {0};
    SlPlanRoute* routes = NULL;
    SlPlanHandler* handlers = NULL;
    char* path_storage = NULL;
    SlPlanSchemaProperty properties[3];
    SlPlanSchemaNode name_schema = {0};
    SlPlanSchemaNode age_schema = {0};
    SlPlanSchemaNode active_schema = {0};
    SlPlanSchema schema = {0};
    SlPlanRequestBinding binding = {.kind = SL_PLAN_REQUEST_BINDING_BODY_JSON,
                                    .schema = sl_str_from_cstr("CreateUser")};
    SlPlan plan;
    SlHttpRouteTable table = {0};
    SlEngine* engine = NULL;
    SlHttpRequestHead request = {0};
    SlHttpHeader headers[2] = {
        {.name = sl_str_from_cstr("content-type"), .value = sl_str_from_cstr("application/json")},
        {.name = sl_str_from_cstr("content-length"), .value = sl_str_from_cstr("37")}};
    static const unsigned char request_body[] = "{\"name\":\"Ada\",\"age\":42,\"active\":true}";
    static const char response_body[] = "{\"ok\":true}";
    uint64_t checksum = 0U;
    uint64_t index = 0U;
    size_t route_index = 0U;
    SlStatus status;

    if (out_checksum == NULL || route_count == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_arena_init(&setup_arena, setup_storage, sizeof(setup_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_init(&engine_arena, engine_storage, sizeof(engine_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_init(&dispatch_arena, dispatch_storage, sizeof(dispatch_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(&setup_arena, sizeof(SlPlanRoute) * route_count, _Alignof(SlPlanRoute),
                            (void**)&routes);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(&setup_arena, sizeof(SlPlanHandler) * route_count,
                            _Alignof(SlPlanHandler), (void**)&handlers);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(&setup_arena, 32U * route_count, _Alignof(char), (void**)&path_storage);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    memset(routes, 0, sizeof(SlPlanRoute) * route_count);
    memset(handlers, 0, sizeof(SlPlanHandler) * route_count);
    for (route_index = 0U; route_index < route_count; route_index += 1U) {
        char* path = &path_storage[route_index * 32U];
        int written = snprintf(path, 32U, "/json/%zu", route_index);

        if (written < 0 || written >= 32) {
            return sl_status_from_code(SL_STATUS_INVALID_STATE);
        }
        handlers[route_index] =
            (SlPlanHandler){(SlHandlerId)(route_index + 1U), sl_str_from_cstr("handler"),
                            sl_str_from_cstr("json dispatch handler")};
        routes[route_index].method = sl_str_from_cstr("POST");
        routes[route_index].pattern = sl_str_from_cstr(path);
        routes[route_index].handler_id = (SlHandlerId)(route_index + 1U);
    }

    sl_bench_json_schema_fixture(properties, &name_schema, &age_schema, &active_schema, &schema);
    routes[route_count - 1U].bindings = &binding;
    routes[route_count - 1U].binding_count = 1U;
    routes[route_count - 1U].json_request.mode = mode;
    routes[route_count - 1U].json_request.materialization =
        mode == SL_PLAN_JSON_REQUEST_NATIVE_SCHEMA ? SL_PLAN_JSON_MATERIALIZATION_MATERIALIZE_ONCE
                                                   : SL_PLAN_JSON_MATERIALIZATION_NONE;
    routes[route_count - 1U].json_request.unknown_fields = SL_PLAN_JSON_UNKNOWN_FIELDS_REJECT;
    routes[route_count - 1U].json_request.schema = sl_str_from_cstr("CreateUser");
    routes[route_count - 1U].json_request.max_body_bytes = SL_HTTP_DEFAULT_MAX_BODY_LENGTH;
    routes[route_count - 1U].json_request.max_depth = 50U;
    routes[route_count - 1U].json_request.max_string_bytes = 4096U;
    routes[route_count - 1U].json_request.max_array_length = 1024U;
    routes[route_count - 1U].native_response_kind = sl_str_from_cstr("json");
    routes[route_count - 1U].native_response_status = 200U;
    routes[route_count - 1U].native_response_body = sl_str_from_cstr(response_body);
    routes[route_count - 1U].native_response_content_type = sl_str_from_cstr("application/json");

    plan = sl_bench_json_plan(&schema, 1U);
    plan.target.platform = sl_str_from_cstr(SL_PLAN_TARGET_PLATFORM_WINDOWS_X64);
    plan.target.engine = sl_str_from_cstr("none");
    plan.handlers = handlers;
    plan.handler_count = route_count;
    plan.routes = routes;
    plan.route_count = route_count;

    status = sl_bench_json_create_noop_engine(&engine_arena, &engine);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http_route_table_build(&setup_arena, &plan, &table, NULL);
    if (!sl_status_is_ok(status)) {
        sl_engine_destroy(engine);
        return status;
    }

    request.method = SL_HTTP_METHOD_POST;
    request.path = routes[route_count - 1U].pattern;
    request.raw_target = request.path;
    request.headers = headers;
    request.header_count = sizeof(headers) / sizeof(headers[0]);
    request.body = sl_bytes_from_parts(request_body, sizeof(request_body) - 1U);

    for (index = 0U; index < iterations; index += 1U) {
        SlEngineResult result = {0};
        SlDiag diag = {0};

        sl_arena_reset(&dispatch_arena);
        status = sl_http_dispatch_request_head(&dispatch_arena, engine, &plan, &table.dispatch,
                                               &request, &result, &diag);
        if (!sl_status_is_ok(status) || result.kind != SL_ENGINE_RESULT_JSON ||
            result.response.status != 200U)
        {
            sl_engine_destroy(engine);
            return sl_status_is_ok(status) ? sl_status_from_code(SL_STATUS_INVALID_STATE) : status;
        }
        checksum += (uint64_t)result.response.status;
        checksum += sl_bench_json_checksum(result.response.body);
    }

    sl_engine_destroy(engine);
    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus bench_json_dispatch_generic_json(const SlBenchContext* context, uint64_t iterations,
                                                 uint64_t* out_checksum)
{
    (void)context;
    return bench_json_dispatch_route_count(1U, SL_PLAN_JSON_REQUEST_GENERIC, iterations,
                                           out_checksum);
}

static SlStatus bench_json_dispatch_native_json(const SlBenchContext* context, uint64_t iterations,
                                                uint64_t* out_checksum)
{
    (void)context;
    return bench_json_dispatch_route_count(1U, SL_PLAN_JSON_REQUEST_NATIVE_SCHEMA, iterations,
                                           out_checksum);
}

static SlStatus bench_json_dispatch_1k_route_native_json(const SlBenchContext* context,
                                                         uint64_t iterations,
                                                         uint64_t* out_checksum)
{
    (void)context;
    return bench_json_dispatch_route_count(1000U, SL_PLAN_JSON_REQUEST_NATIVE_SCHEMA, iterations,
                                           out_checksum);
}

static SlStatus bench_json_dispatch_10k_route_native_json(const SlBenchContext* context,
                                                          uint64_t iterations,
                                                          uint64_t* out_checksum)
{
    (void)context;
    return bench_json_dispatch_route_count(10000U, SL_PLAN_JSON_REQUEST_NATIVE_SCHEMA, iterations,
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
    {"json.request.generic_parse.small_login", "json",
     "generic baseline parse for a small JSON login body", 1000U, 100000U,
     bench_json_request_generic_parse_valid,
     "baseline JSON parse only; no schema validation, handler, socket, or response writer", false,
     sizeof("{\"username\":\"ada\",\"password\":\"longpass\"}") - 1U, 1U, 0U},
    {"json.request.native_schema.valid", "json",
     "validate a schema-backed JSON request body without entering JavaScript", 1000U, 100000U,
     bench_json_request_native_schema_valid,
     "parses and validates the request body; no sockets, response writer, or JavaScript handler",
     false, sizeof("{\"name\":\"Ada\",\"age\":42,\"active\":true}") - 1U, 1U, 0U},
    {"json.request.native_materialize_once.small_login", "json",
     "validate a schema-backed JSON body and materialize one cached JSON value", 1000U, 50000U,
     bench_json_request_native_materialize_once,
     "models the native-validated materialize-once handoff without entering V8", false,
     sizeof("{\"name\":\"Ada\",\"age\":42,\"active\":true}") - 1U, 1U, 0U},
    {"json.request.generic_parse.medium_body", "json",
     "generic baseline parse for a medium JSON body", 100U, 20000U,
     bench_json_request_generic_parse_medium_body,
     "medium body parser row; no schema validation, handler, socket, or response writer", false,
     4095U, 1U, 0U},
    {"json.request.generic_parse.malformed", "json", "generic baseline malformed JSON reject", 100U,
     20000U, bench_json_request_generic_parse_reject,
     "baseline malformed JSON parser rejection; no schema problem serialization", false,
     sizeof("{\"username\":\"ada\",\"password\":") - 1U, 1U, 0U},
    {"json.request.native_schema.reject", "json",
     "reject a schema-backed JSON request body and build validation problem details", 100U, 20000U,
     bench_json_request_native_schema_reject,
     "negative-path validation with max-bound and unknown-field diagnostics", false,
     sizeof("{\"name\":\"NameThatIsTooLongForTheSchemaLimit\",\"age\":121,\"active\":true,"
            "\"extra\":true}") -
         1U,
     1U, 0U},
    {"json.response.generic.serialize", "json", "serialize a dynamic JSON response through yyjson",
     1000U, 100000U, bench_json_response_generic_serialize,
     "generic response serialization baseline; no schema-backed native writer or sockets", false,
     sizeof("{\"name\":\"Ada\",\"count\":7,\"score\":1.5,\"ok\":true,\"tags\":[\"a\",\"b\"]}") - 1U,
     1U, 0U},
    {"json.response.native_static.write", "json", "write a preencoded native Results.json response",
     1000U, 100000U, bench_json_response_native_static_write,
     "response body is already JSON bytes; no schema serialization or sockets", false,
     sizeof("{\"ok\":true,\"items\":[1,2,3]}") - 1U, 1U, 0U},
    {"json.response.native_schema.write", "json",
     "write a supported schema-backed dynamic JSON response in stable field order", 1000U, 100000U,
     bench_json_response_native_schema_write,
     "native schema response writer shape; no sockets or JavaScript handler execution", false,
     sizeof("{\"name\":\"Ada\",\"escaped\":\"line\\nquote\\\"slash\\\\\",\"count\":7,\"score\":1.5,"
            "\"ok\":true,\"note\":null,\"nested\":{\"label\":\"inner\"},\"tags\":[\"a\",\"b\"]}") -
         1U,
     1U, 0U},
    {"json.response.native_static.head_write", "json",
     "write preencoded native Results.json metadata while suppressing the body", 1000U, 100000U,
     bench_json_response_native_static_head_write,
     "models HEAD response writing after GET dispatch; body bytes are omitted on the wire", false,
     0U, 1U, 0U},
    {"json.response.large_list.write", "json", "write a large JSON list response body", 100U,
     10000U, bench_json_response_large_list_write,
     "large response/list writer row; body is preencoded JSON bytes", false, 9120U, 1U, 0U},
    {"json.dispatch.full.generic_json", "json",
     "route a POST request, generically parse JSON, and return a native JSON response", 1000U,
     50000U, bench_json_dispatch_generic_json,
     "full in-process dispatch path without sockets; handler is bypassed by native response", false,
     (sizeof("{\"name\":\"Ada\",\"age\":42,\"active\":true}") - 1U) +
         (sizeof("{\"ok\":true}") - 1U),
     2U, 0U},
    {"json.dispatch.full.native_json", "json",
     "route a POST request, natively validate JSON, and return a native JSON response", 1000U,
     50000U, bench_json_dispatch_native_json,
     "full in-process dispatch path without sockets; handler is bypassed by native response", false,
     (sizeof("{\"name\":\"Ada\",\"age\":42,\"active\":true}") - 1U) +
         (sizeof("{\"ok\":true}") - 1U),
     2U, 0U},
    {"json.dispatch.routes_1k.native_json", "json",
     "route through a 1000-route table with schema-backed JSON validation", 100U, 10000U,
     bench_json_dispatch_1k_route_native_json,
     "route table is built before timing; no sockets or JavaScript handler", false,
     (sizeof("{\"name\":\"Ada\",\"age\":42,\"active\":true}") - 1U) +
         (sizeof("{\"ok\":true}") - 1U),
     2U, 0U},
    {"json.dispatch.routes_10k.native_json", "json",
     "route through a 10000-route table with schema-backed JSON validation", 10U, 1000U,
     bench_json_dispatch_10k_route_native_json,
     "route table is built before timing; no sockets or JavaScript handler", false,
     (sizeof("{\"name\":\"Ada\",\"age\":42,\"active\":true}") - 1U) +
         (sizeof("{\"ok\":true}") - 1U),
     2U, 0U},
    {"json.dispatch.native_schema_static_response", "json",
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

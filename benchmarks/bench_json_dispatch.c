#include "bench_internal.h"

#include "sloppy/http_context.h"
#include "sloppy/http_response.h"
#include "sloppy/request_validation.h"

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
    {"json.request.native_schema.valid", "json",
     "validate a schema-backed JSON request body without entering JavaScript", 1000U, 100000U,
     bench_json_request_native_schema_valid,
     "parses and validates the request body; no sockets, response writer, or JavaScript handler",
     false, sizeof("{\"name\":\"Ada\",\"age\":42,\"active\":true}") - 1U, 1U, 0U},
    {"json.request.native_schema.reject", "json",
     "reject a schema-backed JSON request body and build validation problem details", 100U, 20000U,
     bench_json_request_native_schema_reject,
     "negative-path validation with max-bound and unknown-field diagnostics", false,
     sizeof("{\"name\":\"NameThatIsTooLongForTheSchemaLimit\",\"age\":121,\"active\":true,"
            "\"extra\":true}") -
         1U,
     1U, 0U},
    {"json.response.native_static.write", "json", "write a preencoded native Results.json response",
     1000U, 100000U, bench_json_response_native_static_write,
     "response body is already JSON bytes; no schema serialization or sockets", false,
     sizeof("{\"ok\":true,\"items\":[1,2,3]}") - 1U, 1U, 0U},
    {"json.response.native_static.head_write", "json",
     "write preencoded native Results.json metadata while suppressing the body", 1000U, 100000U,
     bench_json_response_native_static_head_write,
     "models HEAD response writing after GET dispatch; body bytes are omitted on the wire", false,
     0U, 1U, 0U},
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

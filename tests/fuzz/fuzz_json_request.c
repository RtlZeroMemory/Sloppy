#include "sloppy/request_validation.h"

#include "fuzz_support.h"

#include <stddef.h>
#include <stdint.h>

#define FUZZ_JSON_REQUEST_ARENA_SIZE 131072U

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    unsigned char arena_storage[FUZZ_JSON_REQUEST_ARENA_SIZE];
    SlArena arena = {0};
    SlPlanSchemaProperty properties[2];
    SlPlanSchemaNode name_schema = {.kind = SL_PLAN_SCHEMA_STRING};
    SlPlanSchemaNode count_schema = {.kind = SL_PLAN_SCHEMA_INT};
    SlPlanSchemaNode body_schema = {
        .kind = SL_PLAN_SCHEMA_OBJECT, .properties = properties, .property_count = 2U};
    SlPlanSchema schema = {.name = sl_str_from_cstr("FuzzBody"), .definition = body_schema};
    SlPlanRequestBinding binding = {.kind = SL_PLAN_REQUEST_BINDING_BODY_JSON,
                                    .schema = sl_str_from_cstr("FuzzBody")};
    SlPlanRoute route = {.bindings = &binding,
                         .binding_count = 1U,
                         .json_request = {
                             .mode = SL_PLAN_JSON_REQUEST_NATIVE_SCHEMA,
                             .materialization = SL_PLAN_JSON_MATERIALIZATION_MATERIALIZE_ONCE,
                             .unknown_fields = SL_PLAN_JSON_UNKNOWN_FIELDS_IGNORE,
                             .schema = sl_str_from_cstr("FuzzBody"),
                             .max_depth = 32U,
                         }};
    SlPlan plan = {
        .version = 1U, .routes = &route, .route_count = 1U, .schemas = &schema, .schema_count = 1U};
    SlHttpRequestHead request = {0};
    SlHttpRequestContext context = {0};
    SlEngineResult result = {0};
    SlDiag diag = {0};
    size_t body_offset = 0U;

    if (data == NULL || size == 0U) {
        return 0;
    }
    if (!sl_status_is_ok(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)))) {
        return 0;
    }

    name_schema.has_max = true;
    name_schema.max_value = (int64_t)((data[0] % 64U) + 1U);
    count_schema.has_max = true;
    count_schema.max_value = (int64_t)(data[0] % 32U);
    if (size > 1U && (data[1] & 1U) != 0U) {
        route.json_request.unknown_fields = SL_PLAN_JSON_UNKNOWN_FIELDS_REJECT;
    }
    if (size > 2U) {
        route.json_request.max_array_length = (size_t)((data[2] % 16U) + 1U);
        body_offset = 3U;
    }

    properties[0] =
        (SlPlanSchemaProperty){.name = sl_str_from_cstr("name"), .schema = &name_schema};
    properties[1] =
        (SlPlanSchemaProperty){.name = sl_str_from_cstr("count"), .schema = &count_schema};

    request.body = sl_bytes_from_parts(data + body_offset, size - body_offset);
    context.request = &request;
    context.body_kind = SL_HTTP_REQUEST_BODY_JSON;

    sl_request_validation_validate(&arena, &plan, &route, &context, &result, &diag);
    return 0;
}

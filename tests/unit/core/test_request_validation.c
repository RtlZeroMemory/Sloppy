#include "sloppy/request_validation.h"

#include "sloppy/json_profile.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static bool bytes_contains(SlBytes bytes, const char* needle)
{
    size_t needle_len = strlen(needle);
    size_t index = 0U;

    if (needle == NULL || needle_len == 0U || bytes.ptr == NULL || bytes.length < needle_len) {
        return false;
    }
    for (index = 0U; index <= bytes.length - needle_len; index += 1U) {
        if (memcmp(bytes.ptr + index, needle, needle_len) == 0) {
            return true;
        }
    }
    return false;
}

static size_t bytes_count(SlBytes bytes, const char* needle)
{
    size_t needle_len = strlen(needle);
    size_t index = 0U;
    size_t count = 0U;

    if (needle == NULL || needle_len == 0U || bytes.ptr == NULL || bytes.length < needle_len) {
        return 0U;
    }
    for (index = 0U; index <= bytes.length - needle_len; index += 1U) {
        if (memcmp(bytes.ptr + index, needle, needle_len) == 0) {
            count += 1U;
        }
    }
    return count;
}

static bool validation_problem_has_issue_shape(SlBytes bytes)
{
    return bytes_contains(bytes, "\"code\":\"SLOPPY_E_VALIDATION_FAILED\"") &&
           bytes_contains(bytes, "\"errors\":[") && bytes_contains(bytes, "\"path\":") &&
           bytes_contains(bytes, "\"code\":") && bytes_contains(bytes, "\"message\":");
}

static SlPlan empty_plan(void)
{
    SlPlan plan = {0};
    plan.version = 1U;
    return plan;
}

static SlHttpRequestContext request_context(SlHttpRequestHead* request)
{
    SlHttpRequestContext context = {0};
    context.request = request;
    context.body_kind = SL_HTTP_REQUEST_BODY_JSON;
    context.protocol = sl_str_from_cstr("http/1.1");
    context.scheme = sl_str_from_cstr("http");
    return context;
}

static void enable_json_profile_for_process(void)
{
#if defined(_WIN32)
    _putenv_s("SLOPPY_JSON_PROFILE", "1");
#else
    setenv("SLOPPY_JSON_PROFILE", "1", 1);
#endif
}

static int test_valid_native_body_profile_does_not_render_paths(void)
{
    unsigned char arena_storage[8192];
    SlArena arena = {0};
    SlPlanSchemaProperty properties[1];
    SlPlanSchemaNode name_schema = {.kind = SL_PLAN_SCHEMA_STRING};
    SlPlanSchemaNode body_schema = {
        .kind = SL_PLAN_SCHEMA_OBJECT, .properties = properties, .property_count = 1U};
    SlPlanSchema schema = {.name = sl_str_from_cstr("CreateUser"), .definition = body_schema};
    SlPlan plan = empty_plan();
    SlPlanRequestBinding binding = {.kind = SL_PLAN_REQUEST_BINDING_BODY_JSON,
                                    .schema = sl_str_from_cstr("CreateUser")};
    SlPlanRoute route = {.bindings = &binding,
                         .binding_count = 1U,
                         .json_request = {
                             .mode = SL_PLAN_JSON_REQUEST_NATIVE_SCHEMA,
                             .schema = sl_str_from_cstr("CreateUser"),
                         }};
    SlHttpRequestHead request = {0};
    SlHttpRequestContext context = request_context(&request);
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlJsonProfileSnapshot snapshot = {0};

    enable_json_profile_for_process();
    properties[0] =
        (SlPlanSchemaProperty){.name = sl_str_from_cstr("name"), .schema = &name_schema};
    plan.schemas = &schema;
    plan.schema_count = 1U;
    context.request_schema = &schema;
    request.body = sl_bytes_from_parts((const unsigned char*)"{\"name\":\"Ada\"}",
                                       sizeof("{\"name\":\"Ada\"}") - 1U);

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
        0)
    {
        return 1;
    }

    sl_json_profile_reset("valid_native_body_profile", 1U);
    if (expect_status(
            sl_request_validation_validate(&arena, &plan, &route, &context, &result, &diag),
            SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_NONE)
    {
        return 2;
    }
    sl_json_profile_snapshot(&snapshot);
    if (!sl_json_profile_snapshot_has_data(&snapshot) ||
        snapshot.counters[SL_JSON_PROFILE_COUNTER_PATHS_RENDERED] != 0U ||
        snapshot.counters[SL_JSON_PROFILE_COUNTER_ISSUES_EMITTED] != 0U)
    {
        return 3;
    }

    return 0;
}

static int test_no_bindings_and_invalid_arguments_are_output_atomic(void)
{
    unsigned char arena_storage[4096];
    SlArena arena = {0};
    SlPlan plan = empty_plan();
    SlPlanRoute route = {0};
    SlHttpRequestHead request = {0};
    SlHttpRequestContext context = request_context(&request);
    SlEngineResult result = {.kind = SL_ENGINE_RESULT_TEXT, .text = sl_str_from_cstr("sentinel")};
    SlDiag diag = {.code = SL_DIAG_INTERNAL_ERROR};

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
        0)
    {
        return 1;
    }

    if (expect_status(
            sl_request_validation_validate(&arena, &plan, &route, &context, &result, &diag),
            SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_NONE)
    {
        return 2;
    }

    result.kind = SL_ENGINE_RESULT_TEXT;
    diag.code = SL_DIAG_INTERNAL_ERROR;
    if (expect_status(sl_request_validation_validate(&arena, &plan, &route, &context, NULL, &diag),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_INTERNAL_ERROR)
    {
        return 3;
    }

    if (expect_status(sl_request_validation_validate(NULL, &plan, &route, &context, &result, &diag),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_NONE)
    {
        return 4;
    }

    route.binding_count = 1U;
    route.bindings = NULL;
    result.kind = SL_ENGINE_RESULT_TEXT;
    diag.code = SL_DIAG_INTERNAL_ERROR;
    if (expect_status(
            sl_request_validation_validate(&arena, &plan, &route, &context, &result, &diag),
            SL_STATUS_INVALID_ARGUMENT) != 0 ||
        result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_NONE)
    {
        return 5;
    }

    return 0;
}

static int test_body_schema_metadata_failures_publish_problem_details(void)
{
    unsigned char arena_storage[8192];
    SlArena arena = {0};
    SlPlan plan = empty_plan();
    SlHttpRequestHead request = {0};
    SlHttpRequestContext context = request_context(&request);
    SlPlanRequestBinding binding = {.kind = SL_PLAN_REQUEST_BINDING_BODY_JSON};
    SlPlanRoute route = {.bindings = &binding, .binding_count = 1U};
    SlEngineResult result = {0};
    SlDiag diag = {0};

    request.body = sl_bytes_from_parts((const unsigned char*)"{\"name\":\"Ada\"}",
                                       sizeof("{\"name\":\"Ada\"}") - 1U);

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
        0)
    {
        return 10;
    }

    if (expect_status(
            sl_request_validation_validate(&arena, &plan, &route, &context, &result, &diag),
            SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_ERROR || result.response.status != 400U ||
        diag.code != SL_DIAG_REQUEST_VALIDATION_FAILED ||
        !validation_problem_has_issue_shape(result.response.body) ||
        !bytes_contains(result.response.body, "JSON body schema metadata is missing."))
    {
        return 11;
    }

    sl_arena_reset(&arena);
    result = (SlEngineResult){0};
    diag = (SlDiag){0};
    binding.schema = sl_str_from_cstr("CreateUser");
    if (expect_status(
            sl_request_validation_validate(&arena, &plan, &route, &context, &result, &diag),
            SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_ERROR || result.response.status != 400U ||
        diag.code != SL_DIAG_REQUEST_VALIDATION_FAILED ||
        !validation_problem_has_issue_shape(result.response.body) ||
        !bytes_contains(result.response.body, "JSON body schema is not available at runtime."))
    {
        return 12;
    }

    return 0;
}

static int test_header_lookup_is_case_insensitive_and_success_is_silent(void)
{
    unsigned char arena_storage[4096];
    SlArena arena = {0};
    SlHttpHeader headers[1] = {{sl_str_from_cstr("X-Request-Id"), sl_str_from_cstr("req-42")}};
    SlHttpRequestHead request = {.headers = headers, .header_count = 1U};
    SlHttpRequestContext context = request_context(&request);
    SlPlan plan = empty_plan();
    SlPlanRequestBinding binding = {.kind = SL_PLAN_REQUEST_BINDING_HEADER,
                                    .name = sl_str_from_cstr("x-request-id"),
                                    .type = sl_str_from_cstr("string")};
    SlPlanRoute route = {.bindings = &binding, .binding_count = 1U};
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
        0)
    {
        return 20;
    }

    if (expect_status(
            sl_request_validation_validate(&arena, &plan, &route, &context, &result, &diag),
            SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_NONE)
    {
        return 21;
    }

    return 0;
}

static int test_scalar_bindings_coerce_route_query_and_header_values(void)
{
    unsigned char arena_storage[4096];
    SlArena arena = {0};
    SlPlan plan = empty_plan();
    SlPlanRequestBinding bindings[3] = {{.kind = SL_PLAN_REQUEST_BINDING_ROUTE,
                                         .name = sl_str_from_cstr("id"),
                                         .type = sl_str_from_cstr("Route<number>")},
                                        {.kind = SL_PLAN_REQUEST_BINDING_QUERY,
                                         .name = sl_str_from_cstr("includeDeleted"),
                                         .type = sl_str_from_cstr("Query<boolean>")},
                                        {.kind = SL_PLAN_REQUEST_BINDING_HEADER,
                                         .name = sl_str_from_cstr("x-enabled"),
                                         .type = sl_str_from_cstr("bool")}};
    SlPlanRoute route = {.bindings = bindings, .binding_count = 3U};
    SlHttpHeader headers[1] = {{sl_str_from_cstr("X-Enabled"), sl_str_from_cstr("FALSE")}};
    SlRouteParam route_params[1] = {{.name = sl_str_from_cstr("id"),
                                     .value = sl_str_from_cstr("-42"),
                                     .kind = SL_ROUTE_PARAM_STRING}};
    SlHttpQueryParam query_params[1] = {
        {sl_str_from_cstr("includeDeleted"), sl_str_from_cstr("TrUe")}};
    SlHttpRequestHead request = {.headers = headers, .header_count = 1U};
    SlHttpRequestContext context = request_context(&request);
    SlEngineResult result = {0};
    SlDiag diag = {0};

    context.route_params = route_params;
    context.route_param_count = 1U;
    context.query_params = query_params;
    context.query_param_count = 1U;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
        0)
    {
        return 22;
    }

    if (expect_status(
            sl_request_validation_validate(&arena, &plan, &route, &context, &result, &diag),
            SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_NONE)
    {
        return 23;
    }

    query_params[0].value = sl_str_from_cstr("maybe");
    if (expect_status(
            sl_request_validation_validate(&arena, &plan, &route, &context, &result, &diag),
            SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_ERROR || result.response.status != 400U ||
        diag.code != SL_DIAG_REQUEST_VALIDATION_FAILED ||
        !validation_problem_has_issue_shape(result.response.body) ||
        !bytes_contains(result.response.body, "\"includeDeleted\"") ||
        !bytes_contains(result.response.body, "Expected a boolean value."))
    {
        return 24;
    }

    return 0;
}

static int test_issue_cap_truncates_without_failure(void)
{
    unsigned char arena_storage[16384];
    SlArena arena = {0};
    SlPlan plan = empty_plan();
    SlHttpRequestHead request = {0};
    SlHttpRequestContext context = request_context(&request);
    SlPlanRequestBinding bindings[10];
    SlPlanRoute route = {.bindings = bindings, .binding_count = 10U};
    SlEngineResult result = {0};
    SlDiag diag = {0};
    size_t index = 0U;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
        0)
    {
        return 30;
    }

    for (index = 0U; index < 10U; index += 1U) {
        bindings[index] = (SlPlanRequestBinding){
            .kind = SL_PLAN_REQUEST_BINDING_QUERY,
            .name = sl_str_from_cstr(index == 9U ? "q9" : "q"),
            .type = sl_str_from_cstr("string"),
        };
    }

    if (expect_status(
            sl_request_validation_validate(&arena, &plan, &route, &context, &result, &diag),
            SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_ERROR || result.response.status != 400U ||
        diag.code != SL_DIAG_REQUEST_VALIDATION_FAILED ||
        !validation_problem_has_issue_shape(result.response.body) ||
        bytes_count(result.response.body, "Query parameter is required.") == 0U ||
        bytes_contains(result.response.body, "\"q9\""))
    {
        return 31;
    }

    return 0;
}

static int test_body_schema_validates_required_literal_and_email_contracts(void)
{
    unsigned char arena_storage[16384];
    SlArena arena = {0};
    SlPlanSchemaProperty properties[3];
    SlPlanSchemaNode name_schema = {.kind = SL_PLAN_SCHEMA_STRING};
    SlPlanSchemaNode email_schema = {.kind = SL_PLAN_SCHEMA_STRING,
                                     .validation = sl_str_from_cstr("email")};
    SlPlanSchemaNode kind_schema = {.kind = SL_PLAN_SCHEMA_LITERAL,
                                    .literal_kind = SL_PLAN_SCHEMA_LITERAL_STRING,
                                    .literal_string = sl_str_from_cstr("user")};
    SlPlanSchemaNode body_schema = {
        .kind = SL_PLAN_SCHEMA_OBJECT, .properties = properties, .property_count = 3U};
    SlPlanSchema schema = {.name = sl_str_from_cstr("CreateUser"), .definition = body_schema};
    SlPlan plan = empty_plan();
    SlPlanRequestBinding binding = {.kind = SL_PLAN_REQUEST_BINDING_BODY_JSON,
                                    .schema = sl_str_from_cstr("CreateUser")};
    SlPlanRoute route = {.bindings = &binding, .binding_count = 1U};
    SlHttpRequestHead request = {0};
    SlHttpRequestContext context = request_context(&request);
    SlEngineResult result = {0};
    SlDiag diag = {0};

    properties[0] =
        (SlPlanSchemaProperty){.name = sl_str_from_cstr("name"), .schema = &name_schema};
    properties[1] =
        (SlPlanSchemaProperty){.name = sl_str_from_cstr("email"), .schema = &email_schema};
    properties[2] =
        (SlPlanSchemaProperty){.name = sl_str_from_cstr("kind"), .schema = &kind_schema};
    plan.schemas = &schema;
    plan.schema_count = 1U;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
        0)
    {
        return 40;
    }

    request.body =
        sl_bytes_from_parts((const unsigned char*)"{\"email\":\"not-an-email\",\"kind\":\"admin\"}",
                            sizeof("{\"email\":\"not-an-email\",\"kind\":\"admin\"}") - 1U);
    if (expect_status(
            sl_request_validation_validate(&arena, &plan, &route, &context, &result, &diag),
            SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_ERROR || result.response.status != 400U ||
        diag.code != SL_DIAG_REQUEST_VALIDATION_FAILED ||
        !validation_problem_has_issue_shape(result.response.body) ||
        !bytes_contains(result.response.body, "\"body.name\"") ||
        !bytes_contains(result.response.body, "Field is required.") ||
        !bytes_contains(result.response.body, "\"body.email\"") ||
        !bytes_contains(result.response.body, "\"string.email\"") ||
        !bytes_contains(result.response.body, "Expected an email address.") ||
        !bytes_contains(result.response.body, "\"body.kind\"") ||
        !bytes_contains(result.response.body, "Expected the declared literal value.") ||
        bytes_contains(result.response.body, "admin") ||
        bytes_contains(result.response.body, "not-an-email"))
    {
        return 41;
    }

    sl_arena_reset(&arena);
    result = (SlEngineResult){0};
    diag = (SlDiag){0};
    request.body = sl_bytes_from_parts(
        (const unsigned char*)"{\"name\":\"Ada\",\"email\":\"ada@example.test\",\"kind\":\"user\"}",
        sizeof("{\"name\":\"Ada\",\"email\":\"ada@example.test\",\"kind\":\"user\"}") - 1U);
    if (expect_status(
            sl_request_validation_validate(&arena, &plan, &route, &context, &result, &diag),
            SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_NONE)
    {
        return 42;
    }

    return 0;
}

static int test_nested_optional_and_nullable_shapes_validate_precisely(void)
{
    unsigned char arena_storage[16384];
    SlArena arena = {0};
    SlPlanSchemaProperty root_properties[1];
    SlPlanSchemaProperty profile_properties[2];
    SlPlanSchemaNode display_name = {.kind = SL_PLAN_SCHEMA_STRING};
    SlPlanSchemaNode avatar = {.kind = SL_PLAN_SCHEMA_STRING, .nullable = true};
    SlPlanSchemaNode profile = {.kind = SL_PLAN_SCHEMA_OBJECT,
                                .properties = profile_properties,
                                .property_count = 2U,
                                .optional = true};
    SlPlanSchemaNode body_schema = {
        .kind = SL_PLAN_SCHEMA_OBJECT, .properties = root_properties, .property_count = 1U};
    SlPlanSchema schema = {.name = sl_str_from_cstr("ProfileUpdate"), .definition = body_schema};
    SlPlan plan = empty_plan();
    SlPlanRequestBinding binding = {.kind = SL_PLAN_REQUEST_BINDING_BODY_JSON,
                                    .schema = sl_str_from_cstr("ProfileUpdate")};
    SlPlanRoute route = {.bindings = &binding, .binding_count = 1U};
    SlHttpRequestHead request = {0};
    SlHttpRequestContext context = request_context(&request);
    SlEngineResult result = {0};
    SlDiag diag = {0};

    profile_properties[0] =
        (SlPlanSchemaProperty){.name = sl_str_from_cstr("displayName"), .schema = &display_name};
    profile_properties[1] =
        (SlPlanSchemaProperty){.name = sl_str_from_cstr("avatar"), .schema = &avatar};
    root_properties[0] =
        (SlPlanSchemaProperty){.name = sl_str_from_cstr("profile"), .schema = &profile};
    plan.schemas = &schema;
    plan.schema_count = 1U;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
        0)
    {
        return 43;
    }

    request.body = sl_bytes_from_parts((const unsigned char*)"{}", sizeof("{}") - 1U);
    if (expect_status(
            sl_request_validation_validate(&arena, &plan, &route, &context, &result, &diag),
            SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_NONE)
    {
        return 44;
    }

    sl_arena_reset(&arena);
    result = (SlEngineResult){0};
    diag = (SlDiag){0};
    request.body = sl_bytes_from_parts(
        (const unsigned char*)"{\"profile\":{\"displayName\":\"Ada\",\"avatar\":null}}",
        sizeof("{\"profile\":{\"displayName\":\"Ada\",\"avatar\":null}}") - 1U);
    if (expect_status(
            sl_request_validation_validate(&arena, &plan, &route, &context, &result, &diag),
            SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_NONE)
    {
        return 45;
    }

    sl_arena_reset(&arena);
    result = (SlEngineResult){0};
    diag = (SlDiag){0};
    request.body = sl_bytes_from_parts(
        (const unsigned char*)"{\"profile\":{\"avatar\":\"https://example.test/a.png\"}}",
        sizeof("{\"profile\":{\"avatar\":\"https://example.test/a.png\"}}") - 1U);
    if (expect_status(
            sl_request_validation_validate(&arena, &plan, &route, &context, &result, &diag),
            SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_ERROR || result.response.status != 400U ||
        diag.code != SL_DIAG_REQUEST_VALIDATION_FAILED ||
        !validation_problem_has_issue_shape(result.response.body) ||
        !bytes_contains(result.response.body, "\"body.profile.displayName\"") ||
        !bytes_contains(result.response.body, "Field is required."))
    {
        return 46;
    }

    return 0;
}

static int test_nested_unknown_and_array_item_paths_are_precise(void)
{
    unsigned char arena_storage[32768];
    SlArena arena = {0};
    SlPlanSchemaProperty root_properties[1];
    SlPlanSchemaProperty profile_properties[2];
    SlPlanSchemaNode display_name = {.kind = SL_PLAN_SCHEMA_STRING};
    SlPlanSchemaNode tag_item = {.kind = SL_PLAN_SCHEMA_INT};
    SlPlanSchemaNode tags = {.kind = SL_PLAN_SCHEMA_ARRAY, .items = &tag_item};
    SlPlanSchemaNode profile = {
        .kind = SL_PLAN_SCHEMA_OBJECT, .properties = profile_properties, .property_count = 2U};
    SlPlanSchemaNode body_schema = {
        .kind = SL_PLAN_SCHEMA_OBJECT, .properties = root_properties, .property_count = 1U};
    SlPlanSchema schema = {.name = sl_str_from_cstr("ProfileUpdate"), .definition = body_schema};
    SlPlan plan = empty_plan();
    SlPlanRequestBinding binding = {.kind = SL_PLAN_REQUEST_BINDING_BODY_JSON,
                                    .schema = sl_str_from_cstr("ProfileUpdate")};
    SlPlanRoute route = {.bindings = &binding,
                         .binding_count = 1U,
                         .json_request = {
                             .mode = SL_PLAN_JSON_REQUEST_NATIVE_SCHEMA,
                             .unknown_fields = SL_PLAN_JSON_UNKNOWN_FIELDS_REJECT,
                             .schema = sl_str_from_cstr("ProfileUpdate"),
                         }};
    SlHttpRequestHead request = {0};
    SlHttpRequestContext context = request_context(&request);
    SlEngineResult result = {0};
    SlDiag diag = {0};

    profile_properties[0] =
        (SlPlanSchemaProperty){.name = sl_str_from_cstr("displayName"), .schema = &display_name};
    profile_properties[1] =
        (SlPlanSchemaProperty){.name = sl_str_from_cstr("tags"), .schema = &tags};
    root_properties[0] =
        (SlPlanSchemaProperty){.name = sl_str_from_cstr("profile"), .schema = &profile};
    plan.schemas = &schema;
    plan.schema_count = 1U;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
        0)
    {
        return 70;
    }

    request.body = sl_bytes_from_parts(
        (const unsigned char*)"{\"profile\":{\"displayName\":\"Ada\",\"extra\":true}}",
        sizeof("{\"profile\":{\"displayName\":\"Ada\",\"extra\":true}}") - 1U);
    if (expect_status(
            sl_request_validation_validate(&arena, &plan, &route, &context, &result, &diag),
            SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_ERROR || result.response.status != 400U ||
        !validation_problem_has_issue_shape(result.response.body) ||
        !bytes_contains(result.response.body, "\"body.profile.extra\"") ||
        !bytes_contains(result.response.body, "\"unknown\""))
    {
        return 71;
    }

    sl_arena_reset(&arena);
    result = (SlEngineResult){0};
    diag = (SlDiag){0};
    request.body = sl_bytes_from_parts(
        (const unsigned char*)"{\"profile\":{\"displayName\":\"Ada\",\"tags\":[1,"
                              "\"SECRET_PAYLOAD_SHOULD_NOT_APPEAR\"]}}",
        sizeof("{\"profile\":{\"displayName\":\"Ada\",\"tags\":[1,"
               "\"SECRET_PAYLOAD_SHOULD_NOT_APPEAR\"]}}") -
            1U);
    if (expect_status(
            sl_request_validation_validate(&arena, &plan, &route, &context, &result, &diag),
            SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_ERROR || result.response.status != 400U ||
        !validation_problem_has_issue_shape(result.response.body) ||
        !bytes_contains(result.response.body, "\"body.profile.tags[1]\"") ||
        !bytes_contains(result.response.body, "Expected an integer.") ||
        bytes_contains(result.response.body, "SECRET_PAYLOAD_SHOULD_NOT_APPEAR"))
    {
        return 72;
    }

    return 0;
}

static int test_large_object_required_field_fallback_handles_more_than_64_properties(void)
{
    unsigned char arena_storage[32768];
    SlArena arena = {0};
    SlPlanSchemaProperty properties[65];
    SlPlanSchemaNode optional_string = {.kind = SL_PLAN_SCHEMA_STRING, .optional = true};
    SlPlanSchemaNode required_string = {.kind = SL_PLAN_SCHEMA_STRING};
    SlPlanSchemaNode body_schema = {
        .kind = SL_PLAN_SCHEMA_OBJECT, .properties = properties, .property_count = 65U};
    SlPlanSchema schema = {.name = sl_str_from_cstr("LargeShape"), .definition = body_schema};
    SlPlan plan = empty_plan();
    SlPlanRequestBinding binding = {.kind = SL_PLAN_REQUEST_BINDING_BODY_JSON,
                                    .schema = sl_str_from_cstr("LargeShape")};
    SlPlanRoute route = {.bindings = &binding,
                         .binding_count = 1U,
                         .json_request = {
                             .mode = SL_PLAN_JSON_REQUEST_NATIVE_SCHEMA,
                             .schema = sl_str_from_cstr("LargeShape"),
                         }};
    SlHttpRequestHead request = {0};
    SlHttpRequestContext context = request_context(&request);
    SlEngineResult result = {0};
    SlDiag diag = {0};

    for (size_t index = 0U; index < 64U; index += 1U) {
        properties[index] =
            (SlPlanSchemaProperty){.name = sl_str_from_cstr("unused"), .schema = &optional_string};
    }
    properties[64] =
        (SlPlanSchemaProperty){.name = sl_str_from_cstr("p64"), .schema = &required_string};
    plan.schemas = &schema;
    plan.schema_count = 1U;
    request.body = sl_bytes_from_parts((const unsigned char*)"{\"p64\":\"ok\"}",
                                       sizeof("{\"p64\":\"ok\"}") - 1U);

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
        0)
    {
        return 80;
    }

    if (expect_status(
            sl_request_validation_validate(&arena, &plan, &route, &context, &result, &diag),
            SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_NONE)
    {
        return 81;
    }

    return 0;
}

static int test_deep_configured_depth_over_path_frame_limit_remains_valid(void)
{
    unsigned char arena_storage[65536];
    char body[1024];
    size_t cursor = 0U;
    SlArena arena = {0};
    SlPlanSchemaProperty property = {0};
    SlPlanSchemaNode recursive = {.kind = SL_PLAN_SCHEMA_OBJECT, .optional = true};
    SlPlanSchema schema = {.name = sl_str_from_cstr("Recursive"), .definition = recursive};
    SlPlan plan = empty_plan();
    SlPlanRequestBinding binding = {.kind = SL_PLAN_REQUEST_BINDING_BODY_JSON,
                                    .schema = sl_str_from_cstr("Recursive")};
    SlPlanRoute route = {.bindings = &binding,
                         .binding_count = 1U,
                         .json_request = {
                             .mode = SL_PLAN_JSON_REQUEST_NATIVE_SCHEMA,
                             .max_depth = 80U,
                             .schema = sl_str_from_cstr("Recursive"),
                         }};
    SlHttpRequestHead request = {0};
    SlHttpRequestContext context = request_context(&request);
    SlEngineResult result = {0};
    SlDiag diag = {0};

    property = (SlPlanSchemaProperty){.name = sl_str_from_cstr("x"), .schema = &recursive};
    recursive.properties = &property;
    recursive.property_count = 1U;
    schema.definition = recursive;
    plan.schemas = &schema;
    plan.schema_count = 1U;

    for (size_t depth = 0U; depth < 70U; depth += 1U) {
        body[cursor] = '{';
        cursor += 1U;
        body[cursor] = '"';
        cursor += 1U;
        body[cursor] = 'x';
        cursor += 1U;
        body[cursor] = '"';
        cursor += 1U;
        body[cursor] = ':';
        cursor += 1U;
    }
    body[cursor] = '{';
    cursor += 1U;
    body[cursor] = '}';
    cursor += 1U;
    for (size_t depth = 0U; depth < 70U; depth += 1U) {
        body[cursor] = '}';
        cursor += 1U;
    }
    request.body = sl_bytes_from_parts((const unsigned char*)body, cursor);

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
        0)
    {
        return 90;
    }

    if (expect_status(
            sl_request_validation_validate(&arena, &plan, &route, &context, &result, &diag),
            SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_NONE)
    {
        return 91;
    }

    return 0;
}

static int test_long_unknown_field_path_degrades_to_validation_problem(void)
{
    unsigned char arena_storage[65536];
    char body[8192];
    size_t cursor = 0U;
    SlArena arena = {0};
    SlPlanSchemaNode body_schema = {.kind = SL_PLAN_SCHEMA_OBJECT};
    SlPlanSchema schema = {.name = sl_str_from_cstr("Empty"), .definition = body_schema};
    SlPlan plan = empty_plan();
    SlPlanRequestBinding binding = {.kind = SL_PLAN_REQUEST_BINDING_BODY_JSON,
                                    .schema = sl_str_from_cstr("Empty")};
    SlPlanRoute route = {.bindings = &binding,
                         .binding_count = 1U,
                         .json_request = {
                             .mode = SL_PLAN_JSON_REQUEST_NATIVE_SCHEMA,
                             .unknown_fields = SL_PLAN_JSON_UNKNOWN_FIELDS_REJECT,
                             .schema = sl_str_from_cstr("Empty"),
                         }};
    SlHttpRequestHead request = {0};
    SlHttpRequestContext context = request_context(&request);
    SlEngineResult result = {0};
    SlDiag diag = {0};

    plan.schemas = &schema;
    plan.schema_count = 1U;
    body[cursor] = '{';
    cursor += 1U;
    body[cursor] = '"';
    cursor += 1U;
    for (size_t index = 0U; index < 5000U; index += 1U) {
        body[cursor] = 'a';
        cursor += 1U;
    }
    body[cursor] = '"';
    cursor += 1U;
    body[cursor] = ':';
    cursor += 1U;
    body[cursor] = 't';
    cursor += 1U;
    body[cursor] = 'r';
    cursor += 1U;
    body[cursor] = 'u';
    cursor += 1U;
    body[cursor] = 'e';
    cursor += 1U;
    body[cursor] = '}';
    cursor += 1U;
    request.body = sl_bytes_from_parts((const unsigned char*)body, cursor);

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
        0)
    {
        return 100;
    }

    if (expect_status(
            sl_request_validation_validate(&arena, &plan, &route, &context, &result, &diag),
            SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_ERROR || result.response.status != 400U ||
        diag.code != SL_DIAG_REQUEST_VALIDATION_FAILED ||
        !validation_problem_has_issue_shape(result.response.body) ||
        !bytes_contains(result.response.body, "\"body.<truncated>\"") ||
        !bytes_contains(result.response.body, "\"unknown\"") ||
        bytes_contains(result.response.body, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"))
    {
        return 101;
    }

    return 0;
}

static int test_malformed_json_body_fails_closed(void)
{
    unsigned char arena_storage[8192];
    SlArena arena = {0};
    SlPlanSchemaNode body_schema = {.kind = SL_PLAN_SCHEMA_OBJECT};
    SlPlanSchema schema = {.name = sl_str_from_cstr("CreateUser"), .definition = body_schema};
    SlPlan plan = empty_plan();
    SlPlanRequestBinding binding = {.kind = SL_PLAN_REQUEST_BINDING_BODY_JSON,
                                    .schema = sl_str_from_cstr("CreateUser")};
    SlPlanRoute route = {.bindings = &binding, .binding_count = 1U};
    SlHttpRequestHead request = {0};
    SlHttpRequestContext context = request_context(&request);
    SlEngineResult result = {0};
    SlDiag diag = {0};

    plan.schemas = &schema;
    plan.schema_count = 1U;
    request.body = sl_bytes_from_parts((const unsigned char*)"{bad", sizeof("{bad") - 1U);

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
        0)
    {
        return 50;
    }

    if (expect_status(
            sl_request_validation_validate(&arena, &plan, &route, &context, &result, &diag),
            SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_ERROR || result.response.status != 400U ||
        diag.code != SL_DIAG_REQUEST_VALIDATION_FAILED ||
        !validation_problem_has_issue_shape(result.response.body) ||
        !bytes_contains(result.response.body, "Expected a valid JSON request body."))
    {
        return 51;
    }

    return 0;
}

static int test_native_json_constraints_apply_max_bounds_and_unknown_field_policy(void)
{
    unsigned char arena_storage[16384];
    SlArena arena = {0};
    SlPlanSchemaProperty properties[3];
    SlPlanSchemaNode tag_item = {.kind = SL_PLAN_SCHEMA_STRING};
    SlPlanSchemaNode name_schema = {.kind = SL_PLAN_SCHEMA_STRING, .has_max = true, .max_value = 5};
    SlPlanSchemaNode age_schema = {.kind = SL_PLAN_SCHEMA_INT, .has_max = true, .max_value = 6};
    SlPlanSchemaNode tags_schema = {
        .kind = SL_PLAN_SCHEMA_ARRAY, .items = &tag_item, .has_max = true, .max_value = 2};
    SlPlanSchemaNode body_schema = {
        .kind = SL_PLAN_SCHEMA_OBJECT, .properties = properties, .property_count = 3U};
    SlPlanSchema schema = {.name = sl_str_from_cstr("CreateUser"), .definition = body_schema};
    SlPlan plan = empty_plan();
    SlPlanRequestBinding binding = {.kind = SL_PLAN_REQUEST_BINDING_BODY_JSON,
                                    .schema = sl_str_from_cstr("CreateUser")};
    SlPlanRoute route = {.bindings = &binding,
                         .binding_count = 1U,
                         .json_request = {
                             .mode = SL_PLAN_JSON_REQUEST_NATIVE_SCHEMA,
                             .materialization = SL_PLAN_JSON_MATERIALIZATION_MATERIALIZE_ONCE,
                             .unknown_fields = SL_PLAN_JSON_UNKNOWN_FIELDS_REJECT,
                             .schema = sl_str_from_cstr("CreateUser"),
                         }};
    SlHttpRequestHead request = {0};
    SlHttpRequestContext context = request_context(&request);
    SlEngineResult result = {0};
    SlDiag diag = {0};

    properties[0] =
        (SlPlanSchemaProperty){.name = sl_str_from_cstr("name"), .schema = &name_schema};
    properties[1] = (SlPlanSchemaProperty){.name = sl_str_from_cstr("age"), .schema = &age_schema};
    properties[2] =
        (SlPlanSchemaProperty){.name = sl_str_from_cstr("tags"), .schema = &tags_schema};
    plan.schemas = &schema;
    plan.schema_count = 1U;
    request.body = sl_bytes_from_parts(
        (const unsigned char*)"{\"name\":\"abcdef\",\"age\":7,\"tags\":[\"a\",\"b\",\"c\"],"
                              "\"extra\":true}",
        sizeof("{\"name\":\"abcdef\",\"age\":7,\"tags\":[\"a\",\"b\",\"c\"],\"extra\":true}") - 1U);

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
        0)
    {
        return 60;
    }

    if (expect_status(
            sl_request_validation_validate(&arena, &plan, &route, &context, &result, &diag),
            SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_ERROR || result.response.status != 400U ||
        diag.code != SL_DIAG_REQUEST_VALIDATION_FAILED ||
        !validation_problem_has_issue_shape(result.response.body) ||
        !bytes_contains(result.response.body, "\"string.max\"") ||
        !bytes_contains(result.response.body, "\"number.max\"") ||
        !bytes_contains(result.response.body, "\"array.max\"") ||
        !bytes_contains(result.response.body, "\"unknown\"") ||
        bytes_contains(result.response.body, "abcdef"))
    {
        return 61;
    }

    sl_arena_reset(&arena);
    result = (SlEngineResult){0};
    diag = (SlDiag){0};
    request.body = sl_bytes_from_parts(
        (const unsigned char*)"{\"name\":\"Ada\",\"age\":6,\"tags\":[\"a\",\"b\"]}",
        sizeof("{\"name\":\"Ada\",\"age\":6,\"tags\":[\"a\",\"b\"]}") - 1U);
    if (expect_status(
            sl_request_validation_validate(&arena, &plan, &route, &context, &result, &diag),
            SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_NONE)
    {
        return 62;
    }

    return 0;
}

int main(void)
{
    int (*tests[])(void) = {
        test_valid_native_body_profile_does_not_render_paths,
        test_no_bindings_and_invalid_arguments_are_output_atomic,
        test_body_schema_metadata_failures_publish_problem_details,
        test_header_lookup_is_case_insensitive_and_success_is_silent,
        test_scalar_bindings_coerce_route_query_and_header_values,
        test_issue_cap_truncates_without_failure,
        test_body_schema_validates_required_literal_and_email_contracts,
        test_nested_optional_and_nullable_shapes_validate_precisely,
        test_nested_unknown_and_array_item_paths_are_precise,
        test_large_object_required_field_fallback_handles_more_than_64_properties,
        test_deep_configured_depth_over_path_frame_limit_remains_valid,
        test_long_unknown_field_path_degrades_to_validation_problem,
        test_malformed_json_body_fails_closed,
        test_native_json_constraints_apply_max_bounds_and_unknown_field_policy};
    size_t index = 0U;

    for (index = 0U; index < sizeof(tests) / sizeof(tests[0]); index += 1U) {
        int result = tests[index]();
        if (result != 0) {
            return (int)((index + 1U) * 100U) + result;
        }
    }
    return 0;
}

#include "sloppy/http_dispatch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ARENA_SIZE 65536U
#define TEST_STRINGIFY_IMPL(value) #value
#define TEST_STRINGIFY(value) TEST_STRINGIFY_IMPL(value)
#define DEEP_JSON_TEN_OPEN "[[[[[[[[[["
#define DEEP_JSON_TEN_CLOSE "]]]]]]]]]]"
#define DEEP_JSON_MAX_DEPTH_BODY                                                                   \
    DEEP_JSON_TEN_OPEN DEEP_JSON_TEN_OPEN DEEP_JSON_TEN_OPEN DEEP_JSON_TEN_OPEN DEEP_JSON_TEN_OPEN \
        "["                                                                                        \
        "]" DEEP_JSON_TEN_CLOSE DEEP_JSON_TEN_CLOSE DEEP_JSON_TEN_CLOSE DEEP_JSON_TEN_CLOSE        \
            DEEP_JSON_TEN_CLOSE
#define DEEP_JSON_MAX_DEPTH_BODY_LENGTH 102
#define DEEP_JSON_BODY                                                                             \
    DEEP_JSON_TEN_OPEN DEEP_JSON_TEN_OPEN DEEP_JSON_TEN_OPEN DEEP_JSON_TEN_OPEN DEEP_JSON_TEN_OPEN \
        "["                                                                                        \
        "0"                                                                                        \
        "]" DEEP_JSON_TEN_CLOSE DEEP_JSON_TEN_CLOSE DEEP_JSON_TEN_CLOSE DEEP_JSON_TEN_CLOSE        \
            DEEP_JSON_TEN_CLOSE
#define DEEP_JSON_BODY_LENGTH 103

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static int expect_format_complete(int written, size_t capacity)
{
    if (written < 0) {
        return 1;
    }
    return expect_true((size_t)written < capacity);
}

static int expect_str_equal(SlStr actual, const char* expected)
{
    return expect_true(sl_str_equal(actual, sl_str_from_cstr(expected)));
}

static int expect_result_text_equal(const SlEngineResult* result, const char* expected)
{
    size_t expected_length = strlen(expected);

    if (result == NULL || expected == NULL || result->kind != SL_ENGINE_RESULT_TEXT) {
        return 1;
    }
    if (result->payload_kind == SL_ENGINE_RESULT_PAYLOAD_TEXT) {
        return expect_true(sl_str_equal(result->text, sl_str_from_cstr(expected)));
    }
    if (result->payload_kind == SL_ENGINE_RESULT_PAYLOAD_RESPONSE) {
        return expect_true(result->response.body.length == expected_length &&
                           result->response.body.ptr != NULL &&
                           memcmp(result->response.body.ptr, expected, expected_length) == 0);
    }
    return 1;
}

static int set_test_env_value(const char* key, const char* value)
{
#ifdef _WIN32
    return _putenv_s(key, value == NULL ? "" : value);
#else
    if (value == NULL) {
        return unsetenv(key);
    }
    return setenv(key, value, 1);
#endif
}

#ifndef _WIN32
static char* duplicate_cstr(const char* value)
{
    size_t length = 0U;
    char* copy = NULL;

    if (value == NULL) {
        return NULL;
    }
    length = strlen(value);
    copy = (char*)malloc(length + 1U);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, value, length + 1U);
    return copy;
}
#endif

static int copy_test_env_value(const char* key, char** out_value)
{
    if (key == NULL || out_value == NULL) {
        return 1;
    }
    *out_value = NULL;
#ifdef _WIN32
    {
        size_t length = 0U;
        return _dupenv_s(out_value, &length, key);
    }
#else
    {
        const char* current = getenv(key);
        if (current == NULL) {
            return 0;
        }
        *out_value = duplicate_cstr(current);
        return *out_value == NULL ? 1 : 0;
    }
#endif
}

static int restore_test_env_value(const char* key, const char* original_value)
{
    return set_test_env_value(key, original_value);
}

static int expect_body_contains(SlBytes body, const char* expected)
{
    SlStr needle = sl_str_from_cstr(expected);
    size_t index = 0U;

    if (body.ptr == NULL || needle.ptr == NULL || needle.length == 0U ||
        body.length < needle.length)
    {
        return 1;
    }
    for (index = 0U; index <= body.length - needle.length; index += 1U) {
        if (memcmp(body.ptr + index, needle.ptr, needle.length) == 0) {
            return 0;
        }
    }
    return 1;
}

static SlBytes bytes_from_cstr(const char* text)
{
    SlStr str = sl_str_from_cstr(text);
    return sl_bytes_from_parts((const unsigned char*)str.ptr, str.length);
}

static SlStr body_limit_plus_one_header_value(void)
{
    static char digits[32];
    size_t cursor = sizeof(digits);
    size_t value = SL_HTTP_DEFAULT_MAX_BODY_LENGTH + 1U;

    do {
        cursor -= 1U;
        digits[cursor] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && cursor > 0U);

    return sl_str_from_parts(digits + cursor, sizeof(digits) - cursor);
}

static int init_arena(SlArena* arena, unsigned char* storage, size_t storage_size)
{
    return expect_status(sl_arena_init(arena, storage, storage_size), SL_STATUS_OK);
}

static SlEngineOptions noop_options(void)
{
    SlEngineOptions options = {0};

    options.kind = SL_ENGINE_KIND_NONE;
    options.runtime_name = sl_str_from_cstr("http-dispatch-test");
    options.runtime_version = sl_str_from_cstr("0.3.0-test");
    options.target_platform = sl_str_from_cstr(SL_PLAN_TARGET_PLATFORM_WINDOWS_X64);
    options.target_engine = sl_str_from_cstr("none");
    return options;
}

static SlPlan one_handler_plan(SlPlanHandler* handler)
{
    SlPlan plan = {0};

    handler->id = 1U;
    handler->export_name = sl_str_from_cstr("__sloppy_handler_1");
    handler->display_name = sl_str_from_cstr("Http.Hello");
    plan.version = SL_PLAN_CURRENT_VERSION;
    plan.handlers = handler;
    plan.handler_count = 1U;
    return plan;
}

static SlPlan route_table_plan(SlPlanHandler* handlers, SlPlanRoute* routes)
{
    SlPlan plan = {0};

    memset(handlers, 0, 2U * sizeof(handlers[0]));
    memset(routes, 0, 2U * sizeof(routes[0]));

    handlers[0].id = 1U;
    handlers[0].export_name = sl_str_from_cstr("__sloppy_handler_1");
    handlers[0].display_name = sl_str_from_cstr("Users.Param");
    handlers[1].id = 2U;
    handlers[1].export_name = sl_str_from_cstr("__sloppy_handler_2");
    handlers[1].display_name = sl_str_from_cstr("Users.Me");

    routes[0].method = sl_str_from_cstr("GET");
    routes[0].pattern = sl_str_from_cstr("/users/{id}");
    routes[0].handler_id = 1U;
    routes[0].name = sl_str_from_cstr("Users.Param");
    routes[1].method = sl_str_from_cstr("GET");
    routes[1].pattern = sl_str_from_cstr("/users/me");
    routes[1].handler_id = 2U;
    routes[1].name = sl_str_from_cstr("Users.Me");

    plan.version = SL_PLAN_CURRENT_VERSION;
    plan.handlers = handlers;
    plan.handler_count = 2U;
    plan.routes = routes;
    plan.route_count = 2U;
    return plan;
}

static int parse_request(SlArena* arena, const char* text, SlHttpRequestHead* out)
{
    SlStatus status = sl_http_parse_request_head(arena, bytes_from_cstr(text), NULL, out, NULL);
    return expect_status(status, SL_STATUS_OK);
}

static int parse_pattern(SlArena* arena, const char* text, SlRoutePattern* out)
{
    SlStatus status = sl_route_pattern_parse(arena, sl_str_from_cstr(text), out, NULL);
    return expect_status(status, SL_STATUS_OK);
}

static int create_noop_engine(SlArena* arena, SlEngine** out_engine)
{
    SlEngineOptions options = noop_options();
    return expect_status(sl_engine_create(&options, arena, out_engine), SL_STATUS_OK);
}

static int test_get_missing_route_fails_cleanly(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlHttpRequestHead request = {0};
    SlRoutePattern pattern = {0};
    SlHttpRouteBinding route = {0};
    SlHttpDispatchTable table = {0};
    SlPlanHandler handler = {0};
    SlPlan plan = one_handler_plan(&handler);
    SlEngineResult result = {.kind = SL_ENGINE_RESULT_TEXT, .text = sl_str_from_cstr("stale")};
    SlDiag diag = {0};

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena, "GET /missing HTTP/1.1\r\nHost: example\r\n\r\n", &request) != 0 ||
        parse_pattern(&arena, "/hello", &pattern) != 0)
    {
        sl_engine_destroy(engine);
        return 1;
    }

    route.method = SL_HTTP_METHOD_GET;
    route.pattern = &pattern;
    route.handler_id = 1U;
    table.routes = &route;
    table.route_count = 1U;

    if (expect_status(
            sl_http_dispatch_request_head(&arena, engine, &plan, &table, &request, &result, &diag),
            SL_STATUS_OUT_OF_RANGE) != 0)
    {
        sl_engine_destroy(engine);
        return 2;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_HTTP_ROUTE_NOT_FOUND ||
        expect_str_equal(diag.message, "no matching HTTP route was found") != 0)
    {
        sl_engine_destroy(engine);
        return 3;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_route_table_build_orders_literal_before_params(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlPlanHandler handlers[2];
    SlPlanRoute routes[2];
    SlPlan plan = route_table_plan(handlers, routes);
    SlHttpRouteTable table = {0};
    SlDiag diag = {0};

    if (init_arena(&arena, storage, sizeof(storage)) != 0) {
        return 4;
    }

    if (expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag), SL_STATUS_OK) != 0) {
        return 5;
    }

    if (table.route_count != 2U || table.dispatch.route_count != 2U ||
        table.dispatch.routes == NULL || table.dispatch.routes[0].handler_id != 2U ||
        table.dispatch.routes[0].handler != &handlers[1] ||
        table.dispatch.routes[1].handler_id != 1U ||
        table.dispatch.routes[1].handler != &handlers[0] || !table.dispatch.handler_cache_trusted ||
        table.dispatch.exact_route_buckets == NULL ||
        table.dispatch.exact_route_bucket_count == 0U || table.dispatch.param_routes == NULL ||
        table.dispatch.param_route_count != 1U || table.dispatch.param_routes[0].handler_id != 1U ||
        table.dispatch.param_routes[0].handler != &handlers[0] ||
        table.dispatch.param_route_buckets == NULL || table.dispatch.param_route_bucket_count == 0U)
    {
        return 6;
    }

    return expect_true(diag.code == SL_DIAG_NONE);
}

static int test_literal_route_wins_over_param_route_end_to_end(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlPlanHandler handlers[2];
    SlPlanRoute routes[2];
    SlPlanRequestBinding stale_header = {0};
    SlPlan plan = route_table_plan(handlers, routes);
    SlHttpRouteTable table = {0};
    SlHttpRequestHead request = {0};
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlStatus status;

    routes[0].bindings = &stale_header;
    routes[0].binding_count = 1U;
    stale_header.kind = SL_PLAN_REQUEST_BINDING_HEADER;
    stale_header.name = sl_str_from_cstr("x-param-only");
    stale_header.type = sl_str_from_cstr("string");

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena, "GET /users/me HTTP/1.1\r\nHost: example\r\n\r\n", &request) != 0 ||
        expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag), SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 60;
    }

    status = sl_http_dispatch_request_head(&arena, engine, &plan, &table.dispatch, &request,
                                           &result, &diag);
    sl_engine_destroy(engine);
    if (expect_status(status, SL_STATUS_UNSUPPORTED) != 0 || result.kind != SL_ENGINE_RESULT_NONE ||
        diag.code != SL_DIAG_UNSUPPORTED_ENGINE)
    {
        return 61;
    }
    return 0;
}

static int test_route_table_rejects_duplicate_method_pattern(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlPlanHandler handlers[2];
    SlPlanRoute routes[2];
    SlPlan plan = route_table_plan(handlers, routes);
    SlHttpRouteTable table = {0};
    SlDiag diag = {0};

    if (init_arena(&arena, storage, sizeof(storage)) != 0) {
        return 7;
    }

    routes[1].pattern = sl_str_from_cstr("/users/{id}");
    if (expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        return 8;
    }

    return expect_true(table.route_count == 0U && diag.code == SL_DIAG_DUPLICATE_ROUTE);
}

static int test_route_table_rejects_duplicate_handler_ids(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlPlanHandler handlers[2];
    SlPlanRoute routes[2];
    SlPlan plan = route_table_plan(handlers, routes);
    SlHttpRouteTable table = {0};
    SlDiag diag = {0};

    if (init_arena(&arena, storage, sizeof(storage)) != 0) {
        return 73;
    }

    handlers[1].id = handlers[0].id;
    if (expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        return 74;
    }

    return expect_true(table.route_count == 0U && diag.code == SL_DIAG_DUPLICATE_HANDLER_ID);
}

static int test_route_table_build_keeps_method_metadata(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlPlanHandler handlers[2];
    SlPlanRoute routes[2];
    SlPlan plan = route_table_plan(handlers, routes);
    SlHttpRouteTable table = {0};
    SlDiag diag = {0};

    if (init_arena(&arena, storage, sizeof(storage)) != 0) {
        return 70;
    }

    routes[0].pattern = sl_str_from_cstr("/users");
    routes[1].method = sl_str_from_cstr("POST");
    routes[1].pattern = sl_str_from_cstr("/users");

    if (expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag), SL_STATUS_OK) != 0) {
        return 71;
    }
    if (table.route_count != 2U || table.dispatch.route_count != 2U ||
        table.dispatch.routes == NULL || table.dispatch.routes[0].handler_id != 1U ||
        table.dispatch.routes[0].method != SL_HTTP_METHOD_GET ||
        table.dispatch.routes[1].handler_id != 2U ||
        table.dispatch.routes[1].method != SL_HTTP_METHOD_POST ||
        table.dispatch.exact_route_buckets == NULL ||
        table.dispatch.exact_route_bucket_count == 0U || table.dispatch.param_routes != NULL ||
        table.dispatch.param_route_count != 0U || diag.code != SL_DIAG_NONE)
    {
        return 72;
    }

    return 0;
}

static int test_route_table_exact_index_reports_method_mismatch(void)
{
    unsigned char route_storage[TEST_ARENA_SIZE];
    unsigned char request_storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena route_arena = {0};
    SlArena request_arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlPlanHandler handler = {0};
    SlPlanRoute route = {0};
    SlPlan plan = one_handler_plan(&handler);
    SlHttpRouteTable table = {0};
    SlHttpRequestHead request = {0};
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlStatus status;

    if (init_arena(&route_arena, route_storage, sizeof(route_storage)) != 0 ||
        init_arena(&request_arena, request_storage, sizeof(request_storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&request_arena, "GET /users HTTP/1.1\r\nHost: example\r\n\r\n", &request) !=
            0)
    {
        sl_engine_destroy(engine);
        return 76;
    }

    route.method = sl_str_from_cstr("POST");
    route.pattern = sl_str_from_cstr("/users");
    route.handler_id = 1U;
    route.name = sl_str_from_cstr("Users.Create");
    plan.routes = &route;
    plan.route_count = 1U;

    if (expect_status(sl_http_route_table_build(&route_arena, &plan, &table, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 77;
    }
    if (table.dispatch.exact_route_buckets == NULL ||
        table.dispatch.exact_route_bucket_count == 0U || table.dispatch.param_route_count != 0U)
    {
        sl_engine_destroy(engine);
        return 78;
    }

    status = sl_http_dispatch_request_head(&route_arena, engine, &plan, &table.dispatch, &request,
                                           &result, &diag);
    sl_engine_destroy(engine);
    if (expect_status(status, SL_STATUS_UNSUPPORTED) != 0) {
        return 79;
    }
    return expect_true(diag.code == SL_DIAG_HTTP_METHOD_NOT_ALLOWED);
}

static int test_route_table_param_buckets_preserve_specificity_order(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlPlanHandler handlers[2] = {0};
    SlPlanRoute routes[2] = {0};
    SlPlanRequestBinding binding = {0};
    SlPlan plan = {0};
    SlHttpRouteTable table = {0};
    SlHttpRequestHead request = {0};
    SlEngineResult result = {0};
    SlDiag diag = {0};

    handlers[0].id = 1U;
    handlers[0].export_name = sl_str_from_cstr("__sloppy_handler_1");
    handlers[0].display_name = sl_str_from_cstr("Generic.Settings");
    handlers[1].id = 2U;
    handlers[1].export_name = sl_str_from_cstr("__sloppy_handler_2");
    handlers[1].display_name = sl_str_from_cstr("Profile.Slug");
    routes[0].method = sl_str_from_cstr("GET");
    routes[0].pattern = sl_str_from_cstr("/{section}/settings");
    routes[0].handler_id = 1U;
    routes[0].bindings = &binding;
    routes[0].binding_count = 1U;
    binding.kind = SL_PLAN_REQUEST_BINDING_HEADER;
    binding.name = sl_str_from_cstr("x-required");
    routes[1].method = sl_str_from_cstr("GET");
    routes[1].pattern = sl_str_from_cstr("/profile/{slug}");
    routes[1].handler_id = 2U;
    plan.version = SL_PLAN_CURRENT_VERSION;
    plan.handlers = handlers;
    plan.handler_count = 2U;
    plan.routes = routes;
    plan.route_count = 2U;

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena, "GET /profile/settings HTTP/1.1\r\nHost: example\r\n\r\n",
                      &request) != 0 ||
        expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag), SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 160;
    }

    if (table.dispatch.param_route_buckets == NULL || table.dispatch.param_route_bucket_count < 2U)
    {
        sl_engine_destroy(engine);
        return 161;
    }

    if (expect_status(sl_http_dispatch_request_head(&arena, engine, &plan, &table.dispatch,
                                                    &request, &result, &diag),
                      SL_STATUS_UNSUPPORTED) != 0 ||
        result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_UNSUPPORTED_ENGINE)
    {
        sl_engine_destroy(engine);
        return 162;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_route_table_orders_constrained_params_before_unconstrained(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlPlanHandler handlers[3] = {0};
    SlPlanRoute routes[3] = {0};
    SlPlan plan = {0};
    SlHttpRouteTable table = {0};
    SlDiag diag = {0};

    handlers[0].id = 1U;
    handlers[0].export_name = sl_str_from_cstr("__sloppy_handler_1");
    handlers[1].id = 2U;
    handlers[1].export_name = sl_str_from_cstr("__sloppy_handler_2");
    handlers[2].id = 3U;
    handlers[2].export_name = sl_str_from_cstr("__sloppy_handler_3");
    routes[0].method = sl_str_from_cstr("GET");
    routes[0].pattern = sl_str_from_cstr("/users/{slug}");
    routes[0].handler_id = 1U;
    routes[1].method = sl_str_from_cstr("GET");
    routes[1].pattern = sl_str_from_cstr("/users/{id:int}");
    routes[1].handler_id = 2U;
    routes[2].method = sl_str_from_cstr("GET");
    routes[2].pattern = sl_str_from_cstr("/users/{name:alpha}/profile");
    routes[2].handler_id = 3U;
    plan.version = SL_PLAN_CURRENT_VERSION;
    plan.handlers = handlers;
    plan.handler_count = 3U;
    plan.routes = routes;
    plan.route_count = 3U;

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag), SL_STATUS_OK) != 0)
    {
        return 163;
    }

    if (table.dispatch.route_count != 3U || table.dispatch.routes == NULL ||
        table.dispatch.routes[0].handler_id != 3U || table.dispatch.routes[1].handler_id != 2U ||
        table.dispatch.routes[2].handler_id != 1U)
    {
        return 164;
    }

    return expect_true(diag.code == SL_DIAG_NONE);
}

static int test_route_table_orders_mixed_position_literal_specificity(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlPlanHandler handlers[2] = {0};
    SlPlanRoute routes[2] = {0};
    SlPlan plan = {0};
    SlHttpRouteTable table = {0};
    SlDiag diag = {0};

    handlers[0].id = 1U;
    handlers[0].export_name = sl_str_from_cstr("__sloppy_handler_1");
    handlers[1].id = 2U;
    handlers[1].export_name = sl_str_from_cstr("__sloppy_handler_2");
    routes[0].method = sl_str_from_cstr("GET");
    routes[0].pattern = sl_str_from_cstr("/{kind}/profile");
    routes[0].handler_id = 1U;
    routes[1].method = sl_str_from_cstr("GET");
    routes[1].pattern = sl_str_from_cstr("/users/{name}");
    routes[1].handler_id = 2U;
    plan.version = SL_PLAN_CURRENT_VERSION;
    plan.handlers = handlers;
    plan.handler_count = 2U;
    plan.routes = routes;
    plan.route_count = 2U;

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag), SL_STATUS_OK) != 0)
    {
        return 165;
    }

    if (table.dispatch.route_count != 2U || table.dispatch.routes == NULL ||
        table.dispatch.routes[0].handler_id != 2U || table.dispatch.routes[1].handler_id != 1U ||
        table.dispatch.param_route_count != 2U || table.dispatch.param_routes == NULL ||
        table.dispatch.param_routes[0].handler_id != 2U ||
        table.dispatch.param_routes[1].handler_id != 1U)
    {
        return 166;
    }

    return expect_true(diag.code == SL_DIAG_NONE);
}

static int test_route_table_param_trie_matches_shared_prefix_and_constraints(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlPlanHandler handlers[2] = {0};
    SlPlanRoute routes[2] = {0};
    SlPlan plan = {0};
    SlHttpRouteTable table = {0};
    SlHttpRequestHead valid_request = {0};
    SlHttpRequestHead invalid_request = {0};
    SlEngineResult result = {0};
    SlDiag diag = {0};

    handlers[0].id = 1U;
    handlers[0].export_name = sl_str_from_cstr("__sloppy_handler_1");
    handlers[0].display_name = sl_str_from_cstr("Org.Users.Show");
    handlers[1].id = 2U;
    handlers[1].export_name = sl_str_from_cstr("__sloppy_handler_2");
    handlers[1].display_name = sl_str_from_cstr("Org.Settings");
    routes[0].method = sl_str_from_cstr("GET");
    routes[0].pattern = sl_str_from_cstr("/orgs/{org}/users/{id:int}");
    routes[0].handler_id = 1U;
    routes[1].method = sl_str_from_cstr("GET");
    routes[1].pattern = sl_str_from_cstr("/orgs/{org}/settings");
    routes[1].handler_id = 2U;
    plan.version = SL_PLAN_CURRENT_VERSION;
    plan.handlers = handlers;
    plan.handler_count = 2U;
    plan.routes = routes;
    plan.route_count = 2U;

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena, "GET /orgs/acme/users/42 HTTP/1.1\r\nHost: example\r\n\r\n",
                      &valid_request) != 0 ||
        parse_request(&arena, "GET /orgs/acme/users/nope HTTP/1.1\r\nHost: example\r\n\r\n",
                      &invalid_request) != 0 ||
        expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag), SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 167;
    }

    if (table.dispatch.param_route_trie_roots == NULL ||
        table.dispatch.param_route_trie_root_count != 1U ||
        table.dispatch.param_route_trie_nodes == NULL ||
        table.dispatch.param_route_trie_node_count == 0U ||
        table.dispatch.param_route_trie_edges == NULL ||
        table.dispatch.param_route_trie_edge_count == 0U)
    {
        sl_engine_destroy(engine);
        return 168;
    }

    if (expect_status(sl_http_dispatch_request_head(&arena, engine, &plan, &table.dispatch,
                                                    &valid_request, &result, &diag),
                      SL_STATUS_UNSUPPORTED) != 0 ||
        result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_UNSUPPORTED_ENGINE)
    {
        sl_engine_destroy(engine);
        return 169;
    }

    result = (SlEngineResult){0};
    diag = (SlDiag){0};
    if (expect_status(sl_http_dispatch_request_head(&arena, engine, &plan, &table.dispatch,
                                                    &invalid_request, &result, &diag),
                      SL_STATUS_OUT_OF_RANGE) != 0 ||
        result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_HTTP_ROUTE_NOT_FOUND)
    {
        sl_engine_destroy(engine);
        return 170;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_route_dispatch_env_classic_and_validate_modes(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char route_storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena route_arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlPlanHandler handler = {0};
    SlPlanRoute route = {0};
    SlPlan plan = one_handler_plan(&handler);
    SlHttpRequestHead request = {0};
    SlDiag diag = {0};
    const struct
    {
        const char* text;
        SlHttpRouteDispatchMode mode;
    } modes[] = {
        {"compiled", SL_HTTP_ROUTE_DISPATCH_MODE_COMPILED},
        {"classic", SL_HTTP_ROUTE_DISPATCH_MODE_CLASSIC},
        {"validate", SL_HTTP_ROUTE_DISPATCH_MODE_VALIDATE},
    };
    char* saved_env = NULL;
    size_t mode_index = 0U;
    int result_code = 0;

    if (copy_test_env_value("SLOPPY_ROUTE_DISPATCH", &saved_env) != 0) {
        return 171;
    }

    route.method = sl_str_from_cstr("GET");
    route.pattern = sl_str_from_cstr("/users/{id:int}");
    route.handler_id = 1U;
    plan.routes = &route;
    plan.route_count = 1U;

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena, "GET /users/42 HTTP/1.1\r\nHost: example\r\n\r\n", &request) != 0)
    {
        sl_engine_destroy(engine);
        restore_test_env_value("SLOPPY_ROUTE_DISPATCH", saved_env);
        free(saved_env);
        return 172;
    }

    for (mode_index = 0U; mode_index < sizeof(modes) / sizeof(modes[0]); mode_index += 1U) {
        SlHttpRouteTable table = {0};
        SlEngineResult result = {0};
        route_arena = (SlArena){0};
        diag = (SlDiag){0};
        if (set_test_env_value("SLOPPY_ROUTE_DISPATCH", modes[mode_index].text) != 0) {
            result_code = 173;
            break;
        }
        if (init_arena(&route_arena, route_storage, sizeof(route_storage)) != 0 ||
            expect_status(sl_http_route_table_build(&route_arena, &plan, &table, &diag),
                          SL_STATUS_OK) != 0)
        {
            result_code = 174;
            break;
        }
        if (table.dispatch.dispatch_mode != modes[mode_index].mode) {
            result_code = 175;
            break;
        }
        if (set_test_env_value("SLOPPY_ROUTE_DISPATCH", "compiled") != 0 ||
            table.dispatch.dispatch_mode != modes[mode_index].mode)
        {
            result_code = 176;
            break;
        }
        if (expect_status(sl_http_dispatch_request_head(&arena, engine, &plan, &table.dispatch,
                                                        &request, &result, &diag),
                          SL_STATUS_UNSUPPORTED) != 0 ||
            result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_UNSUPPORTED_ENGINE)
        {
            result_code = 177;
            break;
        }
    }

    if (restore_test_env_value("SLOPPY_ROUTE_DISPATCH", saved_env) != 0 && result_code == 0) {
        result_code = 178;
    }
    free(saved_env);
    sl_engine_destroy(engine);
    return result_code;
}

static int test_route_table_param_buckets_report_method_mismatch(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlPlanHandler handler = {0};
    SlPlanRoute route = {0};
    SlPlan plan = one_handler_plan(&handler);
    SlHttpRouteTable table = {0};
    SlHttpRequestHead request = {0};
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlStatus status;

    route.method = sl_str_from_cstr("POST");
    route.pattern = sl_str_from_cstr("/api/{id}");
    route.handler_id = 1U;
    plan.routes = &route;
    plan.route_count = 1U;

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena, "GET /api/1 HTTP/1.1\r\nHost: example\r\n\r\n", &request) != 0 ||
        expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag), SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 163;
    }

    status = sl_http_dispatch_request_head(&arena, engine, &plan, &table.dispatch, &request,
                                           &result, &diag);
    sl_engine_destroy(engine);
    if (expect_status(status, SL_STATUS_UNSUPPORTED) != 0 || result.kind != SL_ENGINE_RESULT_NONE ||
        diag.code != SL_DIAG_HTTP_METHOD_NOT_ALLOWED)
    {
        return 164;
    }
    return 0;
}

static int test_parameter_first_route_reaches_bucket(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlPlanHandler handler = {0};
    SlPlanRoute route = {0};
    SlPlan plan = one_handler_plan(&handler);
    SlHttpRouteTable table = {0};
    SlHttpRequestHead request = {0};
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlStatus status;

    route.method = sl_str_from_cstr("GET");
    route.pattern = sl_str_from_cstr("/{tenant}/users");
    route.handler_id = 1U;
    plan.routes = &route;
    plan.route_count = 1U;

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena, "GET /acme/users HTTP/1.1\r\nHost: example\r\n\r\n", &request) != 0 ||
        expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag), SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 165;
    }

    status = sl_http_dispatch_request_head(&arena, engine, &plan, &table.dispatch, &request,
                                           &result, &diag);
    sl_engine_destroy(engine);
    if (expect_status(status, SL_STATUS_UNSUPPORTED) != 0 || result.kind != SL_ENGINE_RESULT_NONE ||
        diag.code != SL_DIAG_UNSUPPORTED_ENGINE)
    {
        return 166;
    }
    return 0;
}

static int test_large_param_bucket_miss_uses_indexed_lookup(void)
{
    unsigned char storage[TEST_ARENA_SIZE * 4U];
    unsigned char request_storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena request_arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlPlanHandler handler = {0};
    SlPlanRoute routes[96] = {0};
    char patterns[96][32];
    SlPlan plan = one_handler_plan(&handler);
    SlHttpRouteTable table = {0};
    SlHttpRequestHead request = {0};
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlStatus status;
    size_t index = 0U;

    for (index = 0U; index < 96U; index += 1U) {
        int written =
            snprintf(patterns[index], sizeof(patterns[index]), "/bucket%02zu/{id}", index);
        if (expect_format_complete(written, sizeof(patterns[index])) != 0) {
            return 176;
        }
        routes[index].method = sl_str_from_cstr("GET");
        routes[index].pattern = sl_str_from_cstr(patterns[index]);
        routes[index].handler_id = 1U;
    }
    plan.routes = routes;
    plan.route_count = 96U;

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&request_arena, request_storage, sizeof(request_storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&request_arena, "GET /missing/123 HTTP/1.1\r\nHost: example\r\n\r\n",
                      &request) != 0 ||
        expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag), SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 177;
    }

    if (table.dispatch.param_route_bucket_count == 0U ||
        table.dispatch.param_route_bucket_slots == NULL ||
        table.dispatch.param_route_bucket_slot_count == 0U)
    {
        sl_engine_destroy(engine);
        return 178;
    }

    status = sl_http_dispatch_request_head(&arena, engine, &plan, &table.dispatch, &request,
                                           &result, &diag);
    sl_engine_destroy(engine);
    if (expect_status(status, SL_STATUS_OUT_OF_RANGE) != 0 ||
        result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_HTTP_ROUTE_NOT_FOUND)
    {
        return 179;
    }
    return 0;
}

static int test_generated_options_route_reaches_engine(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlPlanHandler handler = {0};
    SlPlanRoute route = {0};
    SlPlan plan = one_handler_plan(&handler);
    SlHttpRouteTable table = {0};
    SlHttpRequestHead request = {0};
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlStatus status;

    route.method = sl_str_from_cstr("OPTIONS");
    route.pattern = sl_str_from_cstr("/cors/{id}");
    route.handler_id = 1U;
    plan.routes = &route;
    plan.route_count = 1U;

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena, "OPTIONS /cors/1 HTTP/1.1\r\nHost: example\r\n\r\n", &request) != 0 ||
        expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag), SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 167;
    }

    status = sl_http_dispatch_request_head(&arena, engine, &plan, &table.dispatch, &request,
                                           &result, &diag);
    sl_engine_destroy(engine);
    if (expect_status(status, SL_STATUS_UNSUPPORTED) != 0 || result.kind != SL_ENGINE_RESULT_NONE ||
        diag.code != SL_DIAG_UNSUPPORTED_ENGINE)
    {
        return 168;
    }
    return 0;
}

static int test_route_table_build_accepts_non_get_only_metadata(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlPlanHandler handler = {0};
    SlPlanRoute route = {0};
    SlPlan plan = one_handler_plan(&handler);
    SlHttpRouteTable table = {0};
    SlDiag diag = {0};

    if (init_arena(&arena, storage, sizeof(storage)) != 0) {
        return 73;
    }

    route.method = sl_str_from_cstr("POST");
    route.pattern = sl_str_from_cstr("/users");
    route.handler_id = 1U;
    route.name = sl_str_from_cstr("Users.Create");
    plan.routes = &route;
    plan.route_count = 1U;

    if (expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag), SL_STATUS_OK) != 0) {
        return 74;
    }
    if (table.route_count != 1U || table.dispatch.route_count != 1U ||
        table.dispatch.routes == NULL || table.dispatch.routes[0].method != SL_HTTP_METHOD_POST ||
        table.dispatch.routes[0].handler_id != 1U || diag.code != SL_DIAG_NONE)
    {
        return 75;
    }

    return 0;
}

static int test_allow_header_lists_matching_methods_and_head_for_get(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlPlanHandler handlers[4];
    SlPlanRoute routes[4];
    SlPlan plan = {0};
    SlHttpRouteTable table = {0};
    SlStr allow = sl_str_from_cstr("stale");
    SlDiag diag = {0};

    handlers[0].id = 1U;
    handlers[0].export_name = sl_str_from_cstr("__sloppy_handler_1");
    handlers[0].display_name = sl_str_from_cstr("Users.Get");
    handlers[1].id = 2U;
    handlers[1].export_name = sl_str_from_cstr("__sloppy_handler_2");
    handlers[1].display_name = sl_str_from_cstr("Users.Update");
    handlers[2].id = 3U;
    handlers[2].export_name = sl_str_from_cstr("__sloppy_handler_3");
    handlers[2].display_name = sl_str_from_cstr("Health.Get");
    handlers[3].id = 4U;
    handlers[3].export_name = sl_str_from_cstr("__sloppy_handler_4");
    handlers[3].display_name = sl_str_from_cstr("Users.Preflight");

    routes[0].method = sl_str_from_cstr("GET");
    routes[0].pattern = sl_str_from_cstr("/users/{id}");
    routes[0].handler_id = 1U;
    routes[1].method = sl_str_from_cstr("PATCH");
    routes[1].pattern = sl_str_from_cstr("/users/{id}");
    routes[1].handler_id = 2U;
    routes[2].method = sl_str_from_cstr("GET");
    routes[2].pattern = sl_str_from_cstr("/health");
    routes[2].handler_id = 3U;
    routes[3].method = sl_str_from_cstr("OPTIONS");
    routes[3].pattern = sl_str_from_cstr("/users/{id}");
    routes[3].handler_id = 4U;

    plan.version = SL_PLAN_CURRENT_VERSION;
    plan.handlers = handlers;
    plan.handler_count = 4U;
    plan.routes = routes;
    plan.route_count = 4U;

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag), SL_STATUS_OK) != 0)
    {
        return 76;
    }

    if (expect_status(sl_http_dispatch_allow_header_for_path(
                          &arena, &table.dispatch, sl_str_from_cstr("/users/123"), &allow),
                      SL_STATUS_OK) != 0 ||
        expect_str_equal(allow, "GET, HEAD, PATCH, OPTIONS") != 0)
    {
        return 77;
    }

    if (expect_status(sl_http_dispatch_allow_header_for_path(&arena, &table.dispatch,
                                                             sl_str_from_cstr("/missing"), &allow),
                      SL_STATUS_OK) != 0 ||
        !sl_str_is_empty(allow))
    {
        return 78;
    }

    return 0;
}

static int test_method_mismatch_returns_method_not_allowed(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlHttpRequestHead request = {0};
    SlRoutePattern pattern = {0};
    SlHttpRouteBinding route = {0};
    SlHttpDispatchTable table = {0};
    SlPlanHandler handler = {0};
    SlPlan plan = one_handler_plan(&handler);
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena, "POST /hello HTTP/1.1\r\nHost: example\r\n\r\n", &request) != 0 ||
        parse_pattern(&arena, "/hello", &pattern) != 0)
    {
        sl_engine_destroy(engine);
        return 10;
    }

    route.method = SL_HTTP_METHOD_GET;
    route.pattern = &pattern;
    route.handler_id = 1U;
    table.routes = &route;
    table.route_count = 1U;

    if (expect_status(
            sl_http_dispatch_request_head(&arena, engine, &plan, &table, &request, &result, &diag),
            SL_STATUS_UNSUPPORTED) != 0)
    {
        sl_engine_destroy(engine);
        return 11;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_HTTP_METHOD_NOT_ALLOWED ||
        expect_str_equal(diag.message, "HTTP method is not allowed for this route") != 0)
    {
        sl_engine_destroy(engine);
        return 12;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_head_request_matches_get_route_before_engine(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlHttpRequestHead request = {0};
    SlRoutePattern pattern = {0};
    SlHttpRouteBinding route = {0};
    SlHttpDispatchTable table = {0};
    SlPlanHandler handler = {0};
    SlPlan plan = one_handler_plan(&handler);
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena, "HEAD /hello HTTP/1.1\r\nHost: example\r\n\r\n", &request) != 0 ||
        parse_pattern(&arena, "/hello", &pattern) != 0)
    {
        sl_engine_destroy(engine);
        return 1;
    }

    route.method = SL_HTTP_METHOD_GET;
    route.pattern = &pattern;
    route.handler_id = 1U;
    table.routes = &route;
    table.route_count = 1U;

    if (expect_status(
            sl_http_dispatch_request_head(&arena, engine, &plan, &table, &request, &result, &diag),
            SL_STATUS_UNSUPPORTED) != 0)
    {
        sl_engine_destroy(engine);
        return 2;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_UNSUPPORTED_ENGINE) {
        sl_engine_destroy(engine);
        return 3;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_head_route_binding_is_rejected(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlHttpRequestHead request = {0};
    SlRoutePattern pattern = {0};
    SlHttpRouteBinding route = {0};
    SlHttpDispatchTable table = {0};
    SlPlanHandler handler = {0};
    SlPlan plan = one_handler_plan(&handler);
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena, "HEAD /hello HTTP/1.1\r\nHost: example\r\n\r\n", &request) != 0 ||
        parse_pattern(&arena, "/hello", &pattern) != 0)
    {
        sl_engine_destroy(engine);
        return 1;
    }

    route.method = SL_HTTP_METHOD_HEAD;
    route.pattern = &pattern;
    route.handler_id = 1U;
    table.routes = &route;
    table.route_count = 1U;

    if (expect_status(
            sl_http_dispatch_request_head(&arena, engine, &plan, &table, &request, &result, &diag),
            SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        sl_engine_destroy(engine);
        return 2;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE) {
        sl_engine_destroy(engine);
        return 3;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_supported_non_get_methods_reach_engine(void)
{
    typedef struct MethodCase
    {
        SlHttpMethod method;
        const char* request;
    } MethodCase;

    static const MethodCase cases[] = {
        {SL_HTTP_METHOD_POST, "POST /hello HTTP/1.1\r\nHost: example\r\n\r\n"},
        {SL_HTTP_METHOD_PUT, "PUT /hello HTTP/1.1\r\nHost: example\r\n\r\n"},
        {SL_HTTP_METHOD_PATCH, "PATCH /hello HTTP/1.1\r\nHost: example\r\n\r\n"},
        {SL_HTTP_METHOD_DELETE, "DELETE /hello HTTP/1.1\r\nHost: example\r\n\r\n"}};
    size_t index = 0U;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index += 1U) {
        unsigned char storage[TEST_ARENA_SIZE];
        unsigned char engine_storage[1024];
        SlArena arena = {0};
        SlArena engine_arena = {0};
        SlEngine* engine = NULL;
        SlHttpRequestHead request = {0};
        SlRoutePattern pattern = {0};
        SlHttpRouteBinding route = {0};
        SlHttpDispatchTable table = {0};
        SlPlanHandler handler = {0};
        SlPlan plan = one_handler_plan(&handler);
        SlEngineResult result = {0};
        SlDiag diag = {0};

        if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
            init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
            create_noop_engine(&engine_arena, &engine) != 0 ||
            parse_request(&arena, cases[index].request, &request) != 0 ||
            parse_pattern(&arena, "/hello", &pattern) != 0)
        {
            sl_engine_destroy(engine);
            return 80 + (int)index;
        }

        route.method = cases[index].method;
        route.pattern = &pattern;
        route.handler_id = 1U;
        table.routes = &route;
        table.route_count = 1U;

        if (expect_status(sl_http_dispatch_request_head(&arena, engine, &plan, &table, &request,
                                                        &result, &diag),
                          SL_STATUS_UNSUPPORTED) != 0 ||
            diag.code != SL_DIAG_UNSUPPORTED_ENGINE)
        {
            sl_engine_destroy(engine);
            return 84 + (int)index;
        }

        sl_engine_destroy(engine);
    }

    return 0;
}

static int test_transfer_encoding_body_is_rejected_before_handler_call(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlHttpRequestHead request = {0};
    SlHttpHeader headers[2];
    SlRoutePattern pattern = {0};
    SlHttpRouteBinding route = {0};
    SlHttpDispatchTable table = {0};
    SlPlanHandler handler = {0};
    SlPlan plan = one_handler_plan(&handler);
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_pattern(&arena, "/hello", &pattern) != 0)
    {
        sl_engine_destroy(engine);
        return 13;
    }

    headers[0].name = sl_str_from_cstr("Transfer-Encoding");
    headers[0].value = sl_str_from_cstr("gzip");
    request.method = SL_HTTP_METHOD_GET;
    request.path = sl_str_from_cstr("/hello");
    request.headers = headers;
    request.header_count = 1U;

    route.method = SL_HTTP_METHOD_GET;
    route.pattern = &pattern;
    route.handler_id = 1U;
    table.routes = &route;
    table.route_count = 1U;

    if (expect_status(
            sl_http_dispatch_request_head(&arena, engine, &plan, &table, &request, &result, &diag),
            SL_STATUS_UNSUPPORTED) != 0)
    {
        sl_engine_destroy(engine);
        return 14;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_HTTP_UNSUPPORTED_BODY) {
        sl_engine_destroy(engine);
        return 15;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_json_body_reaches_engine_when_valid(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlHttpRequestHead request = {0};
    SlRoutePattern pattern = {0};
    SlHttpRouteBinding route = {0};
    SlHttpDispatchTable table = {0};
    SlPlanHandler handler = {0};
    SlPlan plan = one_handler_plan(&handler);
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena,
                      "POST /hello HTTP/1.1\r\nHost: example\r\nContent-Type: "
                      "application/json; charset=utf-8\r\nContent-Length: 11\r\n\r\n"
                      "{\"ok\":true}",
                      &request) != 0 ||
        parse_pattern(&arena, "/hello", &pattern) != 0)
    {
        sl_engine_destroy(engine);
        return 90;
    }

    route.method = SL_HTTP_METHOD_POST;
    route.pattern = &pattern;
    route.handler_id = 1U;
    table.routes = &route;
    table.route_count = 1U;

    if (expect_status(
            sl_http_dispatch_request_head(&arena, engine, &plan, &table, &request, &result, &diag),
            SL_STATUS_UNSUPPORTED) != 0 ||
        diag.code != SL_DIAG_UNSUPPORTED_ENGINE)
    {
        sl_engine_destroy(engine);
        return 91;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_invalid_json_body_fails_before_handler_call(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlHttpRequestHead request = {0};
    SlRoutePattern pattern = {0};
    SlHttpRouteBinding route = {0};
    SlHttpDispatchTable table = {0};
    SlPlanHandler handler = {0};
    SlPlan plan = one_handler_plan(&handler);
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena,
                      "POST /hello HTTP/1.1\r\nHost: example\r\n"
                      "Content-Type: application/json\r\nContent-Length: 6\r\n\r\n{\"ok\":",
                      &request) != 0 ||
        parse_pattern(&arena, "/hello", &pattern) != 0)
    {
        sl_engine_destroy(engine);
        return 92;
    }

    route.method = SL_HTTP_METHOD_POST;
    route.pattern = &pattern;
    route.handler_id = 1U;
    table.routes = &route;
    table.route_count = 1U;

    if (expect_status(
            sl_http_dispatch_request_head(&arena, engine, &plan, &table, &request, &result, &diag),
            SL_STATUS_INVALID_ARGUMENT) != 0 ||
        result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_MALFORMED_JSON)
    {
        sl_engine_destroy(engine);
        return 93;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_unsupported_body_content_type_fails_before_handler_call(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlHttpRequestHead request = {0};
    SlHttpHeader headers[2];
    SlRoutePattern pattern = {0};
    SlHttpRouteBinding route = {0};
    SlHttpDispatchTable table = {0};
    SlPlanHandler handler = {0};
    SlPlan plan = one_handler_plan(&handler);
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_pattern(&arena, "/hello", &pattern) != 0)
    {
        sl_engine_destroy(engine);
        return 94;
    }

    headers[0].name = sl_str_from_cstr("Content-Type");
    headers[0].value = sl_str_from_cstr("application/x-unsupported");
    headers[1].name = sl_str_from_cstr("Content-Length");
    headers[1].value = sl_str_from_cstr("3");
    request.method = SL_HTTP_METHOD_POST;
    request.path = sl_str_from_cstr("/hello");
    request.raw_target = sl_str_from_cstr("/hello");
    request.headers = headers;
    request.header_count = 2U;
    request.body = bytes_from_cstr("abc");

    route.method = SL_HTTP_METHOD_POST;
    route.pattern = &pattern;
    route.handler_id = 1U;
    table.routes = &route;
    table.route_count = 1U;

    if (expect_status(
            sl_http_dispatch_request_head(&arena, engine, &plan, &table, &request, &result, &diag),
            SL_STATUS_UNSUPPORTED) != 0 ||
        result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_HTTP_UNSUPPORTED_MEDIA_TYPE)
    {
        sl_engine_destroy(engine);
        return 95;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_non_empty_body_without_content_length_fails_before_handler_call(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlHttpRequestHead request = {0};
    SlHttpHeader headers[1];
    SlRoutePattern pattern = {0};
    SlHttpRouteBinding route = {0};
    SlHttpDispatchTable table = {0};
    SlPlanHandler handler = {0};
    SlPlan plan = one_handler_plan(&handler);
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_pattern(&arena, "/hello", &pattern) != 0)
    {
        sl_engine_destroy(engine);
        return 101;
    }

    headers[0].name = sl_str_from_cstr("Content-Type");
    headers[0].value = sl_str_from_cstr("text/plain");
    request.method = SL_HTTP_METHOD_POST;
    request.path = sl_str_from_cstr("/hello");
    request.raw_target = sl_str_from_cstr("/hello");
    request.headers = headers;
    request.header_count = 1U;
    request.body = bytes_from_cstr("abc");

    route.method = SL_HTTP_METHOD_POST;
    route.pattern = &pattern;
    route.handler_id = 1U;
    table.routes = &route;
    table.route_count = 1U;

    if (expect_status(
            sl_http_dispatch_request_head(&arena, engine, &plan, &table, &request, &result, &diag),
            SL_STATUS_UNSUPPORTED) != 0 ||
        result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_HTTP_UNSUPPORTED_BODY)
    {
        sl_engine_destroy(engine);
        return 102;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_body_too_large_fails_before_handler_call(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    static unsigned char body[SL_HTTP_DEFAULT_MAX_BODY_LENGTH + 1U];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlHttpRequestHead request = {0};
    SlHttpHeader headers[2];
    SlRoutePattern pattern = {0};
    SlHttpRouteBinding route = {0};
    SlHttpDispatchTable table = {0};
    SlPlanHandler handler = {0};
    SlPlan plan = one_handler_plan(&handler);
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_pattern(&arena, "/hello", &pattern) != 0)
    {
        sl_engine_destroy(engine);
        return 96;
    }

    headers[0].name = sl_str_from_cstr("Content-Type");
    headers[0].value = sl_str_from_cstr("text/plain");
    headers[1].name = sl_str_from_cstr("Content-Length");
    headers[1].value = body_limit_plus_one_header_value();
    request.method = SL_HTTP_METHOD_POST;
    request.path = sl_str_from_cstr("/hello");
    request.raw_target = sl_str_from_cstr("/hello");
    request.headers = headers;
    request.header_count = 2U;
    body[0] = 'x';
    body[SL_HTTP_DEFAULT_MAX_BODY_LENGTH] = 'x';
    request.body = sl_bytes_from_parts(body, sizeof(body));

    route.method = SL_HTTP_METHOD_POST;
    route.pattern = &pattern;
    route.handler_id = 1U;
    table.routes = &route;
    table.route_count = 1U;

    if (expect_status(
            sl_http_dispatch_request_head(&arena, engine, &plan, &table, &request, &result, &diag),
            SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_HTTP_BODY_LIMIT)
    {
        sl_engine_destroy(engine);
        return 97;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_lifecycle_dispatch_uses_backend_body_limit(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    static unsigned char body[SL_HTTP_DEFAULT_MAX_BODY_LENGTH + 1U];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlHttpBackendOptions options = {0};
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};
    SlHttpRequestLifecycle request = {0};
    SlHttpHeader headers[2];
    SlRoutePattern pattern = {0};
    SlHttpRouteBinding route = {0};
    SlHttpDispatchTable table = {0};
    SlPlanHandler handler = {0};
    SlPlan plan = one_handler_plan(&handler);
    SlEngineResult result = {0};
    SlDiag diag = {0};

    options.parse.max_body_length = SL_HTTP_DEFAULT_MAX_BODY_LENGTH + 8U;
    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        expect_status(sl_http_backend_init(&backend, &options, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_backend_start(&backend, NULL, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_backend_accept_connection(&backend, &connection, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_begin(&connection, &arena, &request, NULL), SL_STATUS_OK) !=
            0 ||
        parse_pattern(&arena, "/hello", &pattern) != 0)
    {
        sl_engine_destroy(engine);
        return 103;
    }

    headers[0].name = sl_str_from_cstr("Content-Type");
    headers[0].value = sl_str_from_cstr("text/plain");
    headers[1].name = sl_str_from_cstr("Content-Length");
    headers[1].value = body_limit_plus_one_header_value();
    request.head.method = SL_HTTP_METHOD_POST;
    request.head.path = sl_str_from_cstr("/hello");
    request.head.raw_target = sl_str_from_cstr("/hello");
    request.head.headers = headers;
    request.head.header_count = 2U;
    for (size_t body_index = 0U; body_index < sizeof(body); body_index += 1U) {
        body[body_index] = 'x';
    }
    request.head.body = sl_bytes_from_parts(body, sizeof(body));

    route.method = SL_HTTP_METHOD_POST;
    route.pattern = &pattern;
    route.handler_id = 1U;
    table.routes = &route;
    table.route_count = 1U;

    if (expect_status(sl_http_dispatch_request_lifecycle(&arena, engine, &plan, &table, &request,
                                                         &result, &diag),
                      SL_STATUS_UNSUPPORTED) != 0 ||
        result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_UNSUPPORTED_ENGINE)
    {
        sl_engine_destroy(engine);
        return 104;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_missing_plan_handler_fails_before_engine_call(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlHttpRequestHead request = {0};
    SlRoutePattern pattern = {0};
    SlHttpRouteBinding route = {0};
    SlHttpDispatchTable table = {0};
    SlPlanHandler handler = {0};
    SlPlan plan = one_handler_plan(&handler);
    SlEngineResult result = {.kind = SL_ENGINE_RESULT_TEXT, .text = sl_str_from_cstr("stale")};
    SlDiag diag = {0};

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena, "GET /hello HTTP/1.1\r\nHost: example\r\n\r\n", &request) != 0 ||
        parse_pattern(&arena, "/hello", &pattern) != 0)
    {
        sl_engine_destroy(engine);
        return 20;
    }

    route.method = SL_HTTP_METHOD_GET;
    route.pattern = &pattern;
    route.handler_id = 2U;
    table.routes = &route;
    table.route_count = 1U;

    if (expect_status(
            sl_http_dispatch_request_head(&arena, engine, &plan, &table, &request, &result, &diag),
            SL_STATUS_OUT_OF_RANGE) != 0)
    {
        sl_engine_destroy(engine);
        return 21;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_ENGINE_CALL_ERROR ||
        expect_str_equal(diag.message, "matched route handler ID was not found in app plan") != 0)
    {
        sl_engine_destroy(engine);
        return 22;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_stale_plan_route_table_ignores_cached_handler(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlPlanHandler source_handler = {0};
    SlPlanRoute source_route = {0};
    SlPlan source_plan = {0};
    SlPlan current_plan = {0};
    SlHttpRouteTable table = {0};
    SlHttpRequestHead request = {0};
    SlEngineResult result = {.kind = SL_ENGINE_RESULT_TEXT, .text = sl_str_from_cstr("stale")};
    SlDiag diag = {0};

    source_handler.id = 1U;
    source_handler.export_name = sl_str_from_cstr("__sloppy_handler_1");
    source_handler.display_name = sl_str_from_cstr("Http.Stale");
    source_route.method = sl_str_from_cstr("GET");
    source_route.pattern = sl_str_from_cstr("/stale");
    source_route.handler_id = 1U;
    source_route.name = sl_str_from_cstr("Http.Stale");
    source_plan.version = SL_PLAN_CURRENT_VERSION;
    source_plan.handlers = &source_handler;
    source_plan.handler_count = 1U;
    source_plan.routes = &source_route;
    source_plan.route_count = 1U;
    current_plan.version = SL_PLAN_CURRENT_VERSION;

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena, "GET /stale HTTP/1.1\r\nHost: example\r\n\r\n", &request) != 0 ||
        expect_status(sl_http_route_table_build(&arena, &source_plan, &table, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 23;
    }

    if (expect_status(sl_http_dispatch_request_head(&arena, engine, &current_plan, &table.dispatch,
                                                    &request, &result, &diag),
                      SL_STATUS_OUT_OF_RANGE) != 0)
    {
        sl_engine_destroy(engine);
        return 24;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_ENGINE_CALL_ERROR ||
        expect_str_equal(diag.message, "matched route handler ID was not found in app plan") != 0)
    {
        sl_engine_destroy(engine);
        return 25;
    }

    sl_engine_destroy(engine);
    return 0;
}

static SlPlan validation_plan(SlPlanHandler* handler, SlPlanRoute* route,
                              SlPlanRequestBinding* bindings, SlPlanSchema* schemas,
                              SlPlanSchemaProperty* properties, SlPlanSchemaNode* property_nodes)
{
    SlPlan plan = one_handler_plan(handler);

    route->method = sl_str_from_cstr("POST");
    route->pattern = sl_str_from_cstr("/users");
    route->handler_id = 1U;
    route->bindings = bindings;
    route->binding_count = 1U;
    route->json_request.mode = SL_PLAN_JSON_REQUEST_NATIVE_SCHEMA;
    route->json_request.materialization = SL_PLAN_JSON_MATERIALIZATION_MATERIALIZE_ONCE;
    route->json_request.schema = sl_str_from_cstr("UserCreate");

    bindings[0].kind = SL_PLAN_REQUEST_BINDING_BODY_JSON;
    bindings[0].parameter = sl_str_from_cstr("input");
    bindings[0].schema = sl_str_from_cstr("UserCreate");
    bindings[0].type = sl_str_from_cstr("UserCreate");

    schemas[0].name = sl_str_from_cstr("UserCreate");
    schemas[0].definition.kind = SL_PLAN_SCHEMA_OBJECT;
    schemas[0].definition.properties = properties;
    schemas[0].definition.property_count = 3U;

    properties[0].name = sl_str_from_cstr("name");
    properties[0].schema = &property_nodes[0];
    property_nodes[0].kind = SL_PLAN_SCHEMA_STRING;
    property_nodes[0].has_min = true;
    property_nodes[0].min_value = 1;

    properties[1].name = sl_str_from_cstr("email");
    properties[1].schema = &property_nodes[1];
    property_nodes[1].kind = SL_PLAN_SCHEMA_STRING;
    property_nodes[1].validation = sl_str_from_cstr("email");

    properties[2].name = sl_str_from_cstr("password");
    properties[2].schema = &property_nodes[2];
    property_nodes[2].kind = SL_PLAN_SCHEMA_STRING;
    property_nodes[2].has_min = true;
    property_nodes[2].min_value = 8;
    property_nodes[2].secret = true;

    plan.routes = route;
    plan.route_count = 1U;
    plan.schemas = schemas;
    plan.schema_count = 1U;
    return plan;
}

static int test_plan_backed_body_validation_returns_problem_before_handler_call(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlHttpRequestHead request = {0};
    SlHttpRouteTable table = {0};
    SlPlanHandler handler = {0};
    SlPlanRoute route = {0};
    SlPlanRequestBinding bindings[1] = {0};
    SlPlanSchema schemas[1] = {0};
    SlPlanSchemaProperty properties[3] = {0};
    SlPlanSchemaNode property_nodes[3] = {0};
    SlPlan plan = validation_plan(&handler, &route, bindings, schemas, properties, property_nodes);
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena,
                      "POST /users HTTP/1.1\r\nHost: example\r\n"
                      "Content-Type: application/json\r\nContent-Length: 43\r\n\r\n"
                      "{\"name\":\"\",\"email\":\"no\",\"password\":\"short\"}",
                      &request) != 0 ||
        expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag), SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 140;
    }

    if (expect_status(sl_http_dispatch_request_head(&arena, engine, &plan, &table.dispatch,
                                                    &request, &result, &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_ERROR || result.response.status != 400U ||
        result.response.kind != SL_HTTP_RESPONSE_PROBLEM ||
        diag.code != SL_DIAG_REQUEST_VALIDATION_FAILED)
    {
        sl_engine_destroy(engine);
        return 141;
    }

    if (expect_body_contains(result.response.body, "\"body.name\"") != 0 ||
        expect_body_contains(result.response.body, "\"body.email\"") != 0 ||
        expect_body_contains(result.response.body, "\"body.password\"") != 0 ||
        expect_body_contains(result.response.body, "short") == 0 ||
        expect_body_contains(result.response.body, "\"no\"") == 0)
    {
        sl_engine_destroy(engine);
        return 142;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_plan_backed_json_body_rejects_wrong_content_type_before_handler_call(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlHttpRequestHead request = {0};
    SlHttpRouteTable table = {0};
    SlPlanHandler handler = {0};
    SlPlanRoute route = {0};
    SlPlanRequestBinding bindings[1] = {0};
    SlPlanSchema schemas[1] = {0};
    SlPlanSchemaProperty properties[3] = {0};
    SlPlanSchemaNode property_nodes[3] = {0};
    SlPlan plan = validation_plan(&handler, &route, bindings, schemas, properties, property_nodes);
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena,
                      "POST /users HTTP/1.1\r\nHost: example\r\n"
                      "Content-Type: text/plain\r\nContent-Length: 2\r\n\r\n{}",
                      &request) != 0 ||
        expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag), SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 143;
    }

    if (expect_status(sl_http_dispatch_request_head(&arena, engine, &plan, &table.dispatch,
                                                    &request, &result, &diag),
                      SL_STATUS_UNSUPPORTED) != 0 ||
        result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_HTTP_UNSUPPORTED_MEDIA_TYPE)
    {
        sl_engine_destroy(engine);
        return 144;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_plan_backed_json_route_accepts_structured_json_content_type(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlHttpRequestHead request = {0};
    SlHttpRouteTable table = {0};
    SlPlanHandler handler = {0};
    SlPlanRoute route = {0};
    SlPlanRequestBinding bindings[1] = {0};
    SlPlanSchema schemas[1] = {0};
    SlPlanSchemaProperty properties[3] = {0};
    SlPlanSchemaNode property_nodes[3] = {0};
    SlPlan plan = validation_plan(&handler, &route, bindings, schemas, properties, property_nodes);
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena,
                      "POST /users HTTP/1.1\r\nHost: example\r\n"
                      "Content-Type: application/vnd.sloppy+json\r\nContent-Length: 62\r\n\r\n"
                      "{\"name\":\"Ada\",\"email\":\"ada@example.com\",\"password\":\"longpass\"}",
                      &request) != 0 ||
        expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag), SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 145;
    }

    if (expect_status(sl_http_dispatch_request_head(&arena, engine, &plan, &table.dispatch,
                                                    &request, &result, &diag),
                      SL_STATUS_UNSUPPORTED) != 0 ||
        result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_UNSUPPORTED_ENGINE)
    {
        sl_engine_destroy(engine);
        return 146;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_native_json_body_exceeding_route_max_rejects_before_validation(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlHttpRequestHead request = {0};
    SlHttpRouteTable table = {0};
    SlPlanHandler handler = {0};
    SlPlanRoute route = {0};
    SlPlanRequestBinding bindings[1] = {0};
    SlPlanSchema schemas[1] = {0};
    SlPlanSchemaProperty properties[3] = {0};
    SlPlanSchemaNode property_nodes[3] = {0};
    SlPlan plan = validation_plan(&handler, &route, bindings, schemas, properties, property_nodes);
    SlEngineResult result = {0};
    SlDiag diag = {0};

    route.json_request.max_body_bytes = 1U;
    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena,
                      "POST /users HTTP/1.1\r\nHost: example\r\n"
                      "Content-Type: application/json\r\nContent-Length: 2\r\n\r\n{}",
                      &request) != 0 ||
        expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag), SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 147;
    }

    if (expect_status(sl_http_dispatch_request_head(&arena, engine, &plan, &table.dispatch,
                                                    &request, &result, &diag),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_HTTP_BODY_LIMIT)
    {
        sl_engine_destroy(engine);
        return 148;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_route_json_max_overrides_higher_backend_body_limit(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlHttpBackendOptions options = {0};
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};
    SlHttpRequestLifecycle request = {0};
    SlHttpRouteTable table = {0};
    SlPlanHandler handler = {0};
    SlPlanRoute route = {0};
    SlPlanRequestBinding bindings[1] = {0};
    SlPlanSchema schemas[1] = {0};
    SlPlanSchemaProperty properties[3] = {0};
    SlPlanSchemaNode property_nodes[3] = {0};
    SlPlan plan = validation_plan(&handler, &route, bindings, schemas, properties, property_nodes);
    SlEngineResult result = {0};
    SlDiag diag = {0};

    options.parse.max_body_length = 1024U;
    route.json_request.max_body_bytes = 4U;
    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        expect_status(sl_http_backend_init(&backend, &options, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_backend_start(&backend, NULL, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_backend_accept_connection(&backend, &connection, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_begin(&connection, &arena, &request, NULL), SL_STATUS_OK) !=
            0 ||
        parse_request(&arena,
                      "POST /users HTTP/1.1\r\nHost: example\r\n"
                      "Content-Type: application/json\r\nContent-Length: 5\r\n\r\n12345",
                      &request.head) != 0 ||
        expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag), SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 149;
    }

    if (expect_status(sl_http_dispatch_request_lifecycle(&arena, engine, &plan, &table.dispatch,
                                                         &request, &result, &diag),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_HTTP_BODY_LIMIT)
    {
        sl_engine_destroy(engine);
        return 150;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_json_route_without_route_max_uses_backend_body_limit(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlHttpBackendOptions options = {0};
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};
    SlHttpRequestLifecycle request = {0};
    SlHttpRouteTable table = {0};
    SlPlanHandler handler = {0};
    SlPlanRoute route = {0};
    SlPlanRequestBinding bindings[1] = {0};
    SlPlanSchema schemas[1] = {0};
    SlPlanSchemaProperty properties[3] = {0};
    SlPlanSchemaNode property_nodes[3] = {0};
    SlPlan plan = validation_plan(&handler, &route, bindings, schemas, properties, property_nodes);
    SlEngineResult result = {0};
    SlDiag diag = {0};

    options.parse.max_body_length = 4U;
    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        expect_status(sl_http_backend_init(&backend, &options, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_backend_start(&backend, NULL, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_backend_accept_connection(&backend, &connection, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_begin(&connection, &arena, &request, NULL), SL_STATUS_OK) !=
            0 ||
        parse_request(&arena,
                      "POST /users HTTP/1.1\r\nHost: example\r\n"
                      "Content-Type: application/json\r\nContent-Length: 5\r\n\r\n12345",
                      &request.head) != 0 ||
        expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag), SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 151;
    }

    if (expect_status(sl_http_dispatch_request_lifecycle(&arena, engine, &plan, &table.dispatch,
                                                         &request, &result, &diag),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_HTTP_BODY_LIMIT)
    {
        sl_engine_destroy(engine);
        return 152;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_generic_json_route_still_uses_json_media_and_size_policy(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlHttpRequestHead request = {0};
    SlHttpRouteTable table = {0};
    SlPlanHandler handler = {0};
    SlPlanRoute route = {0};
    SlPlan plan = one_handler_plan(&handler);
    SlEngineResult result = {0};
    SlDiag diag = {0};

    route.method = sl_str_from_cstr("POST");
    route.pattern = sl_str_from_cstr("/echo");
    route.handler_id = 1U;
    route.json_request.mode = SL_PLAN_JSON_REQUEST_GENERIC;
    route.json_request.materialization = SL_PLAN_JSON_MATERIALIZATION_GENERIC;
    route.json_request.max_body_bytes = 4U;
    plan.routes = &route;
    plan.route_count = 1U;

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena,
                      "POST /echo HTTP/1.1\r\nHost: example\r\n"
                      "Content-Type: application/json\r\nContent-Length: 5\r\n\r\n12345",
                      &request) != 0 ||
        expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag), SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 153;
    }

    if (expect_status(sl_http_dispatch_request_head(&arena, engine, &plan, &table.dispatch,
                                                    &request, &result, &diag),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_HTTP_BODY_LIMIT)
    {
        sl_engine_destroy(engine);
        return 154;
    }

    sl_arena_reset(&arena);
    result = (SlEngineResult){0};
    diag = (SlDiag){0};
    if (parse_request(&arena,
                      "POST /echo HTTP/1.1\r\nHost: example\r\n"
                      "Content-Type: text/plain\r\nContent-Length: 2\r\n\r\n{}",
                      &request) != 0 ||
        expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag), SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 155;
    }

    if (expect_status(sl_http_dispatch_request_head(&arena, engine, &plan, &table.dispatch,
                                                    &request, &result, &diag),
                      SL_STATUS_UNSUPPORTED) != 0 ||
        result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_HTTP_UNSUPPORTED_MEDIA_TYPE)
    {
        sl_engine_destroy(engine);
        return 156;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int expect_required_json_body_rejected(const char* request_text)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlHttpRequestHead request = {0};
    SlHttpRouteTable table = {0};
    SlPlanHandler handler = {0};
    SlPlanRoute route = {0};
    SlPlanRequestBinding bindings[1] = {0};
    SlPlanSchema schemas[1] = {0};
    SlPlanSchemaProperty properties[3] = {0};
    SlPlanSchemaNode property_nodes[3] = {0};
    SlPlan plan = validation_plan(&handler, &route, bindings, schemas, properties, property_nodes);
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena, request_text, &request) != 0 ||
        expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag), SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 1;
    }

    if (expect_status(sl_http_dispatch_request_head(&arena, engine, &plan, &table.dispatch,
                                                    &request, &result, &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_ERROR || result.response.status != 400U ||
        result.response.kind != SL_HTTP_RESPONSE_PROBLEM ||
        diag.code != SL_DIAG_REQUEST_VALIDATION_FAILED ||
        expect_body_contains(result.response.body, "\"body.required\"") != 0)
    {
        sl_engine_destroy(engine);
        return 2;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_schema_json_route_rejects_empty_or_missing_body_before_handler_call(void)
{
    static const char* const requests[] = {
        "POST /users HTTP/1.1\r\nHost: example\r\n\r\n",
        "POST /users HTTP/1.1\r\nHost: example\r\nContent-Length: 0\r\n\r\n",
        "POST /users HTTP/1.1\r\nHost: example\r\nContent-Type: application/json\r\n\r\n",
        ("POST /users HTTP/1.1\r\nHost: example\r\nContent-Type: application/json\r\n"
         "Content-Length: 0\r\n\r\n"),
    };
    size_t index = 0U;

    for (index = 0U; index < sizeof(requests) / sizeof(requests[0]); index += 1U) {
        if (expect_required_json_body_rejected(requests[index]) != 0) {
            return 157 + (int)index;
        }
    }
    return 0;
}

typedef struct JsonModeDispatchSnapshot
{
    SlStatusCode status_code;
    SlDiagCode diag_code;
    SlEngineResultKind result_kind;
    uint16_t response_status;
} JsonModeDispatchSnapshot;

static int run_validation_plan_with_json_mode(const char* mode, const char* request_text,
                                              bool native_response, JsonModeDispatchSnapshot* out)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlHttpRequestHead request = {0};
    SlHttpRouteTable table = {0};
    SlPlanHandler handler = {0};
    SlPlanRoute route = {0};
    SlPlanRequestBinding bindings[1] = {0};
    SlPlanSchema schemas[1] = {0};
    SlPlanSchemaProperty properties[3] = {0};
    SlPlanSchemaNode property_nodes[3] = {0};
    SlPlan plan = validation_plan(&handler, &route, bindings, schemas, properties, property_nodes);
    SlEngineResult result = {0};
    SlDiag diag = {0};
    char* original = NULL;
    SlStatus status;

    if (out == NULL || copy_test_env_value("SLOPPY_JSON_DISPATCH", &original) != 0 ||
        set_test_env_value("SLOPPY_JSON_DISPATCH", mode) != 0)
    {
        free(original);
        return 1;
    }

    if (native_response) {
        route.native_response_kind = sl_str_from_cstr("json");
        route.native_response_status = 200U;
        route.native_response_body = sl_str_from_cstr("{\"ok\":true}");
        route.native_response_content_type = sl_str_from_cstr("application/json");
    }

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena, request_text, &request) != 0 ||
        expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag), SL_STATUS_OK) != 0)
    {
        restore_test_env_value("SLOPPY_JSON_DISPATCH", original);
        free(original);
        sl_engine_destroy(engine);
        return 2;
    }

    status = sl_http_dispatch_request_head(&arena, engine, &plan, &table.dispatch, &request,
                                           &result, &diag);
    out->status_code = sl_status_code(status);
    out->diag_code = diag.code;
    out->result_kind = result.kind;
    out->response_status =
        result.payload_kind == SL_ENGINE_RESULT_PAYLOAD_RESPONSE ? result.response.status : 0U;

    if (restore_test_env_value("SLOPPY_JSON_DISPATCH", original) != 0) {
        free(original);
        sl_engine_destroy(engine);
        return 3;
    }
    free(original);
    sl_engine_destroy(engine);
    return 0;
}

static int test_json_dispatch_mode_generic_matches_native_for_supported_schema(void)
{
    static const char* valid_request =
        "POST /users HTTP/1.1\r\nHost: example\r\nContent-Type: application/json\r\n"
        "Content-Length: 62\r\n\r\n"
        "{\"name\":\"Ada\",\"email\":\"ada@example.com\",\"password\":\"longpass\"}";
    static const char* invalid_request =
        "POST /users HTTP/1.1\r\nHost: example\r\nContent-Type: application/json\r\n"
        "Content-Length: 59\r\n\r\n"
        "{\"name\":\"Ada\",\"email\":\"not-an-email\",\"password\":\"longpass\"}";
    JsonModeDispatchSnapshot native_valid = {0};
    JsonModeDispatchSnapshot generic_valid = {0};
    JsonModeDispatchSnapshot native_invalid = {0};
    JsonModeDispatchSnapshot generic_invalid = {0};

    if (run_validation_plan_with_json_mode("native", valid_request, true, &native_valid) != 0 ||
        run_validation_plan_with_json_mode("generic", valid_request, true, &generic_valid) != 0 ||
        run_validation_plan_with_json_mode("native", invalid_request, false, &native_invalid) !=
            0 ||
        run_validation_plan_with_json_mode("generic", invalid_request, false, &generic_invalid) !=
            0)
    {
        return 161;
    }

    if (native_valid.status_code != generic_valid.status_code ||
        native_valid.diag_code != generic_valid.diag_code ||
        native_valid.result_kind != generic_valid.result_kind ||
        native_valid.response_status != generic_valid.response_status ||
        native_invalid.status_code != generic_invalid.status_code ||
        native_invalid.diag_code != generic_invalid.diag_code ||
        native_invalid.result_kind != generic_invalid.result_kind ||
        native_invalid.response_status != generic_invalid.response_status)
    {
        return 162;
    }

    return 0;
}

static int test_json_dispatch_validate_mode_matches_native_for_supported_route(void)
{
    static const char* request =
        "POST /users HTTP/1.1\r\nHost: example\r\nContent-Type: application/json\r\n"
        "Content-Length: 62\r\n\r\n"
        "{\"name\":\"Ada\",\"email\":\"ada@example.com\",\"password\":\"longpass\"}";
    JsonModeDispatchSnapshot native_result = {0};
    JsonModeDispatchSnapshot validate_result = {0};

    if (run_validation_plan_with_json_mode("native", request, true, &native_result) != 0 ||
        run_validation_plan_with_json_mode("validate", request, true, &validate_result) != 0)
    {
        return 163;
    }

    if (native_result.status_code != validate_result.status_code ||
        native_result.diag_code != validate_result.diag_code ||
        native_result.result_kind != validate_result.result_kind ||
        native_result.response_status != validate_result.response_status)
    {
        return 164;
    }

    return 0;
}

static int test_plan_backed_route_query_header_validation_returns_problem(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlHttpRequestHead request = {0};
    SlHttpRouteTable table = {0};
    SlPlanHandler handler = {0};
    SlPlanRoute route = {0};
    SlPlanRequestBinding bindings[3] = {0};
    SlPlan plan = one_handler_plan(&handler);
    SlEngineResult result = {0};
    SlDiag diag = {0};

    route.method = sl_str_from_cstr("GET");
    route.pattern = sl_str_from_cstr("/users/{id}");
    route.handler_id = 1U;
    route.bindings = bindings;
    route.binding_count = 3U;
    bindings[0].kind = SL_PLAN_REQUEST_BINDING_ROUTE;
    bindings[0].parameter = sl_str_from_cstr("id");
    bindings[0].name = sl_str_from_cstr("id");
    bindings[0].schema = sl_str_from_cstr("number");
    bindings[0].type = sl_str_from_cstr("Route<number>");
    bindings[1].kind = SL_PLAN_REQUEST_BINDING_QUERY;
    bindings[1].parameter = sl_str_from_cstr("includeDeleted");
    bindings[1].name = sl_str_from_cstr("includeDeleted");
    bindings[1].schema = sl_str_from_cstr("bool");
    bindings[1].type = sl_str_from_cstr("Query<boolean>");
    bindings[2].kind = SL_PLAN_REQUEST_BINDING_HEADER;
    bindings[2].parameter = sl_str_from_cstr("trace");
    bindings[2].name = sl_str_from_cstr("x-trace-id");
    bindings[2].type = sl_str_from_cstr("string");
    plan.routes = &route;
    plan.route_count = 1U;

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena,
                      "GET /users/not-int?includeDeleted=maybe HTTP/1.1\r\nHost: example\r\n\r\n",
                      &request) != 0 ||
        expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag), SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 143;
    }

    if (expect_status(sl_http_dispatch_request_head(&arena, engine, &plan, &table.dispatch,
                                                    &request, &result, &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_ERROR || result.response.status != 400U ||
        result.response.kind != SL_HTTP_RESPONSE_PROBLEM ||
        diag.code != SL_DIAG_REQUEST_VALIDATION_FAILED)
    {
        sl_engine_destroy(engine);
        return 144;
    }

    if (expect_body_contains(result.response.body, "\"id\"") != 0 ||
        expect_body_contains(result.response.body, "\"includeDeleted\"") != 0 ||
        expect_body_contains(result.response.body, "\"x-trace-id\"") != 0 ||
        expect_body_contains(result.response.body, "not-int") == 0 ||
        expect_body_contains(result.response.body, "maybe") == 0)
    {
        sl_engine_destroy(engine);
        return 145;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_plan_backed_nullable_required_body_field_must_be_present(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlHttpRequestHead request = {0};
    SlHttpRouteTable table = {0};
    SlPlanHandler handler = {0};
    SlPlanRoute route = {0};
    SlPlanRequestBinding bindings[1] = {0};
    SlPlanSchema schemas[1] = {0};
    SlPlanSchemaProperty properties[3] = {0};
    SlPlanSchemaNode property_nodes[3] = {0};
    SlPlan plan = validation_plan(&handler, &route, bindings, schemas, properties, property_nodes);
    SlEngineResult result = {0};
    SlDiag diag = {0};

    property_nodes[1].nullable = true;
    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena,
                      "POST /users HTTP/1.1\r\nHost: example\r\n"
                      "Content-Type: application/json\r\nContent-Length: 36\r\n\r\n"
                      "{\"name\":\"Ada\",\"password\":\"longpass\"}",
                      &request) != 0 ||
        expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag), SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 146;
    }

    if (expect_status(sl_http_dispatch_request_head(&arena, engine, &plan, &table.dispatch,
                                                    &request, &result, &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_ERROR || result.response.status != 400U ||
        diag.code != SL_DIAG_REQUEST_VALIDATION_FAILED)
    {
        sl_engine_destroy(engine);
        return 147;
    }

    if (expect_body_contains(result.response.body, "\"body.email\"") != 0 ||
        expect_body_contains(result.response.body, "Ada") == 0 ||
        expect_body_contains(result.response.body, "longpass") == 0)
    {
        sl_engine_destroy(engine);
        return 148;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_plan_backed_array_validation_reports_indexed_paths(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlHttpRequestHead request = {0};
    SlHttpRouteTable table = {0};
    SlPlanHandler handler = {0};
    SlPlanRoute route = {0};
    SlPlanRequestBinding bindings[1] = {0};
    SlPlanSchema schemas[1] = {0};
    SlPlanSchemaNode item = {0};
    SlPlan plan = one_handler_plan(&handler);
    SlEngineResult result = {0};
    SlDiag diag = {0};

    route.method = sl_str_from_cstr("POST");
    route.pattern = sl_str_from_cstr("/tags");
    route.handler_id = 1U;
    route.bindings = bindings;
    route.binding_count = 1U;
    bindings[0].kind = SL_PLAN_REQUEST_BINDING_BODY_JSON;
    bindings[0].parameter = sl_str_from_cstr("tags");
    bindings[0].schema = sl_str_from_cstr("Tags");
    schemas[0].name = sl_str_from_cstr("Tags");
    schemas[0].definition.kind = SL_PLAN_SCHEMA_ARRAY;
    schemas[0].definition.items = &item;
    item.kind = SL_PLAN_SCHEMA_STRING;
    item.has_min = true;
    item.min_value = 1;
    plan.routes = &route;
    plan.route_count = 1U;
    plan.schemas = schemas;
    plan.schema_count = 1U;

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena,
                      "POST /tags HTTP/1.1\r\nHost: example\r\n"
                      "Content-Type: application/json\r\nContent-Length: 9\r\n\r\n"
                      "[\"ok\",\"\"]",
                      &request) != 0 ||
        expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag), SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 151;
    }

    if (expect_status(sl_http_dispatch_request_head(&arena, engine, &plan, &table.dispatch,
                                                    &request, &result, &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_ERROR || result.response.status != 400U ||
        result.response.kind != SL_HTTP_RESPONSE_PROBLEM ||
        diag.code != SL_DIAG_REQUEST_VALIDATION_FAILED)
    {
        sl_engine_destroy(engine);
        return 152;
    }

    if (expect_body_contains(result.response.body, "\"body[1]\"") != 0) {
        sl_engine_destroy(engine);
        return 153;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_plan_backed_body_validation_rejects_excessive_json_depth(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlHttpRequestHead request = {0};
    SlHttpRouteTable table = {0};
    SlPlanHandler handler = {0};
    SlPlanRoute route = {0};
    SlPlanRequestBinding bindings[1] = {0};
    SlPlanSchema schemas[1] = {0};
    SlPlan plan = one_handler_plan(&handler);
    SlEngineResult result = {0};
    SlDiag diag = {0};

    route.method = sl_str_from_cstr("POST");
    route.pattern = sl_str_from_cstr("/deep");
    route.handler_id = 1U;
    route.bindings = bindings;
    route.binding_count = 1U;
    bindings[0].kind = SL_PLAN_REQUEST_BINDING_BODY_JSON;
    bindings[0].parameter = sl_str_from_cstr("body");
    bindings[0].schema = sl_str_from_cstr("DeepArray");
    schemas[0].name = sl_str_from_cstr("DeepArray");
    schemas[0].definition.kind = SL_PLAN_SCHEMA_ARRAY;
    schemas[0].definition.items = &schemas[0].definition;
    plan.routes = &route;
    plan.route_count = 1U;
    plan.schemas = schemas;
    plan.schema_count = 1U;

    if (expect_true(sizeof(DEEP_JSON_BODY) - 1U == DEEP_JSON_BODY_LENGTH) != 0 ||
        init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena,
                      "POST /deep HTTP/1.1\r\nHost: example\r\n"
                      "Content-Type: application/json\r\nContent-Length: " TEST_STRINGIFY(
                          DEEP_JSON_BODY_LENGTH) "\r\n\r\n" DEEP_JSON_BODY,
                      &request) != 0 ||
        expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag), SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 154;
    }

    if (expect_status(sl_http_dispatch_request_head(&arena, engine, &plan, &table.dispatch,
                                                    &request, &result, &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_ERROR || result.response.status != 400U ||
        result.response.kind != SL_HTTP_RESPONSE_PROBLEM ||
        diag.code != SL_DIAG_REQUEST_VALIDATION_FAILED)
    {
        sl_engine_destroy(engine);
        return 155;
    }

    if (expect_body_contains(result.response.body, "maximum validation depth") != 0) {
        sl_engine_destroy(engine);
        return 156;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_plan_backed_body_validation_accepts_max_json_depth(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlHttpRequestHead request = {0};
    SlHttpRouteTable table = {0};
    SlPlanHandler handler = {0};
    SlPlanRoute route = {0};
    SlPlanRequestBinding bindings[1] = {0};
    SlPlanSchema schemas[1] = {0};
    SlPlan plan = one_handler_plan(&handler);
    SlEngineResult result = {0};
    SlDiag diag = {0};

    route.method = sl_str_from_cstr("POST");
    route.pattern = sl_str_from_cstr("/deep");
    route.handler_id = 1U;
    route.bindings = bindings;
    route.binding_count = 1U;
    bindings[0].kind = SL_PLAN_REQUEST_BINDING_BODY_JSON;
    bindings[0].parameter = sl_str_from_cstr("body");
    bindings[0].schema = sl_str_from_cstr("DeepArray");
    schemas[0].name = sl_str_from_cstr("DeepArray");
    schemas[0].definition.kind = SL_PLAN_SCHEMA_ARRAY;
    schemas[0].definition.items = &schemas[0].definition;
    plan.routes = &route;
    plan.route_count = 1U;
    plan.schemas = schemas;
    plan.schema_count = 1U;

    if (expect_true(sizeof(DEEP_JSON_MAX_DEPTH_BODY) - 1U == DEEP_JSON_MAX_DEPTH_BODY_LENGTH) !=
            0 ||
        init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena,
                      "POST /deep HTTP/1.1\r\nHost: example\r\n"
                      "Content-Type: application/json\r\nContent-Length: " TEST_STRINGIFY(
                          DEEP_JSON_MAX_DEPTH_BODY_LENGTH) "\r\n\r\n" DEEP_JSON_MAX_DEPTH_BODY,
                      &request) != 0 ||
        expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag), SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 157;
    }

    if (expect_status(sl_http_dispatch_request_head(&arena, engine, &plan, &table.dispatch,
                                                    &request, &result, &diag),
                      SL_STATUS_UNSUPPORTED) != 0 ||
        result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_UNSUPPORTED_ENGINE)
    {
        sl_engine_destroy(engine);
        return 158;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_manual_dispatch_ignores_stale_route_index_for_validation(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlHttpRequestHead request = {0};
    SlRoutePattern pattern = {0};
    SlHttpRouteBinding route = {0};
    SlHttpDispatchTable table = {0};
    SlPlanHandler handler = {0};
    SlPlanRoute routes[2] = {0};
    SlPlanRequestBinding stale_binding = {0};
    SlPlan plan = one_handler_plan(&handler);
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlStatus status;

    routes[0].method = sl_str_from_cstr("GET");
    routes[0].pattern = sl_str_from_cstr("/admin");
    routes[0].handler_id = 1U;
    routes[0].bindings = &stale_binding;
    routes[0].binding_count = 1U;
    stale_binding.kind = SL_PLAN_REQUEST_BINDING_HEADER;
    stale_binding.parameter = sl_str_from_cstr("trace");
    stale_binding.name = sl_str_from_cstr("x-trace-id");
    stale_binding.type = sl_str_from_cstr("string");
    routes[1].method = sl_str_from_cstr("GET");
    routes[1].pattern = sl_str_from_cstr("/users");
    routes[1].handler_id = 1U;
    plan.routes = routes;
    plan.route_count = 2U;

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena, "GET /users HTTP/1.1\r\nHost: example\r\n\r\n", &request) != 0 ||
        parse_pattern(&arena, "/users", &pattern) != 0)
    {
        sl_engine_destroy(engine);
        return 149;
    }

    route.method = SL_HTTP_METHOD_GET;
    route.pattern = &pattern;
    route.handler_id = 1U;
    route.route_index = 0U;
    table.routes = &route;
    table.route_count = 1U;

    status = sl_http_dispatch_request_head(&arena, engine, &plan, &table, &request, &result, &diag);
    if (sl_status_code(status) != SL_STATUS_UNSUPPORTED || result.kind != SL_ENGINE_RESULT_NONE ||
        diag.code != SL_DIAG_UNSUPPORTED_ENGINE)
    {
        sl_engine_destroy(engine);
        return 150;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_route_params_may_match_but_are_not_required_by_dispatch(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlHttpRequestHead request = {0};
    SlRoutePattern pattern = {0};
    SlHttpRouteBinding route = {0};
    SlHttpDispatchTable table = {0};
    SlPlanHandler handler = {0};
    SlPlan plan = one_handler_plan(&handler);
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena, "GET /users/123 HTTP/1.1\r\nHost: example\r\n\r\n", &request) != 0 ||
        parse_pattern(&arena, "/users/{id:int}", &pattern) != 0)
    {
        sl_engine_destroy(engine);
        return 30;
    }

    route.method = SL_HTTP_METHOD_GET;
    route.pattern = &pattern;
    route.handler_id = 1U;
    table.routes = &route;
    table.route_count = 1U;

    if (expect_status(
            sl_http_dispatch_request_head(&arena, engine, &plan, &table, &request, &result, &diag),
            SL_STATUS_UNSUPPORTED) != 0)
    {
        sl_engine_destroy(engine);
        return 31;
    }

    if (diag.code != SL_DIAG_UNSUPPORTED_ENGINE) {
        sl_engine_destroy(engine);
        return 32;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int dispatch_noop_case(const char* request_text, SlHttpMethod route_method,
                              const char* pattern_text, SlStatusCode expected_status,
                              SlDiagCode expected_diag_code)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlHttpRequestHead request = {0};
    SlRoutePattern pattern = {0};
    SlHttpRouteBinding route = {0};
    SlHttpDispatchTable table = {0};
    SlPlanHandler handler = {0};
    SlPlan plan = one_handler_plan(&handler);
    SlEngineResult result = {.kind = SL_ENGINE_RESULT_TEXT, .text = sl_str_from_cstr("stale")};
    SlDiag diag = {0};
    SlStatus status;

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena, request_text, &request) != 0 ||
        parse_pattern(&arena, pattern_text, &pattern) != 0)
    {
        sl_engine_destroy(engine);
        return 1;
    }

    route.method = route_method;
    route.pattern = &pattern;
    route.handler_id = 1U;
    table.routes = &route;
    table.route_count = 1U;

    status = sl_http_dispatch_request_head(&arena, engine, &plan, &table, &request, &result, &diag);
    if (sl_status_code(status) != expected_status || result.kind != SL_ENGINE_RESULT_NONE ||
        diag.code != expected_diag_code)
    {
        sl_engine_destroy(engine);
        return 2;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_conformance_smoke_default_http_cases(void)
{
    typedef struct DispatchCase
    {
        const char* request;
        SlHttpMethod route_method;
        const char* pattern;
        SlStatusCode status;
        SlDiagCode diag;
    } DispatchCase;

    static const DispatchCase cases[] = {
        {"GET /ok HTTP/1.1\r\nHost: example\r\n\r\n", SL_HTTP_METHOD_GET, "/ok",
         SL_STATUS_UNSUPPORTED, SL_DIAG_UNSUPPORTED_ENGINE},
        {"POST /ok HTTP/1.1\r\nHost: example\r\nContent-Length: 0\r\n\r\n", SL_HTTP_METHOD_POST,
         "/ok", SL_STATUS_UNSUPPORTED, SL_DIAG_UNSUPPORTED_ENGINE},
        {"PUT /ok HTTP/1.1\r\nHost: example\r\nContent-Type: text/plain\r\nContent-Length: "
         "5\r\n\r\nhello",
         SL_HTTP_METHOD_PUT, "/ok", SL_STATUS_UNSUPPORTED, SL_DIAG_UNSUPPORTED_ENGINE},
        {"PATCH /ok HTTP/1.1\r\nHost: example\r\nContent-Type: application/json\r\nContent-"
         "Length: 11\r\n\r\n"
         "{\"ok\":true}",
         SL_HTTP_METHOD_PATCH, "/ok", SL_STATUS_UNSUPPORTED, SL_DIAG_UNSUPPORTED_ENGINE},
        {"DELETE /ok HTTP/1.1\r\nHost: example\r\n\r\n", SL_HTTP_METHOD_DELETE, "/ok",
         SL_STATUS_UNSUPPORTED, SL_DIAG_UNSUPPORTED_ENGINE},
        {"GET /missing HTTP/1.1\r\nHost: example\r\n\r\n", SL_HTTP_METHOD_GET, "/ok",
         SL_STATUS_OUT_OF_RANGE, SL_DIAG_HTTP_ROUTE_NOT_FOUND},
        {"POST /ok HTTP/1.1\r\nHost: example\r\n\r\n", SL_HTTP_METHOD_GET, "/ok",
         SL_STATUS_UNSUPPORTED, SL_DIAG_HTTP_METHOD_NOT_ALLOWED},
        {"POST /ok HTTP/1.1\r\nHost: example\r\nContent-Type: application/json\r\nContent-"
         "Length: 6\r\n\r\n"
         "{\"ok\":",
         SL_HTTP_METHOD_POST, "/ok", SL_STATUS_INVALID_ARGUMENT, SL_DIAG_MALFORMED_JSON},
        {"POST /ok HTTP/1.1\r\nHost: example\r\nContent-Type: application/octet-stream\r\n"
         "Content-Length: 3\r\n\r\nabc",
         SL_HTTP_METHOD_POST, "/ok", SL_STATUS_UNSUPPORTED, SL_DIAG_UNSUPPORTED_ENGINE},
        {"POST /ok HTTP/1.1\r\nHost: example\r\nContent-Type: application/x-unsupported\r\n"
         "Content-Length: 3\r\n\r\nabc",
         SL_HTTP_METHOD_POST, "/ok", SL_STATUS_UNSUPPORTED, SL_DIAG_HTTP_UNSUPPORTED_MEDIA_TYPE},
        {"GET /ok?q=%zz HTTP/1.1\r\nHost: example\r\n\r\n", SL_HTTP_METHOD_GET, "/ok",
         SL_STATUS_INVALID_ARGUMENT, SL_DIAG_INVALID_HTTP_REQUEST},
    };
    size_t index = 0U;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index += 1U) {
        if (dispatch_noop_case(cases[index].request, cases[index].route_method,
                               cases[index].pattern, cases[index].status, cases[index].diag) != 0)
        {
            return 110 + (int)index;
        }
    }

    return 0;
}

static int test_plan_route_without_query_binding_skips_malformed_query(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlPlanHandler handler = {0};
    SlPlanRoute route = {0};
    SlPlan plan = one_handler_plan(&handler);
    SlHttpRouteTable table = {0};
    SlHttpRequestHead request = {0};
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlStatus status;

    route.method = sl_str_from_cstr("GET");
    route.pattern = sl_str_from_cstr("/ok");
    route.handler_id = 1U;
    sl_plan_route_mark_bindings_empty(&route);
    plan.routes = &route;
    plan.route_count = 1U;

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena, "GET /ok?q=%zz HTTP/1.1\r\nHost: example\r\n\r\n", &request) != 0 ||
        expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag), SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 170;
    }

    status = sl_http_dispatch_request_head(&arena, engine, &plan, &table.dispatch, &request,
                                           &result, &diag);
    sl_engine_destroy(engine);
    if (expect_status(status, SL_STATUS_UNSUPPORTED) != 0 || result.kind != SL_ENGINE_RESULT_NONE ||
        diag.code != SL_DIAG_UNSUPPORTED_ENGINE)
    {
        return 171;
    }

    return 0;
}

static int test_plan_route_with_query_binding_rejects_malformed_query(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlPlanHandler handler = {0};
    SlPlanRoute route = {0};
    SlPlanRequestBinding binding = {0};
    SlPlan plan = one_handler_plan(&handler);
    SlHttpRouteTable table = {0};
    SlHttpRequestHead request = {0};
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlStatus status;

    route.method = sl_str_from_cstr("GET");
    route.pattern = sl_str_from_cstr("/ok");
    route.handler_id = 1U;
    route.bindings = &binding;
    route.binding_count = 1U;
    binding.kind = SL_PLAN_REQUEST_BINDING_QUERY;
    binding.name = sl_str_from_cstr("q");
    plan.routes = &route;
    plan.route_count = 1U;

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena, "GET /ok?q=%zz HTTP/1.1\r\nHost: example\r\n\r\n", &request) != 0 ||
        expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag), SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 172;
    }

    status = sl_http_dispatch_request_head(&arena, engine, &plan, &table.dispatch, &request,
                                           &result, &diag);
    sl_engine_destroy(engine);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
        result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_INVALID_HTTP_REQUEST)
    {
        return 173;
    }

    return 0;
}

static int test_plan_route_with_middleware_keeps_query_conservative(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlPlanHandler handler = {0};
    SlPlanRoute route = {0};
    SlPlanRequestBinding binding = {0};
    SlPlan plan = one_handler_plan(&handler);
    SlHttpRouteTable table = {0};
    SlHttpRequestHead request = {0};
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlStatus status;

    route.method = sl_str_from_cstr("GET");
    route.pattern = sl_str_from_cstr("/ok");
    route.handler_id = 1U;
    route.bindings = &binding;
    route.binding_count = 1U;
    binding.kind = SL_PLAN_REQUEST_BINDING_CONTEXT;
    binding.name = sl_str_from_cstr("RequestContext");
    plan.routes = &route;
    plan.route_count = 1U;

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena, "GET /ok?q=%zz HTTP/1.1\r\nHost: example\r\n\r\n", &request) != 0 ||
        expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag), SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 174;
    }

    status = sl_http_dispatch_request_head(&arena, engine, &plan, &table.dispatch, &request,
                                           &result, &diag);
    sl_engine_destroy(engine);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
        result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_INVALID_HTTP_REQUEST)
    {
        return 175;
    }

    return 0;
}

static int test_native_static_text_response_skips_engine(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlPlanHandler handler = {0};
    SlPlanRoute route = {0};
    SlPlan plan = one_handler_plan(&handler);
    SlHttpRouteTable table = {0};
    SlHttpRequestHead request = {0};
    SlEngineResult result = {0};
    SlDiag diag = {0};

    route.method = sl_str_from_cstr("GET");
    route.pattern = sl_str_from_cstr("/healthz");
    route.handler_id = 1U;
    route.native_response_kind = sl_str_from_cstr("text");
    route.native_response_status = 200U;
    route.native_response_body = sl_str_from_cstr("ok");
    route.native_response_content_type = sl_str_from_cstr("text/sloppy-test");
    plan.routes = &route;
    plan.route_count = 1U;

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena, "GET /healthz HTTP/1.1\r\nHost: example\r\n\r\n", &request) != 0 ||
        expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_dispatch_request_head(&arena, engine, &plan, &table.dispatch,
                                                    &request, &result, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 176;
    }

    sl_engine_destroy(engine);
    return expect_true(
        result.kind == SL_ENGINE_RESULT_TEXT &&
        result.payload_kind == SL_ENGINE_RESULT_PAYLOAD_RESPONSE &&
        result.response.status == 200U && expect_result_text_equal(&result, "ok") == 0 &&
        sl_str_equal(result.response.content_type, sl_str_from_cstr("text/sloppy-test")));
}

static void set_native_text_route(SlPlanRoute* route, const char* method, const char* pattern,
                                  SlHandlerId handler_id, const char* body)
{
    route->method = sl_str_from_cstr(method);
    route->pattern = sl_str_from_cstr(pattern);
    route->handler_id = handler_id;
    route->native_response_kind = sl_str_from_cstr("text");
    route->native_response_status = 200U;
    route->native_response_body = sl_str_from_cstr(body);
    route->native_response_content_type = sl_str_from_cstr("text/plain; charset=utf-8");
}

static int expect_validate_dispatch_agrees(SlPlanRoute* routes, size_t route_count,
                                           const char* request_text, SlStatusCode expected_status,
                                           SlDiagCode expected_diag, const char* expected_text)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char engine_storage[1024];
    SlArena arena = {0};
    SlArena engine_arena = {0};
    SlEngine* engine = NULL;
    SlPlanHandler handlers[8] = {0};
    SlPlan plan = {0};
    SlHttpRouteTable table = {0};
    SlHttpRequestHead request = {0};
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlStatus status;
    size_t index = 0U;
    int result_code = 0;

    for (index = 0U; index < route_count; index += 1U) {
        handlers[index].id = (SlHandlerId)(index + 1U);
        handlers[index].export_name = sl_str_from_cstr("__sloppy_handler");
    }
    plan.version = SL_PLAN_CURRENT_VERSION;
    plan.handlers = handlers;
    plan.handler_count = route_count;
    plan.routes = routes;
    plan.route_count = route_count;

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        create_noop_engine(&engine_arena, &engine) != 0 ||
        parse_request(&arena, request_text, &request) != 0 ||
        expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag), SL_STATUS_OK) != 0)
    {
        result_code = 2;
        goto cleanup;
    }
    table.dispatch.dispatch_mode = SL_HTTP_ROUTE_DISPATCH_MODE_VALIDATE;

    diag = (SlDiag){0};
    status = sl_http_dispatch_request_head(&arena, engine, &plan, &table.dispatch, &request,
                                           &result, &diag);
    if (sl_status_code(status) != expected_status || diag.code != expected_diag) {
        result_code = 4;
        goto cleanup;
    }
    if (expected_text != NULL) {
        if (expect_result_text_equal(&result, expected_text) != 0) {
            result_code = 5;
            goto cleanup;
        }
    }
    else if (result.kind != SL_ENGINE_RESULT_NONE) {
        result_code = 6;
        goto cleanup;
    }

cleanup:
    sl_engine_destroy(engine);
    return result_code;
}

static int test_compiled_and_classic_dispatch_agree_in_validate_mode(void)
{
    SlPlanRoute routes[4] = {0};

    set_native_text_route(&routes[0], "GET", "/users/{id}", 1U, "param");
    set_native_text_route(&routes[1], "GET", "/users/me", 2U, "static");
    if (expect_validate_dispatch_agrees(routes, 2U,
                                        "GET /users/me HTTP/1.1\r\nHost: example\r\n\r\n",
                                        SL_STATUS_OK, SL_DIAG_NONE, "static") != 0)
    {
        return 1;
    }

    memset(routes, 0, sizeof(routes));
    set_native_text_route(&routes[0], "GET", "/users/{slug}", 1U, "string");
    set_native_text_route(&routes[1], "GET", "/users/{id:int}", 2U, "int");
    if (expect_validate_dispatch_agrees(routes, 2U,
                                        "GET /users/42 HTTP/1.1\r\nHost: example\r\n\r\n",
                                        SL_STATUS_OK, SL_DIAG_NONE, "int") != 0 ||
        expect_validate_dispatch_agrees(routes, 2U,
                                        "GET /users/nope HTTP/1.1\r\nHost: example\r\n\r\n",
                                        SL_STATUS_OK, SL_DIAG_NONE, "string") != 0)
    {
        return 2;
    }

    memset(routes, 0, sizeof(routes));
    set_native_text_route(&routes[0], "GET", "/{tenant}/users/{id:int}", 1U, "tenant");
    if (expect_validate_dispatch_agrees(routes, 1U,
                                        "GET /acme/users/42 HTTP/1.1\r\nHost: example\r\n\r\n",
                                        SL_STATUS_OK, SL_DIAG_NONE, "tenant") != 0)
    {
        return 3;
    }

    memset(routes, 0, sizeof(routes));
    set_native_text_route(&routes[0], "GET", "/orgs/{org}/users/{id:int}", 1U, "user");
    set_native_text_route(&routes[1], "GET", "/orgs/{org}/settings", 2U, "settings");
    if (expect_validate_dispatch_agrees(routes, 2U,
                                        "GET /orgs/acme/settings HTTP/1.1\r\nHost: example\r\n\r\n",
                                        SL_STATUS_OK, SL_DIAG_NONE, "settings") != 0)
    {
        return 4;
    }

    memset(routes, 0, sizeof(routes));
    set_native_text_route(&routes[0], "POST", "/users/{id}", 1U, "post");
    if (expect_validate_dispatch_agrees(
            routes, 1U, "GET /users/42 HTTP/1.1\r\nHost: example\r\n\r\n", SL_STATUS_UNSUPPORTED,
            SL_DIAG_HTTP_METHOD_NOT_ALLOWED, NULL) != 0)
    {
        return 5;
    }

    memset(routes, 0, sizeof(routes));
    set_native_text_route(&routes[0], "GET", "/users/{id}", 1U, "user");
    if (expect_validate_dispatch_agrees(
            routes, 1U, "GET /users/42/ HTTP/1.1\r\nHost: example\r\n\r\n", SL_STATUS_OUT_OF_RANGE,
            SL_DIAG_HTTP_ROUTE_NOT_FOUND, NULL) != 0)
    {
        return 6;
    }

    memset(routes, 0, sizeof(routes));
    set_native_text_route(&routes[0], "GET", "/health", 1U, "ok");
    if (expect_validate_dispatch_agrees(routes, 1U,
                                        "HEAD /health HTTP/1.1\r\nHost: example\r\n\r\n",
                                        SL_STATUS_OK, SL_DIAG_NONE, "ok") != 0)
    {
        return 7;
    }

    return 0;
}

static int test_named_route_url_generation_validates_param_set(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlPlanHandler handler = {0};
    SlPlanRoute route = {0};
    SlPlan plan = one_handler_plan(&handler);
    SlHttpRouteTable table = {0};
    SlRouteParam params[3];
    SlRouteMatch match = {0};
    SlStr url = {0};
    SlDiag diag = {0};

    route.method = sl_str_from_cstr("GET");
    route.pattern = sl_str_from_cstr("/users/{id:int}/files/{file}");
    route.handler_id = 1U;
    route.name = sl_str_from_cstr("user.file");
    plan.routes = &route;
    plan.route_count = 1U;
    params[0] = (SlRouteParam){sl_str_from_cstr("id"), sl_str_from_cstr("42"), SL_ROUTE_PARAM_INT};
    params[1] =
        (SlRouteParam){sl_str_from_cstr("file"), sl_str_from_cstr("a b/c"), SL_ROUTE_PARAM_STRING};

    if (init_arena(&arena, storage, sizeof(storage)) != 0 ||
        expect_status(sl_http_route_table_build(&arena, &plan, &table, &diag), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_dispatch_generate_url(
                          &arena, &table.dispatch, sl_str_from_cstr("user.file"), params, 2U, &url),
                      SL_STATUS_OK) != 0 ||
        expect_str_equal(url, "/users/42/files/a%20b%2Fc") != 0 ||
        expect_status(sl_route_pattern_match(&arena, table.dispatch.routes[0].pattern, url, &match),
                      SL_STATUS_OK) != 0 ||
        !match.matched)
    {
        return 177;
    }

    if (expect_status(sl_http_dispatch_generate_url(
                          &arena, &table.dispatch, sl_str_from_cstr("user.file"), params, 1U, &url),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 178;
    }

    params[0].value = sl_str_from_cstr("abc");
    if (expect_status(sl_http_dispatch_generate_url(
                          &arena, &table.dispatch, sl_str_from_cstr("user.file"), params, 2U, &url),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 179;
    }

    params[0].value = sl_str_from_cstr("42");
    params[2] =
        (SlRouteParam){sl_str_from_cstr("extra"), sl_str_from_cstr("x"), SL_ROUTE_PARAM_STRING};
    if (expect_status(sl_http_dispatch_generate_url(
                          &arena, &table.dispatch, sl_str_from_cstr("user.file"), params, 3U, &url),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 180;
    }

    params[1] = (SlRouteParam){sl_str_from_cstr("id"), sl_str_from_cstr("43"), SL_ROUTE_PARAM_INT};
    if (expect_status(sl_http_dispatch_generate_url(
                          &arena, &table.dispatch, sl_str_from_cstr("user.file"), params, 2U, &url),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 181;
    }

    return 0;
}

typedef int (*HttpDispatchTestFn)(void);

typedef struct HttpDispatchTestCase
{
    HttpDispatchTestFn fn;
    const char* name;
} HttpDispatchTestCase;

#define HTTP_DISPATCH_TEST(fn) {fn, #fn}

int main(void)
{
    static const HttpDispatchTestCase tests[] = {
        HTTP_DISPATCH_TEST(test_get_missing_route_fails_cleanly),
        HTTP_DISPATCH_TEST(test_route_table_build_orders_literal_before_params),
        HTTP_DISPATCH_TEST(test_literal_route_wins_over_param_route_end_to_end),
        HTTP_DISPATCH_TEST(test_route_table_rejects_duplicate_method_pattern),
        HTTP_DISPATCH_TEST(test_route_table_rejects_duplicate_handler_ids),
        HTTP_DISPATCH_TEST(test_route_table_build_keeps_method_metadata),
        HTTP_DISPATCH_TEST(test_route_table_exact_index_reports_method_mismatch),
        HTTP_DISPATCH_TEST(test_route_table_param_buckets_preserve_specificity_order),
        HTTP_DISPATCH_TEST(test_route_table_orders_constrained_params_before_unconstrained),
        HTTP_DISPATCH_TEST(test_route_table_orders_mixed_position_literal_specificity),
        HTTP_DISPATCH_TEST(test_route_table_param_trie_matches_shared_prefix_and_constraints),
        HTTP_DISPATCH_TEST(test_route_dispatch_env_classic_and_validate_modes),
        HTTP_DISPATCH_TEST(test_route_table_param_buckets_report_method_mismatch),
        HTTP_DISPATCH_TEST(test_parameter_first_route_reaches_bucket),
        HTTP_DISPATCH_TEST(test_large_param_bucket_miss_uses_indexed_lookup),
        HTTP_DISPATCH_TEST(test_generated_options_route_reaches_engine),
        HTTP_DISPATCH_TEST(test_route_table_build_accepts_non_get_only_metadata),
        HTTP_DISPATCH_TEST(test_allow_header_lists_matching_methods_and_head_for_get),
        HTTP_DISPATCH_TEST(test_method_mismatch_returns_method_not_allowed),
        HTTP_DISPATCH_TEST(test_head_request_matches_get_route_before_engine),
        HTTP_DISPATCH_TEST(test_head_route_binding_is_rejected),
        HTTP_DISPATCH_TEST(test_supported_non_get_methods_reach_engine),
        HTTP_DISPATCH_TEST(test_transfer_encoding_body_is_rejected_before_handler_call),
        HTTP_DISPATCH_TEST(test_json_body_reaches_engine_when_valid),
        HTTP_DISPATCH_TEST(test_invalid_json_body_fails_before_handler_call),
        HTTP_DISPATCH_TEST(test_unsupported_body_content_type_fails_before_handler_call),
        HTTP_DISPATCH_TEST(test_non_empty_body_without_content_length_fails_before_handler_call),
        HTTP_DISPATCH_TEST(test_body_too_large_fails_before_handler_call),
        HTTP_DISPATCH_TEST(test_lifecycle_dispatch_uses_backend_body_limit),
        HTTP_DISPATCH_TEST(test_missing_plan_handler_fails_before_engine_call),
        HTTP_DISPATCH_TEST(test_stale_plan_route_table_ignores_cached_handler),
        HTTP_DISPATCH_TEST(test_plan_backed_body_validation_returns_problem_before_handler_call),
        HTTP_DISPATCH_TEST(
            test_plan_backed_json_body_rejects_wrong_content_type_before_handler_call),
        HTTP_DISPATCH_TEST(test_plan_backed_json_route_accepts_structured_json_content_type),
        HTTP_DISPATCH_TEST(test_native_json_body_exceeding_route_max_rejects_before_validation),
        HTTP_DISPATCH_TEST(test_route_json_max_overrides_higher_backend_body_limit),
        HTTP_DISPATCH_TEST(test_json_route_without_route_max_uses_backend_body_limit),
        HTTP_DISPATCH_TEST(test_generic_json_route_still_uses_json_media_and_size_policy),
        HTTP_DISPATCH_TEST(
            test_schema_json_route_rejects_empty_or_missing_body_before_handler_call),
        HTTP_DISPATCH_TEST(test_json_dispatch_mode_generic_matches_native_for_supported_schema),
        HTTP_DISPATCH_TEST(test_json_dispatch_validate_mode_matches_native_for_supported_route),
        HTTP_DISPATCH_TEST(test_plan_backed_route_query_header_validation_returns_problem),
        HTTP_DISPATCH_TEST(test_plan_backed_nullable_required_body_field_must_be_present),
        HTTP_DISPATCH_TEST(test_plan_backed_array_validation_reports_indexed_paths),
        HTTP_DISPATCH_TEST(test_plan_backed_body_validation_rejects_excessive_json_depth),
        HTTP_DISPATCH_TEST(test_plan_backed_body_validation_accepts_max_json_depth),
        HTTP_DISPATCH_TEST(test_manual_dispatch_ignores_stale_route_index_for_validation),
        HTTP_DISPATCH_TEST(test_route_params_may_match_but_are_not_required_by_dispatch),
        HTTP_DISPATCH_TEST(test_conformance_smoke_default_http_cases),
        HTTP_DISPATCH_TEST(test_plan_route_without_query_binding_skips_malformed_query),
        HTTP_DISPATCH_TEST(test_plan_route_with_query_binding_rejects_malformed_query),
        HTTP_DISPATCH_TEST(test_plan_route_with_middleware_keeps_query_conservative),
        HTTP_DISPATCH_TEST(test_native_static_text_response_skips_engine),
        HTTP_DISPATCH_TEST(test_compiled_and_classic_dispatch_agree_in_validate_mode),
        HTTP_DISPATCH_TEST(test_named_route_url_generation_validates_param_set),
    };
    size_t index = 0U;

    for (index = 0U; index < sizeof(tests) / sizeof(tests[0]); index += 1U) {
        fprintf(stderr, "running %s\n", tests[index].name);
        fflush(stderr);
        int result = tests[index].fn();
        if (result != 0) {
            fprintf(stderr, "%s failed with code %d\n", tests[index].name, result);
            return result;
        }
    }

    return 0;
}

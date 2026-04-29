#include "sloppy/http_dispatch.h"

#include <stdbool.h>
#include <stddef.h>

#define TEST_ARENA_SIZE 65536U

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static int expect_str_equal(SlStr actual, const char* expected)
{
    return expect_true(sl_str_equal(actual, sl_str_from_cstr(expected)));
}

static SlBytes bytes_from_cstr(const char* text)
{
    SlStr str = sl_str_from_cstr(text);
    return sl_bytes_from_parts((const unsigned char*)str.ptr, str.length);
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
        table.dispatch.routes[1].handler_id != 1U)
    {
        return 6;
    }

    return expect_true(diag.code == SL_DIAG_NONE);
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

static int test_non_get_fails_before_route_match(void)
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

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_HTTP_UNSUPPORTED_METHOD) {
        sl_engine_destroy(engine);
        return 12;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_request_body_is_rejected_before_handler_call(void)
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
        return 13;
    }

    headers[0].name = sl_str_from_cstr("Content-Length");
    headers[0].value = sl_str_from_cstr("1");
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

int main(void)
{
    int result = test_get_missing_route_fails_cleanly();
    if (result != 0) {
        return result;
    }

    result = test_route_table_build_orders_literal_before_params();
    if (result != 0) {
        return result;
    }

    result = test_route_table_rejects_duplicate_method_pattern();
    if (result != 0) {
        return result;
    }

    result = test_non_get_fails_before_route_match();
    if (result != 0) {
        return result;
    }

    result = test_request_body_is_rejected_before_handler_call();
    if (result != 0) {
        return result;
    }

    result = test_missing_plan_handler_fails_before_engine_call();
    if (result != 0) {
        return result;
    }

    return test_route_params_may_match_but_are_not_required_by_dispatch();
}

#include "bench_internal.h"

#include "sloppy/arena.h"
#include "sloppy/crypto.h"
#include "sloppy/engine.h"
#include "sloppy/http.h"
#include "sloppy/http_backend.h"
#include "sloppy/http_dispatch.h"
#include "sloppy/plan.h"
#include "sloppy/route.h"
#include "sloppy/route_artifact.h"
#include "sloppy/runtime_contract.h"
#include "sloppy/string.h"

#include <stdio.h>
#include <string.h>

static SlPlan sl_bench_plan(const SlPlanHandler* handlers, size_t handler_count)
{
    SlPlan plan = {0};
    plan.version = SL_PLAN_CURRENT_VERSION;
    plan.compiler_version = sl_str_from_cstr("bench");
    plan.runtime_min_version = sl_str_from_cstr(SL_PLAN_RUNTIME_MIN_VERSION_0_1_0);
    plan.stdlib_version = sl_str_from_cstr(SL_PLAN_STDLIB_VERSION_0_1_0);
    plan.target.platform = sl_str_from_cstr(SL_PLAN_TARGET_PLATFORM_WINDOWS_X64);
    plan.target.engine = sl_str_from_cstr("none");
    plan.bundle.path = sl_str_from_cstr("benchmarks/fixtures/dispatch-plan.json");
    plan.source_map.path = sl_str_from_cstr("benchmarks/fixtures/dispatch-plan.map.json");
    plan.handlers = handlers;
    plan.handler_count = handler_count;
    return plan;
}

static SlEngineOptions sl_bench_noop_options(void)
{
    SlEngineOptions options = {0};
    options.kind = SL_ENGINE_KIND_NONE;
    options.runtime_name = sl_str_from_cstr("sloppy-bench");
    options.runtime_version = sl_str_from_cstr("0.0.0");
    options.target_platform = sl_str_from_cstr(SL_PLAN_TARGET_PLATFORM_WINDOWS_X64);
    options.target_engine = sl_str_from_cstr("none");
    return options;
}

static SlStatus sl_bench_create_noop_engine(SlArena* arena, SlEngine** out_engine)
{
    SlEngineOptions options = sl_bench_noop_options();
    return sl_engine_create(&options, arena, out_engine);
}

static SlStatus sl_bench_format_route_path(char* buffer, size_t buffer_size, size_t route_index)
{
    int written = 0;

    if (buffer == NULL || buffer_size == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    written = snprintf(buffer, buffer_size, "/routes/%zu", route_index);
    if (written < 0 || (size_t)written >= buffer_size) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    return sl_status_ok();
}

static SlStatus sl_bench_format_param_route_path(char* buffer, size_t buffer_size,
                                                 size_t route_index)
{
    int written = 0;

    if (buffer == NULL || buffer_size == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    written = snprintf(buffer, buffer_size, "/orgs/{org}/teams/%zu/users/{id:int}", route_index);
    if (written < 0 || (size_t)written >= buffer_size) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    return sl_status_ok();
}

static SlStatus sl_bench_format_param_request_path(char* buffer, size_t buffer_size,
                                                   size_t route_index)
{
    int written = 0;

    if (buffer == NULL || buffer_size == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    written = snprintf(buffer, buffer_size, "/orgs/acme/teams/%zu/users/42", route_index);
    if (written < 0 || (size_t)written >= buffer_size) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    return sl_status_ok();
}

typedef enum SlBenchRouteDispatchMode
{
    SL_BENCH_ROUTE_DISPATCH_COMPILED,
    SL_BENCH_ROUTE_DISPATCH_CLASSIC,
    SL_BENCH_ROUTE_DISPATCH_VALIDATE
} SlBenchRouteDispatchMode;

typedef enum SlBenchDispatchExpectation
{
    SL_BENCH_EXPECT_NOOP,
    SL_BENCH_EXPECT_NATIVE_TEXT,
    SL_BENCH_EXPECT_MISSING,
    SL_BENCH_EXPECT_METHOD_MISMATCH
} SlBenchDispatchExpectation;

static SlHttpRouteDispatchMode sl_bench_route_dispatch_mode_value(SlBenchRouteDispatchMode mode)
{
    switch (mode) {
    case SL_BENCH_ROUTE_DISPATCH_COMPILED:
        return SL_HTTP_ROUTE_DISPATCH_MODE_COMPILED;
    case SL_BENCH_ROUTE_DISPATCH_CLASSIC:
        return SL_HTTP_ROUTE_DISPATCH_MODE_CLASSIC;
    case SL_BENCH_ROUTE_DISPATCH_VALIDATE:
        return SL_HTTP_ROUTE_DISPATCH_MODE_VALIDATE;
    }
    return SL_HTTP_ROUTE_DISPATCH_MODE_COMPILED;
}

static bool sl_bench_result_text_equal(const SlEngineResult* result, const char* expected)
{
    size_t expected_length = expected == NULL ? 0U : strlen(expected);

    if (result == NULL || expected == NULL || result->kind != SL_ENGINE_RESULT_TEXT) {
        return false;
    }
    if (result->payload_kind == SL_ENGINE_RESULT_PAYLOAD_TEXT) {
        return sl_str_equal(result->text, sl_str_from_cstr(expected));
    }
    if (result->payload_kind == SL_ENGINE_RESULT_PAYLOAD_RESPONSE) {
        return result->response.body.length == expected_length &&
               result->response.body.ptr != NULL &&
               memcmp(result->response.body.ptr, expected, expected_length) == 0;
    }
    return false;
}

static size_t sl_bench_result_body_length(const SlEngineResult* result)
{
    if (result == NULL) {
        return 0U;
    }
    if (result->payload_kind == SL_ENGINE_RESULT_PAYLOAD_TEXT) {
        return result->text.length;
    }
    if (result->payload_kind == SL_ENGINE_RESULT_PAYLOAD_RESPONSE) {
        return result->response.body.length;
    }
    return 0U;
}

static void sl_bench_set_native_text_route(SlPlanRoute* route, const char* method,
                                           const char* pattern, SlHandlerId handler_id,
                                           const char* body)
{
    *route = (SlPlanRoute){0};
    route->method = sl_str_from_cstr(method);
    route->pattern = sl_str_from_cstr(pattern);
    route->handler_id = handler_id;
    route->native_response_kind = sl_str_from_cstr("text");
    route->native_response_status = 200U;
    route->native_response_body = sl_str_from_cstr(body);
    route->native_response_content_type = sl_str_from_cstr("text/plain; charset=utf-8");
}

static SlStatus bench_plan_handler_lookup(const SlBenchContext* context, uint64_t iterations,
                                          uint64_t* out_checksum)
{
    SlPlanHandler handlers[] = {
        {1U, sl_str_from_cstr("index"), sl_str_from_cstr("GET /")},
        {2U, sl_str_from_cstr("getUser"), sl_str_from_cstr("GET /users/{id:int}")},
        {3U, sl_str_from_cstr("getPost"),
         sl_str_from_cstr("GET /users/{id:int}/posts/{postId:int}")},
        {4U, sl_str_from_cstr("health"), sl_str_from_cstr("GET /health")},
    };
    SlPlan plan = sl_bench_plan(handlers, sizeof(handlers) / sizeof(handlers[0]));
    uint64_t checksum = 0U;
    uint64_t index;

    (void)context;

    for (index = 0U; index < iterations; index += 1U) {
        const SlPlanHandler* handler = NULL;
        SlHandlerId id = (SlHandlerId)((index % 4U) + 1U);
        SlStatus status = sl_plan_find_handler_by_id(&plan, id, &handler);
        if (!sl_status_is_ok(status) || handler == NULL) {
            return status;
        }
        checksum += (uint64_t)handler->id;
        checksum += (uint64_t)handler->export_name.length;
    }

    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus bench_runtime_contract_noop_dispatch(const SlBenchContext* context,
                                                     uint64_t iterations, uint64_t* out_checksum)
{
    unsigned char engine_storage[1024];
    unsigned char result_storage[4096];
    SlArena engine_arena;
    SlArena result_arena;
    SlEngine* engine = NULL;
    SlPlanHandler handlers[] = {
        {1U, sl_str_from_cstr("handlerOne"), sl_str_from_cstr("GET /one")},
    };
    SlPlan plan = sl_bench_plan(handlers, sizeof(handlers) / sizeof(handlers[0]));
    uint64_t checksum = 0U;
    uint64_t index;
    SlStatus status;

    (void)context;

    status = sl_arena_init(&engine_arena, engine_storage, sizeof(engine_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_init(&result_arena, result_storage, sizeof(result_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_bench_create_noop_engine(&engine_arena, &engine);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    for (index = 0U; index < iterations; index += 1U) {
        SlEngineResult result = {0};
        sl_arena_reset(&result_arena);
        status = sl_runtime_contract_call_handler(engine, &result_arena, &plan, 1U, &result, NULL);
        if (sl_status_code(status) != SL_STATUS_UNSUPPORTED) {
            sl_engine_destroy(engine);
            return status;
        }
        checksum += (uint64_t)sl_status_code(status);
        checksum += (uint64_t)result.kind;
    }

    sl_engine_destroy(engine);
    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus bench_http_get_dispatch_noop(const SlBenchContext* context, uint64_t iterations,
                                             uint64_t* out_checksum)
{
    unsigned char engine_storage[1024];
    unsigned char route_storage[4096];
    unsigned char dispatch_storage[8192];
    SlArena engine_arena;
    SlArena route_arena;
    SlArena dispatch_arena;
    SlEngine* engine = NULL;
    SlRoutePattern pattern = {0};
    SlHttpRequestHead request = {0};
    SlHttpRouteBinding binding = {
        .method = SL_HTTP_METHOD_GET,
        .pattern = &pattern,
        .handler_id = 1U,
    };
    SlHttpDispatchTable table = {.routes = &binding, .route_count = 1U};
    SlPlanHandler handlers[] = {
        {1U, sl_str_from_cstr("getUserPost"),
         sl_str_from_cstr("GET /users/{id:int}/posts/{postId:int}")},
    };
    SlPlan plan = sl_bench_plan(handlers, sizeof(handlers) / sizeof(handlers[0]));
    uint64_t checksum = 0U;
    uint64_t index;
    SlStatus status;

    (void)context;

    status = sl_arena_init(&engine_arena, engine_storage, sizeof(engine_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_init(&route_arena, route_storage, sizeof(route_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_init(&dispatch_arena, dispatch_storage, sizeof(dispatch_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_bench_create_noop_engine(&engine_arena, &engine);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_route_pattern_parse(
        &route_arena, sl_str_from_cstr("/users/{id:int}/posts/{postId:int}"), &pattern, NULL);
    if (!sl_status_is_ok(status)) {
        sl_engine_destroy(engine);
        return status;
    }

    request.method = SL_HTTP_METHOD_GET;
    request.path = sl_str_from_cstr("/users/123/posts/456");
    request.raw_target = request.path;

    for (index = 0U; index < iterations; index += 1U) {
        SlEngineResult result = {0};
        sl_arena_reset(&dispatch_arena);
        status = sl_http_dispatch_request_head(&dispatch_arena, engine, &plan, &table, &request,
                                               &result, NULL);
        if (sl_status_code(status) != SL_STATUS_UNSUPPORTED) {
            sl_engine_destroy(engine);
            return status;
        }
        checksum += (uint64_t)sl_status_code(status);
        checksum += (uint64_t)result.kind;
    }

    sl_engine_destroy(engine);
    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus bench_route_table_param_dispatch_noop(const SlBenchContext* context,
                                                      uint64_t iterations, uint64_t* out_checksum)
{
    unsigned char engine_storage[1024];
    unsigned char route_storage[8192];
    unsigned char dispatch_storage[8192];
    SlArena engine_arena;
    SlArena route_arena;
    SlArena dispatch_arena;
    SlEngine* engine = NULL;
    SlPlanHandler handler = {1U, sl_str_from_cstr("getUserPost"),
                             sl_str_from_cstr("GET /users/{id:int}/posts/{postId:int}")};
    SlPlanRequestBinding bindings[] = {
        {.parameter = sl_str_from_cstr("id"),
         .name = sl_str_from_cstr("id"),
         .schema = sl_str_from_cstr("number"),
         .type = sl_str_from_cstr("Route<number>"),
         .kind = SL_PLAN_REQUEST_BINDING_ROUTE},
        {.parameter = sl_str_from_cstr("postId"),
         .name = sl_str_from_cstr("postId"),
         .schema = sl_str_from_cstr("number"),
         .type = sl_str_from_cstr("Route<number>"),
         .kind = SL_PLAN_REQUEST_BINDING_ROUTE},
    };
    SlPlanRoute route = {
        .method = sl_str_from_cstr("GET"),
        .pattern = sl_str_from_cstr("/users/{id:int}/posts/{postId:int}"),
        .handler_id = 1U,
        .bindings = bindings,
        .binding_count = sizeof(bindings) / sizeof(bindings[0]),
    };
    SlPlan plan = sl_bench_plan(&handler, 1U);
    SlHttpRouteTable table = {0};
    SlHttpRequestHead request = {0};
    uint64_t checksum = 0U;
    uint64_t index;
    SlStatus status;

    (void)context;

    plan.routes = &route;
    plan.route_count = 1U;

    status = sl_arena_init(&engine_arena, engine_storage, sizeof(engine_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_init(&route_arena, route_storage, sizeof(route_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_init(&dispatch_arena, dispatch_storage, sizeof(dispatch_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_bench_create_noop_engine(&engine_arena, &engine);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http_route_table_build(&route_arena, &plan, &table, NULL);
    if (!sl_status_is_ok(status)) {
        sl_engine_destroy(engine);
        return status;
    }

    request.method = SL_HTTP_METHOD_GET;
    request.path = sl_str_from_cstr("/users/123/posts/456");
    request.raw_target = request.path;

    for (index = 0U; index < iterations; index += 1U) {
        SlEngineResult result = {0};
        sl_arena_reset(&dispatch_arena);
        status = sl_http_dispatch_request_head(&dispatch_arena, engine, &plan, &table.dispatch,
                                               &request, &result, NULL);
        if (sl_status_code(status) != SL_STATUS_UNSUPPORTED) {
            sl_engine_destroy(engine);
            return status;
        }
        checksum += (uint64_t)sl_status_code(status);
        checksum += (uint64_t)result.kind;
    }

    sl_engine_destroy(engine);
    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus sl_bench_route_table_dispatch_loop(size_t route_count, size_t target_index,
                                                   SlHttpMethod request_method,
                                                   SlBenchRouteDispatchMode mode,
                                                   SlBenchDispatchExpectation expectation,
                                                   uint64_t iterations, uint64_t* out_checksum)
{
    enum
    {
        SL_BENCH_MAX_ROUTES = 10000U
    };
    static unsigned char engine_storage[1024];
    static unsigned char route_storage[8U * 1024U * 1024U];
    static unsigned char dispatch_storage[8192];
    static SlPlanRoute routes[SL_BENCH_MAX_ROUTES];
    static char pattern_storage[SL_BENCH_MAX_ROUTES][32];
    static SlHttpRouteTable cached_table;
    static size_t cached_route_count;
    static bool cached_ready;
    char request_path[32];
    SlArena engine_arena;
    SlArena route_arena;
    SlArena dispatch_arena;
    SlEngine* engine = NULL;
    static SlPlanHandler handler;
    SlPlan plan;
    SlHttpRequestHead request = {0};
    uint64_t checksum = 0U;
    uint64_t index;
    size_t route_index = 0U;
    bool rebuild_table = false;
    SlStatus status;

    if (route_count == 0U || route_count > SL_BENCH_MAX_ROUTES || out_checksum == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    handler = (SlPlanHandler){1U, sl_str_from_cstr("handlerOne"), sl_str_from_cstr("GET /routes")};
    plan = sl_bench_plan(&handler, 1U);

    status = sl_arena_init(&engine_arena, engine_storage, sizeof(engine_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_init(&dispatch_arena, dispatch_storage, sizeof(dispatch_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_bench_create_noop_engine(&engine_arena, &engine);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    rebuild_table = !cached_ready || cached_route_count != route_count;
    if (rebuild_table) {
        status = sl_arena_init(&route_arena, route_storage, sizeof(route_storage));
        if (!sl_status_is_ok(status)) {
            sl_engine_destroy(engine);
            return status;
        }
        for (route_index = 0U; route_index < route_count; route_index += 1U) {
            status = sl_bench_format_route_path(pattern_storage[route_index],
                                                sizeof(pattern_storage[route_index]), route_index);
            if (!sl_status_is_ok(status)) {
                sl_engine_destroy(engine);
                return status;
            }
            routes[route_index] = (SlPlanRoute){0};
            routes[route_index].method = sl_str_from_cstr("GET");
            routes[route_index].pattern = sl_str_from_cstr(pattern_storage[route_index]);
            routes[route_index].handler_id = 1U;
        }
        plan.routes = routes;
        plan.route_count = route_count;
        status = sl_http_route_table_build(&route_arena, &plan, &cached_table, NULL);
        if (!sl_status_is_ok(status)) {
            sl_engine_destroy(engine);
            return status;
        }
        cached_route_count = route_count;
        cached_ready = true;
    }
    cached_table.dispatch.dispatch_mode = sl_bench_route_dispatch_mode_value(mode);
    plan.routes = routes;
    plan.route_count = route_count;

    status = sl_bench_format_route_path(request_path, sizeof(request_path), target_index);
    if (!sl_status_is_ok(status)) {
        sl_engine_destroy(engine);
        return status;
    }
    request.method = request_method;
    request.path = sl_str_from_cstr(request_path);
    request.raw_target = request.path;

    for (index = 0U; index < iterations; index += 1U) {
        SlEngineResult result = {0};
        sl_arena_reset(&dispatch_arena);
        status = sl_http_dispatch_request_head(&dispatch_arena, engine, &plan,
                                               &cached_table.dispatch, &request, &result, NULL);
        if (expectation == SL_BENCH_EXPECT_MISSING) {
            if (sl_status_code(status) != SL_STATUS_OUT_OF_RANGE) {
                sl_engine_destroy(engine);
                return status;
            }
        }
        else if (expectation == SL_BENCH_EXPECT_METHOD_MISMATCH) {
            if (sl_status_code(status) != SL_STATUS_UNSUPPORTED) {
                sl_engine_destroy(engine);
                return status;
            }
        }
        else if (sl_status_code(status) != SL_STATUS_UNSUPPORTED) {
            sl_engine_destroy(engine);
            return status;
        }
        checksum += (uint64_t)sl_status_code(status);
        checksum += (uint64_t)result.kind;
    }

    sl_engine_destroy(engine);
    *out_checksum = checksum;
    return sl_status_ok();
}

#define SL_BENCH_ROUTE_TABLE_FN(name, count, target, method, mode, expectation)                    \
    static SlStatus name(const SlBenchContext* context, uint64_t iterations,                       \
                         uint64_t* out_checksum)                                                   \
    {                                                                                              \
        (void)context;                                                                             \
        return sl_bench_route_table_dispatch_loop((count), (target), (method), (mode),             \
                                                  (expectation), iterations, out_checksum);        \
    }

#define SL_BENCH_STATIC_SET(prefix, count, middle, last, mode)                                     \
    SL_BENCH_ROUTE_TABLE_FN(prefix##_first, count, 0U, SL_HTTP_METHOD_GET, mode,                   \
                            SL_BENCH_EXPECT_NOOP)                                                  \
    SL_BENCH_ROUTE_TABLE_FN(prefix##_middle, count, middle, SL_HTTP_METHOD_GET, mode,              \
                            SL_BENCH_EXPECT_NOOP)                                                  \
    SL_BENCH_ROUTE_TABLE_FN(prefix##_last, count, last, SL_HTTP_METHOD_GET, mode,                  \
                            SL_BENCH_EXPECT_NOOP)                                                  \
    SL_BENCH_ROUTE_TABLE_FN(prefix##_missing, count, (last) + 2U, SL_HTTP_METHOD_GET, mode,        \
                            SL_BENCH_EXPECT_MISSING)                                               \
    SL_BENCH_ROUTE_TABLE_FN(prefix##_method_mismatch, count, middle, SL_HTTP_METHOD_POST, mode,    \
                            SL_BENCH_EXPECT_METHOD_MISMATCH)

SL_BENCH_STATIC_SET(bench_route_table_10_compiled, 10U, 5U, 9U, SL_BENCH_ROUTE_DISPATCH_COMPILED)
SL_BENCH_STATIC_SET(bench_route_table_10_classic, 10U, 5U, 9U, SL_BENCH_ROUTE_DISPATCH_CLASSIC)
SL_BENCH_STATIC_SET(bench_route_table_10_validate, 10U, 5U, 9U, SL_BENCH_ROUTE_DISPATCH_VALIDATE)
SL_BENCH_STATIC_SET(bench_route_table_100_compiled, 100U, 50U, 99U,
                    SL_BENCH_ROUTE_DISPATCH_COMPILED)
SL_BENCH_STATIC_SET(bench_route_table_100_classic, 100U, 50U, 99U, SL_BENCH_ROUTE_DISPATCH_CLASSIC)
SL_BENCH_STATIC_SET(bench_route_table_100_validate, 100U, 50U, 99U,
                    SL_BENCH_ROUTE_DISPATCH_VALIDATE)
SL_BENCH_STATIC_SET(bench_route_table_1000_compiled, 1000U, 500U, 999U,
                    SL_BENCH_ROUTE_DISPATCH_COMPILED)
SL_BENCH_STATIC_SET(bench_route_table_1000_classic, 1000U, 500U, 999U,
                    SL_BENCH_ROUTE_DISPATCH_CLASSIC)
SL_BENCH_STATIC_SET(bench_route_table_1000_validate, 1000U, 500U, 999U,
                    SL_BENCH_ROUTE_DISPATCH_VALIDATE)
SL_BENCH_STATIC_SET(bench_route_table_10000_compiled, 10000U, 5000U, 9999U,
                    SL_BENCH_ROUTE_DISPATCH_COMPILED)
SL_BENCH_STATIC_SET(bench_route_table_10000_classic, 10000U, 5000U, 9999U,
                    SL_BENCH_ROUTE_DISPATCH_CLASSIC)
SL_BENCH_STATIC_SET(bench_route_table_10000_validate, 10000U, 5000U, 9999U,
                    SL_BENCH_ROUTE_DISPATCH_VALIDATE)

static SlStatus sl_bench_param_trie_dispatch_loop(SlBenchRouteDispatchMode mode,
                                                  const char* request_path,
                                                  SlBenchDispatchExpectation expectation,
                                                  const char* expected_text,
                                                  bool capture_route_params, uint64_t iterations,
                                                  uint64_t* out_checksum)
{
    static unsigned char engine_storage[1024];
    static unsigned char route_storage[65536];
    static unsigned char dispatch_storage[8192];
    static SlPlanRoute routes[8];
    static SlPlanRequestBinding route0_bindings[2];
    static SlHttpRouteTable cached_table;
    static bool cached_capture_route_params;
    static bool cached_ready;
    SlArena engine_arena;
    SlArena route_arena;
    SlArena dispatch_arena;
    SlEngine* engine = NULL;
    static SlPlanHandler handlers[8];
    SlPlan plan;
    SlHttpRequestHead request = {0};
    uint64_t checksum = 0U;
    uint64_t index;
    SlStatus status;

    if (request_path == NULL || out_checksum == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    handlers[0] = (SlPlanHandler){1U, sl_str_from_cstr("handlerOne"), sl_str_from_cstr("one")};
    handlers[1] = (SlPlanHandler){2U, sl_str_from_cstr("handlerTwo"), sl_str_from_cstr("two")};
    handlers[2] = (SlPlanHandler){3U, sl_str_from_cstr("handlerThree"), sl_str_from_cstr("three")};
    handlers[3] = (SlPlanHandler){4U, sl_str_from_cstr("handlerFour"), sl_str_from_cstr("four")};
    handlers[4] = (SlPlanHandler){5U, sl_str_from_cstr("handlerFive"), sl_str_from_cstr("five")};
    handlers[5] = (SlPlanHandler){6U, sl_str_from_cstr("handlerSix"), sl_str_from_cstr("six")};
    handlers[6] = (SlPlanHandler){7U, sl_str_from_cstr("handlerSeven"), sl_str_from_cstr("seven")};
    handlers[7] = (SlPlanHandler){8U, sl_str_from_cstr("handlerEight"), sl_str_from_cstr("eight")};
    plan = sl_bench_plan(handlers, sizeof(handlers) / sizeof(handlers[0]));
    plan.routes = routes;
    plan.route_count = sizeof(routes) / sizeof(routes[0]);

    status = sl_arena_init(&engine_arena, engine_storage, sizeof(engine_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_init(&dispatch_arena, dispatch_storage, sizeof(dispatch_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_bench_create_noop_engine(&engine_arena, &engine);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (!cached_ready || cached_capture_route_params != capture_route_params) {
        status = sl_arena_init(&route_arena, route_storage, sizeof(route_storage));
        if (!sl_status_is_ok(status)) {
            sl_engine_destroy(engine);
            return status;
        }
        sl_bench_set_native_text_route(&routes[0], "GET", "/orgs/{org}/users/{id:int}", 1U,
                                       "param-hit");
        if (capture_route_params) {
            route0_bindings[0] = (SlPlanRequestBinding){0};
            route0_bindings[0].name = sl_str_from_cstr("org");
            route0_bindings[0].schema = sl_str_from_cstr("string");
            route0_bindings[0].type = sl_str_from_cstr("Route<string>");
            route0_bindings[0].kind = SL_PLAN_REQUEST_BINDING_ROUTE;
            route0_bindings[1] = (SlPlanRequestBinding){0};
            route0_bindings[1].name = sl_str_from_cstr("id");
            route0_bindings[1].schema = sl_str_from_cstr("number");
            route0_bindings[1].type = sl_str_from_cstr("Route<number>");
            route0_bindings[1].kind = SL_PLAN_REQUEST_BINDING_ROUTE;
            routes[0].bindings = route0_bindings;
            routes[0].binding_count = sizeof(route0_bindings) / sizeof(route0_bindings[0]);
        }
        sl_bench_set_native_text_route(&routes[1], "GET", "/orgs/{org}/settings", 2U,
                                       "shared-prefix");
        sl_bench_set_native_text_route(&routes[2], "GET", "/{tenant}/users/{id:int}", 3U,
                                       "parameter-first");
        sl_bench_set_native_text_route(&routes[3], "GET", "/users/me", 4U, "static");
        sl_bench_set_native_text_route(&routes[4], "GET", "/users/{id:int}", 5U, "int");
        sl_bench_set_native_text_route(&routes[5], "GET", "/users/{name}", 6U, "string");
        sl_bench_set_native_text_route(&routes[6], "GET", "/files/{id:uuid}", 7U, "uuid");
        sl_bench_set_native_text_route(&routes[7], "POST", "/orgs/{org}/users/{id:int}", 8U,
                                       "post");
        status = sl_http_route_table_build(&route_arena, &plan, &cached_table, NULL);
        if (!sl_status_is_ok(status)) {
            sl_engine_destroy(engine);
            return status;
        }
        cached_capture_route_params = capture_route_params;
        cached_ready = true;
    }
    cached_table.dispatch.dispatch_mode = sl_bench_route_dispatch_mode_value(mode);

    request.method = SL_HTTP_METHOD_GET;
    request.path = sl_str_from_cstr(request_path);
    request.raw_target = request.path;

    for (index = 0U; index < iterations; index += 1U) {
        SlEngineResult result = {0};
        sl_arena_reset(&dispatch_arena);
        status = sl_http_dispatch_request_head(&dispatch_arena, engine, &plan,
                                               &cached_table.dispatch, &request, &result, NULL);
        if (expectation == SL_BENCH_EXPECT_MISSING) {
            if (sl_status_code(status) != SL_STATUS_OUT_OF_RANGE) {
                sl_engine_destroy(engine);
                return status;
            }
        }
        else {
            if (!sl_status_is_ok(status) || !sl_bench_result_text_equal(&result, expected_text)) {
                sl_engine_destroy(engine);
                return sl_status_from_code(SL_STATUS_INVALID_STATE);
            }
        }
        checksum += (uint64_t)sl_status_code(status);
        checksum += (uint64_t)result.kind;
        checksum += (uint64_t)sl_bench_result_body_length(&result);
    }

    sl_engine_destroy(engine);
    *out_checksum = checksum;
    return sl_status_ok();
}

#define SL_BENCH_PARAM_TRIE_FN(name, mode, path, expectation, expected_text, capture)              \
    static SlStatus name(const SlBenchContext* context, uint64_t iterations,                       \
                         uint64_t* out_checksum)                                                   \
    {                                                                                              \
        (void)context;                                                                             \
        return sl_bench_param_trie_dispatch_loop((mode), (path), (expectation), (expected_text),   \
                                                 (capture), iterations, out_checksum);             \
    }

SL_BENCH_PARAM_TRIE_FN(bench_param_hit_compiled, SL_BENCH_ROUTE_DISPATCH_COMPILED,
                       "/orgs/acme/users/42", SL_BENCH_EXPECT_NATIVE_TEXT, "param-hit", false)
SL_BENCH_PARAM_TRIE_FN(bench_param_hit_classic, SL_BENCH_ROUTE_DISPATCH_CLASSIC,
                       "/orgs/acme/users/42", SL_BENCH_EXPECT_NATIVE_TEXT, "param-hit", false)
SL_BENCH_PARAM_TRIE_FN(bench_param_hit_validate, SL_BENCH_ROUTE_DISPATCH_VALIDATE,
                       "/orgs/acme/users/42", SL_BENCH_EXPECT_NATIVE_TEXT, "param-hit", false)
SL_BENCH_PARAM_TRIE_FN(bench_param_hit_capture_compiled, SL_BENCH_ROUTE_DISPATCH_COMPILED,
                       "/orgs/acme/users/42", SL_BENCH_EXPECT_NATIVE_TEXT, "param-hit", true)
SL_BENCH_PARAM_TRIE_FN(bench_param_miss_compiled, SL_BENCH_ROUTE_DISPATCH_COMPILED,
                       "/orgs/acme/missing/42", SL_BENCH_EXPECT_MISSING, NULL, false)
SL_BENCH_PARAM_TRIE_FN(bench_param_miss_classic, SL_BENCH_ROUTE_DISPATCH_CLASSIC,
                       "/orgs/acme/missing/42", SL_BENCH_EXPECT_MISSING, NULL, false)
SL_BENCH_PARAM_TRIE_FN(bench_param_miss_validate, SL_BENCH_ROUTE_DISPATCH_VALIDATE,
                       "/orgs/acme/missing/42", SL_BENCH_EXPECT_MISSING, NULL, false)
SL_BENCH_PARAM_TRIE_FN(bench_constraint_miss_compiled, SL_BENCH_ROUTE_DISPATCH_COMPILED,
                       "/users/nope", SL_BENCH_EXPECT_NATIVE_TEXT, "string", false)
SL_BENCH_PARAM_TRIE_FN(bench_constraint_miss_classic, SL_BENCH_ROUTE_DISPATCH_CLASSIC,
                       "/users/nope", SL_BENCH_EXPECT_NATIVE_TEXT, "string", false)
SL_BENCH_PARAM_TRIE_FN(bench_constraint_miss_validate, SL_BENCH_ROUTE_DISPATCH_VALIDATE,
                       "/users/nope", SL_BENCH_EXPECT_NATIVE_TEXT, "string", false)
SL_BENCH_PARAM_TRIE_FN(bench_parameter_first_compiled, SL_BENCH_ROUTE_DISPATCH_COMPILED,
                       "/tenant-a/users/42", SL_BENCH_EXPECT_NATIVE_TEXT, "parameter-first", false)
SL_BENCH_PARAM_TRIE_FN(bench_parameter_first_classic, SL_BENCH_ROUTE_DISPATCH_CLASSIC,
                       "/tenant-a/users/42", SL_BENCH_EXPECT_NATIVE_TEXT, "parameter-first", false)
SL_BENCH_PARAM_TRIE_FN(bench_parameter_first_validate, SL_BENCH_ROUTE_DISPATCH_VALIDATE,
                       "/tenant-a/users/42", SL_BENCH_EXPECT_NATIVE_TEXT, "parameter-first", false)
SL_BENCH_PARAM_TRIE_FN(bench_static_vs_param_compiled, SL_BENCH_ROUTE_DISPATCH_COMPILED,
                       "/users/me", SL_BENCH_EXPECT_NATIVE_TEXT, "static", false)
SL_BENCH_PARAM_TRIE_FN(bench_static_vs_param_classic, SL_BENCH_ROUTE_DISPATCH_CLASSIC, "/users/me",
                       SL_BENCH_EXPECT_NATIVE_TEXT, "static", false)
SL_BENCH_PARAM_TRIE_FN(bench_static_vs_param_validate, SL_BENCH_ROUTE_DISPATCH_VALIDATE,
                       "/users/me", SL_BENCH_EXPECT_NATIVE_TEXT, "static", false)
SL_BENCH_PARAM_TRIE_FN(bench_constrained_vs_string_compiled, SL_BENCH_ROUTE_DISPATCH_COMPILED,
                       "/users/42", SL_BENCH_EXPECT_NATIVE_TEXT, "int", false)
SL_BENCH_PARAM_TRIE_FN(bench_constrained_vs_string_classic, SL_BENCH_ROUTE_DISPATCH_CLASSIC,
                       "/users/42", SL_BENCH_EXPECT_NATIVE_TEXT, "int", false)
SL_BENCH_PARAM_TRIE_FN(bench_constrained_vs_string_validate, SL_BENCH_ROUTE_DISPATCH_VALIDATE,
                       "/users/42", SL_BENCH_EXPECT_NATIVE_TEXT, "int", false)

static SlStatus sl_bench_param_heavy_dispatch_loop(size_t route_count, size_t target_index,
                                                   SlBenchRouteDispatchMode mode,
                                                   uint64_t iterations, uint64_t* out_checksum)
{
    enum
    {
        SL_BENCH_MAX_ROUTES = 10000U
    };
    static unsigned char engine_storage[1024];
    static unsigned char route_storage[64U * 1024U * 1024U];
    static unsigned char dispatch_storage[8192];
    static SlPlanRoute routes[SL_BENCH_MAX_ROUTES];
    static char pattern_storage[SL_BENCH_MAX_ROUTES][64];
    static SlHttpRouteTable cached_table;
    static size_t cached_route_count;
    static bool cached_ready;
    char request_path[64];
    SlArena engine_arena;
    SlArena route_arena;
    SlArena dispatch_arena;
    SlEngine* engine = NULL;
    static SlPlanHandler handler;
    SlPlan plan;
    SlHttpRequestHead request = {0};
    uint64_t checksum = 0U;
    uint64_t index;
    size_t route_index = 0U;
    SlStatus status;

    if (route_count == 0U || route_count > SL_BENCH_MAX_ROUTES || target_index >= route_count ||
        out_checksum == NULL)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    handler = (SlPlanHandler){1U, sl_str_from_cstr("handlerOne"), sl_str_from_cstr("GET /routes")};
    plan = sl_bench_plan(&handler, 1U);

    status = sl_arena_init(&engine_arena, engine_storage, sizeof(engine_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_init(&dispatch_arena, dispatch_storage, sizeof(dispatch_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_bench_create_noop_engine(&engine_arena, &engine);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (!cached_ready || cached_route_count != route_count) {
        status = sl_arena_init(&route_arena, route_storage, sizeof(route_storage));
        if (!sl_status_is_ok(status)) {
            sl_engine_destroy(engine);
            return status;
        }
        for (route_index = 0U; route_index < route_count; route_index += 1U) {
            status = sl_bench_format_param_route_path(
                pattern_storage[route_index], sizeof(pattern_storage[route_index]), route_index);
            if (!sl_status_is_ok(status)) {
                sl_engine_destroy(engine);
                return status;
            }
            sl_bench_set_native_text_route(&routes[route_index], "GET",
                                           pattern_storage[route_index], 1U, "param-heavy");
        }
        plan.routes = routes;
        plan.route_count = route_count;
        status = sl_http_route_table_build(&route_arena, &plan, &cached_table, NULL);
        if (!sl_status_is_ok(status)) {
            sl_engine_destroy(engine);
            return status;
        }
        cached_route_count = route_count;
        cached_ready = true;
    }
    cached_table.dispatch.dispatch_mode = sl_bench_route_dispatch_mode_value(mode);
    plan.routes = routes;
    plan.route_count = route_count;

    status = sl_bench_format_param_request_path(request_path, sizeof(request_path), target_index);
    if (!sl_status_is_ok(status)) {
        sl_engine_destroy(engine);
        return status;
    }
    request.method = SL_HTTP_METHOD_GET;
    request.path = sl_str_from_cstr(request_path);
    request.raw_target = request.path;

    for (index = 0U; index < iterations; index += 1U) {
        SlEngineResult result = {0};
        sl_arena_reset(&dispatch_arena);
        status = sl_http_dispatch_request_head(&dispatch_arena, engine, &plan,
                                               &cached_table.dispatch, &request, &result, NULL);
        if (!sl_status_is_ok(status) || !sl_bench_result_text_equal(&result, "param-heavy")) {
            sl_engine_destroy(engine);
            return sl_status_from_code(SL_STATUS_INVALID_STATE);
        }
        checksum += (uint64_t)result.kind;
        checksum += (uint64_t)sl_bench_result_body_length(&result);
    }

    sl_engine_destroy(engine);
    *out_checksum = checksum;
    return sl_status_ok();
}

#define SL_BENCH_PARAM_HEAVY_FN(name, count, target, mode)                                         \
    static SlStatus name(const SlBenchContext* context, uint64_t iterations,                       \
                         uint64_t* out_checksum)                                                   \
    {                                                                                              \
        (void)context;                                                                             \
        return sl_bench_param_heavy_dispatch_loop((count), (target), (mode), iterations,           \
                                                  out_checksum);                                   \
    }

SL_BENCH_PARAM_HEAVY_FN(bench_param_heavy_100_compiled_last, 100U, 99U,
                        SL_BENCH_ROUTE_DISPATCH_COMPILED)
SL_BENCH_PARAM_HEAVY_FN(bench_param_heavy_100_classic_last, 100U, 99U,
                        SL_BENCH_ROUTE_DISPATCH_CLASSIC)
SL_BENCH_PARAM_HEAVY_FN(bench_param_heavy_1000_compiled_last, 1000U, 999U,
                        SL_BENCH_ROUTE_DISPATCH_COMPILED)
SL_BENCH_PARAM_HEAVY_FN(bench_param_heavy_1000_classic_last, 1000U, 999U,
                        SL_BENCH_ROUTE_DISPATCH_CLASSIC)
SL_BENCH_PARAM_HEAVY_FN(bench_param_heavy_10000_compiled_last, 10000U, 9999U,
                        SL_BENCH_ROUTE_DISPATCH_COMPILED)
SL_BENCH_PARAM_HEAVY_FN(bench_param_heavy_10000_classic_last, 10000U, 9999U,
                        SL_BENCH_ROUTE_DISPATCH_CLASSIC)

static SlStatus sl_bench_native_response_loop(const char* kind, const char* body,
                                              uint64_t iterations, uint64_t* out_checksum)
{
    unsigned char engine_storage[1024];
    unsigned char dispatch_storage[8192];
    static unsigned char route_storage[8192];
    static SlPlanRoute route;
    static SlHttpRouteTable cached_table;
    static const char* cached_kind;
    static const char* cached_body;
    static bool cached_ready;
    SlArena engine_arena;
    SlArena route_arena;
    SlArena dispatch_arena;
    SlEngine* engine = NULL;
    static SlPlanHandler handler;
    SlPlan plan;
    SlHttpRequestHead request = {0};
    uint64_t checksum = 0U;
    uint64_t index;
    SlStatus status;

    if (kind == NULL || body == NULL || out_checksum == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    handler =
        (SlPlanHandler){1U, sl_str_from_cstr("nativeResponse"), sl_str_from_cstr("GET /native")};
    plan = sl_bench_plan(&handler, 1U);
    plan.routes = &route;
    plan.route_count = 1U;

    status = sl_arena_init(&engine_arena, engine_storage, sizeof(engine_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_init(&dispatch_arena, dispatch_storage, sizeof(dispatch_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_bench_create_noop_engine(&engine_arena, &engine);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (!cached_ready || strcmp(cached_kind, kind) != 0 || strcmp(cached_body, body) != 0) {
        status = sl_arena_init(&route_arena, route_storage, sizeof(route_storage));
        if (!sl_status_is_ok(status)) {
            sl_engine_destroy(engine);
            return status;
        }
        route.method = sl_str_from_cstr("GET");
        route.pattern = sl_str_from_cstr("/native");
        route.handler_id = 1U;
        route.native_response_kind = sl_str_from_cstr(kind);
        route.native_response_status = 200U;
        route.native_response_body = sl_str_from_cstr(body);
        route.native_response_content_type =
            sl_str_equal(route.native_response_kind, sl_str_from_cstr("json"))
                ? sl_str_from_cstr("application/json")
                : sl_str_from_cstr("text/plain; charset=utf-8");
        status = sl_http_route_table_build(&route_arena, &plan, &cached_table, NULL);
        if (!sl_status_is_ok(status)) {
            sl_engine_destroy(engine);
            return status;
        }
        cached_kind = kind;
        cached_body = body;
        cached_ready = true;
    }
    request.method = SL_HTTP_METHOD_GET;
    request.path = sl_str_from_cstr("/native");
    request.raw_target = request.path;

    for (index = 0U; index < iterations; index += 1U) {
        SlEngineResult result = {0};
        sl_arena_reset(&dispatch_arena);
        status = sl_http_dispatch_request_head(&dispatch_arena, engine, &plan,
                                               &cached_table.dispatch, &request, &result, NULL);
        if (!sl_status_is_ok(status) ||
            (result.kind != SL_ENGINE_RESULT_TEXT && result.kind != SL_ENGINE_RESULT_JSON) ||
            result.payload_kind != SL_ENGINE_RESULT_PAYLOAD_RESPONSE)
        {
            sl_engine_destroy(engine);
            return sl_status_from_code(SL_STATUS_INVALID_STATE);
        }
        checksum += (uint64_t)result.kind;
        checksum += (uint64_t)sl_bench_result_body_length(&result);
        checksum += (uint64_t)result.response.status;
    }

    sl_engine_destroy(engine);
    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus bench_native_response_text(const SlBenchContext* context, uint64_t iterations,
                                           uint64_t* out_checksum)
{
    (void)context;
    return sl_bench_native_response_loop("text", "hello", iterations, out_checksum);
}

static SlStatus bench_native_response_json(const SlBenchContext* context, uint64_t iterations,
                                           uint64_t* out_checksum)
{
    (void)context;
    return sl_bench_native_response_loop("json", "{\"ok\":true}", iterations, out_checksum);
}

static SlStatus bench_native_response_ok(const SlBenchContext* context, uint64_t iterations,
                                         uint64_t* out_checksum)
{
    (void)context;
    return sl_bench_native_response_loop("json", "{\"value\":1}", iterations, out_checksum);
}

static SlStatus bench_route_table_build_loop(size_t route_count, uint64_t iterations,
                                             uint64_t* out_checksum)
{
    enum
    {
        SL_BENCH_MAX_ROUTES = 10000U
    };
    static unsigned char route_storage[8U * 1024U * 1024U];
    static SlPlanRoute routes[SL_BENCH_MAX_ROUTES];
    static char pattern_storage[SL_BENCH_MAX_ROUTES][32];
    SlPlanHandler handler = {1U, sl_str_from_cstr("handlerOne"), sl_str_from_cstr("GET /routes")};
    SlPlan plan = sl_bench_plan(&handler, 1U);
    uint64_t checksum = 0U;
    uint64_t iteration;
    size_t route_index;
    SlStatus status;

    if (route_count == 0U || route_count > SL_BENCH_MAX_ROUTES || out_checksum == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    for (route_index = 0U; route_index < route_count; route_index += 1U) {
        status = sl_bench_format_route_path(pattern_storage[route_index],
                                            sizeof(pattern_storage[route_index]), route_index);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        routes[route_index] = (SlPlanRoute){0};
        routes[route_index].method = sl_str_from_cstr("GET");
        routes[route_index].pattern = sl_str_from_cstr(pattern_storage[route_index]);
        routes[route_index].handler_id = 1U;
    }
    plan.routes = routes;
    plan.route_count = route_count;

    for (iteration = 0U; iteration < iterations; iteration += 1U) {
        SlArena route_arena;
        SlHttpRouteTable table = {0};
        status = sl_arena_init(&route_arena, route_storage, sizeof(route_storage));
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_http_route_table_build(&route_arena, &plan, &table, NULL);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        checksum += (uint64_t)table.route_count;
        checksum += (uint64_t)table.dispatch.exact_route_bucket_count;
    }
    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus bench_route_table_build_param_loop(size_t route_count, uint64_t iterations,
                                                   uint64_t* out_checksum)
{
    enum
    {
        SL_BENCH_MAX_ROUTES = 10000U
    };
    static unsigned char route_storage[64U * 1024U * 1024U];
    static SlPlanRoute routes[SL_BENCH_MAX_ROUTES];
    static char pattern_storage[SL_BENCH_MAX_ROUTES][64];
    SlPlanHandler handler = {1U, sl_str_from_cstr("handlerOne"), sl_str_from_cstr("GET /routes")};
    SlPlan plan = sl_bench_plan(&handler, 1U);
    uint64_t checksum = 0U;
    uint64_t iteration;
    size_t route_index;
    SlStatus status;

    if (route_count == 0U || route_count > SL_BENCH_MAX_ROUTES || out_checksum == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    for (route_index = 0U; route_index < route_count; route_index += 1U) {
        status = sl_bench_format_param_route_path(
            pattern_storage[route_index], sizeof(pattern_storage[route_index]), route_index);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        routes[route_index] = (SlPlanRoute){0};
        routes[route_index].method = sl_str_from_cstr("GET");
        routes[route_index].pattern = sl_str_from_cstr(pattern_storage[route_index]);
        routes[route_index].handler_id = 1U;
    }
    plan.routes = routes;
    plan.route_count = route_count;

    for (iteration = 0U; iteration < iterations; iteration += 1U) {
        SlArena route_arena;
        SlHttpRouteTable table = {0};
        status = sl_arena_init(&route_arena, route_storage, sizeof(route_storage));
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_http_route_table_build(&route_arena, &plan, &table, NULL);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        checksum += (uint64_t)table.route_count;
        checksum += (uint64_t)table.dispatch.param_route_count;
        checksum += (uint64_t)table.dispatch.param_route_trie_node_count;
        checksum += (uint64_t)table.dispatch.param_route_trie_edge_count;
    }
    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus bench_route_table_build_param_100(const SlBenchContext* context,
                                                  uint64_t iterations, uint64_t* out_checksum)
{
    (void)context;
    return bench_route_table_build_param_loop(100U, iterations, out_checksum);
}

static SlStatus bench_route_table_build_param_1000(const SlBenchContext* context,
                                                   uint64_t iterations, uint64_t* out_checksum)
{
    (void)context;
    return bench_route_table_build_param_loop(1000U, iterations, out_checksum);
}

static SlStatus bench_route_table_build_param_10000(const SlBenchContext* context,
                                                    uint64_t iterations, uint64_t* out_checksum)
{
    (void)context;
    return bench_route_table_build_param_loop(10000U, iterations, out_checksum);
}

static SlStatus bench_route_table_build_1000(const SlBenchContext* context, uint64_t iterations,
                                             uint64_t* out_checksum)
{
    (void)context;
    return bench_route_table_build_loop(1000U, iterations, out_checksum);
}

static SlStatus bench_route_table_build_10000(const SlBenchContext* context, uint64_t iterations,
                                              uint64_t* out_checksum)
{
    (void)context;
    return bench_route_table_build_loop(10000U, iterations, out_checksum);
}

#define SL_BENCH_ROUTE_ARTIFACT_SIZE 128U
#define SL_BENCH_ROUTE_ARTIFACT_CHECKSUM_OFFSET 40U
#define SL_BENCH_ROUTE_ARTIFACT_HASH_LENGTH                                                        \
    ((sizeof("sha256:") - 1U) + ((size_t)SL_CRYPTO_SHA256_SIZE * 2U))
#define SL_BENCH_FNV_OFFSET_BASIS UINT64_C(0xcbf29ce484222325)
#define SL_BENCH_FNV_PRIME UINT64_C(0x00000100000001b3)

static void sl_bench_put_u32(unsigned char* bytes, size_t offset, uint32_t value)
{
    bytes[offset] = (unsigned char)(value & 0xffU);
    bytes[offset + 1U] = (unsigned char)((value >> 8U) & 0xffU);
    bytes[offset + 2U] = (unsigned char)((value >> 16U) & 0xffU);
    bytes[offset + 3U] = (unsigned char)((value >> 24U) & 0xffU);
}

static void sl_bench_put_u64(unsigned char* bytes, size_t offset, uint64_t value)
{
    size_t index = 0U;
    for (index = 0U; index < 8U; index += 1U) {
        bytes[offset + index] = (unsigned char)((value >> (index * 8U)) & 0xffU);
    }
}

static void sl_bench_copy_bytes(unsigned char* destination, const char* source, size_t length)
{
    size_t index = 0U;

    for (index = 0U; index < length; index += 1U) {
        destination[index] = (unsigned char)source[index];
    }
}

static uint64_t sl_bench_route_artifact_checksum(const unsigned char* bytes, size_t length)
{
    uint64_t hash = SL_BENCH_FNV_OFFSET_BASIS;
    size_t index = 0U;
    for (index = 0U; index < length; index += 1U) {
        unsigned char byte = bytes[index];
        if (index >= SL_BENCH_ROUTE_ARTIFACT_CHECKSUM_OFFSET &&
            index < SL_BENCH_ROUTE_ARTIFACT_CHECKSUM_OFFSET + 8U)
        {
            byte = 0U;
        }
        hash ^= (uint64_t)byte;
        hash *= SL_BENCH_FNV_PRIME;
    }
    return hash;
}

static SlBytes sl_bench_make_route_artifact(unsigned char* bytes)
{
    static const unsigned char magic[4] = {'S', 'L', 'R', 'T'};
    const char* pattern = "/health";
    const char* name = "health";
    size_t string_offset = 112U;

    memset(bytes, 0, SL_BENCH_ROUTE_ARTIFACT_SIZE);
    memcpy(bytes, magic, sizeof(magic));
    sl_bench_put_u32(bytes, 4U, SL_ROUTE_ARTIFACT_VERSION_1);
    sl_bench_put_u32(bytes, 8U, SL_ROUTE_ARTIFACT_ENDIAN_MARKER);
    sl_bench_put_u32(bytes, 12U, SL_ROUTE_ARTIFACT_HEADER_SIZE);
    sl_bench_put_u32(bytes, 16U, 1U);
    sl_bench_put_u32(bytes, 20U, 1U);
    sl_bench_put_u32(bytes, 24U, SL_ROUTE_ARTIFACT_HEADER_SIZE);
    sl_bench_put_u32(bytes, 28U, SL_ROUTE_ARTIFACT_ENTRY_SIZE);
    sl_bench_put_u32(bytes, 32U, (uint32_t)string_offset);
    sl_bench_put_u32(bytes, 36U, 13U);
    sl_bench_put_u32(bytes, 56U, (uint32_t)string_offset);
    sl_bench_put_u32(bytes, 60U, 13U);
    sl_bench_put_u32(bytes, 64U, 1U);
    sl_bench_put_u32(bytes, 68U, 1U);
    sl_bench_put_u32(bytes, 72U, 0U);
    sl_bench_put_u32(bytes, 76U, 7U);
    sl_bench_put_u32(bytes, 80U, 7U);
    sl_bench_put_u32(bytes, 84U, 6U);
    sl_bench_put_u32(bytes, 88U, 1U);
    sl_bench_put_u32(bytes, 92U, 1U);
    sl_bench_copy_bytes(bytes + string_offset, pattern, 7U);
    sl_bench_copy_bytes(bytes + string_offset + 7U, name, 6U);
    sl_bench_put_u64(bytes, SL_BENCH_ROUTE_ARTIFACT_CHECKSUM_OFFSET,
                     sl_bench_route_artifact_checksum(bytes, SL_BENCH_ROUTE_ARTIFACT_SIZE));
    return sl_bytes_from_parts(bytes, SL_BENCH_ROUTE_ARTIFACT_SIZE);
}

static SlStatus sl_bench_route_artifact_hash(SlBytes artifact, char* out_hash,
                                             size_t out_hash_length)
{
    unsigned char digest[SL_CRYPTO_SHA256_SIZE] = {0};
    SlStatus status;

    if (artifact.ptr == NULL || out_hash == NULL ||
        out_hash_length != SL_BENCH_ROUTE_ARTIFACT_HASH_LENGTH)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    memcpy(out_hash, "sha256:", sizeof("sha256:") - 1U);
    status =
        sl_crypto_hash(SL_CRYPTO_HASH_SHA256, artifact, (SlOwnedBytes){digest, sizeof(digest)});
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_crypto_hex_encode(sl_bytes_from_parts(digest, sizeof(digest)),
                                out_hash + sizeof("sha256:") - 1U,
                                out_hash_length - (sizeof("sha256:") - 1U));
}

static SlStatus bench_route_artifact_validate(const SlBenchContext* context, uint64_t iterations,
                                              uint64_t* out_checksum)
{
    unsigned char artifact_storage[SL_BENCH_ROUTE_ARTIFACT_SIZE];
    unsigned char arena_storage[4096];
    char expected_hash_storage[SL_BENCH_ROUTE_ARTIFACT_HASH_LENGTH];
    SlPlanHandler handler = {1U, sl_str_from_cstr("__sloppy_handler_1"),
                             sl_str_from_cstr("Health")};
    SlPlanRoute route = {
        .method = sl_str_from_cstr("GET"),
        .pattern = sl_str_from_cstr("/health"),
        .handler_id = 1U,
        .name = sl_str_from_cstr("health"),
    };
    SlPlan plan = sl_bench_plan(&handler, 1U);
    SlBytes artifact = sl_bench_make_route_artifact(artifact_storage);
    SlStr expected_hash = sl_str_from_parts(expected_hash_storage, sizeof(expected_hash_storage));
    uint64_t checksum = 0U;
    uint64_t index;
    SlStatus status;

    (void)context;

    if (out_checksum == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    plan.routes = &route;
    plan.route_count = 1U;
    status = sl_bench_route_artifact_hash(artifact, expected_hash_storage,
                                          sizeof(expected_hash_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }

    for (index = 0U; index < iterations; index += 1U) {
        SlArena arena;
        SlRouteArtifactSummary summary = {0};
        status = sl_arena_init(&arena, arena_storage, sizeof(arena_storage));
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_route_artifact_validate(&arena, artifact, expected_hash, &plan, &summary, NULL);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        checksum += (uint64_t)summary.route_count;
        checksum += (uint64_t)summary.string_table_size;
    }
    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus bench_http_body_reader_json_known_length(const SlBenchContext* context,
                                                         uint64_t iterations,
                                                         uint64_t* out_checksum)
{
    unsigned char arena_storage[8192];
    const char head_text[] = "POST /echo HTTP/1.1\r\nHost: example.test\r\n\r\n";
    const char first_text[] = "{\"bench\"";
    const char second_text[] = ":true}";
    SlArena arena;
    SlHttpBackend backend = {0};
    SlStr content_type = sl_str_from_cstr("application/json; charset=utf-8");
    SlBytes head = {0};
    SlBytes first = {0};
    SlBytes second = {0};
    uint64_t checksum = 0U;
    uint64_t index;
    SlStatus status;

    (void)context;

    head = sl_bytes_from_parts((const unsigned char*)head_text, sizeof(head_text) - 1U);
    first = sl_bytes_from_parts((const unsigned char*)first_text, sizeof(first_text) - 1U);
    second = sl_bytes_from_parts((const unsigned char*)second_text, sizeof(second_text) - 1U);

    status = sl_arena_init(&arena, arena_storage, sizeof(arena_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http_backend_init(&backend, NULL, NULL);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http_backend_start(&backend, NULL, NULL);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    for (index = 0U; index < iterations; index += 1U) {
        SlHttpConnection connection = {0};
        SlHttpRequestLifecycle request = {0};
        SlHttpBodyReader reader = {0};
        SlByteBuilderStats stats = {0};

        sl_arena_reset(&arena);
        status = sl_http_backend_accept_connection(&backend, &connection, NULL);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_http_request_begin(&connection, &arena, &request, NULL);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_http_request_parse_head(&request, head, NULL);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_http_request_body_reader_begin(&request, content_type, 14U, &reader, NULL);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_http_request_body_reader_append(&reader, first, NULL);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_http_request_body_reader_append(&reader, second, NULL);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_http_request_body_reader_finish(&reader, NULL);
        if (!sl_status_is_ok(status)) {
            return status;
        }

        stats = sl_byte_builder_stats(&reader.builder);
        checksum += (uint64_t)request.head.body.length;
        checksum += (uint64_t)stats.grow_count;
        checksum += (uint64_t)stats.copied_bytes;
        checksum += (uint64_t)stats.appended_bytes;

        status = sl_http_request_body_reader_close(&reader, NULL);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_http_request_close(&request, NULL);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_http_connection_close(&connection, NULL);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    *out_checksum = checksum;
    return sl_status_ok();
}

#define SL_BENCH_ROUTE_TABLE_ENTRY(count, mode, scenario, label, function_name, note)              \
    {"route.dispatch.generated_table." #count "." #mode "." #scenario,                             \
     "route",                                                                                      \
     label,                                                                                        \
     1000U,                                                                                        \
     100000U,                                                                                      \
     function_name,                                                                                \
     note "; dispatch_mode=" #mode,                                                                \
     false}

#define SL_BENCH_ROUTE_TABLE_ENTRIES(count, mode)                                                  \
    SL_BENCH_ROUTE_TABLE_ENTRY(                                                                    \
        count, mode, first, "dispatch first static route in a generated " #count "-route table",   \
        bench_route_table_##count##_##mode##_first, "route table is cached after warmup"),         \
        SL_BENCH_ROUTE_TABLE_ENTRY(                                                                \
            count, mode, middle,                                                                   \
            "dispatch middle static route in a generated " #count "-route table",                  \
            bench_route_table_##count##_##mode##_middle, "route table is cached after warmup"),    \
        SL_BENCH_ROUTE_TABLE_ENTRY(                                                                \
            count, mode, last, "dispatch last static route in a generated " #count "-route table", \
            bench_route_table_##count##_##mode##_last, "route table is cached after warmup"),      \
        SL_BENCH_ROUTE_TABLE_ENTRY(                                                                \
            count, mode, missing, "miss a static path in a generated " #count "-route table",      \
            bench_route_table_##count##_##mode##_missing, "route table is cached after warmup"),   \
        SL_BENCH_ROUTE_TABLE_ENTRY(count, mode, method_mismatch,                                   \
                                   "dispatch wrong method for a generated " #count "-route table", \
                                   bench_route_table_##count##_##mode##_method_mismatch,           \
                                   "route table is cached after warmup")

#define SL_BENCH_PARAM_TRIE_ENTRY(mode, scenario, label, function_name)                            \
    {"route.dispatch.param_trie." #mode "." #scenario,                                             \
     "route",                                                                                      \
     label,                                                                                        \
     1000U,                                                                                        \
     100000U,                                                                                      \
     function_name,                                                                                \
     "mixed parameter route table is cached after warmup; dispatch_mode=" #mode,                   \
     false}

#define SL_BENCH_PARAM_HEAVY_ENTRY(count, mode, function_name, measured_iterations)                \
    {"route.dispatch.param_heavy." #count "." #mode ".last",                                       \
     "route",                                                                                      \
     "dispatch last parameter route in a shared-prefix " #count "-route table",                    \
     100U,                                                                                         \
     measured_iterations,                                                                          \
     function_name,                                                                                \
     "parameter route table is cached after warmup; dispatch_mode=" #mode,                         \
     false}

static const SlBenchDefinition handler_definitions[] = {
    {"handler.plan.lookup", "handler", "lookup handler IDs in a borrowed Sloppy Plan table", 10000U,
     1000000U, bench_plan_handler_lookup, "non-V8 lookup only; no JavaScript enters this benchmark",
     false, 0U, 0U},
    {"handler.runtime_contract.noop_unsupported", "handler",
     "resolve handler export then hit the current noop engine boundary", 1000U, 100000U,
     bench_runtime_contract_noop_dispatch,
     "measures current non-V8 dispatch plumbing and expected unsupported engine result", false, 0U, 0U},
    {"http.dispatch.get.noop_unsupported", "handler",
     "synthetic parsed GET dispatch through route match, plan lookup, and noop engine", 1000U,
     100000U, bench_http_get_dispatch_noop,
     "not a server throughput benchmark; no sockets or response writer are involved", false, 0U, 0U},
    {"route.dispatch.generated_table.param", "route",
     "dispatch and capture params through a generated route table", 1000U, 100000U,
     bench_route_table_param_dispatch_noop,
     "generated route table is built before timing; noop engine result is expected", false},
    SL_BENCH_ROUTE_TABLE_ENTRIES(10, compiled),
    SL_BENCH_ROUTE_TABLE_ENTRIES(10, classic),
    SL_BENCH_ROUTE_TABLE_ENTRIES(10, validate),
    SL_BENCH_ROUTE_TABLE_ENTRIES(100, compiled),
    SL_BENCH_ROUTE_TABLE_ENTRIES(100, classic),
    SL_BENCH_ROUTE_TABLE_ENTRIES(100, validate),
    SL_BENCH_ROUTE_TABLE_ENTRIES(1000, compiled),
    SL_BENCH_ROUTE_TABLE_ENTRIES(1000, classic),
    SL_BENCH_ROUTE_TABLE_ENTRIES(1000, validate),
    SL_BENCH_ROUTE_TABLE_ENTRIES(10000, compiled),
    SL_BENCH_ROUTE_TABLE_ENTRIES(10000, classic),
    SL_BENCH_ROUTE_TABLE_ENTRIES(10000, validate),
    SL_BENCH_PARAM_TRIE_ENTRY(compiled, param_hit,
                              "dispatch parameter hit through shared-prefix route table",
                              bench_param_hit_compiled),
    SL_BENCH_PARAM_TRIE_ENTRY(compiled, param_hit_no_capture,
                              "dispatch parameter hit without route capture materialization",
                              bench_param_hit_compiled),
    SL_BENCH_PARAM_TRIE_ENTRY(compiled, param_hit_capture,
                              "dispatch parameter hit with route capture materialization",
                              bench_param_hit_capture_compiled),
    SL_BENCH_PARAM_TRIE_ENTRY(classic, param_hit,
                              "dispatch parameter hit through shared-prefix route table",
                              bench_param_hit_classic),
    SL_BENCH_PARAM_TRIE_ENTRY(validate, param_hit,
                              "dispatch parameter hit through shared-prefix route table",
                              bench_param_hit_validate),
    SL_BENCH_PARAM_TRIE_ENTRY(compiled, param_miss, "miss mixed parameter route table",
                              bench_param_miss_compiled),
    SL_BENCH_PARAM_TRIE_ENTRY(classic, param_miss, "miss mixed parameter route table",
                              bench_param_miss_classic),
    SL_BENCH_PARAM_TRIE_ENTRY(validate, param_miss, "miss mixed parameter route table",
                              bench_param_miss_validate),
    SL_BENCH_PARAM_TRIE_ENTRY(compiled, constraint_miss,
                              "reject constrained route then match string route",
                              bench_constraint_miss_compiled),
    SL_BENCH_PARAM_TRIE_ENTRY(classic, constraint_miss,
                              "reject constrained route then match string route",
                              bench_constraint_miss_classic),
    SL_BENCH_PARAM_TRIE_ENTRY(validate, constraint_miss,
                              "reject constrained route then match string route",
                              bench_constraint_miss_validate),
    SL_BENCH_PARAM_TRIE_ENTRY(compiled, parameter_first,
                              "dispatch parameter-first route with generic bucket",
                              bench_parameter_first_compiled),
    SL_BENCH_PARAM_TRIE_ENTRY(classic, parameter_first,
                              "dispatch parameter-first route with generic bucket",
                              bench_parameter_first_classic),
    SL_BENCH_PARAM_TRIE_ENTRY(validate, parameter_first,
                              "dispatch parameter-first route with generic bucket",
                              bench_parameter_first_validate),
    SL_BENCH_PARAM_TRIE_ENTRY(compiled, static_vs_param, "prefer static route over parameter route",
                              bench_static_vs_param_compiled),
    SL_BENCH_PARAM_TRIE_ENTRY(classic, static_vs_param, "prefer static route over parameter route",
                              bench_static_vs_param_classic),
    SL_BENCH_PARAM_TRIE_ENTRY(validate, static_vs_param, "prefer static route over parameter route",
                              bench_static_vs_param_validate),
    SL_BENCH_PARAM_TRIE_ENTRY(compiled, constrained_vs_string,
                              "prefer constrained parameter route over string parameter route",
                              bench_constrained_vs_string_compiled),
    SL_BENCH_PARAM_TRIE_ENTRY(classic, constrained_vs_string,
                              "prefer constrained parameter route over string parameter route",
                              bench_constrained_vs_string_classic),
    SL_BENCH_PARAM_TRIE_ENTRY(validate, constrained_vs_string,
                              "prefer constrained parameter route over string parameter route",
                              bench_constrained_vs_string_validate),
    SL_BENCH_PARAM_HEAVY_ENTRY(100, compiled, bench_param_heavy_100_compiled_last, 100000U),
    SL_BENCH_PARAM_HEAVY_ENTRY(100, classic, bench_param_heavy_100_classic_last, 100000U),
    SL_BENCH_PARAM_HEAVY_ENTRY(1000, compiled, bench_param_heavy_1000_compiled_last, 20000U),
    SL_BENCH_PARAM_HEAVY_ENTRY(1000, classic, bench_param_heavy_1000_classic_last, 20000U),
    SL_BENCH_PARAM_HEAVY_ENTRY(10000, compiled, bench_param_heavy_10000_compiled_last, 1000U),
    SL_BENCH_PARAM_HEAVY_ENTRY(10000, classic, bench_param_heavy_10000_classic_last, 1000U),
    {"route.dispatch.native_response.text", "route",
     "dispatch a native Results.text literal and construct the response", 1000U, 100000U,
     bench_native_response_text,
     "measures dispatch plus native response construction; no sockets or response writer", false},
    {"route.dispatch.native_response.json", "route",
     "dispatch a native Results.json literal and construct the response", 1000U, 100000U,
     bench_native_response_json,
     "measures dispatch plus native response construction; no sockets or response writer", false},
    {"route.dispatch.native_response.ok", "route",
     "dispatch a native Results.ok literal and construct the response", 1000U, 100000U,
     bench_native_response_ok,
     "measures dispatch plus native response construction; no sockets or response writer", false},
    {"route.dispatch.table_build.1000", "route",
     "build a route dispatch table from a 1000-route Plan", 100U, 1000U,
     bench_route_table_build_1000,
     "measures Plan-backed route table materialization before serving", false},
    {"route.dispatch.table_build.10000", "route",
     "build a route dispatch table from a 10000-route Plan", 10U, 100U,
     bench_route_table_build_10000,
     "measures Plan-backed route table materialization before serving", false},
    {"route.dispatch.table_build_param.100", "route",
     "build a route dispatch table from a 100-route shared-prefix parameter Plan", 100U, 1000U,
     bench_route_table_build_param_100,
     "measures Plan-backed parameter bucket and trie materialization before serving", false},
    {"route.dispatch.table_build_param.1000", "route",
     "build a route dispatch table from a 1000-route shared-prefix parameter Plan", 20U, 200U,
     bench_route_table_build_param_1000,
     "measures Plan-backed parameter bucket and trie materialization before serving", false},
    {"route.dispatch.table_build_param.10000", "route",
     "build a route dispatch table from a 10000-route shared-prefix parameter Plan", 1U, 3U,
     bench_route_table_build_param_10000,
     "measures Plan-backed parameter bucket and trie materialization before serving", false},
    {"route.dispatch.artifact_validate.1", "route",
     "validate a one-route routes.slrt artifact against Plan metadata", 1000U, 100000U,
     bench_route_artifact_validate,
     "artifactSizeBytes=128; routeCount=1; validates artifact contract but dispatch table still "
     "comes from Plan",
     false},
    {"http.body_reader.json_known_length", "handler",
     "bounded HTTP body reader with a declared JSON content length and chunked appends", 1000U,
     100000U, bench_http_body_reader_json_known_length,
     "tracks builder checksum counters for the known-length body reader optimization", false, 0U, 0U},
};

#undef SL_BENCH_PARAM_TRIE_ENTRY
#undef SL_BENCH_PARAM_HEAVY_ENTRY
#undef SL_BENCH_PARAM_HEAVY_FN
#undef SL_BENCH_ROUTE_TABLE_ENTRIES
#undef SL_BENCH_ROUTE_TABLE_ENTRY

const SlBenchDefinition* sl_bench_handler_dispatch_definitions(size_t* out_count)
{
    if (out_count != NULL) {
        *out_count = sizeof(handler_definitions) / sizeof(handler_definitions[0]);
    }

    return handler_definitions;
}

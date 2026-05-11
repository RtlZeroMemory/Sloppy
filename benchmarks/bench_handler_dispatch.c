#include "bench_internal.h"

#include "sloppy/arena.h"
#include "sloppy/engine.h"
#include "sloppy/http.h"
#include "sloppy/http_backend.h"
#include "sloppy/http_dispatch.h"
#include "sloppy/plan.h"
#include "sloppy/route.h"
#include "sloppy/runtime_contract.h"
#include "sloppy/string.h"

#include <stdio.h>

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
                                                   bool expect_missing, uint64_t iterations,
                                                   uint64_t* out_checksum)
{
    enum
    {
        SL_BENCH_MAX_ROUTES = 1000U
    };
    static unsigned char engine_storage[1024];
    static unsigned char route_storage[1048576];
    static unsigned char dispatch_storage[8192];
    static SlPlanRoute routes[SL_BENCH_MAX_ROUTES];
    static char pattern_storage[SL_BENCH_MAX_ROUTES][32];
    char request_path[32];
    SlArena engine_arena;
    SlArena route_arena;
    SlArena dispatch_arena;
    SlEngine* engine = NULL;
    SlPlanHandler handler = {1U, sl_str_from_cstr("handlerOne"), sl_str_from_cstr("GET /routes")};
    SlPlan plan = sl_bench_plan(&handler, 1U);
    SlHttpRouteTable table = {0};
    SlHttpRequestHead request = {0};
    uint64_t checksum = 0U;
    uint64_t index;
    size_t route_index = 0U;
    SlStatus status;

    if (route_count == 0U || route_count > SL_BENCH_MAX_ROUTES || out_checksum == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

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
    status = sl_http_route_table_build(&route_arena, &plan, &table, NULL);
    if (!sl_status_is_ok(status)) {
        sl_engine_destroy(engine);
        return status;
    }

    status = sl_bench_format_route_path(request_path, sizeof(request_path), target_index);
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
        status = sl_http_dispatch_request_head(&dispatch_arena, engine, &plan, &table.dispatch,
                                               &request, &result, NULL);
        if (expect_missing) {
            if (sl_status_code(status) != SL_STATUS_OUT_OF_RANGE) {
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

#define SL_BENCH_ROUTE_TABLE_FN(name, count, target, missing)                                      \
    static SlStatus name(const SlBenchContext* context, uint64_t iterations,                       \
                         uint64_t* out_checksum)                                                   \
    {                                                                                              \
        (void)context;                                                                             \
        return sl_bench_route_table_dispatch_loop((count), (target), (missing), iterations,        \
                                                  out_checksum);                                   \
    }

SL_BENCH_ROUTE_TABLE_FN(bench_route_table_10_first, 10U, 0U, false)
SL_BENCH_ROUTE_TABLE_FN(bench_route_table_10_middle, 10U, 5U, false)
SL_BENCH_ROUTE_TABLE_FN(bench_route_table_10_last, 10U, 9U, false)
SL_BENCH_ROUTE_TABLE_FN(bench_route_table_10_missing, 10U, 11U, true)
SL_BENCH_ROUTE_TABLE_FN(bench_route_table_100_first, 100U, 0U, false)
SL_BENCH_ROUTE_TABLE_FN(bench_route_table_100_middle, 100U, 50U, false)
SL_BENCH_ROUTE_TABLE_FN(bench_route_table_100_last, 100U, 99U, false)
SL_BENCH_ROUTE_TABLE_FN(bench_route_table_100_missing, 100U, 101U, true)
SL_BENCH_ROUTE_TABLE_FN(bench_route_table_1000_first, 1000U, 0U, false)
SL_BENCH_ROUTE_TABLE_FN(bench_route_table_1000_middle, 1000U, 500U, false)
SL_BENCH_ROUTE_TABLE_FN(bench_route_table_1000_last, 1000U, 999U, false)
SL_BENCH_ROUTE_TABLE_FN(bench_route_table_1000_missing, 1000U, 1001U, true)

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

static const SlBenchDefinition handler_definitions[] = {
    {"handler.plan.lookup", "handler", "lookup handler IDs in a borrowed Sloppy Plan table", 10000U,
     1000000U, bench_plan_handler_lookup, "non-V8 lookup only; no JavaScript enters this benchmark",
     false},
    {"handler.runtime_contract.noop_unsupported", "handler",
     "resolve handler export then hit the current noop engine boundary", 1000U, 100000U,
     bench_runtime_contract_noop_dispatch,
     "measures current non-V8 dispatch plumbing and expected unsupported engine result", false},
    {"http.dispatch.get.noop_unsupported", "handler",
     "synthetic parsed GET dispatch through route match, plan lookup, and noop engine", 1000U,
     100000U, bench_http_get_dispatch_noop,
     "not a server throughput benchmark; no sockets or response writer are involved", false},
    {"route.dispatch.generated_table.param", "route",
     "dispatch and capture params through a generated route table", 1000U, 100000U,
     bench_route_table_param_dispatch_noop,
     "generated route table is built before timing; noop engine result is expected", false},
    {"route.dispatch.generated_table.10.first", "route",
     "dispatch first static route in a generated 10-route table", 1000U, 100000U,
     bench_route_table_10_first, "generated route table is built before timing", false},
    {"route.dispatch.generated_table.10.middle", "route",
     "dispatch middle static route in a generated 10-route table", 1000U, 100000U,
     bench_route_table_10_middle, "generated route table is built before timing", false},
    {"route.dispatch.generated_table.10.last", "route",
     "dispatch last static route in a generated 10-route table", 1000U, 100000U,
     bench_route_table_10_last, "generated route table is built before timing", false},
    {"route.dispatch.generated_table.10.missing", "route",
     "miss a static path in a generated 10-route table", 1000U, 100000U,
     bench_route_table_10_missing, "generated route table is built before timing", false},
    {"route.dispatch.generated_table.100.first", "route",
     "dispatch first static route in a generated 100-route table", 1000U, 100000U,
     bench_route_table_100_first, "generated route table is built before timing", false},
    {"route.dispatch.generated_table.100.middle", "route",
     "dispatch middle static route in a generated 100-route table", 1000U, 100000U,
     bench_route_table_100_middle, "generated route table is built before timing", false},
    {"route.dispatch.generated_table.100.last", "route",
     "dispatch last static route in a generated 100-route table", 1000U, 100000U,
     bench_route_table_100_last, "generated route table is built before timing", false},
    {"route.dispatch.generated_table.100.missing", "route",
     "miss a static path in a generated 100-route table", 1000U, 100000U,
     bench_route_table_100_missing, "generated route table is built before timing", false},
    {"route.dispatch.generated_table.1000.first", "route",
     "dispatch first static route in a generated 1000-route table", 1000U, 100000U,
     bench_route_table_1000_first, "generated route table is built before timing", false},
    {"route.dispatch.generated_table.1000.middle", "route",
     "dispatch middle static route in a generated 1000-route table", 1000U, 100000U,
     bench_route_table_1000_middle, "generated route table is built before timing", false},
    {"route.dispatch.generated_table.1000.last", "route",
     "dispatch last static route in a generated 1000-route table", 1000U, 100000U,
     bench_route_table_1000_last, "generated route table is built before timing", false},
    {"route.dispatch.generated_table.1000.missing", "route",
     "miss a static path in a generated 1000-route table", 1000U, 100000U,
     bench_route_table_1000_missing, "generated route table is built before timing", false},
    {"http.body_reader.json_known_length", "handler",
     "bounded HTTP body reader with a declared JSON content length and chunked appends", 1000U,
     100000U, bench_http_body_reader_json_known_length,
     "tracks builder checksum counters for the known-length body reader optimization", false},
};

const SlBenchDefinition* sl_bench_handler_dispatch_definitions(size_t* out_count)
{
    if (out_count != NULL) {
        *out_count = sizeof(handler_definitions) / sizeof(handler_definitions[0]);
    }

    return handler_definitions;
}

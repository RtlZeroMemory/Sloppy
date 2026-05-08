#include "bench_internal.h"

#include "sloppy/arena.h"
#include "sloppy/engine.h"
#include "sloppy/http.h"
#include "sloppy/http_dispatch.h"
#include "sloppy/plan.h"
#include "sloppy/route.h"
#include "sloppy/runtime_contract.h"
#include "sloppy/string.h"

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
    SlHttpRouteBinding binding = {SL_HTTP_METHOD_GET, &pattern, 1U, 0U};
    SlHttpDispatchTable table = {&binding, 1U};
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
};

const SlBenchDefinition* sl_bench_handler_dispatch_definitions(size_t* out_count)
{
    if (out_count != NULL) {
        *out_count = sizeof(handler_definitions) / sizeof(handler_definitions[0]);
    }

    return handler_definitions;
}

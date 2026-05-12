#include "bench_internal.h"

#include "sloppy/arena.h"
#include "sloppy/engine.h"
#include "sloppy/features.h"
#include "sloppy/http.h"
#include "sloppy/http_dispatch.h"
#include "sloppy/plan.h"
#include "sloppy/route.h"
#include "sloppy/string.h"

#include <stdint.h>

#define SL_BENCH_V8_ENGINE_ARENA_SIZE (64U * 1024U)
#define SL_BENCH_V8_RESULT_ARENA_SIZE (128U * 1024U)

static SlPlan sl_bench_v8_plan(const SlPlanHandler* handlers, size_t handler_count)
{
    SlPlan plan = {0};

    plan.version = SL_PLAN_CURRENT_VERSION;
    plan.compiler_version = sl_str_from_cstr("bench");
    plan.runtime_min_version = sl_str_from_cstr(SL_PLAN_RUNTIME_MIN_VERSION_0_1_0);
    plan.stdlib_version = sl_str_from_cstr(SL_PLAN_STDLIB_VERSION_0_1_0);
    plan.target.platform = sl_str_from_cstr(SL_PLAN_TARGET_PLATFORM_WINDOWS_X64);
    plan.target.engine = sl_str_from_cstr(SL_PLAN_TARGET_ENGINE_V8);
    plan.bundle.path = sl_str_from_cstr("benchmarks/fixtures/v8-flow.plan.json");
    plan.source_map.path = sl_str_from_cstr("benchmarks/fixtures/v8-flow.map.json");
    plan.handlers = handlers;
    plan.handler_count = handler_count;
    return plan;
}

static void sl_bench_v8_feature_activate(SlRuntimeFeatureSet* features, SlRuntimeFeatureId id)
{
    if (features == NULL || id >= SL_RUNTIME_FEATURE_COUNT) {
        return;
    }

    features->active_mask |= ((uint32_t)1U << (uint32_t)id);
}

static SlRuntimeFeatureSet sl_bench_v8_core_features(void)
{
    SlRuntimeFeatureSet features = {0};

    sl_bench_v8_feature_activate(&features, SL_RUNTIME_FEATURE_CORE);
    sl_bench_v8_feature_activate(&features, SL_RUNTIME_FEATURE_V8);
    return features;
}

static SlRuntimeFeatureSet sl_bench_v8_app_http_features(void)
{
    SlRuntimeFeatureSet features = sl_bench_v8_core_features();

    sl_bench_v8_feature_activate(&features, SL_RUNTIME_FEATURE_HTTP);
    sl_bench_v8_feature_activate(&features, SL_RUNTIME_FEATURE_STDLIB_APP);
    sl_bench_v8_feature_activate(&features, SL_RUNTIME_FEATURE_STDLIB_RESULTS);
    return features;
}

static SlRuntimeFeatureSet sl_bench_v8_time_features(void)
{
    SlRuntimeFeatureSet features = sl_bench_v8_core_features();

    sl_bench_v8_feature_activate(&features, SL_RUNTIME_FEATURE_STDLIB_TIME);
    return features;
}

static SlRuntimeFeatureSet sl_bench_v8_crypto_features(void)
{
    SlRuntimeFeatureSet features = sl_bench_v8_core_features();

    sl_bench_v8_feature_activate(&features, SL_RUNTIME_FEATURE_STDLIB_CRYPTO);
    return features;
}

static SlEngineOptions sl_bench_v8_options(const SlRuntimeFeatureSet* features)
{
    SlEngineOptions options = {0};

    options.kind = SL_ENGINE_KIND_V8;
    options.runtime_name = sl_str_from_cstr("sloppy-v8-bridge-bench");
    options.runtime_version = sl_str_from_cstr("0.0.0-bench");
    options.target_platform = sl_str_from_cstr(SL_PLAN_TARGET_PLATFORM_WINDOWS_X64);
    options.target_engine = sl_str_from_cstr(SL_PLAN_TARGET_ENGINE_V8);
    options.runtime_features = features;
    return options;
}

static SlStatus sl_bench_v8_create(SlArena* arena, SlEngine** out_engine,
                                   const SlRuntimeFeatureSet* features)
{
    SlEngineOptions options = sl_bench_v8_options(features);

    return sl_engine_create(&options, arena, out_engine);
}

static SlStatus sl_bench_v8_eval(SlEngine* engine, const char* name, const char* source)
{
    return sl_engine_eval_source(engine, sl_str_from_cstr(name), sl_str_from_cstr(source), NULL);
}

static SlHttpRequestContext
sl_bench_v8_request_context(const SlHttpRequestHead* request, const SlRouteParam* route_params,
                            size_t route_param_count, const SlHttpQueryParam* query_params,
                            size_t query_param_count, SlHttpRequestBodyKind body_kind)
{
    SlHttpRequestContext context = {0};

    context.request = request;
    context.request_id = 42U;
    context.connection_id = 7U;
    context.scheme = sl_str_from_cstr("http");
    context.protocol = sl_str_from_cstr("HTTP/1.1");
    context.query_string = sl_str_from_cstr("q=abc");
    context.content_type = sl_str_from_cstr("application/json; charset=utf-8");
    context.content_length = request == NULL ? 0U : request->body.length;
    context.has_content_length = request != NULL && request->body.length != 0U;
    context.route_params = route_params;
    context.route_param_count = route_param_count;
    context.query_params = query_params;
    context.query_param_count = query_param_count;
    context.body_kind = body_kind;
    return context;
}

static SlStatus sl_bench_v8_startup_core_only(const SlBenchContext* context, uint64_t iterations,
                                              uint64_t* out_checksum)
{
    SlRuntimeFeatureSet features = sl_bench_v8_core_features();
    uint64_t checksum = 0U;
    uint64_t index;

    if (context == NULL || !context->include_v8) {
        return sl_status_from_code(SL_STATUS_UNSUPPORTED);
    }

    for (index = 0U; index < iterations; index += 1U) {
        unsigned char engine_storage[SL_BENCH_V8_ENGINE_ARENA_SIZE];
        SlArena engine_arena = {0};
        SlEngine* engine = NULL;
        SlStatus status = sl_arena_init(&engine_arena, engine_storage, sizeof(engine_storage));
        if (!sl_status_is_ok(status)) {
            return status;
        }

        status = sl_bench_v8_create(&engine_arena, &engine, &features);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        checksum += (uint64_t)SL_ENGINE_KIND_V8;
        sl_engine_destroy(engine);
    }

    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus sl_bench_v8_startup_app_http(const SlBenchContext* context, uint64_t iterations,
                                             uint64_t* out_checksum)
{
    SlRuntimeFeatureSet features = sl_bench_v8_app_http_features();
    uint64_t checksum = 0U;
    uint64_t index;

    if (context == NULL || !context->include_v8) {
        return sl_status_from_code(SL_STATUS_UNSUPPORTED);
    }

    for (index = 0U; index < iterations; index += 1U) {
        unsigned char engine_storage[SL_BENCH_V8_ENGINE_ARENA_SIZE];
        SlArena engine_arena = {0};
        SlEngine* engine = NULL;
        SlStatus status = sl_arena_init(&engine_arena, engine_storage, sizeof(engine_storage));
        if (!sl_status_is_ok(status)) {
            return status;
        }

        status = sl_bench_v8_create(&engine_arena, &engine, &features);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        checksum += (uint64_t)SL_ENGINE_KIND_V8;
        sl_engine_destroy(engine);
    }

    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus sl_bench_v8_eval_register_handlers(const SlBenchContext* context,
                                                   uint64_t iterations, uint64_t* out_checksum)
{
    SlRuntimeFeatureSet features = sl_bench_v8_app_http_features();
    uint64_t checksum = 0U;
    uint64_t index;

    if (context == NULL || !context->include_v8) {
        return sl_status_from_code(SL_STATUS_UNSUPPORTED);
    }

    for (index = 0U; index < iterations; index += 1U) {
        unsigned char engine_storage[SL_BENCH_V8_ENGINE_ARENA_SIZE];
        SlArena engine_arena = {0};
        SlEngine* engine = NULL;
        SlStatus status = sl_arena_init(&engine_arena, engine_storage, sizeof(engine_storage));
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_bench_v8_create(&engine_arena, &engine, &features);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status =
            sl_bench_v8_eval(engine, "bench-register.js",
                             "__sloppy_register_handler(1, function(ctx) { return 'one'; });"
                             "__sloppy_register_handler(2, function(ctx) { return 'two'; });"
                             "__sloppy_register_handler(3, function(ctx) { return 'three'; });"
                             "__sloppy_register_handler(4, function(ctx) { return 'four'; });");
        if (!sl_status_is_ok(status)) {
            sl_engine_destroy(engine);
            return status;
        }
        checksum += 4U;
        sl_engine_destroy(engine);
    }

    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus sl_bench_v8_call_function_source(const SlBenchContext* context, uint64_t iterations,
                                                 uint64_t* out_checksum,
                                                 const SlRuntimeFeatureSet* features,
                                                 const char* source_name, const char* source,
                                                 const char* function_name,
                                                 SlStatusCode expected_status)
{
    unsigned char engine_storage[SL_BENCH_V8_ENGINE_ARENA_SIZE];
    unsigned char result_storage[SL_BENCH_V8_RESULT_ARENA_SIZE];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngine* engine = NULL;
    uint64_t checksum = 0U;
    uint64_t index;
    SlStatus status;

    if (context == NULL || !context->include_v8) {
        return sl_status_from_code(SL_STATUS_UNSUPPORTED);
    }

    status = sl_arena_init(&engine_arena, engine_storage, sizeof(engine_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_init(&result_arena, result_storage, sizeof(result_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_bench_v8_create(&engine_arena, &engine, features);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_bench_v8_eval(engine, source_name, source);
    if (!sl_status_is_ok(status)) {
        sl_engine_destroy(engine);
        return status;
    }

    for (index = 0U; index < iterations; index += 1U) {
        SlEngineResult result = {0};
        sl_arena_reset(&result_arena);
        status = sl_engine_call_function0(engine, &result_arena, sl_str_from_cstr(function_name),
                                          &result, NULL);
        if (sl_status_code(status) != expected_status) {
            sl_engine_destroy(engine);
            return status;
        }
        checksum += (uint64_t)sl_status_code(status);
        checksum += (uint64_t)result.kind;
        checksum += (uint64_t)result.text.length;
        checksum += (uint64_t)result.response.body.length;
    }

    sl_engine_destroy(engine);
    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus sl_bench_v8_call_noop_proxy(const SlBenchContext* context, uint64_t iterations,
                                            uint64_t* out_checksum)
{
    SlRuntimeFeatureSet features = sl_bench_v8_core_features();

    return sl_bench_v8_call_function_source(
        context, iterations, out_checksum, &features, "bench-call-noop.js",
        "globalThis.bridge_noop = function() { return 'ok'; };", "bridge_noop", SL_STATUS_OK);
}

static SlStatus sl_bench_v8_result_primitive_json(const SlBenchContext* context,
                                                  uint64_t iterations, uint64_t* out_checksum)
{
    SlRuntimeFeatureSet features = sl_bench_v8_core_features();

    return sl_bench_v8_call_function_source(
        context, iterations, out_checksum, &features, "bench-result-json.js",
        "globalThis.bridge_result_json = function() { return { __sloppyResult: true, kind: "
        "'json', status: 200, contentType: 'application/json; charset=utf-8', body: { ok: true, "
        "count: 3 } }; };",
        "bridge_result_json", SL_STATUS_OK);
}

static SlStatus sl_bench_v8_result_string_utf8(const SlBenchContext* context, uint64_t iterations,
                                               uint64_t* out_checksum)
{
    SlRuntimeFeatureSet features = sl_bench_v8_core_features();

    return sl_bench_v8_call_function_source(
        context, iterations, out_checksum, &features, "bench-result-string.js",
        "globalThis.bridge_string = function() { return 'hello \\u03a9 bridge'; };",
        "bridge_string", SL_STATUS_OK);
}

static SlStatus sl_bench_v8_promise_resolve_text(const SlBenchContext* context, uint64_t iterations,
                                                 uint64_t* out_checksum)
{
    SlRuntimeFeatureSet features = sl_bench_v8_core_features();

    return sl_bench_v8_call_function_source(
        context, iterations, out_checksum, &features, "bench-promise-resolve.js",
        "globalThis.bridge_resolve = function() { return Promise.resolve('ok'); };",
        "bridge_resolve", SL_STATUS_OK);
}

static SlStatus sl_bench_v8_promise_native_delay_resolve(const SlBenchContext* context,
                                                         uint64_t iterations,
                                                         uint64_t* out_checksum)
{
    SlRuntimeFeatureSet features = sl_bench_v8_time_features();

    return sl_bench_v8_call_function_source(
        context, iterations, out_checksum, &features, "bench-promise-native-delay.js",
        "globalThis.bridge_native_delay = function() {"
        "  return __sloppy.time.delay(0).then(function() { return 'ok'; });"
        "};",
        "bridge_native_delay", SL_STATUS_OK);
}

static SlStatus sl_bench_v8_promise_reject_error(const SlBenchContext* context, uint64_t iterations,
                                                 uint64_t* out_checksum)
{
    SlRuntimeFeatureSet features = sl_bench_v8_core_features();

    return sl_bench_v8_call_function_source(
        context, iterations, out_checksum, &features, "bench-promise-reject.js",
        "globalThis.bridge_reject = function() { return Promise.reject(new Error('bench reject')); "
        "};",
        "bridge_reject", SL_STATUS_INVALID_STATE);
}

static SlStatus sl_bench_v8_invalid_result_header(const SlBenchContext* context,
                                                  uint64_t iterations, uint64_t* out_checksum)
{
    SlRuntimeFeatureSet features = sl_bench_v8_core_features();

    return sl_bench_v8_call_function_source(
        context, iterations, out_checksum, &features, "bench-invalid-header.js",
        "globalThis.bridge_invalid_header = function() { return { __sloppyResult: true, kind: "
        "'text', status: 200, contentType: 'text/plain; charset=utf-8', headers: { "
        "'Content-Length': '1' }, body: 'bad' }; };",
        "bridge_invalid_header", SL_STATUS_INVALID_STATE);
}

static SlStatus sl_bench_v8_native_time_noarg(const SlBenchContext* context, uint64_t iterations,
                                              uint64_t* out_checksum)
{
    SlRuntimeFeatureSet features = sl_bench_v8_time_features();

    return sl_bench_v8_call_function_source(
        context, iterations, out_checksum, &features, "bench-native-time.js",
        "globalThis.bridge_time = function() { return String(__sloppy.time.monotonicMs() >= 0); "
        "};",
        "bridge_time", SL_STATUS_OK);
}

static SlStatus sl_bench_v8_native_crypto_primitive(const SlBenchContext* context,
                                                    uint64_t iterations, uint64_t* out_checksum)
{
    SlRuntimeFeatureSet features = sl_bench_v8_crypto_features();

    return sl_bench_v8_call_function_source(
        context, iterations, out_checksum, &features, "bench-native-crypto-primitive.js",
        "globalThis.bridge_random_hex = function() { return __sloppy.crypto.randomHex(16); };",
        "bridge_random_hex", SL_STATUS_OK);
}

static SlStatus sl_bench_v8_native_crypto_bytes(const SlBenchContext* context, uint64_t iterations,
                                                uint64_t* out_checksum)
{
    SlRuntimeFeatureSet features = sl_bench_v8_crypto_features();

    return sl_bench_v8_call_function_source(
        context, iterations, out_checksum, &features, "bench-native-crypto-bytes.js",
        "const bridgeBytes = new Uint8Array([1, 2, 3, 4, 5, 6, 7, 8]);"
        "globalThis.bridge_crypto_bytes = function() { return "
        "__sloppy.crypto.nonCryptoXxHash64(bridgeBytes); };",
        "bridge_crypto_bytes", SL_STATUS_OK);
}

static SlStatus sl_bench_v8_native_crypto_string_bytes(const SlBenchContext* context,
                                                       uint64_t iterations, uint64_t* out_checksum)
{
    SlRuntimeFeatureSet features = sl_bench_v8_crypto_features();

    return sl_bench_v8_call_function_source(
        context, iterations, out_checksum, &features, "bench-native-crypto-string-bytes.js",
        "const bridgeHashBytes = new Uint8Array([1, 2, 3, 4, 5, 6, 7, 8]);"
        "globalThis.bridge_crypto_string_bytes = function() { return "
        "String(__sloppy.crypto.hash('sha256', bridgeHashBytes).length); };",
        "bridge_crypto_string_bytes", SL_STATUS_OK);
}

static SlStatus sl_bench_v8_context_source(const SlBenchContext* context, uint64_t iterations,
                                           uint64_t* out_checksum, const char* source_name,
                                           const char* source, const char* function_name,
                                           const SlHttpRequestContext* request_context)
{
    unsigned char engine_storage[SL_BENCH_V8_ENGINE_ARENA_SIZE];
    unsigned char result_storage[SL_BENCH_V8_RESULT_ARENA_SIZE];
    SlRuntimeFeatureSet features = sl_bench_v8_core_features();
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngine* engine = NULL;
    uint64_t checksum = 0U;
    uint64_t index;
    SlStatus status;

    if (context == NULL || !context->include_v8) {
        return sl_status_from_code(SL_STATUS_UNSUPPORTED);
    }

    status = sl_arena_init(&engine_arena, engine_storage, sizeof(engine_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_init(&result_arena, result_storage, sizeof(result_storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_bench_v8_create(&engine_arena, &engine, &features);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_bench_v8_eval(engine, source_name, source);
    if (!sl_status_is_ok(status)) {
        sl_engine_destroy(engine);
        return status;
    }

    for (index = 0U; index < iterations; index += 1U) {
        SlEngineResult result = {0};
        sl_arena_reset(&result_arena);
        status = sl_engine_call_function_with_context(
            engine, &result_arena, sl_str_from_cstr(function_name), request_context, &result, NULL);
        if (!sl_status_is_ok(status)) {
            sl_engine_destroy(engine);
            return status;
        }
        checksum += (uint64_t)result.kind;
        checksum += (uint64_t)result.text.length;
        checksum += (uint64_t)result.response.body.length;
    }

    sl_engine_destroy(engine);
    *out_checksum = checksum;
    return sl_status_ok();
}

static SlStatus sl_bench_v8_context_base(const SlBenchContext* context, uint64_t iterations,
                                         uint64_t* out_checksum)
{
    SlHttpRequestHead request = {0};
    SlRouteParam route_params[] = {
        {sl_str_from_cstr("id"), sl_str_from_cstr("123"), SL_ROUTE_PARAM_STRING},
    };
    SlHttpQueryParam query_params[] = {
        {sl_str_from_cstr("q"), sl_str_from_cstr("abc")},
    };
    SlHttpRequestContext request_context;

    request.method = SL_HTTP_METHOD_GET;
    request.path = sl_str_from_cstr("/users/123");
    request.raw_target = sl_str_from_cstr("/users/123?q=abc");
    request_context = sl_bench_v8_request_context(&request, route_params, 1U, query_params, 1U,
                                                  SL_HTTP_REQUEST_BODY_NONE);
    request_context.needs_request = true;
    request_context.needs_route_params = true;
    request_context.needs_query_params = true;

    return sl_bench_v8_context_source(
        context, iterations, out_checksum, "bench-context-base.js",
        "globalThis.bridge_context_base = function(ctx) { return ctx.request.method + ':' + "
        "ctx.route.id + ':' + ctx.query.q + ':' + ctx.request.path; };",
        "bridge_context_base", &request_context);
}

static SlStatus sl_bench_v8_context_headers_get(const SlBenchContext* context, uint64_t iterations,
                                                uint64_t* out_checksum)
{
    SlHttpHeader headers[] = {
        {sl_str_from_cstr("Host"), sl_str_from_cstr("example")},
        {sl_str_from_cstr("Content-Type"), sl_str_from_cstr("application/json; charset=utf-8")},
        {sl_str_from_cstr("X-Trace"), sl_str_from_cstr("one")},
        {sl_str_from_cstr("x-trace"), sl_str_from_cstr("two")},
    };
    SlHttpRequestHead request = {0};
    SlHttpRequestContext request_context;

    request.method = SL_HTTP_METHOD_GET;
    request.path = sl_str_from_cstr("/headers");
    request.raw_target = request.path;
    request.headers = headers;
    request.header_count = sizeof(headers) / sizeof(headers[0]);
    request_context =
        sl_bench_v8_request_context(&request, NULL, 0U, NULL, 0U, SL_HTTP_REQUEST_BODY_NONE);
    request_context.needs_request = true;
    request_context.needs_headers = true;

    return sl_bench_v8_context_source(
        context, iterations, out_checksum, "bench-context-headers-get.js",
        "globalThis.bridge_headers_get = function(ctx) { return "
        "ctx.request.headers.get('X-Trace') + ':' + ctx.request.headers.get('content-type'); };",
        "bridge_headers_get", &request_context);
}

static SlStatus sl_bench_v8_context_headers_entries(const SlBenchContext* context,
                                                    uint64_t iterations, uint64_t* out_checksum)
{
    SlHttpHeader headers[] = {
        {sl_str_from_cstr("Host"), sl_str_from_cstr("example")},
        {sl_str_from_cstr("Content-Type"), sl_str_from_cstr("application/json; charset=utf-8")},
        {sl_str_from_cstr("X-Trace"), sl_str_from_cstr("one")},
        {sl_str_from_cstr("x-trace"), sl_str_from_cstr("two")},
    };
    SlHttpRequestHead request = {0};
    SlHttpRequestContext request_context;

    request.method = SL_HTTP_METHOD_GET;
    request.path = sl_str_from_cstr("/headers");
    request.raw_target = request.path;
    request.headers = headers;
    request.header_count = sizeof(headers) / sizeof(headers[0]);
    request_context =
        sl_bench_v8_request_context(&request, NULL, 0U, NULL, 0U, SL_HTTP_REQUEST_BODY_NONE);
    request_context.needs_request = true;
    request_context.needs_headers = true;

    return sl_bench_v8_context_source(context, iterations, out_checksum,
                                      "bench-context-headers-entries.js",
                                      "globalThis.bridge_headers_entries = function(ctx) { return "
                                      "String(ctx.request.headers.entries().length); };",
                                      "bridge_headers_entries", &request_context);
}

static SlStatus sl_bench_v8_context_body_text_json(const SlBenchContext* context,
                                                   uint64_t iterations, uint64_t* out_checksum)
{
    static const unsigned char body[] = "{\"id\":123,\"name\":\"bench\"}";
    SlHttpRequestHead request = {0};
    SlHttpRequestContext request_context;

    request.method = SL_HTTP_METHOD_POST;
    request.path = sl_str_from_cstr("/body");
    request.raw_target = request.path;
    request.body = sl_bytes_from_parts(body, sizeof(body) - 1U);
    request_context =
        sl_bench_v8_request_context(&request, NULL, 0U, NULL, 0U, SL_HTTP_REQUEST_BODY_JSON);
    request_context.needs_request = true;
    request_context.needs_body = true;

    return sl_bench_v8_context_source(
        context, iterations, out_checksum, "bench-context-body-text-json.js",
        "globalThis.bridge_body_text_json = function(ctx) { const text = ctx.request.text(); "
        "return text.length + ':' + ctx.request.json().id; };",
        "bridge_body_text_json", &request_context);
}

static SlStatus sl_bench_v8_context_body_bytes(const SlBenchContext* context, uint64_t iterations,
                                               uint64_t* out_checksum)
{
    static const unsigned char body[] = {0U, 65U, 66U, 0U, 255U};
    SlHttpRequestHead request = {0};
    SlHttpRequestContext request_context;

    request.method = SL_HTTP_METHOD_POST;
    request.path = sl_str_from_cstr("/bytes");
    request.raw_target = request.path;
    request.body = sl_bytes_from_parts(body, sizeof(body));
    request_context =
        sl_bench_v8_request_context(&request, NULL, 0U, NULL, 0U, SL_HTTP_REQUEST_BODY_BYTES);
    request_context.content_type = sl_str_from_cstr("application/octet-stream");
    request_context.needs_request = true;
    request_context.needs_body = true;

    return sl_bench_v8_context_source(
        context, iterations, out_checksum, "bench-context-body-bytes.js",
        "globalThis.bridge_body_bytes = function(ctx) { const bytes = ctx.request.body.bytes(); "
        "return String(bytes.length) + ':' + bytes[0] + ':' + bytes[3] + ':' + bytes[4]; };",
        "bridge_body_bytes", &request_context);
}

static SlStatus sl_bench_v8_flow_http_dispatch_json(const SlBenchContext* context,
                                                    uint64_t iterations, uint64_t* out_checksum)
{
    static const char request_text[] = "POST /bench?q=abc HTTP/1.1\r\n"
                                       "Host: localhost\r\n"
                                       "Content-Type: application/json\r\n"
                                       "X-Trace: abcdefgh\r\n"
                                       "Content-Length: 27\r\n"
                                       "\r\n"
                                       "{\"name\":\"sloppy\",\"count\":3}";
    unsigned char engine_storage[SL_BENCH_V8_ENGINE_ARENA_SIZE];
    unsigned char route_storage[4096];
    unsigned char dispatch_storage[SL_BENCH_V8_RESULT_ARENA_SIZE];
    SlRuntimeFeatureSet features = sl_bench_v8_app_http_features();
    SlArena engine_arena = {0};
    SlArena route_arena = {0};
    SlArena dispatch_arena = {0};
    SlEngine* engine = NULL;
    SlRoutePattern pattern = {0};
    SlHttpRouteBinding binding = {
        .method = SL_HTTP_METHOD_POST,
        .pattern = &pattern,
        .handler_id = 1U,
    };
    SlHttpDispatchTable table = {.routes = &binding, .route_count = 1U};
    SlPlanHandler handlers[] = {
        {1U, sl_str_from_cstr("benchHttp"), sl_str_from_cstr("POST /bench")},
    };
    SlPlan plan = sl_bench_v8_plan(handlers, sizeof(handlers) / sizeof(handlers[0]));
    uint64_t checksum = 0U;
    uint64_t index;
    SlStatus status;

    if (context == NULL || !context->include_v8) {
        return sl_status_from_code(SL_STATUS_UNSUPPORTED);
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
    status = sl_route_pattern_parse(&route_arena, sl_str_from_cstr("/bench"), &pattern, NULL);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_bench_v8_create(&engine_arena, &engine, &features);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_bench_v8_eval(engine, "bench-flow-http-dispatch.js",
                              "__sloppy_register_handler(1, function(ctx) {"
                              "  const body = ctx.request.body.json();"
                              "  const trace = ctx.request.headers.get('x-trace');"
                              "  return { __sloppyResult: true, kind: 'json', status: 200,"
                              "    contentType: 'application/json; charset=utf-8',"
                              "    body: { method: ctx.request.method, path: ctx.request.path,"
                              "      query: ctx.request.queryString, traceLength: trace.length,"
                              "      name: body.name, count: body.count } };"
                              "});");
    if (!sl_status_is_ok(status)) {
        sl_engine_destroy(engine);
        return status;
    }

    for (index = 0U; index < iterations; index += 1U) {
        SlHttpRequestHead request = {0};
        SlEngineResult result = {0};

        sl_arena_reset(&dispatch_arena);
        status = sl_http_parse_request_head(
            &dispatch_arena,
            sl_bytes_from_parts((const unsigned char*)request_text, sizeof(request_text) - 1U),
            NULL, &request, NULL);
        if (!sl_status_is_ok(status)) {
            sl_engine_destroy(engine);
            return status;
        }
        status = sl_http_dispatch_request_head(&dispatch_arena, engine, &plan, &table, &request,
                                               &result, NULL);
        if (!sl_status_is_ok(status) || result.kind != SL_ENGINE_RESULT_JSON) {
            sl_engine_destroy(engine);
            return sl_status_is_ok(status) ? sl_status_from_code(SL_STATUS_INVALID_STATE) : status;
        }
        checksum += (uint64_t)result.kind;
        checksum += (uint64_t)result.response.status;
        checksum += (uint64_t)result.response.body.length;
    }

    sl_engine_destroy(engine);
    *out_checksum = checksum;
    return sl_status_ok();
}

static const SlBenchDefinition v8_bridge_definitions[] = {
    {"v8.startup.create.core_only", "v8-bridge",
     "create and destroy a V8 engine with the minimal explicit feature set", 5U, 50U,
     sl_bench_v8_startup_core_only,
     "internal startup evidence only; includes isolate/context and root intrinsic setup", true, 0U,
     0U, 0U},
    {"v8.startup.create.app_http", "v8-bridge",
     "create and destroy a V8 engine with app/http/result feature activation", 5U, 50U,
     sl_bench_v8_startup_app_http,
     "internal startup evidence only; not request throughput or public performance data", true, 0U,
     0U, 0U},
    {"v8.bridge.eval.register_handlers", "v8-bridge",
     "evaluate JS that calls the native handler-registration intrinsic", 5U, 50U,
     sl_bench_v8_eval_register_handlers,
     "measures JS-to-native registration during source evaluation; handlers are recreated per "
     "iteration",
     true, 0U, 0U, 0U},
    {"v8.bridge.call.noop_proxy", "v8-bridge",
     "call a zero-argument JS function through the engine ABI and copy a tiny text result", 1000U,
     10000U, sl_bench_v8_call_noop_proxy,
     "public-ABI proxy for handler-call overhead; not a pure V8 Fast API benchmark", true, 0U, 0U,
     0U},
    {"v8.bridge.native.time_noarg", "v8-bridge",
     "call a JS function that performs one no-argument JS-to-native time intrinsic call", 1000U,
     10000U, sl_bench_v8_native_time_noarg,
     "includes JS handler entry, native intrinsic call, and text result conversion", true, 0U, 0U,
     0U},
    {"v8.bridge.native.crypto_primitive", "v8-bridge",
     "call a JS function that passes a primitive number into a native crypto intrinsic", 100U,
     1000U, sl_bench_v8_native_crypto_primitive,
     "includes random backend cost; used as bridge evidence, not crypto throughput data", true, 0U,
     0U, 0U},
    {"v8.bridge.native.crypto_bytes", "v8-bridge",
     "call a JS function that passes a Uint8Array into a native crypto intrinsic", 1000U, 10000U,
     sl_bench_v8_native_crypto_bytes,
     "exercises byte argument conversion and native text result conversion", true, 0U, 0U, 0U},
    {"v8.bridge.native.crypto_string_bytes", "v8-bridge",
     "call a JS function that passes a string and Uint8Array into a native crypto intrinsic", 1000U,
     10000U, sl_bench_v8_native_crypto_string_bytes,
     "exercises string argument lookup, byte argument copy, native bytes result, and JS length "
     "read",
     true, 0U, 0U, 0U},
    {"v8.bridge.result.primitive_json", "v8-bridge",
     "convert a supported Results.json-style descriptor with primitive fields", 1000U, 10000U,
     sl_bench_v8_result_primitive_json,
     "internal result-conversion evidence; no public response throughput claim", true, 0U, 0U, 0U},
    {"v8.bridge.result.string_utf8", "v8-bridge",
     "copy a UTF-8 JavaScript string result into arena-owned native storage", 1000U, 10000U,
     sl_bench_v8_result_string_utf8, "exercises string conversion and result arena copy", true, 0U,
     0U, 0U},
    {"v8.bridge.promise.resolve_text", "v8-bridge",
     "convert an already-fulfilled JavaScript Promise resolving to text", 1000U, 10000U,
     sl_bench_v8_promise_resolve_text, "owner-thread microtask drain evidence only", true, 0U, 0U,
     0U},
    {"v8.bridge.promise.native_delay_resolve", "v8-bridge",
     "settle a native timer Promise back onto the V8 owner thread", 100U, 1000U,
     sl_bench_v8_promise_native_delay_resolve,
     "includes native continuation posting, owner-thread Promise settlement, and text result "
     "conversion",
     true, 0U, 0U, 0U},
    {"v8.bridge.promise.reject_error", "v8-bridge",
     "convert an already-rejected JavaScript Promise into the stable rejection status path", 100U,
     1000U, sl_bench_v8_promise_reject_error,
     "expected rejection is counted as the measured outcome", true, 0U, 0U, 0U},
    {"v8.bridge.options.invalid_result_header", "v8-bridge",
     "validate and reject a result descriptor with a managed response header", 100U, 1000U,
     sl_bench_v8_invalid_result_header,
     "expected invalid descriptor is counted as the measured outcome", true, 0U, 0U, 0U},
    {"v8.bridge.context.base", "v8-bridge",
     "materialize base route/query/request metadata and call a context handler", 1000U, 10000U,
     sl_bench_v8_context_base,
     "synthetic request context evidence; no socket, router, or response writer involved", true, 0U,
     0U, 0U},
    {"v8.bridge.context.headers_get", "v8-bridge",
     "materialize request headers and perform case-insensitive header lookups", 1000U, 10000U,
     sl_bench_v8_context_headers_get,
     "exercises lazy header snapshot lookup without full entries materialization", true, 0U, 0U,
     0U},
    {"v8.bridge.context.headers_entries", "v8-bridge",
     "materialize full request header entries only when entries() is requested", 100U, 1000U,
     sl_bench_v8_context_headers_entries,
     "separate from header lookup so eager entries cost remains visible", true, 0U, 0U, 0U},
    {"v8.bridge.context.body_text_json", "v8-bridge",
     "transfer request body bytes, lazily create text, and parse JSON from the JS facade", 100U,
     1000U, sl_bench_v8_context_body_text_json,
     "synthetic body facade evidence; JSON parse cost is included", true, 0U, 0U, 0U},
    {"v8.bridge.context.body_bytes", "v8-bridge",
     "transfer request body bytes and expose them as a Uint8Array through the body facade", 1000U,
     10000U, sl_bench_v8_context_body_bytes,
     "covers zero, embedded NUL, and high-byte request body data", true, 0U, 0U, 0U},
    {"v8.flow.http_dispatch.json", "v8-bridge",
     "parse a complete HTTP request, route it, enter a registered V8 handler, and convert a JSON "
     "result",
     100U, 1000U, sl_bench_v8_flow_http_dispatch_json,
     "current in-process Sloppy flow evidence; no socket, TLS, kernel, or public throughput claim",
     true, 0U, 0U, 0U},
};

const SlBenchDefinition* sl_bench_v8_bridge_definitions(size_t* out_count)
{
    if (out_count != NULL) {
        *out_count = sizeof(v8_bridge_definitions) / sizeof(v8_bridge_definitions[0]);
    }

    return v8_bridge_definitions;
}

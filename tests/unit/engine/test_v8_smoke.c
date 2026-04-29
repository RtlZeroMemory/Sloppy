#include "sloppy/engine.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static int expect_str_contains(SlStr haystack, SlStr needle)
{
    size_t index = 0U;
    size_t inner = 0U;

    if (needle.length == 0U) {
        return 0;
    }

    if (haystack.length < needle.length || haystack.ptr == NULL || needle.ptr == NULL) {
        return 1;
    }

    for (index = 0U; index <= haystack.length - needle.length; index += 1U) {
        for (inner = 0U; inner < needle.length; inner += 1U) {
            if (haystack.ptr[index + inner] != needle.ptr[inner]) {
                break;
            }
        }

        if (inner == needle.length) {
            return 0;
        }
    }

    return 1;
}

static int expect_bytes_equal(SlBytes actual, const char* expected)
{
    size_t expected_length = strlen(expected);

    return expect_true(actual.length == expected_length && actual.ptr != NULL &&
                       memcmp(actual.ptr, expected, expected_length) == 0);
}

static int init_arena(SlArena* arena, unsigned char* storage, size_t storage_size)
{
    return expect_status(sl_arena_init(arena, storage, storage_size), SL_STATUS_OK);
}

static SlHttpRequestHead test_request(SlHttpMethod method)
{
    SlHttpRequestHead request = {0};

    request.method = method;
    request.path = sl_str_from_cstr("/users/123");
    request.raw_target = sl_str_from_cstr("/users/123?q=abc");
    return request;
}

static SlHttpRequestContext test_request_context(const SlHttpRequestHead* request)
{
    SlHttpRequestContext context = {0};

    context.request = request;
    return context;
}

static SlEngineOptions v8_options(void)
{
    SlEngineOptions options = {0};

    options.kind = SL_ENGINE_KIND_V8;
    options.runtime_name = sl_str_from_cstr("sloppy-v8-smoke-test");
    options.runtime_version = sl_str_from_cstr("0.2.0-test");
    options.target_platform = sl_str_from_cstr(SL_PLAN_TARGET_PLATFORM_WINDOWS_X64);
    options.target_engine = sl_str_from_cstr(SL_PLAN_TARGET_ENGINE_V8);
    return options;
}

static int test_eval_and_call_global_function(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 1;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0 ||
        engine == NULL)
    {
        return 2;
    }

    if (expect_status(sl_engine_eval_source(
                          engine, sl_str_from_cstr("v8-smoke.js"),
                          sl_str_from_cstr(
                              "globalThis.sloppy_smoke = function () { return \"sloppy-ok\"; };"),
                          &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 3;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_smoke"), &result, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 4;
    }

    if (result.kind != SL_ENGINE_RESULT_TEXT ||
        !sl_str_equal(result.text, sl_str_from_cstr("sloppy-ok")))
    {
        sl_engine_destroy(engine);
        return 5;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_eval_syntax_error_returns_diagnostic(void)
{
    unsigned char engine_storage[8192];
    SlArena engine_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0) {
        return 10;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 11;
    }

    if (expect_status(sl_engine_eval_source(engine, sl_str_from_cstr("syntax-smoke.js"),
                                            sl_str_from_cstr("function nope( {"), &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        sl_engine_destroy(engine);
        return 12;
    }

    if (diag.severity != SL_DIAG_SEVERITY_ERROR || diag.code != SL_DIAG_ENGINE_COMPILE_ERROR ||
        expect_str_contains(diag.message, sl_str_from_cstr("SyntaxError")) != 0 ||
        !sl_str_equal(diag.primary_span.path, sl_str_from_cstr("syntax-smoke.js")) ||
        !diag.primary_span.has_location || diag.primary_span.line == 0U ||
        diag.primary_span.column == 0U)
    {
        sl_engine_destroy(engine);
        return 13;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_missing_function_returns_diagnostic(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {.kind = SL_ENGINE_RESULT_TEXT, .text = sl_str_from_cstr("stale")};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 20;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 21;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("missing_smoke"), &result, &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        sl_engine_destroy(engine);
        return 22;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.severity != SL_DIAG_SEVERITY_ERROR ||
        diag.code != SL_DIAG_ENGINE_CALL_ERROR ||
        expect_str_contains(diag.message, sl_str_from_cstr("missing_smoke")) != 0)
    {
        sl_engine_destroy(engine);
        return 23;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_non_callable_global_returns_diagnostic(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 30;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 31;
    }

    if (expect_status(sl_engine_eval_source(engine, sl_str_from_cstr("v8-noncallable.js"),
                                            sl_str_from_cstr("globalThis.sloppy_value = 42;"),
                                            &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 32;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_value"), &result, &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        sl_engine_destroy(engine);
        return 33;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.severity != SL_DIAG_SEVERITY_ERROR ||
        diag.code != SL_DIAG_ENGINE_CALL_ERROR ||
        expect_str_contains(diag.message, sl_str_from_cstr("not callable")) != 0)
    {
        sl_engine_destroy(engine);
        return 34;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_throwing_function_returns_diagnostic(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 40;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 41;
    }

    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("v8-throw.js"),
                sl_str_from_cstr(
                    "globalThis.sloppy_throw = function () { throw new Error(\"boom\"); };"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 42;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_throw"), &result, &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        sl_engine_destroy(engine);
        return 43;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.severity != SL_DIAG_SEVERITY_ERROR ||
        diag.code != SL_DIAG_ENGINE_EXCEPTION ||
        expect_str_contains(diag.message, sl_str_from_cstr("boom")) != 0 ||
        !sl_str_equal(diag.primary_span.path, sl_str_from_cstr("v8-throw.js")) ||
        !diag.primary_span.has_location)
    {
        sl_engine_destroy(engine);
        return 44;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_unsupported_result_returns_call_diagnostic(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 50;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 51;
    }

    if (expect_status(sl_engine_eval_source(engine, sl_str_from_cstr("v8-result.js"),
                                            sl_str_from_cstr("globalThis.sloppy_object = function "
                                                             "() { return { ok: true }; };"),
                                            &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 52;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_object"), &result, &diag),
                      SL_STATUS_UNSUPPORTED) != 0)
    {
        sl_engine_destroy(engine);
        return 53;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.severity != SL_DIAG_SEVERITY_ERROR ||
        diag.code != SL_DIAG_INVALID_HTTP_RESULT ||
        expect_str_contains(diag.message, sl_str_from_cstr("unsupported result type")) != 0)
    {
        sl_engine_destroy(engine);
        return 54;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_create_destroy_create_reuses_process_platform(void)
{
    unsigned char engine_storage[8192];
    SlArena engine_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* first = NULL;
    SlEngine* second = NULL;

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0) {
        return 60;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &first), SL_STATUS_OK) != 0 ||
        first == NULL)
    {
        return 61;
    }

    sl_engine_destroy(first);

    if (expect_status(sl_engine_create(&options, &engine_arena, &second), SL_STATUS_OK) != 0 ||
        second == NULL)
    {
        return 62;
    }

    sl_engine_destroy(second);
    return 0;
}

static int test_request_context_reports_actual_method(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlHttpRequestHead request = test_request(SL_HTTP_METHOD_POST);
    SlHttpRequestContext context = test_request_context(&request);

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 70;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 71;
    }

    if (expect_status(sl_engine_eval_source(
                          engine, sl_str_from_cstr("v8-context-method.js"),
                          sl_str_from_cstr("globalThis.sloppy_context = function (ctx) { return "
                                           "{ __sloppyResult: true, kind: \"json\", status: 200, "
                                           "contentType: \"application/json; charset=utf-8\", "
                                           "body: { method: ctx.request.method } }; };"),
                          &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 72;
    }

    if (expect_status(sl_engine_call_function_with_context(engine, &result_arena,
                                                           sl_str_from_cstr("sloppy_context"),
                                                           &context, &result, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 73;
    }

    if (result.kind != SL_ENGINE_RESULT_JSON ||
        expect_bytes_equal(result.response.body, "{\"method\":\"POST\"}") != 0)
    {
        sl_engine_destroy(engine);
        return 74;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_missing_json_body_serializes_as_null(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlHttpRequestHead request = test_request(SL_HTTP_METHOD_GET);
    SlHttpRequestContext context = test_request_context(&request);

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 80;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 81;
    }

    if (expect_status(sl_engine_eval_source(
                          engine, sl_str_from_cstr("v8-json-null.js"),
                          sl_str_from_cstr("globalThis.sloppy_missing_body = function () { return "
                                           "{ __sloppyResult: true, kind: \"json\", status: 200, "
                                           "contentType: \"application/json; charset=utf-8\" }; };"
                                           "globalThis.sloppy_undefined_body = function () { "
                                           "return { __sloppyResult: true, kind: \"json\", "
                                           "status: 200, contentType: "
                                           "\"application/json; charset=utf-8\", body: undefined "
                                           "}; };"),
                          &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 82;
    }

    if (expect_status(sl_engine_call_function_with_context(engine, &result_arena,
                                                           sl_str_from_cstr("sloppy_missing_body"),
                                                           &context, &result, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 83;
    }

    if (result.kind != SL_ENGINE_RESULT_JSON ||
        expect_bytes_equal(result.response.body, "null") != 0)
    {
        sl_engine_destroy(engine);
        return 84;
    }

    result = (SlEngineResult){0};
    if (expect_status(sl_engine_call_function_with_context(
                          engine, &result_arena, sl_str_from_cstr("sloppy_undefined_body"),
                          &context, &result, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 85;
    }

    if (result.kind != SL_ENGINE_RESULT_JSON ||
        expect_bytes_equal(result.response.body, "null") != 0)
    {
        sl_engine_destroy(engine);
        return 86;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_registered_handler_receives_context(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlHttpRequestHead request = test_request(SL_HTTP_METHOD_GET);
    SlHttpRequestContext context = test_request_context(&request);

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 90;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0 ||
        expect_status(
            sl_engine_eval_source(engine, sl_str_from_cstr("v8-register.js"),
                                  sl_str_from_cstr("__sloppy_register_handler(1, function (ctx) { "
                                                   "return ctx.request.rawTarget; });"),
                                  &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 91;
    }

    if (expect_status(sl_engine_call_registered_handler_with_context(engine, &result_arena, 1U,
                                                                     &context, &result, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 92;
    }

    if (result.kind != SL_ENGINE_RESULT_TEXT ||
        !sl_str_equal(result.text, sl_str_from_cstr("/users/123?q=abc")))
    {
        sl_engine_destroy(engine);
        return 93;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_duplicate_registered_handler_fails_during_eval(void)
{
    unsigned char engine_storage[8192];
    SlArena engine_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0) {
        return 100;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 101;
    }

    if (expect_status(
            sl_engine_eval_source(engine, sl_str_from_cstr("v8-duplicate-register.js"),
                                  sl_str_from_cstr("__sloppy_register_handler(1, () => \"a\");"
                                                   "__sloppy_register_handler(1, () => \"b\");"),
                                  &diag),
            SL_STATUS_INVALID_STATE) != 0)
    {
        sl_engine_destroy(engine);
        return 102;
    }

    if (diag.code != SL_DIAG_ENGINE_EXCEPTION ||
        expect_str_contains(diag.message, sl_str_from_cstr("duplicate handler ID")) != 0)
    {
        sl_engine_destroy(engine);
        return 103;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_failed_eval_rolls_back_registered_handlers(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlHttpRequestHead request = test_request(SL_HTTP_METHOD_GET);
    SlHttpRequestContext context = test_request_context(&request);

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 105;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 106;
    }

    if (expect_status(
            sl_engine_eval_source(engine, sl_str_from_cstr("v8-rollback-register.js"),
                                  sl_str_from_cstr("__sloppy_register_handler(1, () => "
                                                   "\"stale\"); throw new Error(\"boom\");"),
                                  &diag),
            SL_STATUS_INVALID_STATE) != 0)
    {
        sl_engine_destroy(engine);
        return 107;
    }

    if (expect_status(sl_engine_call_registered_handler_with_context(engine, &result_arena, 1U,
                                                                     &context, &result, &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        sl_engine_destroy(engine);
        return 108;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_ENGINE_CALL_ERROR ||
        expect_str_contains(diag.message, sl_str_from_cstr("unregistered handler ID 1")) != 0)
    {
        sl_engine_destroy(engine);
        return 109;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_intrinsic_misuse_fails_during_eval(void)
{
    unsigned char engine_storage[8192];
    SlArena engine_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0) {
        return 110;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 111;
    }

    if (expect_status(sl_engine_eval_source(engine, sl_str_from_cstr("v8-bad-register.js"),
                                            sl_str_from_cstr("__sloppy_register_handler(\"1\", "
                                                             "() => \"bad\");"),
                                            &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        sl_engine_destroy(engine);
        return 112;
    }

    if (diag.code != SL_DIAG_ENGINE_EXCEPTION ||
        expect_str_contains(diag.message, sl_str_from_cstr("handler ID")) != 0)
    {
        sl_engine_destroy(engine);
        return 113;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_missing_registered_handler_returns_diagnostic(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlHttpRequestHead request = test_request(SL_HTTP_METHOD_GET);
    SlHttpRequestContext context = test_request_context(&request);

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 120;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 121;
    }

    if (expect_status(sl_engine_call_registered_handler_with_context(engine, &result_arena, 99U,
                                                                     &context, &result, &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        sl_engine_destroy(engine);
        return 122;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_ENGINE_CALL_ERROR ||
        expect_str_contains(diag.message, sl_str_from_cstr("unregistered handler ID 99")) != 0)
    {
        sl_engine_destroy(engine);
        return 123;
    }

    sl_engine_destroy(engine);
    return 0;
}

int main(void)
{
    int result = 0;

    result = test_eval_and_call_global_function();
    if (result != 0) {
        return result;
    }

    result = test_eval_syntax_error_returns_diagnostic();
    if (result != 0) {
        return result;
    }

    result = test_missing_function_returns_diagnostic();
    if (result != 0) {
        return result;
    }

    result = test_non_callable_global_returns_diagnostic();
    if (result != 0) {
        return result;
    }

    result = test_throwing_function_returns_diagnostic();
    if (result != 0) {
        return result;
    }

    result = test_unsupported_result_returns_call_diagnostic();
    if (result != 0) {
        return result;
    }

    result = test_create_destroy_create_reuses_process_platform();
    if (result != 0) {
        return result;
    }

    result = test_request_context_reports_actual_method();
    if (result != 0) {
        return result;
    }

    result = test_missing_json_body_serializes_as_null();
    if (result != 0) {
        return result;
    }

    result = test_registered_handler_receives_context();
    if (result != 0) {
        return result;
    }

    result = test_duplicate_registered_handler_fails_during_eval();
    if (result != 0) {
        return result;
    }

    result = test_failed_eval_rolls_back_registered_handlers();
    if (result != 0) {
        return result;
    }

    result = test_intrinsic_misuse_fails_during_eval();
    if (result != 0) {
        return result;
    }

    return test_missing_registered_handler_returns_diagnostic();
}

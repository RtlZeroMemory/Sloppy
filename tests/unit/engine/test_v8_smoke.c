#include "sloppy/app_host.h"
#include "sloppy/engine.h"
#include "sloppy/fs.h"

#include <stdbool.h>
#include <stddef.h>
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

static int set_test_env(const char* key, const char* value)
{
#ifdef _WIN32
    return _putenv_s(key, value);
#else
    return setenv(key, value, 1);
#endif
}

static int expect_response_header(const SlHttpResponse* response, const char* name,
                                  const char* value)
{
    size_t index = 0U;

    if (response == NULL || name == NULL || value == NULL ||
        (response->header_count != 0U && response->headers == NULL))
    {
        return 1;
    }

    for (index = 0U; index < response->header_count; index += 1U) {
        if (sl_str_equal(response->headers[index].name, sl_str_from_cstr(name)) &&
            sl_str_equal(response->headers[index].value, sl_str_from_cstr(value)))
        {
            return 0;
        }
    }

    return 1;
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

static void init_sqlite_plan(SlPlan* plan, SlPlanDataProvider* provider,
                             SlPlanCapability* capability, const char* access)
{
    *provider = (SlPlanDataProvider){
        .token = sl_str_from_cstr("data.main"),
        .provider = sl_str_from_cstr("sqlite"),
        .capability = sl_str_from_cstr("data.main"),
        .service = sl_str_empty(),
        .database = sl_str_from_cstr(":memory:"),
    };
    *capability = (SlPlanCapability){
        .token = sl_str_from_cstr("data.main"),
        .kind = sl_str_from_cstr("database"),
        .access = sl_str_from_cstr(access),
        .provider = sl_str_from_cstr("data.main"),
    };
    *plan = (SlPlan){
        .data_providers = provider,
        .data_provider_count = 1U,
        .capabilities = capability,
        .capability_count = 1U,
    };
}

static int attach_runtime_features(SlEngineOptions* options, const SlPlan* plan,
                                   SlArena* diag_arena, SlRuntimeFeatureSet* features)
{
    SlRuntimeFeatureAvailability availability = sl_runtime_feature_default_availability();
    SlDiag diag = {0};

    if (expect_status(
            sl_runtime_feature_activate_plan(plan, &availability, diag_arena, features, &diag),
            SL_STATUS_OK) != 0)
    {
        return 1;
    }
    options->runtime_features = features;
    return 0;
}

static int attach_sqlite_plan(SlEngineOptions* options, SlPlan* plan,
                              SlCapabilityRegistry* registry, SlPlanDataProvider* provider,
                              SlPlanCapability* capability, SlArena* diag_arena,
                              SlRuntimeFeatureSet* features, const char* access)
{
    init_sqlite_plan(plan, provider, capability, access);
    if (expect_status(sl_capability_registry_init_from_plan(plan, registry), SL_STATUS_OK) != 0) {
        return 1;
    }
    if (attach_runtime_features(options, plan, diag_arena, features) != 0) {
        return 2;
    }
    options->plan = plan;
    options->capabilities = registry;
    return 0;
}

static int test_eval_and_call_global_function(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[1024];
    unsigned char feature_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlArena feature_arena = {0};
    SlEngineOptions options = v8_options();
    SlPlanRequiredFeature required = {.id = sl_str_from_cstr("stdlib.fs")};
    SlPlan plan = {.required_features = &required, .required_feature_count = 1U};
    SlRuntimeFeatureSet features = {0};
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0 ||
        init_arena(&feature_arena, feature_storage, sizeof(feature_storage)) != 0 ||
        attach_runtime_features(&options, &plan, &feature_arena, &features) != 0)
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

static int test_filesystem_intrinsic_promise_roundtrip(void)
{
    unsigned char engine_storage[65536];
    unsigned char result_storage[4096];
    unsigned char feature_storage[4096];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlArena feature_arena = {0};
    SlEngineOptions options = v8_options();
    SlPlanRequiredFeature required = {.id = sl_str_from_cstr("stdlib.fs")};
    SlPlan plan = {.required_features = &required, .required_feature_count = 1U};
    SlRuntimeFeatureSet features = {0};
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlStr path = sl_str_from_cstr("./sloppy-v8-fs-test.txt");
    SlStr invalid_path = sl_str_from_cstr("./sloppy-v8-fs-invalid.bin");
    SlStr dir_path = sl_str_from_cstr("./sloppy-v8-fs-dir");

    (void)sl_fs_delete_file(path, NULL);
    (void)sl_fs_delete_file(invalid_path, NULL);
    (void)sl_fs_delete_directory(dir_path, true, NULL);
    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0 ||
        init_arena(&feature_arena, feature_storage, sizeof(feature_storage)) != 0 ||
        attach_runtime_features(&options, &plan, &feature_arena, &features) != 0)
    {
        return 1;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0 ||
        engine == NULL)
    {
        return 2;
    }

    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("v8-fs.js"),
                sl_str_from_cstr(
                    "globalThis.sloppy_fs_roundtrip = async function () {"
                    " await globalThis.__sloppy.fs.writeText(\"./sloppy-v8-fs-test.txt\", "
                    "\"fs-ok\");"
                    " return await globalThis.__sloppy.fs.readText(\"./sloppy-v8-fs-test.txt\");"
                    "};"
                    "globalThis.sloppy_fs_invalid_utf8 = async function () {"
                    " await globalThis.__sloppy.fs.writeBytes(\"./sloppy-v8-fs-invalid.bin\", "
                    "new Uint8Array([255]));"
                    " try { await globalThis.__sloppy.fs.readText("
                    "\"./sloppy-v8-fs-invalid.bin\"); return \"missing rejection\"; }"
                    " catch (err) { return String(err && err.message ? err.message : err); }"
                    "};"
                    "globalThis.sloppy_fs_advanced = async function () {"
                    " await globalThis.__sloppy.fs.directoryCreate("
                    "\"./sloppy-v8-fs-dir\", true);"
                    " await globalThis.__sloppy.fs.atomicWriteText("
                    "\"./sloppy-v8-fs-dir/a.txt\", \"abcdef\");"
                    " const entries = await globalThis.__sloppy.fs.directoryList("
                    "\"./sloppy-v8-fs-dir\");"
                    " const entry = entries.find((item) => item && item.name === 'a.txt');"
                    " if (!entry) { throw new Error('missing a.txt'); }"
                    " const handle = await globalThis.__sloppy.fs.openHandle("
                    "\"./sloppy-v8-fs-dir/a.txt\", \"read\", false);"
                    " const bytes = await globalThis.__sloppy.fs.handleRead(handle, 3);"
                    " await globalThis.__sloppy.fs.handleClose(handle);"
                    " const writer = await globalThis.__sloppy.fs.openHandle("
                    "\"./sloppy-v8-fs-dir/write.txt\", \"write\", true);"
                    " await globalThis.__sloppy.fs.handleWriteText(writer, \"write-ok\");"
                    " await globalThis.__sloppy.fs.handleClose(writer);"
                    " const written = await globalThis.__sloppy.fs.readText("
                    "\"./sloppy-v8-fs-dir/write.txt\");"
                    " const watcher = await globalThis.__sloppy.fs.watch("
                    "\"./sloppy-v8-fs-dir\", true, { queueCapacity: 4 });"
                    " await globalThis.__sloppy.fs.writeText("
                    "\"./sloppy-v8-fs-dir/watched.txt\", \"watch\");"
                    " const watchEvent = await globalThis.__sloppy.fs.watchNext(watcher);"
                    " await globalThis.__sloppy.fs.watchClose(watcher);"
                    " await globalThis.__sloppy.fs.directoryDelete("
                    "\"./sloppy-v8-fs-dir\", true);"
                    " return entry.name + ':' + bytes.byteLength + ':' + written + ':' + "
                    "watchEvent.kind + ':' + watchEvent.path;"
                    "};"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 3;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_fs_roundtrip"), &result,
                                               &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_TEXT ||
        !sl_str_equal(result.text, sl_str_from_cstr("fs-ok")))
    {
        sl_engine_destroy(engine);
        (void)sl_fs_delete_file(path, NULL);
        (void)sl_fs_delete_file(invalid_path, NULL);
        (void)sl_fs_delete_directory(dir_path, true, NULL);
        return 4;
    }

    result = (SlEngineResult){0};
    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_fs_invalid_utf8"), &result,
                                               &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_TEXT ||
        !sl_str_equal(result.text, sl_str_from_cstr("Invalid UTF-8 in file")))
    {
        sl_engine_destroy(engine);
        (void)sl_fs_delete_file(path, NULL);
        (void)sl_fs_delete_file(invalid_path, NULL);
        (void)sl_fs_delete_directory(dir_path, true, NULL);
        return 5;
    }

    result = (SlEngineResult){0};
    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_fs_advanced"), &result,
                                               &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_TEXT ||
        !sl_str_equal(result.text, sl_str_from_cstr("a.txt:3:write-ok:created:watched.txt")))
    {
        sl_engine_destroy(engine);
        (void)sl_fs_delete_file(path, NULL);
        (void)sl_fs_delete_file(invalid_path, NULL);
        (void)sl_fs_delete_directory(dir_path, true, NULL);
        return 6;
    }

    sl_engine_destroy(engine);
    (void)sl_fs_delete_file(path, NULL);
    (void)sl_fs_delete_file(invalid_path, NULL);
    (void)sl_fs_delete_directory(dir_path, true, NULL);
    return 0;
}

static int test_v8_string_interop_uses_explicit_lengths(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};
    const char source[] =
        "globalThis.sloppy_embedded = function () { return 'pre\\u0000post'; }; trailing";
    const char function_name[] = {'s', 'l', 'o', 'p', 'p', 'y', '_', 'e',
                                  'm', 'b', 'e', 'd', 'd', 'e', 'd'};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 6;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0 ||
        engine == NULL)
    {
        return 7;
    }

    if (expect_status(sl_engine_eval_source(
                          engine, sl_str_from_cstr("v8-string-interop.js"),
                          sl_str_from_parts(source, sizeof(source) - sizeof(" trailing")), &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 8;
    }

    if (expect_status(sl_engine_call_function0(
                          engine, &result_arena,
                          sl_str_from_parts(function_name, sizeof(function_name)), &result, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 9;
    }

    if (result.kind != SL_ENGINE_RESULT_TEXT ||
        result.text.length != sizeof("pre") - 1U + 1U + sizeof("post") - 1U ||
        result.text.ptr == NULL || memcmp(result.text.ptr, "pre", 3U) != 0 ||
        result.text.ptr[3] != '\0' || memcmp(result.text.ptr + 4, "post", 4U) != 0)
    {
        sl_engine_destroy(engine);
        return 10;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_v8_string_interop_copies_utf8_to_native_result(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};
    const unsigned char expected[] = {'c', 'a', 'f', 0xC3U, 0xA9U};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 11;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0 ||
        engine == NULL)
    {
        return 12;
    }

    if (expect_status(
            sl_engine_eval_source(engine, sl_str_from_cstr("v8-utf8-interop.js"),
                                  sl_str_from_cstr("globalThis.sloppy_utf8 = function () {"
                                                   "  return 'caf\\u00e9';"
                                                   "};"),
                                  &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 13;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_utf8"), &result, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 14;
    }

    if (result.kind != SL_ENGINE_RESULT_TEXT || result.text.length != sizeof(expected) ||
        result.text.ptr == NULL || memcmp(result.text.ptr, expected, sizeof(expected)) != 0)
    {
        sl_engine_destroy(engine);
        return 15;
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

static int test_throwing_function_remaps_source_map_location(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};
    const char source[] = "globalThis.sloppy_throw_mapped = function () {\n"
                          "  throw new Error(\"mapped boom\");\n"
                          "};";
    const char source_map[] =
        "{\"version\":3,\"file\":\"generated-app.js\",\"sources\":[\"src/users.js\"],"
        "\"sourcesContent\":[\"function handler() {\\n  throw new Error();\\n}\\n\"],"
        "\"names\":[],\"mappings\":\";EASI\"}";

    options.source_map =
        sl_bytes_from_parts((const unsigned char*)source_map, sizeof(source_map) - 1U);
    options.source_map_source_name = sl_str_from_cstr("generated-app.js");

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 45;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 46;
    }

    if (expect_status(sl_engine_eval_source(engine, sl_str_from_cstr("generated-app.js"),
                                            sl_str_from_cstr(source), &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 47;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_throw_mapped"), &result,
                                               &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        sl_engine_destroy(engine);
        return 48;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_ENGINE_EXCEPTION ||
        expect_str_contains(diag.message, sl_str_from_cstr("mapped boom")) != 0 ||
        !sl_str_equal(diag.primary_span.path, sl_str_from_cstr("src/users.js")) ||
        !diag.primary_span.has_location || diag.primary_span.line != 10U || diag.hint_count != 0U ||
        diag.related_count == 0U)
    {
        sl_engine_destroy(engine);
        return 49;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_source_map_original_column_does_not_add_generated_delta(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};
    const char source[] = "globalThis.sloppy_column_mapped = function () {\n"
                          "  throw new Error(\"column mapped\");\n"
                          "};";
    const char source_map[] =
        "{\"version\":3,\"file\":\"generated-app.js\",\"sources\":[\"src/columns.js\"],"
        "\"names\":[],\"mappings\":\";AAAoB\"}";

    options.source_map =
        sl_bytes_from_parts((const unsigned char*)source_map, sizeof(source_map) - 1U);
    options.source_map_source_name = sl_str_from_cstr("generated-app.js");

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 50;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 51;
    }

    if (expect_status(sl_engine_eval_source(engine, sl_str_from_cstr("generated-app.js"),
                                            sl_str_from_cstr(source), &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 52;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_column_mapped"), &result,
                                               &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        sl_engine_destroy(engine);
        return 53;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_ENGINE_EXCEPTION ||
        !sl_str_equal(diag.primary_span.path, sl_str_from_cstr("src/columns.js")) ||
        !diag.primary_span.has_location || diag.primary_span.line != 1U ||
        diag.primary_span.column != 21U)
    {
        sl_engine_destroy(engine);
        return 54;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_unmapped_source_map_segment_reports_generated_location(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};
    const char source[] = "globalThis.sloppy_unmapped_segment = function () {\n"
                          "  throw new Error(\"unmapped segment\");\n"
                          "};";
    const char source_map[] =
        "{\"version\":3,\"file\":\"generated-app.js\",\"sources\":[\"src/unmapped.js\"],"
        "\"names\":[],\"mappings\":\";AAAA,A\"}";

    options.source_map =
        sl_bytes_from_parts((const unsigned char*)source_map, sizeof(source_map) - 1U);
    options.source_map_source_name = sl_str_from_cstr("generated-app.js");

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 55;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 56;
    }

    if (expect_status(sl_engine_eval_source(engine, sl_str_from_cstr("generated-app.js"),
                                            sl_str_from_cstr(source), &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 57;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_unmapped_segment"), &result,
                                               &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        sl_engine_destroy(engine);
        return 58;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE) {
        sl_engine_destroy(engine);
        return 59;
    }
    if (diag.code != SL_DIAG_ENGINE_EXCEPTION) {
        sl_engine_destroy(engine);
        return 70;
    }
    if (!sl_str_equal(diag.primary_span.path, sl_str_from_cstr("generated-app.js"))) {
        sl_engine_destroy(engine);
        return 71;
    }
    if (!diag.primary_span.has_location) {
        sl_engine_destroy(engine);
        return 72;
    }
    sl_engine_destroy(engine);
    return 0;
}

static int test_malformed_source_map_reports_generated_location(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};
    const char malformed_map[] =
        "{\"version\":3,\"sources\":[\"src/users.js\"],\"mappings\":\"gggggggggggggggggA\"}";

    options.source_map =
        sl_bytes_from_parts((const unsigned char*)malformed_map, sizeof(malformed_map) - 1U);
    options.source_map_source_name = sl_str_from_cstr("generated-app.js");

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 60;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 61;
    }

    if (expect_status(sl_engine_eval_source(
                          engine, sl_str_from_cstr("generated-app.js"),
                          sl_str_from_cstr("globalThis.sloppy_bad_map = function () { throw new "
                                           "Error(\"fallback boom\"); };"),
                          &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 62;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_bad_map"), &result, &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        sl_engine_destroy(engine);
        return 63;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_ENGINE_EXCEPTION ||
        !sl_str_equal(diag.primary_span.path, sl_str_from_cstr("generated-app.js")) ||
        diag.hint_count == 0U ||
        expect_str_contains(diag.hints[0], sl_str_from_cstr("Malformed source map")) != 0)
    {
        sl_engine_destroy(engine);
        return 64;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_registered_handler_throw_remaps_source_map_location(void)
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
    const char source[] = "__sloppy_register_handler(1, function(ctx) {\n"
                          "  throw new Error(\"handler mapped\");\n"
                          "});";
    const char source_map[] =
        "{\"version\":3,\"file\":\"generated-app.js\",\"sources\":[\"routes/users.js\"],"
        "\"sourcesContent\":[\"export function users() {\\n  throw new Error();\\n}\\n\"],"
        "\"names\":[],\"mappings\":\";EAKA\"}";

    options.source_map =
        sl_bytes_from_parts((const unsigned char*)source_map, sizeof(source_map) - 1U);
    options.source_map_source_name = sl_str_from_cstr("generated-app.js");

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 65;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 66;
    }

    if (expect_status(sl_engine_eval_source(engine, sl_str_from_cstr("generated-app.js"),
                                            sl_str_from_cstr(source), &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 67;
    }

    if (expect_status(sl_engine_call_registered_handler_with_context(engine, &result_arena, 1U,
                                                                     &context, &result, &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        sl_engine_destroy(engine);
        return 68;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_ENGINE_EXCEPTION ||
        expect_str_contains(diag.message, sl_str_from_cstr("handler mapped")) != 0 ||
        !sl_str_equal(diag.primary_span.path, sl_str_from_cstr("routes/users.js")) ||
        !diag.primary_span.has_location || diag.primary_span.line != 6U)
    {
        sl_engine_destroy(engine);
        return 69;
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

static int test_promise_result_settles_text(void)
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
        return 55;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 56;
    }

    if (expect_status(
            sl_engine_eval_source(engine, sl_str_from_cstr("v8-promise.js"),
                                  sl_str_from_cstr("globalThis.sloppy_promise = async function "
                                                   "() { return \"later\"; };"),
                                  &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 57;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_promise"), &result, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 58;
    }

    if (result.kind != SL_ENGINE_RESULT_TEXT ||
        !sl_str_equal(result.text, sl_str_from_cstr("later")))
    {
        sl_engine_destroy(engine);
        return 59;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_time_intrinsic_delay_settles_on_owner_thread(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[1024];
    unsigned char feature_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlArena feature_arena = {0};
    SlEngineOptions options = v8_options();
    SlPlanRequiredFeature required = {.id = sl_str_from_cstr("stdlib.time")};
    SlPlan plan = {.required_features = &required, .required_feature_count = 1U};
    SlRuntimeFeatureSet features = {0};
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0 ||
        init_arena(&feature_arena, feature_storage, sizeof(feature_storage)) != 0 ||
        attach_runtime_features(&options, &plan, &feature_arena, &features) != 0)
    {
        return 401;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 402;
    }

    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("v8-time-delay.js"),
                sl_str_from_cstr("globalThis.sloppy_time_delay = async function () {"
                                 "  const before = globalThis.__sloppy.time.monotonicMs();"
                                 "  const delays = [globalThis.__sloppy.time.delay(10)];"
                                 "  for (let i = 0; i < 4; i += 1) {"
                                 "    delays.push(globalThis.__sloppy.time.delay(0));"
                                 "  }"
                                 "  await Promise.all(delays);"
                                 "  const after = globalThis.__sloppy.time.monotonicMs();"
                                 "  return after >= before ? 'time-delay-ok' : 'time-regressed';"
                                 "};"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 403;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_time_delay"), &result,
                                               &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 404;
    }

    if (result.kind != SL_ENGINE_RESULT_TEXT ||
        !sl_str_equal(result.text, sl_str_from_cstr("time-delay-ok")))
    {
        sl_engine_destroy(engine);
        return 405;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_time_intrinsic_inactive_feature_is_not_registered(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[1024];
    unsigned char feature_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlArena feature_arena = {0};
    SlEngineOptions options = v8_options();
    SlPlan plan = {0};
    SlRuntimeFeatureSet features = {0};
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0 ||
        init_arena(&feature_arena, feature_storage, sizeof(feature_storage)) != 0 ||
        attach_runtime_features(&options, &plan, &feature_arena, &features) != 0)
    {
        return 406;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 407;
    }

    if (expect_status(
            sl_engine_eval_source(engine, sl_str_from_cstr("v8-time-inactive.js"),
                                  sl_str_from_cstr("globalThis.sloppy_time_inactive = function () {"
                                                   "  return globalThis.__sloppy.time === undefined"
                                                   "    ? 'time-inactive-ok'"
                                                   "    : 'time-unexpectedly-active';"
                                                   "};"),
                                  &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 408;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_time_inactive"), &result,
                                               &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 409;
    }

    if (result.kind != SL_ENGINE_RESULT_TEXT ||
        !sl_str_equal(result.text, sl_str_from_cstr("time-inactive-ok")))
    {
        sl_engine_destroy(engine);
        return 410;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_crypto_intrinsic_hash_hmac_random_and_constant_time(void)
{
    unsigned char engine_storage[16384];
    unsigned char result_storage[1024];
    unsigned char feature_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlArena feature_arena = {0};
    SlEngineOptions options = v8_options();
    SlPlanRequiredFeature required = {.id = sl_str_from_cstr("stdlib.crypto")};
    SlPlan plan = {.required_features = &required, .required_feature_count = 1U};
    SlRuntimeFeatureSet features = {0};
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0 ||
        init_arena(&feature_arena, feature_storage, sizeof(feature_storage)) != 0 ||
        attach_runtime_features(&options, &plan, &feature_arena, &features) != 0)
    {
        return 411;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 412;
    }

    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("v8-crypto.js"),
                sl_str_from_cstr("globalThis.sloppy_crypto_smoke = async function () {"
                                 "  const c = globalThis.__sloppy.crypto;"
                                 "  const enc = (s) => new Uint8Array(Array.from(s).map(ch => "
                                 "ch.charCodeAt(0)));"
                                 "  const hex = (bytes) => Array.from(bytes).map(b => "
                                 "b.toString(16).padStart(2, '0')).join('');"
                                 "  const digest = hex(c.hash('sha256', enc('abc')));"
                                 "  const key = new Uint8Array(20); key.fill(0x0b);"
                                 "  const sig = c.hmac('sha256', key, enc('Hi There'));"
                                 "  const uuid = c.randomUuid();"
                                 "  const code = c.randomNumericCode(6);"
                                 "  const randomHexLength = c.randomHex(4).length;"
                                 "  const emptyRandomText = c.randomHex(0) + "
                                 "c.randomToken(0) + c.randomNumericCode(0);"
                                 "  const passwordHash = await c.passwordHash(enc('password'), "
                                 "2, 67108864);"
                                 "  const passwordOk = await c.passwordVerify(enc('password'), "
                                 "passwordHash);"
                                 "  const wrongPasswordRejected = !(await c.passwordVerify("
                                 "enc('wrong-password'), passwordHash));"
                                 "  const passwordRehash = await c.passwordNeedsRehash("
                                 "passwordHash, 3, 67108864);"
                                 "  const nonCrypto = c.nonCryptoXxHash64(enc('hello'));"
                                 "  const oversizedHashRejected = await (async () => {"
                                 "    try { await c.passwordVerify(enc('password'), 'x'.repeat("
                                 "129)); return false; }"
                                 "    catch (error) { return error instanceof TypeError; }"
                                 "  })();"
                                 "  const oversizedRehashRejected = await (async () => {"
                                 "    try { await c.passwordNeedsRehash('x'.repeat(129), 2, "
                                 "67108864); return false; }"
                                 "    catch (error) { return error instanceof TypeError; }"
                                 "  })();"
                                 "  const equal = c.constantTimeEquals(sig, sig);"
                                 "  return digest + ':' + hex(sig).slice(0, 8) + ':' + "
                                 "uuid[14] + ':' + code.length + ':' + randomHexLength + ':' + "
                                 "emptyRandomText.length + ':' + passwordHash.startsWith("
                                 "'$argon2id$') + ':' + passwordOk + ':' + passwordRehash + ':' + "
                                 "wrongPasswordRejected + ':' + nonCrypto + ':' + "
                                 "oversizedHashRejected + ':' + "
                                 "oversizedRehashRejected + ':' + equal;"
                                 "};"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 413;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_crypto_smoke"), &result,
                                               &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 414;
    }

    if (result.kind != SL_ENGINE_RESULT_TEXT ||
        !sl_str_equal(result.text, sl_str_from_cstr("ba7816bf8f01cfea414140de5dae2223"
                                                    "b00361a396177a9cb410ff61f20015ad:"
                                                    "b0344c61:4:6:8:0:true:true:true:true:"
                                                    "26c7827d889f6da3:true:true:true")))
    {
        sl_engine_destroy(engine);
        return 415;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_crypto_intrinsic_inactive_feature_is_not_registered(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[1024];
    unsigned char feature_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlArena feature_arena = {0};
    SlEngineOptions options = v8_options();
    SlPlan plan = {0};
    SlRuntimeFeatureSet features = {0};
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0 ||
        init_arena(&feature_arena, feature_storage, sizeof(feature_storage)) != 0 ||
        attach_runtime_features(&options, &plan, &feature_arena, &features) != 0)
    {
        return 416;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 417;
    }

    if (expect_status(
            sl_engine_eval_source(engine, sl_str_from_cstr("v8-crypto-inactive.js"),
                                  sl_str_from_cstr("globalThis.sloppy_crypto_inactive = function "
                                                   "() {"
                                                   "  return globalThis.__sloppy.crypto === "
                                                   "undefined"
                                                   "    ? 'crypto-inactive-ok'"
                                                   "    : 'crypto-unexpectedly-active';"
                                                   "};"),
                                  &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 418;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_crypto_inactive"), &result,
                                               &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 419;
    }

    if (result.kind != SL_ENGINE_RESULT_TEXT ||
        !sl_str_equal(result.text, sl_str_from_cstr("crypto-inactive-ok")))
    {
        sl_engine_destroy(engine);
        return 420;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_codec_intrinsic_namespace_registered_when_active(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[1024];
    unsigned char feature_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlArena feature_arena = {0};
    SlEngineOptions options = v8_options();
    SlPlanRequiredFeature required = {.id = sl_str_from_cstr("stdlib.codec")};
    SlPlan plan = {.required_features = &required, .required_feature_count = 1U};
    SlRuntimeFeatureSet features = {0};
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0 ||
        init_arena(&feature_arena, feature_storage, sizeof(feature_storage)) != 0 ||
        attach_runtime_features(&options, &plan, &feature_arena, &features) != 0)
    {
        return 421;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 422;
    }

    if (expect_status(sl_engine_eval_source(
                          engine, sl_str_from_cstr("v8-codec-active.js"),
                          sl_str_from_cstr("globalThis.sloppy_codec_active = function "
                                           "() {"
                                           "  const c = globalThis.__sloppy.codec;"
                                           "  if (!c || typeof c.gzip !== 'function' || "
                                           "typeof c.gunzip !== 'function') {"
                                           "    return 'codec-missing';"
                                           "  }"
                                           "  const input = new Uint8Array([0, 104, 105, "
                                           "255]);"
                                           "  const gz = c.gzip(input, 6);"
                                           "  const out = c.gunzip(gz, 64);"
                                           "  const same = out.length === input.length && "
                                           "out.every((byte, index) => byte === "
                                           "input[index]);"
                                           "  const limited = (() => { try { c.gunzip(gz, "
                                           "1); return false; } catch (error) { return "
                                           "String(error.message).includes("
                                           "'SLOPPY_E_CODEC_DECOMPRESSION_LIMIT_EXCEEDED'); "
                                           "} })();"
                                           "  const corrupt = (() => { try { c.gunzip(new "
                                           "Uint8Array([1,2,3]), 64); return false; } "
                                           "catch (error) { return String(error.message)"
                                           ".includes('SLOPPY_E_CODEC_COMPRESSED_STREAM_"
                                           "CORRUPT'); } })();"
                                           "  return 'codec-active-ok:' + gz[0] + ':' + "
                                           "gz[1] + ':' + same + ':' + limited + ':' + "
                                           "corrupt;"
                                           "};"),
                          &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 423;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_codec_active"), &result,
                                               &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 424;
    }

    if (result.kind != SL_ENGINE_RESULT_TEXT ||
        !sl_str_equal(result.text, sl_str_from_cstr("codec-active-ok:31:139:true:true:true")))
    {
        sl_engine_destroy(engine);
        return 425;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_codec_intrinsic_inactive_feature_is_not_registered(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[1024];
    unsigned char feature_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlArena feature_arena = {0};
    SlEngineOptions options = v8_options();
    SlPlan plan = {0};
    SlRuntimeFeatureSet features = {0};
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0 ||
        init_arena(&feature_arena, feature_storage, sizeof(feature_storage)) != 0 ||
        attach_runtime_features(&options, &plan, &feature_arena, &features) != 0)
    {
        return 426;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 427;
    }

    if (expect_status(
            sl_engine_eval_source(engine, sl_str_from_cstr("v8-codec-inactive.js"),
                                  sl_str_from_cstr("globalThis.sloppy_codec_inactive = function "
                                                   "() {"
                                                   "  return globalThis.__sloppy.codec === "
                                                   "undefined"
                                                   "    ? 'codec-inactive-ok'"
                                                   "    : 'codec-unexpectedly-active';"
                                                   "};"),
                                  &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 428;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_codec_inactive"), &result,
                                               &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 429;
    }

    if (result.kind != SL_ENGINE_RESULT_TEXT ||
        !sl_str_equal(result.text, sl_str_from_cstr("codec-inactive-ok")))
    {
        sl_engine_destroy(engine);
        return 430;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_net_intrinsic_namespace_is_registered(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[1024];
    unsigned char feature_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlArena feature_arena = {0};
    SlEngineOptions options = v8_options();
    SlPlanRequiredFeature required = {.id = sl_str_from_cstr("stdlib.net")};
    SlPlan plan = {.required_features = &required, .required_feature_count = 1U};
    SlRuntimeFeatureSet features = {0};
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0 ||
        init_arena(&feature_arena, feature_storage, sizeof(feature_storage)) != 0 ||
        attach_runtime_features(&options, &plan, &feature_arena, &features) != 0)
    {
        return 431;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 432;
    }

    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("v8-net-active.js"),
                sl_str_from_cstr("globalThis.sloppy_net_active = function () {"
                                 "  const n = globalThis.__sloppy.net;"
                                 "  const names = ['connect', 'listen', 'accept', 'write',"
                                 "    'read', 'readLine', 'readUntil', 'close', 'abort',"
                                 "    'closeListener', 'abortListener'];"
                                 "  return n && names.every((name) => typeof n[name] === "
                                 "'function')"
                                 "    ? 'net-active-ok'"
                                 "    : 'net-missing';"
                                 "};"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 433;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_net_active"), &result,
                                               &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 434;
    }

    if (result.kind != SL_ENGINE_RESULT_TEXT ||
        !sl_str_equal(result.text, sl_str_from_cstr("net-active-ok")))
    {
        sl_engine_destroy(engine);
        return 435;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_net_intrinsic_inactive_feature_is_not_registered(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[1024];
    unsigned char feature_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlArena feature_arena = {0};
    SlEngineOptions options = v8_options();
    SlPlan plan = {0};
    SlRuntimeFeatureSet features = {0};
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0 ||
        init_arena(&feature_arena, feature_storage, sizeof(feature_storage)) != 0 ||
        attach_runtime_features(&options, &plan, &feature_arena, &features) != 0)
    {
        return 436;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 437;
    }

    if (expect_status(
            sl_engine_eval_source(engine, sl_str_from_cstr("v8-net-inactive.js"),
                                  sl_str_from_cstr("globalThis.sloppy_net_inactive = function () {"
                                                   "  return globalThis.__sloppy.net === "
                                                   "undefined"
                                                   "    ? 'net-inactive-ok'"
                                                   "    : 'net-unexpectedly-active';"
                                                   "};"),
                                  &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 438;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_net_inactive"), &result,
                                               &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 439;
    }

    if (result.kind != SL_ENGINE_RESULT_TEXT ||
        !sl_str_equal(result.text, sl_str_from_cstr("net-inactive-ok")))
    {
        sl_engine_destroy(engine);
        return 440;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_os_intrinsic_system_and_environment(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[1024];
    unsigned char feature_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlArena feature_arena = {0};
    SlEngineOptions options = v8_options();
    SlPlanRequiredFeature required = {.id = sl_str_from_cstr("stdlib.os")};
    SlPlan plan = {.required_features = &required, .required_feature_count = 1U};
    SlRuntimeFeatureSet features = {0};
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (set_test_env("SLOPPY_OS_V8_TEST", "v8-env-ok") != 0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0 ||
        init_arena(&feature_arena, feature_storage, sizeof(feature_storage)) != 0 ||
        attach_runtime_features(&options, &plan, &feature_arena, &features) != 0)
    {
        return 445;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 446;
    }

    if (expect_status(sl_engine_eval_source(
                          engine, sl_str_from_cstr("v8-os-active.js"),
                          sl_str_from_cstr("globalThis.sloppy_os_active = function () {"
                                           "  const os = globalThis.__sloppy.os;"
                                           "  if (!os || typeof os.systemInfo !== 'function') {"
                                           "    return 'os-missing';"
                                           "  }"
                                           "  const info = os.systemInfo();"
                                           "  const env = os.environmentGet('SLOPPY_OS_V8_TEST');"
                                           "  const has = os.environmentHas('SLOPPY_OS_V8_TEST');"
                                           "  const listed = os.environmentList('SLOPPY_OS_V8_')"
                                           "    .includes('SLOPPY_OS_V8_TEST');"
                                           "  return info.platform + ':' + info.arch + ':' +"
                                           "    (info.cpuCount > 0) + ':' + env + ':' + has + ':' +"
                                           "    listed;"
                                           "};"),
                          &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 447;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_os_active"), &result,
                                               &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 448;
    }

    if (result.kind != SL_ENGINE_RESULT_TEXT ||
        expect_str_contains(result.text, sl_str_from_cstr(":true:v8-env-ok:true:true")) != 0)
    {
        sl_engine_destroy(engine);
        return 449;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_os_intrinsic_inactive_feature_is_not_registered(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[1024];
    unsigned char feature_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlArena feature_arena = {0};
    SlEngineOptions options = v8_options();
    SlPlan plan = {0};
    SlRuntimeFeatureSet features = {0};
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0 ||
        init_arena(&feature_arena, feature_storage, sizeof(feature_storage)) != 0 ||
        attach_runtime_features(&options, &plan, &feature_arena, &features) != 0)
    {
        return 450;
    }
    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 451;
    }
    if (expect_status(
            sl_engine_eval_source(engine, sl_str_from_cstr("v8-os-inactive.js"),
                                  sl_str_from_cstr("globalThis.sloppy_os_inactive = function () {"
                                                   "  return globalThis.__sloppy.os === undefined"
                                                   "    ? 'os-inactive-ok'"
                                                   "    : 'os-unexpectedly-active';"
                                                   "};"),
                                  &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 452;
    }
    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_os_inactive"), &result,
                                               &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 453;
    }
    if (result.kind != SL_ENGINE_RESULT_TEXT ||
        !sl_str_equal(result.text, sl_str_from_cstr("os-inactive-ok")))
    {
        sl_engine_destroy(engine);
        return 454;
    }
    sl_engine_destroy(engine);
    return 0;
}

static int test_promise_result_settles_json_after_microtask(void)
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
        return 60;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 61;
    }

    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("v8-promise-json.js"),
                sl_str_from_cstr("globalThis.sloppy_json_promise = function () {"
                                 "  return Promise.resolve().then(() => ({ __sloppyResult: true, "
                                 "    kind: 'json', status: 200, contentType: "
                                 "    'application/json; charset=utf-8', body: { ok: true } }));"
                                 "};"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 62;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_json_promise"), &result,
                                               &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 63;
    }

    if (result.kind != SL_ENGINE_RESULT_JSON ||
        expect_bytes_equal(result.response.body, "{\"ok\":true}") != 0)
    {
        sl_engine_destroy(engine);
        return 64;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_promise_rejection_returns_diagnostic(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[1024];
    unsigned char json_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlArena json_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlStr json = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0 ||
        init_arena(&json_arena, json_storage, sizeof(json_storage)) != 0)
    {
        return 65;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 66;
    }

    if (expect_status(
            sl_engine_eval_source(engine, sl_str_from_cstr("v8-promise-reject.js"),
                                  sl_str_from_cstr("globalThis.sloppy_reject = async function () "
                                                   "{ throw new Error('async boom'); };"),
                                  &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 67;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_reject"), &result, &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        sl_engine_destroy(engine);
        return 68;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_ENGINE_PROMISE_REJECTION ||
        expect_str_contains(diag.message, sl_str_from_cstr("async boom")) != 0)
    {
        sl_engine_destroy(engine);
        return 69;
    }

    if (expect_status(sl_diag_render_json(&json_arena, &diag, &json), SL_STATUS_OK) != 0 ||
        !sl_str_equal(json, sl_str_from_cstr(
                                "{\"code\":\"SLOPPY_E_ENGINE_PROMISE_REJECTION\","
                                "\"severity\":\"error\","
                                "\"message\":\"JavaScript handler Promise rejected: Error: async "
                                "boom\","
                                "\"hints\":[\"Rejected async handlers produce a safe error "
                                "response.\"]}\n")))
    {
        sl_engine_destroy(engine);
        return 70;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_pending_promise_returns_deadline_diagnostic(void)
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
        return 70;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 71;
    }

    if (expect_status(sl_engine_eval_source(
                          engine, sl_str_from_cstr("v8-promise-pending.js"),
                          sl_str_from_cstr("globalThis.sloppy_pending = function () { return new "
                                           "Promise(() => {}); };"),
                          &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 72;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_pending"), &result, &diag),
                      SL_STATUS_DEADLINE_EXCEEDED) != 0)
    {
        sl_engine_destroy(engine);
        return 73;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_ENGINE_PROMISE_PENDING ||
        expect_str_contains(diag.message, sl_str_from_cstr("bounded microtask drain")) != 0)
    {
        sl_engine_destroy(engine);
        return 74;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_recursive_microtasks_are_bounded(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[2048];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 75;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 76;
    }

    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("v8-recursive-microtask.js"),
                sl_str_from_cstr("globalThis.sloppy_recursive_microtask = function () {"
                                 "  function loop() { return Promise.resolve().then(loop); }"
                                 "  return Promise.resolve().then(loop);"
                                 "};"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 77;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_recursive_microtask"),
                                               &result, &diag),
                      SL_STATUS_DEADLINE_EXCEEDED) != 0)
    {
        sl_engine_destroy(engine);
        return 78;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_ENGINE_PROMISE_PENDING ||
        expect_str_contains(diag.message, sl_str_from_cstr("bounded checkpoint limit")) != 0)
    {
        sl_engine_destroy(engine);
        return 79;
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

static int test_call_after_destroy_returns_lifecycle_diagnostic(void)
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
        return 65;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 66;
    }

    sl_engine_destroy(engine);
    sl_engine_destroy(engine);

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_smoke"), &result, &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        return 67;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_ENGINE_CALL_ERROR ||
        expect_str_contains(diag.message, sl_str_from_cstr("after dispose")) != 0)
    {
        return 68;
    }

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

static int test_request_context_exposes_headers_and_body(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[4096];
    static const unsigned char body[] = "{\"id\":123}";
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlHttpHeader headers[5];
    SlHttpRequestHead request = test_request(SL_HTTP_METHOD_POST);
    SlHttpRequestContext context = {0};

    headers[0].name = sl_str_from_cstr("Host");
    headers[0].value = sl_str_from_cstr("example");
    headers[1].name = sl_str_from_cstr("Content-Type");
    headers[1].value = sl_str_from_cstr("application/json; charset=utf-8");
    headers[2].name = sl_str_from_cstr("X-Trace");
    headers[2].value = sl_str_from_cstr("one");
    headers[3].name = sl_str_from_cstr("x-trace");
    headers[3].value = sl_str_from_cstr("two");
    headers[4].name = sl_str_from_cstr("Accept");
    headers[4].value = sl_str_from_cstr("application/json");
    request.headers = headers;
    request.header_count = 5U;
    request.body = sl_bytes_from_parts(body, sizeof(body) - 1U);
    context = test_request_context(&request);
    context.body_kind = SL_HTTP_REQUEST_BODY_JSON;

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 75;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 76;
    }

    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("v8-context-body.js"),
                sl_str_from_cstr("globalThis.sloppy_context_body = function (ctx) { return {"
                                 "  __sloppyResult: true, kind: 'json', status: 200,"
                                 "  contentType: 'application/json; charset=utf-8', body: {"
                                 "    contentType: ctx.request.headers.get('content-type'),"
                                 "    trace: ctx.request.headers.get('X-Trace'),"
                                 "    missing: ctx.request.headers.get('missing'),"
                                 "    entries: ctx.request.headers.entries(),"
                                 "    text: ctx.request.text(), json: ctx.request.json()"
                                 "  } }; };"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 77;
    }

    if (expect_status(sl_engine_call_function_with_context(engine, &result_arena,
                                                           sl_str_from_cstr("sloppy_context_body"),
                                                           &context, &result, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 78;
    }

    if (result.kind != SL_ENGINE_RESULT_JSON ||
        expect_bytes_equal(result.response.body,
                           "{\"contentType\":\"application/json; charset=utf-8\","
                           "\"trace\":\"one, two\",\"missing\":null,"
                           "\"entries\":[[\"host\",\"example\"],[\"content-type\","
                           "\"application/json; charset=utf-8\"],[\"x-trace\","
                           "\"one, two\"],[\"accept\",\"application/json\"]],"
                           "\"text\":\"{\\\"id\\\":123}\","
                           "\"json\":{\"id\":123}}") != 0)
    {
        sl_engine_destroy(engine);
        return 79;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_result_descriptor_copies_headers_and_location(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[2048];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 80;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 81;
    }

    if (expect_status(sl_engine_eval_source(
                          engine, sl_str_from_cstr("v8-result-headers.js"),
                          sl_str_from_cstr("globalThis.sloppy_result_headers = function () {"
                                           "  return { __sloppyResult: true, kind: 'json',"
                                           "    status: 201, contentType: "
                                           "    'application/json; charset=utf-8',"
                                           "    headers: { 'x-result': 'ok' },"
                                           "    location: '/users/1', body: { ok: true } };"
                                           "};"),
                          &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 82;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_result_headers"), &result,
                                               &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 83;
    }

    if (result.kind != SL_ENGINE_RESULT_JSON || result.response.status != 201U ||
        result.response.header_count != 2U ||
        expect_response_header(&result.response, "x-result", "ok") != 0 ||
        expect_response_header(&result.response, "Location", "/users/1") != 0 ||
        expect_bytes_equal(result.response.body, "{\"ok\":true}") != 0)
    {
        sl_engine_destroy(engine);
        return 84;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_invalid_result_headers_fail_safely(void)
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
        return 85;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 86;
    }

    if (expect_status(sl_engine_eval_source(
                          engine, sl_str_from_cstr("v8-invalid-result-headers.js"),
                          sl_str_from_cstr("globalThis.sloppy_invalid_headers = function () {"
                                           "  return { __sloppyResult: true, kind: 'text',"
                                           "    status: 200, contentType: "
                                           "    'text/plain; charset=utf-8',"
                                           "    headers: { 'Content-Length': '1' },"
                                           "    body: 'bad' };"
                                           "};"),
                          &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 87;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_invalid_headers"), &result,
                                               &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        sl_engine_destroy(engine);
        return 88;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_INVALID_HTTP_RESULT) {
        sl_engine_destroy(engine);
        return 89;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_request_context_exposes_signal_shape(void)
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
        return 75;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 76;
    }

    if (expect_status(sl_engine_eval_source(
                          engine, sl_str_from_cstr("v8-context-signal.js"),
                          sl_str_from_cstr("globalThis.sloppy_signal = function (ctx) {"
                                           "  ctx.signal.throwIfAborted();"
                                           "  return { __sloppyResult: true, kind: 'json', "
                                           "    status: 200, contentType: "
                                           "    'application/json; charset=utf-8',"
                                           "    body: { aborted: ctx.signal.aborted, reason: "
                                           "ctx.signal.reason, hasThrow: typeof "
                                           "ctx.signal.throwIfAborted, deadline: ctx.deadline } };"
                                           "};"),
                          &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 77;
    }

    if (expect_status(sl_engine_call_function_with_context(engine, &result_arena,
                                                           sl_str_from_cstr("sloppy_signal"),
                                                           &context, &result, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 78;
    }

    if (result.kind != SL_ENGINE_RESULT_JSON ||
        expect_bytes_equal(result.response.body,
                           "{\"aborted\":false,\"reason\":null,\"hasThrow\":\"function\","
                           "\"deadline\":null}") != 0)
    {
        sl_engine_destroy(engine);
        return 79;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_cancelled_request_fails_before_entering_handler(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlEngineResult called_result = {0};
    SlDiag diag = {0};
    SlHttpRequestHead request = test_request(SL_HTTP_METHOD_GET);
    SlHttpRequestContext context = test_request_context(&request);
    SlCancellationToken cancellation = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 80;
    }

    sl_cancellation_token_init(&cancellation);
    if (expect_status(sl_cancellation_token_cancel(&cancellation, SL_CANCELLATION_REASON_CANCELLED,
                                                   sl_str_from_cstr("test abort")),
                      SL_STATUS_OK) != 0)
    {
        return 81;
    }
    context.cancellation = &cancellation;

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 82;
    }

    if (expect_status(sl_engine_eval_source(
                          engine, sl_str_from_cstr("v8-cancelled.js"),
                          sl_str_from_cstr("globalThis.sloppy_called = false;"
                                           "globalThis.sloppy_cancelled = function () {"
                                           "  globalThis.sloppy_called = true; return 'bad';"
                                           "};"
                                           "globalThis.sloppy_was_called = function () {"
                                           "  return globalThis.sloppy_called ? 'yes' : 'no';"
                                           "};"),
                          &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 83;
    }

    if (expect_status(sl_engine_call_function_with_context(engine, &result_arena,
                                                           sl_str_from_cstr("sloppy_cancelled"),
                                                           &context, &result, &diag),
                      SL_STATUS_CANCELLED) != 0)
    {
        sl_engine_destroy(engine);
        return 84;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_ENGINE_CANCELLED ||
        expect_str_contains(diag.message, sl_str_from_cstr("test abort")) != 0)
    {
        sl_engine_destroy(engine);
        return 85;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_was_called"),
                                               &called_result, &diag),
                      SL_STATUS_OK) != 0 ||
        !sl_str_equal(called_result.text, sl_str_from_cstr("no")))
    {
        sl_engine_destroy(engine);
        return 86;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_deadline_cancelled_request_returns_deadline_status(void)
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
    SlCancellationToken cancellation = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 87;
    }

    sl_cancellation_token_init(&cancellation);
    if (expect_status(sl_cancellation_token_cancel(&cancellation,
                                                   SL_CANCELLATION_REASON_DEADLINE_EXCEEDED,
                                                   sl_str_from_cstr("handler deadline elapsed")),
                      SL_STATUS_OK) != 0)
    {
        return 88;
    }
    context.cancellation = &cancellation;

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 89;
    }

    if (expect_status(sl_engine_eval_source(engine, sl_str_from_cstr("v8-deadline.js"),
                                            sl_str_from_cstr("globalThis.sloppy_deadline = "
                                                             "function () { return 'bad'; };"),
                                            &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 90;
    }

    if (expect_status(sl_engine_call_function_with_context(engine, &result_arena,
                                                           sl_str_from_cstr("sloppy_deadline"),
                                                           &context, &result, &diag),
                      SL_STATUS_DEADLINE_EXCEEDED) != 0)
    {
        sl_engine_destroy(engine);
        return 91;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_ENGINE_CANCELLED ||
        expect_str_contains(diag.message, sl_str_from_cstr("deadline")) != 0)
    {
        sl_engine_destroy(engine);
        return 92;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_backpressure_request_returns_capacity_status(void)
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
    SlCancellationToken cancellation = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 101;
    }

    sl_cancellation_token_init(&cancellation);
    if (expect_status(sl_cancellation_token_cancel(&cancellation,
                                                   SL_CANCELLATION_REASON_BACKPRESSURE,
                                                   sl_str_from_cstr("queue is full")),
                      SL_STATUS_OK) != 0)
    {
        return 102;
    }
    context.cancellation = &cancellation;

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 103;
    }

    if (expect_status(sl_engine_eval_source(engine, sl_str_from_cstr("v8-backpressure.js"),
                                            sl_str_from_cstr("globalThis.sloppy_backpressure = "
                                                             "function () { return 'bad'; };"),
                                            &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 104;
    }

    if (expect_status(sl_engine_call_function_with_context(engine, &result_arena,
                                                           sl_str_from_cstr("sloppy_backpressure"),
                                                           &context, &result, &diag),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0)
    {
        sl_engine_destroy(engine);
        return 105;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_ENGINE_BACKPRESSURE ||
        expect_str_contains(diag.message, sl_str_from_cstr("queue is full")) != 0)
    {
        sl_engine_destroy(engine);
        return 106;
    }

    sl_engine_destroy(engine);
    return 0;
}

typedef struct SlV8ScopeHarness
{
    SlEngine* engine;
    SlArena* result_arena;
    SlStr function_name;
    const SlHttpRequestContext* context;
    SlEngineResult* result;
    int* cleanup_count;
} SlV8ScopeHarness;

static void count_scope_cleanup(void* payload, void* user)
{
    int* cleanup_count = (int*)payload;

    (void)user;
    if (cleanup_count != NULL) {
        *cleanup_count += 1;
    }
}

static SlStatus call_v8_handler_in_request_scope(SlAppRequestScope* request_scope, void* user,
                                                 SlDiag* out_diag)
{
    SlV8ScopeHarness* harness = (SlV8ScopeHarness*)user;
    SlStatus status;

    if (request_scope == NULL || harness == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_app_request_scope_add_cleanup(request_scope, count_scope_cleanup,
                                              harness->cleanup_count, NULL);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_engine_call_function_with_context(harness->engine, harness->result_arena,
                                                harness->function_name, harness->context,
                                                harness->result, out_diag);
}

static int run_scope_cleanup_case(SlEngine* engine, SlArena* result_arena, SlStr function_name,
                                  const SlHttpRequestContext* context, SlStatusCode expected_status,
                                  SlDiagCode expected_diag)
{
    SlScopeCleanup cleanups[1];
    SlEngineResult result = {0};
    SlDiag diag = {0};
    int cleanup_count = 0;
    SlV8ScopeHarness harness = {0};
    SlStatus status;

    harness.engine = engine;
    harness.result_arena = result_arena;
    harness.function_name = function_name;
    harness.context = context;
    harness.result = &result;
    harness.cleanup_count = &cleanup_count;

    status = sl_app_request_scope_execute(cleanups, 1U, call_v8_handler_in_request_scope, &harness,
                                          &diag);
    if (expect_status(status, expected_status) != 0 || cleanup_count != 1) {
        return 1;
    }
    if (expected_diag != SL_DIAG_NONE && diag.code != expected_diag) {
        return 2;
    }

    return 0;
}

static int test_request_scope_cleanup_runs_for_async_outcomes(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[2048];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlDiag diag = {0};
    SlHttpRequestHead request = test_request(SL_HTTP_METHOD_GET);
    SlHttpRequestContext context = test_request_context(&request);
    SlHttpRequestContext cancelled_context = test_request_context(&request);
    SlCancellationToken cancellation = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 93;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 94;
    }

    if (expect_status(sl_engine_eval_source(
                          engine, sl_str_from_cstr("v8-scope-cleanup.js"),
                          sl_str_from_cstr("globalThis.sloppy_scope_resolve = async function () { "
                                           "return 'scope-ok'; };"
                                           "globalThis.sloppy_scope_throw = function () { throw "
                                           "new Error('scope throw'); };"
                                           "globalThis.sloppy_scope_reject = async function () { "
                                           "throw new Error('scope reject'); };"
                                           "globalThis.sloppy_scope_pending = function () { return "
                                           "new Promise(() => {}); };"
                                           "globalThis.sloppy_scope_cancel = function () { return "
                                           "'bad'; };"),
                          &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 95;
    }

    if (run_scope_cleanup_case(engine, &result_arena, sl_str_from_cstr("sloppy_scope_resolve"),
                               &context, SL_STATUS_OK, SL_DIAG_NONE) != 0)
    {
        sl_engine_destroy(engine);
        return 96;
    }

    if (run_scope_cleanup_case(engine, &result_arena, sl_str_from_cstr("sloppy_scope_throw"),
                               &context, SL_STATUS_INVALID_STATE, SL_DIAG_ENGINE_EXCEPTION) != 0)
    {
        sl_engine_destroy(engine);
        return 97;
    }

    if (run_scope_cleanup_case(engine, &result_arena, sl_str_from_cstr("sloppy_scope_reject"),
                               &context, SL_STATUS_INVALID_STATE,
                               SL_DIAG_ENGINE_PROMISE_REJECTION) != 0)
    {
        sl_engine_destroy(engine);
        return 98;
    }

    if (run_scope_cleanup_case(engine, &result_arena, sl_str_from_cstr("sloppy_scope_pending"),
                               &context, SL_STATUS_DEADLINE_EXCEEDED,
                               SL_DIAG_ENGINE_PROMISE_PENDING) != 0)
    {
        sl_engine_destroy(engine);
        return 99;
    }

    sl_cancellation_token_init(&cancellation);
    if (expect_status(sl_cancellation_token_cancel(&cancellation, SL_CANCELLATION_REASON_CANCELLED,
                                                   sl_str_from_cstr("scope cancel")),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 100;
    }
    cancelled_context.cancellation = &cancellation;
    if (run_scope_cleanup_case(engine, &result_arena, sl_str_from_cstr("sloppy_scope_cancel"),
                               &cancelled_context, SL_STATUS_CANCELLED,
                               SL_DIAG_ENGINE_CANCELLED) != 0)
    {
        sl_engine_destroy(engine);
        return 101;
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

static int test_sqlite_intrinsics_execute_query_and_close(void)
{
    unsigned char engine_storage[16384];
    unsigned char result_storage[4096];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlPlan plan = {0};
    SlPlanDataProvider providers[1];
    SlPlanCapability capabilities[1];
    SlCapabilityRegistry registry = {0};
    SlRuntimeFeatureSet features = {0};
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 130;
    }

    if (attach_sqlite_plan(&options, &plan, &registry, providers, capabilities, &engine_arena,
                           &features, "readwrite") != 0)
    {
        return 131;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 132;
    }

    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("sqlite-bridge.js"),
                sl_str_from_cstr(
                    "globalThis.sqliteSmoke = function () {"
                    "  const db = __sloppy.data.sqlite.open({ provider: 'sqlite', database: "
                    "':memory:', capability: 'data.main' });"
                    "  __sloppy.data.sqlite.exec(db, 'create table users (id integer primary key, "
                    "name text not null, raw blob)', []);"
                    "  __sloppy.data.sqlite.exec(db, \"insert into users (name, raw) values (?, "
                    "x'0041ff')\", "
                    "['Ada']);"
                    "  __sloppy.data.sqlite.transactionBegin(db);"
                    "  __sloppy.data.sqlite.transactionExec(db, \"insert into users (name, raw) "
                    "values (?, x'01')\", ['Grace']);"
                    "  __sloppy.data.sqlite.transactionCommit(db);"
                    "  __sloppy.data.sqlite.transactionBegin(db);"
                    "  __sloppy.data.sqlite.transactionExec(db, \"insert into users (name, raw) "
                    "values (?, x'02')\", ['Rollback']);"
                    "  __sloppy.data.sqlite.transactionRollback(db);"
                    "  const row = __sloppy.data.sqlite.queryOne(db, 'select name, raw from users "
                    "where id = ?', [1]);"
                    "  const rows = __sloppy.data.sqlite.query(db, 'select name from users order "
                    "by id', []);"
                    "  const typed = __sloppy.data.sqlite.queryOne(db, 'select typeof(?) as kind', "
                    "[9007199254740991]);"
                    "  const raw = Array.from(row.raw);"
                    "  __sloppy.data.sqlite.close(db);"
                    "  return { __sloppyResult: true, kind: 'json', status: 200, "
                    "    contentType: 'application/json; charset=utf-8', body: { rowName: "
                    "row.name, rawIsBytes: row.raw instanceof Uint8Array, raw, rows, typed } };"
                    "};"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 133;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sqliteSmoke"), &result, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 134;
    }

    if (result.kind != SL_ENGINE_RESULT_JSON ||
        expect_bytes_equal(result.response.body,
                           "{\"rowName\":\"Ada\",\"rawIsBytes\":true,\"raw\":[0,65,255],"
                           "\"rows\":[{\"name\":\"Ada\"},{\"name\":\"Grace\"}],"
                           "\"typed\":{\"kind\":\"integer\"}}") != 0)
    {
        sl_engine_destroy(engine);
        return 135;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_sqlite_intrinsic_stale_handle_fails(void)
{
    unsigned char engine_storage[16384];
    unsigned char result_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlPlan plan = {0};
    SlPlanDataProvider providers[1];
    SlPlanCapability capabilities[1];
    SlCapabilityRegistry registry = {0};
    SlRuntimeFeatureSet features = {0};
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 140;
    }

    if (attach_sqlite_plan(&options, &plan, &registry, providers, capabilities, &engine_arena,
                           &features, "readwrite") != 0)
    {
        return 141;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 142;
    }

    if (expect_status(sl_engine_eval_source(
                          engine, sl_str_from_cstr("sqlite-stale.js"),
                          sl_str_from_cstr("globalThis.sqliteStale = function () {"
                                           "  const db = __sloppy.data.sqlite.open({ provider: "
                                           "'data.main' });"
                                           "  __sloppy.data.sqlite.close(db);"
                                           "  __sloppy.data.sqlite.queryOne(db, 'select 1', []);"
                                           "};"),
                          &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 143;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sqliteStale"), &result, &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        sl_engine_destroy(engine);
        return 144;
    }

    if (diag.code != SL_DIAG_ENGINE_EXCEPTION ||
        expect_str_contains(diag.message, sl_str_from_cstr("resource id is stale")) != 0)
    {
        sl_engine_destroy(engine);
        return 145;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_sqlite_intrinsic_invalid_arguments_fail(void)
{
    unsigned char engine_storage[16384];
    unsigned char result_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlPlan plan = {0};
    SlPlanDataProvider providers[1];
    SlPlanCapability capabilities[1];
    SlCapabilityRegistry registry = {0};
    SlRuntimeFeatureSet features = {0};
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 150;
    }

    if (attach_sqlite_plan(&options, &plan, &registry, providers, capabilities, &engine_arena,
                           &features, "readwrite") != 0)
    {
        return 151;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 152;
    }

    if (expect_status(sl_engine_eval_source(
                          engine, sl_str_from_cstr("sqlite-invalid.js"),
                          sl_str_from_cstr("globalThis.sqliteInvalid = function () {"
                                           "  const db = __sloppy.data.sqlite.open({ provider: "
                                           "'data.main' });"
                                           "  try {"
                                           "    __sloppy.data.sqlite.exec(db, 'select ?', [{}]);"
                                           "  } finally {"
                                           "    __sloppy.data.sqlite.close(db);"
                                           "  }"
                                           "};"),
                          &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 153;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sqliteInvalid"), &result, &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        sl_engine_destroy(engine);
        return 154;
    }

    if (diag.code != SL_DIAG_ENGINE_EXCEPTION ||
        expect_str_contains(diag.message, sl_str_from_cstr("parameters support only")) != 0)
    {
        sl_engine_destroy(engine);
        return 155;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_sqlite_intrinsic_rejects_huge_parameter_array_before_reserve(void)
{
    unsigned char engine_storage[16384];
    unsigned char result_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlPlan plan = {0};
    SlPlanDataProvider providers[1];
    SlPlanCapability capabilities[1];
    SlCapabilityRegistry registry = {0};
    SlRuntimeFeatureSet features = {0};
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 156;
    }

    if (attach_sqlite_plan(&options, &plan, &registry, providers, capabilities, &engine_arena,
                           &features, "readwrite") != 0)
    {
        return 157;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 158;
    }

    if (expect_status(sl_engine_eval_source(
                          engine, sl_str_from_cstr("sqlite-huge-params.js"),
                          sl_str_from_cstr(
                              "globalThis.sqliteHugeParams = function () {"
                              "  const db = __sloppy.data.sqlite.open({ provider: 'data.main' });"
                              "  try {"
                              "    const params = new Array(32767);"
                              "    __sloppy.data.sqlite.exec(db, 'select ?', params);"
                              "  } finally {"
                              "    __sloppy.data.sqlite.close(db);"
                              "  }"
                              "};"),
                          &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 159;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sqliteHugeParams"), &result,
                                               &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        sl_engine_destroy(engine);
        return 1591;
    }

    if (diag.code != SL_DIAG_ENGINE_EXCEPTION ||
        expect_str_contains(diag.message,
                            sl_str_from_cstr("sqlite parameter array exceeds supported "
                                             "parameter count")) != 0)
    {
        sl_engine_destroy(engine);
        return 1592;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_sqlite_intrinsic_missing_provider_fails(void)
{
    unsigned char engine_storage[16384];
    unsigned char result_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlPlan plan = {0};
    SlPlanDataProvider providers[1];
    SlPlanCapability capabilities[1];
    SlCapabilityRegistry registry = {0};
    SlRuntimeFeatureSet features = {0};
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 160;
    }

    if (attach_sqlite_plan(&options, &plan, &registry, providers, capabilities, &engine_arena,
                           &features, "readwrite") != 0)
    {
        return 161;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 162;
    }

    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("sqlite-missing-provider.js"),
                sl_str_from_cstr("globalThis.sqliteMissingProvider = function () {"
                                 "  __sloppy.data.sqlite.open({ provider: 'data.missing' });"
                                 "};"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 163;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sqliteMissingProvider"), &result,
                                               &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        sl_engine_destroy(engine);
        return 164;
    }

    if (diag.code != SL_DIAG_ENGINE_EXCEPTION ||
        expect_str_contains(diag.message, sl_str_from_cstr("sqlite provider is not configured")) !=
            0)
    {
        sl_engine_destroy(engine);
        return 165;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_sqlite_intrinsic_missing_capability_registry_fails_closed(void)
{
    unsigned char engine_storage[16384];
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
        return 170;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 171;
    }

    if (expect_status(sl_engine_eval_source(
                          engine, sl_str_from_cstr("sqlite-missing-registry.js"),
                          sl_str_from_cstr("globalThis.sqliteMissingRegistry = function () {"
                                           "  __sloppy.data.sqlite.open({ database: ':memory:', "
                                           "capability: 'data.main' });"
                                           "};"),
                          &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 172;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sqliteMissingRegistry"), &result,
                                               &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        sl_engine_destroy(engine);
        return 173;
    }

    if (diag.code != SL_DIAG_ENGINE_EXCEPTION ||
        expect_str_contains(diag.message, sl_str_from_cstr("capability registry is unavailable")) !=
            0)
    {
        sl_engine_destroy(engine);
        return 174;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_sqlite_intrinsic_inactive_feature_is_not_registered(void)
{
    unsigned char engine_storage[16384];
    unsigned char result_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlPlanHandler handler = {.id = 1U,
                             .export_name = sl_str_from_cstr("__sloppy_handler_1"),
                             .display_name = sl_str_from_cstr("Home")};
    SlPlanRoute route = {.method = sl_str_from_cstr("GET"),
                         .pattern = sl_str_from_cstr("/"),
                         .handler_id = 1U,
                         .name = sl_str_from_cstr("Home")};
    SlPlan plan = {.version = SL_PLAN_CURRENT_VERSION,
                   .target = {.platform = sl_str_from_cstr(SL_PLAN_TARGET_PLATFORM_WINDOWS_X64),
                              .engine = sl_str_from_cstr(SL_PLAN_TARGET_ENGINE_V8)},
                   .handlers = &handler,
                   .handler_count = 1U,
                   .routes = &route,
                   .route_count = 1U};
    SlRuntimeFeatureSet features = {0};
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 176;
    }

    if (attach_runtime_features(&options, &plan, &engine_arena, &features) != 0) {
        return 177;
    }
    if (sl_runtime_feature_set_contains(&features, SL_RUNTIME_FEATURE_PROVIDER_SQLITE)) {
        return 178;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 179;
    }

    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("sqlite-inactive-feature.js"),
                sl_str_from_cstr("globalThis.sqliteInactive = function () {"
                                 "  if (globalThis.__sloppy && globalThis.__sloppy.data && "
                                 "globalThis.__sloppy.data.sqlite) {"
                                 "    throw new Error('sqlite intrinsic unexpectedly active');"
                                 "  }"
                                 "  throw new Error('sqlite intrinsic inactive');"
                                 "};"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 180;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sqliteInactive"), &result, &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        sl_engine_destroy(engine);
        return 181;
    }

    if (diag.code != SL_DIAG_ENGINE_EXCEPTION ||
        expect_str_contains(diag.message, sl_str_from_cstr("sqlite intrinsic inactive")) != 0 ||
        expect_str_contains(diag.message, sl_str_from_cstr("unexpectedly active")) == 0)
    {
        sl_engine_destroy(engine);
        return 182;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_sqlite_intrinsic_denied_capability_fails_before_read(void)
{
    unsigned char engine_storage[16384];
    unsigned char result_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlPlan plan = {0};
    SlPlanDataProvider providers[1];
    SlPlanCapability capabilities[1];
    SlCapabilityRegistry registry = {0};
    SlRuntimeFeatureSet features = {0};
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 180;
    }

    if (attach_sqlite_plan(&options, &plan, &registry, providers, capabilities, &engine_arena,
                           &features, "write") != 0)
    {
        return 181;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 182;
    }

    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("sqlite-denied.js"),
                sl_str_from_cstr(
                    "globalThis.sqliteDenied = function () {"
                    "  const db = __sloppy.data.sqlite.open({ provider: 'sqlite', database: "
                    "':memory:', capability: 'data.main', access: 'write' });"
                    "  try {"
                    "    __sloppy.data.sqlite.exec(db, 'create table users (id integer)', []);"
                    "    __sloppy.data.sqlite.queryOne(db, 'select id from users', []);"
                    "  } finally {"
                    "    __sloppy.data.sqlite.close(db);"
                    "  }"
                    "};"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 183;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sqliteDenied"), &result, &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        sl_engine_destroy(engine);
        return 184;
    }

    if (diag.code != SL_DIAG_ENGINE_EXCEPTION ||
        expect_str_contains(diag.message,
                            sl_str_from_cstr("capability access denied: insufficient handle "
                                             "access")) != 0 ||
        expect_str_contains(diag.message, sl_str_from_cstr("operation: read")) != 0)
    {
        sl_engine_destroy(engine);
        return 185;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_sqlite_intrinsic_read_capability_cannot_open_for_write(void)
{
    unsigned char engine_storage[16384];
    unsigned char result_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlPlan plan = {0};
    SlPlanDataProvider providers[1];
    SlPlanCapability capabilities[1];
    SlCapabilityRegistry registry = {0};
    SlRuntimeFeatureSet features = {0};
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 190;
    }

    providers[0] = (SlPlanDataProvider){
        .token = sl_str_from_cstr("data.main"),
        .provider = sl_str_from_cstr("sqlite"),
        .capability = sl_str_from_cstr("data.read"),
        .service = sl_str_empty(),
        .database = sl_str_from_cstr(":memory:"),
    };
    capabilities[0] = (SlPlanCapability){
        .token = sl_str_from_cstr("data.read"),
        .kind = sl_str_from_cstr("database"),
        .access = sl_str_from_cstr("read"),
        .provider = sl_str_from_cstr("data.main"),
    };
    plan = (SlPlan){
        .data_providers = providers,
        .data_provider_count = 1U,
        .capabilities = capabilities,
        .capability_count = 1U,
    };
    if (expect_status(sl_capability_registry_init_from_plan(&plan, &registry), SL_STATUS_OK) != 0) {
        return 191;
    }
    options.plan = &plan;
    options.capabilities = &registry;
    if (attach_runtime_features(&options, &plan, &engine_arena, &features) != 0) {
        return 192;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 193;
    }

    if (expect_status(sl_engine_eval_source(
                          engine, sl_str_from_cstr("sqlite-denied-open.js"),
                          sl_str_from_cstr("globalThis.sqliteDeniedOpen = function () {"
                                           "  __sloppy.data.sqlite.open({ provider: 'sqlite', "
                                           "database: ':memory:', capability: 'data.read' });"
                                           "};"),
                          &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 194;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sqliteDeniedOpen"), &result,
                                               &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        sl_engine_destroy(engine);
        return 194;
    }

    if (diag.code != SL_DIAG_ENGINE_EXCEPTION ||
        expect_str_contains(
            diag.message, sl_str_from_cstr("capability access denied: insufficient access")) != 0 ||
        expect_str_contains(diag.message, sl_str_from_cstr("operation: readwrite")) != 0)
    {
        sl_engine_destroy(engine);
        return 195;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_sqlite_intrinsic_provider_shorthand_read_capability_keeps_readwrite_default(void)
{
    unsigned char engine_storage[16384];
    unsigned char result_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlPlan plan = {0};
    SlPlanDataProvider providers[1];
    SlPlanCapability capabilities[1];
    SlCapabilityRegistry registry = {0};
    SlRuntimeFeatureSet features = {0};
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 1960;
    }

    providers[0] = (SlPlanDataProvider){
        .token = sl_str_from_cstr("data.main"),
        .provider = sl_str_from_cstr("sqlite"),
        .capability = sl_str_from_cstr("data.read"),
        .service = sl_str_empty(),
        .database = sl_str_from_cstr(":memory:"),
    };
    capabilities[0] = (SlPlanCapability){
        .token = sl_str_from_cstr("data.read"),
        .kind = sl_str_from_cstr("database"),
        .access = sl_str_from_cstr("read"),
        .provider = sl_str_from_cstr("data.main"),
    };
    plan = (SlPlan){
        .data_providers = providers,
        .data_provider_count = 1U,
        .capabilities = capabilities,
        .capability_count = 1U,
    };
    if (expect_status(sl_capability_registry_init_from_plan(&plan, &registry), SL_STATUS_OK) != 0) {
        return 1961;
    }
    options.plan = &plan;
    options.capabilities = &registry;
    if (attach_runtime_features(&options, &plan, &engine_arena, &features) != 0) {
        return 1962;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 1963;
    }

    if (expect_status(sl_engine_eval_source(
                          engine, sl_str_from_cstr("sqlite-shorthand-denied-open.js"),
                          sl_str_from_cstr("globalThis.sqliteShorthandDeniedOpen = function () {"
                                           "  __sloppy.data.sqlite.open({ provider: 'data.main' });"
                                           "};"),
                          &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 1964;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sqliteShorthandDeniedOpen"),
                                               &result, &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        sl_engine_destroy(engine);
        return 1964;
    }

    if (diag.code != SL_DIAG_ENGINE_EXCEPTION ||
        expect_str_contains(
            diag.message, sl_str_from_cstr("capability access denied: insufficient access")) != 0 ||
        expect_str_contains(diag.message, sl_str_from_cstr("operation: readwrite")) != 0)
    {
        sl_engine_destroy(engine);
        return 1965;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_sqlite_intrinsic_explicit_write_capability_opens_write_only_handle(void)
{
    unsigned char engine_storage[16384];
    unsigned char result_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlPlan plan = {0};
    SlPlanDataProvider providers[1];
    SlPlanCapability capabilities[1];
    SlCapabilityRegistry registry = {0};
    SlRuntimeFeatureSet features = {0};
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 200;
    }

    if (attach_sqlite_plan(&options, &plan, &registry, providers, capabilities, &engine_arena,
                           &features, "write") != 0)
    {
        return 201;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 202;
    }

    if (expect_status(sl_engine_eval_source(
                          engine, sl_str_from_cstr("sqlite-write-only.js"),
                          sl_str_from_cstr("globalThis.sqliteWriteOnly = function () {"
                                           "  const db = __sloppy.data.sqlite.open({ provider: "
                                           "'sqlite', database: ':memory:', capability: "
                                           "'data.main', access: 'write' });"
                                           "  try {"
                                           "    __sloppy.data.sqlite.exec(db, 'create table t "
                                           "(id integer)', []);"
                                           "    __sloppy.data.sqlite.query(db, 'select id from t', "
                                           "[]);"
                                           "  } finally {"
                                           "    __sloppy.data.sqlite.close(db);"
                                           "  }"
                                           "};"),
                          &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 203;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sqliteWriteOnly"), &result, &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        sl_engine_destroy(engine);
        return 204;
    }

    if (diag.code != SL_DIAG_ENGINE_EXCEPTION ||
        expect_str_contains(diag.message, sl_str_from_cstr("insufficient handle access")) != 0 ||
        expect_str_contains(diag.message, sl_str_from_cstr("actual access: write")) != 0 ||
        expect_str_contains(diag.message, sl_str_from_cstr("operation: read")) != 0)
    {
        sl_engine_destroy(engine);
        return 205;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_sqlite_intrinsic_provider_kind_mismatch_fails_before_open(void)
{
    unsigned char engine_storage[16384];
    unsigned char result_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlPlanDataProvider provider = {
        .token = sl_str_from_cstr("data.main"),
        .provider = sl_str_from_cstr("postgres"),
        .capability = sl_str_from_cstr("data.main"),
        .service = sl_str_empty(),
        .database = sl_str_from_cstr(":memory:"),
    };
    SlPlanCapability capability = {
        .token = sl_str_from_cstr("data.main"),
        .kind = sl_str_from_cstr("database"),
        .access = sl_str_from_cstr("readwrite"),
        .provider = sl_str_from_cstr("data.main"),
    };
    SlPlan plan = {.data_providers = &provider,
                   .data_provider_count = 1U,
                   .capabilities = &capability,
                   .capability_count = 1U};
    SlCapabilityRegistry registry = {0};
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 210;
    }

    if (expect_status(sl_capability_registry_init_from_plan(&plan, &registry), SL_STATUS_OK) != 0) {
        return 211;
    }
    options.plan = &plan;
    options.capabilities = &registry;

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 213;
    }

    if (expect_status(sl_engine_eval_source(
                          engine, sl_str_from_cstr("sqlite-provider-kind.js"),
                          sl_str_from_cstr("globalThis.sqliteProviderKind = function () {"
                                           "  __sloppy.data.sqlite.open({ provider: 'data.main' });"
                                           "};"),
                          &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 214;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sqliteProviderKind"), &result,
                                               &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        sl_engine_destroy(engine);
        return 214;
    }

    if (diag.code != SL_DIAG_ENGINE_EXCEPTION ||
        expect_str_contains(diag.message, sl_str_from_cstr("non-sqlite provider")) != 0)
    {
        sl_engine_destroy(engine);
        return 215;
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

    result = test_v8_string_interop_uses_explicit_lengths();
    if (result != 0) {
        return result;
    }

    result = test_v8_string_interop_copies_utf8_to_native_result();
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

    result = test_throwing_function_remaps_source_map_location();
    if (result != 0) {
        return result;
    }

    result = test_source_map_original_column_does_not_add_generated_delta();
    if (result != 0) {
        return result;
    }

    result = test_unmapped_source_map_segment_reports_generated_location();
    if (result != 0) {
        return result;
    }

    result = test_malformed_source_map_reports_generated_location();
    if (result != 0) {
        return result;
    }

    result = test_registered_handler_throw_remaps_source_map_location();
    if (result != 0) {
        return result;
    }

    result = test_unsupported_result_returns_call_diagnostic();
    if (result != 0) {
        return result;
    }

    result = test_promise_result_settles_text();
    if (result != 0) {
        return result;
    }

    result = test_time_intrinsic_delay_settles_on_owner_thread();
    if (result != 0) {
        return result;
    }

    result = test_time_intrinsic_inactive_feature_is_not_registered();
    if (result != 0) {
        return result;
    }

    result = test_crypto_intrinsic_hash_hmac_random_and_constant_time();
    if (result != 0) {
        return result;
    }

    result = test_crypto_intrinsic_inactive_feature_is_not_registered();
    if (result != 0) {
        return result;
    }

    result = test_codec_intrinsic_namespace_registered_when_active();
    if (result != 0) {
        return result;
    }

    result = test_codec_intrinsic_inactive_feature_is_not_registered();
    if (result != 0) {
        return result;
    }

    result = test_net_intrinsic_namespace_is_registered();
    if (result != 0) {
        return result;
    }

    result = test_net_intrinsic_inactive_feature_is_not_registered();
    if (result != 0) {
        return result;
    }

    result = test_os_intrinsic_system_and_environment();
    if (result != 0) {
        return result;
    }

    result = test_os_intrinsic_inactive_feature_is_not_registered();
    if (result != 0) {
        return result;
    }

    result = test_promise_result_settles_json_after_microtask();
    if (result != 0) {
        return result;
    }

    result = test_promise_rejection_returns_diagnostic();
    if (result != 0) {
        return result;
    }

    result = test_pending_promise_returns_deadline_diagnostic();
    if (result != 0) {
        return result;
    }

    result = test_recursive_microtasks_are_bounded();
    if (result != 0) {
        return result;
    }

    result = test_create_destroy_create_reuses_process_platform();
    if (result != 0) {
        return result;
    }

    result = test_call_after_destroy_returns_lifecycle_diagnostic();
    if (result != 0) {
        return result;
    }

    result = test_request_context_reports_actual_method();
    if (result != 0) {
        return result;
    }

    result = test_request_context_exposes_headers_and_body();
    if (result != 0) {
        return result;
    }

    result = test_result_descriptor_copies_headers_and_location();
    if (result != 0) {
        return result;
    }

    result = test_invalid_result_headers_fail_safely();
    if (result != 0) {
        return result;
    }

    result = test_request_context_exposes_signal_shape();
    if (result != 0) {
        return result;
    }

    result = test_cancelled_request_fails_before_entering_handler();
    if (result != 0) {
        return result;
    }

    result = test_deadline_cancelled_request_returns_deadline_status();
    if (result != 0) {
        return result;
    }

    result = test_backpressure_request_returns_capacity_status();
    if (result != 0) {
        return result;
    }

    result = test_request_scope_cleanup_runs_for_async_outcomes();
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

    result = test_missing_registered_handler_returns_diagnostic();
    if (result != 0) {
        return result;
    }

    result = test_sqlite_intrinsics_execute_query_and_close();
    if (result != 0) {
        return result;
    }

    result = test_sqlite_intrinsic_stale_handle_fails();
    if (result != 0) {
        return result;
    }

    result = test_sqlite_intrinsic_invalid_arguments_fail();
    if (result != 0) {
        return result;
    }

    result = test_sqlite_intrinsic_rejects_huge_parameter_array_before_reserve();
    if (result != 0) {
        return result;
    }

    result = test_sqlite_intrinsic_missing_provider_fails();
    if (result != 0) {
        return result;
    }

    result = test_sqlite_intrinsic_missing_capability_registry_fails_closed();
    if (result != 0) {
        return result;
    }

    result = test_sqlite_intrinsic_inactive_feature_is_not_registered();
    if (result != 0) {
        return result;
    }

    result = test_sqlite_intrinsic_denied_capability_fails_before_read();
    if (result != 0) {
        return result;
    }

    result = test_sqlite_intrinsic_read_capability_cannot_open_for_write();
    if (result != 0) {
        return result;
    }

    result = test_sqlite_intrinsic_provider_shorthand_read_capability_keeps_readwrite_default();
    if (result != 0) {
        return result;
    }

    result = test_sqlite_intrinsic_explicit_write_capability_opens_write_only_handle();
    if (result != 0) {
        return result;
    }

    result = test_sqlite_intrinsic_provider_kind_mismatch_fails_before_open();
    if (result != 0) {
        return result;
    }

    return test_filesystem_intrinsic_promise_roundtrip();
}

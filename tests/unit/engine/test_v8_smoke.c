#include "sloppy/app_host.h"
#include "sloppy/engine.h"
#include "sloppy/fs.h"
#include "sloppy/http_profile.h"
#include "sloppy/logging.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

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

static int expect_str_equal(SlStr actual, SlStr expected)
{
    return expect_true(sl_str_equal(actual, expected));
}

static int expect_bytes_equal(SlBytes actual, const char* expected)
{
    size_t expected_length = strlen(expected);

    return expect_true(actual.length == expected_length && actual.ptr != NULL &&
                       memcmp(actual.ptr, expected, expected_length) == 0);
}

static int expect_bytes_exact(SlBytes actual, const unsigned char* expected, size_t expected_length)
{
    return expect_true(
        actual.length == expected_length && (actual.ptr != NULL || expected_length == 0U) &&
        (expected_length == 0U || memcmp(actual.ptr, expected, expected_length) == 0));
}

static int set_test_env(const char* key, const char* value)
{
#ifdef _WIN32
    return _putenv_s(key, value);
#else
    return setenv(key, value, 1);
#endif
}

static int unset_test_env(const char* key)
{
#ifdef _WIN32
    return _putenv_s(key, "");
#else
    return unsetenv(key);
#endif
}

typedef struct TestEnvValue
{
    bool had_value;
    char value[512];
} TestEnvValue;

static int capture_test_env(const char* key, TestEnvValue* out)
{
    size_t length;

    if (key == NULL || out == NULL) {
        return 1;
    }

    out->value[0] = '\0';
#ifdef _WIN32
    {
        char* value = NULL;
        errno_t env_status = _dupenv_s(&value, &length, key);
        if (env_status != 0) {
            return 1;
        }
        out->had_value = value != NULL;
        if (value == NULL) {
            return 0;
        }
        if (length == 0U || length > sizeof(out->value)) {
            free(value);
            return 1;
        }
        memcpy(out->value, value, length);
        free(value);
        return 0;
    }
#else
    const char* value = getenv(key);
    out->had_value = value != NULL;
    if (value == NULL) {
        return 0;
    }

    length = strlen(value);
    if (length >= sizeof(out->value)) {
        return 1;
    }
    memcpy(out->value, value, length + 1U);
    return 0;
#endif
}

static int restore_test_env(const char* key, const TestEnvValue* saved)
{
    if (key == NULL || saved == NULL) {
        return 1;
    }
    return saved->had_value ? set_test_env(key, saved->value) : unset_test_env(key);
}

static int profile_json_contains_all(const char** expected, size_t expected_count)
{
    unsigned char storage[65536];
    SlByteBuilder builder = {0};
    SlBytes bytes = {0};
    SlStr json = sl_str_empty();
    size_t index = 0U;

    if (!sl_status_is_ok(sl_byte_builder_init_fixed(&builder, storage, sizeof(storage))) ||
        !sl_status_is_ok(sl_http_profile_write_json(&builder)))
    {
        return 1;
    }
    bytes = sl_byte_builder_view(&builder);
    json = sl_str_from_parts((const char*)bytes.ptr, bytes.length);
    for (index = 0U; index < expected_count; index += 1U) {
        if (expect_str_contains(json, sl_str_from_cstr(expected[index])) != 0) {
            fprintf(stderr, "missing profile fragment: %s\n%.*s\n", expected[index],
                    (int)json.length, json.ptr == NULL ? "" : json.ptr);
            return 1;
        }
    }
    return 0;
}

static int enable_http_profile(TestEnvValue* saved)
{
    if (capture_test_env("SLOPPY_HTTP_PROFILE", saved) != 0 ||
        set_test_env("SLOPPY_HTTP_PROFILE", "1") != 0)
    {
        return 1;
    }
    sl_http_profile_reset();
    return 0;
}

static int restore_http_profile(const TestEnvValue* saved)
{
    int result = restore_test_env("SLOPPY_HTTP_PROFILE", saved);
    sl_http_profile_reset();
    return result;
}

static unsigned long test_process_id(void)
{
#ifdef _WIN32
    return (unsigned long)GetCurrentProcessId();
#else
    return (unsigned long)getpid();
#endif
}

static int make_test_cache_dir(char* buffer, size_t capacity, const char* prefix)
{
    static unsigned int counter = 0U;
    int written;

    if (buffer == NULL || capacity == 0U || prefix == NULL) {
        return 1;
    }

    written = snprintf(buffer, capacity, "artifacts/%s-%lu-%u", prefix, test_process_id(), counter);
    counter += 1U;
    return expect_format_complete(written, capacity);
}

static int test_directory_has_entries(SlArena* arena, const char* path)
{
    SlArenaMark mark;
    SlFsDirectoryList list = {0};
    SlStatus status;
    SlStatus reset_status;
    int result;

    if (arena == NULL || path == NULL) {
        return 1;
    }

    mark = sl_arena_mark(arena);
    status = sl_fs_list_directory(arena, sl_str_from_cstr(path), &list, NULL);
    result = sl_status_is_ok(status) && list.count > 0U ? 0 : 1;
    reset_status = sl_arena_reset_to(arena, mark);
    return result != 0 || !sl_status_is_ok(reset_status) ? 1 : 0;
}

static int os_child_main(int argc, char** argv)
{
    if (argc >= 3 && strcmp(argv[2], "echo") == 0) {
        puts("v8-child-output");
        return 5;
    }
    if (argc >= 3 && strcmp(argv[2], "stdin") == 0) {
        char buffer[128];
        if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
            return 6;
        }
        printf("v8-stdin:%s", buffer);
        return 0;
    }
    return 7;
}

static SlStr self_process_path(char* buffer, size_t capacity, const char* fallback)
{
#ifdef _WIN32
    DWORD length = GetModuleFileNameA(NULL, buffer, (DWORD)capacity);
    if (length != 0U && length < capacity) {
        return sl_str_from_parts(buffer, (size_t)length);
    }
#else
    (void)buffer;
    (void)capacity;
#endif
    return sl_str_from_cstr(fallback);
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
    context.scheme = sl_str_from_cstr("http");
    context.needs_route_params = true;
    context.needs_query_params = true;
    context.needs_headers = true;
    context.needs_body = true;
    context.needs_request = true;
    context.needs_connection = true;
    context.needs_signal = true;
    context.needs_log = true;
    context.needs_metadata = true;
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

typedef struct TestWebSocketBridge
{
    size_t send_count;
    SlWebSocketOpcode last_opcode;
    unsigned char last_payload[128];
    size_t last_payload_length;
    size_t close_count;
    uint16_t close_code;
    char close_reason[32];
    size_t close_reason_length;
} TestWebSocketBridge;

static SlStatus test_websocket_bridge_send(void* user, const SlWebSocketFrameWriteOptions* options,
                                           SlDiag* out_diag)
{
    TestWebSocketBridge* bridge = (TestWebSocketBridge*)user;

    (void)out_diag;
    if (bridge == NULL || options == NULL || options->payload.length > sizeof(bridge->last_payload))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    bridge->send_count += 1U;
    bridge->last_opcode = options->opcode;
    bridge->last_payload_length = options->payload.length;
    if (options->payload.length != 0U) {
        memcpy(bridge->last_payload, options->payload.ptr, options->payload.length);
    }
    return sl_status_ok();
}

static SlStatus test_websocket_bridge_close(void* user, uint16_t code, SlStr reason,
                                            SlDiag* out_diag)
{
    TestWebSocketBridge* bridge = (TestWebSocketBridge*)user;

    (void)out_diag;
    if (bridge == NULL || reason.length >= sizeof(bridge->close_reason)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    bridge->close_count += 1U;
    bridge->close_code = code;
    bridge->close_reason_length = reason.length;
    if (reason.length != 0U) {
        memcpy(bridge->close_reason, reason.ptr, reason.length);
    }
    bridge->close_reason[reason.length] = '\0';
    return sl_status_ok();
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

    sl_fs_delete_file(path, NULL);
    sl_fs_delete_file(invalid_path, NULL);
    sl_fs_delete_directory(dir_path, true, NULL);
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
                    " if ('slot' in handle || 'generation' in handle) {"
                    "   throw new Error('filesystem handle leaked resource internals');"
                    " }"
                    " let forgedHandleRejected = false;"
                    " try { await globalThis.__sloppy.fs.handleRead({ slot: 0, generation: 1 }, "
                    "1); }"
                    " catch (error) { forgedHandleRejected = "
                    "String(error.message).includes('handle id is stale or closed'); }"
                    " if (!forgedHandleRejected) { throw new Error('forged filesystem handle was "
                    "accepted'); }"
                    " const bytes = await globalThis.__sloppy.fs.handleRead(handle, 3);"
                    " await globalThis.__sloppy.fs.handleClose(handle);"
                    " const writer = await globalThis.__sloppy.fs.openHandle("
                    "\"./sloppy-v8-fs-dir/write.txt\", \"write\", true);"
                    " await globalThis.__sloppy.fs.handleWriteText(writer, \"write-ok\");"
                    " const sliced = new Uint8Array([120, 115, 108, 105, 99, 101]).subarray(1, "
                    "5);"
                    " await globalThis.__sloppy.fs.handleWriteBytes(writer, sliced);"
                    " await globalThis.__sloppy.fs.handleClose(writer);"
                    " const written = await globalThis.__sloppy.fs.readText("
                    "\"./sloppy-v8-fs-dir/write.txt\");"
                    " const watcher = await globalThis.__sloppy.fs.watch("
                    "\"./sloppy-v8-fs-dir\", true, { queueCapacity: 4 });"
                    " if ('slot' in watcher || 'generation' in watcher) {"
                    "   throw new Error('filesystem watch leaked resource internals');"
                    " }"
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
        sl_fs_delete_file(path, NULL);
        sl_fs_delete_file(invalid_path, NULL);
        sl_fs_delete_directory(dir_path, true, NULL);
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
        sl_fs_delete_file(path, NULL);
        sl_fs_delete_file(invalid_path, NULL);
        sl_fs_delete_directory(dir_path, true, NULL);
        return 5;
    }

    result = (SlEngineResult){0};
    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_fs_advanced"), &result,
                                               &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_TEXT ||
        !sl_str_equal(result.text, sl_str_from_cstr("a.txt:3:write-okslic:created:watched.txt")))
    {
        sl_engine_destroy(engine);
        sl_fs_delete_file(path, NULL);
        sl_fs_delete_file(invalid_path, NULL);
        sl_fs_delete_directory(dir_path, true, NULL);
        return 6;
    }

    sl_engine_destroy(engine);
    sl_fs_delete_file(path, NULL);
    sl_fs_delete_file(invalid_path, NULL);
    sl_fs_delete_directory(dir_path, true, NULL);
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

static int expect_malformed_source_map_case(const char* mappings, const char* function_name,
                                            const char* message, int failure_base)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};
    char source[256];
    char source_map[512];
    const char* throw_token = NULL;
    size_t expected_column = 0U;

    if (expect_format_complete(snprintf(source, sizeof(source),
                                        "globalThis.%s = function () { throw new Error(\"%s\"); };",
                                        function_name, message),
                               sizeof(source)) != 0 ||
        expect_format_complete(
            snprintf(source_map, sizeof(source_map),
                     "{\"version\":3,\"sources\":[\"src/malformed.js\"],\"names\":[],"
                     "\"mappings\":\"%s\"}",
                     mappings),
            sizeof(source_map)) != 0)
    {
        return failure_base;
    }
    throw_token = strstr(source, "throw");
    if (throw_token == NULL) {
        return failure_base + 6;
    }
    expected_column = (size_t)(throw_token - source) + 1U;

    options.source_map = sl_bytes_from_parts((const unsigned char*)source_map, strlen(source_map));
    options.source_map_source_name = sl_str_from_cstr("generated-app.js");

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return failure_base + 1;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return failure_base + 2;
    }

    if (expect_status(sl_engine_eval_source(engine, sl_str_from_cstr("generated-app.js"),
                                            sl_str_from_cstr(source), &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return failure_base + 3;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr(function_name), &result, &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        sl_engine_destroy(engine);
        return failure_base + 4;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_ENGINE_EXCEPTION ||
        !diag.primary_span.has_location || diag.primary_span.line != 1U ||
        diag.primary_span.column != expected_column ||
        !sl_str_equal(diag.primary_span.path, sl_str_from_cstr("generated-app.js")) ||
        diag.hint_count == 0U ||
        expect_str_contains(diag.hints[0], sl_str_from_cstr("Malformed source map")) != 0)
    {
        sl_engine_destroy(engine);
        return failure_base + 5;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_source_map_bounds_malformed_cases_report_generated_location(void)
{
    int result = expect_malformed_source_map_case("DAAA", "sloppy_negative_generated_column",
                                                  "negative generated column", 73);
    if (result != 0) {
        return result;
    }

    result = expect_malformed_source_map_case("+///////////PAAA,CAAA",
                                              "sloppy_generated_column_overflow",
                                              "generated column overflow", 80);
    if (result != 0) {
        return result;
    }

    result = expect_malformed_source_map_case(
        "AA+///////////PA,CACA", "sloppy_original_line_overflow", "original line overflow", 87);
    if (result != 0) {
        return result;
    }

    return expect_malformed_source_map_case("AAA+///////////P", "sloppy_original_column_overflow",
                                            "original column overflow", 94);
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

static int test_plain_object_result_returns_json_response(void)
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
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 53;
    }

    if (result.kind != SL_ENGINE_RESULT_JSON ||
        result.payload_kind != SL_ENGINE_RESULT_PAYLOAD_RESPONSE ||
        result.response.status != 200U ||
        expect_bytes_equal(result.response.body, "{\"ok\":true}") != 0)
    {
        sl_engine_destroy(engine);
        return 54;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_primitive_results_follow_http_contract(void)
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
        return 520;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 521;
    }

    if (expect_status(sl_engine_eval_source(
                          engine, sl_str_from_cstr("v8-primitive-results.js"),
                          sl_str_from_cstr("globalThis.sloppy_undefined = function () {};"
                                           "globalThis.sloppy_null = function () { return "
                                           "null; };"
                                           "globalThis.sloppy_number = function () { return "
                                           "42; };"
                                           "globalThis.sloppy_boolean = function () { return "
                                           "true; };"
                                           "globalThis.sloppy_array = function () { return "
                                           "[1, 2, 3]; };"),
                          &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 522;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_undefined"), &result,
                                               &diag),
                      SL_STATUS_UNSUPPORTED) != 0 ||
        result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_INVALID_HTTP_RESULT)
    {
        sl_engine_destroy(engine);
        return 523;
    }

    sl_arena_reset(&result_arena);
    result = (SlEngineResult){0};
    diag = (SlDiag){0};
    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_null"), &result, &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_JSON ||
        result.payload_kind != SL_ENGINE_RESULT_PAYLOAD_RESPONSE ||
        result.response.status != 200U || expect_bytes_equal(result.response.body, "null") != 0)
    {
        sl_engine_destroy(engine);
        return 524;
    }

    sl_arena_reset(&result_arena);
    result = (SlEngineResult){0};
    diag = (SlDiag){0};
    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_number"), &result, &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_JSON ||
        result.payload_kind != SL_ENGINE_RESULT_PAYLOAD_RESPONSE ||
        result.response.status != 200U || expect_bytes_equal(result.response.body, "42") != 0)
    {
        sl_engine_destroy(engine);
        return 525;
    }

    sl_arena_reset(&result_arena);
    result = (SlEngineResult){0};
    diag = (SlDiag){0};
    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_boolean"), &result, &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_JSON ||
        result.payload_kind != SL_ENGINE_RESULT_PAYLOAD_RESPONSE ||
        result.response.status != 200U || expect_bytes_equal(result.response.body, "true") != 0)
    {
        sl_engine_destroy(engine);
        return 526;
    }

    sl_arena_reset(&result_arena);
    result = (SlEngineResult){0};
    diag = (SlDiag){0};
    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_array"), &result, &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_JSON ||
        result.payload_kind != SL_ENGINE_RESULT_PAYLOAD_RESPONSE ||
        result.response.status != 200U || expect_bytes_equal(result.response.body, "[1,2,3]") != 0)
    {
        sl_engine_destroy(engine);
        return 527;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_result_descriptor_negative_shapes_fail_safely(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[4096];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 530;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 531;
    }

    if (expect_status(sl_engine_eval_source(
                          engine, sl_str_from_cstr("v8-result-negative-shapes.js"),
                          sl_str_from_cstr(
                              "globalThis.sloppy_missing_status = function () {"
                              "  return { __sloppyResult: true, kind: 'text', contentType: "
                              "    'text/plain; charset=utf-8', body: 'bad' };"
                              "};"
                              "globalThis.sloppy_unsupported_status = function () {"
                              "  return { __sloppyResult: true, kind: 'text', status: 418,"
                              "    contentType: 'text/plain; charset=utf-8', body: 'bad' };"
                              "};"
                              "globalThis.sloppy_missing_content_type = function () {"
                              "  return { __sloppyResult: true, kind: 'text', status: 200,"
                              "    body: 'bad' };"
                              "};"
                              "globalThis.sloppy_text_non_string_body = function () {"
                              "  return { __sloppyResult: true, kind: 'text', status: 200,"
                              "    contentType: 'text/plain; charset=utf-8', body: { bad: true } };"
                              "};"
                              "globalThis.sloppy_bytes_non_binary_body = function () {"
                              "  return { __sloppyResult: true, kind: 'bytes', status: 200,"
                              "    contentType: 'application/octet-stream', body: 'bad' };"
                              "};"
                              "globalThis.sloppy_json_circular_body = function () {"
                              "  const value = {}; value.self = value;"
                              "  return { __sloppyResult: true, kind: 'json', status: 200,"
                              "    contentType: 'application/json; charset=utf-8', body: value };"
                              "};"
                              "globalThis.sloppy_kind_without_marker = function () {"
                              "  return { kind: 'json' };"
                              "};"
                              "globalThis.sloppy_false_marker = function () {"
                              "  return { __sloppyResult: false };"
                              "};"),
                          &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 532;
    }

#define EXPECT_INVALID_RESULT(function_name, expected_status, failure_code)                        \
    do {                                                                                           \
        sl_arena_reset(&result_arena);                                                             \
        result = (SlEngineResult){0};                                                              \
        diag = (SlDiag){0};                                                                        \
        if (expect_status(sl_engine_call_function0(engine, &result_arena,                          \
                                                   sl_str_from_cstr(function_name), &result,       \
                                                   &diag),                                         \
                          expected_status) != 0 ||                                                 \
            result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_INVALID_HTTP_RESULT)      \
        {                                                                                          \
            sl_engine_destroy(engine);                                                             \
            return failure_code;                                                                   \
        }                                                                                          \
    } while (0)

    EXPECT_INVALID_RESULT("sloppy_missing_status", SL_STATUS_INVALID_STATE, 533);
    EXPECT_INVALID_RESULT("sloppy_unsupported_status", SL_STATUS_INVALID_STATE, 534);
    EXPECT_INVALID_RESULT("sloppy_missing_content_type", SL_STATUS_INVALID_STATE, 535);
    EXPECT_INVALID_RESULT("sloppy_text_non_string_body", SL_STATUS_INVALID_STATE, 536);
    EXPECT_INVALID_RESULT("sloppy_bytes_non_binary_body", SL_STATUS_INVALID_STATE, 537);
    EXPECT_INVALID_RESULT("sloppy_json_circular_body", SL_STATUS_INVALID_STATE, 538);
    EXPECT_INVALID_RESULT("sloppy_kind_without_marker", SL_STATUS_UNSUPPORTED, 539);
    EXPECT_INVALID_RESULT("sloppy_false_marker", SL_STATUS_UNSUPPORTED, 540);

#undef EXPECT_INVALID_RESULT

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

static int test_profile_counts_top_level_return_type_once(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[2048];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlHttpRequestHead request = test_request(SL_HTTP_METHOD_GET);
    SlHttpRequestContext context = test_request_context(&request);
    TestEnvValue profile_env = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0 ||
        enable_http_profile(&profile_env) != 0)
    {
        return 60;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        restore_http_profile(&profile_env);
        return 61;
    }

    if (expect_status(sl_engine_eval_source(
                          engine, sl_str_from_cstr("v8-profile-return-types.js"),
                          sl_str_from_cstr(
                              "globalThis.profileSyncReturn = function () { return { ok: true }; };"
                              "globalThis.profilePromiseReturn = async function () { return { ok: "
                              "true }; };"),
                          &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        restore_http_profile(&profile_env);
        return 62;
    }

    sl_http_profile_reset();
    if (expect_status(sl_engine_call_function_with_context(engine, &result_arena,
                                                           sl_str_from_cstr("profileSyncReturn"),
                                                           &context, &result, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        restore_http_profile(&profile_env);
        return 63;
    }
    {
        const char* expected[] = {"\"syncReturns\": 1", "\"promiseReturns\": 0"};
        if (profile_json_contains_all(expected, sizeof(expected) / sizeof(expected[0])) != 0) {
            sl_engine_destroy(engine);
            restore_http_profile(&profile_env);
            return 64;
        }
    }

    sl_arena_reset(&result_arena);
    result = (SlEngineResult){0};
    sl_http_profile_reset();
    if (expect_status(sl_engine_call_function_with_context(engine, &result_arena,
                                                           sl_str_from_cstr("profilePromiseReturn"),
                                                           &context, &result, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        restore_http_profile(&profile_env);
        return 65;
    }
    {
        const char* expected[] = {"\"syncReturns\": 0", "\"promiseReturns\": 1"};
        if (profile_json_contains_all(expected, sizeof(expected) / sizeof(expected[0])) != 0) {
            sl_engine_destroy(engine);
            restore_http_profile(&profile_env);
            return 66;
        }
    }

    sl_engine_destroy(engine);
    if (restore_http_profile(&profile_env) != 0) {
        return 67;
    }
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
    unsigned char engine_storage[32768];
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
                                 "  const sig512 = c.hmac('sha512', key, enc('Hi There'));"
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
                                 "hex(sig512).slice(0, 8) + ':' + "
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
                                                    "b0344c61:87aa7cde:4:6:8:0:true:true:true:true:"
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
                                 "  const names = ['connect', 'connectTls', 'listen', 'accept',"
                                 "    'write',"
                                 "    'read', 'readLine', 'readUntil', 'close', 'abort',"
                                 "    'closeListener', 'abortListener', 'connectLocal',"
                                 "    'listenLocal', 'acceptLocal', 'writeLocal', 'readLocal',"
                                 "    'readLineLocal', 'readUntilLocal', 'closeLocal',"
                                 "    'abortLocal', 'closeLocalServer', 'abortLocalServer'];"
                                 "  const caps = ['tlsCaPath', 'tlsCaBundlePath',"
                                 "    'tlsTrustStorePath', 'tlsClientCertificate',"
                                 "    'tlsInsecureSkipVerify'];"
                                 "  return n && names.every((name) => typeof n[name] === "
                                 "'function') && caps.every((name) => n[name] === true)"
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

static int test_net_local_endpoint_intrinsic_rejects_invalid_runtime_path(void)
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
        return 436;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 437;
    }

    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("v8-net-local-invalid.js"),
                sl_str_from_cstr(
                    "globalThis.sloppy_net_local_invalid = async function () {"
                    "  const invalidPaths = ['runtime:/../bad.sock', 'runtime://bad.sock',"
                    "    'runtime:/bad//sock', 'runtime:/bad name.sock'];"
                    "  try { await globalThis.__sloppy.net.closeLocal({ slot: 0, generation: 1 });"
                    "    return 'accepted-forged-handle';"
                    "  } catch (err) {"
                    "    const message = String(err && err.message ? err.message : err);"
                    "    if (!message.includes('handle is stale or closed')) {"
                    "      return 'wrong-forged-error:' + message;"
                    "    }"
                    "  }"
                    "  for (const path of invalidPaths) {"
                    "    try { await globalThis.__sloppy.net.connectLocal({ path });"
                    "      return 'accepted:' + path;"
                    "    } catch (err) {"
                    "      const message = String(err && err.message ? err.message : err);"
                    "      if (!message.includes('local IPC endpoint path is invalid')) {"
                    "        return 'wrong-error:' + message;"
                    "      }"
                    "    }"
                    "  }"
                    "  return 'local-invalid-ok';"
                    "};"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 438;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_net_local_invalid"),
                                               &result, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 439;
    }

    if (result.kind != SL_ENGINE_RESULT_TEXT ||
        !sl_str_equal(result.text, sl_str_from_cstr("local-invalid-ok")))
    {
        sl_engine_destroy(engine);
        return 440;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_http_client_feature_activates_net_intrinsic_namespace(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[1024];
    unsigned char feature_storage[1024];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlArena feature_arena = {0};
    SlEngineOptions options = v8_options();
    SlPlanRequiredFeature required = {.id = sl_str_from_cstr("stdlib.httpclient")};
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
        return 441;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 442;
    }

    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("v8-http-client-net-bridge.js"),
                sl_str_from_cstr("globalThis.sloppy_http_client_net_bridge = function () {"
                                 "  const n = globalThis.__sloppy.net;"
                                 "  return n && typeof n.connect === 'function' &&"
                                 "    typeof n.connectTls === 'function' &&"
                                 "    typeof n.write === 'function' &&"
                                 "    typeof n.readUntil === 'function' &&"
                                 "    typeof n.close === 'function' &&"
                                 "    n.tlsCaPath === true && n.tlsCaBundlePath === true &&"
                                 "    n.tlsTrustStorePath === true &&"
                                 "    n.tlsClientCertificate === true &&"
                                 "    n.tlsInsecureSkipVerify === true"
                                 "      ? 'httpclient-net-bridge-ok'"
                                 "      : 'httpclient-net-bridge-missing';"
                                 "};"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 443;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_http_client_net_bridge"),
                                               &result, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 444;
    }

    if (result.kind != SL_ENGINE_RESULT_TEXT ||
        !sl_str_equal(result.text, sl_str_from_cstr("httpclient-net-bridge-ok")))
    {
        sl_engine_destroy(engine);
        return 445;
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

static int test_workers_intrinsic_namespace_registered_when_active(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[1024];
    unsigned char feature_storage[2048];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlArena feature_arena = {0};
    SlEngineOptions options = v8_options();
    SlPlanRequiredFeature required = {.id = sl_str_from_cstr("stdlib.workers")};
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
        return 500;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 501;
    }

    if (expect_status(sl_engine_eval_source(
                          engine, sl_str_from_cstr("v8-workers-active.js"),
                          sl_str_from_cstr("globalThis.sloppy_workers_active = function () {"
                                           "  const w = globalThis.__sloppy.workers;"
                                           "  return w && w.feature === 'stdlib.workers' &&"
                                           "    w.boundedByDefault === true &&"
                                           "    w.rawNativeHandlesExposed === false &&"
                                           "    w.ownerThreadSettlement === true &&"
                                           "    typeof w.runPool === 'function' &&"
                                           "    typeof w.startWorker === 'function'"
                                           "      ? 'workers-active-ok'"
                                           "      : 'workers-active-missing';"
                                           "};"),
                          &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 502;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_workers_active"), &result,
                                               &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 503;
    }

    if (result.kind != SL_ENGINE_RESULT_TEXT ||
        !sl_str_equal(result.text, sl_str_from_cstr("workers-active-ok")))
    {
        sl_engine_destroy(engine);
        return 504;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_workers_intrinsic_pool_runs_off_owner_thread(void)
{
    unsigned char engine_storage[65536];
    unsigned char result_storage[4096];
    unsigned char feature_storage[2048];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlArena feature_arena = {0};
    SlEngineOptions options = v8_options();
    SlPlanRequiredFeature required = {.id = sl_str_from_cstr("stdlib.workers")};
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
        return 510;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 511;
    }

    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("v8-workers-pool.js"),
                sl_str_from_cstr(
                    "globalThis.sloppy_workers_pool = async function () {"
                    "  const promise = globalThis.__sloppy.workers.runPool("
                    "    'cpu',"
                    "    async function (ctx) {"
                    "      let total = 0;"
                    "      for (let index = 0; index < 1000; index += 1) { total += index; }"
                    "      const raw = new Uint8Array(ctx.input.raw);"
                    "      const view = ctx.input.view;"
                    "      const nested = ctx.input.nested.bytes;"
                    "      return new Uint8Array(["
                    "        ctx.input.value + total - 499500,"
                    "        raw[1],"
                    "        view[0],"
                    "        nested[2]"
                    "      ]);"
                    "    },"
                    "    {"
                    "      value: 1,"
                    "      raw: new Uint8Array([2, 3]).buffer,"
                    "      view: new Uint8Array([4, 5, 6]),"
                    "      nested: { bytes: new Uint8Array([7, 8, 9]) }"
                    "    },"
                    "    {}"
                    "  );"
                    "  const ownerThreadContinued = 'owner-free';"
                    "  const bytes = await promise;"
                    "  return ownerThreadContinued + ':' +"
                    "    (bytes instanceof Uint8Array) + ':' + Array.from(bytes).join(',');"
                    "};"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 512;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_workers_pool"), &result,
                                               &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 513;
    }

    if (result.kind != SL_ENGINE_RESULT_TEXT ||
        !sl_str_equal(result.text, sl_str_from_cstr("owner-free:true:1,3,4,9")))
    {
        sl_engine_destroy(engine);
        return 514;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_workers_intrinsic_js_worker_start_invoke_stop(void)
{
    unsigned char engine_storage[65536];
    unsigned char result_storage[4096];
    unsigned char feature_storage[2048];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlArena feature_arena = {0};
    SlEngineOptions options = v8_options();
    SlPlanRequiredFeature required = {.id = sl_str_from_cstr("stdlib.workers")};
    SlPlan plan = {.required_features = &required, .required_feature_count = 1U};
    SlRuntimeFeatureSet features = {0};
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};
    char worker_path_buffer[128];
    char script_buffer[4096];
    SlStr worker_path = {0};
    SlBytes worker_source = sl_bytes_from_parts(
        (const unsigned char*)"export async function parse(payload) {"
                              "  return 'tokens:' + payload.text.split(/\\s+/u).length;"
                              "}\n"
                              "let count = 0;\n"
                              "export function countCalls() { count += 1; return count; }\n"
                              "export function bytes(payload) {"
                              "  const raw = new Uint8Array(payload.raw);"
                              "  return new Uint8Array([raw[0], payload.view[1]]);"
                              "}\n"
                              "export function onMessage(payload) { return 'msg:' + payload.kind; "
                              "}\n",
        sizeof("export async function parse(payload) {"
               "  return 'tokens:' + payload.text.split(/\\s+/u).length;"
               "}\n"
               "let count = 0;\n"
               "export function countCalls() { count += 1; return count; }\n"
               "export function bytes(payload) {"
               "  const raw = new Uint8Array(payload.raw);"
               "  return new Uint8Array([raw[0], payload.view[1]]);"
               "}\n"
               "export function onMessage(payload) { return 'msg:' + payload.kind; }\n") -
            1U);

    if (expect_format_complete(snprintf(worker_path_buffer, sizeof(worker_path_buffer),
                                        "./sloppy-v8-worker-test-%p.js", (void*)&engine_storage[0]),
                               sizeof(worker_path_buffer)) != 0)
    {
        return 515;
    }
    worker_path = sl_str_from_cstr(worker_path_buffer);
    sl_fs_delete_file(worker_path, NULL);
    if (expect_status(sl_fs_write_file(worker_path, worker_source, false, &diag), SL_STATUS_OK) !=
            0 ||
        init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0 ||
        init_arena(&feature_arena, feature_storage, sizeof(feature_storage)) != 0 ||
        attach_runtime_features(&options, &plan, &feature_arena, &features) != 0)
    {
        sl_fs_delete_file(worker_path, NULL);
        return 515;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        sl_fs_delete_file(worker_path, NULL);
        return 516;
    }

    if (expect_format_complete(
            snprintf(script_buffer, sizeof(script_buffer),
                     "globalThis.sloppy_workers_js_worker = async function () {"
                     "  const worker = globalThis.__sloppy.workers.startWorker('%s', { "
                     "memoryLimitMb: 128 });"
                     "  const parsed = await worker.invoke('parse', { text: 'one two three' });"
                     "  const firstCount = await worker.invoke('countCalls');"
                     "  const secondCount = await worker.invoke('countCalls');"
                     "  const bytes = await worker.invoke('bytes', {"
                     "    raw: new Uint8Array([11, 12]).buffer,"
                     "    view: new Uint8Array([13, 14])"
                     "  });"
                     "  const posted = await worker.post({ kind: 'ping' });"
                     "  await worker.stop();"
                     "  try {"
                     "    await worker.invoke('parse', { text: 'late' });"
                     "    return 'missing-stale-error';"
                     "  } catch (err) {"
                     "    return parsed + ':' + (bytes instanceof Uint8Array) + ':' +"
                     "      Array.from(bytes).join(',') + ':' + firstCount + ',' + secondCount + "
                     "':' +"
                     "      posted + ':' + err.code;"
                     "  }"
                     "};"
                     "globalThis.sloppy_workers_resource_limit = function () {"
                     "  try {"
                     "    globalThis.__sloppy.workers.startWorker('%s', { memoryLimitMb: 1 });"
                     "    return 'missing-resource-error';"
                     "  } catch (err) {"
                     "    return err.code;"
                     "  }"
                     "};",
                     worker_path_buffer, worker_path_buffer),
            sizeof(script_buffer)) != 0)
    {
        sl_engine_destroy(engine);
        sl_fs_delete_file(worker_path, NULL);
        return 517;
    }
    if (expect_status(sl_engine_eval_source(engine, sl_str_from_cstr("v8-workers-js-worker.js"),
                                            sl_str_from_parts(script_buffer, strlen(script_buffer)),
                                            &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        sl_fs_delete_file(worker_path, NULL);
        return 517;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_workers_js_worker"),
                                               &result, &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_TEXT ||
        !sl_str_equal(result.text, sl_str_from_cstr("tokens:3:true:11,14:1,2:msg:ping:"
                                                    "SLOPPY_E_WORKER_STALE_HANDLE")))
    {
        sl_engine_destroy(engine);
        sl_fs_delete_file(worker_path, NULL);
        return 518;
    }

    result = (SlEngineResult){0};
    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_workers_resource_limit"),
                                               &result, &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_TEXT ||
        !sl_str_equal(result.text, sl_str_from_cstr("SLOPPY_E_WORKER_RESOURCE_LIMIT_EXCEEDED")))
    {
        sl_engine_destroy(engine);
        sl_fs_delete_file(worker_path, NULL);
        return 519;
    }

    sl_engine_destroy(engine);
    sl_fs_delete_file(worker_path, NULL);
    return 0;
}

static int test_workers_intrinsic_inactive_feature_is_not_registered(void)
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
        return 505;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 506;
    }

    if (expect_status(sl_engine_eval_source(
                          engine, sl_str_from_cstr("v8-workers-inactive.js"),
                          sl_str_from_cstr("globalThis.sloppy_workers_inactive = function () {"
                                           "  return globalThis.__sloppy.workers === undefined"
                                           "    ? 'workers-inactive-ok'"
                                           "    : 'workers-unexpectedly-active';"
                                           "};"),
                          &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 507;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_workers_inactive"), &result,
                                               &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 508;
    }

    if (result.kind != SL_ENGINE_RESULT_TEXT ||
        !sl_str_equal(result.text, sl_str_from_cstr("workers-inactive-ok")))
    {
        sl_engine_destroy(engine);
        return 509;
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

    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("v8-os-active.js"),
                sl_str_from_cstr("globalThis.sloppy_os_active = function () {"
                                 "  const os = globalThis.__sloppy.os;"
                                 "  if (!os || typeof os.systemInfo !== 'function') {"
                                 "    return 'os-missing';"
                                 "  }"
                                 "  if (typeof os.processInfo !== 'function') {"
                                 "    return 'process-info-missing';"
                                 "  }"
                                 "  const info = os.systemInfo();"
                                 "  const process = os.processInfo();"
                                 "  const env = os.environmentGet('SLOPPY_OS_V8_TEST');"
                                 "  const has = os.environmentHas('SLOPPY_OS_V8_TEST');"
                                 "  const listed = os.environmentList('SLOPPY_OS_V8_')"
                                 "    .includes('SLOPPY_OS_V8_TEST');"
                                 "  return info.platform + ':' + info.arch + ':' +"
                                 "    (info.cpuCount > 0) + ':' + env + ':' + has + ':' +"
                                 "    listed + ':' + (process.pid > 0) + ':' +"
                                 "    (typeof process.cwd === 'string') + ':' +"
                                 "    (typeof process.argsAvailable === 'boolean') + ':' +"
                                 "    (!process.argsAvailable || Array.isArray(process.args));"
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
        expect_str_contains(result.text,
                            sl_str_from_cstr(":true:v8-env-ok:true:true:true:true:true:true")) != 0)
    {
        sl_engine_destroy(engine);
        return 449;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_os_intrinsic_process_info_respects_policy(void)
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
    SlOsPolicy policy = sl_os_strict_policy(NULL, 0U, false, sl_str_empty());
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    options.os_policy = &policy;
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

    if (expect_status(sl_engine_eval_source(
                          engine, sl_str_from_cstr("v8-os-process-info-denied.js"),
                          sl_str_from_cstr("globalThis.sloppy_os_process_info_denied = function "
                                           "() {"
                                           "  try { globalThis.__sloppy.os.processInfo(); }"
                                           "  catch (error) {"
                                           "    return String(error.message).includes("
                                           "'SLOPPY_E_OS_FEATURE_UNAVAILABLE')"
                                           "      ? 'process-info-denied-ok' : 'wrong-error';"
                                           "  }"
                                           "  return 'process-info-unexpectedly-allowed';"
                                           "};"),
                          &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 452;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_os_process_info_denied"),
                                               &result, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 453;
    }

    if (result.kind != SL_ENGINE_RESULT_TEXT ||
        expect_str_contains(result.text, sl_str_from_cstr("process-info-denied-ok")) != 0)
    {
        sl_engine_destroy(engine);
        return 454;
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

static int test_os_intrinsic_process_run_start_and_signals(const char* self_path)
{
    unsigned char engine_storage[16384];
    unsigned char result_storage[4096];
    unsigned char feature_storage[1024];
    char self_storage[4096];
    char self_js[4096];
    char source[8192];
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
    SlStr self = self_process_path(self_storage, sizeof(self_storage), self_path);
    size_t index = 0U;

    if (self.length == 0U || self.length >= sizeof(self_js)) {
        return 455;
    }
    for (index = 0U; index < self.length; index += 1U) {
        self_js[index] = self.ptr[index] == '\\' ? '/' : self.ptr[index];
    }
    self_js[self.length] = '\0';
    if (expect_format_complete(
            snprintf(source, sizeof(source),
                     "globalThis.sloppy_os_process = async function () {"
                     "  const os = globalThis.__sloppy.os;"
                     "  const command = \"%s\";"
                     "  const run = await os.processRun(command, ['--sloppy-os-child', 'echo'], {"
                     "    capture: 'text', maxStdoutBytes: 2097152, maxStderrBytes: 2097152, "
                     "timeoutMs: 5000"
                     "  });"
                     "  const pressure = [];"
                     "  for (let index = 0; index < 70; index++) {"
                     "    pressure.push(os.processRun(command, ['--sloppy-os-child', 'echo'], {"
                     "      capture: 'text', maxStdoutBytes: 1024, maxStderrBytes: 1024, "
                     "timeoutMs: 5000"
                     "    }));"
                     "  }"
                     "  const pressureResults = await Promise.all(pressure);"
                     "  const proc = await os.processStart(command, ['--sloppy-os-child', "
                     "'stdin'], {"
                     "    stdin: 'pipe', stdout: 'pipe', stderr: 'ignore'"
                     "  });"
                     "  if ('slot' in proc || 'generation' in proc || !Object.isFrozen(proc)) {"
                     "    return 'leaked-process-handle';"
                     "  }"
                     "  try {"
                     "    await os.processWait({ slot: 0, generation: 1 }, { timeoutMs: 1 });"
                     "    return 'accepted-forged-process';"
                     "  } catch (err) {"
                     "    const message = String(err && err.message ? err.message : err);"
                     "    if (!message.includes('process handle invalid')) {"
                     "      return 'wrong-forged-process-error:' + message;"
                     "    }"
                     "  }"
                     "  await os.processWriteStdin(proc, 'hello\\n');"
                     "  await os.processCloseStdin(proc);"
                     "  const exit = await os.processWait(proc, { timeoutMs: 5000 });"
                     "  const stdout = await os.processReadStdout(proc, 256);"
                     "  const registration = os.signalsOnShutdown(function () {});"
                     "  registration.dispose();"
                     "  await os.processDispose(proc);"
                     "  return run.exitCode + ':' + run.stdout.trim() + ':' +"
                     "    exit.exitCode + ':' + stdout.trim() + ':' + pressureResults.length + "
                     "':' +"
                     "    pressureResults.every((item) => item.exitCode === 5);"
                     "};",
                     self_js),
            sizeof(source)) != 0)
    {
        return 456;
    }

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0 ||
        init_arena(&feature_arena, feature_storage, sizeof(feature_storage)) != 0 ||
        attach_runtime_features(&options, &plan, &feature_arena, &features) != 0)
    {
        return 457;
    }
    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 458;
    }
    if (expect_status(sl_engine_eval_source(engine, sl_str_from_cstr("v8-os-process.js"),
                                            sl_str_from_cstr(source), &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 459;
    }
    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_os_process"), &result,
                                               &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 460;
    }
    if (result.kind != SL_ENGINE_RESULT_TEXT ||
        !sl_str_equal(result.text, sl_str_from_cstr("5:v8-child-output:0:v8-stdin:hello:70:true")))
    {
        sl_engine_destroy(engine);
        return 461;
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

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_V8_UNHANDLED_REJECTION ||
        expect_str_contains(diag.message, sl_str_from_cstr("async boom")) != 0)
    {
        sl_engine_destroy(engine);
        return 69;
    }

    if (expect_status(sl_diag_render_json(&json_arena, &diag, &json), SL_STATUS_OK) != 0 ||
        !sl_str_equal(json, sl_str_from_cstr(
                                "{\"code\":\"SLOPPY_E_V8_UNHANDLED_REJECTION\","
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

static int test_startup_snapshot_rebuilds_runtime_handler_map(void)
{
    unsigned char engine_storage[32768];
    unsigned char result_storage[2048];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlRuntimeFeatureSet features = {0};
    SlEngineOptions options = v8_options();
    SlEngine* first = NULL;
    SlEngine* second = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlHttpRequestHead request = test_request(SL_HTTP_METHOD_GET);
    SlHttpRequestContext context = test_request_context(&request);
    TestEnvValue saved_snapshot_dir = {0};
    TestEnvValue saved_code_cache_dir = {0};
    char snapshot_dir[256] = {0};
    char code_cache_dir[256] = {0};
    bool snapshot_env_captured = false;
    bool code_cache_env_captured = false;
    int return_code = 0;

    features.active_mask = UINT32_C(1) << SL_RUNTIME_FEATURE_STDLIB_APP;
    options.runtime_features = &features;

    if (capture_test_env("SLOPPY_V8_SNAPSHOT_DIR", &saved_snapshot_dir) != 0) {
        return 63;
    }
    snapshot_env_captured = true;
    if (capture_test_env("SLOPPY_V8_CODE_CACHE_DIR", &saved_code_cache_dir) != 0) {
        return_code = 63;
        goto cleanup;
    }
    code_cache_env_captured = true;
    if (make_test_cache_dir(snapshot_dir, sizeof(snapshot_dir), "test-v8-startup-snapshot") != 0 ||
        make_test_cache_dir(code_cache_dir, sizeof(code_cache_dir),
                            "test-v8-startup-snapshot-code-cache") != 0)
    {
        return_code = 63;
        goto cleanup;
    }

    if (snapshot_dir[0] != '\0') {
        sl_fs_delete_directory(sl_str_from_cstr(snapshot_dir), true, NULL);
    }
    if (code_cache_dir[0] != '\0') {
        sl_fs_delete_directory(sl_str_from_cstr(code_cache_dir), true, NULL);
    }
    if (set_test_env("SLOPPY_V8_SNAPSHOT_DIR", snapshot_dir) != 0 ||
        set_test_env("SLOPPY_V8_CODE_CACHE_DIR", code_cache_dir) != 0)
    {
        return_code = 63;
        goto cleanup;
    }
    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return_code = 64;
        goto cleanup;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &first), SL_STATUS_OK) != 0 ||
        expect_status(
            sl_engine_eval_source(first, sl_str_from_cstr("v8-snapshot-register-a.js"),
                                  sl_str_from_cstr("__sloppy_register_handler(7, function (ctx) { "
                                                   "return ctx.request.rawTarget; });"),
                                  &diag),
            SL_STATUS_OK) != 0 ||
        expect_status(sl_engine_call_registered_handler_with_context(first, &result_arena, 7U,
                                                                     &context, &result, &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_TEXT ||
        !sl_str_equal(result.text, sl_str_from_cstr("/users/123?q=abc")))
    {
        return_code = 65;
        goto cleanup;
    }

    sl_engine_destroy(first);
    first = NULL;
    if (test_directory_has_entries(&engine_arena, snapshot_dir) != 0) {
        return_code = 71;
        goto cleanup;
    }

    result = (SlEngineResult){0};
    sl_arena_reset(&result_arena);

    if (expect_status(sl_engine_create(&options, &engine_arena, &second), SL_STATUS_OK) != 0 ||
        expect_status(sl_engine_call_registered_handler_with_context(second, &result_arena, 7U,
                                                                     &context, &result, &diag),
                      SL_STATUS_INVALID_STATE) != 0 ||
        result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_ENGINE_CALL_ERROR ||
        expect_status(
            sl_engine_eval_source(second, sl_str_from_cstr("v8-snapshot-register-b.js"),
                                  sl_str_from_cstr("__sloppy_register_handler(8, function () { "
                                                   "return 'snapshot-ok'; });"),
                                  &diag),
            SL_STATUS_OK) != 0 ||
        expect_status(sl_engine_call_registered_handler_with_context(second, &result_arena, 8U,
                                                                     &context, &result, &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_TEXT ||
        !sl_str_equal(result.text, sl_str_from_cstr("snapshot-ok")))
    {
        return_code = 66;
        goto cleanup;
    }

cleanup:
    if (first != NULL) {
        sl_engine_destroy(first);
    }
    if (second != NULL) {
        sl_engine_destroy(second);
    }
    if (snapshot_dir[0] != '\0') {
        sl_fs_delete_directory(sl_str_from_cstr(snapshot_dir), true, NULL);
    }
    if (code_cache_dir[0] != '\0') {
        sl_fs_delete_directory(sl_str_from_cstr(code_cache_dir), true, NULL);
    }
    if (snapshot_env_captured &&
        restore_test_env("SLOPPY_V8_SNAPSHOT_DIR", &saved_snapshot_dir) != 0 && return_code == 0)
    {
        return_code = 72;
    }
    if (code_cache_env_captured &&
        restore_test_env("SLOPPY_V8_CODE_CACHE_DIR", &saved_code_cache_dir) != 0 &&
        return_code == 0)
    {
        return_code = 73;
    }
    return return_code;
}

static int test_startup_snapshot_supports_native_intrinsics(void)
{
    unsigned char engine_storage[65536];
    SlArena engine_arena = {0};
    SlRuntimeFeatureSet features = {0};
    SlEngineOptions options = v8_options();
    SlEngine* first = NULL;
    SlEngine* second = NULL;
    SlEngine* third = NULL;
    SlDiag diag = {0};
    TestEnvValue saved_snapshot_dir = {0};
    char snapshot_dir[256] = {0};
    bool snapshot_env_captured = false;
    int return_code = 0;
    /*
     * FFI needs Plan-backed registry state and is covered by
     * conformance.v8.ffi_native, not startup snapshot caching.
     */
    const uint64_t all_features_mask = ((UINT64_C(1) << SL_RUNTIME_FEATURE_COUNT) - UINT64_C(1)) &
                                       ~(UINT64_C(1) << (uint32_t)SL_RUNTIME_FEATURE_STDLIB_FFI);
    const char* probe_source =
        "if (typeof __sloppy.time.monotonicMs !== 'function') throw new Error('missing time');"
        "if (typeof __sloppy.crypto.randomUuid !== 'function') throw new Error('missing crypto');"
        "if (typeof __sloppy.codec.gzip !== 'function') throw new Error('missing codec');"
        "if (typeof __sloppy.net.connect !== 'function') throw new Error('missing net');"
        "if (typeof __sloppy.os.systemInfo !== 'function') throw new Error('missing os');"
        "if (typeof __sloppy.workers.runPool !== 'function') throw new Error('missing workers');"
        "if (typeof __sloppy.fs.exists !== 'function') throw new Error('missing fs');"
        "if (typeof __sloppy.data.sqlite.open !== 'function') throw new Error('missing sqlite');"
        "if (typeof __sloppy.data.postgres.open !== 'function') throw new Error('missing pg');"
        "if (typeof __sloppy.data.sqlserver.open !== 'function') throw new Error('missing mssql');";
    const char* reduced_mask_probe_source =
        "if (globalThis.__sloppy && globalThis.__sloppy.data && "
        "globalThis.__sloppy.data.sqlite) {"
        "  throw new Error('sqlite intrinsic unexpectedly snapshotted');"
        "}";

    features.active_mask = all_features_mask;
    options.runtime_features = &features;

    if (capture_test_env("SLOPPY_V8_SNAPSHOT_DIR", &saved_snapshot_dir) != 0) {
        return 67;
    }
    snapshot_env_captured = true;
    if (make_test_cache_dir(snapshot_dir, sizeof(snapshot_dir),
                            "test-v8-startup-snapshot-native") != 0)
    {
        return_code = 67;
        goto cleanup;
    }
    sl_fs_delete_directory(sl_str_from_cstr(snapshot_dir), true, NULL);
    if (set_test_env("SLOPPY_V8_SNAPSHOT_DIR", snapshot_dir) != 0) {
        return_code = 67;
        goto cleanup;
    }
    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0) {
        return_code = 68;
        goto cleanup;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &first), SL_STATUS_OK) != 0 ||
        expect_status(sl_engine_eval_source(first, sl_str_from_cstr("v8-snapshot-native-a.js"),
                                            sl_str_from_cstr(probe_source), &diag),
                      SL_STATUS_OK) != 0)
    {
        return_code = 69;
        goto cleanup;
    }
    sl_engine_destroy(first);
    first = NULL;
    if (test_directory_has_entries(&engine_arena, snapshot_dir) != 0) {
        return_code = 74;
        goto cleanup;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &second), SL_STATUS_OK) != 0 ||
        expect_status(sl_engine_eval_source(second, sl_str_from_cstr("v8-snapshot-native-b.js"),
                                            sl_str_from_cstr(probe_source), &diag),
                      SL_STATUS_OK) != 0)
    {
        return_code = 70;
        goto cleanup;
    }
    sl_engine_destroy(second);
    second = NULL;

    features.active_mask =
        all_features_mask & ~(UINT64_C(1) << (uint32_t)SL_RUNTIME_FEATURE_PROVIDER_SQLITE);
    if (expect_status(sl_engine_create(&options, &engine_arena, &third), SL_STATUS_OK) != 0 ||
        expect_status(sl_engine_eval_source(third,
                                            sl_str_from_cstr("v8-snapshot-native-reduced-mask.js"),
                                            sl_str_from_cstr(reduced_mask_probe_source), &diag),
                      SL_STATUS_OK) != 0)
    {
        return_code = 76;
        goto cleanup;
    }
    features.active_mask = all_features_mask;

cleanup:
    if (first != NULL) {
        sl_engine_destroy(first);
    }
    if (second != NULL) {
        sl_engine_destroy(second);
    }
    if (third != NULL) {
        sl_engine_destroy(third);
    }
    if (snapshot_dir[0] != '\0') {
        sl_fs_delete_directory(sl_str_from_cstr(snapshot_dir), true, NULL);
    }
    if (snapshot_env_captured &&
        restore_test_env("SLOPPY_V8_SNAPSHOT_DIR", &saved_snapshot_dir) != 0 && return_code == 0)
    {
        return_code = 75;
    }
    return return_code;
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
    unsigned char result_storage[2048];
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
static int test_request_context_log_writes_native_event(void)
{
    unsigned char engine_storage[16384];
    unsigned char result_storage[2048];
    unsigned char logging_storage[262144];
    unsigned char snapshot_storage[65536];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlArena logging_arena = {0};
    SlArena snapshot_arena = {0};
    SlLogRuntimeConfig logging_config = sl_log_runtime_config_default();
    SlLogRuntime* logging = NULL;
    SlLogSink* memory = NULL;
    SlLogMemorySnapshot snapshot = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlHttpRequestHead request = test_request(SL_HTTP_METHOD_GET);
    SlHttpRequestContext context = test_request_context(&request);
    SlRouteParam route_params[1] = {
        {.name = sl_str_from_cstr("name"), .value = sl_str_from_cstr("Ada")},
    };

    context.request_id = 42U;
    context.route_params = route_params;
    context.route_param_count = 1U;
    context.route_name = sl_str_from_cstr("Users.Get");
    context.route_pattern = sl_str_from_cstr("/users/{name}");
    logging_config.minimum_level = SL_LOG_LEVEL_INFO;
    logging_config.queue_capacity = 4U;
    logging_config.sink_capacity = 1U;

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0 ||
        init_arena(&logging_arena, logging_storage, sizeof(logging_storage)) != 0 ||
        init_arena(&snapshot_arena, snapshot_storage, sizeof(snapshot_storage)) != 0 ||
        expect_status(sl_log_runtime_create(&logging_arena, &logging_config, &logging),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_log_memory_sink_create(&logging_arena, 4U, &memory), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_runtime_add_sink(logging, memory), SL_STATUS_OK) != 0)
    {
        return 80;
    }
    options.logging = logging;

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        sl_log_runtime_shutdown(logging);
        return 81;
    }

    if (expect_status(sl_engine_eval_source(
                          engine, sl_str_from_cstr("v8-context-log.js"),
                          sl_str_from_cstr(
                              "globalThis.sloppy_context_log = function (ctx) {"
                              "  if (!ctx.log.isEnabled('info')) throw new Error('disabled');"
                              "  if (String(ctx.log.debug).indexOf('[native code]') !== -1) {"
                              "    throw new Error('disabled log crossed native bridge');"
                              "  }"
                              "  const disabledFields = {"
                              "    get expensive() { throw new Error('disabled field converted'); }"
                              "  };"
                              "  ctx.log.debug({ toString() {"
                              "    throw new Error('disabled message converted');"
                              "  } }, disabledFields);"
                              "  ctx.log.forCategory('users').info('user fetched', {"
                              "    userId: '123', token: 'SECRET_VALUE', ok: true, attempt: 3"
                              "  });"
                              "  return { __sloppyResult: true, kind: 'json', status: 200,"
                              "    contentType: 'application/json; charset=utf-8', body: {"
                              "      requestId: ctx.requestId, routeName: ctx.routeName,"
                              "      routePattern: ctx.routePattern, name: ctx.route.name"
                              "    } };"
                              "};"),
                          &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        sl_log_runtime_shutdown(logging);
        return 82;
    }
    {
        SlStatus call_status = sl_engine_call_function_with_context(
            engine, &result_arena, sl_str_from_cstr("sloppy_context_log"), &context, &result,
            &diag);
        if (expect_status(call_status, SL_STATUS_OK) != 0) {
            fprintf(stderr, "context log status=%d result=%d diag=%d message=%.*s\n",
                    (int)sl_status_code(call_status), (int)result.kind, (int)diag.code,
                    (int)diag.message.length, diag.message.ptr == NULL ? "" : diag.message.ptr);
            sl_engine_destroy(engine);
            sl_log_runtime_shutdown(logging);
            return 83;
        }
    }

    if (result.kind != SL_ENGINE_RESULT_JSON ||
        expect_bytes_equal(result.response.body,
                           "{\"requestId\":\"42\",\"routeName\":\"Users.Get\","
                           "\"routePattern\":\"/users/{name}\",\"name\":\"Ada\"}") != 0 ||
        expect_status(sl_log_runtime_flush(logging), SL_STATUS_OK) != 0 ||
        expect_status(sl_log_memory_sink_snapshot(memory, &snapshot_arena, &snapshot),
                      SL_STATUS_OK) != 0 ||
        snapshot.count != 1U)
    {
        sl_engine_destroy(engine);
        sl_log_runtime_shutdown(logging);
        return 84;
    }

    if (snapshot.events[0].level != SL_LOG_LEVEL_INFO ||
        !sl_str_equal(sl_log_event_category(&snapshot.events[0]), sl_str_from_cstr("users")) ||
        !sl_str_equal(sl_log_event_message(&snapshot.events[0]),
                      sl_str_from_cstr("user fetched")) ||
        !sl_str_equal(sl_log_event_request_id(&snapshot.events[0]), sl_str_from_cstr("42")) ||
        !sl_str_equal(sl_log_event_route_pattern(&snapshot.events[0]),
                      sl_str_from_cstr("/users/{name}")) ||
        snapshot.events[0].field_count != 4U || !snapshot.events[0].fields[1].redacted ||
        !sl_str_equal(sl_log_field_text_value(&snapshot.events[0].fields[1]),
                      sl_str_from_cstr("[REDACTED]")) ||
        !snapshot.events[0].fields[2].bool_value || snapshot.events[0].fields[3].i64_value != 3)
    {
        sl_engine_destroy(engine);
        sl_log_runtime_shutdown(logging);
        return 85;
    }

    sl_engine_destroy(engine);
    if (expect_status(sl_log_runtime_shutdown(logging), SL_STATUS_OK) != 0) {
        return 86;
    }
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

static int test_request_context_preserves_empty_header_lookup(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[2048];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlHttpHeader headers[2];
    SlHttpRequestHead request = test_request(SL_HTTP_METHOD_GET);
    SlHttpRequestContext context = {0};

    headers[0].name = sl_str_from_cstr("Host");
    headers[0].value = sl_str_from_cstr("example");
    headers[1].name = sl_str_from_cstr("X-Empty");
    headers[1].value = sl_str_empty();
    request.headers = headers;
    request.header_count = 2U;
    context = test_request_context(&request);

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 401;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 402;
    }

    if (expect_status(sl_engine_eval_source(
                          engine, sl_str_from_cstr("v8-empty-header.js"),
                          sl_str_from_cstr("globalThis.sloppy_empty_header = function (ctx) {"
                                           "  return ctx.request.headers.get('x-empty') === ''"
                                           "    ? 'empty' : 'wrong';"
                                           "};"),
                          &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 403;
    }

    if (expect_status(sl_engine_call_function_with_context(engine, &result_arena,
                                                           sl_str_from_cstr("sloppy_empty_header"),
                                                           &context, &result, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 404;
    }

    if (result.kind != SL_ENGINE_RESULT_TEXT ||
        expect_str_equal(result.text, sl_str_from_cstr("empty")) != 0)
    {
        sl_engine_destroy(engine);
        return 405;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_request_context_header_facade_is_opt_in(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[2048];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlHttpHeader headers[2];
    SlHttpRequestHead request = test_request(SL_HTTP_METHOD_GET);
    SlHttpRequestContext context = {0};

    headers[0].name = sl_str_from_cstr("Host");
    headers[0].value = sl_str_from_cstr("example");
    headers[1].name = sl_str_from_cstr("User-Agent");
    headers[1].value = sl_str_from_cstr("sloppy-test");
    request.headers = headers;
    request.header_count = 2U;
    context = test_request_context(&request);

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 410;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 411;
    }

    if (expect_status(sl_engine_eval_source(
                          engine, sl_str_from_cstr("v8-header-facade.js"),
                          sl_str_from_cstr("globalThis.sloppy_header_facade = function (ctx) {"
                                           "  return typeof ctx.header === 'undefined'"
                                           "    ? 'missing' : ctx.header.userAgent;"
                                           "};"),
                          &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 412;
    }

    if (expect_status(sl_engine_call_function_with_context(engine, &result_arena,
                                                           sl_str_from_cstr("sloppy_header_facade"),
                                                           &context, &result, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 413;
    }
    if (result.kind != SL_ENGINE_RESULT_TEXT ||
        expect_str_equal(result.text, sl_str_from_cstr("missing")) != 0)
    {
        sl_engine_destroy(engine);
        return 414;
    }

    sl_arena_reset(&result_arena);
    result = (SlEngineResult){0};
    context.needs_headers = false;
    context.needs_header_facade = true;
    if (expect_status(sl_engine_call_function_with_context(engine, &result_arena,
                                                           sl_str_from_cstr("sloppy_header_facade"),
                                                           &context, &result, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 415;
    }
    if (result.kind != SL_ENGINE_RESULT_TEXT ||
        expect_str_equal(result.text, sl_str_from_cstr("sloppy-test")) != 0)
    {
        sl_engine_destroy(engine);
        return 416;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_request_context_metadata_objects_are_opt_in(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[2048];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlHttpRequestHead request = test_request(SL_HTTP_METHOD_GET);
    SlRouteParam route_params[1] = {
        {sl_str_from_cstr("id"), sl_str_from_cstr("123"), SL_ROUTE_PARAM_STRING},
    };
    SlHttpRequestContext context = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 431;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 432;
    }

    if (expect_status(sl_engine_eval_source(
                          engine, sl_str_from_cstr("v8-context-lazy-metadata.js"),
                          sl_str_from_cstr(
                              "globalThis.sloppy_context_metadata = function (ctx) {"
                              "  return { __sloppyResult: true, kind: 'json', status: 200,"
                              "    contentType: 'application/json; charset=utf-8',"
                              "    body: { request: typeof ctx.request, log: typeof ctx.log,"
                              "      connection: typeof ctx.connection, signal: typeof ctx.signal,"
                              "      requestId: typeof ctx.requestId } };"
                              "};"
                              "globalThis.sloppy_context_route_only = function (ctx) {"
                              "  return { __sloppyResult: true, kind: 'json', status: 200,"
                              "    contentType: 'application/json; charset=utf-8',"
                              "    body: { id: ctx.route.id, request: typeof ctx.request,"
                              "      log: typeof ctx.log } };"
                              "};"),
                          &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 433;
    }

    context = test_request_context(&request);
    context.needs_route_params = false;
    context.needs_query_params = false;
    context.needs_headers = false;
    context.needs_body = false;
    context.needs_request = false;
    context.needs_connection = false;
    context.needs_signal = false;
    context.needs_log = false;
    context.needs_metadata = false;
    if (expect_status(sl_engine_call_function_with_context(
                          engine, &result_arena, sl_str_from_cstr("sloppy_context_metadata"),
                          &context, &result, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 434;
    }
    if (result.kind != SL_ENGINE_RESULT_JSON ||
        expect_bytes_equal(result.response.body,
                           "{\"request\":\"undefined\",\"log\":\"undefined\","
                           "\"connection\":\"undefined\",\"signal\":\"undefined\","
                           "\"requestId\":\"undefined\"}") != 0)
    {
        sl_engine_destroy(engine);
        return 435;
    }

    sl_arena_reset(&result_arena);
    result = (SlEngineResult){0};
    diag = (SlDiag){0};
    context.route_params = route_params;
    context.route_param_count = 1U;
    context.needs_route_params = true;
    if (expect_status(sl_engine_call_function_with_context(
                          engine, &result_arena, sl_str_from_cstr("sloppy_context_route_only"),
                          &context, &result, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 436;
    }
    if (result.kind != SL_ENGINE_RESULT_JSON ||
        expect_bytes_equal(result.response.body, "{\"id\":\"123\",\"request\":\"undefined\","
                                                 "\"log\":\"undefined\"}") != 0)
    {
        sl_engine_destroy(engine);
        return 437;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_request_context_profile_counts_only_requested_facets(void)
{
    unsigned char engine_storage[16384];
    unsigned char result_storage[4096];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlHttpHeader headers[1];
    SlHttpQueryParam query_params[1];
    SlHttpRequestHead request = test_request(SL_HTTP_METHOD_GET);
    SlHttpRequestContext context = {0};
    TestEnvValue profile_env = {0};

    headers[0].name = sl_str_from_cstr("X-Trace");
    headers[0].value = sl_str_from_cstr("trace-1");
    request.headers = headers;
    request.header_count = 1U;
    query_params[0].name = sl_str_from_cstr("q");
    query_params[0].value = sl_str_from_cstr("needle");

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0 ||
        enable_http_profile(&profile_env) != 0)
    {
        return 438;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        restore_http_profile(&profile_env);
        return 439;
    }

    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("v8-profile-facets.js"),
                sl_str_from_cstr(
                    "globalThis.profileNoCtx = function () { return 'ok'; };"
                    "globalThis.profileQuery = function (ctx) { return ctx.query.q; };"
                    "globalThis.profileHeader = function (ctx) { return ctx.header.xTrace; };"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        restore_http_profile(&profile_env);
        return 440;
    }

    context = test_request_context(&request);
    context.needs_route_params = false;
    context.needs_query_params = false;
    context.needs_headers = false;
    context.needs_body = false;
    context.needs_request = false;
    context.needs_connection = false;
    context.needs_signal = false;
    context.needs_log = false;
    context.needs_metadata = false;
    sl_http_profile_reset();
    if (expect_status(sl_engine_call_function_with_context(engine, &result_arena,
                                                           sl_str_from_cstr("profileNoCtx"),
                                                           &context, &result, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        restore_http_profile(&profile_env);
        return 441;
    }
    {
        const char* expected[] = {"\"ctxCreated\": 1",           "\"queryMaterialized\": 0",
                                  "\"headersMaterialized\": 0",  "\"bodyFacadeMaterialized\": 0",
                                  "\"bodyJsonMaterialized\": 0", "\"servicesMaterialized\": 0"};
        if (profile_json_contains_all(expected, sizeof(expected) / sizeof(expected[0])) != 0) {
            sl_engine_destroy(engine);
            restore_http_profile(&profile_env);
            return 442;
        }
    }

    sl_arena_reset(&result_arena);
    result = (SlEngineResult){0};
    context.query_params = query_params;
    context.query_param_count = 1U;
    context.needs_query_params = true;
    sl_http_profile_reset();
    if (expect_status(sl_engine_call_function_with_context(engine, &result_arena,
                                                           sl_str_from_cstr("profileQuery"),
                                                           &context, &result, &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_TEXT ||
        expect_str_equal(result.text, sl_str_from_cstr("needle")) != 0)
    {
        sl_engine_destroy(engine);
        restore_http_profile(&profile_env);
        return 443;
    }
    {
        const char* expected[] = {"\"queryMaterialized\": 1", "\"headersMaterialized\": 0",
                                  "\"bodyFacadeMaterialized\": 0", "\"bodyJsonMaterialized\": 0",
                                  "\"servicesMaterialized\": 0"};
        if (profile_json_contains_all(expected, sizeof(expected) / sizeof(expected[0])) != 0) {
            sl_engine_destroy(engine);
            restore_http_profile(&profile_env);
            return 444;
        }
    }

    sl_arena_reset(&result_arena);
    result = (SlEngineResult){0};
    context.needs_query_params = false;
    context.query_params = NULL;
    context.query_param_count = 0U;
    context.needs_header_facade = true;
    sl_http_profile_reset();
    if (expect_status(sl_engine_call_function_with_context(engine, &result_arena,
                                                           sl_str_from_cstr("profileHeader"),
                                                           &context, &result, &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_TEXT ||
        expect_str_equal(result.text, sl_str_from_cstr("trace-1")) != 0)
    {
        sl_engine_destroy(engine);
        restore_http_profile(&profile_env);
        return 445;
    }
    {
        const char* expected[] = {"\"queryMaterialized\": 0", "\"headersMaterialized\": 1",
                                  "\"bodyFacadeMaterialized\": 0", "\"bodyJsonMaterialized\": 0",
                                  "\"servicesMaterialized\": 0"};
        if (profile_json_contains_all(expected, sizeof(expected) / sizeof(expected[0])) != 0) {
            sl_engine_destroy(engine);
            restore_http_profile(&profile_env);
            return 446;
        }
    }

    sl_engine_destroy(engine);
    if (restore_http_profile(&profile_env) != 0) {
        return 447;
    }
    return 0;
}

static int test_request_context_preserves_binary_body_bytes(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[4096];
    static const unsigned char body[] = {0U, 65U, 0U, 255U};
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlHttpRequestHead request = test_request(SL_HTTP_METHOD_POST);
    SlHttpRequestContext context = {0};

    request.body = sl_bytes_from_parts(body, sizeof(body));
    context = test_request_context(&request);
    context.body_kind = SL_HTTP_REQUEST_BODY_BYTES;
    context.content_type = sl_str_from_cstr("application/octet-stream");
    context.has_content_length = true;
    context.content_length = sizeof(body);

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 406;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 407;
    }

    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("v8-context-binary-body.js"),
                sl_str_from_cstr("globalThis.sloppy_binary_body = function (ctx) {"
                                 "  const bytes = ctx.request.body.bytes();"
                                 "  return { __sloppyResult: true, kind: 'json', status: 200,"
                                 "    contentType: 'application/json; charset=utf-8', body: {"
                                 "      len: bytes.length, first: bytes[0], nul: bytes[2],"
                                 "      last: bytes[3]"
                                 "    } };"
                                 "};"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 408;
    }

    if (expect_status(sl_engine_call_function_with_context(engine, &result_arena,
                                                           sl_str_from_cstr("sloppy_binary_body"),
                                                           &context, &result, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 409;
    }

    if (result.kind != SL_ENGINE_RESULT_JSON ||
        expect_bytes_equal(result.response.body, "{\"len\":4,\"first\":0,\"nul\":0,"
                                                 "\"last\":255}") != 0)
    {
        sl_engine_destroy(engine);
        return 410;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_request_context_exposes_https_scheme_without_native_handles(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[4096];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlHttpRequestHead request = test_request(SL_HTTP_METHOD_GET);
    SlHttpRequestContext context = {0};

    request.version_major = 1U;
    request.version_minor = 1U;
    context = test_request_context(&request);
    context.scheme = sl_str_from_cstr("https");

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 416;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 417;
    }

    if (expect_status(sl_engine_eval_source(
                          engine, sl_str_from_cstr("v8-context-https-scheme.js"),
                          sl_str_from_cstr("globalThis.sloppy_https_scheme = function (ctx) {"
                                           "  const keys = Object.keys(ctx.connection);"
                                           "  return { __sloppyResult: true, kind: 'json',"
                                           "    status: 200, contentType: 'application/json',"
                                           "    body: { requestScheme: ctx.request.scheme,"
                                           "      connectionScheme: ctx.connection.scheme,"
                                           "      secure: ctx.connection.secure,"
                                           "      unsafe: keys.some((key) =>"
                                           "        /socket|libuv|tls|native|handle/i.test(key))"
                                           "    } };"
                                           "};"),
                          &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 418;
    }

    if (expect_status(sl_engine_call_function_with_context(engine, &result_arena,
                                                           sl_str_from_cstr("sloppy_https_scheme"),
                                                           &context, &result, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 419;
    }

    {
        SlStr body_text =
            sl_str_from_parts((const char*)result.response.body.ptr, result.response.body.length);
        if (result.kind != SL_ENGINE_RESULT_JSON ||
            expect_str_contains(body_text, sl_str_from_cstr("\"requestScheme\":\"https\"")) != 0 ||
            expect_str_contains(body_text, sl_str_from_cstr("\"connectionScheme\":\"https\"")) !=
                0 ||
            expect_str_contains(body_text, sl_str_from_cstr("\"secure\":true")) != 0 ||
            expect_str_contains(body_text, sl_str_from_cstr("\"unsafe\":false")) != 0)
        {
            sl_engine_destroy(engine);
            return 420;
        }
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_request_context_body_object_is_one_shot(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[4096];
    static const unsigned char body[] = "{\"ok\":true}";
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlHttpRequestHead request = test_request(SL_HTTP_METHOD_POST);
    SlHttpRequestContext context = {0};

    request.body = sl_bytes_from_parts(body, sizeof(body) - 1U);
    context = test_request_context(&request);
    context.body_kind = SL_HTTP_REQUEST_BODY_JSON;

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 421;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 422;
    }

    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("v8-context-body-one-shot.js"),
                sl_str_from_cstr("globalThis.sloppy_body_one_shot = function (ctx) {"
                                 "  const first = ctx.request.body.text();"
                                 "  let second = 'not-thrown';"
                                 "  try { ctx.request.body.json(); }"
                                 "  catch (error) { second = error instanceof TypeError"
                                 "      && /already consumed/.test(String(error.message))"
                                 "      ? 'consumed' : 'wrong'; }"
                                 "  return { __sloppyResult: true, kind: 'json', status: 200,"
                                 "    contentType: 'application/json', body: {"
                                 "      first, second, consumed: ctx.request.body.consumed"
                                 "    } };"
                                 "};"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 423;
    }

    if (expect_status(sl_engine_call_function_with_context(engine, &result_arena,
                                                           sl_str_from_cstr("sloppy_body_one_shot"),
                                                           &context, &result, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 424;
    }

    {
        SlStr body_text =
            sl_str_from_parts((const char*)result.response.body.ptr, result.response.body.length);
        if (result.kind != SL_ENGINE_RESULT_JSON ||
            expect_str_contains(body_text, sl_str_from_cstr("\"first\":\"{\\\"ok\\\":true}\"")) !=
                0 ||
            expect_str_contains(body_text, sl_str_from_cstr("\"second\":\"consumed\"")) != 0 ||
            expect_str_contains(body_text, sl_str_from_cstr("\"consumed\":true")) != 0)
        {
            sl_engine_destroy(engine);
            return 425;
        }
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_native_validated_json_body_is_materialized_once(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[4096];
    static const unsigned char body[] = "{\"name\":\"Ada\",\"ok\":true}";
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlHttpRequestHead request = test_request(SL_HTTP_METHOD_POST);
    SlHttpRequestContext context = {0};

    request.body = sl_bytes_from_parts(body, sizeof(body) - 1U);
    context = test_request_context(&request);
    context.body_kind = SL_HTTP_REQUEST_BODY_JSON;
    context.native_json_validated = true;

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 426;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 427;
    }

    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("v8-native-json-materialized.js"),
                sl_str_from_cstr("globalThis.sloppy_native_json_body = function (ctx) {"
                                 "  const first = ctx.request.json();"
                                 "  first.name = 'Grace';"
                                 "  const second = ctx.request.json();"
                                 "  const fromBody = ctx.request.body.json();"
                                 "  let after = 'not-thrown';"
                                 "  try { ctx.request.body.json(); }"
                                 "  catch (error) { after = error instanceof TypeError"
                                 "      && /already consumed/.test(String(error.message))"
                                 "      ? 'consumed' : 'wrong'; }"
                                 "  return { __sloppyResult: true, kind: 'json', status: 200,"
                                 "    contentType: 'application/json', body: {"
                                 "      same: first === second, bodySame: first === fromBody,"
                                 "      name: fromBody.name, after,"
                                 "      consumed: ctx.request.body.consumed"
                                 "    } };"
                                 "};"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 428;
    }

    if (expect_status(sl_engine_call_function_with_context(
                          engine, &result_arena, sl_str_from_cstr("sloppy_native_json_body"),
                          &context, &result, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 429;
    }

    {
        SlStr body_text =
            sl_str_from_parts((const char*)result.response.body.ptr, result.response.body.length);
        if (result.kind != SL_ENGINE_RESULT_JSON ||
            expect_str_contains(body_text, sl_str_from_cstr("\"same\":true")) != 0 ||
            expect_str_contains(body_text, sl_str_from_cstr("\"bodySame\":true")) != 0 ||
            expect_str_contains(body_text, sl_str_from_cstr("\"name\":\"Grace\"")) != 0 ||
            expect_str_contains(body_text, sl_str_from_cstr("\"after\":\"consumed\"")) != 0 ||
            expect_str_contains(body_text, sl_str_from_cstr("\"consumed\":true")) != 0)
        {
            sl_engine_destroy(engine);
            return 430;
        }
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_native_schema_json_response_serializes_supported_shape(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[8192];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlHttpRequestHead request = test_request(SL_HTTP_METHOD_GET);
    SlHttpRequestContext context = {0};
    SlPlanSchemaNode string_node = {.kind = SL_PLAN_SCHEMA_STRING};
    SlPlanSchemaNode int_node = {.kind = SL_PLAN_SCHEMA_INT};
    SlPlanSchemaNode number_node = {.kind = SL_PLAN_SCHEMA_NUMBER};
    SlPlanSchemaNode bool_node = {.kind = SL_PLAN_SCHEMA_BOOLEAN};
    SlPlanSchemaNode null_node = {.kind = SL_PLAN_SCHEMA_NULL, .nullable = true};
    SlPlanSchemaNode nested_string_node = {.kind = SL_PLAN_SCHEMA_STRING};
    SlPlanSchemaProperty nested_props[1] = {
        {.name = sl_str_from_cstr("label"), .schema = &nested_string_node}};
    SlPlanSchemaNode nested_node = {
        .properties = nested_props, .property_count = 1U, .kind = SL_PLAN_SCHEMA_OBJECT};
    SlPlanSchemaNode tag_node = {.kind = SL_PLAN_SCHEMA_STRING};
    SlPlanSchemaNode tags_node = {.items = &tag_node, .kind = SL_PLAN_SCHEMA_ARRAY};
    SlPlanSchemaNode optional_node = {.kind = SL_PLAN_SCHEMA_STRING, .optional = true};
    SlPlanSchemaProperty props[9] = {
        {.name = sl_str_from_cstr("name"), .schema = &string_node},
        {.name = sl_str_from_cstr("escaped"), .schema = &string_node},
        {.name = sl_str_from_cstr("count"), .schema = &int_node},
        {.name = sl_str_from_cstr("score"), .schema = &number_node},
        {.name = sl_str_from_cstr("ok"), .schema = &bool_node},
        {.name = sl_str_from_cstr("note"), .schema = &null_node},
        {.name = sl_str_from_cstr("nested"), .schema = &nested_node},
        {.name = sl_str_from_cstr("tags"), .schema = &tags_node},
        {.name = sl_str_from_cstr("optional"), .schema = &optional_node}};
    SlPlanSchemaNode root_node = {
        .properties = props, .property_count = 9U, .kind = SL_PLAN_SCHEMA_OBJECT};
    SlPlanSchema schema = {.name = sl_str_from_cstr("NativeResponse"), .definition = root_node};
    SlStr body = sl_str_empty();

    context = test_request_context(&request);
    context.response_schema = &schema;

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 431;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 432;
    }

    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("v8-native-schema-response.js"),
                sl_str_from_cstr("globalThis.sloppy_native_schema_response = function () {"
                                 "  return { __sloppyResult: true, kind: 'json', status: 200,"
                                 "    contentType: 'application/json', body: {"
                                 "      extra: 'ignored', name: 'Ada',"
                                 "      escaped: 'line\\nquote\"slash\\\\', count: 7, score: 1.5,"
                                 "      ok: true, note: null, nested: { label: 'inner' },"
                                 "      tags: ['a', 'b']"
                                 "    } };"
                                 "};"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 433;
    }

    if (expect_status(sl_engine_call_function_with_context(
                          engine, &result_arena, sl_str_from_cstr("sloppy_native_schema_response"),
                          &context, &result, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 434;
    }

    body = sl_str_from_parts((const char*)result.response.body.ptr, result.response.body.length);
    if (result.kind != SL_ENGINE_RESULT_JSON ||
        !sl_str_equal(
            body, sl_str_from_cstr("{\"name\":\"Ada\",\"escaped\":\"line\\nquote\\\"slash\\\\\","
                                   "\"count\":7,\"score\":1.5,\"ok\":true,\"note\":null,"
                                   "\"nested\":{\"label\":\"inner\"},\"tags\":[\"a\",\"b\"]}")))
    {
        sl_engine_destroy(engine);
        return 435;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_native_schema_json_response_capacity_exceeded(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[196608];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlHttpRequestHead request = test_request(SL_HTTP_METHOD_GET);
    SlHttpRequestContext context = {0};
    SlPlanSchemaNode string_node = {.kind = SL_PLAN_SCHEMA_STRING};
    SlPlanSchemaProperty props[1] = {{.name = sl_str_from_cstr("value"), .schema = &string_node}};
    SlPlanSchemaNode root_node = {
        .properties = props, .property_count = 1U, .kind = SL_PLAN_SCHEMA_OBJECT};
    SlPlanSchema schema = {.name = sl_str_from_cstr("LargeNativeResponse"),
                           .definition = root_node};

    context = test_request_context(&request);
    context.response_schema = &schema;

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 436;
    }
    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 437;
    }
    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("v8-native-schema-response-capacity.js"),
                sl_str_from_cstr("globalThis.sloppy_native_schema_response_capacity = function () {"
                                 "  return { __sloppyResult: true, kind: 'json', status: 200,"
                                 "    contentType: 'application/json',"
                                 "    body: { value: 'a'.repeat(70000) } };"
                                 "};"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 438;
    }
    if (expect_status(sl_engine_call_function_with_context(
                          engine, &result_arena,
                          sl_str_from_cstr("sloppy_native_schema_response_capacity"), &context,
                          &result, &diag),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        diag.code != SL_DIAG_INVALID_HTTP_RESULT)
    {
        sl_engine_destroy(engine);
        return 439;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_unsupported_native_schema_json_response_falls_back_to_generic(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[2048];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlHttpRequestHead request = test_request(SL_HTTP_METHOD_GET);
    SlHttpRequestContext context = {0};
    SlPlanSchemaNode literal_node = {.literal_string = sl_str_from_cstr("active"),
                                     .kind = SL_PLAN_SCHEMA_LITERAL,
                                     .literal_kind = SL_PLAN_SCHEMA_LITERAL_STRING};
    SlPlanSchema schema = {.name = sl_str_from_cstr("UnsupportedResponse"),
                           .definition = literal_node};
    SlStr body = sl_str_empty();

    context = test_request_context(&request);
    context.response_schema = &schema;

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 440;
    }
    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 441;
    }
    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("v8-native-schema-response-fallback.js"),
                sl_str_from_cstr("globalThis.sloppy_native_schema_response_fallback = function () {"
                                 "  return { __sloppyResult: true, kind: 'json', status: 200,"
                                 "    contentType: 'application/json', body: 'active' };"
                                 "};"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 442;
    }
    if (expect_status(sl_engine_call_function_with_context(
                          engine, &result_arena,
                          sl_str_from_cstr("sloppy_native_schema_response_fallback"), &context,
                          &result, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 443;
    }
    body = sl_str_from_parts((const char*)result.response.body.ptr, result.response.body.length);
    if (result.kind != SL_ENGINE_RESULT_JSON || !sl_str_equal(body, sl_str_from_cstr("\"active\"")))
    {
        sl_engine_destroy(engine);
        return 444;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_request_context_helper_functions_are_frozen(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[2048];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlHttpRequestHead request = test_request(SL_HTTP_METHOD_GET);
    SlHttpRequestContext context = {0};

    context = test_request_context(&request);

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 411;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 412;
    }

    if (expect_status(sl_engine_eval_source(
                          engine, sl_str_from_cstr("v8-context-frozen-helpers.js"),
                          sl_str_from_cstr("globalThis.sloppy_frozen_helpers = function (ctx) {"
                                           "  ctx.request.bytes.prototype = { leaked: true };"
                                           "  ctx.request.bytes.bridgeMutation = 'leak';"
                                           "  return Object.isFrozen(ctx.request.bytes)"
                                           "    && ctx.request.bytes.prototype === undefined"
                                           "    && ctx.request.bytes.bridgeMutation === undefined"
                                           "    ? 'frozen' : 'mutable';"
                                           "};"),
                          &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 413;
    }

    if (expect_status(sl_engine_call_function_with_context(
                          engine, &result_arena, sl_str_from_cstr("sloppy_frozen_helpers"),
                          &context, &result, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 414;
    }

    if (result.kind != SL_ENGINE_RESULT_TEXT ||
        expect_str_equal(result.text, sl_str_from_cstr("frozen")) != 0)
    {
        sl_engine_destroy(engine);
        return 415;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_request_context_private_state_ignores_public_mutation(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[2048];
    static const unsigned char body_bytes[] = {'h', 'e', 'l', 'l', 'o'};
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlHttpRequestHead request = test_request(SL_HTTP_METHOD_POST);
    SlHttpRequestContext context = {0};

    request.body = sl_bytes_from_parts(body_bytes, sizeof(body_bytes));
    context = test_request_context(&request);
    context.body_kind = SL_HTTP_REQUEST_BODY_TEXT;

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 416;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 417;
    }

    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("v8-context-private-state.js"),
                sl_str_from_cstr("globalThis.sloppy_context_private_state = function (ctx) {"
                                 "  ctx.signal.aborted = true;"
                                 "  ctx.signal.reason = 'poison';"
                                 "  ctx.request.body.consumed = true;"
                                 "  ctx.request.body.kind = 'json';"
                                 "  let signalOk = 'clear';"
                                 "  try { ctx.signal.throwIfAborted(); }"
                                 "  catch (_err) { signalOk = 'poisoned'; }"
                                 "  const first = ctx.request.body.text();"
                                 "  let second = 'missing';"
                                 "  try { ctx.request.body.text(); }"
                                 "  catch (err) {"
                                 "    second = String(err && err.message ? err.message : err)"
                                 "      .includes('already consumed') ? 'consumed' : 'wrong';"
                                 "  }"
                                 "  return JSON.stringify({ signalOk, first, second });"
                                 "};"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 418;
    }

    if (expect_status(sl_engine_call_function_with_context(
                          engine, &result_arena, sl_str_from_cstr("sloppy_context_private_state"),
                          &context, &result, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 419;
    }

    if (result.kind != SL_ENGINE_RESULT_TEXT ||
        expect_str_equal(
            result.text,
            sl_str_from_cstr(
                "{\"signalOk\":\"clear\",\"first\":\"hello\",\"second\":\"consumed\"}")) != 0)
    {
        sl_engine_destroy(engine);
        return 420;
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

static int test_result_descriptor_fast_paths_cover_common_results(void)
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
        return 441;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 442;
    }

    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("v8-result-fast-paths.js"),
                sl_str_from_cstr("function fast(descriptor, kind, jsonText) {"
                                 "  Object.defineProperty(descriptor, '__sloppyFastResult',"
                                 "    { value: kind });"
                                 "  if (jsonText !== undefined) {"
                                 "    Object.defineProperty(descriptor, '__sloppyJsonText',"
                                 "      { value: jsonText });"
                                 "  }"
                                 "  return Object.freeze(descriptor);"
                                 "}"
                                 "globalThis.sloppy_fast_text = function () {"
                                 "  return fast({ __sloppyResult: true, kind: 'text', status: 200,"
                                 "    contentType: 'text/plain; charset=utf-8', headers: undefined,"
                                 "    body: 'ok' }, 1);"
                                 "};"
                                 "globalThis.sloppy_fast_json = function () {"
                                 "  return fast({ __sloppyResult: true, kind: 'json', status: 200,"
                                 "    contentType: 'application/json; charset=utf-8',"
                                 "    headers: undefined, body: undefined }, 3, '{\"ok\":true}');"
                                 "};"
                                 "globalThis.sloppy_fast_created = function () {"
                                 "  return fast({ __sloppyResult: true, kind: 'json', status: 201,"
                                 "    contentType: 'application/json; charset=utf-8',"
                                 "    headers: undefined, location: '/items/1',"
                                 "    body: undefined }, 4, '{\"id\":1}');"
                                 "};"
                                 "globalThis.sloppy_fast_no_content = function () {"
                                 "  return fast({ __sloppyResult: true, kind: 'empty', status: 204,"
                                 "    contentType: undefined, headers: undefined }, 2);"
                                 "};"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 443;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_fast_text"), &result,
                                               &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_TEXT ||
        result.payload_kind != SL_ENGINE_RESULT_PAYLOAD_RESPONSE ||
        expect_bytes_equal(result.response.body, "ok") != 0)
    {
        sl_engine_destroy(engine);
        return 444;
    }

    sl_arena_reset(&result_arena);
    result = (SlEngineResult){0};
    diag = (SlDiag){0};
    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_fast_json"), &result,
                                               &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_JSON ||
        expect_bytes_equal(result.response.body, "{\"ok\":true}") != 0)
    {
        sl_engine_destroy(engine);
        return 445;
    }

    sl_arena_reset(&result_arena);
    result = (SlEngineResult){0};
    diag = (SlDiag){0};
    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_fast_created"), &result,
                                               &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_JSON || result.response.status != 201U ||
        result.response.header_count != 1U ||
        expect_response_header(&result.response, "Location", "/items/1") != 0 ||
        expect_bytes_equal(result.response.body, "{\"id\":1}") != 0)
    {
        sl_engine_destroy(engine);
        return 446;
    }

    sl_arena_reset(&result_arena);
    result = (SlEngineResult){0};
    diag = (SlDiag){0};
    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_fast_no_content"), &result,
                                               &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_NONE || result.response.status != 204U ||
        result.response.body.length != 0U || result.response.content_type.length != 0U)
    {
        sl_engine_destroy(engine);
        return 447;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_result_descriptor_fast_paths_fall_back_for_mutated_shapes(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[8192];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 448;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 449;
    }

    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("v8-result-fast-fallbacks.js"),
                sl_str_from_cstr(
                    "function mark(descriptor, kind, jsonText) {"
                    "  Object.defineProperty(descriptor, '__sloppyFastResult', { value: kind });"
                    "  if (jsonText !== undefined) {"
                    "    Object.defineProperty(descriptor, '__sloppyJsonText', { value: jsonText "
                    "});"
                    "  }"
                    "  return Object.freeze(descriptor);"
                    "}"
                    "globalThis.sloppy_fast_custom_headers = function () {"
                    "  return mark({ __sloppyResult: true, kind: 'json', status: 200,"
                    "    contentType: 'application/json; charset=utf-8',"
                    "    headers: { 'x-fast': 'fallback' }, body: { ok: true } },"
                    "    3, '{\"fast\":true}');"
                    "};"
                    "globalThis.sloppy_fast_custom_content_type = function () {"
                    "  return mark({ __sloppyResult: true, kind: 'text', status: 200,"
                    "    contentType: 'text/custom', headers: undefined, body: 'ok' }, 1);"
                    "};"
                    "globalThis.sloppy_fast_custom_status = function () {"
                    "  return mark({ __sloppyResult: true, kind: 'text', status: 201,"
                    "    contentType: 'text/plain; charset=utf-8', headers: undefined,"
                    "    body: 'ok' }, 1);"
                    "};"
                    "globalThis.sloppy_fast_invalid_created_location = function () {"
                    "  return mark({ __sloppyResult: true, kind: 'json', status: 201,"
                    "    contentType: 'application/json; charset=utf-8', headers: undefined,"
                    "    location: 'bad\\u0001location', body: { id: 1 } },"
                    "    4, '{\"id\":1}');"
                    "};"
                    "globalThis.sloppy_fast_mutated_text = function () {"
                    "  return mark({ __sloppyResult: true, kind: 'text', status: 200,"
                    "    contentType: 'text/plain; charset=utf-8', headers: undefined,"
                    "    body: 'mutated' }, 1);"
                    "};"
                    "globalThis.sloppy_fast_large_json = function () {"
                    "  return { __sloppyResult: true, kind: 'json', status: 200,"
                    "    contentType: 'application/json; charset=utf-8', headers: undefined,"
                    "    body: { value: 'xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx' } };"
                    "};"
                    "globalThis.sloppy_fast_bad_marker = function () {"
                    "  return mark({ __sloppyResult: true, kind: 'json', status: 200,"
                    "    contentType: 'application/json; charset=utf-8', headers: undefined,"
                    "    body: { marker: 'fallback' } }, 99, '{\"marker\":\"fast\"}');"
                    "};"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 450;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_fast_custom_headers"),
                                               &result, &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_JSON ||
        expect_bytes_equal(result.response.body, "{\"ok\":true}") != 0 ||
        expect_response_header(&result.response, "x-fast", "fallback") != 0)
    {
        sl_engine_destroy(engine);
        return 451;
    }

    sl_arena_reset(&result_arena);
    result = (SlEngineResult){0};
    diag = (SlDiag){0};
    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_fast_custom_content_type"),
                                               &result, &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_TEXT ||
        expect_str_equal(result.response.content_type, sl_str_from_cstr("text/custom")) != 0)
    {
        sl_engine_destroy(engine);
        return 452;
    }

    sl_arena_reset(&result_arena);
    result = (SlEngineResult){0};
    diag = (SlDiag){0};
    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_fast_custom_status"),
                                               &result, &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_TEXT || result.response.status != 201U)
    {
        sl_engine_destroy(engine);
        return 453;
    }

    sl_arena_reset(&result_arena);
    result = (SlEngineResult){0};
    diag = (SlDiag){0};
    if (expect_status(sl_engine_call_function0(
                          engine, &result_arena,
                          sl_str_from_cstr("sloppy_fast_invalid_created_location"), &result, &diag),
                      SL_STATUS_INVALID_STATE) != 0 ||
        diag.code != SL_DIAG_INVALID_HTTP_RESULT)
    {
        sl_engine_destroy(engine);
        return 454;
    }

    sl_arena_reset(&result_arena);
    result = (SlEngineResult){0};
    diag = (SlDiag){0};
    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_fast_mutated_text"),
                                               &result, &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_TEXT ||
        result.payload_kind != SL_ENGINE_RESULT_PAYLOAD_RESPONSE ||
        expect_bytes_equal(result.response.body, "mutated") != 0)
    {
        sl_engine_destroy(engine);
        return 455;
    }

    sl_arena_reset(&result_arena);
    result = (SlEngineResult){0};
    diag = (SlDiag){0};
    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_fast_large_json"), &result,
                                               &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_JSON || result.response.body.length <= 256U)
    {
        sl_engine_destroy(engine);
        return 456;
    }

    sl_arena_reset(&result_arena);
    result = (SlEngineResult){0};
    diag = (SlDiag){0};
    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_fast_bad_marker"), &result,
                                               &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_JSON ||
        expect_bytes_equal(result.response.body, "{\"marker\":\"fallback\"}") != 0)
    {
        sl_engine_destroy(engine);
        return 457;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_result_descriptor_inherited_fields_do_not_poison_headers(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[4096];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 458;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 459;
    }

    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("v8-result-inherited-fields.js"),
                sl_str_from_cstr(
                    "globalThis.sloppy_inherited_fields = function () {"
                    "  const proto = { headers: { 'x-inherited': 'poison' }, location: '/proto' };"
                    "  const descriptor = Object.create(proto);"
                    "  descriptor.__sloppyResult = true;"
                    "  descriptor.kind = 'json';"
                    "  descriptor.status = 200;"
                    "  descriptor.contentType = 'application/json; charset=utf-8';"
                    "  descriptor.body = { ok: true };"
                    "  return descriptor;"
                    "};"
                    "globalThis.sloppy_inherited_created_location = function () {"
                    "  const proto = { location: '/proto' };"
                    "  const descriptor = Object.create(proto);"
                    "  descriptor.__sloppyResult = true;"
                    "  Object.defineProperty(descriptor, '__sloppyFastResult', { value: 4 });"
                    "  Object.defineProperty(descriptor, '__sloppyJsonText',"
                    "    { value: '{\\\"spoofed\\\":true}' });"
                    "  descriptor.kind = 'json';"
                    "  descriptor.status = 201;"
                    "  descriptor.contentType = 'application/json; charset=utf-8';"
                    "  descriptor.headers = undefined;"
                    "  descriptor.body = { ownedCreated: true };"
                    "  return descriptor;"
                    "};"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 460;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_inherited_fields"), &result,
                                               &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_JSON || result.response.header_count != 0U ||
        expect_bytes_equal(result.response.body, "{\"ok\":true}") != 0)
    {
        sl_engine_destroy(engine);
        return 461;
    }

    sl_arena_reset(&result_arena);
    result = (SlEngineResult){0};
    diag = (SlDiag){0};
    if (expect_status(sl_engine_call_function0(
                          engine, &result_arena,
                          sl_str_from_cstr("sloppy_inherited_created_location"), &result, &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_JSON || result.response.status != 201U ||
        result.response.header_count != 0U ||
        expect_bytes_equal(result.response.body, "{\"ownedCreated\":true}") != 0)
    {
        sl_engine_destroy(engine);
        return 462;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_result_descriptor_inherited_core_fields_do_not_admit_descriptor(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[4096];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 466;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 467;
    }

    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("v8-result-inherited-core-fields.js"),
                sl_str_from_cstr("globalThis.sloppy_inherited_core_fields = function () {"
                                 "  const proto = { __sloppyResult: true, kind: 'json',"
                                 "    status: 200, contentType: 'application/json; charset=utf-8',"
                                 "    body: { spoofed: true } };"
                                 "  return Object.create(proto);"
                                 "};"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 468;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_inherited_core_fields"),
                                               &result, &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_JSON || expect_bytes_equal(result.response.body, "{}") != 0)
    {
        sl_engine_destroy(engine);
        return 469;
    }

    sl_engine_destroy(engine);
    return 0;
}
static int test_result_descriptor_inherited_fast_markers_do_not_spoof_body(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[4096];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 462;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 463;
    }

    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("v8-result-inherited-fast-markers.js"),
                sl_str_from_cstr("globalThis.sloppy_inherited_fast_markers = function () {"
                                 "  const proto = { __sloppyFastResult: 3, __sloppyJsonText: "
                                 "'{\"spoofed\":true}' };"
                                 "  const descriptor = Object.create(proto);"
                                 "  descriptor.__sloppyResult = true;"
                                 "  descriptor.kind = 'json';"
                                 "  descriptor.status = 200;"
                                 "  descriptor.contentType = 'application/json; charset=utf-8';"
                                 "  descriptor.headers = undefined;"
                                 "  descriptor.body = { owned: true };"
                                 "  return descriptor;"
                                 "};"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 464;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_inherited_fast_markers"),
                                               &result, &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_JSON ||
        expect_bytes_equal(result.response.body, "{\"owned\":true}") != 0)
    {
        sl_engine_destroy(engine);
        return 465;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_result_descriptor_preserves_binary_body(void)
{
    unsigned char engine_storage[8192];
    unsigned char result_storage[2048];
    static const unsigned char expected[] = {0U, 65U, 0U, 255U};
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 96;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 97;
    }

    if (expect_status(
            sl_engine_eval_source(engine, sl_str_from_cstr("v8-result-bytes.js"),
                                  sl_str_from_cstr("globalThis.sloppy_result_bytes = function () {"
                                                   "  return { __sloppyResult: true, kind: 'bytes',"
                                                   "    status: 200, contentType: "
                                                   "    'application/x-test',"
                                                   "    headers: { 'x-result': 'bytes' },"
                                                   "    body: new Uint8Array([0, 65, 0, 255]) };"
                                                   "};"),
                                  &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 98;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_result_bytes"), &result,
                                               &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 99;
    }

    if (result.kind != SL_ENGINE_RESULT_BYTES || result.response.status != 200U ||
        !sl_str_equal(result.response.content_type, sl_str_from_cstr("application/x-test")) ||
        expect_response_header(&result.response, "x-result", "bytes") != 0 ||
        expect_bytes_exact(result.response.body, expected, sizeof(expected)) != 0)
    {
        sl_engine_destroy(engine);
        return 100;
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
                                           "};"
                                           "globalThis.sloppy_invalid_transfer_header = function "
                                           "() {"
                                           "  return { __sloppyResult: true, kind: 'text',"
                                           "    status: 200, contentType: "
                                           "    'text/plain; charset=utf-8',"
                                           "    headers: { 'Transfer-Encoding': 'chunked' },"
                                           "    body: 'bad' };"
                                           "};"
                                           "globalThis.sloppy_invalid_keep_alive_header = function "
                                           "() {"
                                           "  return { __sloppyResult: true, kind: 'text',"
                                           "    status: 200, contentType: "
                                           "    'text/plain; charset=utf-8',"
                                           "    headers: { 'Keep-Alive': 'timeout=5' },"
                                           "    body: 'bad' };"
                                           "};"
                                           "globalThis.sloppy_invalid_header_value = function () {"
                                           "  return { __sloppyResult: true, kind: 'text',"
                                           "    status: 200, contentType: "
                                           "    'text/plain; charset=utf-8',"
                                           "    headers: { 'x-result': 'bad\\u0000value' },"
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

    sl_arena_reset(&result_arena);
    result = (SlEngineResult){0};
    diag = (SlDiag){0};
    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_invalid_transfer_header"),
                                               &result, &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        sl_engine_destroy(engine);
        return 90;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_INVALID_HTTP_RESULT) {
        sl_engine_destroy(engine);
        return 91;
    }

    sl_arena_reset(&result_arena);
    result = (SlEngineResult){0};
    diag = (SlDiag){0};
    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_invalid_keep_alive_header"),
                                               &result, &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        sl_engine_destroy(engine);
        return 92;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_INVALID_HTTP_RESULT) {
        sl_engine_destroy(engine);
        return 93;
    }

    sl_arena_reset(&result_arena);
    result = (SlEngineResult){0};
    diag = (SlDiag){0};
    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sloppy_invalid_header_value"),
                                               &result, &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        sl_engine_destroy(engine);
        return 94;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_INVALID_HTTP_RESULT) {
        sl_engine_destroy(engine);
        return 95;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_result_descriptor_throwing_headers_accessor_fails_safely(void)
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
        return 101;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 102;
    }

    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("v8-throwing-result-headers.js"),
                sl_str_from_cstr(
                    "globalThis.sloppy_throwing_headers = function () {"
                    "  const descriptor = { __sloppyResult: true, kind: 'text', status: 200,"
                    "    contentType: 'text/plain; charset=utf-8', body: 'ok' };"
                    "  Object.defineProperty(descriptor, 'headers', {"
                    "    get() { throw new Error('poison'); }"
                    "  });"
                    "  return descriptor;"
                    "};"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 103;
    }

    {
        SlStatus call_status = sl_engine_call_function0(
            engine, &result_arena, sl_str_from_cstr("sloppy_throwing_headers"), &result, &diag);
        if (expect_status(call_status, SL_STATUS_INVALID_STATE) != 0 ||
            result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_INVALID_HTTP_RESULT ||
            expect_str_contains(diag.message, sl_str_from_cstr("invalid headers")) != 0)
        {
            fprintf(stderr, "throwing headers status=%d result=%d diag=%d message=%.*s\n",
                    (int)sl_status_code(call_status), (int)result.kind, (int)diag.code,
                    (int)diag.message.length, diag.message.ptr == NULL ? "" : diag.message.ptr);
            sl_engine_destroy(engine);
            return 104;
        }
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_result_descriptor_throwing_core_accessors_fail_safely(void)
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
        return 111;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 112;
    }

    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("v8-throwing-result-core-accessors.js"),
                sl_str_from_cstr(
                    "function descriptorWithThrowing(name) {"
                    "  const descriptor = { __sloppyResult: true, kind: 'json', status: 200,"
                    "    contentType: 'application/json; charset=utf-8', body: { ok: true } };"
                    "  Object.defineProperty(descriptor, name, {"
                    "    get() { throw new Error('poison ' + name); }"
                    "  });"
                    "  return descriptor;"
                    "}"
                    "globalThis.sloppy_throwing_kind = function () {"
                    "  return descriptorWithThrowing('kind');"
                    "};"
                    "globalThis.sloppy_throwing_status = function () {"
                    "  return descriptorWithThrowing('status');"
                    "};"
                    "globalThis.sloppy_throwing_content_type = function () {"
                    "  return descriptorWithThrowing('contentType');"
                    "};"
                    "globalThis.sloppy_throwing_body = function () {"
                    "  return descriptorWithThrowing('body');"
                    "};"
                    "globalThis.sloppy_throwing_location = function () {"
                    "  return descriptorWithThrowing('location');"
                    "};"
                    "globalThis.sloppy_throwing_location_own_descriptor = function () {"
                    "  const descriptor = { __sloppyResult: true, kind: 'json', status: 201,"
                    "    contentType: 'application/json; charset=utf-8', body: { ok: true } };"
                    "  return new Proxy(descriptor, {"
                    "    getOwnPropertyDescriptor(target, prop) {"
                    "      if (prop === 'location') { throw new Error('location trap'); }"
                    "      return Reflect.getOwnPropertyDescriptor(target, prop);"
                    "    }"
                    "  });"
                    "};"
                    "globalThis.sloppy_throwing_headers_own_keys = function () {"
                    "  return { __sloppyResult: true, kind: 'text', status: 200,"
                    "    contentType: 'text/plain; charset=utf-8', body: 'ok',"
                    "    headers: new Proxy({}, { ownKeys() { throw new Error('keys'); } }) };"
                    "};"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 113;
    }

#define EXPECT_THROWING_DESCRIPTOR(function_name, failure_code)                                    \
    do {                                                                                           \
        sl_arena_reset(&result_arena);                                                             \
        result = (SlEngineResult){0};                                                              \
        diag = (SlDiag){0};                                                                        \
        if (expect_status(sl_engine_call_function0(engine, &result_arena,                          \
                                                   sl_str_from_cstr(function_name), &result,       \
                                                   &diag),                                         \
                          SL_STATUS_INVALID_STATE) != 0 ||                                         \
            result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_INVALID_HTTP_RESULT)      \
        {                                                                                          \
            sl_engine_destroy(engine);                                                             \
            return failure_code;                                                                   \
        }                                                                                          \
    } while (0)

    EXPECT_THROWING_DESCRIPTOR("sloppy_throwing_kind", 114);
    EXPECT_THROWING_DESCRIPTOR("sloppy_throwing_status", 115);
    EXPECT_THROWING_DESCRIPTOR("sloppy_throwing_content_type", 116);
    EXPECT_THROWING_DESCRIPTOR("sloppy_throwing_body", 117);
    EXPECT_THROWING_DESCRIPTOR("sloppy_throwing_location", 118);
    EXPECT_THROWING_DESCRIPTOR("sloppy_throwing_location_own_descriptor", 119);
    EXPECT_THROWING_DESCRIPTOR("sloppy_throwing_headers_own_keys", 120);

#undef EXPECT_THROWING_DESCRIPTOR

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
                               SL_DIAG_V8_UNHANDLED_REJECTION) != 0)
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

static int test_registered_websocket_handler_echoes_native_message(void)
{
    unsigned char engine_storage[16384];
    SlArena engine_arena = {0};
    SlEngineOptions options = v8_options();
    SlEngine* engine = NULL;
    SlDiag diag = {0};
    SlHttpRequestHead request = test_request(SL_HTTP_METHOD_GET);
    SlHttpRequestContext context = test_request_context(&request);
    TestWebSocketBridge bridge_state = {0};
    SlEngineWebSocketBridge bridge = {0};
    SlEngineWebSocketSession* session = NULL;
    const unsigned char payload[] = {'h', '\0', 'i'};
    const unsigned char expected_echo[] = {'e', 'c', 'h', 'o', ':', 'h', '\0', 'i'};
    SlWebSocketFrame frame = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0) {
        return 94;
    }

    bridge.user = &bridge_state;
    bridge.protocol = sl_str_from_cstr("sloppy.realtime");
    bridge.send = test_websocket_bridge_send;
    bridge.close = test_websocket_bridge_close;

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0 ||
        expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("v8-websocket-register.js"),
                sl_str_from_cstr(
                    "__sloppy_register_handler(2, async function (ctx) {"
                    "  const socket = ctx.__sloppyWebSocket;"
                    "  if (socket.protocol !== 'sloppy.realtime') throw new Error('bad protocol');"
                    "  await socket.accept();"
                    "  const first = await socket.messages().next();"
                    "  if (first.value.kind !== 'text') throw new Error('missing kind');"
                    "  if (first.value.text.length !== 3) throw new Error('truncated text');"
                    "  await socket.sendText('echo:' + first.value.text);"
                    "  await socket.close(1001, 'bye');"
                    "});"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 95;
    }

    if (expect_status(sl_engine_call_registered_websocket_handler_with_context(
                          engine, &engine_arena, 2U, &context, &bridge, &session, &diag),
                      SL_STATUS_OK) != 0 ||
        session == NULL)
    {
        sl_engine_destroy(engine);
        return 96;
    }

    frame.fin = true;
    frame.opcode = SL_WEBSOCKET_OPCODE_TEXT;
    frame.payload = sl_bytes_from_parts(payload, sizeof(payload));

    if (expect_status(sl_engine_websocket_receive(engine, session, &frame, &diag), SL_STATUS_OK) !=
        0)
    {
        sl_engine_destroy(engine);
        return 97;
    }

    if (bridge_state.send_count != 1U || bridge_state.last_opcode != SL_WEBSOCKET_OPCODE_TEXT ||
        bridge_state.last_payload_length != sizeof(expected_echo) ||
        memcmp(bridge_state.last_payload, expected_echo, sizeof(expected_echo)) != 0)
    {
        sl_engine_destroy(engine);
        return 98;
    }

    if (bridge_state.close_count != 1U || bridge_state.close_code != 1001U ||
        bridge_state.close_reason_length != strlen("bye") ||
        memcmp(bridge_state.close_reason, "bye", strlen("bye")) != 0)
    {
        sl_engine_destroy(engine);
        return 99;
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
                    "globalThis.sqliteSmoke = async function () {"
                    "  const db = __sloppy.data.sqlite.open({ provider: 'sqlite', database: "
                    "':memory:', capability: 'data.main' });"
                    "  if ('slot' in db || 'generation' in db) {"
                    "    throw new Error('sqlite handle leaked resource internals');"
                    "  }"
                    "  let forgedDbRejected = false;"
                    "  try { await __sloppy.data.sqlite.queryOne({ slot: 0, generation: 1 }, "
                    "'select 1', []); }"
                    "  catch (error) { forgedDbRejected = String(error.message).includes('opaque "
                    "Sloppy handle'); }"
                    "  if (!forgedDbRejected) { throw new Error('forged sqlite handle was "
                    "accepted'); }"
                    "  await __sloppy.data.sqlite.exec(db, 'create table users (id integer primary "
                    "key, "
                    "name text not null, raw blob)', []);"
                    "  await __sloppy.data.sqlite.exec(db, \"insert into users (name, raw) values "
                    "(?, "
                    "x'0041ff')\", "
                    "['Ada']);"
                    "  await __sloppy.data.sqlite.transactionBegin(db);"
                    "  await __sloppy.data.sqlite.transactionExec(db, \"insert into users (name, "
                    "raw) "
                    "values (?, x'01')\", ['Grace']);"
                    "  await __sloppy.data.sqlite.transactionCommit(db);"
                    "  await __sloppy.data.sqlite.transactionBegin(db);"
                    "  await __sloppy.data.sqlite.transactionExec(db, \"insert into users (name, "
                    "raw) "
                    "values (?, x'02')\", ['Rollback']);"
                    "  await __sloppy.data.sqlite.transactionRollback(db);"
                    "  const row = await __sloppy.data.sqlite.queryOne(db, 'select name, raw from "
                    "users "
                    "where id = ?', [1]);"
                    "  const rows = await __sloppy.data.sqlite.query(db, 'select name from users "
                    "order "
                    "by id', []);"
                    "  const duplicate = await __sloppy.data.sqlite.query(db, 'select 1 as same, "
                    "2 as same', []);"
                    "  const rawDuplicate = await __sloppy.data.sqlite.queryRaw(db, 'select 1 as "
                    "same, 2 as same', []);"
                    "  await __sloppy.data.sqlite.transactionBegin(db);"
                    "  const txRaw = await __sloppy.data.sqlite.transactionQueryRaw(db, 'select "
                    "count(*) as count from users', []);"
                    "  await __sloppy.data.sqlite.transactionCommit(db);"
                    "  const typed = await __sloppy.data.sqlite.queryOne(db, 'select typeof(?) as "
                    "kind', "
                    "[9007199254740991]);"
                    "  const big = await __sloppy.data.sqlite.queryOne(db, 'select "
                    "9007199254740993 as value', []);"
                    "  const bigParam = await __sloppy.data.sqlite.queryOne(db, 'select "
                    "typeof(?) as kind, ? as value', [9007199254740993n, "
                    "9007199254740993n]);"
                    "  const raw = Array.from(row.raw);"
                    "  __sloppy.data.sqlite.close(db);"
                    "  return { __sloppyResult: true, kind: 'json', status: 200, "
                    "    contentType: 'application/json; charset=utf-8', body: { rowName: "
                    "row.name, rawIsBytes: row.raw instanceof Uint8Array, raw, rows, "
                    "rowsMode: rows.mode, rowsColumnNames: rows.columnNames, "
                    "rowsColumns: rows.columns.map((c) => c.name + ':' + c.index), "
                    "rowKeys: Object.keys(rows[0]), rowsJson: JSON.stringify(rows), "
                    "duplicateValue: duplicate[0].same, duplicateColumnNames: "
                    "duplicate.columnNames, rawMode: rawDuplicate.mode, "
                    "rawDuplicateRows: rawDuplicate.rows, rawDuplicateColumnNames: "
                    "rawDuplicate.columnNames, txRawCount: txRaw.rows[0][0], typed, "
                    "bigType: typeof big.value, bigValue: big.value.toString(), "
                    "bigParamType: typeof bigParam.value, bigParamValue: "
                    "bigParam.value.toString(), bigParamKind: bigParam.kind } };"
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
        expect_bytes_equal(
            result.response.body,
            "{\"rowName\":\"Ada\",\"rawIsBytes\":true,\"raw\":[0,65,255],"
            "\"rows\":[{\"name\":\"Ada\"},{\"name\":\"Grace\"}],"
            "\"rowsMode\":\"object\",\"rowsColumnNames\":[\"name\"],"
            "\"rowsColumns\":[\"name:0\"],\"rowKeys\":[\"name\"],"
            "\"rowsJson\":\"[{\\\"name\\\":\\\"Ada\\\"},{\\\"name\\\":\\\"Grace\\\"}]\","
            "\"duplicateValue\":2,\"duplicateColumnNames\":[\"same\",\"same\"],"
            "\"rawMode\":\"raw\",\"rawDuplicateRows\":[[1,2]],"
            "\"rawDuplicateColumnNames\":[\"same\",\"same\"],\"txRawCount\":2,"
            "\"typed\":{\"kind\":\"integer\"},\"bigType\":\"bigint\","
            "\"bigValue\":\"9007199254740993\",\"bigParamType\":\"bigint\","
            "\"bigParamValue\":\"9007199254740993\","
            "\"bigParamKind\":\"integer\"}") != 0)
    {
        sl_engine_destroy(engine);
        return 135;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_sqlite_intrinsic_query_max_rows_option(void)
{
    unsigned char engine_storage[262144];
    unsigned char result_storage[65536];
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
        return 1360;
    }

    if (attach_sqlite_plan(&options, &plan, &registry, providers, capabilities, &engine_arena,
                           &features, "readwrite") != 0)
    {
        return 1361;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 1362;
    }

    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("sqlite-max-rows.js"),
                sl_str_from_cstr(
                    "globalThis.sqliteMaxRows = async function () {"
                    "  const db = __sloppy.data.sqlite.open({ provider: 'sqlite', database: "
                    "':memory:', capability: 'data.main' });"
                    "  await __sloppy.data.sqlite.exec(db, 'create table users (id integer "
                    "primary key, name text not null)', []);"
                    "  await __sloppy.data.sqlite.exec(db, \"insert into users (name) values "
                    "('Ada'), ('Grace')\", []);"
                    "  const rows = await __sloppy.data.sqlite.query(db, 'select name from users "
                    "order by id', [], { maxRows: 2 });"
                    "  let queryRejected = false;"
                    "  try { await __sloppy.data.sqlite.query(db, 'select name from users order "
                    "by id', [], { maxRows: 1 }); }"
                    "  catch (error) { queryRejected = String(error.message).includes('exceeded "
                    "max rows'); }"
                    "  let rawRejected = false;"
                    "  try { await __sloppy.data.sqlite.queryRaw(db, 'select name from users "
                    "order by id', [], { maxRows: 1 }); }"
                    "  catch (error) { rawRejected = String(error.message).includes('exceeded "
                    "max rows'); }"
                    "  await __sloppy.data.sqlite.exec(db, 'with recursive cnt(x) as (select 1 "
                    "union all select x + 1 from cnt where x < 150) insert into users (name) "
                    "select char(85,115,101,114,45) || x from cnt', []);"
                    "  const cursor = await __sloppy.data.sqlite.queryCursor(db, 'select name "
                    "from users order by id', [], { batchSize: 32 });"
                    "  let cursorCount = 0;"
                    "  let cursorFirst = null;"
                    "  let cursorLast = null;"
                    "  try {"
                    "    for (;;) {"
                    "      const item = await __sloppy.data.sqlite.cursorNext(cursor);"
                    "      if (item.done) break;"
                    "      cursorCount += 1;"
                    "      cursorFirst = cursorFirst ?? item.value.name;"
                    "      cursorLast = item.value.name;"
                    "    }"
                    "  } finally {"
                    "    __sloppy.data.sqlite.cursorClose(cursor);"
                    "  }"
                    "  let cursorAfterCloseRejected = false;"
                    "  try { await __sloppy.data.sqlite.cursorNext(cursor); }"
                    "  catch (error) { cursorAfterCloseRejected = true; }"
                    "  const rawCursor = await __sloppy.data.sqlite.queryRawCursor(db, 'select "
                    "name from users order by id', [], { maxRows: 2 });"
                    "  let cursorMaxRowsRejected = false;"
                    "  try {"
                    "    await __sloppy.data.sqlite.cursorNext(rawCursor);"
                    "    await __sloppy.data.sqlite.cursorNext(rawCursor);"
                    "    await __sloppy.data.sqlite.cursorNext(rawCursor);"
                    "  } catch (error) { cursorMaxRowsRejected = "
                    "String(error.message).includes('exceeded max rows'); }"
                    "  finally { __sloppy.data.sqlite.cursorClose(rawCursor); }"
                    "  let timeoutRejected = false;"
                    "  try { await __sloppy.data.sqlite.query(db, 'with recursive cnt(x) as "
                    "(select 1 union all select x + 1 from cnt where x < 1000000) select "
                    "count(*) as total from cnt', [], { timeoutMs: 1 }); }"
                    "  catch (error) { timeoutRejected = String(error.message).includes("
                    "'deadline was exceeded'); }"
                    "  await __sloppy.data.sqlite.transactionBegin(db);"
                    "  const txRaw = await __sloppy.data.sqlite.transactionQueryRaw(db, 'select "
                    "name from users where id <= 2 order by id', [], { maxRows: 2 });"
                    "  await __sloppy.data.sqlite.transactionCommit(db);"
                    "  __sloppy.data.sqlite.close(db);"
                    "  return { __sloppyResult: true, kind: 'json', status: 200, contentType: "
                    "'application/json; charset=utf-8', body: { rowsLength: rows.length, "
                    "queryRejected, rawRejected, cursorCount, cursorFirst, cursorLast, "
                    "cursorAfterCloseRejected, cursorMaxRowsRejected, timeoutRejected, "
                    "txRowsLength: txRaw.rows.length "
                    "} };"
                    "};"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 1363;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sqliteMaxRows"), &result, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 1364;
    }

    if (result.kind != SL_ENGINE_RESULT_JSON ||
        expect_bytes_equal(result.response.body,
                           "{\"rowsLength\":2,\"queryRejected\":true,\"rawRejected\":true,"
                           "\"cursorCount\":152,\"cursorFirst\":\"Ada\","
                           "\"cursorLast\":\"User-150\","
                           "\"cursorAfterCloseRejected\":true,"
                           "\"cursorMaxRowsRejected\":true,\"timeoutRejected\":true,"
                           "\"txRowsLength\":2}") != 0)
    {
        sl_engine_destroy(engine);
        return 1365;
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
                                           "    let sawUndefined = false;"
                                           "    try {"
                                           "      __sloppy.data.sqlite.exec(db, 'select ?', "
                                           "[undefined]);"
                                           "    } catch (error) {"
                                           "      sawUndefined = "
                                           "String(error.message).includes('undefined "
                                           "parameters');"
                                           "    }"
                                           "    if (!sawUndefined) {"
                                           "      throw new Error('sqlite undefined parameter was "
                                           "not rejected');"
                                           "    }"
                                           "    let sawUnsafeNumber = false;"
                                           "    try {"
                                           "      __sloppy.data.sqlite.exec(db, 'select ?', "
                                           "[9007199254740993]);"
                                           "    } catch (error) {"
                                           "      sawUnsafeNumber = "
                                           "String(error.message).includes('safe integer');"
                                           "    }"
                                           "    if (!sawUnsafeNumber) {"
                                           "      throw new Error('sqlite unsafe integer "
                                           "parameter was not rejected');"
                                           "    }"
                                           "    let sawUnsafeNegativeNumber = false;"
                                           "    try {"
                                           "      __sloppy.data.sqlite.exec(db, 'select ?', "
                                           "[-9007199254740993]);"
                                           "    } catch (error) {"
                                           "      sawUnsafeNegativeNumber = "
                                           "String(error.message).includes('safe integer');"
                                           "    }"
                                           "    if (!sawUnsafeNegativeNumber) {"
                                           "      throw new Error('sqlite negative unsafe "
                                           "integer parameter was not rejected');"
                                           "    }"
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
                    "globalThis.sqliteDenied = async function () {"
                    "  const db = __sloppy.data.sqlite.open({ provider: 'sqlite', database: "
                    "':memory:', capability: 'data.main', access: 'write' });"
                    "  try {"
                    "    await __sloppy.data.sqlite.exec(db, 'create table users (id integer)', "
                    "[]);"
                    "    await __sloppy.data.sqlite.queryOne(db, 'select id from users', []);"
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

    if (diag.code != SL_DIAG_V8_UNHANDLED_REJECTION ||
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

    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("sqlite-write-only.js"),
                sl_str_from_cstr("globalThis.sqliteWriteOnly = async function () {"
                                 "  const db = __sloppy.data.sqlite.open({ provider: "
                                 "'sqlite', database: ':memory:', capability: "
                                 "'data.main', access: 'write' });"
                                 "  try {"
                                 "    await __sloppy.data.sqlite.exec(db, 'create table t "
                                 "(id integer)', []);"
                                 "    await __sloppy.data.sqlite.query(db, 'select id from t', "
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

    if (diag.code != SL_DIAG_V8_UNHANDLED_REJECTION ||
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

static int test_sqlite_intrinsic_close_pending_then_stale_handle(void)
{
    unsigned char engine_storage[16384];
    unsigned char result_storage[2048];
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
        return 216;
    }

    if (attach_sqlite_plan(&options, &plan, &registry, providers, capabilities, &engine_arena,
                           &features, "readwrite") != 0)
    {
        return 217;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 218;
    }

    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("sqlite-close-pending.js"),
                sl_str_from_cstr(
                    "globalThis.sqliteClosePending = async function () {"
                    "  const db = __sloppy.data.sqlite.open({ provider: 'data.main' });"
                    "  await __sloppy.data.sqlite.exec(db, 'create table t (id integer)', []);"
                    "  const pending = __sloppy.data.sqlite.exec(db, 'insert into t values (1)', "
                    "[]);"
                    "  let closeRejected = false;"
                    "  try { __sloppy.data.sqlite.close(db); }"
                    "  catch (error) { closeRejected = String(error.message).includes('pending "
                    "operations'); }"
                    "  await pending;"
                    "  __sloppy.data.sqlite.close(db);"
                    "  let staleRejected = false;"
                    "  try { await __sloppy.data.sqlite.queryOne(db, 'select 1', []); }"
                    "  catch (error) { staleRejected = String(error.message).includes('stale'); }"
                    "  return { __sloppyResult: true, kind: 'json', status: 200,"
                    "    contentType: 'application/json; charset=utf-8',"
                    "    body: { closeRejected, staleRejected } };"
                    "};"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 219;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sqliteClosePending"), &result,
                                               &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_JSON ||
        expect_bytes_equal(result.response.body,
                           "{\"closeRejected\":true,\"staleRejected\":true}") != 0)
    {
        sl_engine_destroy(engine);
        return 220;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_sqlite_intrinsic_transaction_misuse_and_recovery(void)
{
    unsigned char engine_storage[16384];
    unsigned char result_storage[2048];
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
        return 221;
    }

    if (attach_sqlite_plan(&options, &plan, &registry, providers, capabilities, &engine_arena,
                           &features, "readwrite") != 0)
    {
        return 222;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 223;
    }

    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("sqlite-transaction-misuse.js"),
                sl_str_from_cstr(
                    "globalThis.sqliteTransactionMisuse = async function () {"
                    "  const db = __sloppy.data.sqlite.open({ provider: 'data.main' });"
                    "  await __sloppy.data.sqlite.exec(db, 'create table t (id integer)', []);"
                    "  await __sloppy.data.sqlite.transactionBegin(db);"
                    "  let nestedRejected = false;"
                    "  try { await __sloppy.data.sqlite.transactionBegin(db); }"
                    "  catch (error) { nestedRejected = String(error.message).includes('nested "
                    "transactions'); }"
                    "  await __sloppy.data.sqlite.transactionRollback(db);"
                    "  let commitRejected = false;"
                    "  try { await __sloppy.data.sqlite.transactionCommit(db); }"
                    "  catch (error) { commitRejected = true; }"
                    "  let rollbackRejected = false;"
                    "  try { await __sloppy.data.sqlite.transactionRollback(db); }"
                    "  catch (error) { rollbackRejected = true; }"
                    "  await __sloppy.data.sqlite.transactionBegin(db);"
                    "  await __sloppy.data.sqlite.transactionExec(db, 'insert into t values (1)', "
                    "[]);"
                    "  await __sloppy.data.sqlite.transactionRollback(db);"
                    "  const row = await __sloppy.data.sqlite.queryOne(db, 'select count(*) as "
                    "count from t', []);"
                    "  __sloppy.data.sqlite.close(db);"
                    "  return { __sloppyResult: true, kind: 'json', status: 200,"
                    "    contentType: 'application/json; charset=utf-8',"
                    "    body: { nestedRejected, commitRejected, rollbackRejected, count: "
                    "row.count } };"
                    "};"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 224;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("sqliteTransactionMisuse"), &result,
                                               &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_JSON ||
        expect_bytes_equal(result.response.body, "{\"nestedRejected\":true,\"commitRejected\":true,"
                                                 "\"rollbackRejected\":true,\"count\":0}") != 0)
    {
        sl_engine_destroy(engine);
        return 225;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_provider_bridge_non_live_invalid_shapes(void)
{
    unsigned char engine_storage[32768];
    unsigned char result_storage[2048];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlEngineOptions options = v8_options();
    SlPlanDataProvider providers[2] = {{.token = sl_str_from_cstr("data.pg"),
                                        .provider = sl_str_from_cstr("postgres"),
                                        .capability = sl_str_empty(),
                                        .service = sl_str_from_cstr("data.pg"),
                                        .database = sl_str_empty()},
                                       {.token = sl_str_from_cstr("data.sqlsrv"),
                                        .provider = sl_str_from_cstr("sqlserver"),
                                        .capability = sl_str_empty(),
                                        .service = sl_str_from_cstr("data.sqlsrv"),
                                        .database = sl_str_empty()}};
    SlPlanCapability capabilities[2] = {{.token = sl_str_from_cstr("data.pg"),
                                         .kind = sl_str_from_cstr("database"),
                                         .access = sl_str_from_cstr("readwrite"),
                                         .provider = sl_str_from_cstr("data.pg")},
                                        {.token = sl_str_from_cstr("data.sqlsrv"),
                                         .kind = sl_str_from_cstr("database"),
                                         .access = sl_str_from_cstr("readwrite"),
                                         .provider = sl_str_from_cstr("data.sqlsrv")}};
    SlPlan plan = {.data_providers = providers,
                   .data_provider_count = 2U,
                   .capabilities = capabilities,
                   .capability_count = 2U};
    SlCapabilityRegistry registry = {0};
    SlRuntimeFeatureSet features = {0};
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 226;
    }

    if (expect_status(sl_capability_registry_init_from_plan(&plan, &registry), SL_STATUS_OK) != 0) {
        return 227;
    }
    if (attach_runtime_features(&options, &plan, &engine_arena, &features) != 0) {
        return 227;
    }
    options.plan = &plan;
    options.capabilities = &registry;

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 228;
    }

    if (expect_status(
            sl_engine_eval_source(
                engine, sl_str_from_cstr("provider-bridge-invalid-shapes.js"),
                sl_str_from_cstr(
                    "globalThis.providerBridgeInvalidShapes = function () {"
                    "  const checks = [];"
                    "  function expectThrow(fn, text) {"
                    "    try { fn(); } catch (error) { "
                    "checks.push(text === undefined || String(error.message).includes(text)); "
                    "return; }"
                    "    checks.push(false);"
                    "  }"
                    "  function expectOpenClose(open, close) {"
                    "    try { close(open()); checks.push(true); }"
                    "    catch (error) { checks.push(false); }"
                    "  }"
                    "  expectThrow(() => __sloppy.data.postgres.open(), 'open requires open "
                    "options');"
                    "  expectThrow(() => __sloppy.data.postgres.query({ slot: 0, generation: 1 }, "
                    "'select 1', []), 'opaque Sloppy handle');"
                    "  expectThrow(() => __sloppy.data.postgres.query(null, 'select 1', []), "
                    "undefined);"
                    "  expectThrow(() => __sloppy.data.postgres.open(Object.create({ "
                    "provider: 'data.pg' })), 'open requires open options');"
                    "  expectThrow(() => __sloppy.data.postgres.open(Object.create({ "
                    "connectionString: 'postgres://localhost/sloppy' })), 'open requires open "
                    "options');"
                    "  const pgInheritedMax = { connectionString: 'postgres://localhost/sloppy', "
                    "capability: 'data.pg' };"
                    "  Object.setPrototypeOf(pgInheritedMax, { maxConnections: 'bad' });"
                    "  expectOpenClose(() => __sloppy.data.postgres.open(pgInheritedMax), "
                    "__sloppy.data.postgres.close);"
                    "  expectThrow(() => __sloppy.data.postgres.open({ "
                    "connectionString: 'postgres://localhost/sloppy', capability: 'data.pg', "
                    "get maxConnections() { throw new Error('max'); } }), 'open requires open "
                    "options');"
                    "  expectThrow(() => __sloppy.data.sqlserver.open(), 'open requires open "
                    "options');"
                    "  expectThrow(() => __sloppy.data.sqlserver.query({ slot: 0, generation: 1 }, "
                    "'select 1', []), 'opaque Sloppy handle');"
                    "  expectThrow(() => __sloppy.data.sqlserver.query(null, 'select 1', []), "
                    "undefined);"
                    "  expectThrow(() => __sloppy.data.sqlserver.open(Object.create({ "
                    "provider: 'data.sqlsrv' })), 'open requires open options');"
                    "  expectThrow(() => __sloppy.data.sqlserver.open(Object.create({ "
                    "connectionString: 'Driver={ODBC Driver 18 for SQL Server};Server=localhost;' "
                    "})), 'open requires open options');"
                    "  const sqlSrvInheritedMax = { connectionString: 'Driver={ODBC Driver 18 for "
                    "SQL Server};Server=localhost;', capability: 'data.sqlsrv' };"
                    "  Object.setPrototypeOf(sqlSrvInheritedMax, { maxConnections: 'bad' });"
                    "  expectOpenClose(() => __sloppy.data.sqlserver.open(sqlSrvInheritedMax), "
                    "__sloppy.data.sqlserver.close);"
                    "  expectThrow(() => __sloppy.data.sqlserver.open({ "
                    "connectionString: 'Driver={ODBC Driver 18 for SQL Server};Server=localhost;', "
                    "capability: 'data.sqlsrv', get maxConnections() { throw new Error('max'); } "
                    "}), 'open requires open options');"
                    "  return { __sloppyResult: true, kind: 'json', status: 200,"
                    "    contentType: 'application/json; charset=utf-8', body: { checks } };"
                    "};"),
                &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 229;
    }

    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("providerBridgeInvalidShapes"),
                                               &result, &diag),
                      SL_STATUS_OK) != 0 ||
        result.kind != SL_ENGINE_RESULT_JSON ||
        expect_bytes_equal(result.response.body, "{\"checks\":[true,true,true,true,true,true,true,"
                                                 "true,true,true,true,true,true,true]}") != 0)
    {
        sl_engine_destroy(engine);
        return 230;
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

int main(int argc, char** argv)
{
    int result = 0;

    if (argc >= 2 && strcmp(argv[1], "--sloppy-os-child") == 0) {
        return os_child_main(argc, argv);
    }

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

    result = test_source_map_bounds_malformed_cases_report_generated_location();
    if (result != 0) {
        return result;
    }

    result = test_registered_handler_throw_remaps_source_map_location();
    if (result != 0) {
        return result;
    }

    result = test_plain_object_result_returns_json_response();
    if (result != 0) {
        return result;
    }

    result = test_primitive_results_follow_http_contract();
    if (result != 0) {
        return result;
    }

    result = test_result_descriptor_negative_shapes_fail_safely();
    if (result != 0) {
        return result;
    }

    result = test_promise_result_settles_text();
    if (result != 0) {
        return result;
    }

    result = test_profile_counts_top_level_return_type_once();
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

    result = test_net_local_endpoint_intrinsic_rejects_invalid_runtime_path();
    if (result != 0) {
        return result;
    }

    result = test_http_client_feature_activates_net_intrinsic_namespace();
    if (result != 0) {
        return result;
    }

    result = test_net_intrinsic_inactive_feature_is_not_registered();
    if (result != 0) {
        return result;
    }

    result = test_workers_intrinsic_namespace_registered_when_active();
    if (result != 0) {
        return result;
    }

    result = test_workers_intrinsic_pool_runs_off_owner_thread();
    if (result != 0) {
        return result;
    }

    result = test_workers_intrinsic_js_worker_start_invoke_stop();
    if (result != 0) {
        return result;
    }

    result = test_workers_intrinsic_inactive_feature_is_not_registered();
    if (result != 0) {
        return result;
    }

    result = test_os_intrinsic_system_and_environment();
    if (result != 0) {
        return result;
    }

    result = test_os_intrinsic_process_info_respects_policy();
    if (result != 0) {
        return result;
    }

    result = test_os_intrinsic_inactive_feature_is_not_registered();
    if (result != 0) {
        return result;
    }

    result = test_os_intrinsic_process_run_start_and_signals(argv[0]);
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

    result = test_startup_snapshot_rebuilds_runtime_handler_map();
    if (result != 0) {
        return result;
    }

    result = test_startup_snapshot_supports_native_intrinsics();
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

    result = test_request_context_log_writes_native_event();
    if (result != 0) {
        return result;
    }

    result = test_request_context_exposes_headers_and_body();
    if (result != 0) {
        return result;
    }

    result = test_request_context_preserves_empty_header_lookup();
    if (result != 0) {
        return result;
    }

    result = test_request_context_header_facade_is_opt_in();
    if (result != 0) {
        return result;
    }

    result = test_request_context_metadata_objects_are_opt_in();
    if (result != 0) {
        return result;
    }

    result = test_request_context_profile_counts_only_requested_facets();
    if (result != 0) {
        return result;
    }

    result = test_request_context_preserves_binary_body_bytes();
    if (result != 0) {
        return result;
    }

    result = test_request_context_exposes_https_scheme_without_native_handles();
    if (result != 0) {
        return result;
    }

    result = test_request_context_body_object_is_one_shot();
    if (result != 0) {
        return result;
    }

    result = test_native_validated_json_body_is_materialized_once();
    if (result != 0) {
        return result;
    }

    result = test_native_schema_json_response_serializes_supported_shape();
    if (result != 0) {
        return result;
    }

    result = test_native_schema_json_response_capacity_exceeded();
    if (result != 0) {
        return result;
    }

    result = test_unsupported_native_schema_json_response_falls_back_to_generic();
    if (result != 0) {
        return result;
    }

    result = test_request_context_helper_functions_are_frozen();
    if (result != 0) {
        return result;
    }

    result = test_request_context_private_state_ignores_public_mutation();
    if (result != 0) {
        return result;
    }

    result = test_result_descriptor_copies_headers_and_location();
    if (result != 0) {
        return result;
    }

    result = test_result_descriptor_fast_paths_cover_common_results();
    if (result != 0) {
        return result;
    }

    result = test_result_descriptor_fast_paths_fall_back_for_mutated_shapes();
    if (result != 0) {
        return result;
    }

    result = test_result_descriptor_inherited_fields_do_not_poison_headers();
    if (result != 0) {
        return result;
    }

    result = test_result_descriptor_inherited_core_fields_do_not_admit_descriptor();
    if (result != 0) {
        return result;
    }

    result = test_result_descriptor_inherited_fast_markers_do_not_spoof_body();
    if (result != 0) {
        return result;
    }

    result = test_result_descriptor_preserves_binary_body();
    if (result != 0) {
        return result;
    }

    result = test_invalid_result_headers_fail_safely();
    if (result != 0) {
        return result;
    }

    result = test_result_descriptor_throwing_headers_accessor_fails_safely();
    if (result != 0) {
        return result;
    }

    result = test_result_descriptor_throwing_core_accessors_fail_safely();
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

    result = test_registered_websocket_handler_echoes_native_message();
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

    result = test_sqlite_intrinsic_query_max_rows_option();
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

    result = test_sqlite_intrinsic_close_pending_then_stale_handle();
    if (result != 0) {
        return result;
    }

    result = test_sqlite_intrinsic_transaction_misuse_and_recovery();
    if (result != 0) {
        return result;
    }

    result = test_provider_bridge_non_live_invalid_shapes();
    if (result != 0) {
        return result;
    }

    result = test_sqlite_intrinsic_provider_kind_mismatch_fails_before_open();
    if (result != 0) {
        return result;
    }

    return test_filesystem_intrinsic_promise_roundtrip();
}

#include "sloppy/engine.h"

#include <stdbool.h>
#include <stddef.h>

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static SlEngineOptions noop_options(void)
{
    SlEngineOptions options = {0};

    options.kind = SL_ENGINE_KIND_NONE;
    options.runtime_name = sl_str_from_cstr("sloppy-test");
    options.runtime_version = sl_str_from_cstr("0.2.0-test");
    options.target_platform = sl_str_from_cstr(SL_PLAN_TARGET_PLATFORM_WINDOWS_X64);
    options.target_engine = sl_str_from_cstr("none");
    return options;
}

static int init_arena(SlArena* arena, unsigned char* storage, size_t storage_size)
{
    return expect_status(sl_arena_init(arena, storage, storage_size), SL_STATUS_OK);
}

static int test_noop_create_info_destroy(void)
{
    unsigned char storage[512];
    SlArena arena = {0};
    SlEngineOptions options = noop_options();
    SlEngine* engine = NULL;
    SlEngineInfo info = {0};

    if (init_arena(&arena, storage, sizeof(storage)) != 0) {
        return 1;
    }

    if (expect_status(sl_engine_create(&options, &arena, &engine), SL_STATUS_OK) != 0 ||
        engine == NULL)
    {
        return 2;
    }

    if (expect_status(sl_engine_info(engine, &info), SL_STATUS_OK) != 0) {
        return 3;
    }

    if (info.kind != SL_ENGINE_KIND_NONE || !sl_str_equal(info.name, sl_str_from_cstr("noop")) ||
        !sl_str_equal(info.version, sl_str_from_cstr("0")))
    {
        return 4;
    }

    sl_engine_destroy(engine);
    if (expect_status(sl_engine_info(engine, &info), SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 5;
    }

    return 0;
}

static int test_destroy_null_allowed(void)
{
    sl_engine_destroy(NULL);
    return 0;
}

static int test_create_invalid_options(void)
{
    unsigned char storage[128];
    SlArena arena = {0};
    SlEngineOptions options = noop_options();
    SlEngine* engine = (SlEngine*)1;

    if (init_arena(&arena, storage, sizeof(storage)) != 0) {
        return 10;
    }

    if (expect_status(sl_engine_create(NULL, &arena, &engine), SL_STATUS_INVALID_ARGUMENT) != 0 ||
        engine != NULL)
    {
        return 11;
    }

    if (expect_status(sl_engine_create(&options, NULL, &engine), SL_STATUS_INVALID_ARGUMENT) != 0 ||
        engine != NULL)
    {
        return 12;
    }

    if (expect_status(sl_engine_create(&options, &arena, NULL), SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 13;
    }

    options = noop_options();
    options.runtime_name = sl_str_from_parts(NULL, 1U);
    if (expect_status(sl_engine_create(&options, &arena, &engine), SL_STATUS_INVALID_ARGUMENT) !=
            0 ||
        engine != NULL)
    {
        return 14;
    }

    return 0;
}

static int test_v8_create_is_unsupported_without_bridge(void)
{
    unsigned char storage[128];
    SlArena arena = {0};
    SlEngineOptions options = noop_options();
    SlEngine* engine = (SlEngine*)1;

    if (init_arena(&arena, storage, sizeof(storage)) != 0) {
        return 20;
    }

    options.kind = SL_ENGINE_KIND_V8;
    options.target_engine = sl_str_from_cstr(SL_PLAN_TARGET_ENGINE_V8);

    if (expect_status(sl_engine_create(&options, &arena, &engine), SL_STATUS_UNSUPPORTED) != 0 ||
        engine != NULL)
    {
        return 21;
    }

    return 0;
}

static int test_noop_call_handler_is_unsupported(void)
{
    unsigned char storage[1024];
    SlArena arena = {0};
    SlEngineOptions options = noop_options();
    SlEngine* engine = NULL;
    SlEngineHandlerCall call = {1U};
    SlEngineResult result = {SL_ENGINE_RESULT_TEXT, sl_str_from_cstr("stale")};
    SlDiag diag = {0};

    if (init_arena(&arena, storage, sizeof(storage)) != 0) {
        return 30;
    }

    if (expect_status(sl_engine_create(&options, &arena, &engine), SL_STATUS_OK) != 0) {
        return 31;
    }

    if (expect_status(sl_engine_call_handler(engine, &call, &result, &diag),
                      SL_STATUS_UNSUPPORTED) != 0)
    {
        return 32;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE || result.text.ptr != NULL || result.text.length != 0U)
    {
        return 33;
    }

    if (diag.code != SL_DIAG_UNSUPPORTED_ENGINE ||
        !sl_str_equal(diag.message,
                      sl_str_from_cstr("engine handler execution is not implemented")))
    {
        return 34;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_call_handler_invalid_arguments(void)
{
    unsigned char storage[512];
    SlArena arena = {0};
    SlEngineOptions options = noop_options();
    SlEngine* engine = NULL;
    SlEngineHandlerCall call = {1U};
    SlEngineResult result = {0};

    if (init_arena(&arena, storage, sizeof(storage)) != 0) {
        return 40;
    }

    if (expect_status(sl_engine_create(&options, &arena, &engine), SL_STATUS_OK) != 0) {
        return 41;
    }

    if (expect_status(sl_engine_call_handler(NULL, &call, &result, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 42;
    }

    if (expect_status(sl_engine_call_handler(engine, NULL, &result, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 43;
    }

    if (expect_status(sl_engine_call_handler(engine, &call, NULL, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 44;
    }

    call.handler_id = SL_HANDLER_ID_INVALID;
    if (expect_status(sl_engine_call_handler(engine, &call, &result, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 45;
    }

    sl_engine_destroy(engine);
    return 0;
}

int main(void)
{
    int result = 0;

    result = test_noop_create_info_destroy();
    if (result != 0) {
        return result;
    }

    result = test_destroy_null_allowed();
    if (result != 0) {
        return result;
    }

    result = test_create_invalid_options();
    if (result != 0) {
        return result;
    }

    result = test_v8_create_is_unsupported_without_bridge();
    if (result != 0) {
        return result;
    }

    result = test_noop_call_handler_is_unsupported();
    if (result != 0) {
        return result;
    }

    return test_call_handler_invalid_arguments();
}

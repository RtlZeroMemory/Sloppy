#include "sloppy/engine.h"

#include <cstddef>
#include <thread>

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
    if (needle.length == 0U) {
        return 0;
    }

    if (haystack.length < needle.length || haystack.ptr == nullptr || needle.ptr == nullptr) {
        return 1;
    }

    for (size_t index = 0U; index <= haystack.length - needle.length; index += 1U) {
        size_t inner = 0U;
        for (; inner < needle.length; inner += 1U) {
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

static SlEngineOptions v8_options(void)
{
    SlEngineOptions options = {};

    options.kind = SL_ENGINE_KIND_V8;
    options.runtime_name = sl_str_from_cstr("sloppy-v8-owner-thread-test");
    options.runtime_version = sl_str_from_cstr("0.8.0-test");
    options.target_platform = sl_str_from_cstr(SL_PLAN_TARGET_PLATFORM_WINDOWS_X64);
    options.target_engine = sl_str_from_cstr(SL_PLAN_TARGET_ENGINE_V8);
    return options;
}

static int test_wrong_thread_eval_fails_before_entering_v8(void)
{
    unsigned char engine_storage[8192];
    SlArena engine_arena = {};
    SlEngineOptions options = v8_options();
    SlEngine* engine = nullptr;
    SlDiag diag = {};
    SlStatus worker_status = sl_status_ok();

    if (expect_status(sl_arena_init(&engine_arena, engine_storage, sizeof(engine_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0 ||
        engine == nullptr)
    {
        return 1;
    }

    std::thread worker([&]() {
        worker_status =
            sl_engine_eval_source(engine, sl_str_from_cstr("wrong-thread.js"),
                                  sl_str_from_cstr("globalThis.should_not_run = true;"), &diag);
    });
    worker.join();

    if (expect_status(worker_status, SL_STATUS_INVALID_STATE) != 0 ||
        diag.code != SL_DIAG_ENGINE_CALL_ERROR ||
        expect_str_contains(diag.message, sl_str_from_cstr("non-owner thread")) != 0)
    {
        sl_engine_destroy(engine);
        return 2;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_wrong_thread_destroy_invalidates_handle_without_entering_v8(void)
{
    unsigned char engine_storage[8192];
    SlArena engine_arena = {};
    SlEngineOptions options = v8_options();
    SlEngine* engine = nullptr;
    SlEngineInfo info = {};

    if (expect_status(sl_arena_init(&engine_arena, engine_storage, sizeof(engine_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0 ||
        engine == nullptr)
    {
        return 1;
    }

    std::thread worker([&]() { sl_engine_destroy(engine); });
    worker.join();

    if (expect_status(sl_engine_info(engine, &info), SL_STATUS_INVALID_STATE) != 0) {
        sl_engine_destroy(engine);
        return 2;
    }

    sl_engine_destroy(engine);
    return 0;
}

int main(void)
{
    int result = test_wrong_thread_eval_fails_before_entering_v8();

    if (result != 0) {
        return result;
    }

    result = test_wrong_thread_destroy_invalidates_handle_without_entering_v8();
    if (result != 0) {
        return 10 + result;
    }

    return 0;
}

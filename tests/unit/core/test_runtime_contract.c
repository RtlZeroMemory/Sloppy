#include "sloppy/runtime_contract.h"

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

static int init_arena(SlArena* arena, unsigned char* storage, size_t storage_size)
{
    return expect_status(sl_arena_init(arena, storage, storage_size), SL_STATUS_OK);
}

static SlEngineOptions noop_options(void)
{
    SlEngineOptions options = {0};

    options.kind = SL_ENGINE_KIND_NONE;
    options.runtime_name = sl_str_from_cstr("runtime-contract-test");
    options.runtime_version = sl_str_from_cstr("0.2.0-test");
    options.target_platform = sl_str_from_cstr(SL_PLAN_TARGET_PLATFORM_WINDOWS_X64);
    options.target_engine = sl_str_from_cstr("none");
    return options;
}

static SlPlan one_handler_plan(SlPlanHandler* handler)
{
    SlPlan plan = {0};

    handler->id = 1U;
    handler->export_name = sl_str_from_cstr("__sloppy_handler_1");
    handler->display_name = sl_str_from_cstr("Smoke.Hello");
    plan.version = SL_PLAN_CURRENT_VERSION;
    plan.handlers = handler;
    plan.handler_count = 1U;
    return plan;
}

static int test_missing_handler_id_returns_diagnostic_without_v8(void)
{
    unsigned char engine_storage[1024];
    unsigned char diag_storage[1024];
    SlArena engine_arena = {0};
    SlArena diag_arena = {0};
    SlEngineOptions options = noop_options();
    SlEngine* engine = NULL;
    SlPlanHandler handler = {0};
    SlPlan plan = one_handler_plan(&handler);
    SlEngineResult result = {.kind = SL_ENGINE_RESULT_TEXT, .text = sl_str_from_cstr("stale")};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&diag_arena, diag_storage, sizeof(diag_storage)) != 0)
    {
        return 1;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 2;
    }

    if (expect_status(
            sl_runtime_contract_call_handler(engine, &diag_arena, &plan, 2U, &result, &diag),
            SL_STATUS_OUT_OF_RANGE) != 0)
    {
        return 3;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_ENGINE_CALL_ERROR ||
        !sl_str_equal(diag.message, sl_str_from_cstr("app plan handler ID was not found")))
    {
        return 4;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_duplicate_handler_ids_return_diagnostic_without_v8(void)
{
    unsigned char engine_storage[1024];
    unsigned char diag_storage[1024];
    SlArena engine_arena = {0};
    SlArena diag_arena = {0};
    SlEngineOptions options = noop_options();
    SlEngine* engine = NULL;
    SlPlanHandler handlers[2] = {0};
    SlPlan plan = {0};
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&diag_arena, diag_storage, sizeof(diag_storage)) != 0)
    {
        return 10;
    }

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 11;
    }

    handlers[0].id = 1U;
    handlers[0].export_name = sl_str_from_cstr("__sloppy_handler_1");
    handlers[0].display_name = sl_str_from_cstr("Smoke.One");
    handlers[1].id = 1U;
    handlers[1].export_name = sl_str_from_cstr("__sloppy_handler_1b");
    handlers[1].display_name = sl_str_from_cstr("Smoke.Two");
    plan.version = SL_PLAN_CURRENT_VERSION;
    plan.handlers = handlers;
    plan.handler_count = 2U;

    if (expect_status(
            sl_runtime_contract_call_handler(engine, &diag_arena, &plan, 1U, &result, &diag),
            SL_STATUS_INVALID_STATE) != 0)
    {
        return 12;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_DUPLICATE_HANDLER_ID) {
        return 13;
    }

    sl_engine_destroy(engine);
    return 0;
}

int main(void)
{
    if (test_missing_handler_id_returns_diagnostic_without_v8() != 0) {
        return 1;
    }

    if (test_duplicate_handler_ids_return_diagnostic_without_v8() != 0) {
        return 2;
    }

    return 0;
}

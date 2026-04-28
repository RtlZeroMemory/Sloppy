/*
 * src/engine/engine.c
 *
 * Implements the engine-neutral C ABI stub used before real V8 bridge execution exists.
 * The module provides a deterministic noop engine and an unsupported V8 path so core C
 * code can target an opaque SlEngine without including V8 or C++ concepts.
 *
 * Safety invariants:
 * - SlEngine is opaque to callers and stores no JS/native raw pointer visible to JS;
 * - the noop engine is arena-backed and owns no independently closable resources;
 * - no platform APIs, V8 headers, C++ types, JavaScript loading, or handler execution
 *   appear in this file;
 * - the ABI is not thread-safe yet; future V8 owner-thread checks belong to the bridge.
 *
 * Tests: tests/unit/core/test_engine.c.
 */
#include "sloppy/engine.h"

#include <stddef.h>

struct SlEngine
{
    SlEngineKind kind;
    SlArena* arena;
    bool active;
};

static SlStr sl_engine_literal(const char* ptr, size_t length)
{
    return sl_str_from_parts(ptr, length);
}

static bool sl_engine_str_valid(SlStr str)
{
    return str.length == 0U || str.ptr != NULL;
}

static bool sl_engine_options_valid(const SlEngineOptions* options)
{
    return options != NULL && sl_engine_str_valid(options->runtime_name) &&
           sl_engine_str_valid(options->runtime_version) &&
           sl_engine_str_valid(options->target_platform) &&
           sl_engine_str_valid(options->target_engine);
}

static SlStatus sl_engine_write_unsupported_diag(SlArena* arena, SlDiag* out_diag, SlStr message,
                                                 SlStr hint)
{
    SlDiagBuilder builder;
    SlStatus status;

    if (out_diag == NULL) {
        return sl_status_ok();
    }

    if (arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_diag_builder_init(&builder, arena, SL_DIAG_SEVERITY_ERROR,
                                  SL_DIAG_UNSUPPORTED_ENGINE, message);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_diag_builder_add_hint(&builder, hint);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_diag_builder_finish(&builder, out_diag);
}

static SlStatus sl_engine_create_noop(SlArena* arena, SlEngine** out_engine)
{
    void* memory = NULL;
    SlEngine* engine = NULL;
    SlStatus status = sl_arena_alloc(arena, sizeof(SlEngine), _Alignof(SlEngine), &memory);

    if (!sl_status_is_ok(status)) {
        return status;
    }

    engine = (SlEngine*)memory;
    engine->kind = SL_ENGINE_KIND_NONE;
    engine->arena = arena;
    engine->active = true;
    *out_engine = engine;
    return sl_status_ok();
}

SlStatus sl_engine_create(const SlEngineOptions* options, SlArena* arena, SlEngine** out_engine)
{
    if (out_engine == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_engine = NULL;

    if (!sl_engine_options_valid(options) || arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    switch (options->kind) {
    case SL_ENGINE_KIND_NONE:
        return sl_engine_create_noop(arena, out_engine);
    case SL_ENGINE_KIND_V8:
        return sl_status_from_code(SL_STATUS_UNSUPPORTED);
    default:
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
}

void sl_engine_destroy(SlEngine* engine)
{
    if (engine == NULL) {
        return;
    }

    engine->active = false;
}

SlStatus sl_engine_info(const SlEngine* engine, SlEngineInfo* out_info)
{
    if (out_info == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_info = (SlEngineInfo){0};

    if (engine == NULL || !engine->active) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    switch (engine->kind) {
    case SL_ENGINE_KIND_NONE:
        out_info->kind = SL_ENGINE_KIND_NONE;
        out_info->name = sl_engine_literal("noop", sizeof("noop") - 1U);
        out_info->version = sl_engine_literal("0", sizeof("0") - 1U);
        return sl_status_ok();
    case SL_ENGINE_KIND_V8:
        out_info->kind = SL_ENGINE_KIND_V8;
        out_info->name = sl_engine_literal("v8", sizeof("v8") - 1U);
        out_info->version = sl_engine_literal("unsupported", sizeof("unsupported") - 1U);
        return sl_status_ok();
    default:
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
}

SlStatus sl_engine_call_handler(SlEngine* engine, const SlEngineHandlerCall* call,
                                SlEngineResult* out_result, SlDiag* out_diag)
{
    if (out_result == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_result = (SlEngineResult){0};

    if (engine == NULL || !engine->active || call == NULL || !sl_handler_id_valid(call->handler_id))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    SlStatus diag_status = sl_engine_write_unsupported_diag(
        engine->arena, out_diag,
        sl_engine_literal("engine handler execution is not implemented",
                          sizeof("engine handler execution is not implemented") - 1U),
        sl_engine_literal(
            "TASK 07.C will add V8-backed loading and exported handler calls.",
            sizeof("TASK 07.C will add V8-backed loading and exported handler calls.") - 1U));
    if (!sl_status_is_ok(diag_status)) {
        return diag_status;
    }

    return sl_status_from_code(SL_STATUS_UNSUPPORTED);
}

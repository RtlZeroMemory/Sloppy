/*
 * src/engine/engine.c
 *
 * Implements the engine-neutral C ABI wrapper. The module provides a deterministic noop
 * engine and dispatches to the optional V8 bridge when the build explicitly enables it, so
 * core C code can target an opaque SlEngine without including V8 or C++ concepts.
 *
 * Safety invariants:
 * - SlEngine is opaque to callers and stores no JS/native raw pointer visible to JS;
 * - the noop engine is arena-backed and owns no independently closable resources;
 * - no platform APIs, V8 headers, or C++ types appear in this file;
 * - JavaScript loading and global function calls are dispatched only through the internal
 *   backend boundary;
 * - lifecycle misuse returns deterministic status/diagnostics before backend dispatch;
 * - optional V8 owner-thread checks belong to the bridge.
 *
 * Tests: tests/unit/core/test_engine.c.
 */
#include "engine_internal.h"

#include <stddef.h>

static SlStr sl_engine_literal(const char* ptr, size_t length)
{
    return sl_str_from_parts(ptr, length);
}

static bool sl_engine_str_valid(SlStr str)
{
    return str.length == 0U || str.ptr != NULL;
}

static bool sl_engine_bytes_valid(SlBytes bytes)
{
    return bytes.length == 0U || bytes.ptr != NULL;
}

static bool sl_engine_options_valid(const SlEngineOptions* options)
{
    bool has_source_map = options != NULL && options->source_map.length != 0U;
    bool has_source_map_source_name =
        options != NULL && options->source_map_source_name.length != 0U;

    return options != NULL && sl_engine_str_valid(options->runtime_name) &&
           sl_engine_str_valid(options->runtime_version) &&
           sl_engine_str_valid(options->target_platform) &&
           sl_engine_str_valid(options->target_engine) &&
           (options->ffi_library_override_count == 0U || options->ffi_library_overrides != NULL) &&
           sl_engine_bytes_valid(options->source_map) &&
           sl_engine_str_valid(options->source_map_source_name) &&
           has_source_map == has_source_map_source_name;
}

static SlStatus sl_engine_write_unsupported_diag(SlArena* arena, SlDiag* out_diag, SlStr message,
                                                 SlStr hint)
{
    SlDiagBuilder builder;
    SlStatus status;

    if (out_diag == NULL) {
        return sl_status_from_code(SL_STATUS_UNSUPPORTED);
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

    status = sl_diag_builder_finish(&builder, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_status_from_code(SL_STATUS_UNSUPPORTED);
}

static SlStatus sl_engine_write_invalid_lifecycle_diag(SlArena* arena, SlDiag* out_diag,
                                                       SlStr operation)
{
    SlDiagBuilder builder;
    SlStatus status;

    if (out_diag == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    if (arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_diag_builder_init(
        &builder, arena, SL_DIAG_SEVERITY_ERROR, SL_DIAG_ENGINE_CALL_ERROR,
        sl_engine_literal("engine operation attempted after dispose",
                          sizeof("engine operation attempted after dispose") - 1U));
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_diag_builder_add_related(&builder, sl_source_span_unknown(), operation);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_diag_builder_add_hint(
        &builder,
        sl_engine_literal("Create a new engine before evaluating source or calling handlers.",
                          sizeof("Create a new engine before evaluating source or calling "
                                 "handlers.") -
                              1U));
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_diag_builder_finish(&builder, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_status_from_code(SL_STATUS_INVALID_STATE);
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
    engine->backend = NULL;
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
#if defined(SLOPPY_ENABLE_V8_BRIDGE)
        return sl_engine_v8_create(options, arena, out_engine);
#else
        return sl_status_from_code(SL_STATUS_UNSUPPORTED);
#endif
    default:
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
}

void sl_engine_destroy(SlEngine* engine)
{
    if (engine == NULL) {
        return;
    }

#if defined(SLOPPY_ENABLE_V8_BRIDGE)
    if (engine->kind == SL_ENGINE_KIND_V8 && engine->backend != NULL) {
        sl_engine_v8_destroy(engine);
        return;
    }
#endif

    engine->active = false;
}

SlStatus sl_engine_info(const SlEngine* engine, SlEngineInfo* out_info)
{
    if (out_info == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_info = (SlEngineInfo){0};

    if (engine == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (!engine->active) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    switch (engine->kind) {
    case SL_ENGINE_KIND_NONE:
        out_info->kind = SL_ENGINE_KIND_NONE;
        out_info->name = sl_engine_literal("noop", sizeof("noop") - 1U);
        out_info->version = sl_engine_literal("0", sizeof("0") - 1U);
        return sl_status_ok();
    case SL_ENGINE_KIND_V8:
#if defined(SLOPPY_ENABLE_V8_BRIDGE)
        return sl_engine_v8_info(engine, out_info);
#else
        return sl_status_from_code(SL_STATUS_UNSUPPORTED);
#endif
    default:
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
}

SlStatus sl_engine_eval_source(SlEngine* engine, SlStr source_name, SlStr source, SlDiag* out_diag)
{
    if (engine == NULL || !sl_engine_str_valid(source_name) || !sl_engine_str_valid(source) ||
        source.length == 0U)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (!engine->active) {
        return sl_engine_write_invalid_lifecycle_diag(
            engine->arena, out_diag,
            sl_engine_literal("sl_engine_eval_source", sizeof("sl_engine_eval_source") - 1U));
    }

    if (engine->kind != SL_ENGINE_KIND_V8) {
        return sl_engine_write_unsupported_diag(
            engine->arena, out_diag,
            sl_engine_literal("engine source evaluation requires V8",
                              sizeof("engine source evaluation requires V8") - 1U),
            sl_engine_literal("Configure with SLOPPY_ENABLE_V8=ON and a valid V8 SDK.",
                              sizeof("Configure with SLOPPY_ENABLE_V8=ON and a valid V8 SDK.") -
                                  1U));
    }

#if defined(SLOPPY_ENABLE_V8_BRIDGE)
    return sl_engine_v8_eval_source(engine, source_name, source, out_diag);
#else
    (void)out_diag;
    return sl_status_from_code(SL_STATUS_UNSUPPORTED);
#endif
}

SlStatus sl_engine_call_function0(SlEngine* engine, SlArena* arena, SlStr function_name,
                                  SlEngineResult* out_result, SlDiag* out_diag)
{
    if (out_result == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_result = (SlEngineResult){0};

    if (engine == NULL || arena == NULL || !sl_engine_str_valid(function_name) ||
        function_name.length == 0U)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (!engine->active) {
        return sl_engine_write_invalid_lifecycle_diag(
            engine->arena, out_diag,
            sl_engine_literal("sl_engine_call_function0", sizeof("sl_engine_call_function0") - 1U));
    }

    if (engine->kind != SL_ENGINE_KIND_V8) {
        return sl_engine_write_unsupported_diag(
            engine->arena, out_diag,
            sl_engine_literal("engine function calls require V8",
                              sizeof("engine function calls require V8") - 1U),
            sl_engine_literal("Configure with SLOPPY_ENABLE_V8=ON and a valid V8 SDK.",
                              sizeof("Configure with SLOPPY_ENABLE_V8=ON and a valid V8 SDK.") -
                                  1U));
    }

#if defined(SLOPPY_ENABLE_V8_BRIDGE)
    return sl_engine_v8_call_function0(engine, arena, function_name, out_result, out_diag);
#else
    (void)out_diag;
    return sl_status_from_code(SL_STATUS_UNSUPPORTED);
#endif
}

SlStatus sl_engine_call_function_with_context(SlEngine* engine, SlArena* arena, SlStr function_name,
                                              const SlHttpRequestContext* request_context,
                                              SlEngineResult* out_result, SlDiag* out_diag)
{
    if (out_result == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_result = (SlEngineResult){0};

    if (engine == NULL || arena == NULL || request_context == NULL ||
        request_context->request == NULL ||
        (request_context->route_param_count != 0U && request_context->route_params == NULL) ||
        (request_context->query_param_count != 0U && request_context->query_params == NULL) ||
        !sl_engine_str_valid(function_name) || function_name.length == 0U)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (!engine->active) {
        return sl_engine_write_invalid_lifecycle_diag(
            engine->arena, out_diag,
            sl_engine_literal("sl_engine_call_function_with_context",
                              sizeof("sl_engine_call_function_with_context") - 1U));
    }

    if (engine->kind != SL_ENGINE_KIND_V8) {
        return sl_engine_write_unsupported_diag(
            engine->arena, out_diag,
            sl_engine_literal("engine request context calls require V8",
                              sizeof("engine request context calls require V8") - 1U),
            sl_engine_literal("Configure with SLOPPY_ENABLE_V8=ON and a valid V8 SDK.",
                              sizeof("Configure with SLOPPY_ENABLE_V8=ON and a valid V8 SDK.") -
                                  1U));
    }

#if defined(SLOPPY_ENABLE_V8_BRIDGE)
    return sl_engine_v8_call_function_with_context(engine, arena, function_name, request_context,
                                                   out_result, out_diag);
#else
    (void)out_diag;
    return sl_status_from_code(SL_STATUS_UNSUPPORTED);
#endif
}

SlStatus sl_engine_validate_registered_handlers(SlEngine* engine, const SlPlan* plan,
                                                SlDiag* out_diag)
{
    if (engine == NULL || plan == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (!engine->active) {
        return sl_engine_write_invalid_lifecycle_diag(
            engine->arena, out_diag,
            sl_engine_literal("sl_engine_validate_registered_handlers",
                              sizeof("sl_engine_validate_registered_handlers") - 1U));
    }

    if (engine->kind != SL_ENGINE_KIND_V8) {
        return sl_engine_write_unsupported_diag(
            engine->arena, out_diag,
            sl_engine_literal("engine registered handler validation requires V8",
                              sizeof("engine registered handler validation requires V8") - 1U),
            sl_engine_literal("Configure with SLOPPY_ENABLE_V8=ON and load the bootstrap runtime.",
                              sizeof("Configure with SLOPPY_ENABLE_V8=ON and load the bootstrap "
                                     "runtime.") -
                                  1U));
    }

#if defined(SLOPPY_ENABLE_V8_BRIDGE)
    return sl_engine_v8_validate_registered_handlers(engine, plan, out_diag);
#else
    (void)out_diag;
    return sl_status_from_code(SL_STATUS_UNSUPPORTED);
#endif
}

SlStatus sl_engine_call_registered_handler_with_context(SlEngine* engine, SlArena* arena,
                                                        SlHandlerId handler_id,
                                                        const SlHttpRequestContext* request_context,
                                                        SlEngineResult* out_result,
                                                        SlDiag* out_diag)
{
    if (out_result == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_result = (SlEngineResult){0};

    if (engine == NULL || arena == NULL || !sl_handler_id_valid(handler_id) ||
        request_context == NULL || request_context->request == NULL ||
        (request_context->route_param_count != 0U && request_context->route_params == NULL) ||
        (request_context->query_param_count != 0U && request_context->query_params == NULL))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (!engine->active) {
        return sl_engine_write_invalid_lifecycle_diag(
            engine->arena, out_diag,
            sl_engine_literal("sl_engine_call_registered_handler_with_context",
                              sizeof("sl_engine_call_registered_handler_with_context") - 1U));
    }

    if (engine->kind != SL_ENGINE_KIND_V8) {
        return sl_engine_write_unsupported_diag(
            engine->arena, out_diag,
            sl_engine_literal("engine registered handler calls require V8",
                              sizeof("engine registered handler calls require V8") - 1U),
            sl_engine_literal("Configure with SLOPPY_ENABLE_V8=ON and a valid V8 SDK.",
                              sizeof("Configure with SLOPPY_ENABLE_V8=ON and a valid V8 SDK.") -
                                  1U));
    }

#if defined(SLOPPY_ENABLE_V8_BRIDGE)
    return sl_engine_v8_call_registered_handler_with_context(engine, arena, handler_id,
                                                             request_context, out_result, out_diag);
#else
    (void)out_diag;
    return sl_status_from_code(SL_STATUS_UNSUPPORTED);
#endif
}

SlStatus sl_engine_call_handler(SlEngine* engine, const SlEngineHandlerCall* call,
                                SlEngineResult* out_result, SlDiag* out_diag)
{
    if (out_result == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_result = (SlEngineResult){0};

    if (engine == NULL || call == NULL || !sl_handler_id_valid(call->handler_id)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (!engine->active) {
        return sl_engine_write_invalid_lifecycle_diag(
            engine->arena, out_diag,
            sl_engine_literal("sl_engine_call_handler", sizeof("sl_engine_call_handler") - 1U));
    }

    return sl_engine_write_unsupported_diag(
        engine->arena, out_diag,
        sl_engine_literal("engine handler execution is not implemented",
                          sizeof("engine handler execution is not implemented") - 1U),
        sl_engine_literal("Use registered handler dispatch for executable V8 handler calls.",
                          sizeof("Use registered handler dispatch for executable V8 handler "
                                 "calls.") -
                              1U));
}

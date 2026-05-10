/*
 * src/core/runtime_contract.c
 *
 * Implements the first compiler-planned/runtime-hosted/engine-executed smoke contract.
 * The helper remains deliberately small: it trusts the Plan parser for schema validation,
 * checks the borrowed handler table invariants that matter at dispatch time, and crosses
 * the engine boundary by calling the plan-provided JavaScript export name.
 */
#include "sloppy/runtime_contract.h"

static SlStr sl_runtime_literal(const char* ptr, size_t length)
{
    return sl_str_from_parts(ptr, length);
}

static SlStatus sl_runtime_write_diag(SlArena* arena, SlDiag* out_diag, SlDiagCode code,
                                      SlStr message, SlStr hint, SlStatusCode status_code)
{
    SlDiagBuilder builder;
    SlStatus status;

    if (out_diag == NULL) {
        return sl_status_from_code(status_code);
    }

    if (arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_diag_builder_init(&builder, arena, SL_DIAG_SEVERITY_ERROR, code, message);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (!sl_str_is_empty(hint)) {
        status = sl_diag_builder_add_hint(&builder, hint);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    status = sl_diag_builder_finish(&builder, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_status_from_code(status_code);
}

static SlStatus sl_runtime_resolve_handler(SlEngine* engine, SlArena* arena, const SlPlan* plan,
                                           SlHandlerId handler_id,
                                           const SlPlanHandler** out_handler, SlDiag* out_diag)
{
    const SlPlanHandler* handler = NULL;
    SlStatus status;

    if (out_handler == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_handler = NULL;

    if (engine == NULL || arena == NULL || plan == NULL || !sl_handler_id_valid(handler_id)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (sl_plan_has_duplicate_handler_ids(plan)) {
        return sl_runtime_write_diag(
            arena, out_diag, SL_DIAG_DUPLICATE_HANDLER_ID,
            sl_runtime_literal("app plan contains duplicate handler IDs",
                               sizeof("app plan contains duplicate handler IDs") - 1U),
            sl_runtime_literal("Reject duplicate handler IDs before runtime dispatch.",
                               sizeof("Reject duplicate handler IDs before runtime dispatch.") -
                                   1U),
            SL_STATUS_INVALID_STATE);
    }

    status = sl_plan_find_handler_by_id(plan, handler_id, &handler);
    if (sl_status_code(status) == SL_STATUS_OUT_OF_RANGE) {
        return sl_runtime_write_diag(
            arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR,
            sl_runtime_literal("app plan handler ID was not found",
                               sizeof("app plan handler ID was not found") - 1U),
            sl_runtime_literal("Ensure app.plan.json contains the requested handler ID.",
                               sizeof("Ensure app.plan.json contains the requested handler ID.") -
                                   1U),
            SL_STATUS_OUT_OF_RANGE);
    }

    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (handler == NULL || sl_str_is_empty(handler->export_name)) {
        return sl_runtime_write_diag(
            arena, out_diag, SL_DIAG_INVALID_PLAN_FIELD,
            sl_runtime_literal("app plan handler export name is missing",
                               sizeof("app plan handler export name is missing") - 1U),
            sl_runtime_literal("Regenerate or reject the app plan before runtime dispatch.",
                               sizeof("Regenerate or reject the app plan before runtime "
                                      "dispatch.") -
                                   1U),
            SL_STATUS_INVALID_STATE);
    }

    *out_handler = handler;
    return sl_status_ok();
}

SlStatus sl_runtime_contract_call_handler(SlEngine* engine, SlArena* arena, const SlPlan* plan,
                                          SlHandlerId handler_id, SlEngineResult* out_result,
                                          SlDiag* out_diag)
{
    const SlPlanHandler* handler = NULL;
    SlStatus status;

    if (out_result == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_result = (SlEngineResult){0};
    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }

    status = sl_runtime_resolve_handler(engine, arena, plan, handler_id, &handler, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (handler == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    return sl_engine_call_function0(engine, arena, handler->export_name, out_result, out_diag);
}

SlStatus sl_runtime_contract_call_handler_with_context(SlEngine* engine, SlArena* arena,
                                                       const SlPlan* plan, SlHandlerId handler_id,
                                                       const SlHttpRequestContext* request_context,
                                                       SlEngineResult* out_result, SlDiag* out_diag)
{
    const SlPlanHandler* handler = NULL;
    SlStatus status;

    if (out_result == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_result = (SlEngineResult){0};
    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }

    if (request_context == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_runtime_resolve_handler(engine, arena, plan, handler_id, &handler, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (handler == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    (void)handler;
    return sl_engine_call_registered_handler_with_context(engine, arena, handler_id,
                                                          request_context, out_result, out_diag);
}

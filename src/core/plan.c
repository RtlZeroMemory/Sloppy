/*
 * src/core/plan.c
 *
 * Implements small helpers for Sloppy Plan v1's borrowed native schema. This module defines
 * shape-level behavior only: supported version checks, handler ID rules, handler lookup,
 * and duplicate handler ID detection.
 *
 * Safety invariants:
 * - no allocation, parser, file I/O, platform API, or engine dependency;
 * - all strings and handler arrays are borrowed from the caller;
 * - handler lookup returns a borrowed pointer into the caller-owned handler table.
 *
 * Tests: tests/unit/core/test_plan.c.
 */
#include "sloppy/plan.h"

bool sl_plan_version_supported(uint32_t version)
{
    return version == SL_PLAN_VERSION_1;
}

bool sl_handler_id_valid(SlHandlerId id)
{
    return id != SL_HANDLER_ID_INVALID;
}

SlStatus sl_plan_find_handler_by_id(const SlPlan* plan, SlHandlerId id, const SlPlanHandler** out)
{
    size_t index = 0U;

    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out = NULL;

    if (plan == NULL || !sl_handler_id_valid(id) ||
        (plan->handler_count > 0U && plan->handlers == NULL))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    for (index = 0U; index < plan->handler_count; index += 1U) {
        if (plan->handlers[index].id == id) {
            *out = &plan->handlers[index];
            return sl_status_ok();
        }
    }

    return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
}

bool sl_plan_has_duplicate_handler_ids(const SlPlan* plan)
{
    size_t outer = 0U;
    size_t inner = 0U;

    if (plan == NULL || plan->handlers == NULL || plan->handler_count < 2U) {
        return false;
    }

    for (outer = 0U; outer < plan->handler_count - 1U; outer += 1U) {
        for (inner = outer + 1U; inner < plan->handler_count; inner += 1U) {
            if (plan->handlers[outer].id == plan->handlers[inner].id) {
                return true;
            }
        }
    }

    return false;
}

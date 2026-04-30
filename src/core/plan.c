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

#include "sloppy/http.h"

static bool sl_plan_token_equal(SlStr left, SlStr right)
{
    return !sl_str_is_empty(left) && sl_str_equal(left, right);
}

bool sl_plan_version_supported(uint32_t version)
{
    return version == SL_PLAN_VERSION_1;
}

bool sl_handler_id_valid(SlHandlerId id)
{
    return id != SL_HANDLER_ID_INVALID;
}

bool sl_plan_route_method_supported(SlStr method)
{
    SlHttpMethod http_method = SL_HTTP_METHOD_UNKNOWN;
    return sl_status_is_ok(sl_http_method_from_str(method, &http_method)) &&
           sl_http_method_supported(http_method);
}

bool sl_plan_route_method_runnable(SlStr method)
{
    return sl_plan_route_method_supported(method);
}

bool sl_plan_provider_supported(SlStr provider)
{
    return sl_str_equal(provider, sl_str_from_cstr("sqlite")) ||
           sl_str_equal(provider, sl_str_from_cstr("postgres")) ||
           sl_str_equal(provider, sl_str_from_cstr("sqlserver"));
}

bool sl_plan_capability_kind_supported(SlStr kind)
{
    return sl_str_equal(kind, sl_str_from_cstr("database")) ||
           sl_str_equal(kind, sl_str_from_cstr("filesystem")) ||
           sl_str_equal(kind, sl_str_from_cstr("network"));
}

bool sl_plan_capability_access_supported(SlStr kind, SlStr access)
{
    if (sl_str_equal(kind, sl_str_from_cstr("database"))) {
        return sl_str_equal(access, sl_str_from_cstr("read")) ||
               sl_str_equal(access, sl_str_from_cstr("write")) ||
               sl_str_equal(access, sl_str_from_cstr("readwrite"));
    }
    if (sl_str_equal(kind, sl_str_from_cstr("filesystem"))) {
        return sl_str_equal(access, sl_str_from_cstr("read")) ||
               sl_str_equal(access, sl_str_from_cstr("write")) ||
               sl_str_equal(access, sl_str_from_cstr("readwrite"));
    }
    if (sl_str_equal(kind, sl_str_from_cstr("network"))) {
        return sl_str_equal(access, sl_str_from_cstr("connect")) ||
               sl_str_equal(access, sl_str_from_cstr("listen")) ||
               sl_str_equal(access, sl_str_from_cstr("connect-listen"));
    }
    return false;
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

bool sl_plan_has_duplicate_routes(const SlPlan* plan)
{
    size_t outer = 0U;
    size_t inner = 0U;

    if (plan == NULL || plan->routes == NULL || plan->route_count < 2U) {
        return false;
    }

    for (outer = 0U; outer < plan->route_count - 1U; outer += 1U) {
        for (inner = outer + 1U; inner < plan->route_count; inner += 1U) {
            if (sl_str_equal(plan->routes[outer].method, plan->routes[inner].method) &&
                sl_str_equal(plan->routes[outer].pattern, plan->routes[inner].pattern))
            {
                return true;
            }
        }
    }

    return false;
}

bool sl_plan_has_duplicate_route_names(const SlPlan* plan)
{
    size_t outer = 0U;
    size_t inner = 0U;

    if (plan == NULL || plan->routes == NULL || plan->route_count < 2U) {
        return false;
    }

    for (outer = 0U; outer < plan->route_count - 1U; outer += 1U) {
        for (inner = outer + 1U; inner < plan->route_count; inner += 1U) {
            if (sl_plan_token_equal(plan->routes[outer].name, plan->routes[inner].name)) {
                return true;
            }
        }
    }

    return false;
}

bool sl_plan_has_duplicate_data_provider_tokens(const SlPlan* plan)
{
    size_t outer = 0U;
    size_t inner = 0U;

    if (plan == NULL || plan->data_providers == NULL || plan->data_provider_count < 2U) {
        return false;
    }

    for (outer = 0U; outer < plan->data_provider_count - 1U; outer += 1U) {
        for (inner = outer + 1U; inner < plan->data_provider_count; inner += 1U) {
            if (sl_str_equal(plan->data_providers[outer].token, plan->data_providers[inner].token))
            {
                return true;
            }
        }
    }

    return false;
}

bool sl_plan_has_duplicate_capability_tokens(const SlPlan* plan)
{
    size_t outer = 0U;
    size_t inner = 0U;

    if (plan == NULL || plan->capabilities == NULL || plan->capability_count < 2U) {
        return false;
    }

    for (outer = 0U; outer < plan->capability_count - 1U; outer += 1U) {
        for (inner = outer + 1U; inner < plan->capability_count; inner += 1U) {
            if (sl_str_equal(plan->capabilities[outer].token, plan->capabilities[inner].token)) {
                return true;
            }
        }
    }

    return false;
}

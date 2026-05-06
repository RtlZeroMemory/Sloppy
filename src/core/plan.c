/*
 * src/core/plan.c
 *
 * Implements helpers for Sloppy Plan v1's borrowed native schema. This module defines
 * shape-level behavior and arena-based metadata interning for stable Plan pointers.
 *
 * Safety invariants:
 * - arena-only allocation; no heap malloc/free, parser, file I/O, platform API, or engine
 *   dependency;
 * - parsed Plan strings and arrays remain caller-owned until sl_plan_intern_metadata copies
 *   selected stable metadata into the caller-provided arena;
 * - handler lookup returns a borrowed pointer into the caller-owned handler table.
 *
 * Tests: tests/unit/core/test_plan.c.
 */
#include "sloppy/plan.h"

#include "sloppy/checked_math.h"
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
           sl_str_equal(kind, sl_str_from_cstr("network")) ||
           sl_str_equal(kind, sl_str_from_cstr("os")) ||
           sl_str_equal(kind, sl_str_from_cstr("env")) ||
           sl_str_equal(kind, sl_str_from_cstr("process")) ||
           sl_str_equal(kind, sl_str_from_cstr("signals"));
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
               sl_str_equal(access, sl_str_from_cstr("readwrite")) ||
               sl_str_equal(access, sl_str_from_cstr("append")) ||
               sl_str_equal(access, sl_str_from_cstr("delete")) ||
               sl_str_equal(access, sl_str_from_cstr("list")) ||
               sl_str_equal(access, sl_str_from_cstr("metadata")) ||
               sl_str_equal(access, sl_str_from_cstr("watch")) ||
               sl_str_equal(access, sl_str_from_cstr("lock"));
    }
    if (sl_str_equal(kind, sl_str_from_cstr("network"))) {
        return sl_str_equal(access, sl_str_from_cstr("connect")) ||
               sl_str_equal(access, sl_str_from_cstr("listen")) ||
               sl_str_equal(access, sl_str_from_cstr("connect-listen"));
    }
    if (sl_str_equal(kind, sl_str_from_cstr("os"))) {
        return sl_str_equal(access, sl_str_from_cstr("info"));
    }
    if (sl_str_equal(kind, sl_str_from_cstr("env"))) {
        return sl_str_equal(access, sl_str_from_cstr("read")) ||
               sl_str_equal(access, sl_str_from_cstr("list"));
    }
    if (sl_str_equal(kind, sl_str_from_cstr("process"))) {
        return sl_str_equal(access, sl_str_from_cstr("run")) ||
               sl_str_equal(access, sl_str_from_cstr("shell")) ||
               sl_str_equal(access, sl_str_from_cstr("signal")) ||
               sl_str_equal(access, sl_str_from_cstr("kill"));
    }
    if (sl_str_equal(kind, sl_str_from_cstr("signals"))) {
        return sl_str_equal(access, sl_str_from_cstr("shutdown"));
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

static SlStatus sl_plan_intern_required(SlInternTable* table, SlStr text, SlStr* out)
{
    SlInternedString interned = {0};
    SlStatus status;

    if (table == NULL || out == NULL || (text.length > 0U && text.ptr == NULL)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (sl_str_is_empty(text)) {
        *out = sl_str_empty();
        return sl_status_ok();
    }

    status = sl_intern_table_intern(table, text, &interned);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    *out = interned.text;
    return sl_status_ok();
}

static SlStatus sl_plan_alloc_copy(SlArena* arena, const void* src, size_t count, size_t item_size,
                                   size_t alignment, void** out)
{
    void* storage = NULL;
    size_t bytes = 0U;
    size_t index = 0U;
    SlStatus status;

    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out = NULL;
    if (count == 0U) {
        return sl_status_ok();
    }
    if (arena == NULL || src == NULL || item_size == 0U || alignment == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_checked_mul_size(count, item_size, &bytes);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(arena, bytes, alignment, &storage);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    for (index = 0U; index < bytes; index += 1U) {
        ((unsigned char*)storage)[index] = ((const unsigned char*)src)[index];
    }
    *out = storage;
    return sl_status_ok();
}

static SlStatus sl_plan_intern_header(SlInternTable* table, SlPlan* staged)
{
    SlStatus status;

    status = sl_plan_intern_required(table, staged->compiler_version, &staged->compiler_version);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status =
        sl_plan_intern_required(table, staged->runtime_min_version, &staged->runtime_min_version);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_plan_intern_required(table, staged->stdlib_version, &staged->stdlib_version);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_plan_intern_required(table, staged->target.platform, &staged->target.platform);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_plan_intern_required(table, staged->target.engine, &staged->target.engine);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_plan_intern_required(table, staged->bundle.id, &staged->bundle.id);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_plan_intern_required(table, staged->source_map.id, &staged->source_map.id);
}

static SlStatus sl_plan_intern_handlers(SlArena* arena, SlInternTable* table, SlPlan* staged)
{
    SlPlanHandler* handlers = NULL;
    size_t index = 0U;
    SlStatus status;

    if (staged->handler_count == 0U) {
        staged->handlers = NULL;
        return sl_status_ok();
    }
    if (staged->handlers == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_plan_alloc_copy(arena, staged->handlers, staged->handler_count,
                                sizeof(SlPlanHandler), _Alignof(SlPlanHandler), (void**)&handlers);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (handlers == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    for (index = 0U; index < staged->handler_count; index += 1U) {
        status = sl_plan_intern_required(table, handlers[index].export_name,
                                         &handlers[index].export_name);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_plan_intern_required(table, handlers[index].display_name,
                                         &handlers[index].display_name);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    staged->handlers = handlers;
    return sl_status_ok();
}

static SlStatus sl_plan_intern_routes(SlArena* arena, SlInternTable* table, SlPlan* staged)
{
    SlPlanRoute* routes = NULL;
    size_t index = 0U;
    SlStatus status;

    if (staged->route_count == 0U) {
        staged->routes = NULL;
        return sl_status_ok();
    }
    if (staged->routes == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_plan_alloc_copy(arena, staged->routes, staged->route_count, sizeof(SlPlanRoute),
                                _Alignof(SlPlanRoute), (void**)&routes);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (routes == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    for (index = 0U; index < staged->route_count; index += 1U) {
        status = sl_plan_intern_required(table, routes[index].method, &routes[index].method);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_plan_intern_required(table, routes[index].pattern, &routes[index].pattern);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_plan_intern_required(table, routes[index].name, &routes[index].name);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    staged->routes = routes;
    return sl_status_ok();
}

static SlStatus sl_plan_intern_providers(SlArena* arena, SlInternTable* table, SlPlan* staged)
{
    SlPlanDataProvider* providers = NULL;
    size_t index = 0U;
    SlStatus status;

    if (staged->data_provider_count == 0U) {
        staged->data_providers = NULL;
        return sl_status_ok();
    }
    if (staged->data_providers == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_plan_alloc_copy(arena, staged->data_providers, staged->data_provider_count,
                                sizeof(SlPlanDataProvider), _Alignof(SlPlanDataProvider),
                                (void**)&providers);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (providers == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    for (index = 0U; index < staged->data_provider_count; index += 1U) {
        status = sl_plan_intern_required(table, providers[index].token, &providers[index].token);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status =
            sl_plan_intern_required(table, providers[index].provider, &providers[index].provider);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_plan_intern_required(table, providers[index].capability,
                                         &providers[index].capability);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status =
            sl_plan_intern_required(table, providers[index].service, &providers[index].service);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    staged->data_providers = providers;
    return sl_status_ok();
}

static SlStatus sl_plan_intern_capabilities(SlArena* arena, SlInternTable* table, SlPlan* staged)
{
    SlPlanCapability* capabilities = NULL;
    size_t index = 0U;
    SlStatus status;

    if (staged->capability_count == 0U) {
        staged->capabilities = NULL;
        return sl_status_ok();
    }
    if (staged->capabilities == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_plan_alloc_copy(arena, staged->capabilities, staged->capability_count,
                                sizeof(SlPlanCapability), _Alignof(SlPlanCapability),
                                (void**)&capabilities);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (capabilities == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    for (index = 0U; index < staged->capability_count; index += 1U) {
        status =
            sl_plan_intern_required(table, capabilities[index].token, &capabilities[index].token);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status =
            sl_plan_intern_required(table, capabilities[index].kind, &capabilities[index].kind);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status =
            sl_plan_intern_required(table, capabilities[index].access, &capabilities[index].access);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_plan_intern_required(table, capabilities[index].provider,
                                         &capabilities[index].provider);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    staged->capabilities = capabilities;
    return sl_status_ok();
}

static SlStatus sl_plan_intern_required_features(SlArena* arena, SlInternTable* table,
                                                 SlPlan* staged)
{
    SlPlanRequiredFeature* features = NULL;
    size_t index = 0U;
    SlStatus status;

    if (staged->required_feature_count == 0U) {
        staged->required_features = NULL;
        return sl_status_ok();
    }
    if (staged->required_features == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_plan_alloc_copy(arena, staged->required_features, staged->required_feature_count,
                                sizeof(SlPlanRequiredFeature), _Alignof(SlPlanRequiredFeature),
                                (void**)&features);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (features == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    for (index = 0U; index < staged->required_feature_count; index += 1U) {
        status = sl_plan_intern_required(table, features[index].id, &features[index].id);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    staged->required_features = features;
    return sl_status_ok();
}

SlStatus sl_plan_intern_metadata(SlArena* arena, const SlPlan* plan, size_t capacity,
                                 size_t bucket_count, SlPlan* out_plan, SlInternTable* out_table)
{
    SlArenaMark mark = {0};
    SlInternTable table = {0};
    SlPlan staged = {0};
    SlStatus status;

    if (arena == NULL || plan == NULL || out_plan == NULL || out_table == NULL || capacity == 0U ||
        bucket_count == 0U)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if ((plan->handler_count > 0U && plan->handlers == NULL) ||
        (plan->route_count > 0U && plan->routes == NULL) ||
        (plan->data_provider_count > 0U && plan->data_providers == NULL) ||
        (plan->capability_count > 0U && plan->capabilities == NULL) ||
        (plan->required_feature_count > 0U && plan->required_features == NULL))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    mark = sl_arena_mark(arena);
    staged = *plan;
    table.generation = out_table->generation;

    status = sl_intern_table_init(&table, arena, capacity, bucket_count);
    if (!sl_status_is_ok(status)) {
        goto failure;
    }

    status = sl_plan_intern_header(&table, &staged);
    if (!sl_status_is_ok(status)) {
        goto failure;
    }
    status = sl_plan_intern_handlers(arena, &table, &staged);
    if (!sl_status_is_ok(status)) {
        goto failure;
    }
    status = sl_plan_intern_routes(arena, &table, &staged);
    if (!sl_status_is_ok(status)) {
        goto failure;
    }
    status = sl_plan_intern_providers(arena, &table, &staged);
    if (!sl_status_is_ok(status)) {
        goto failure;
    }
    status = sl_plan_intern_capabilities(arena, &table, &staged);
    if (!sl_status_is_ok(status)) {
        goto failure;
    }
    status = sl_plan_intern_required_features(arena, &table, &staged);
    if (!sl_status_is_ok(status)) {
        goto failure;
    }

    *out_plan = staged;
    *out_table = table;
    return sl_status_ok();

failure:
    (void)sl_arena_reset_to(arena, mark);
    return status;
}

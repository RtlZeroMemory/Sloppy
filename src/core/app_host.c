/*
 * src/core/app_host.c
 *
 * Native app-host hardening for the current Plan-backed runtime path. This module validates
 * app graph metadata that already exists in native Plan structs and provides app/request
 * lifecycle cleanup wrappers used by request execution. It intentionally does not implement
 * DI, middleware, provider opening, module execution, HTTP precedence, or V8 behavior.
 */
#include "sloppy/app_host.h"

static SlStr sl_app_host_literal(const char* ptr, size_t length)
{
    return sl_str_from_parts(ptr, length);
}

static bool sl_app_host_route_method_runnable(SlStr method)
{
    return sl_plan_route_method_runnable(method);
}

static bool sl_app_host_token_syntax_valid(SlStr token)
{
    size_t index = 0U;

    if (sl_str_is_empty(token)) {
        return false;
    }

    for (index = 0U; index < token.length; index += 1U) {
        char ch = token.ptr[index];
        bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                  (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-';
        if (!ok) {
            return false;
        }
    }

    return true;
}

static SlStatus sl_app_host_diag(const SlAppHostStartupValidation* options, SlDiag* out_diag,
                                 SlDiagCode code, SlStr message, SlStr hint,
                                 SlStatusCode status_code)
{
    SlDiagBuilder builder;
    SlStatus status;

    if (out_diag == NULL) {
        return sl_status_from_code(status_code);
    }
    if (options == NULL || options->diag_arena == NULL) {
        *out_diag = (SlDiag){0};
        return sl_status_from_code(status_code);
    }

    status =
        sl_diag_builder_init(&builder, options->diag_arena, SL_DIAG_SEVERITY_ERROR, code, message);
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

static bool sl_app_host_plan_has_service_token(const SlPlan* plan, SlStr token, size_t before)
{
    size_t index = 0U;

    if (plan == NULL || plan->data_providers == NULL || sl_str_is_empty(token)) {
        return false;
    }

    for (index = 0U; index < before; index += 1U) {
        if (sl_str_equal(plan->data_providers[index].service, token)) {
            return true;
        }
    }
    return false;
}

static bool sl_app_host_plan_has_provider(const SlPlan* plan, SlStr token)
{
    size_t index = 0U;

    if (plan == NULL || plan->data_providers == NULL || sl_str_is_empty(token)) {
        return false;
    }

    for (index = 0U; index < plan->data_provider_count; index += 1U) {
        if (sl_str_equal(plan->data_providers[index].token, token)) {
            return true;
        }
    }
    return false;
}

static bool sl_app_host_plan_has_capability(const SlPlan* plan, SlStr token)
{
    size_t index = 0U;

    if (plan == NULL || plan->capabilities == NULL || sl_str_is_empty(token)) {
        return false;
    }

    for (index = 0U; index < plan->capability_count; index += 1U) {
        if (sl_str_equal(plan->capabilities[index].token, token)) {
            return true;
        }
    }
    return false;
}

static SlStatus sl_app_host_validate_plan_header(const SlPlan* plan,
                                                 const SlAppHostStartupValidation* options,
                                                 SlDiag* out_diag)
{
    if (plan == NULL) {
        return sl_app_host_diag(
            options, out_diag, SL_DIAG_INVALID_ARGUMENT,
            sl_app_host_literal("missing app plan", sizeof("missing app plan") - 1U),
            sl_app_host_literal("load and parse app.plan.json before app-host startup validation",
                                sizeof("load and parse app.plan.json before app-host startup "
                                       "validation") -
                                    1U),
            SL_STATUS_INVALID_ARGUMENT);
    }
    if (!sl_plan_version_supported(plan->version)) {
        return sl_app_host_diag(
            options, out_diag, SL_DIAG_INVALID_PLAN_VERSION,
            sl_app_host_literal("unsupported app plan version",
                                sizeof("unsupported app plan version") - 1U),
            sl_app_host_literal("rebuild the app with a compatible sloppyc version",
                                sizeof("rebuild the app with a compatible sloppyc version") - 1U),
            SL_STATUS_UNSUPPORTED);
    }
    if (!sl_str_equal(plan->target.engine, sl_str_from_cstr(SL_PLAN_TARGET_ENGINE_V8)) ||
        !sl_str_equal(plan->target.platform, sl_str_from_cstr(SL_PLAN_TARGET_PLATFORM_WINDOWS_X64)))
    {
        return sl_app_host_diag(
            options, out_diag, SL_DIAG_UNSUPPORTED_ENGINE,
            sl_app_host_literal("unsupported app plan target",
                                sizeof("unsupported app plan target") - 1U),
            sl_app_host_literal("the current dev app host accepts windows-x64/v8 artifacts only",
                                sizeof("the current dev app host accepts windows-x64/v8 artifacts "
                                       "only") -
                                    1U),
            SL_STATUS_UNSUPPORTED);
    }
    if (!sl_str_equal(plan->runtime_min_version,
                      sl_str_from_cstr(SL_PLAN_RUNTIME_MIN_VERSION_0_1_0)))
    {
        return sl_app_host_diag(
            options, out_diag, SL_DIAG_INVALID_PLAN_FIELD,
            sl_app_host_literal("unsupported app plan runtime version",
                                sizeof("unsupported app plan runtime version") - 1U),
            sl_app_host_literal("rebuild the app with the current runtime-compatible compiler",
                                sizeof("rebuild the app with the current runtime-compatible "
                                       "compiler") -
                                    1U),
            SL_STATUS_UNSUPPORTED);
    }
    if (plan->handler_count == 0U || plan->handlers == NULL) {
        return sl_app_host_diag(
            options, out_diag, SL_DIAG_INVALID_PLAN_FIELD,
            sl_app_host_literal("app plan has no handler table",
                                sizeof("app plan has no handler table") - 1U),
            sl_app_host_literal("handlers[] must contain at least one startup-visible handler",
                                sizeof("handlers[] must contain at least one startup-visible "
                                       "handler") -
                                    1U),
            SL_STATUS_INVALID_ARGUMENT);
    }
    if (sl_plan_has_duplicate_handler_ids(plan)) {
        return sl_app_host_diag(
            options, out_diag, SL_DIAG_DUPLICATE_HANDLER_ID,
            sl_app_host_literal("duplicate handler id", sizeof("duplicate handler id") - 1U),
            sl_app_host_literal("handler ids must be unique before request dispatch starts",
                                sizeof("handler ids must be unique before request dispatch "
                                       "starts") -
                                    1U),
            SL_STATUS_INVALID_STATE);
    }
    return sl_status_ok();
}

static SlStatus sl_app_host_validate_routes(const SlPlan* plan,
                                            const SlAppHostStartupValidation* options,
                                            SlDiag* out_diag)
{
    size_t index = 0U;
    size_t runnable_routes = 0U;

    if (plan->route_count > 0U && plan->routes == NULL) {
        return sl_app_host_diag(
            options, out_diag, SL_DIAG_INVALID_PLAN_FIELD,
            sl_app_host_literal("app plan route table is malformed",
                                sizeof("app plan route table is malformed") - 1U),
            sl_app_host_literal("routes must point to route_count entries during startup "
                                "validation",
                                sizeof("routes must point to route_count entries during startup "
                                       "validation") -
                                    1U),
            SL_STATUS_INVALID_ARGUMENT);
    }
    if (options != NULL && options->require_runnable_route && plan->route_count == 0U) {
        return sl_app_host_diag(
            options, out_diag, SL_DIAG_INVALID_PLAN_FIELD,
            sl_app_host_literal("app plan does not contain runnable route metadata",
                                sizeof("app plan does not contain runnable route metadata") - 1U),
            sl_app_host_literal("compiler-emitted runnable artifacts must include at least one "
                                "route",
                                sizeof("compiler-emitted runnable artifacts must include at least "
                                       "one route") -
                                    1U),
            SL_STATUS_INVALID_ARGUMENT);
    }
    if (sl_plan_has_duplicate_routes(plan)) {
        return sl_app_host_diag(
            options, out_diag, SL_DIAG_INVALID_PLAN_FIELD,
            sl_app_host_literal("duplicate app plan route",
                                sizeof("duplicate app plan route") - 1U),
            sl_app_host_literal("route method and pattern pairs must be unique before serving",
                                sizeof("route method and pattern pairs must be unique before "
                                       "serving") -
                                    1U),
            SL_STATUS_INVALID_STATE);
    }
    if (sl_plan_has_duplicate_route_names(plan)) {
        return sl_app_host_diag(
            options, out_diag, SL_DIAG_INVALID_PLAN_FIELD,
            sl_app_host_literal("duplicate app plan route name",
                                sizeof("duplicate app plan route name") - 1U),
            sl_app_host_literal("non-empty route names must be unique before serving",
                                sizeof("non-empty route names must be unique before serving") - 1U),
            SL_STATUS_INVALID_STATE);
    }

    for (index = 0U; index < plan->route_count; index += 1U) {
        const SlPlanHandler* handler = NULL;
        const SlPlanRoute* route = &plan->routes[index];
        if (!sl_plan_route_method_supported(route->method)) {
            return sl_app_host_diag(
                options, out_diag, SL_DIAG_INVALID_PLAN_FIELD,
                sl_app_host_literal("unsupported app plan route method",
                                    sizeof("unsupported app plan route method") - 1U),
                sl_app_host_literal(
                    "Plan route metadata must use a supported framework method",
                    sizeof("Plan route metadata must use a supported framework method") - 1U),
                SL_STATUS_UNSUPPORTED);
        }
        if (!sl_status_is_ok(sl_plan_find_handler_by_id(plan, route->handler_id, &handler))) {
            return sl_app_host_diag(
                options, out_diag, SL_DIAG_INVALID_PLAN_FIELD,
                sl_app_host_literal("app plan route references missing handler",
                                    sizeof("app plan route references missing handler") - 1U),
                sl_app_host_literal("routes[].handlerId must reference handlers[].id before "
                                    "serving",
                                    sizeof("routes[].handlerId must reference handlers[].id before "
                                           "serving") -
                                        1U),
                SL_STATUS_INVALID_ARGUMENT);
        }
        if (!sl_app_host_route_method_runnable(route->method)) {
            continue;
        }
        runnable_routes += 1U;
    }

    if (options != NULL && options->require_runnable_route && runnable_routes == 0U) {
        return sl_app_host_diag(
            options, out_diag, SL_DIAG_INVALID_PLAN_FIELD,
            sl_app_host_literal("app plan does not contain runnable route metadata",
                                sizeof("app plan does not contain runnable route metadata") - 1U),
            sl_app_host_literal("the current dev app host serves GET route metadata only",
                                sizeof("the current dev app host serves GET route metadata only") -
                                    1U),
            SL_STATUS_INVALID_ARGUMENT);
    }
    if (options != NULL && options->max_runnable_routes > 0U &&
        runnable_routes > options->max_runnable_routes)
    {
        return sl_app_host_diag(
            options, out_diag, SL_DIAG_INVALID_PLAN_FIELD,
            sl_app_host_literal("app plan has too many runnable routes",
                                sizeof("app plan has too many runnable routes") - 1U),
            sl_app_host_literal("reduce route metadata or raise the host route-table capacity",
                                sizeof("reduce route metadata or raise the host route-table "
                                       "capacity") -
                                    1U),
            SL_STATUS_CAPACITY_EXCEEDED);
    }

    return sl_status_ok();
}

static SlStatus sl_app_host_validate_one_provider(const SlPlan* plan, size_t index,
                                                  const SlAppHostStartupValidation* options,
                                                  SlDiag* out_diag)
{
    const SlPlanDataProvider* provider = &plan->data_providers[index];

    if (!sl_app_host_token_syntax_valid(provider->token) ||
        !sl_plan_provider_supported(provider->provider))
    {
        return sl_app_host_diag(
            options, out_diag, SL_DIAG_INVALID_PLAN_FIELD,
            sl_app_host_literal("invalid app plan provider metadata",
                                sizeof("invalid app plan provider metadata") - 1U),
            sl_app_host_literal("provider metadata must use supported tokens and provider values",
                                sizeof("provider metadata must use supported tokens and provider "
                                       "values") -
                                    1U),
            SL_STATUS_INVALID_ARGUMENT);
    }

    if (!sl_str_is_empty(provider->service)) {
        if (!sl_app_host_token_syntax_valid(provider->service)) {
            return sl_app_host_diag(
                options, out_diag, SL_DIAG_INVALID_PLAN_FIELD,
                sl_app_host_literal("invalid app plan service token",
                                    sizeof("invalid app plan service token") - 1U),
                sl_app_host_literal("service tokens may contain letters, digits, '.', '_', and '-'",
                                    sizeof("service tokens may contain letters, digits, '.', '_', "
                                           "and '-'") -
                                        1U),
                SL_STATUS_INVALID_ARGUMENT);
        }
        if (sl_app_host_plan_has_service_token(plan, provider->service, index)) {
            return sl_app_host_diag(
                options, out_diag, SL_DIAG_INVALID_PLAN_FIELD,
                sl_app_host_literal("duplicate app plan service token",
                                    sizeof("duplicate app plan service token") - 1U),
                sl_app_host_literal(
                    "service tokens represented in provider metadata must be unique",
                    sizeof("service tokens represented in provider metadata must be unique") - 1U),
                SL_STATUS_INVALID_STATE);
        }
    }

    if (!sl_str_is_empty(provider->capability) &&
        !sl_app_host_plan_has_capability(plan, provider->capability))
    {
        return sl_app_host_diag(
            options, out_diag, SL_DIAG_INVALID_PLAN_FIELD,
            sl_app_host_literal("app plan provider references missing capability",
                                sizeof("app plan provider references missing capability") - 1U),
            sl_app_host_literal("dataProviders[].capability must reference capabilities[].token",
                                sizeof("dataProviders[].capability must reference "
                                       "capabilities[].token") -
                                    1U),
            SL_STATUS_INVALID_ARGUMENT);
    }

    return sl_status_ok();
}

static SlStatus sl_app_host_validate_one_capability(const SlPlan* plan, size_t index,
                                                    const SlAppHostStartupValidation* options,
                                                    SlDiag* out_diag)
{
    const SlPlanCapability* capability = &plan->capabilities[index];

    if (!sl_app_host_token_syntax_valid(capability->token) ||
        !sl_plan_capability_kind_supported(capability->kind) ||
        !sl_plan_capability_access_supported(capability->kind, capability->access))
    {
        return sl_app_host_diag(
            options, out_diag, SL_DIAG_INVALID_PLAN_FIELD,
            sl_app_host_literal("invalid app plan capability metadata",
                                sizeof("invalid app plan capability metadata") - 1U),
            sl_app_host_literal(
                "capability metadata must use supported token, kind, and access values",
                sizeof("capability metadata must use supported token, kind, and access values") -
                    1U),
            SL_STATUS_INVALID_ARGUMENT);
    }
    if (!sl_str_is_empty(capability->provider) &&
        !sl_app_host_plan_has_provider(plan, capability->provider))
    {
        return sl_app_host_diag(
            options, out_diag, SL_DIAG_INVALID_PLAN_FIELD,
            sl_app_host_literal("app plan capability references missing provider",
                                sizeof("app plan capability references missing provider") - 1U),
            sl_app_host_literal("capabilities[].provider must reference dataProviders[].token",
                                sizeof("capabilities[].provider must reference "
                                       "dataProviders[].token") -
                                    1U),
            SL_STATUS_INVALID_ARGUMENT);
    }

    return sl_status_ok();
}

static SlStatus sl_app_host_validate_metadata_shape(const SlPlan* plan,
                                                    const SlAppHostStartupValidation* options,
                                                    SlDiag* out_diag)
{
    if (plan->data_provider_count > 0U && plan->data_providers == NULL) {
        return sl_app_host_diag(
            options, out_diag, SL_DIAG_INVALID_PLAN_FIELD,
            sl_app_host_literal("app plan provider metadata is malformed",
                                sizeof("app plan provider metadata is malformed") - 1U),
            sl_app_host_literal("dataProviders must point to data_provider_count entries",
                                sizeof("dataProviders must point to data_provider_count entries") -
                                    1U),
            SL_STATUS_INVALID_ARGUMENT);
    }
    if (plan->capability_count > 0U && plan->capabilities == NULL) {
        return sl_app_host_diag(
            options, out_diag, SL_DIAG_INVALID_PLAN_FIELD,
            sl_app_host_literal("app plan capability metadata is malformed",
                                sizeof("app plan capability metadata is malformed") - 1U),
            sl_app_host_literal("capabilities must point to capability_count entries",
                                sizeof("capabilities must point to capability_count entries") - 1U),
            SL_STATUS_INVALID_ARGUMENT);
    }
    if (sl_plan_has_duplicate_data_provider_tokens(plan)) {
        return sl_app_host_diag(
            options, out_diag, SL_DIAG_INVALID_PLAN_FIELD,
            sl_app_host_literal("duplicate app plan provider token",
                                sizeof("duplicate app plan provider token") - 1U),
            sl_app_host_literal("data provider tokens must be unique before startup",
                                sizeof("data provider tokens must be unique before startup") - 1U),
            SL_STATUS_INVALID_STATE);
    }
    if (sl_plan_has_duplicate_capability_tokens(plan)) {
        return sl_app_host_diag(
            options, out_diag, SL_DIAG_INVALID_PLAN_FIELD,
            sl_app_host_literal("duplicate app plan capability token",
                                sizeof("duplicate app plan capability token") - 1U),
            sl_app_host_literal("capability tokens must be unique before startup",
                                sizeof("capability tokens must be unique before startup") - 1U),
            SL_STATUS_INVALID_STATE);
    }
    return sl_status_ok();
}

static SlStatus sl_app_host_validate_metadata(const SlPlan* plan,
                                              const SlAppHostStartupValidation* options,
                                              SlDiag* out_diag)
{
    size_t index = 0U;
    SlStatus status = sl_app_host_validate_metadata_shape(plan, options, out_diag);

    if (!sl_status_is_ok(status)) {
        return status;
    }

    for (index = 0U; index < plan->data_provider_count; index += 1U) {
        status = sl_app_host_validate_one_provider(plan, index, options, out_diag);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    for (index = 0U; index < plan->capability_count; index += 1U) {
        status = sl_app_host_validate_one_capability(plan, index, options, out_diag);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    return sl_status_ok();
}

SlStatus sl_app_host_validate_startup(const SlPlan* plan, const SlAppHostStartupValidation* options,
                                      SlDiag* out_diag)
{
    SlStatus status;

    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }

    status = sl_app_host_validate_plan_header(plan, options, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_app_host_validate_routes(plan, options, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_app_host_validate_metadata(plan, options, out_diag);
}

static SlStatus sl_app_lifecycle_diag(SlDiag* out_diag, SlStr message, SlStr hint,
                                      SlStatusCode status_code)
{
    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
        out_diag->severity = SL_DIAG_SEVERITY_ERROR;
        out_diag->code = SL_DIAG_APP_LIFECYCLE;
        out_diag->message = message;
        if (!sl_str_is_empty(hint)) {
            out_diag->hints[0] = hint;
            out_diag->hint_count = 1U;
        }
    }
    return sl_status_from_code(status_code);
}

static bool sl_app_lifecycle_can_register_cleanup(SlAppLifecycleState state)
{
    return state == SL_APP_LIFECYCLE_STATE_RUNNING;
}

static bool sl_app_lifecycle_can_open_request(SlAppLifecycleState state)
{
    return state == SL_APP_LIFECYCLE_STATE_RUNNING;
}

static SlStatus sl_app_lifecycle_close_cleanups(SlAppLifecycle* lifecycle,
                                                SlAppLifecycleState success_state, SlDiag* out_diag)
{
    SlStatus status = sl_scope_close(&lifecycle->cleanups);

    lifecycle->state = success_state;
    if (!sl_status_is_ok(status)) {
        lifecycle->state = SL_APP_LIFECYCLE_STATE_FAILED;
        return sl_app_lifecycle_diag(
            out_diag,
            sl_app_host_literal("app lifecycle cleanup failed",
                                sizeof("app lifecycle cleanup failed") - 1U),
            sl_app_host_literal("app shutdown cleanup callbacks must be deterministic",
                                sizeof("app shutdown cleanup callbacks must be deterministic") -
                                    1U),
            SL_STATUS_INVALID_STATE);
    }

    return sl_status_ok();
}

static bool sl_app_resource_cleanup_valid(const SlAppResourceCleanup* resource)
{
    return resource != NULL && resource->table != NULL && sl_resource_id_is_valid(resource->id);
}

static bool sl_app_resource_cleanup_has_kind(const SlAppResourceCleanup* resource)
{
    return resource != NULL && resource->kind != SL_RESOURCE_KIND_NONE;
}

static void sl_app_resource_cleanup_close(void* payload, void* user)
{
    SlAppResourceCleanup* resource = (SlAppResourceCleanup*)payload;
    SlStatus status;

    (void)user;
    if (!sl_app_resource_cleanup_valid(resource)) {
        return;
    }

    status = sl_app_resource_cleanup_has_kind(resource)
                 ? sl_resource_table_close_kind(resource->table, resource->id, resource->kind, NULL)
                 : sl_resource_table_close(resource->table, resource->id, NULL);
    if (sl_status_is_ok(status)) {
        resource->id = sl_resource_id_invalid();
    }
}

SlStatus sl_app_lifecycle_start(SlAppLifecycle* lifecycle, SlScopeCleanup* storage,
                                size_t cleanup_capacity, SlDiag* out_diag)
{
    return sl_app_lifecycle_start_with_id(lifecycle, storage, cleanup_capacity, 0U, out_diag);
}

SlStatus sl_app_lifecycle_start_with_id(SlAppLifecycle* lifecycle, SlScopeCleanup* storage,
                                        size_t cleanup_capacity, uint64_t app_id, SlDiag* out_diag)
{
    SlStatus status;

    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (lifecycle == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (lifecycle->state == SL_APP_LIFECYCLE_STATE_STARTING ||
        lifecycle->state == SL_APP_LIFECYCLE_STATE_RUNNING ||
        lifecycle->state == SL_APP_LIFECYCLE_STATE_STOPPING ||
        lifecycle->state == SL_APP_LIFECYCLE_STATE_DRAINING)
    {
        return sl_app_lifecycle_diag(
            out_diag,
            sl_app_host_literal("app lifecycle is already started",
                                sizeof("app lifecycle is already started") - 1U),
            sl_app_host_literal("shutdown the current app lifecycle before starting another one",
                                sizeof("shutdown the current app lifecycle before starting another "
                                       "one") -
                                    1U),
            SL_STATUS_INVALID_STATE);
    }
    if (lifecycle->state == SL_APP_LIFECYCLE_STATE_STOPPED) {
        return sl_app_lifecycle_diag(
            out_diag,
            sl_app_host_literal("app lifecycle is already shut down",
                                sizeof("app lifecycle is already shut down") - 1U),
            sl_app_host_literal("use a fresh zero-initialized lifecycle for the next app",
                                sizeof("use a fresh zero-initialized lifecycle for the next app") -
                                    1U),
            SL_STATUS_INVALID_STATE);
    }
    if (lifecycle->state == SL_APP_LIFECYCLE_STATE_FAILED) {
        return sl_app_lifecycle_diag(
            out_diag,
            sl_app_host_literal("app lifecycle has failed",
                                sizeof("app lifecycle has failed") - 1U),
            sl_app_host_literal("use a fresh zero-initialized lifecycle for the next app",
                                sizeof("use a fresh zero-initialized lifecycle for the next app") -
                                    1U),
            SL_STATUS_INVALID_STATE);
    }

    lifecycle->state = SL_APP_LIFECYCLE_STATE_STARTING;
    status = sl_scope_init(&lifecycle->cleanups, storage, cleanup_capacity);
    if (!sl_status_is_ok(status)) {
        lifecycle->state = SL_APP_LIFECYCLE_STATE_FAILED;
        return status;
    }

    lifecycle->app_id = app_id;
    lifecycle->active_request_scopes = 0U;
    lifecycle->state = SL_APP_LIFECYCLE_STATE_RUNNING;
    return sl_status_ok();
}

SlStatus sl_app_lifecycle_add_cleanup(SlAppLifecycle* lifecycle, SlScopeCleanupFn fn, void* payload,
                                      void* user, SlDiag* out_diag)
{
    SlStatus status;

    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (lifecycle == NULL || fn == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (!sl_app_lifecycle_can_register_cleanup(lifecycle->state)) {
        return sl_app_lifecycle_diag(
            out_diag,
            sl_app_host_literal("app lifecycle is not active",
                                sizeof("app lifecycle is not active") - 1U),
            sl_app_host_literal("start the app lifecycle before registering cleanup",
                                sizeof("start the app lifecycle before registering cleanup") - 1U),
            SL_STATUS_INVALID_STATE);
    }

    status = sl_scope_add_cleanup(&lifecycle->cleanups, fn, payload, user);
    if (sl_status_code(status) == SL_STATUS_CAPACITY_EXCEEDED) {
        return sl_app_lifecycle_diag(
            out_diag,
            sl_app_host_literal("app lifecycle cleanup capacity was exceeded",
                                sizeof("app lifecycle cleanup capacity was exceeded") - 1U),
            sl_app_host_literal("raise the app lifecycle cleanup capacity for this host",
                                sizeof("raise the app lifecycle cleanup capacity for this host") -
                                    1U),
            SL_STATUS_CAPACITY_EXCEEDED);
    }

    return status;
}

SlStatus sl_app_lifecycle_add_resource_cleanup(SlAppLifecycle* lifecycle,
                                               SlAppResourceCleanup* resource, SlDiag* out_diag)
{
    if (!sl_app_resource_cleanup_valid(resource)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    return sl_app_lifecycle_add_cleanup(lifecycle, sl_app_resource_cleanup_close, resource, NULL,
                                        out_diag);
}

SlStatus sl_app_lifecycle_fail_startup(SlAppLifecycle* lifecycle, SlDiag* out_diag)
{
    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (lifecycle == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (lifecycle->state == SL_APP_LIFECYCLE_STATE_FAILED) {
        return sl_status_ok();
    }
    if (lifecycle->state == SL_APP_LIFECYCLE_STATE_CREATED ||
        lifecycle->state == SL_APP_LIFECYCLE_STATE_STOPPED)
    {
        lifecycle->state = SL_APP_LIFECYCLE_STATE_FAILED;
        return sl_status_ok();
    }

    return sl_app_lifecycle_close_cleanups(lifecycle, SL_APP_LIFECYCLE_STATE_FAILED, out_diag);
}

SlStatus sl_app_lifecycle_begin_shutdown(SlAppLifecycle* lifecycle, SlDiag* out_diag)
{
    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (lifecycle == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (lifecycle->state == SL_APP_LIFECYCLE_STATE_CREATED ||
        lifecycle->state == SL_APP_LIFECYCLE_STATE_STOPPED)
    {
        lifecycle->state = SL_APP_LIFECYCLE_STATE_STOPPED;
        return sl_status_ok();
    }
    if (lifecycle->state == SL_APP_LIFECYCLE_STATE_FAILED) {
        return sl_status_ok();
    }
    if (lifecycle->state == SL_APP_LIFECYCLE_STATE_STOPPING ||
        lifecycle->state == SL_APP_LIFECYCLE_STATE_DRAINING)
    {
        return sl_status_ok();
    }
    if (lifecycle->state != SL_APP_LIFECYCLE_STATE_RUNNING) {
        return sl_app_lifecycle_diag(
            out_diag,
            sl_app_host_literal("app lifecycle is not active",
                                sizeof("app lifecycle is not active") - 1U),
            sl_app_host_literal("start the app lifecycle before shutting it down",
                                sizeof("start the app lifecycle before shutting it down") - 1U),
            SL_STATUS_INVALID_STATE);
    }

    lifecycle->state = lifecycle->active_request_scopes == 0U ? SL_APP_LIFECYCLE_STATE_STOPPING
                                                              : SL_APP_LIFECYCLE_STATE_DRAINING;
    return sl_status_ok();
}

SlStatus sl_app_lifecycle_finish_shutdown(SlAppLifecycle* lifecycle, SlDiag* out_diag)
{
    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (lifecycle == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (lifecycle->state == SL_APP_LIFECYCLE_STATE_CREATED ||
        lifecycle->state == SL_APP_LIFECYCLE_STATE_STOPPED)
    {
        lifecycle->state = SL_APP_LIFECYCLE_STATE_STOPPED;
        return sl_status_ok();
    }
    if (lifecycle->state == SL_APP_LIFECYCLE_STATE_FAILED) {
        return sl_status_ok();
    }
    if (lifecycle->state != SL_APP_LIFECYCLE_STATE_STOPPING &&
        lifecycle->state != SL_APP_LIFECYCLE_STATE_DRAINING)
    {
        return sl_app_lifecycle_diag(
            out_diag,
            sl_app_host_literal("app shutdown has not started",
                                sizeof("app shutdown has not started") - 1U),
            sl_app_host_literal("begin shutdown before finishing app cleanup",
                                sizeof("begin shutdown before finishing app cleanup") - 1U),
            SL_STATUS_INVALID_STATE);
    }
    if (lifecycle->active_request_scopes != 0U) {
        lifecycle->state = SL_APP_LIFECYCLE_STATE_DRAINING;
        return sl_app_lifecycle_diag(
            out_diag,
            sl_app_host_literal("app lifecycle is draining active requests",
                                sizeof("app lifecycle is draining active requests") - 1U),
            sl_app_host_literal("close active request scopes or force shutdown",
                                sizeof("close active request scopes or force shutdown") - 1U),
            SL_STATUS_INVALID_STATE);
    }

    lifecycle->state = SL_APP_LIFECYCLE_STATE_STOPPING;
    return sl_app_lifecycle_close_cleanups(lifecycle, SL_APP_LIFECYCLE_STATE_STOPPED, out_diag);
}

SlStatus sl_app_lifecycle_force_shutdown(SlAppLifecycle* lifecycle, SlDiag* out_diag)
{
    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (lifecycle == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (lifecycle->state == SL_APP_LIFECYCLE_STATE_CREATED ||
        lifecycle->state == SL_APP_LIFECYCLE_STATE_STOPPED)
    {
        lifecycle->state = SL_APP_LIFECYCLE_STATE_STOPPED;
        return sl_status_ok();
    }
    if (lifecycle->state == SL_APP_LIFECYCLE_STATE_FAILED) {
        return sl_status_ok();
    }

    lifecycle->state = SL_APP_LIFECYCLE_STATE_STOPPING;
    lifecycle->active_request_scopes = 0U;
    return sl_app_lifecycle_close_cleanups(lifecycle, SL_APP_LIFECYCLE_STATE_STOPPED, out_diag);
}

SlStatus sl_app_lifecycle_shutdown(SlAppLifecycle* lifecycle, SlDiag* out_diag)
{
    SlStatus status = sl_app_lifecycle_begin_shutdown(lifecycle, out_diag);

    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_app_lifecycle_finish_shutdown(lifecycle, out_diag);
}

SlAppLifecycleState sl_app_lifecycle_state(const SlAppLifecycle* lifecycle)
{
    return lifecycle == NULL ? SL_APP_LIFECYCLE_STATE_UNINITIALIZED : lifecycle->state;
}

bool sl_app_lifecycle_is_started(const SlAppLifecycle* lifecycle)
{
    return sl_app_lifecycle_state(lifecycle) == SL_APP_LIFECYCLE_STATE_RUNNING;
}

bool sl_app_lifecycle_is_shutdown(const SlAppLifecycle* lifecycle)
{
    return sl_app_lifecycle_state(lifecycle) == SL_APP_LIFECYCLE_STATE_STOPPED;
}

uint64_t sl_app_lifecycle_app_id(const SlAppLifecycle* lifecycle)
{
    return lifecycle == NULL ? 0U : lifecycle->app_id;
}

size_t sl_app_lifecycle_active_request_count(const SlAppLifecycle* lifecycle)
{
    return lifecycle == NULL ? 0U : lifecycle->active_request_scopes;
}

static SlStatus sl_app_request_scope_diag(SlDiag* out_diag, SlDiagCode code, SlStr message)
{
    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
        out_diag->severity = SL_DIAG_SEVERITY_ERROR;
        out_diag->code = code;
        out_diag->message = message;
    }
    return sl_status_from_code(SL_STATUS_INVALID_STATE);
}

static SlCancellationReason sl_app_request_outcome_reason(SlAppRequestOutcome outcome)
{
    switch (outcome) {
    case SL_APP_REQUEST_OUTCOME_TIMEOUT:
        return SL_CANCELLATION_REASON_DEADLINE_EXCEEDED;
    case SL_APP_REQUEST_OUTCOME_CANCELLED:
    case SL_APP_REQUEST_OUTCOME_CLIENT_DISCONNECT:
    case SL_APP_REQUEST_OUTCOME_PROVIDER_CANCELLED_BEFORE_START:
    case SL_APP_REQUEST_OUTCOME_PROVIDER_LATE_COMPLETION:
        return SL_CANCELLATION_REASON_CANCELLED;
    case SL_APP_REQUEST_OUTCOME_SHUTDOWN:
        return SL_CANCELLATION_REASON_SHUTDOWN;
    case SL_APP_REQUEST_OUTCOME_BACKPRESSURE:
        return SL_CANCELLATION_REASON_BACKPRESSURE;
    default:
        return SL_CANCELLATION_REASON_NONE;
    }
}

static SlStr sl_app_request_outcome_name(SlAppRequestOutcome outcome)
{
    switch (outcome) {
    case SL_APP_REQUEST_OUTCOME_SUCCESS:
        return sl_app_host_literal("request completed successfully",
                                   sizeof("request completed successfully") - 1U);
    case SL_APP_REQUEST_OUTCOME_SYNC_ERROR:
        return sl_app_host_literal("request failed synchronously",
                                   sizeof("request failed synchronously") - 1U);
    case SL_APP_REQUEST_OUTCOME_V8_EXCEPTION:
        return sl_app_host_literal("request failed with V8 exception",
                                   sizeof("request failed with V8 exception") - 1U);
    case SL_APP_REQUEST_OUTCOME_PROMISE_REJECTION:
        return sl_app_host_literal("request failed with promise rejection",
                                   sizeof("request failed with promise rejection") - 1U);
    case SL_APP_REQUEST_OUTCOME_VALIDATION_FAILURE:
        return sl_app_host_literal("request validation failed",
                                   sizeof("request validation failed") - 1U);
    case SL_APP_REQUEST_OUTCOME_BODY_PARSE_FAILURE:
        return sl_app_host_literal("request body parse failed",
                                   sizeof("request body parse failed") - 1U);
    case SL_APP_REQUEST_OUTCOME_TIMEOUT:
        return sl_app_host_literal("request timed out", sizeof("request timed out") - 1U);
    case SL_APP_REQUEST_OUTCOME_CANCELLED:
        return sl_app_host_literal("request was cancelled", sizeof("request was cancelled") - 1U);
    case SL_APP_REQUEST_OUTCOME_CLIENT_DISCONNECT:
        return sl_app_host_literal("request client disconnected",
                                   sizeof("request client disconnected") - 1U);
    case SL_APP_REQUEST_OUTCOME_RESPONSE_WRITE_FAILURE:
        return sl_app_host_literal("request response write failed",
                                   sizeof("request response write failed") - 1U);
    case SL_APP_REQUEST_OUTCOME_PROVIDER_FAILURE:
        return sl_app_host_literal("request provider operation failed",
                                   sizeof("request provider operation failed") - 1U);
    case SL_APP_REQUEST_OUTCOME_PROVIDER_CANCELLED_BEFORE_START:
        return sl_app_host_literal("request provider operation cancelled before start",
                                   sizeof("request provider operation cancelled before start") -
                                       1U);
    case SL_APP_REQUEST_OUTCOME_PROVIDER_LATE_COMPLETION:
        return sl_app_host_literal("request provider late completion dropped",
                                   sizeof("request provider late completion dropped") - 1U);
    case SL_APP_REQUEST_OUTCOME_SHUTDOWN:
        return sl_app_host_literal("request stopped during app shutdown",
                                   sizeof("request stopped during app shutdown") - 1U);
    case SL_APP_REQUEST_OUTCOME_BACKPRESSURE:
        return sl_app_host_literal("request rejected by backpressure",
                                   sizeof("request rejected by backpressure") - 1U);
    default:
        return sl_app_host_literal("request terminal state is unavailable",
                                   sizeof("request terminal state is unavailable") - 1U);
    }
}

static SlAppRequestOutcome sl_app_request_outcome_from_status(SlStatus status)
{
    switch (sl_status_code(status)) {
    case SL_STATUS_OK:
        return SL_APP_REQUEST_OUTCOME_SUCCESS;
    case SL_STATUS_CANCELLED:
        return SL_APP_REQUEST_OUTCOME_CANCELLED;
    case SL_STATUS_DEADLINE_EXCEEDED:
        return SL_APP_REQUEST_OUTCOME_TIMEOUT;
    case SL_STATUS_CAPACITY_EXCEEDED:
        return SL_APP_REQUEST_OUTCOME_BACKPRESSURE;
    case SL_STATUS_INVALID_ARGUMENT:
    case SL_STATUS_OUT_OF_RANGE:
        return SL_APP_REQUEST_OUTCOME_VALIDATION_FAILURE;
    default:
        return SL_APP_REQUEST_OUTCOME_SYNC_ERROR;
    }
}

static SlDiagCode sl_app_request_diag_from_status(SlStatus status)
{
    switch (sl_status_code(status)) {
    case SL_STATUS_OK:
        return SL_DIAG_NONE;
    case SL_STATUS_CANCELLED:
        return SL_DIAG_ENGINE_CANCELLED;
    case SL_STATUS_DEADLINE_EXCEEDED:
        return SL_DIAG_HTTP_REQUEST_TIMEOUT;
    case SL_STATUS_CAPACITY_EXCEEDED:
        return SL_DIAG_ENGINE_BACKPRESSURE;
    default:
        return SL_DIAG_APP_LIFECYCLE;
    }
}

static SlStatus sl_app_request_scope_close_internal(SlAppRequestScope* request_scope,
                                                    SlDiag* out_diag)
{
    SlStatus status;

    status = sl_scope_close(&request_scope->cleanups);
    request_scope->active = false;
    if (request_scope->lifecycle != NULL && request_scope->lifecycle->active_request_scopes > 0U) {
        request_scope->lifecycle->active_request_scopes -= 1U;
    }
    if (!sl_status_is_ok(status)) {
        return sl_app_request_scope_diag(
            out_diag, SL_DIAG_INTERNAL_ERROR,
            sl_app_host_literal("request scope cleanup failed",
                                sizeof("request scope cleanup failed") - 1U));
    }

    return sl_status_ok();
}

SlStatus sl_app_request_scope_init(SlAppRequestScope* request_scope, SlScopeCleanup* storage,
                                   size_t cleanup_capacity)
{
    SlStatus status;

    if (request_scope == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *request_scope = (SlAppRequestScope){0};
    status = sl_scope_init(&request_scope->cleanups, storage, cleanup_capacity);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    request_scope->active = true;
    request_scope->terminal_status = sl_status_ok();
    return sl_status_ok();
}

SlStatus sl_app_request_scope_init_for_app(SlAppRequestScope* request_scope,
                                           SlAppLifecycle* lifecycle, uint64_t request_id,
                                           SlScopeCleanup* storage, size_t cleanup_capacity,
                                           SlDiag* out_diag)
{
    SlStatus status;

    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (lifecycle == NULL || request_id == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (!sl_app_lifecycle_can_open_request(lifecycle->state)) {
        return sl_app_lifecycle_diag(
            out_diag,
            sl_app_host_literal("app lifecycle is not accepting request scopes",
                                sizeof("app lifecycle is not accepting request scopes") - 1U),
            sl_app_host_literal("start the app lifecycle before opening request scopes",
                                sizeof("start the app lifecycle before opening request scopes") -
                                    1U),
            SL_STATUS_INVALID_STATE);
    }

    status = sl_app_request_scope_init(request_scope, storage, cleanup_capacity);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    request_scope->lifecycle = lifecycle;
    request_scope->app_id = lifecycle->app_id;
    request_scope->request_id = request_id;
    lifecycle->active_request_scopes += 1U;
    return sl_status_ok();
}

SlStatus sl_app_request_scope_add_cleanup(SlAppRequestScope* request_scope, SlScopeCleanupFn fn,
                                          void* payload, void* user)
{
    if (request_scope == NULL || !request_scope->active) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    return sl_scope_add_cleanup(&request_scope->cleanups, fn, payload, user);
}

SlStatus sl_app_request_scope_add_resource_cleanup(SlAppRequestScope* request_scope,
                                                   SlAppResourceCleanup* resource)
{
    if (!sl_app_resource_cleanup_valid(resource)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    return sl_app_request_scope_add_cleanup(request_scope, sl_app_resource_cleanup_close, resource,
                                            NULL);
}

SlStatus sl_app_request_scope_close(SlAppRequestScope* request_scope, SlDiag* out_diag)
{
    if (request_scope == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (!request_scope->active) {
        return sl_status_ok();
    }
    if (!request_scope->terminal) {
        request_scope->terminal = true;
        request_scope->terminal_status = sl_status_ok();
        request_scope->terminal_diag_code = SL_DIAG_NONE;
        request_scope->terminal_reason = SL_CANCELLATION_REASON_NONE;
    }

    return sl_app_request_scope_close_internal(request_scope, out_diag);
}

SlStatus sl_app_request_scope_complete(SlAppRequestScope* request_scope,
                                       SlAppRequestOutcome outcome, SlStatus status,
                                       SlDiagCode diag_code, SlDiag* out_diag)
{
    if (request_scope == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (request_scope->terminal) {
        return sl_app_request_scope_reject_late_completion(request_scope, outcome, out_diag);
    }
    if (!request_scope->active) {
        return sl_app_request_scope_diag(
            out_diag, SL_DIAG_APP_LIFECYCLE,
            sl_app_host_literal("request scope is closed", sizeof("request scope is closed") - 1U));
    }

    request_scope->terminal = true;
    request_scope->terminal_status = status;
    request_scope->terminal_diag_code = diag_code;
    request_scope->terminal_reason = sl_app_request_outcome_reason(outcome);

    return sl_app_request_scope_close_internal(request_scope, out_diag);
}

SlStatus sl_app_request_scope_reject_late_completion(const SlAppRequestScope* request_scope,
                                                     SlAppRequestOutcome outcome, SlDiag* out_diag)
{
    if (request_scope == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
        out_diag->severity = SL_DIAG_SEVERITY_ERROR;
        out_diag->code = SL_DIAG_APP_LIFECYCLE;
        out_diag->message = sl_app_request_outcome_name(outcome);
        out_diag->hints[0] = sl_app_host_literal(
            "late request completion must not touch closed scope state",
            sizeof("late request completion must not touch closed scope state") - 1U);
        out_diag->hint_count = 1U;
    }

    return sl_status_from_code(SL_STATUS_STALE_RESOURCE);
}

bool sl_app_request_scope_is_active(const SlAppRequestScope* request_scope)
{
    return request_scope != NULL && request_scope->active;
}

bool sl_app_request_scope_is_terminal(const SlAppRequestScope* request_scope)
{
    return request_scope != NULL && request_scope->terminal;
}

SlStatus sl_app_request_scope_terminal_status(const SlAppRequestScope* request_scope)
{
    return request_scope == NULL ? sl_status_from_code(SL_STATUS_INVALID_ARGUMENT)
                                 : request_scope->terminal_status;
}

SlDiagCode sl_app_request_scope_terminal_diag_code(const SlAppRequestScope* request_scope)
{
    return request_scope == NULL ? SL_DIAG_NONE : request_scope->terminal_diag_code;
}

SlCancellationReason sl_app_request_scope_terminal_reason(const SlAppRequestScope* request_scope)
{
    return request_scope == NULL ? SL_CANCELLATION_REASON_NONE : request_scope->terminal_reason;
}

uint64_t sl_app_request_scope_app_id(const SlAppRequestScope* request_scope)
{
    return request_scope == NULL ? 0U : request_scope->app_id;
}

uint64_t sl_app_request_scope_request_id(const SlAppRequestScope* request_scope)
{
    return request_scope == NULL ? 0U : request_scope->request_id;
}

SlStatus sl_app_request_scope_execute(SlScopeCleanup* storage, size_t cleanup_capacity,
                                      SlAppRequestScopeHandler handler, void* user,
                                      SlDiag* out_diag)
{
    SlAppRequestScope request_scope;
    SlStatus handler_status;
    SlStatus close_status;

    if (handler == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    handler_status = sl_app_request_scope_init(&request_scope, storage, cleanup_capacity);
    if (!sl_status_is_ok(handler_status)) {
        return handler_status;
    }

    handler_status = handler(&request_scope, user, out_diag);
    close_status = sl_app_request_scope_complete(
        &request_scope, sl_app_request_outcome_from_status(handler_status), handler_status,
        sl_app_request_diag_from_status(handler_status), out_diag);
    if (!sl_status_is_ok(close_status)) {
        return close_status;
    }

    return handler_status;
}

SlStatus sl_app_request_scope_execute_for_app(SlAppLifecycle* lifecycle, uint64_t request_id,
                                              SlScopeCleanup* storage, size_t cleanup_capacity,
                                              SlAppRequestScopeHandler handler, void* user,
                                              SlDiag* out_diag)
{
    SlAppRequestScope request_scope;
    SlStatus handler_status;
    SlStatus close_status;

    if (handler == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    handler_status = sl_app_request_scope_init_for_app(&request_scope, lifecycle, request_id,
                                                       storage, cleanup_capacity, out_diag);
    if (!sl_status_is_ok(handler_status)) {
        return handler_status;
    }

    handler_status = handler(&request_scope, user, out_diag);
    close_status = sl_app_request_scope_complete(
        &request_scope, sl_app_request_outcome_from_status(handler_status), handler_status,
        sl_app_request_diag_from_status(handler_status), out_diag);
    if (!sl_status_is_ok(close_status)) {
        return close_status;
    }

    return handler_status;
}

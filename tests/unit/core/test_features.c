#include "sloppy/features.h"

typedef int (*FeatureTestFn)(void);

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static SlPlan base_plan(SlPlanHandler* handlers, SlPlanRoute* routes)
{
    SlPlan plan = {0};

    handlers[0].id = 1U;
    handlers[0].export_name = sl_str_from_cstr("__sloppy_handler_1");
    handlers[0].display_name = sl_str_from_cstr("Home");

    routes[0].method = sl_str_from_cstr("GET");
    routes[0].pattern = sl_str_from_cstr("/");
    routes[0].handler_id = 1U;
    routes[0].name = sl_str_from_cstr("Home");

    plan.version = SL_PLAN_CURRENT_VERSION;
    plan.target.platform = sl_str_from_cstr(SL_PLAN_TARGET_PLATFORM_WINDOWS_X64);
    plan.target.engine = sl_str_from_cstr(SL_PLAN_TARGET_ENGINE_V8);
    plan.handlers = handlers;
    plan.handler_count = 1U;
    plan.routes = routes;
    plan.route_count = 1U;
    return plan;
}

static SlRuntimeFeatureAvailability all_available(void)
{
    SlRuntimeFeatureAvailability availability = {0};

    availability.v8 = true;
    availability.http = true;
    availability.transport_libuv = true;
    availability.provider_sqlite = true;
    availability.provider_postgres = false;
    availability.provider_sqlserver = false;
    return availability;
}

static int test_minimal_route_activates_expected_features(void)
{
    unsigned char diag_storage[2048];
    SlArena diag_arena = {0};
    SlPlanHandler handlers[1];
    SlPlanRoute routes[1];
    SlPlan plan = base_plan(handlers, routes);
    SlRuntimeFeatureAvailability availability = all_available();
    SlRuntimeFeatureSet set = {0};
    SlDiag diag = {0};

    (void)sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));

    if (expect_status(
            sl_runtime_feature_activate_plan(&plan, &availability, &diag_arena, &set, &diag),
            SL_STATUS_OK) != 0)
    {
        return 1;
    }
    if (!sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_CORE) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_V8) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_HTTP) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_TRANSPORT_LIBUV) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_APP) ||
        !sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_RESULTS) ||
        sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_PROVIDER_SQLITE))
    {
        return 2;
    }
    if (set.activation_count != 6U || set.activations[0].id != SL_RUNTIME_FEATURE_CORE ||
        set.activations[1].id != SL_RUNTIME_FEATURE_V8 ||
        set.activations[2].id != SL_RUNTIME_FEATURE_STDLIB_APP ||
        set.activations[3].id != SL_RUNTIME_FEATURE_STDLIB_RESULTS ||
        set.activations[4].id != SL_RUNTIME_FEATURE_TRANSPORT_LIBUV ||
        set.activations[5].id != SL_RUNTIME_FEATURE_HTTP)
    {
        return 3;
    }
    if (diag.code != SL_DIAG_NONE) {
        return 4;
    }
    return 0;
}

static int test_sqlite_provider_metadata_activates_sqlite(void)
{
    unsigned char diag_storage[2048];
    SlArena diag_arena = {0};
    SlPlanHandler handlers[1];
    SlPlanRoute routes[1];
    SlPlanDataProvider providers[1] = {{0}};
    SlPlan plan = base_plan(handlers, routes);
    SlRuntimeFeatureAvailability availability = all_available();
    SlRuntimeFeatureSet set = {0};
    SlDiag diag = {0};

    providers[0].token = sl_str_from_cstr("data.main");
    providers[0].provider = sl_str_from_cstr("sqlite");
    plan.data_providers = providers;
    plan.data_provider_count = 1U;
    (void)sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));

    if (expect_status(
            sl_runtime_feature_activate_plan(&plan, &availability, &diag_arena, &set, &diag),
            SL_STATUS_OK) != 0)
    {
        return 10;
    }
    if (!sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_PROVIDER_SQLITE) ||
        sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_PROVIDER_POSTGRES))
    {
        return 11;
    }
    if (set.activations[6].id != SL_RUNTIME_FEATURE_PROVIDER_SQLITE ||
        set.activations[6].reason != SL_RUNTIME_FEATURE_REASON_PLAN_PROVIDER ||
        !sl_str_equal(set.activations[6].requested_by, sl_str_from_cstr("data.main")))
    {
        return 12;
    }
    return 0;
}

static int test_unavailable_postgres_required_feature_fails(void)
{
    unsigned char diag_storage[2048];
    SlArena diag_arena = {0};
    SlPlanHandler handlers[1];
    SlPlanRoute routes[1];
    SlPlanRequiredFeature required[1] = {{sl_str_from_cstr("provider.postgres")}};
    SlPlan plan = base_plan(handlers, routes);
    SlRuntimeFeatureAvailability availability = all_available();
    SlRuntimeFeatureSet set = {0};
    SlDiag diag = {0};

    plan.required_features = required;
    plan.required_feature_count = 1U;
    (void)sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));

    if (expect_status(
            sl_runtime_feature_activate_plan(&plan, &availability, &diag_arena, &set, &diag),
            SL_STATUS_UNSUPPORTED) != 0)
    {
        return 20;
    }
    if (diag.code != SL_DIAG_UNAVAILABLE_RUNTIME_FEATURE ||
        !sl_str_equal(diag.related[0].message, sl_str_from_cstr("provider.postgres")))
    {
        return 21;
    }
    return 0;
}

static int test_unknown_required_feature_fails_deterministically(void)
{
    unsigned char diag_storage[2048];
    SlArena diag_arena = {0};
    SlPlanHandler handlers[1];
    SlPlanRoute routes[1];
    SlPlanRequiredFeature required[1] = {{sl_str_from_cstr("future.magic")}};
    SlPlan plan = base_plan(handlers, routes);
    SlRuntimeFeatureAvailability availability = all_available();
    SlRuntimeFeatureSet set = {0};
    SlDiag diag = {0};

    plan.required_features = required;
    plan.required_feature_count = 1U;
    (void)sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));

    if (expect_status(
            sl_runtime_feature_activate_plan(&plan, &availability, &diag_arena, &set, &diag),
            SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 30;
    }
    if (diag.code != SL_DIAG_UNKNOWN_RUNTIME_FEATURE ||
        !sl_str_equal(diag.message, sl_str_from_cstr("unknown runtime feature")) ||
        !sl_str_equal(diag.related[0].message, sl_str_from_cstr("future.magic")))
    {
        return 31;
    }
    return 0;
}

static int test_missing_dependency_fails_deterministically(void)
{
    unsigned char diag_storage[2048];
    SlArena diag_arena = {0};
    SlPlanHandler handlers[1];
    SlPlanRoute routes[1];
    SlPlan plan = base_plan(handlers, routes);
    SlRuntimeFeatureAvailability availability = all_available();
    SlRuntimeFeatureSet set = {0};
    SlDiag diag = {0};

    availability.transport_libuv = false;
    (void)sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));

    if (expect_status(
            sl_runtime_feature_activate_plan(&plan, &availability, &diag_arena, &set, &diag),
            SL_STATUS_UNSUPPORTED) != 0)
    {
        return 40;
    }
    if (diag.code != SL_DIAG_RUNTIME_FEATURE_DEPENDENCY_MISSING ||
        !sl_str_equal(diag.related[0].message, sl_str_from_cstr("transport.libuv")) ||
        !sl_str_equal(diag.related[1].message, sl_str_from_cstr("http")))
    {
        return 41;
    }
    return 0;
}

static int test_v8_disabled_fails_honestly(void)
{
    unsigned char diag_storage[2048];
    SlArena diag_arena = {0};
    SlPlanHandler handlers[1];
    SlPlanRoute routes[1];
    SlPlan plan = base_plan(handlers, routes);
    SlRuntimeFeatureAvailability availability = all_available();
    SlRuntimeFeatureSet set = {0};
    SlDiag diag = {0};

    availability.v8 = false;
    (void)sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));

    if (expect_status(
            sl_runtime_feature_activate_plan(&plan, &availability, &diag_arena, &set, &diag),
            SL_STATUS_UNSUPPORTED) != 0)
    {
        return 50;
    }
    if (diag.code != SL_DIAG_UNAVAILABLE_RUNTIME_FEATURE ||
        !sl_str_equal(diag.related[0].message, sl_str_from_cstr("v8")))
    {
        return 51;
    }
    return 0;
}

int main(void)
{
    static const FeatureTestFn tests[] = {
        test_minimal_route_activates_expected_features,
        test_sqlite_provider_metadata_activates_sqlite,
        test_unavailable_postgres_required_feature_fails,
        test_unknown_required_feature_fails_deterministically,
        test_missing_dependency_fails_deterministically,
        test_v8_disabled_fails_honestly,
    };
    size_t index = 0U;

    for (index = 0U; index < sizeof(tests) / sizeof(tests[0]); index += 1U) {
        if (tests[index]() != 0) {
            return (int)index + 1;
        }
    }
    return 0;
}

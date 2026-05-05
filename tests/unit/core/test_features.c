#include "sloppy/features.h"

#include <stdio.h>

typedef int (*FeatureTestFn)(void);

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static int expect_str_equal(SlStr actual, SlStr expected)
{
    return expect_true(sl_str_equal(actual, expected));
}

static int expect_snapshot(SlStr actual, const char* path)
{
    char expected[2048];
    FILE* file = NULL;
    size_t length = 0U;

#ifdef _MSC_VER
    if (fopen_s(&file, path, "rb") != 0) {
        return 1;
    }
#else
    file = fopen(path, "rb");
#endif

    if (file == NULL) {
        return 1;
    }

    length = fread(expected, 1U, sizeof(expected), file);
    if (ferror(file) != 0) {
        (void)fclose(file);
        return 2;
    }
    if (length == sizeof(expected)) {
        unsigned char extra = 0U;
        const size_t extra_read = fread(&extra, 1U, 1U, file);
        if (extra_read == 1U) {
            (void)fclose(file);
            return 4;
        }
        if (ferror(file) != 0) {
            (void)fclose(file);
            return 2;
        }
    }
    if (fclose(file) != 0) {
        return 3;
    }

    return expect_str_equal(actual, sl_str_from_parts(expected, length));
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

static SlPlan target_only_plan(void)
{
    SlPlan plan = {0};

    plan.version = SL_PLAN_CURRENT_VERSION;
    plan.target.platform = sl_str_from_cstr(SL_PLAN_TARGET_PLATFORM_WINDOWS_X64);
    plan.target.engine = sl_str_from_cstr(SL_PLAN_TARGET_ENGINE_V8);
    return plan;
}

static int expect_activation_diagnostic_snapshot(const SlPlan* plan,
                                                 const SlRuntimeFeatureAvailability* availability,
                                                 SlStatusCode expected_status,
                                                 SlDiagCode expected_diag_code,
                                                 const char* snapshot_path)
{
    unsigned char diag_storage[4096];
    unsigned char render_storage[4096];
    SlArena diag_arena = {0};
    SlArena render_arena = {0};
    SlRuntimeFeatureSet set = {0};
    SlDiag diag = {0};
    SlStr rendered = {0};

    if (expect_status(sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&render_arena, render_storage, sizeof(render_storage)),
                      SL_STATUS_OK) != 0)
    {
        return 1;
    }
    if (expect_status(
            sl_runtime_feature_activate_plan(plan, availability, &diag_arena, &set, &diag),
            expected_status) != 0)
    {
        return 2;
    }
    if (diag.code != expected_diag_code) {
        return 3;
    }
    if (expect_status(sl_diag_render_json(&render_arena, &diag, &rendered), SL_STATUS_OK) != 0) {
        return 4;
    }
    if (expect_snapshot(rendered, snapshot_path) != 0) {
        return 5;
    }

    return 0;
}

static int test_descriptors_publish_import_and_intrinsic_metadata(void)
{
    const SlRuntimeFeatureDescriptor* sqlite =
        sl_runtime_feature_descriptor(SL_RUNTIME_FEATURE_PROVIDER_SQLITE);
    const SlRuntimeFeatureDescriptor* postgres =
        sl_runtime_feature_descriptor(SL_RUNTIME_FEATURE_PROVIDER_POSTGRES);
    const SlRuntimeFeatureDescriptor* data =
        sl_runtime_feature_descriptor(SL_RUNTIME_FEATURE_STDLIB_DATA);
    const SlRuntimeFeatureDescriptor* config =
        sl_runtime_feature_descriptor(SL_RUNTIME_FEATURE_STDLIB_CONFIG);

    if (SL_RUNTIME_FEATURE_COUNT != 12) {
        return 60;
    }
    if (sqlite == NULL || postgres == NULL || data == NULL || config == NULL) {
        return 61;
    }
    if (!sl_str_equal(sqlite->stdlib_import, sl_str_from_cstr("sloppy/providers/sqlite")) ||
        !sl_str_equal(sqlite->v8_intrinsic_namespace, sl_str_from_cstr("__sloppy.data.sqlite")) ||
        !sqlite->requires_v8_intrinsics)
    {
        return 62;
    }
    if (!sl_str_equal(data->stable_id, sl_str_from_cstr("stdlib.data")) ||
        !sl_str_equal(data->stdlib_import, sl_str_from_cstr("sloppy/data")) ||
        !sl_str_is_empty(data->v8_intrinsic_namespace) || data->requires_v8_intrinsics)
    {
        return 63;
    }
    if (!sl_str_equal(config->stable_id, sl_str_from_cstr("stdlib.config")) ||
        !sl_str_equal(config->stdlib_import, sl_str_from_cstr("sloppy/config")) ||
        !sl_str_is_empty(config->v8_intrinsic_namespace))
    {
        return 64;
    }
    if (postgres->available || postgres->requires_v8_intrinsics ||
        !sl_str_equal(postgres->stdlib_import, sl_str_from_cstr("sloppy/providers/postgres")))
    {
        return 65;
    }
    return 0;
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
    if (!sl_runtime_feature_set_contains(&set, SL_RUNTIME_FEATURE_STDLIB_DATA)) {
        return 12;
    }
    if (set.activation_count != 8U || set.activations[6].id != SL_RUNTIME_FEATURE_STDLIB_DATA ||
        set.activations[6].reason != SL_RUNTIME_FEATURE_REASON_DEPENDENCY ||
        !sl_str_equal(set.activations[6].requested_by, sl_str_from_cstr("provider.sqlite")) ||
        set.activations[7].id != SL_RUNTIME_FEATURE_PROVIDER_SQLITE ||
        set.activations[7].reason != SL_RUNTIME_FEATURE_REASON_PLAN_PROVIDER ||
        !sl_str_equal(set.activations[7].requested_by, sl_str_from_cstr("data.main")))
    {
        return 13;
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

static int test_missing_feature_diagnostic_goldens(void)
{
    SlPlanHandler handlers[1];
    SlPlanRoute routes[1];
    SlPlanDataProvider providers[1] = {{0}};
    SlPlanRequiredFeature required[1] = {{0}};
    SlRuntimeFeatureAvailability availability = all_available();
    SlPlan plan = base_plan(handlers, routes);

    required[0].id = sl_str_from_cstr("future.magic");
    plan.required_features = required;
    plan.required_feature_count = 1U;
    if (expect_activation_diagnostic_snapshot(
            &plan, &availability, SL_STATUS_INVALID_ARGUMENT, SL_DIAG_UNKNOWN_RUNTIME_FEATURE,
            "tests/golden/diagnostics/runtime_feature_unknown.json") != 0)
    {
        return 70;
    }

    required[0].id = sl_str_from_cstr("provider.postgres");
    if (expect_activation_diagnostic_snapshot(
            &plan, &availability, SL_STATUS_UNSUPPORTED, SL_DIAG_UNAVAILABLE_RUNTIME_FEATURE,
            "tests/golden/diagnostics/runtime_feature_unavailable_postgres.json") != 0)
    {
        return 71;
    }

    required[0].id = sl_str_from_cstr("provider.sqlserver");
    if (expect_activation_diagnostic_snapshot(
            &plan, &availability, SL_STATUS_UNSUPPORTED, SL_DIAG_UNAVAILABLE_RUNTIME_FEATURE,
            "tests/golden/diagnostics/runtime_feature_unavailable_sqlserver.json") != 0)
    {
        return 72;
    }

    plan.required_features = NULL;
    plan.required_feature_count = 0U;
    providers[0].token = sl_str_from_cstr("data.main");
    providers[0].provider = sl_str_from_cstr("sqlite");
    plan.data_providers = providers;
    plan.data_provider_count = 1U;
    availability.provider_sqlite = false;
    if (expect_activation_diagnostic_snapshot(
            &plan, &availability, SL_STATUS_UNSUPPORTED, SL_DIAG_UNAVAILABLE_RUNTIME_FEATURE,
            "tests/golden/diagnostics/runtime_feature_unavailable_sqlite.json") != 0)
    {
        return 73;
    }

    availability = all_available();
    availability.v8 = false;
    plan.data_providers = NULL;
    plan.data_provider_count = 0U;
    if (expect_activation_diagnostic_snapshot(
            &plan, &availability, SL_STATUS_UNSUPPORTED, SL_DIAG_UNAVAILABLE_RUNTIME_FEATURE,
            "tests/golden/diagnostics/runtime_feature_v8_disabled.json") != 0)
    {
        return 74;
    }

    availability = all_available();
    availability.transport_libuv = false;
    if (expect_activation_diagnostic_snapshot(
            &plan, &availability, SL_STATUS_UNSUPPORTED, SL_DIAG_RUNTIME_FEATURE_DEPENDENCY_MISSING,
            "tests/golden/diagnostics/runtime_feature_missing_transport_dependency.json") != 0)
    {
        return 75;
    }

    availability = all_available();
    availability.transport_libuv = false;
    plan = target_only_plan();
    required[0].id = sl_str_from_cstr("transport.libuv");
    plan.required_features = required;
    plan.required_feature_count = 1U;
    if (expect_activation_diagnostic_snapshot(
            &plan, &availability, SL_STATUS_UNSUPPORTED, SL_DIAG_UNAVAILABLE_RUNTIME_FEATURE,
            "tests/golden/diagnostics/runtime_feature_unavailable_transport.json") != 0)
    {
        return 76;
    }

    return 0;
}

int main(void)
{
    static const FeatureTestFn tests[] = {
        test_descriptors_publish_import_and_intrinsic_metadata,
        test_minimal_route_activates_expected_features,
        test_sqlite_provider_metadata_activates_sqlite,
        test_unavailable_postgres_required_feature_fails,
        test_unknown_required_feature_fails_deterministically,
        test_missing_dependency_fails_deterministically,
        test_v8_disabled_fails_honestly,
        test_missing_feature_diagnostic_goldens,
    };
    size_t index = 0U;

    for (index = 0U; index < sizeof(tests) / sizeof(tests[0]); index += 1U) {
        if (tests[index]() != 0) {
            return (int)index + 1;
        }
    }
    return 0;
}

#include "sloppy/app_host.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct CleanupRecord
{
    int order[4];
    size_t count;
} CleanupRecord;

typedef struct RequestHandlerData
{
    CleanupRecord* record;
    int first_cleanup;
    int second_cleanup;
    SlStatusCode status_code;
    uint64_t expected_app_id;
    uint64_t expected_request_id;
    bool saw_active_scope;
    bool saw_expected_identity;
} RequestHandlerData;

typedef struct ResourceScopeData
{
    SlAppResourceCleanup resource;
    SlStatusCode status_code;
    bool saw_active_scope;
} ResourceScopeData;

typedef struct LifecycleCleanupRecord
{
    int values[4];
    size_t count;
} LifecycleCleanupRecord;

typedef struct LifecycleCleanupPayload
{
    LifecycleCleanupRecord* record;
    int value;
} LifecycleCleanupPayload;

typedef int (*AppHostTestFn)(void);

static SlPlanDataProvider empty_data_provider(void)
{
    SlPlanDataProvider provider;

    provider.token = sl_str_empty();
    provider.provider = sl_str_empty();
    provider.capability = sl_str_empty();
    provider.service = sl_str_empty();
    provider.database = sl_str_empty();
    return provider;
}

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static int expect_str_equal(SlStr left, SlStr right)
{
    return expect_true(sl_str_equal(left, right));
}

static void record_cleanup(void* payload, void* user)
{
    CleanupRecord* record = (CleanupRecord*)user;
    int* value = (int*)payload;
    size_t index = 0U;

    if (record == NULL || value == NULL || record->count >= 4U) {
        return;
    }

    index = record->count;
    record->order[index] = *value;
    record->count += 1U;
}

static void lifecycle_resource_cleanup(void* ptr, void* user)
{
    LifecycleCleanupPayload* payload = (LifecycleCleanupPayload*)ptr;
    LifecycleCleanupRecord* record = payload == NULL ? NULL : payload->record;
    size_t index = 0U;

    (void)user;
    if (record == NULL || record->count >= 4U) {
        return;
    }

    index = record->count;
    record->values[index] = payload->value;
    record->count += 1U;
}

static SlPlan valid_plan(SlPlanHandler* handlers, SlPlanRoute* routes)
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
    plan.runtime_min_version = sl_str_from_cstr(SL_PLAN_RUNTIME_MIN_VERSION_0_1_0);
    plan.target.platform = sl_str_from_cstr(SL_PLAN_TARGET_PLATFORM_WINDOWS_X64);
    plan.target.engine = sl_str_from_cstr(SL_PLAN_TARGET_ENGINE_V8);
    plan.handlers = handlers;
    plan.handler_count = 1U;
    plan.routes = routes;
    plan.route_count = 1U;
    return plan;
}

static SlAppHostStartupValidation validation_options(SlArena* arena)
{
    SlAppHostStartupValidation options = {0};

    options.diag_arena = arena;
    options.require_runnable_route = true;
    options.max_runnable_routes = 128U;
    options.validate_runtime_features = true;
    options.override_runtime_feature_availability = true;
    options.runtime_feature_availability.v8 = true;
    options.runtime_feature_availability.http = true;
    options.runtime_feature_availability.transport_libuv = true;
    options.runtime_feature_availability.provider_sqlite = true;
    return options;
}

static int test_valid_startup_succeeds(void)
{
    unsigned char diag_storage[1024];
    SlArena diag_arena = {0};
    SlPlanHandler handlers[1];
    SlPlanRoute routes[1];
    SlPlan plan = valid_plan(handlers, routes);
    SlDiag diag = {0};
    SlAppHostStartupValidation options;

    if (expect_status(sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage)),
                      SL_STATUS_OK) != 0)
    {
        return 1;
    }
    options = validation_options(&diag_arena);

    if (expect_status(sl_app_host_validate_startup(&plan, &options, &diag), SL_STATUS_OK) != 0) {
        return 2;
    }
    if (diag.code != SL_DIAG_NONE) {
        return 3;
    }

    return 0;
}

static int test_non_get_metadata_is_valid_when_get_route_exists(void)
{
    unsigned char diag_storage[1024];
    SlArena diag_arena = {0};
    SlPlanHandler handlers[2];
    SlPlanRoute routes[2];
    SlPlan plan = valid_plan(handlers, routes);
    SlDiag diag = {0};
    SlAppHostStartupValidation options;

    if (expect_status(sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage)),
                      SL_STATUS_OK) != 0)
    {
        return 4;
    }
    options = validation_options(&diag_arena);
    handlers[1].id = 2U;
    handlers[1].export_name = sl_str_from_cstr("__sloppy_handler_2");
    handlers[1].display_name = sl_str_from_cstr("Create");
    routes[1].method = sl_str_from_cstr("POST");
    routes[1].pattern = sl_str_from_cstr("/");
    routes[1].handler_id = 2U;
    routes[1].name = sl_str_from_cstr("Create");
    plan.handler_count = 2U;
    plan.route_count = 2U;

    if (expect_status(sl_app_host_validate_startup(&plan, &options, &diag), SL_STATUS_OK) != 0) {
        return 4;
    }
    if (diag.code != SL_DIAG_NONE) {
        return 5;
    }

    return 0;
}

static int test_non_get_only_metadata_is_runnable(void)
{
    unsigned char diag_storage[1024];
    SlArena diag_arena = {0};
    SlPlanHandler handlers[1];
    SlPlanRoute routes[1];
    SlPlan plan = valid_plan(handlers, routes);
    SlDiag diag = {0};
    SlAppHostStartupValidation options;

    if (expect_status(sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage)),
                      SL_STATUS_OK) != 0)
    {
        return 6;
    }
    options = validation_options(&diag_arena);
    routes[0].method = sl_str_from_cstr("POST");

    if (expect_status(sl_app_host_validate_startup(&plan, &options, &diag), SL_STATUS_OK) != 0) {
        return 6;
    }
    if (diag.code != SL_DIAG_NONE) {
        return 7;
    }

    options.require_runnable_route = false;
    diag = (SlDiag){0};
    if (expect_status(sl_app_host_validate_startup(&plan, &options, &diag), SL_STATUS_OK) != 0) {
        return 8;
    }
    if (diag.code != SL_DIAG_NONE) {
        return 9;
    }

    return 0;
}

static int test_missing_route_handler_fails_startup(void)
{
    unsigned char diag_storage[1024];
    SlArena diag_arena = {0};
    SlPlanHandler handlers[1];
    SlPlanRoute routes[1];
    SlPlan plan = valid_plan(handlers, routes);
    SlDiag diag = {0};
    SlAppHostStartupValidation options;

    if (expect_status(sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage)),
                      SL_STATUS_OK) != 0)
    {
        return 10;
    }
    options = validation_options(&diag_arena);
    routes[0].handler_id = 2U;

    if (expect_status(sl_app_host_validate_startup(&plan, &options, &diag),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 10;
    }
    if (diag.code != SL_DIAG_INVALID_PLAN_FIELD ||
        !sl_str_equal(diag.message, sl_str_from_cstr("app plan route references missing handler")))
    {
        return 11;
    }

    return 0;
}

static int test_duplicate_route_policy_fails_startup(void)
{
    unsigned char diag_storage[1024];
    SlArena diag_arena = {0};
    SlPlanHandler handlers[1];
    SlPlanRoute routes[2];
    SlPlan plan = valid_plan(handlers, routes);
    SlDiag diag = {0};
    SlAppHostStartupValidation options;

    if (expect_status(sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage)),
                      SL_STATUS_OK) != 0)
    {
        return 20;
    }
    options = validation_options(&diag_arena);
    routes[1] = routes[0];
    routes[1].name = sl_str_from_cstr("Home.Duplicate");
    plan.route_count = 2U;

    if (expect_status(sl_app_host_validate_startup(&plan, &options, &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        return 20;
    }
    if (!sl_str_equal(diag.message, sl_str_from_cstr("duplicate app plan route"))) {
        return 21;
    }

    return 0;
}

static int test_duplicate_service_token_fails_startup(void)
{
    unsigned char diag_storage[1024];
    SlArena diag_arena = {0};
    SlPlanHandler handlers[1];
    SlPlanRoute routes[1];
    SlPlanDataProvider providers[2] = {empty_data_provider(), empty_data_provider()};
    SlPlan plan = valid_plan(handlers, routes);
    SlDiag diag = {0};
    SlAppHostStartupValidation options;

    providers[0] = (SlPlanDataProvider){0};
    providers[1] = (SlPlanDataProvider){0};
    if (expect_status(sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage)),
                      SL_STATUS_OK) != 0)
    {
        return 30;
    }
    options = validation_options(&diag_arena);

    providers[0].token = sl_str_from_cstr("data.main");
    providers[0].provider = sl_str_from_cstr("sqlite");
    providers[0].service = sl_str_from_cstr("services.db");
    providers[1].token = sl_str_from_cstr("data.audit");
    providers[1].provider = sl_str_from_cstr("postgres");
    providers[1].service = sl_str_from_cstr("services.db");
    plan.data_providers = providers;
    plan.data_provider_count = 2U;

    if (expect_status(sl_app_host_validate_startup(&plan, &options, &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        return 30;
    }
    if (!sl_str_equal(diag.message, sl_str_from_cstr("duplicate app plan service token"))) {
        return 31;
    }

    return 0;
}

static int test_startup_reports_plan_driven_runtime_features(void)
{
    unsigned char diag_storage[2048];
    SlArena diag_arena = {0};
    SlPlanHandler handlers[1];
    SlPlanRoute routes[1];
    SlPlanDataProvider providers[1] = {empty_data_provider()};
    SlPlan plan = valid_plan(handlers, routes);
    SlDiag diag = {0};
    SlRuntimeFeatureSet features = {0};
    SlAppHostStartupValidation options;

    providers[0] = (SlPlanDataProvider){0};
    if (expect_status(sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage)),
                      SL_STATUS_OK) != 0)
    {
        return 35;
    }
    options = validation_options(&diag_arena);
    options.out_runtime_features = &features;
    options.override_runtime_feature_availability = true;
    options.runtime_feature_availability.v8 = true;
    options.runtime_feature_availability.http = true;
    options.runtime_feature_availability.transport_libuv = true;
    options.runtime_feature_availability.provider_sqlite = true;

    providers[0].token = sl_str_from_cstr("data.main");
    providers[0].provider = sl_str_from_cstr("sqlite");
    plan.data_providers = providers;
    plan.data_provider_count = 1U;

    if (expect_status(sl_app_host_validate_startup(&plan, &options, &diag), SL_STATUS_OK) != 0) {
        return 35;
    }
    if (!sl_runtime_feature_set_contains(&features, SL_RUNTIME_FEATURE_PROVIDER_SQLITE) ||
        !sl_runtime_feature_set_contains(&features, SL_RUNTIME_FEATURE_STDLIB_DATA) ||
        features.activation_count != 8U || diag.code != SL_DIAG_NONE)
    {
        return 36;
    }

    return 0;
}

static int test_startup_fails_when_plan_requires_unavailable_feature(void)
{
    unsigned char diag_storage[2048];
    SlArena diag_arena = {0};
    SlPlanHandler handlers[1];
    SlPlanRoute routes[1];
    SlPlanRequiredFeature required[1] = {{sl_str_from_cstr("provider.postgres")}};
    SlPlan plan = valid_plan(handlers, routes);
    SlDiag diag = {0};
    SlRuntimeFeatureSet features = {0};
    SlAppHostStartupValidation options;

    if (expect_status(sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage)),
                      SL_STATUS_OK) != 0)
    {
        return 37;
    }
    options = validation_options(&diag_arena);
    options.out_runtime_features = &features;
    options.override_runtime_feature_availability = true;
    options.runtime_feature_availability.v8 = true;
    options.runtime_feature_availability.http = true;
    options.runtime_feature_availability.transport_libuv = true;
    options.runtime_feature_availability.provider_sqlite = true;
    options.runtime_feature_availability.provider_postgres = false;
    plan.required_features = required;
    plan.required_feature_count = 1U;

    if (expect_status(sl_app_host_validate_startup(&plan, &options, &diag),
                      SL_STATUS_UNSUPPORTED) != 0)
    {
        return 37;
    }
    if (diag.code != SL_DIAG_UNAVAILABLE_RUNTIME_FEATURE ||
        !sl_str_equal(diag.related[0].message, sl_str_from_cstr("provider.postgres")) ||
        features.activation_count != 0U)
    {
        return 38;
    }

    return 0;
}

static SlStatus request_handler_success(SlAppRequestScope* scope, void* user, SlDiag* out_diag)
{
    RequestHandlerData* data = (RequestHandlerData*)user;

    (void)out_diag;
    if (data == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    data->saw_active_scope = sl_app_request_scope_is_active(scope);
    data->saw_expected_identity =
        data->expected_app_id == sl_app_request_scope_app_id(scope) &&
        data->expected_request_id == sl_app_request_scope_request_id(scope);
    if (!sl_status_is_ok(sl_app_request_scope_add_cleanup(scope, record_cleanup,
                                                          &data->first_cleanup, data->record)) ||
        !sl_status_is_ok(sl_app_request_scope_add_cleanup(scope, record_cleanup,
                                                          &data->second_cleanup, data->record)))
    {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    return sl_status_ok();
}

static SlStatus request_handler_failure(SlAppRequestScope* scope, void* user, SlDiag* out_diag)
{
    RequestHandlerData* data = (RequestHandlerData*)user;

    (void)out_diag;
    if (data == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    data->saw_active_scope = sl_app_request_scope_is_active(scope);
    data->saw_expected_identity =
        data->expected_app_id == sl_app_request_scope_app_id(scope) &&
        data->expected_request_id == sl_app_request_scope_request_id(scope);
    if (!sl_status_is_ok(sl_app_request_scope_add_cleanup(scope, record_cleanup,
                                                          &data->first_cleanup, data->record)) ||
        !sl_status_is_ok(sl_app_request_scope_add_cleanup(scope, record_cleanup,
                                                          &data->second_cleanup, data->record)))
    {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    return sl_status_from_code(data->status_code);
}

static SlStatus request_handler_resource_cleanup(SlAppRequestScope* scope, void* user,
                                                 SlDiag* out_diag)
{
    ResourceScopeData* data = (ResourceScopeData*)user;

    (void)out_diag;
    if (data == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    data->saw_active_scope = sl_app_request_scope_is_active(scope);
    if (!sl_status_is_ok(sl_app_request_scope_add_resource_cleanup(scope, &data->resource))) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    return sl_status_from_code(data->status_code);
}

static SlStatus request_handler_completes_scope_success(SlAppRequestScope* scope, void* user,
                                                        SlDiag* out_diag)
{
    RequestHandlerData* data = (RequestHandlerData*)user;

    if (data == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    data->saw_active_scope = sl_app_request_scope_is_active(scope);
    if (!sl_status_is_ok(sl_app_request_scope_add_cleanup(scope, record_cleanup,
                                                          &data->first_cleanup, data->record)))
    {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    if (!sl_status_is_ok(sl_app_request_scope_complete(scope, SL_APP_REQUEST_OUTCOME_SUCCESS,
                                                       sl_status_from_code(data->status_code),
                                                       SL_DIAG_NONE, out_diag)))
    {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    return sl_status_from_code(data->status_code);
}

static SlStatus request_handler_completes_scope(SlAppRequestScope* scope, void* user,
                                                SlDiag* out_diag)
{
    RequestHandlerData* data = (RequestHandlerData*)user;
    SlStatus status;

    if (data == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    data->saw_active_scope = sl_app_request_scope_is_active(scope);
    if (!sl_status_is_ok(sl_app_request_scope_add_cleanup(scope, record_cleanup,
                                                          &data->first_cleanup, data->record)))
    {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    status = sl_status_from_code(data->status_code);
    if (!sl_status_is_ok(sl_app_request_scope_complete(
            scope,
            sl_status_is_ok(status) ? SL_APP_REQUEST_OUTCOME_SUCCESS
                                    : SL_APP_REQUEST_OUTCOME_SYNC_ERROR,
            status, sl_status_is_ok(status) ? SL_DIAG_NONE : SL_DIAG_APP_LIFECYCLE, out_diag)))
    {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    return status;
}

static int test_request_scope_cleans_up_on_success(void)
{
    SlScopeCleanup storage[4];
    CleanupRecord record = {{0}, 0U};
    RequestHandlerData data = {&record, 1, 2, SL_STATUS_OK, 0U, 0U, false, false};

    if (expect_status(
            sl_app_request_scope_execute(storage, 4U, request_handler_success, &data, NULL),
            SL_STATUS_OK) != 0)
    {
        return 40;
    }
    if (!data.saw_active_scope || !data.saw_expected_identity || record.count != 2U ||
        record.order[0] != 2 || record.order[1] != 1)
    {
        return 41;
    }

    return 0;
}

static int test_request_scope_execute_accepts_handler_terminalization(void)
{
    SlScopeCleanup storage[2];
    CleanupRecord record = {{0}, 0U};
    RequestHandlerData data = {&record, 7, 0, SL_STATUS_OK, 0U, 0U, false, false};

    if (expect_status(sl_app_request_scope_execute(
                          storage, 2U, request_handler_completes_scope_success, &data, NULL),
                      SL_STATUS_OK) != 0)
    {
        return 44;
    }
    if (!data.saw_active_scope || record.count != 1U || record.order[0] != 7) {
        return 45;
    }

    return 0;
}

static int test_request_scope_execute_accepts_handler_complete(void)
{
    SlScopeCleanup storage[2];
    CleanupRecord record = {{0}, 0U};
    RequestHandlerData data = {&record, 8, 0, SL_STATUS_CANCELLED, 0U, 0U, false, false};

    if (expect_status(
            sl_app_request_scope_execute(storage, 2U, request_handler_completes_scope, &data, NULL),
            SL_STATUS_CANCELLED) != 0)
    {
        return 46;
    }
    if (!data.saw_active_scope || record.count != 1U || record.order[0] != 8) {
        return 47;
    }

    return 0;
}

static int test_request_scope_cleans_up_on_failure(void)
{
    SlScopeCleanup storage[4];
    CleanupRecord record = {{0}, 0U};
    RequestHandlerData data = {&record, 1, 2, SL_STATUS_INVALID_STATE, 0U, 0U, false, false};

    if (expect_status(
            sl_app_request_scope_execute(storage, 4U, request_handler_failure, &data, NULL),
            SL_STATUS_INVALID_STATE) != 0)
    {
        return 50;
    }
    if (!data.saw_active_scope || !data.saw_expected_identity || record.count != 2U ||
        record.order[0] != 2 || record.order[1] != 1)
    {
        return 51;
    }

    return 0;
}

static int run_request_resource_cleanup_case(SlStatusCode status_code)
{
    SlScopeCleanup storage[2];
    SlResourceEntry entries[1];
    SlResourceTable table = {0};
    LifecycleCleanupRecord record = {{0}, 0U};
    LifecycleCleanupPayload payload = {&record, 10};
    ResourceScopeData data = {0};
    SlResourceId id = sl_resource_id_invalid();

    if (expect_status(sl_resource_table_init(&table, entries, 1U), SL_STATUS_OK) != 0) {
        return 1;
    }
    if (expect_status(sl_resource_table_insert(&table, SL_RESOURCE_KIND_TEST_RESOURCE, &payload,
                                               lifecycle_resource_cleanup, NULL, &id, NULL),
                      SL_STATUS_OK) != 0)
    {
        return 2;
    }

    data.resource.table = &table;
    data.resource.id = id;
    data.status_code = status_code;

    if (expect_status(sl_app_request_scope_execute(storage, 2U, request_handler_resource_cleanup,
                                                   &data, NULL),
                      status_code) != 0)
    {
        return 3;
    }
    if (!data.saw_active_scope || record.count != 1U || record.values[0] != 10 ||
        sl_resource_table_live_count(&table) != 0U || sl_resource_id_is_valid(data.resource.id))
    {
        return 4;
    }

    return 0;
}

static int test_request_scope_closes_resources_for_lifecycle_outcomes(void)
{
    if (run_request_resource_cleanup_case(SL_STATUS_OK) != 0) {
        return 60;
    }
    if (run_request_resource_cleanup_case(SL_STATUS_INVALID_STATE) != 0) {
        return 61;
    }
    if (run_request_resource_cleanup_case(SL_STATUS_CANCELLED) != 0) {
        return 62;
    }
    if (run_request_resource_cleanup_case(SL_STATUS_DEADLINE_EXCEEDED) != 0) {
        return 63;
    }
    if (run_request_resource_cleanup_case(SL_STATUS_UNSUPPORTED) != 0) {
        return 64;
    }
    if (run_request_resource_cleanup_case(SL_STATUS_OUT_OF_RANGE) != 0) {
        return 65;
    }

    return 0;
}

static int test_app_lifecycle_shutdown_closes_resources_once(void)
{
    SlScopeCleanup storage[3];
    SlAppLifecycle lifecycle = {0};
    SlResourceEntry entries[2];
    SlResourceTable table = {0};
    LifecycleCleanupRecord record = {{0}, 0U};
    LifecycleCleanupPayload first_payload = {&record, 1};
    LifecycleCleanupPayload second_payload = {&record, 2};
    SlAppResourceCleanup first_cleanup = {0};
    SlAppResourceCleanup second_cleanup = {0};
    SlDiag diag = {0};
    SlResourceId first = sl_resource_id_invalid();
    SlResourceId second = sl_resource_id_invalid();

    if (expect_status(sl_app_lifecycle_shutdown(&lifecycle, &diag), SL_STATUS_OK) != 0 ||
        !sl_app_lifecycle_is_shutdown(&lifecycle) ||
        sl_app_lifecycle_state(&lifecycle) != SL_APP_LIFECYCLE_STATE_STOPPED ||
        diag.code != SL_DIAG_NONE)
    {
        return 70;
    }

    lifecycle = (SlAppLifecycle){0};
    if (expect_status(sl_app_lifecycle_start(&lifecycle, storage, 3U, &diag), SL_STATUS_OK) != 0 ||
        !sl_app_lifecycle_is_started(&lifecycle) ||
        sl_app_lifecycle_state(&lifecycle) != SL_APP_LIFECYCLE_STATE_RUNNING ||
        diag.code != SL_DIAG_NONE)
    {
        return 71;
    }
    if (expect_status(sl_resource_table_init(&table, entries, 2U), SL_STATUS_OK) != 0 ||
        expect_status(sl_resource_table_insert(&table, SL_RESOURCE_KIND_TEST_RESOURCE,
                                               &first_payload, lifecycle_resource_cleanup, NULL,
                                               &first, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_resource_table_insert(&table, SL_RESOURCE_KIND_TEST_RESOURCE,
                                               &second_payload, lifecycle_resource_cleanup, NULL,
                                               &second, NULL),
                      SL_STATUS_OK) != 0)
    {
        return 72;
    }

    first_cleanup.table = &table;
    first_cleanup.id = first;
    second_cleanup.table = &table;
    second_cleanup.id = second;
    if (expect_status(sl_app_lifecycle_add_resource_cleanup(&lifecycle, &first_cleanup, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_app_lifecycle_add_resource_cleanup(&lifecycle, &second_cleanup, &diag),
                      SL_STATUS_OK) != 0)
    {
        return 73;
    }

    if (expect_status(sl_app_lifecycle_shutdown(&lifecycle, &diag), SL_STATUS_OK) != 0 ||
        !sl_app_lifecycle_is_shutdown(&lifecycle) || sl_resource_table_live_count(&table) != 0U ||
        record.count != 2U || record.values[0] != 2 || record.values[1] != 1 ||
        sl_resource_id_is_valid(first_cleanup.id) || sl_resource_id_is_valid(second_cleanup.id))
    {
        return 74;
    }

    if (expect_status(sl_app_lifecycle_shutdown(&lifecycle, &diag), SL_STATUS_OK) != 0 ||
        record.count != 2U || diag.code != SL_DIAG_NONE)
    {
        return 75;
    }

    return 0;
}

static int test_app_lifecycle_preserves_resource_id_on_close_failure(void)
{
    SlScopeCleanup storage[1];
    SlAppLifecycle lifecycle = {0};
    SlResourceTable table = {0};
    SlAppResourceCleanup cleanup = {0};
    SlDiag diag = {0};

    if (expect_status(sl_app_lifecycle_start(&lifecycle, storage, 1U, &diag), SL_STATUS_OK) != 0) {
        return 76;
    }

    cleanup.table = &table;
    cleanup.id.slot = 1U;
    cleanup.id.generation = 1U;
    if (expect_status(sl_app_lifecycle_add_resource_cleanup(&lifecycle, &cleanup, &diag),
                      SL_STATUS_OK) != 0)
    {
        return 77;
    }

    if (expect_status(sl_app_lifecycle_shutdown(&lifecycle, &diag), SL_STATUS_OK) != 0) {
        return 78;
    }
    if (!sl_resource_id_is_valid(cleanup.id)) {
        return 79;
    }

    return 0;
}

static int test_app_lifecycle_rejects_double_start(void)
{
    SlScopeCleanup storage[1];
    SlAppLifecycle lifecycle = {0};
    SlDiag diag = {0};

    if (expect_status(sl_app_lifecycle_start_with_id(&lifecycle, storage, 1U, 42U, &diag),
                      SL_STATUS_OK) != 0 ||
        sl_app_lifecycle_app_id(&lifecycle) != 42U)
    {
        return 86;
    }

    if (expect_status(sl_app_lifecycle_start(&lifecycle, storage, 1U, &diag),
                      SL_STATUS_INVALID_STATE) != 0 ||
        diag.code != SL_DIAG_LIFECYCLE_ALREADY_STARTED ||
        !sl_str_equal(diag.message, sl_str_from_cstr("app lifecycle is already started")))
    {
        return 87;
    }

    if (expect_status(sl_app_lifecycle_force_shutdown(&lifecycle, &diag), SL_STATUS_OK) != 0) {
        return 88;
    }

    return 0;
}

static int test_startup_failure_cleanup_marks_failed(void)
{
    SlScopeCleanup storage[2];
    SlAppLifecycle lifecycle = {0};
    LifecycleCleanupRecord record = {{0}, 0U};
    LifecycleCleanupPayload payload = {&record, 9};
    SlDiag diag = {0};

    if (expect_status(sl_app_lifecycle_start(&lifecycle, storage, 2U, &diag), SL_STATUS_OK) != 0 ||
        expect_status(sl_app_lifecycle_add_cleanup(&lifecycle, lifecycle_resource_cleanup, &payload,
                                                   NULL, &diag),
                      SL_STATUS_OK) != 0)
    {
        return 89;
    }

    if (expect_status(sl_app_lifecycle_fail_startup(&lifecycle, &diag), SL_STATUS_OK) != 0 ||
        sl_app_lifecycle_state(&lifecycle) != SL_APP_LIFECYCLE_STATE_FAILED || record.count != 1U ||
        record.values[0] != 9)
    {
        return 90;
    }

    if (expect_status(sl_app_lifecycle_fail_startup(&lifecycle, &diag), SL_STATUS_OK) != 0 ||
        record.count != 1U)
    {
        return 91;
    }

    return 0;
}

static int test_graceful_shutdown_drains_active_request_scope(void)
{
    SlScopeCleanup app_storage[2];
    SlScopeCleanup request_storage[2];
    SlAppLifecycle lifecycle = {0};
    SlAppRequestScope request_scope = {0};
    SlDiag diag = {0};

    if (expect_status(sl_app_lifecycle_start_with_id(&lifecycle, app_storage, 2U, 7U, &diag),
                      SL_STATUS_OK) != 0)
    {
        return 92;
    }
    if (expect_status(sl_app_request_scope_init_for_app(&request_scope, &lifecycle, 99U,
                                                        request_storage, 2U, &diag),
                      SL_STATUS_OK) != 0 ||
        sl_app_lifecycle_active_request_count(&lifecycle) != 1U ||
        sl_app_request_scope_app_id(&request_scope) != 7U ||
        sl_app_request_scope_request_id(&request_scope) != 99U)
    {
        return 93;
    }
    if (expect_status(sl_app_lifecycle_begin_shutdown(&lifecycle, &diag), SL_STATUS_OK) != 0 ||
        sl_app_lifecycle_state(&lifecycle) != SL_APP_LIFECYCLE_STATE_DRAINING)
    {
        return 94;
    }
    if (expect_status(sl_app_request_scope_init_for_app(&request_scope, &lifecycle, 100U,
                                                        request_storage, 2U, &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        return 95;
    }
    if (expect_status(sl_app_lifecycle_finish_shutdown(&lifecycle, &diag),
                      SL_STATUS_INVALID_STATE) != 0 ||
        !sl_str_equal(diag.message, sl_str_from_cstr("app lifecycle is draining active requests")))
    {
        return 96;
    }
    if (expect_status(sl_app_request_scope_complete(&request_scope, SL_APP_REQUEST_OUTCOME_SUCCESS,
                                                    sl_status_ok(), SL_DIAG_NONE, &diag),
                      SL_STATUS_OK) != 0 ||
        sl_app_lifecycle_active_request_count(&lifecycle) != 0U)
    {
        return 97;
    }
    if (expect_status(sl_app_lifecycle_finish_shutdown(&lifecycle, &diag), SL_STATUS_OK) != 0 ||
        !sl_app_lifecycle_is_shutdown(&lifecycle))
    {
        return 98;
    }

    return 0;
}

static int test_forced_shutdown_closes_app_scope_with_active_request(void)
{
    SlScopeCleanup app_storage[2];
    SlScopeCleanup request_storage[2];
    SlAppLifecycle lifecycle = {0};
    SlAppRequestScope request_scope = {0};
    LifecycleCleanupRecord record = {{0}, 0U};
    LifecycleCleanupPayload payload = {&record, 11};
    SlDiag diag = {0};

    if (expect_status(sl_app_lifecycle_start(&lifecycle, app_storage, 2U, &diag), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_app_lifecycle_add_cleanup(&lifecycle, lifecycle_resource_cleanup, &payload,
                                                   NULL, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_app_request_scope_init_for_app(&request_scope, &lifecycle, 1U,
                                                        request_storage, 2U, &diag),
                      SL_STATUS_OK) != 0)
    {
        return 99;
    }

    if (expect_status(sl_app_lifecycle_force_shutdown(&lifecycle, &diag), SL_STATUS_OK) != 0 ||
        !sl_app_lifecycle_is_shutdown(&lifecycle) ||
        sl_app_lifecycle_active_request_count(&lifecycle) != 0U || record.count != 1U)
    {
        return 100;
    }
    if (expect_status(sl_app_lifecycle_assert_no_leaks(&lifecycle, &diag),
                      SL_STATUS_INVALID_STATE) != 0 ||
        diag.code != SL_DIAG_LIFECYCLE_LEAK_DETECTED ||
        sl_app_lifecycle_snapshot(&lifecycle).active_request_scopes != 1U)
    {
        return 102;
    }
    if (expect_status(sl_app_request_scope_close(&request_scope, &diag), SL_STATUS_OK) != 0 ||
        record.count != 1U)
    {
        return 101;
    }
    if (expect_status(sl_app_lifecycle_assert_no_leaks(&lifecycle, &diag), SL_STATUS_OK) != 0 ||
        sl_app_lifecycle_snapshot(&lifecycle).active_request_scopes != 0U)
    {
        return 103;
    }

    return 0;
}

static int test_app_scope_resource_survives_request_scope(void)
{
    SlScopeCleanup app_storage[2];
    SlScopeCleanup request_storage[2];
    SlAppLifecycle lifecycle = {0};
    SlAppRequestScope request_scope = {0};
    SlResourceEntry entries[1];
    SlResourceTable table = {0};
    LifecycleCleanupRecord record = {{0}, 0U};
    LifecycleCleanupPayload payload = {&record, 12};
    SlAppResourceCleanup app_cleanup = {0};
    SlResourceId id = sl_resource_id_invalid();
    SlDiag diag = {0};

    if (expect_status(sl_app_lifecycle_start(&lifecycle, app_storage, 2U, &diag), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_resource_table_init(&table, entries, 1U), SL_STATUS_OK) != 0 ||
        expect_status(sl_resource_table_insert(&table, SL_RESOURCE_KIND_TEST_RESOURCE, &payload,
                                               lifecycle_resource_cleanup, NULL, &id, NULL),
                      SL_STATUS_OK) != 0)
    {
        return 102;
    }
    app_cleanup.table = &table;
    app_cleanup.id = id;
    if (expect_status(sl_app_lifecycle_add_resource_cleanup(&lifecycle, &app_cleanup, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_app_request_scope_init_for_app(&request_scope, &lifecycle, 1U,
                                                        request_storage, 2U, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_app_request_scope_complete(&request_scope, SL_APP_REQUEST_OUTCOME_SUCCESS,
                                                    sl_status_ok(), SL_DIAG_NONE, &diag),
                      SL_STATUS_OK) != 0)
    {
        return 103;
    }
    if (sl_resource_table_live_count(&table) != 1U || record.count != 0U) {
        return 104;
    }
    if (expect_status(sl_app_lifecycle_shutdown(&lifecycle, &diag), SL_STATUS_OK) != 0 ||
        sl_resource_table_live_count(&table) != 0U || record.count != 1U ||
        sl_resource_id_is_valid(app_cleanup.id))
    {
        return 105;
    }

    return 0;
}

static int test_request_scope_rejects_access_after_close(void)
{
    SlScopeCleanup storage[1];
    SlAppRequestScope request_scope = {0};
    int cleanup_value = 1;
    SlDiag diag = {0};

    if (expect_status(sl_app_request_scope_init(&request_scope, storage, 1U), SL_STATUS_OK) != 0 ||
        expect_status(sl_app_request_scope_complete(&request_scope, SL_APP_REQUEST_OUTCOME_SUCCESS,
                                                    sl_status_ok(), SL_DIAG_NONE, &diag),
                      SL_STATUS_OK) != 0)
    {
        return 106;
    }
    if (expect_status(
            sl_app_request_scope_add_cleanup(&request_scope, record_cleanup, &cleanup_value, NULL),
            SL_STATUS_INVALID_STATE) != 0)
    {
        return 107;
    }

    return 0;
}

static int test_request_scope_close_requires_terminal_outcome(void)
{
    SlScopeCleanup storage[1];
    SlAppRequestScope request_scope = {0};
    SlDiag diag = {0};

    if (expect_status(sl_app_request_scope_init(&request_scope, storage, 1U), SL_STATUS_OK) != 0) {
        return 170;
    }
    if (expect_status(sl_app_request_scope_close(&request_scope, &diag), SL_STATUS_INVALID_STATE) !=
            0 ||
        diag.code != SL_DIAG_LIFECYCLE_REQUEST_SCOPE_CLOSED ||
        !sl_str_equal(diag.message,
                      sl_str_from_cstr("request scope terminal outcome was not recorded")))
    {
        return 171;
    }
    if (!sl_app_request_scope_is_active(&request_scope)) {
        return 172;
    }
    if (expect_status(sl_app_request_scope_complete(&request_scope, SL_APP_REQUEST_OUTCOME_SUCCESS,
                                                    sl_status_ok(), SL_DIAG_NONE, &diag),
                      SL_STATUS_OK) != 0)
    {
        return 173;
    }

    return 0;
}

static int test_request_owned_resource_closes_before_app_resource(void)
{
    SlScopeCleanup app_storage[2];
    SlScopeCleanup request_storage[2];
    SlResourceEntry entries[2];
    SlResourceTable table = {0};
    SlAppLifecycle lifecycle = {0};
    SlAppResourceCleanup app_cleanup = {0};
    SlAppResourceCleanup request_cleanup = {0};
    LifecycleCleanupRecord record = {{0}, 0U};
    LifecycleCleanupPayload app_payload = {&record, 20};
    LifecycleCleanupPayload request_payload = {&record, 30};
    SlResourceId app_id = sl_resource_id_invalid();
    SlResourceId request_id = sl_resource_id_invalid();
    ResourceScopeData data = {0};
    SlDiag diag = {0};

    if (expect_status(sl_app_lifecycle_start(&lifecycle, app_storage, 2U, &diag), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_resource_table_init(&table, entries, 2U), SL_STATUS_OK) != 0 ||
        expect_status(sl_resource_table_insert(&table, SL_RESOURCE_KIND_TEST_RESOURCE, &app_payload,
                                               lifecycle_resource_cleanup, NULL, &app_id, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_resource_table_insert(&table, SL_RESOURCE_KIND_TEST_RESOURCE,
                                               &request_payload, lifecycle_resource_cleanup, NULL,
                                               &request_id, NULL),
                      SL_STATUS_OK) != 0)
    {
        return 108;
    }

    app_cleanup.table = &table;
    app_cleanup.id = app_id;
    request_cleanup.table = &table;
    request_cleanup.id = request_id;
    data.resource = request_cleanup;
    data.status_code = SL_STATUS_OK;
    if (expect_status(sl_app_lifecycle_add_resource_cleanup(&lifecycle, &app_cleanup, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_app_request_scope_execute_for_app(&lifecycle, 1U, request_storage, 2U,
                                                           request_handler_resource_cleanup, &data,
                                                           &diag),
                      SL_STATUS_OK) != 0)
    {
        return 109;
    }
    if (record.count != 1U || record.values[0] != 30 ||
        sl_resource_table_live_count(&table) != 1U || sl_resource_id_is_valid(data.resource.id))
    {
        return 110;
    }
    if (expect_status(sl_app_lifecycle_shutdown(&lifecycle, &diag), SL_STATUS_OK) != 0 ||
        record.count != 2U || record.values[1] != 20)
    {
        return 111;
    }

    return 0;
}

static int run_request_terminal_outcome_case(SlAppRequestOutcome outcome, SlStatusCode status_code,
                                             SlDiagCode diag_code,
                                             SlCancellationReason expected_reason)
{
    SlScopeCleanup app_storage[1];
    SlScopeCleanup request_storage[2];
    SlAppLifecycle lifecycle = {0};
    SlAppRequestScope request_scope = {0};
    CleanupRecord record = {{0}, 0U};
    int cleanup_value = 44;
    SlDiag diag = {0};
    SlStatus status = sl_status_from_code(status_code);

    if (expect_status(sl_app_lifecycle_start(&lifecycle, app_storage, 1U, &diag), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_app_request_scope_init_for_app(&request_scope, &lifecycle, 1U,
                                                        request_storage, 2U, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_app_request_scope_add_cleanup(&request_scope, record_cleanup,
                                                       &cleanup_value, &record),
                      SL_STATUS_OK) != 0)
    {
        return 1;
    }

    if (expect_status(
            sl_app_request_scope_complete(&request_scope, outcome, status, diag_code, &diag),
            SL_STATUS_OK) != 0 ||
        !sl_app_request_scope_is_terminal(&request_scope) ||
        sl_app_request_scope_is_active(&request_scope) ||
        sl_status_code(sl_app_request_scope_terminal_status(&request_scope)) != status_code ||
        sl_app_request_scope_terminal_diag_code(&request_scope) != diag_code ||
        sl_app_request_scope_terminal_reason(&request_scope) != expected_reason ||
        sl_app_lifecycle_active_request_count(&lifecycle) != 0U || record.count != 1U ||
        record.order[0] != 44)
    {
        return 2;
    }

    if (expect_status(sl_app_request_scope_complete(&request_scope,
                                                    SL_APP_REQUEST_OUTCOME_PROVIDER_LATE_COMPLETION,
                                                    sl_status_ok(), SL_DIAG_NONE, &diag),
                      SL_STATUS_STALE_RESOURCE) != 0 ||
        record.count != 1U || diag.code != SL_DIAG_LIFECYCLE_LATE_COMPLETION_DROPPED ||
        sl_status_code(sl_app_request_scope_terminal_status(&request_scope)) != status_code ||
        sl_app_request_scope_terminal_diag_code(&request_scope) != diag_code ||
        sl_app_request_scope_terminal_reason(&request_scope) != expected_reason ||
        sl_app_request_scope_snapshot(&request_scope).late_completion_count != 1U ||
        sl_app_lifecycle_snapshot(&lifecycle).late_completion_count != 1U ||
        !sl_str_equal(diag.message, sl_str_from_cstr("request provider late completion dropped")))
    {
        return 3;
    }

    if (expect_status(sl_app_lifecycle_shutdown(&lifecycle, &diag), SL_STATUS_OK) != 0) {
        return 4;
    }

    return 0;
}

static int test_request_terminal_outcomes_cleanup_once(void)
{
    typedef struct TerminalCase
    {
        SlAppRequestOutcome outcome;
        SlStatusCode status_code;
        SlDiagCode diag_code;
        SlCancellationReason reason;
    } TerminalCase;
    static const TerminalCase cases[] = {
        {SL_APP_REQUEST_OUTCOME_SUCCESS, SL_STATUS_OK, SL_DIAG_NONE, SL_CANCELLATION_REASON_NONE},
        {SL_APP_REQUEST_OUTCOME_SYNC_ERROR, SL_STATUS_INTERNAL, SL_DIAG_INTERNAL_ERROR,
         SL_CANCELLATION_REASON_NONE},
        {SL_APP_REQUEST_OUTCOME_V8_EXCEPTION, SL_STATUS_INTERNAL, SL_DIAG_ENGINE_EXCEPTION,
         SL_CANCELLATION_REASON_NONE},
        {SL_APP_REQUEST_OUTCOME_PROMISE_REJECTION, SL_STATUS_INTERNAL,
         SL_DIAG_ENGINE_PROMISE_REJECTION, SL_CANCELLATION_REASON_NONE},
        {SL_APP_REQUEST_OUTCOME_VALIDATION_FAILURE, SL_STATUS_INVALID_ARGUMENT,
         SL_DIAG_INVALID_ARGUMENT, SL_CANCELLATION_REASON_NONE},
        {SL_APP_REQUEST_OUTCOME_BODY_PARSE_FAILURE, SL_STATUS_INVALID_ARGUMENT,
         SL_DIAG_MALFORMED_JSON, SL_CANCELLATION_REASON_NONE},
        {SL_APP_REQUEST_OUTCOME_TIMEOUT, SL_STATUS_DEADLINE_EXCEEDED, SL_DIAG_HTTP_REQUEST_TIMEOUT,
         SL_CANCELLATION_REASON_DEADLINE_EXCEEDED},
        {SL_APP_REQUEST_OUTCOME_CANCELLED, SL_STATUS_CANCELLED, SL_DIAG_ENGINE_CANCELLED,
         SL_CANCELLATION_REASON_CANCELLED},
        {SL_APP_REQUEST_OUTCOME_CLIENT_DISCONNECT, SL_STATUS_CANCELLED,
         SL_DIAG_HTTP_CONNECTION_CLOSED, SL_CANCELLATION_REASON_CANCELLED},
        {SL_APP_REQUEST_OUTCOME_RESPONSE_WRITE_FAILURE, SL_STATUS_INTERNAL,
         SL_DIAG_HTTP_WRITE_FAILED, SL_CANCELLATION_REASON_NONE},
        {SL_APP_REQUEST_OUTCOME_PROVIDER_FAILURE, SL_STATUS_INTERNAL, SL_DIAG_SQLITE_PROVIDER_ERROR,
         SL_CANCELLATION_REASON_NONE},
        {SL_APP_REQUEST_OUTCOME_PROVIDER_CANCELLED_BEFORE_START, SL_STATUS_CANCELLED,
         SL_DIAG_ENGINE_CANCELLED, SL_CANCELLATION_REASON_CANCELLED},
        {SL_APP_REQUEST_OUTCOME_SHUTDOWN, SL_STATUS_CANCELLED, SL_DIAG_APP_LIFECYCLE,
         SL_CANCELLATION_REASON_SHUTDOWN},
        {SL_APP_REQUEST_OUTCOME_BACKPRESSURE, SL_STATUS_CAPACITY_EXCEEDED,
         SL_DIAG_ENGINE_BACKPRESSURE, SL_CANCELLATION_REASON_BACKPRESSURE},
    };
    size_t index = 0U;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index += 1U) {
        int result =
            run_request_terminal_outcome_case(cases[index].outcome, cases[index].status_code,
                                              cases[index].diag_code, cases[index].reason);
        if (result != 0) {
            return 120 + result;
        }
    }

    return 0;
}

static int test_typed_resource_cleanup_preserves_wrong_kind_resource(void)
{
    SlScopeCleanup storage[2];
    SlResourceEntry entries[1];
    SlResourceTable table = {0};
    LifecycleCleanupRecord record = {{0}, 0U};
    LifecycleCleanupPayload payload = {&record, 88};
    SlAppTypedResourceCleanup cleanup = {0};
    SlAppRequestScope request_scope = {0};
    SlResourceId id = sl_resource_id_invalid();
    SlDiag diag = {0};

    if (expect_status(sl_resource_table_init(&table, entries, 1U), SL_STATUS_OK) != 0 ||
        expect_status(sl_resource_table_insert(&table, SL_RESOURCE_KIND_TEST_RESOURCE, &payload,
                                               lifecycle_resource_cleanup, NULL, &id, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_app_request_scope_init(&request_scope, storage, 2U), SL_STATUS_OK) != 0)
    {
        return 116;
    }

    cleanup.resource.table = &table;
    cleanup.resource.id = id;
    cleanup.kind = SL_RESOURCE_KIND_SQLITE_CONNECTION;
    if (expect_status(sl_app_request_scope_add_typed_resource_cleanup(&request_scope, &cleanup),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_app_request_scope_complete(&request_scope, SL_APP_REQUEST_OUTCOME_SUCCESS,
                                                    sl_status_ok(), SL_DIAG_NONE, &diag),
                      SL_STATUS_OK) != 0)
    {
        return 117;
    }
    if (record.count != 0U || sl_resource_table_live_count(&table) != 1U ||
        !sl_resource_id_is_valid(cleanup.resource.id))
    {
        return 118;
    }

    if (expect_status(
            sl_resource_table_close_kind(&table, id, SL_RESOURCE_KIND_TEST_RESOURCE, &diag),
            SL_STATUS_OK) != 0 ||
        record.count != 1U)
    {
        return 119;
    }

    return 0;
}

static int test_lifecycle_leak_hooks_snapshot_zero_after_cleanup(void)
{
    SlScopeCleanup app_storage[2];
    SlScopeCleanup request_storage[2];
    SlAppLifecycle lifecycle = {0};
    SlAppRequestScope request_scope = {0};
    CleanupRecord record = {{0}, 0U};
    int app_cleanup_value = 70;
    int request_cleanup_value = 71;
    SlDiag diag = {0};
    SlAppLifecycleSnapshot app_snapshot = {0};
    SlAppRequestScopeSnapshot request_snapshot = {0};

    if (expect_status(sl_app_lifecycle_assert_no_leaks(NULL, &diag), SL_STATUS_INVALID_ARGUMENT) !=
            0 ||
        diag.code != SL_DIAG_INVALID_ARGUMENT ||
        expect_status(sl_app_request_scope_assert_no_leaks(NULL, &diag),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_INVALID_ARGUMENT)
    {
        return 129;
    }

    if (expect_status(sl_app_lifecycle_start_with_id(&lifecycle, app_storage, 2U, 700U, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_app_lifecycle_add_cleanup(&lifecycle, record_cleanup, &app_cleanup_value,
                                                   &record, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_app_request_scope_init_for_app(&request_scope, &lifecycle, 701U,
                                                        request_storage, 2U, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_app_request_scope_add_cleanup(&request_scope, record_cleanup,
                                                       &request_cleanup_value, &record),
                      SL_STATUS_OK) != 0)
    {
        return 130;
    }

    app_snapshot = sl_app_lifecycle_snapshot(&lifecycle);
    request_snapshot = sl_app_request_scope_snapshot(&request_scope);
    if (app_snapshot.app_id != 700U || app_snapshot.active_app_scopes != 1U ||
        app_snapshot.active_request_scopes != 1U || app_snapshot.app_cleanup_count != 1U ||
        request_snapshot.app_id != 700U || request_snapshot.request_id != 701U ||
        !request_snapshot.active || request_snapshot.request_cleanup_count != 1U)
    {
        return 131;
    }
    if (expect_status(sl_app_request_scope_assert_no_leaks(&request_scope, &diag),
                      SL_STATUS_INVALID_STATE) != 0 ||
        diag.code != SL_DIAG_LIFECYCLE_LEAK_DETECTED)
    {
        return 132;
    }

    if (expect_status(sl_app_request_scope_complete(&request_scope,
                                                    SL_APP_REQUEST_OUTCOME_VALIDATION_FAILURE,
                                                    sl_status_from_code(SL_STATUS_INVALID_ARGUMENT),
                                                    SL_DIAG_INVALID_ARGUMENT, &diag),
                      SL_STATUS_OK) != 0)
    {
        return 133;
    }

    request_snapshot = sl_app_request_scope_snapshot(&request_scope);
    app_snapshot = sl_app_lifecycle_snapshot(&lifecycle);
    if (request_snapshot.active || !request_snapshot.terminal ||
        request_snapshot.request_cleanup_count != 0U ||
        request_snapshot.leaked_resource_count != 0U || app_snapshot.active_request_scopes != 0U ||
        record.count != 1U || record.order[0] != 71)
    {
        return 134;
    }
    if (expect_status(sl_app_request_scope_assert_no_leaks(&request_scope, &diag), SL_STATUS_OK) !=
        0)
    {
        return 135;
    }
    if (expect_status(sl_app_lifecycle_assert_no_leaks(&lifecycle, &diag),
                      SL_STATUS_INVALID_STATE) != 0 ||
        diag.code != SL_DIAG_LIFECYCLE_LEAK_DETECTED)
    {
        return 136;
    }

    if (expect_status(sl_app_lifecycle_shutdown(&lifecycle, &diag), SL_STATUS_OK) != 0) {
        return 137;
    }

    app_snapshot = sl_app_lifecycle_snapshot(&lifecycle);
    if (app_snapshot.active_app_scopes != 0U || app_snapshot.app_cleanup_count != 0U ||
        app_snapshot.leaked_resource_count != 0U || record.count != 2U || record.order[1] != 70)
    {
        return 138;
    }
    if (expect_status(sl_app_lifecycle_assert_no_leaks(&lifecycle, &diag), SL_STATUS_OK) != 0) {
        return 139;
    }

    return 0;
}

static int test_app_lifecycle_diagnostic_json_is_stable(void)
{
    unsigned char json_storage[1024];
    SlArena json_arena = {0};
    SlAppLifecycle lifecycle = {0};
    SlDiag diag = {0};
    SlStr json = {0};
    SlStatus status;
    SlStatus render_status;
    SlScopeCleanup cleanup_storage[1];
    int cleanup_value = 1;

    if (expect_status(sl_arena_init(&json_arena, json_storage, sizeof(json_storage)),
                      SL_STATUS_OK) != 0)
    {
        return 80;
    }

    status = sl_app_lifecycle_add_cleanup(&lifecycle, record_cleanup, &cleanup_value, NULL, &diag);
    if (expect_status(status, SL_STATUS_INVALID_STATE) != 0 ||
        diag.code != SL_DIAG_LIFECYCLE_NOT_STARTED ||
        !sl_str_equal(diag.message, sl_str_from_cstr("app lifecycle is not active")))
    {
        return 81;
    }

    render_status = sl_diag_render_json(&json_arena, &diag, &json);
    if (expect_status(render_status, SL_STATUS_OK) != 0) {
        return 82;
    }
    if (expect_str_equal(json, sl_str_from_cstr("{\"code\":\"SLOPPY_E_LIFECYCLE_NOT_STARTED\","
                                                "\"severity\":\"error\","
                                                "\"message\":\"app lifecycle is not active\","
                                                "\"hints\":[\"start the app lifecycle before "
                                                "registering cleanup\"]}\n")) != 0)
    {
        return 83;
    }

    if (expect_status(sl_app_lifecycle_start(&lifecycle, cleanup_storage, 1U, &diag),
                      SL_STATUS_OK) != 0)
    {
        return 84;
    }
    if (expect_status(sl_app_lifecycle_shutdown(&lifecycle, &diag), SL_STATUS_OK) != 0) {
        return 85;
    }

    return 0;
}

static int run_app_host_tests(const AppHostTestFn* tests, size_t count)
{
    size_t index = 0U;

    for (index = 0U; index < count; index += 1U) {
        int result = tests[index]();
        if (result != 0) {
            return result;
        }
    }

    return 0;
}

int main(void)
{
    static const AppHostTestFn tests[] = {
        test_valid_startup_succeeds,
        test_non_get_metadata_is_valid_when_get_route_exists,
        test_non_get_only_metadata_is_runnable,
        test_missing_route_handler_fails_startup,
        test_duplicate_route_policy_fails_startup,
        test_duplicate_service_token_fails_startup,
        test_startup_reports_plan_driven_runtime_features,
        test_startup_fails_when_plan_requires_unavailable_feature,
        test_request_scope_cleans_up_on_success,
        test_request_scope_execute_accepts_handler_terminalization,
        test_request_scope_execute_accepts_handler_complete,
        test_request_scope_cleans_up_on_failure,
        test_request_scope_closes_resources_for_lifecycle_outcomes,
        test_app_lifecycle_shutdown_closes_resources_once,
        test_app_lifecycle_preserves_resource_id_on_close_failure,
        test_app_lifecycle_rejects_double_start,
        test_startup_failure_cleanup_marks_failed,
        test_graceful_shutdown_drains_active_request_scope,
        test_forced_shutdown_closes_app_scope_with_active_request,
        test_app_scope_resource_survives_request_scope,
        test_request_scope_rejects_access_after_close,
        test_request_scope_close_requires_terminal_outcome,
        test_request_owned_resource_closes_before_app_resource,
        test_request_terminal_outcomes_cleanup_once,
        test_typed_resource_cleanup_preserves_wrong_kind_resource,
        test_lifecycle_leak_hooks_snapshot_zero_after_cleanup,
        test_app_lifecycle_diagnostic_json_is_stable,
    };

    return run_app_host_tests(tests, sizeof(tests) / sizeof(tests[0]));
}

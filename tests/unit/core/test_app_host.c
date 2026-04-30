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
    bool saw_active_scope;
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

    (void)sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));
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

    (void)sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));
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

    (void)sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));
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

    (void)sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));
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
    SlPlanDataProvider providers[2];
    SlPlan plan = valid_plan(handlers, routes);
    SlDiag diag = {0};
    SlAppHostStartupValidation options;

    (void)sl_arena_init(&diag_arena, diag_storage, sizeof(diag_storage));
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

static SlStatus request_handler_success(SlAppRequestScope* scope, void* user, SlDiag* out_diag)
{
    RequestHandlerData* data = (RequestHandlerData*)user;

    (void)out_diag;
    if (data == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    data->saw_active_scope = sl_app_request_scope_is_active(scope);
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

static int test_request_scope_cleans_up_on_success(void)
{
    SlScopeCleanup storage[4];
    CleanupRecord record = {{0}, 0U};
    RequestHandlerData data = {&record, 1, 2, SL_STATUS_OK, false};

    if (expect_status(
            sl_app_request_scope_execute(storage, 4U, request_handler_success, &data, NULL),
            SL_STATUS_OK) != 0)
    {
        return 40;
    }
    if (!data.saw_active_scope || record.count != 2U || record.order[0] != 2 ||
        record.order[1] != 1)
    {
        return 41;
    }

    return 0;
}

static int test_request_scope_cleans_up_on_failure(void)
{
    SlScopeCleanup storage[4];
    CleanupRecord record = {{0}, 0U};
    RequestHandlerData data = {&record, 1, 2, SL_STATUS_INVALID_STATE, false};

    if (expect_status(
            sl_app_request_scope_execute(storage, 4U, request_handler_failure, &data, NULL),
            SL_STATUS_INVALID_STATE) != 0)
    {
        return 50;
    }
    if (!data.saw_active_scope || record.count != 2U || record.order[0] != 2 ||
        record.order[1] != 1)
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
        !sl_app_lifecycle_is_shutdown(&lifecycle) || diag.code != SL_DIAG_NONE)
    {
        return 70;
    }

    lifecycle = (SlAppLifecycle){0};
    if (expect_status(sl_app_lifecycle_start(&lifecycle, storage, 3U, &diag), SL_STATUS_OK) != 0 ||
        !sl_app_lifecycle_is_started(&lifecycle) || diag.code != SL_DIAG_NONE)
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
    if (expect_status(status, SL_STATUS_INVALID_STATE) != 0 || diag.code != SL_DIAG_APP_LIFECYCLE ||
        !sl_str_equal(diag.message, sl_str_from_cstr("app lifecycle is not active")))
    {
        return 81;
    }

    render_status = sl_diag_render_json(&json_arena, &diag, &json);
    if (expect_status(render_status, SL_STATUS_OK) != 0) {
        return 82;
    }
    if (expect_str_equal(json, sl_str_from_cstr("{\"code\":\"SLOPPY_E_APP_LIFECYCLE\","
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

int main(void)
{
    int result = test_valid_startup_succeeds();
    if (result != 0) {
        return result;
    }
    result = test_non_get_metadata_is_valid_when_get_route_exists();
    if (result != 0) {
        return result;
    }
    result = test_non_get_only_metadata_is_runnable();
    if (result != 0) {
        return result;
    }
    result = test_missing_route_handler_fails_startup();
    if (result != 0) {
        return result;
    }
    result = test_duplicate_route_policy_fails_startup();
    if (result != 0) {
        return result;
    }
    result = test_duplicate_service_token_fails_startup();
    if (result != 0) {
        return result;
    }
    result = test_request_scope_cleans_up_on_success();
    if (result != 0) {
        return result;
    }
    result = test_request_scope_cleans_up_on_failure();
    if (result != 0) {
        return result;
    }
    result = test_request_scope_closes_resources_for_lifecycle_outcomes();
    if (result != 0) {
        return result;
    }
    result = test_app_lifecycle_shutdown_closes_resources_once();
    if (result != 0) {
        return result;
    }
    result = test_app_lifecycle_preserves_resource_id_on_close_failure();
    if (result != 0) {
        return result;
    }
    return test_app_lifecycle_diagnostic_json_is_stable();
}

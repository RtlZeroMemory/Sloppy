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

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
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

int main(void)
{
    int result = test_valid_startup_succeeds();
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
    return test_request_scope_cleans_up_on_failure();
}

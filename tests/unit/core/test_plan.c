#include "sloppy/plan.h"

#include <stdbool.h>
#include <stdio.h>

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static int fixture_exists(const char* path)
{
    FILE* file = NULL;

#ifdef _MSC_VER
    if (fopen_s(&file, path, "rb") != 0) {
        return 0;
    }
#else
    file = fopen(path, "rb");
#endif
    if (file == NULL) {
        return 0;
    }

    if (fclose(file) != 0) {
        return 0;
    }

    return 1;
}

static int test_version_helpers(void)
{
    if (expect_true(SL_PLAN_CURRENT_VERSION == SL_PLAN_VERSION_1) != 0) {
        return 1;
    }

    if (expect_true(sl_plan_version_supported(SL_PLAN_VERSION_1)) != 0) {
        return 2;
    }

    if (expect_true(!sl_plan_version_supported(0U)) != 0 ||
        expect_true(!sl_plan_version_supported(2U)) != 0)
    {
        return 3;
    }

    return 0;
}

static int test_handler_id_helpers(void)
{
    if (expect_true(!sl_handler_id_valid(SL_HANDLER_ID_INVALID)) != 0) {
        return 10;
    }

    if (expect_true(sl_handler_id_valid(1U)) != 0) {
        return 11;
    }

    return 0;
}

static int test_route_method_helpers(void)
{
    if (expect_true(sl_plan_route_method_supported(sl_str_from_cstr("GET"))) != 0 ||
        expect_true(sl_plan_route_method_supported(sl_str_from_cstr("POST"))) != 0 ||
        expect_true(sl_plan_route_method_supported(sl_str_from_cstr("PUT"))) != 0 ||
        expect_true(sl_plan_route_method_supported(sl_str_from_cstr("PATCH"))) != 0 ||
        expect_true(sl_plan_route_method_supported(sl_str_from_cstr("DELETE"))) != 0)
    {
        return 15;
    }

    if (expect_true(!sl_plan_route_method_supported(sl_str_from_cstr("HEAD"))) != 0 ||
        expect_true(!sl_plan_route_method_supported(sl_str_from_cstr("OPTIONS"))) != 0)
    {
        return 16;
    }

    return 0;
}

static int test_handler_lookup(void)
{
    SlPlanHandler handlers[2] = {
        {1U, sl_str_from_cstr("__sloppy_handler_1"), sl_str_from_cstr("Home")},
        {2U, sl_str_from_cstr("__sloppy_handler_2"), sl_str_from_cstr("Health")}};
    SlPlan plan = {0};
    const SlPlanHandler* found = NULL;

    plan.version = SL_PLAN_VERSION_1;
    plan.handlers = handlers;
    plan.handler_count = 2U;

    if (expect_status(sl_plan_find_handler_by_id(&plan, 2U, &found), SL_STATUS_OK) != 0) {
        return 20;
    }

    if (found != &handlers[1] || !sl_str_equal(found->export_name, handlers[1].export_name)) {
        return 21;
    }

    if (expect_status(sl_plan_find_handler_by_id(&plan, 3U, &found), SL_STATUS_OUT_OF_RANGE) != 0 ||
        found != NULL)
    {
        return 22;
    }

    return 0;
}

static int test_lookup_invalid_arguments(void)
{
    SlPlan plan = {0};
    const SlPlanHandler* found = (const SlPlanHandler*)1;

    if (expect_status(sl_plan_find_handler_by_id(NULL, 1U, &found), SL_STATUS_INVALID_ARGUMENT) !=
            0 ||
        found != NULL)
    {
        return 30;
    }

    if (expect_status(sl_plan_find_handler_by_id(&plan, SL_HANDLER_ID_INVALID, &found),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        found != NULL)
    {
        return 31;
    }

    if (expect_status(sl_plan_find_handler_by_id(&plan, 1U, NULL), SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 32;
    }

    plan.handler_count = 1U;
    plan.handlers = NULL;
    if (expect_status(sl_plan_find_handler_by_id(&plan, 1U, &found), SL_STATUS_INVALID_ARGUMENT) !=
            0 ||
        found != NULL)
    {
        return 33;
    }

    return 0;
}

static int test_empty_handler_table(void)
{
    SlPlan plan = {0};
    const SlPlanHandler* found = NULL;

    if (expect_status(sl_plan_find_handler_by_id(&plan, 1U, &found), SL_STATUS_OUT_OF_RANGE) != 0 ||
        found != NULL)
    {
        return 40;
    }

    if (sl_plan_has_duplicate_handler_ids(&plan)) {
        return 41;
    }

    return 0;
}

static int test_duplicate_handler_ids(void)
{
    SlPlanHandler unique_handlers[2] = {
        {1U, sl_str_from_cstr("__sloppy_handler_1"), sl_str_from_cstr("Home")},
        {2U, sl_str_from_cstr("__sloppy_handler_2"), sl_str_from_cstr("Health")}};
    SlPlanHandler duplicate_handlers[3] = {
        {1U, sl_str_from_cstr("__sloppy_handler_1"), sl_str_from_cstr("Home")},
        {2U, sl_str_from_cstr("__sloppy_handler_2"), sl_str_from_cstr("Health")},
        {1U, sl_str_from_cstr("__sloppy_handler_1_duplicate"), sl_str_from_cstr("Home duplicate")}};
    SlPlan plan = {0};

    plan.handlers = unique_handlers;
    plan.handler_count = 2U;
    if (sl_plan_has_duplicate_handler_ids(&plan)) {
        return 50;
    }

    plan.handlers = duplicate_handlers;
    plan.handler_count = 3U;
    if (!sl_plan_has_duplicate_handler_ids(&plan)) {
        return 51;
    }

    if (sl_plan_has_duplicate_handler_ids(NULL)) {
        return 52;
    }

    plan.handlers = NULL;
    plan.handler_count = 2U;
    if (sl_plan_has_duplicate_handler_ids(&plan)) {
        return 53;
    }

    return 0;
}

static int test_fixture_files_exist(void)
{
    static const char* fixtures[] = {"tests/golden/plan/README.md",
                                     "tests/golden/plan/valid-minimal.plan.json",
                                     "tests/golden/plan/valid-multiple-handlers.plan.json",
                                     "tests/golden/plan/unknown-future-field.plan.json",
                                     "tests/golden/plan/malformed-json.plan.json",
                                     "tests/golden/plan/invalid-version.plan.json",
                                     "tests/golden/plan/missing-runtime-minimum-version.plan.json",
                                     "tests/golden/plan/missing-bundle.plan.json",
                                     "tests/golden/plan/missing-bundle-path.plan.json",
                                     "tests/golden/plan/missing-source-map.plan.json",
                                     "tests/golden/plan/missing-handlers.plan.json",
                                     "tests/golden/plan/empty-handlers.plan.json",
                                     "tests/golden/plan/invalid-handler-id.plan.json",
                                     "tests/golden/plan/duplicate-handler-id.plan.json",
                                     "tests/golden/plan/missing-handler-export.plan.json",
                                     "tests/golden/plan/empty-handler-export.plan.json",
                                     "tests/golden/plan/wrong-field-type.plan.json"};
    size_t index = 0U;

    for (index = 0U; index < sizeof(fixtures) / sizeof(fixtures[0]); index += 1U) {
        if (!fixture_exists(fixtures[index])) {
            return 60 + (int)index;
        }
    }

    return 0;
}

int main(void)
{
    int result = 0;

    result = test_version_helpers();
    if (result != 0) {
        return result;
    }

    result = test_handler_id_helpers();
    if (result != 0) {
        return result;
    }

    result = test_route_method_helpers();
    if (result != 0) {
        return result;
    }

    result = test_handler_lookup();
    if (result != 0) {
        return result;
    }

    result = test_lookup_invalid_arguments();
    if (result != 0) {
        return result;
    }

    result = test_empty_handler_table();
    if (result != 0) {
        return result;
    }

    result = test_duplicate_handler_ids();
    if (result != 0) {
        return result;
    }

    return test_fixture_files_exist();
}

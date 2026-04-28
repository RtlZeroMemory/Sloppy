#include "sloppy/plan.h"

#include <stdio.h>

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static int read_fixture(const char* path, unsigned char* buffer, size_t capacity, SlBytes* out)
{
    FILE* file = NULL;
    long size = 0L;
    size_t bytes_read = 0U;

    if (path == NULL || buffer == NULL || out == NULL) {
        return 1;
    }

#ifdef _MSC_VER
    if (fopen_s(&file, path, "rb") != 0) {
        return 2;
    }
#else
    file = fopen(path, "rb");
#endif

    if (file == NULL) {
        return 3;
    }

    if (fseek(file, 0L, SEEK_END) != 0) {
        (void)fclose(file);
        return 4;
    }

    size = ftell(file);
    if (size < 0L || (size_t)size > capacity) {
        (void)fclose(file);
        return 5;
    }

    if (fseek(file, 0L, SEEK_SET) != 0) {
        (void)fclose(file);
        return 6;
    }

    bytes_read = fread(buffer, 1U, (size_t)size, file);
    if (bytes_read != (size_t)size) {
        (void)fclose(file);
        return 7;
    }

    if (fclose(file) != 0) {
        return 8;
    }

    *out = sl_bytes_from_parts(buffer, bytes_read);
    return 0;
}

static SlBytes bytes_from_cstr(const char* text)
{
    return sl_bytes_from_parts((const unsigned char*)text, sl_str_from_cstr(text).length);
}

static int parse_fixture(const char* path, SlPlan* out_plan, SlDiag* out_diag,
                         unsigned char* arena_storage, size_t arena_storage_size)
{
    unsigned char json_storage[8192];
    SlBytes json = {0};
    SlArena arena = {0};
    SlPlanParseOptions options = {0};
    SlStatus status;

    if (read_fixture(path, json_storage, sizeof(json_storage), &json) != 0) {
        return 1000;
    }

    status = sl_arena_init(&arena, arena_storage, arena_storage_size);
    if (!sl_status_is_ok(status)) {
        return 1001;
    }

    options.source_name = sl_str_from_cstr(path);
    status = sl_plan_parse_json(&arena, json, &options, out_plan, out_diag);
    return (int)sl_status_code(status);
}

static int test_parse_minimal_valid_fixture(void)
{
    unsigned char arena_storage[8192];
    SlPlan plan = {0};
    SlDiag diag = {0};
    const SlPlanHandler* handler = NULL;
    int status_code = parse_fixture("tests/golden/plan/minimal-valid.plan.json", &plan, &diag,
                                    arena_storage, sizeof(arena_storage));

    if (status_code != SL_STATUS_OK) {
        return 10;
    }

    if (expect_true(plan.version == SL_PLAN_VERSION_1) != 0) {
        return 11;
    }

    if (expect_true(sl_str_equal(plan.compiler_version, sl_str_from_cstr("sloppyc-placeholder"))) !=
        0)
    {
        return 12;
    }

    if (expect_true(sl_str_equal(plan.runtime_min_version, sl_str_from_cstr("0.1.0"))) != 0) {
        return 13;
    }

    if (expect_true(sl_str_equal(plan.target.platform, sl_str_from_cstr("windows-x64"))) != 0 ||
        expect_true(sl_str_equal(plan.target.engine, sl_str_from_cstr("v8"))) != 0)
    {
        return 14;
    }

    if (expect_true(sl_str_equal(plan.bundle.path, sl_str_from_cstr(".sloppy/app.js"))) != 0 ||
        expect_true(sl_str_equal(plan.source_map.path, sl_str_from_cstr(".sloppy/app.js.map"))) !=
            0)
    {
        return 15;
    }

    if (expect_true(plan.handler_count == 1U) != 0) {
        return 16;
    }

    if (expect_status(sl_plan_find_handler_by_id(&plan, 1U, &handler), SL_STATUS_OK) != 0) {
        return 17;
    }

    if (handler == NULL ||
        expect_true(sl_str_equal(handler->export_name, sl_str_from_cstr("__sloppy_handler_1"))) !=
            0 ||
        expect_true(sl_str_equal(handler->display_name, sl_str_from_cstr("Home"))) != 0)
    {
        return 18;
    }

    if (diag.code != SL_DIAG_NONE) {
        return 19;
    }

    return 0;
}

static int test_invalid_version_fixture_fails(void)
{
    unsigned char arena_storage[8192];
    SlPlan plan = {0};
    SlDiag diag = {0};
    int status_code = parse_fixture("tests/golden/plan/invalid-version.plan.json", &plan, &diag,
                                    arena_storage, sizeof(arena_storage));

    if (status_code != SL_STATUS_UNSUPPORTED) {
        return 20;
    }

    if (diag.severity != SL_DIAG_SEVERITY_ERROR || diag.code != SL_DIAG_INVALID_PLAN_VERSION) {
        return 21;
    }

    if (plan.version != 0U || plan.handler_count != 0U || plan.handlers != NULL) {
        return 22;
    }

    return 0;
}

static int test_missing_bundle_fixture_fails(void)
{
    unsigned char arena_storage[8192];
    SlPlan plan = {0};
    SlDiag diag = {0};
    int status_code = parse_fixture("tests/golden/plan/missing-bundle.plan.json", &plan, &diag,
                                    arena_storage, sizeof(arena_storage));

    if (status_code != SL_STATUS_INVALID_ARGUMENT) {
        return 30;
    }

    if (diag.severity != SL_DIAG_SEVERITY_ERROR || diag.code != SL_DIAG_INVALID_PLAN_FIELD) {
        return 31;
    }

    return plan.handlers == NULL && plan.handler_count == 0U ? 0 : 32;
}

static int test_missing_handler_export_fixture_fails(void)
{
    unsigned char arena_storage[8192];
    SlPlan plan = {0};
    SlDiag diag = {0};
    int status_code = parse_fixture("tests/golden/plan/missing-handler-export.plan.json", &plan,
                                    &diag, arena_storage, sizeof(arena_storage));

    if (status_code != SL_STATUS_INVALID_ARGUMENT) {
        return 40;
    }

    if (diag.severity != SL_DIAG_SEVERITY_ERROR || diag.code != SL_DIAG_INVALID_PLAN_FIELD) {
        return 41;
    }

    if (!sl_str_equal(diag.message, sl_str_from_cstr("invalid handler export name"))) {
        return 42;
    }

    return 0;
}

static int test_failed_parse_rolls_back_plan_allocations(void)
{
    unsigned char json_storage[8192];
    unsigned char arena_storage[8192];
    SlArena arena = {0};
    SlBytes json = {0};
    SlPlan plan = {0};
    SlDiag diag = {0};
    SlPlanParseOptions options = {0};
    size_t used_before = 0U;
    size_t used_after_no_diag = 0U;
    size_t used_after_diag = 0U;
    SlStatus status;

    if (read_fixture("tests/golden/plan/missing-handler-export.plan.json", json_storage,
                     sizeof(json_storage), &json) != 0)
    {
        return 43;
    }

    status = sl_arena_init(&arena, arena_storage, sizeof(arena_storage));
    if (!sl_status_is_ok(status)) {
        return 44;
    }

    used_before = sl_arena_used(&arena);
    status = sl_plan_parse_json(&arena, json, NULL, &plan, NULL);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 45;
    }

    used_after_no_diag = sl_arena_used(&arena);
    if (used_after_no_diag != used_before || plan.handlers != NULL || plan.handler_count != 0U) {
        return 46;
    }

    options.source_name = sl_str_from_cstr("tests/golden/plan/missing-handler-export.plan.json");
    status = sl_plan_parse_json(&arena, json, &options, &plan, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 47;
    }

    used_after_diag = sl_arena_used(&arena);
    if (used_after_diag <= used_after_no_diag) {
        return 48;
    }

    if (plan.handlers != NULL || plan.handler_count != 0U) {
        return 49;
    }

    if (diag.severity != SL_DIAG_SEVERITY_ERROR || diag.code != SL_DIAG_INVALID_PLAN_FIELD ||
        !sl_str_equal(diag.message, sl_str_from_cstr("invalid handler export name")) ||
        !sl_str_equal(diag.primary_span.path, options.source_name))
    {
        return 50;
    }

    return 0;
}

static int test_missing_source_map_fixture_fails(void)
{
    unsigned char arena_storage[8192];
    SlPlan plan = {0};
    SlDiag diag = {0};
    int status_code = parse_fixture("tests/golden/plan/missing-source-map.plan.json", &plan, &diag,
                                    arena_storage, sizeof(arena_storage));

    if (status_code != SL_STATUS_INVALID_ARGUMENT) {
        return 45;
    }

    if (diag.severity != SL_DIAG_SEVERITY_ERROR || diag.code != SL_DIAG_INVALID_PLAN_FIELD) {
        return 46;
    }

    return plan.handlers == NULL && plan.handler_count == 0U ? 0 : 47;
}

static int test_duplicate_handler_id_fixture_fails(void)
{
    unsigned char arena_storage[8192];
    SlPlan plan = {0};
    SlDiag diag = {0};
    int status_code = parse_fixture("tests/golden/plan/duplicate-handler-id.plan.json", &plan,
                                    &diag, arena_storage, sizeof(arena_storage));

    if (status_code != SL_STATUS_INVALID_ARGUMENT) {
        return 50;
    }

    if (diag.severity != SL_DIAG_SEVERITY_ERROR || diag.code != SL_DIAG_DUPLICATE_HANDLER_ID) {
        return 51;
    }

    return plan.handlers == NULL && plan.handler_count == 0U ? 0 : 52;
}

static int test_invalid_handler_id_fails(void)
{
    static const char json[] =
        "{"
        "\"schemaVersion\":1,"
        "\"compilerVersion\":\"sloppyc-placeholder\","
        "\"runtimeMinimumVersion\":\"0.1.0\","
        "\"stdlibVersion\":\"0.1.0\","
        "\"target\":{\"platform\":\"windows-x64\",\"engine\":\"v8\"},"
        "\"bundle\":{\"path\":\".sloppy/app.js\",\"id\":\"app-js-test\",\"hash\":\"test-only\"},"
        "\"sourceMap\":{\"path\":\".sloppy/app.js.map\",\"id\":\"app-js-map-test\","
        "\"hash\":\"test-only\"},"
        "\"handlers\":[{\"id\":0,\"exportName\":\"__sloppy_handler_0\","
        "\"displayName\":\"Home\"}]"
        "}";
    unsigned char arena_storage[8192];
    SlArena arena = {0};
    SlPlan plan = {0};
    SlDiag diag = {0};
    SlStatus status;

    status = sl_arena_init(&arena, arena_storage, sizeof(arena_storage));
    if (!sl_status_is_ok(status)) {
        return 55;
    }

    status = sl_plan_parse_json(&arena, bytes_from_cstr(json), NULL, &plan, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 56;
    }

    if (diag.severity != SL_DIAG_SEVERITY_ERROR || diag.code != SL_DIAG_INVALID_PLAN_FIELD) {
        return 57;
    }

    return plan.handlers == NULL && plan.handler_count == 0U ? 0 : 58;
}

static int test_malformed_json_fails(void)
{
    unsigned char arena_storage[4096];
    SlArena arena = {0};
    SlPlan plan = {0};
    SlDiag diag = {0};
    SlPlanParseOptions options = {0};
    SlStatus status;

    status = sl_arena_init(&arena, arena_storage, sizeof(arena_storage));
    if (!sl_status_is_ok(status)) {
        return 60;
    }

    options.source_name = sl_str_from_cstr("embedded-malformed.plan.json");
    status = sl_plan_parse_json(&arena, bytes_from_cstr("{\"schemaVersion\": 1,"), &options, &plan,
                                &diag);

    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 61;
    }

    if (diag.severity != SL_DIAG_SEVERITY_ERROR || diag.code != SL_DIAG_MALFORMED_JSON) {
        return 62;
    }

    return plan.handlers == NULL && plan.handler_count == 0U ? 0 : 63;
}

static int test_wrong_handler_id_type_fails(void)
{
    static const char json[] =
        "{"
        "\"schemaVersion\":1,"
        "\"compilerVersion\":\"sloppyc-placeholder\","
        "\"runtimeMinimumVersion\":\"0.1.0\","
        "\"stdlibVersion\":\"0.1.0\","
        "\"target\":{\"platform\":\"windows-x64\",\"engine\":\"v8\"},"
        "\"bundle\":{\"path\":\".sloppy/app.js\",\"id\":\"app-js-test\",\"hash\":\"test-only\"},"
        "\"sourceMap\":{\"path\":\".sloppy/app.js.map\",\"id\":\"app-js-map-test\","
        "\"hash\":\"test-only\"},"
        "\"handlers\":[{\"id\":\"1\",\"exportName\":\"__sloppy_handler_1\","
        "\"displayName\":\"Home\"}]"
        "}";
    unsigned char arena_storage[8192];
    SlArena arena = {0};
    SlPlan plan = {0};
    SlDiag diag = {0};
    SlStatus status;

    status = sl_arena_init(&arena, arena_storage, sizeof(arena_storage));
    if (!sl_status_is_ok(status)) {
        return 70;
    }

    status = sl_plan_parse_json(&arena, bytes_from_cstr(json), NULL, &plan, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 71;
    }

    if (diag.severity != SL_DIAG_SEVERITY_ERROR || diag.code != SL_DIAG_INVALID_PLAN_FIELD) {
        return 72;
    }

    return 0;
}

static int test_empty_handlers_fails(void)
{
    static const char json[] =
        "{"
        "\"schemaVersion\":1,"
        "\"compilerVersion\":\"sloppyc-placeholder\","
        "\"runtimeMinimumVersion\":\"0.1.0\","
        "\"stdlibVersion\":\"0.1.0\","
        "\"target\":{\"platform\":\"windows-x64\",\"engine\":\"v8\"},"
        "\"bundle\":{\"path\":\".sloppy/app.js\",\"id\":\"app-js-test\",\"hash\":\"test-only\"},"
        "\"sourceMap\":{\"path\":\".sloppy/app.js.map\",\"id\":\"app-js-map-test\","
        "\"hash\":\"test-only\"},"
        "\"handlers\":[]"
        "}";
    unsigned char arena_storage[8192];
    SlArena arena = {0};
    SlPlan plan = {0};
    SlDiag diag = {0};
    SlStatus status;

    status = sl_arena_init(&arena, arena_storage, sizeof(arena_storage));
    if (!sl_status_is_ok(status)) {
        return 80;
    }

    status = sl_plan_parse_json(&arena, bytes_from_cstr(json), NULL, &plan, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 81;
    }

    if (diag.severity != SL_DIAG_SEVERITY_ERROR || diag.code != SL_DIAG_INVALID_PLAN_FIELD) {
        return 82;
    }

    return 0;
}

static int test_unknown_fields_are_ignored(void)
{
    static const char json[] =
        "{"
        "\"schemaVersion\":1,"
        "\"compilerVersion\":\"sloppyc-placeholder\","
        "\"runtimeMinimumVersion\":\"0.1.0\","
        "\"stdlibVersion\":\"0.1.0\","
        "\"target\":{\"platform\":\"windows-x64\",\"engine\":\"v8\",\"extra\":true},"
        "\"bundle\":{\"path\":\".sloppy/app.js\",\"id\":\"app-js-test\",\"hash\":\"test-only\"},"
        "\"sourceMap\":{\"path\":\".sloppy/app.js.map\",\"id\":\"app-js-map-test\","
        "\"hash\":\"test-only\"},"
        "\"handlers\":[{\"id\":1,\"exportName\":\"__sloppy_handler_1\","
        "\"displayName\":\"Home\",\"future\":\"ignored\"}],"
        "\"futureSection\":{\"ok\":true}"
        "}";
    unsigned char arena_storage[8192];
    SlArena arena = {0};
    SlPlan plan = {0};
    SlDiag diag = {0};
    SlStatus status;

    status = sl_arena_init(&arena, arena_storage, sizeof(arena_storage));
    if (!sl_status_is_ok(status)) {
        return 90;
    }

    status = sl_plan_parse_json(&arena, bytes_from_cstr(json), NULL, &plan, &diag);
    if (expect_status(status, SL_STATUS_OK) != 0) {
        return 91;
    }

    if (plan.handler_count != 1U || diag.code != SL_DIAG_NONE) {
        return 92;
    }

    return 0;
}

static int test_invalid_arguments(void)
{
    unsigned char arena_storage[1024];
    SlArena arena = {0};
    SlPlan plan = {0};
    SlStatus status;

    status = sl_arena_init(&arena, arena_storage, sizeof(arena_storage));
    if (!sl_status_is_ok(status)) {
        return 100;
    }

    if (expect_status(sl_plan_parse_json(NULL, bytes_from_cstr("{}"), NULL, &plan, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 101;
    }

    if (expect_status(sl_plan_parse_json(&arena, sl_bytes_empty(), NULL, &plan, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 102;
    }

    if (expect_status(sl_plan_parse_json(&arena, bytes_from_cstr("{}"), NULL, NULL, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 103;
    }

    return 0;
}

int main(void)
{
    int result = 0;

    result = test_parse_minimal_valid_fixture();
    if (result != 0) {
        return result;
    }

    result = test_invalid_version_fixture_fails();
    if (result != 0) {
        return result;
    }

    result = test_missing_bundle_fixture_fails();
    if (result != 0) {
        return result;
    }

    result = test_missing_handler_export_fixture_fails();
    if (result != 0) {
        return result;
    }

    result = test_failed_parse_rolls_back_plan_allocations();
    if (result != 0) {
        return result;
    }

    result = test_missing_source_map_fixture_fails();
    if (result != 0) {
        return result;
    }

    result = test_duplicate_handler_id_fixture_fails();
    if (result != 0) {
        return result;
    }

    result = test_invalid_handler_id_fails();
    if (result != 0) {
        return result;
    }

    result = test_malformed_json_fails();
    if (result != 0) {
        return result;
    }

    result = test_wrong_handler_id_type_fails();
    if (result != 0) {
        return result;
    }

    result = test_empty_handlers_fails();
    if (result != 0) {
        return result;
    }

    result = test_unknown_fields_are_ignored();
    if (result != 0) {
        return result;
    }

    return test_invalid_arguments();
}

#include "sloppy/plan.h"

#include <stdbool.h>
#include <stdio.h>

#define TEST_ARENA_SIZE 65536U
#define TEST_JSON_SIZE 65536U

typedef struct ValidFixtureCase
{
    const char* path;
    size_t handler_count;
    const char* first_export_name;
    const char* first_display_name;
    const char* second_export_name;
    const char* second_display_name;
    SlHandlerId first_handler_id;
    SlHandlerId second_handler_id;
} ValidFixtureCase;

typedef struct InvalidFixtureCase
{
    const char* path;
    SlStatusCode status_code;
    SlDiagCode diag_code;
    const char* message;
} InvalidFixtureCase;

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
        fclose(file);
        return 4;
    }

    size = ftell(file);
    if (size < 0L || (size_t)size > capacity) {
        fclose(file);
        return 5;
    }

    if (fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return 6;
    }

    bytes_read = fread(buffer, 1U, (size_t)size, file);
    if (bytes_read != (size_t)size) {
        fclose(file);
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

static SlStatus parse_fixture(const char* path, SlPlan* out_plan, SlDiag* out_diag,
                              unsigned char* arena_storage, size_t arena_storage_size)
{
    unsigned char json_storage[TEST_JSON_SIZE];
    SlBytes json = {0};
    SlArena arena = {0};
    SlPlanParseOptions options = {0};
    SlStatus status;

    if (read_fixture(path, json_storage, sizeof(json_storage), &json) != 0) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    status = sl_arena_init(&arena, arena_storage, arena_storage_size);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    options.source_name = sl_str_from_cstr(path);
    return sl_plan_parse_json(&arena, json, &options, out_plan, out_diag);
}

static SlStatus parse_inline_plan(const char* json, SlPlan* out_plan, SlDiag* out_diag,
                                  unsigned char* arena_storage, size_t arena_storage_size)
{
    SlArena arena = {0};
    SlPlanParseOptions options = {0};
    SlStatus status;

    status = sl_arena_init(&arena, arena_storage, arena_storage_size);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    options.source_name = sl_str_from_cstr("inline-test.plan.json");
    return sl_plan_parse_json(&arena, bytes_from_cstr(json), &options, out_plan, out_diag);
}

static int expect_inline_plan_failure(const char* json, SlStatusCode status_code,
                                      SlDiagCode diag_code, const char* message)
{
    unsigned char arena_storage[TEST_ARENA_SIZE];
    SlPlan plan = {0};
    SlDiag diag = {0};
    SlStatus status = parse_inline_plan(json, &plan, &diag, arena_storage, sizeof(arena_storage));

    if (expect_status(status, status_code) != 0) {
        return 1;
    }
    if (diag.code != diag_code || !sl_str_equal(diag.message, sl_str_from_cstr(message))) {
        return 2;
    }
    if (plan.version != 0U || plan.routes != NULL || plan.route_count != 0U ||
        plan.schemas != NULL || plan.schema_count != 0U)
    {
        return 3;
    }
    return 0;
}

static int expect_common_plan_fields(const SlPlan* plan)
{
    if (plan == NULL) {
        return 1;
    }

    if (expect_true(plan->version == SL_PLAN_VERSION_1) != 0) {
        return 2;
    }

    if (expect_true(plan->kind == SL_PLAN_KIND_WEB) != 0) {
        return 22;
    }

    if (expect_true(
            sl_str_equal(plan->compiler_version, sl_str_from_cstr("sloppyc-placeholder"))) != 0)
    {
        return 3;
    }

    if (expect_true(sl_str_equal(plan->runtime_min_version, sl_str_from_cstr("0.1.0"))) != 0 ||
        expect_true(sl_str_equal(plan->stdlib_version, sl_str_from_cstr("0.1.0"))) != 0)
    {
        return 4;
    }

    if (expect_true(sl_str_equal(plan->target.platform, sl_str_from_cstr("windows-x64"))) != 0 ||
        expect_true(sl_str_equal(plan->target.engine, sl_str_from_cstr("v8"))) != 0)
    {
        return 5;
    }

    if (expect_true(sl_str_equal(plan->bundle.path, sl_str_from_cstr(".sloppy/app.js"))) != 0 ||
        expect_true(sl_str_equal(plan->bundle.id, sl_str_from_cstr("app-js-test"))) != 0 ||
        expect_true(sl_str_equal(plan->bundle.hash, sl_str_from_cstr("test-only"))) != 0)
    {
        return 6;
    }

    if (expect_true(sl_str_equal(plan->source_map.path, sl_str_from_cstr(".sloppy/app.js.map"))) !=
            0 ||
        expect_true(sl_str_equal(plan->source_map.id, sl_str_from_cstr("app-js-map-test"))) != 0 ||
        expect_true(sl_str_equal(plan->source_map.hash, sl_str_from_cstr("test-only"))) != 0)
    {
        return 7;
    }

    return 0;
}

static const char* minimal_program_plan_json(void)
{
    return "{"
           "\"schemaVersion\":1,"
           "\"kind\":\"program\","
           "\"compilerVersion\":\"sloppyc-placeholder\","
           "\"runtimeMinimumVersion\":\"0.1.0\","
           "\"stdlibVersion\":\"0.1.0\","
           "\"target\":{\"platform\":\"windows-x64\",\"engine\":\"v8\"},"
           "\"bundle\":{\"path\":\".sloppy/app.js\",\"id\":\"app-js-test\",\"hash\":\"test-only\"},"
           "\"sourceMap\":{\"path\":\".sloppy/app.js.map\",\"id\":\"app-js-map-test\","
           "\"hash\":\"test-only\"},"
           "\"handlers\":[],"
           "\"routes\":[]"
           "}";
}

static int test_program_kind_allows_empty_handlers_and_routes(void)
{
    unsigned char arena_storage[TEST_ARENA_SIZE];
    SlPlan plan = {0};
    SlDiag diag = {0};
    SlStatus status = parse_inline_plan(minimal_program_plan_json(), &plan, &diag, arena_storage,
                                        sizeof(arena_storage));

    if (expect_status(status, SL_STATUS_OK) != 0 || diag.code != SL_DIAG_NONE) {
        return 1;
    }
    if (plan.kind != SL_PLAN_KIND_PROGRAM || plan.handler_count != 0U || plan.route_count != 0U) {
        return 2;
    }
    return 0;
}

static int test_native_ffi_metadata_parses_typed_libraries_and_structs(void)
{
    unsigned char arena_storage[TEST_ARENA_SIZE];
    SlPlan plan = {0};
    SlDiag diag = {0};
    SlStatus status = parse_inline_plan(
        "{\"schemaVersion\":1,\"kind\":\"program\",\"compilerVersion\":\"sloppyc-placeholder\","
        "\"runtimeMinimumVersion\":\"0.1.0\",\"stdlibVersion\":\"0.1.0\","
        "\"target\":{\"platform\":\"windows-x64\",\"engine\":\"v8\"},"
        "\"bundle\":{\"path\":\".sloppy/app.js\",\"id\":\"app-js-test\",\"hash\":\"test-only\"},"
        "\"sourceMap\":{\"path\":\".sloppy/app.js.map\",\"id\":\"app-js-map-test\","
        "\"hash\":\"test-only\"},\"handlers\":[],\"routes\":[],"
        "\"native\":{\"ffi\":[{\"name\":\"ffi-test\",\"convention\":\"system\","
        "\"functions\":[{\"id\":\"ffi:ffi-test:addI32\",\"name\":\"addI32\","
        "\"symbol\":\"sloppy_ffi_add_i32\",\"convention\":\"system\",\"return\":\"i32\","
        "\"parameters\":[\"i32\",\"i32\"]},"
        "{\"id\":\"ffi:ffi-test:enabled\",\"name\":\"enabled\","
        "\"symbol\":\"sloppy_ffi_enabled\",\"convention\":\"system\",\"return\":\"bool32\","
        "\"parameters\":[\"win.BOOL\"]}]}],"
        "\"ffiStructs\":[{\"name\":\"Point\",\"layout\":\"sequential\",\"pack\":4,"
        "\"fields\":[{\"name\":\"x\",\"type\":\"i32\"},"
        "{\"name\":\"y\",\"type\":\"i32\"}]}]}}",
        &plan, &diag, arena_storage, sizeof(arena_storage));

    if (expect_status(status, SL_STATUS_OK) != 0 || diag.code != SL_DIAG_NONE) {
        return 1;
    }
    if (plan.ffi_library_count != 1U || plan.ffi_libraries[0].function_count != 2U ||
        !sl_str_equal(plan.ffi_libraries[0].name, sl_str_from_cstr("ffi-test")) ||
        plan.ffi_libraries[0].functions[0].return_type != SL_PLAN_FFI_TYPE_I32 ||
        plan.ffi_libraries[0].functions[0].parameter_count != 2U ||
        plan.ffi_libraries[0].functions[0].parameters[0] != SL_PLAN_FFI_TYPE_I32 ||
        plan.ffi_libraries[0].functions[0].parameters[1] != SL_PLAN_FFI_TYPE_I32)
    {
        return 2;
    }
    if (plan.ffi_libraries[0].functions[1].return_type != SL_PLAN_FFI_TYPE_I32 ||
        plan.ffi_libraries[0].functions[1].parameter_count != 1U ||
        plan.ffi_libraries[0].functions[1].parameters[0] != SL_PLAN_FFI_TYPE_I32)
    {
        return 4;
    }
    if (plan.ffi_struct_count != 1U || plan.ffi_structs[0].field_count != 2U ||
        !sl_str_equal(plan.ffi_structs[0].name, sl_str_from_cstr("Point")) ||
        !sl_str_equal(plan.ffi_structs[0].layout, sl_str_from_cstr("sequential")) ||
        !plan.ffi_structs[0].has_pack || plan.ffi_structs[0].pack != 4U ||
        plan.ffi_structs[0].fields[0].type != SL_PLAN_FFI_TYPE_I32 ||
        plan.ffi_structs[0].fields[1].type != SL_PLAN_FFI_TYPE_I32)
    {
        return 3;
    }
    return 0;
}

static int test_native_ffi_function_parameters_reject_void(void)
{
    return expect_inline_plan_failure(
        "{\"schemaVersion\":1,\"kind\":\"program\",\"compilerVersion\":\"sloppyc-placeholder\","
        "\"runtimeMinimumVersion\":\"0.1.0\",\"stdlibVersion\":\"0.1.0\","
        "\"target\":{\"platform\":\"windows-x64\",\"engine\":\"v8\"},"
        "\"bundle\":{\"path\":\".sloppy/app.js\",\"id\":\"app-js-test\",\"hash\":\"test-only\"},"
        "\"sourceMap\":{\"path\":\".sloppy/app.js.map\",\"id\":\"app-js-map-test\","
        "\"hash\":\"test-only\"},\"handlers\":[],\"routes\":[],"
        "\"native\":{\"ffi\":[{\"name\":\"ffi-test\",\"convention\":\"system\","
        "\"functions\":[{\"id\":\"ffi:ffi-test:bad\","
        "\"name\":\"bad\",\"symbol\":\"sloppy_bad\",\"convention\":\"system\","
        "\"return\":\"void\",\"parameters\":[\"void\"]}]}]}}",
        SL_STATUS_INVALID_ARGUMENT, SL_DIAG_INVALID_PLAN_FIELD, "unsupported FFI parameter type");
}

static int test_native_ffi_struct_fields_reject_unsized_types(void)
{
    return expect_inline_plan_failure(
        "{\"schemaVersion\":1,\"kind\":\"program\",\"compilerVersion\":\"sloppyc-placeholder\","
        "\"runtimeMinimumVersion\":\"0.1.0\",\"stdlibVersion\":\"0.1.0\","
        "\"target\":{\"platform\":\"windows-x64\",\"engine\":\"v8\"},"
        "\"bundle\":{\"path\":\".sloppy/app.js\",\"id\":\"app-js-test\",\"hash\":\"test-only\"},"
        "\"sourceMap\":{\"path\":\".sloppy/app.js.map\",\"id\":\"app-js-map-test\","
        "\"hash\":\"test-only\"},\"handlers\":[],\"routes\":[],"
        "\"native\":{\"ffiStructs\":[{\"name\":\"Bad\",\"fields\":[{\"name\":\"name\","
        "\"type\":\"cstring\"}]}]}}",
        SL_STATUS_INVALID_ARGUMENT, SL_DIAG_INVALID_PLAN_FIELD,
        "unsupported FFI struct field type");
}

static int test_program_kind_rejects_non_empty_handlers(void)
{
    unsigned char arena_storage[TEST_ARENA_SIZE];
    SlPlan plan = {0};
    SlDiag diag = {0};
    SlStatus status = parse_inline_plan(
        "{\"schemaVersion\":1,\"kind\":\"program\",\"compilerVersion\":\"sloppyc-placeholder\","
        "\"runtimeMinimumVersion\":\"0.1.0\",\"stdlibVersion\":\"0.1.0\","
        "\"target\":{\"platform\":\"windows-x64\",\"engine\":\"v8\"},"
        "\"bundle\":{\"path\":\".sloppy/app.js\",\"id\":\"app-js-test\",\"hash\":\"test-only\"},"
        "\"sourceMap\":{\"path\":\".sloppy/app.js.map\",\"id\":\"app-js-map-test\","
        "\"hash\":\"test-only\"},\"handlers\":[{\"id\":1,\"exportName\":\"__sloppy_handler_1\","
        "\"displayName\":\"main\"}],\"routes\":[]}",
        &plan, &diag, arena_storage, sizeof(arena_storage));

    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 1;
    }
    if (diag.code != SL_DIAG_INVALID_PLAN_FIELD) {
        return 2;
    }
    return 0;
}

static int test_program_kind_rejects_non_empty_routes(void)
{
    unsigned char arena_storage[TEST_ARENA_SIZE];
    SlPlan plan = {0};
    SlDiag diag = {0};
    SlStatus status = parse_inline_plan(
        "{\"schemaVersion\":1,\"kind\":\"program\",\"compilerVersion\":\"sloppyc-placeholder\","
        "\"runtimeMinimumVersion\":\"0.1.0\",\"stdlibVersion\":\"0.1.0\","
        "\"target\":{\"platform\":\"windows-x64\",\"engine\":\"v8\"},"
        "\"bundle\":{\"path\":\".sloppy/app.js\",\"id\":\"app-js-test\",\"hash\":\"test-only\"},"
        "\"sourceMap\":{\"path\":\".sloppy/app.js.map\",\"id\":\"app-js-map-test\","
        "\"hash\":\"test-only\"},\"handlers\":[],"
        "\"routes\":[{\"method\":\"GET\",\"pattern\":\"/\",\"handlerId\":1}]}",
        &plan, &diag, arena_storage, sizeof(arena_storage));

    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 1;
    }
    if (diag.code != SL_DIAG_INVALID_PLAN_FIELD) {
        return 2;
    }
    return 0;
}

static int test_missing_plan_kind_defaults_to_web(void)
{
    unsigned char arena_storage[TEST_ARENA_SIZE];
    SlPlan plan = {0};
    SlDiag diag = {0};
    SlStatus status = parse_inline_plan(
        "{\"schemaVersion\":1,\"compilerVersion\":\"sloppyc-placeholder\","
        "\"runtimeMinimumVersion\":\"0.1.0\",\"stdlibVersion\":\"0.1.0\","
        "\"target\":{\"platform\":\"windows-x64\",\"engine\":\"v8\"},"
        "\"bundle\":{\"path\":\".sloppy/app.js\",\"id\":\"app-js-test\",\"hash\":\"test-only\"},"
        "\"sourceMap\":{\"path\":\".sloppy/app.js.map\",\"id\":\"app-js-map-test\","
        "\"hash\":\"test-only\"},\"handlers\":[{\"id\":1,\"exportName\":\"__sloppy_handler_1\","
        "\"displayName\":\"main\"}],"
        "\"routes\":[{\"method\":\"GET\",\"pattern\":\"/\",\"handlerId\":1}]}",
        &plan, &diag, arena_storage, sizeof(arena_storage));

    if (expect_status(status, SL_STATUS_OK) != 0 || diag.code != SL_DIAG_NONE) {
        return 1;
    }
    if (plan.kind != SL_PLAN_KIND_WEB || plan.handler_count != 1U || plan.route_count != 1U) {
        return 2;
    }
    return 0;
}

static int test_invalid_plan_kind_is_rejected(void)
{
    unsigned char arena_storage[TEST_ARENA_SIZE];
    SlPlan plan = {0};
    SlDiag diag = {0};
    SlStatus status = parse_inline_plan(
        "{\"schemaVersion\":1,\"kind\":\"worker\",\"compilerVersion\":\"sloppyc-placeholder\","
        "\"runtimeMinimumVersion\":\"0.1.0\",\"stdlibVersion\":\"0.1.0\","
        "\"target\":{\"platform\":\"windows-x64\",\"engine\":\"v8\"},"
        "\"bundle\":{\"path\":\".sloppy/app.js\",\"id\":\"app-js-test\",\"hash\":\"test-only\"},"
        "\"sourceMap\":{\"path\":\".sloppy/app.js.map\",\"id\":\"app-js-map-test\","
        "\"hash\":\"test-only\"},\"handlers\":[],\"routes\":[]}",
        &plan, &diag, arena_storage, sizeof(arena_storage));

    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 1;
    }
    if (diag.code != SL_DIAG_INVALID_PLAN_FIELD) {
        return 2;
    }
    return 0;
}

static int test_numeric_plan_kind_is_rejected(void)
{
    unsigned char arena_storage[TEST_ARENA_SIZE];
    SlPlan plan = {0};
    SlDiag diag = {0};
    SlStatus status = parse_inline_plan(
        "{\"schemaVersion\":1,\"kind\":1,\"compilerVersion\":\"sloppyc-placeholder\","
        "\"runtimeMinimumVersion\":\"0.1.0\",\"stdlibVersion\":\"0.1.0\","
        "\"target\":{\"platform\":\"windows-x64\",\"engine\":\"v8\"},"
        "\"bundle\":{\"path\":\".sloppy/app.js\",\"id\":\"app-js-test\",\"hash\":\"test-only\"},"
        "\"sourceMap\":{\"path\":\".sloppy/app.js.map\",\"id\":\"app-js-map-test\","
        "\"hash\":\"test-only\"},\"handlers\":[],\"routes\":[]}",
        &plan, &diag, arena_storage, sizeof(arena_storage));

    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 1;
    }
    if (diag.code != SL_DIAG_INVALID_PLAN_FIELD) {
        return 2;
    }
    return 0;
}

static int expect_handler(const SlPlan* plan, SlHandlerId id, const char* export_name,
                          const char* display_name)
{
    const SlPlanHandler* handler = NULL;

    if (expect_status(sl_plan_find_handler_by_id(plan, id, &handler), SL_STATUS_OK) != 0) {
        return 1;
    }

    if (handler == NULL) {
        return 2;
    }

    if (expect_true(sl_str_equal(handler->export_name, sl_str_from_cstr(export_name))) != 0 ||
        expect_true(sl_str_equal(handler->display_name, sl_str_from_cstr(display_name))) != 0)
    {
        return 3;
    }

    return 0;
}

static int expect_valid_route_methods(const SlPlan* plan)
{
    if (plan == NULL || plan->route_count != 6U) {
        return 1;
    }

    if (!sl_str_equal(plan->routes[0].method, sl_str_from_cstr("GET")) ||
        !sl_str_equal(plan->routes[0].pattern, sl_str_from_cstr("/users")) ||
        plan->routes[0].handler_id != 1U ||
        !sl_str_equal(plan->routes[1].method, sl_str_from_cstr("POST")) ||
        !sl_str_equal(plan->routes[1].pattern, sl_str_from_cstr("/users")) ||
        plan->routes[1].handler_id != 2U ||
        !sl_str_equal(plan->routes[2].method, sl_str_from_cstr("PUT")) ||
        !sl_str_equal(plan->routes[2].pattern, sl_str_from_cstr("/users/{id:int}")) ||
        plan->routes[2].handler_id != 3U ||
        !sl_str_equal(plan->routes[3].method, sl_str_from_cstr("PATCH")) ||
        !sl_str_equal(plan->routes[3].pattern, sl_str_from_cstr("/users/{id:int}")) ||
        plan->routes[3].handler_id != 4U ||
        !sl_str_equal(plan->routes[4].method, sl_str_from_cstr("DELETE")) ||
        !sl_str_equal(plan->routes[4].pattern, sl_str_from_cstr("/users/{id:int}")) ||
        plan->routes[4].handler_id != 5U ||
        !sl_str_equal(plan->routes[5].method, sl_str_from_cstr("OPTIONS")) ||
        !sl_str_equal(plan->routes[5].pattern, sl_str_from_cstr("/users")) ||
        plan->routes[5].handler_id != 6U)
    {
        return 2;
    }

    return 0;
}

static int expect_valid_capability_skeletons(const SlPlan* plan)
{
    if (plan->capability_count != 2U ||
        !sl_str_equal(plan->capabilities[0].kind, sl_str_from_cstr("filesystem")) ||
        !sl_str_equal(plan->capabilities[0].access, sl_str_from_cstr("readwrite")) ||
        !sl_str_equal(plan->capabilities[1].kind, sl_str_from_cstr("network")) ||
        !sl_str_equal(plan->capabilities[1].access, sl_str_from_cstr("connect-listen")))
    {
        return 1;
    }
    return 0;
}

static int expect_single_required_feature(const SlPlan* plan, const char* id)
{
    if (plan->required_feature_count != 1U ||
        !sl_str_equal(plan->required_features[0].id, sl_str_from_cstr(id)))
    {
        return 1;
    }
    return 0;
}

static int expect_valid_filesystem_capability_accesses(const SlPlan* plan)
{
    static const char* expected_tokens[] = {"files.append",   "files.delete", "files.list",
                                            "files.metadata", "files.watch",  "files.lock"};
    static const char* expected_accesses[] = {"append",   "delete", "list",
                                              "metadata", "watch",  "lock"};
    const size_t expected_count = sizeof(expected_accesses) / sizeof(expected_accesses[0]);

    if (plan->capability_count != expected_count) {
        return 1;
    }

    for (size_t index = 0U; index < expected_count; ++index) {
        if (!sl_str_equal(plan->capabilities[index].kind, sl_str_from_cstr("filesystem")) ||
            !sl_str_equal(plan->capabilities[index].token,
                          sl_str_from_cstr(expected_tokens[index])) ||
            !sl_str_equal(plan->capabilities[index].access,
                          sl_str_from_cstr(expected_accesses[index])))
        {
            return 1;
        }
    }

    return 0;
}

static int expect_valid_os_capability_accesses(const SlPlan* plan)
{
    static const char* expected_kinds[] = {"os",      "env",     "env",     "process",
                                           "process", "process", "process", "signals"};
    static const char* expected_accesses[] = {"info",  "read",   "list", "run",
                                              "shell", "signal", "kill", "shutdown"};
    const size_t expected_count = sizeof(expected_accesses) / sizeof(expected_accesses[0]);

    if (plan->capability_count != expected_count) {
        return 1;
    }

    for (size_t index = 0U; index < expected_count; ++index) {
        if (!sl_str_equal(plan->capabilities[index].kind,
                          sl_str_from_cstr(expected_kinds[index])) ||
            !sl_str_equal(plan->capabilities[index].access,
                          sl_str_from_cstr(expected_accesses[index])))
        {
            return 1;
        }
    }

    return 0;
}

static int test_valid_fixture_matrix(void)
{
    static const ValidFixtureCase cases[] = {
        {"tests/golden/plan/valid-minimal.plan.json", 1U, "__sloppy_handler_1", "Home", NULL, NULL,
         1U, 0U},
        {"tests/golden/plan/valid-multiple-handlers.plan.json", 2U, "__sloppy_handler_1", "Home",
         "__sloppy_handler_2", "Health", 1U, 2U},
        {"tests/golden/plan/unknown-future-field.plan.json", 1U, "__sloppy_handler_1", "Home", NULL,
         NULL, 1U, 0U},
        {"tests/golden/plan/valid-route-section.plan.json", 1U, "__sloppy_handler_1", "Home", NULL,
         NULL, 1U, 0U},
        {"tests/golden/plan/valid-route-methods.plan.json", 6U, "__sloppy_handler_1", "Users.List",
         "__sloppy_handler_2", "Users.Create", 1U, 2U},
        {"tests/golden/plan/valid-provider-section.plan.json", 1U, "__sloppy_handler_1", "Home",
         NULL, NULL, 1U, 0U},
        {"tests/golden/plan/valid-capability-section.plan.json", 1U, "__sloppy_handler_1", "Home",
         NULL, NULL, 1U, 0U},
        {"tests/golden/plan/valid-capability-skeletons.plan.json", 1U, "__sloppy_handler_1", "Home",
         NULL, NULL, 1U, 0U},
        {"tests/golden/plan/valid-network-required-feature.plan.json", 1U, "__sloppy_handler_1",
         "Home", NULL, NULL, 1U, 0U},
        {"tests/golden/plan/unknown-required-feature.plan.json", 1U, "__sloppy_handler_1", "Home",
         NULL, NULL, 1U, 0U}};
    size_t index = 0U;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index += 1U) {
        unsigned char arena_storage[TEST_ARENA_SIZE];
        SlPlan plan = {0};
        SlDiag diag = {0};
        SlStatus status =
            parse_fixture(cases[index].path, &plan, &diag, arena_storage, sizeof(arena_storage));

        if (expect_status(status, SL_STATUS_OK) != 0) {
            return 10 + (int)index;
        }

        if (expect_common_plan_fields(&plan) != 0) {
            return 20 + (int)index;
        }

        if (expect_true(plan.handler_count == cases[index].handler_count) != 0) {
            return 30 + (int)index;
        }

        if (expect_handler(&plan, cases[index].first_handler_id, cases[index].first_export_name,
                           cases[index].first_display_name) != 0)
        {
            return 40 + (int)index;
        }

        if (cases[index].second_handler_id != SL_HANDLER_ID_INVALID &&
            expect_handler(&plan, cases[index].second_handler_id, cases[index].second_export_name,
                           cases[index].second_display_name) != 0)
        {
            return 50 + (int)index;
        }

        if (diag.code != SL_DIAG_NONE) {
            return 60 + (int)index;
        }

        if (sl_str_equal(sl_str_from_cstr(cases[index].path),
                         sl_str_from_cstr("tests/golden/plan/valid-route-section.plan.json")) &&
            (plan.route_count != 1U ||
             !sl_str_equal(plan.routes[0].method, sl_str_from_cstr("GET")) ||
             !sl_str_equal(plan.routes[0].pattern, sl_str_from_cstr("/users/{id:int}")) ||
             plan.routes[0].handler_id != 1U ||
             !sl_str_equal(plan.routes[0].name, sl_str_from_cstr("Users.Get"))))
        {
            return 65 + (int)index;
        }

        if (sl_str_equal(sl_str_from_cstr(cases[index].path),
                         sl_str_from_cstr("tests/golden/plan/valid-route-methods.plan.json")) &&
            expect_valid_route_methods(&plan) != 0)
        {
            return 66 + (int)index;
        }

        if (sl_str_equal(sl_str_from_cstr(cases[index].path),
                         sl_str_from_cstr("tests/golden/plan/valid-provider-section.plan.json")) &&
            (plan.data_provider_count != 1U ||
             !sl_str_equal(plan.data_providers[0].token, sl_str_from_cstr("data.main")) ||
             !sl_str_equal(plan.data_providers[0].provider, sl_str_from_cstr("sqlite"))))
        {
            return 67 + (int)index;
        }

        if (sl_str_equal(
                sl_str_from_cstr(cases[index].path),
                sl_str_from_cstr("tests/golden/plan/valid-capability-section.plan.json")) &&
            (plan.capability_count != 1U ||
             !sl_str_equal(plan.capabilities[0].kind, sl_str_from_cstr("database")) ||
             !sl_str_equal(plan.capabilities[0].access, sl_str_from_cstr("readwrite"))))
        {
            return 68 + (int)index;
        }
        if (sl_str_equal(
                sl_str_from_cstr(cases[index].path),
                sl_str_from_cstr("tests/golden/plan/valid-capability-skeletons.plan.json")) &&
            expect_valid_capability_skeletons(&plan) != 0)
        {
            return 69 + (int)index;
        }
        if (sl_str_equal(
                sl_str_from_cstr(cases[index].path),
                sl_str_from_cstr("tests/golden/plan/valid-network-required-feature.plan.json")) &&
            expect_single_required_feature(&plan, "stdlib.net") != 0)
        {
            return 70 + (int)index;
        }
        if (sl_str_equal(
                sl_str_from_cstr(cases[index].path),
                sl_str_from_cstr("tests/golden/plan/unknown-required-feature.plan.json")) &&
            expect_single_required_feature(&plan, "future.compiler.required.feature") != 0)
        {
            return 71 + (int)index;
        }
    }

    return 0;
}

static int test_valid_filesystem_capability_accesses_fixture(void)
{
    unsigned char arena_storage[TEST_ARENA_SIZE];
    SlPlan plan = {0};
    SlDiag diag = {0};
    SlStatus status =
        parse_fixture("tests/golden/plan/valid-filesystem-capability-accesses.plan.json", &plan,
                      &diag, arena_storage, sizeof(arena_storage));

    if (expect_status(status, SL_STATUS_OK) != 0) {
        return 1;
    }
    if (diag.code != SL_DIAG_NONE) {
        return 2;
    }
    return expect_valid_filesystem_capability_accesses(&plan);
}

static int test_valid_os_capability_accesses_fixture(void)
{
    unsigned char arena_storage[TEST_ARENA_SIZE];
    SlPlan plan = {0};
    SlDiag diag = {0};
    SlStatus status = parse_fixture("tests/golden/plan/valid-os-capability-accesses.plan.json",
                                    &plan, &diag, arena_storage, sizeof(arena_storage));

    if (expect_status(status, SL_STATUS_OK) != 0) {
        return 1;
    }
    if (diag.code != SL_DIAG_NONE) {
        return 2;
    }
    if (expect_single_required_feature(&plan, "stdlib.os") != 0) {
        return 3;
    }
    return expect_valid_os_capability_accesses(&plan);
}

static int test_framework_metadata_bindings_and_schemas_fixture(void)
{
    unsigned char arena_storage[TEST_ARENA_SIZE];
    SlPlan plan = {0};
    SlDiag diag = {0};
    SlStatus status =
        parse_fixture("compiler/tests/fixtures/framework-metadata/expected/app.plan.json", &plan,
                      &diag, arena_storage, sizeof(arena_storage));
    const SlPlanRoute* post_route = NULL;
    const SlPlanRoute* get_route = NULL;
    const SlPlanSchema* user_create = NULL;
    bool saw_email = false;
    bool saw_password = false;
    bool saw_profile = false;
    bool saw_role = false;
    size_t index = 0U;

    if (expect_status(status, SL_STATUS_OK) != 0) {
        return 1;
    }
    if (diag.code != SL_DIAG_NONE) {
        return 2;
    }
    if (plan.route_count != 2U || plan.schema_count != 2U) {
        return 3;
    }

    post_route = &plan.routes[0];
    get_route = &plan.routes[1];
    if (!sl_str_equal(post_route->method, sl_str_from_cstr("POST")) ||
        !sl_str_equal(post_route->pattern, sl_str_from_cstr("/users")) ||
        post_route->binding_count != 6U)
    {
        return 4;
    }
    if (post_route->bindings[0].kind != SL_PLAN_REQUEST_BINDING_BODY_JSON ||
        !sl_str_equal(post_route->bindings[0].parameter, sl_str_from_cstr("input")) ||
        !sl_str_equal(post_route->bindings[0].schema, sl_str_from_cstr("UserCreate")) ||
        post_route->bindings[1].kind != SL_PLAN_REQUEST_BINDING_INJECTION ||
        !sl_str_equal(post_route->bindings[1].name, sl_str_from_cstr("main")) ||
        post_route->bindings[5].kind != SL_PLAN_REQUEST_BINDING_CONTEXT)
    {
        return 5;
    }
    if (!sl_str_equal(get_route->method, sl_str_from_cstr("GET")) ||
        !sl_str_equal(get_route->pattern, sl_str_from_cstr("/users/{id}")) ||
        get_route->binding_count != 6U)
    {
        return 6;
    }
    if (get_route->bindings[0].kind != SL_PLAN_REQUEST_BINDING_ROUTE ||
        !sl_str_equal(get_route->bindings[0].name, sl_str_from_cstr("id")) ||
        get_route->bindings[1].kind != SL_PLAN_REQUEST_BINDING_HEADER ||
        !sl_str_equal(get_route->bindings[1].name, sl_str_from_cstr("x-trace-id")) ||
        get_route->bindings[2].kind != SL_PLAN_REQUEST_BINDING_QUERY ||
        !sl_str_equal(get_route->bindings[2].name, sl_str_from_cstr("includeDeleted")) ||
        get_route->bindings[3].kind != SL_PLAN_REQUEST_BINDING_BODY_JSON)
    {
        return 7;
    }

    for (index = 0U; index < plan.schema_count; index += 1U) {
        if (sl_str_equal(plan.schemas[index].name, sl_str_from_cstr("UserCreate"))) {
            user_create = &plan.schemas[index];
        }
    }
    if (user_create == NULL || user_create->definition.kind != SL_PLAN_SCHEMA_OBJECT ||
        user_create->definition.property_count != 10U)
    {
        return 8;
    }
    for (index = 0U; index < user_create->definition.property_count; index += 1U) {
        const SlPlanSchemaProperty* property = &user_create->definition.properties[index];

        if (sl_str_equal(property->name, sl_str_from_cstr("email"))) {
            saw_email = true;
            if (property->schema->kind != SL_PLAN_SCHEMA_STRING ||
                !sl_str_equal(property->schema->validation, sl_str_from_cstr("email")))
            {
                return 9;
            }
        }
        if (sl_str_equal(property->name, sl_str_from_cstr("password"))) {
            saw_password = true;
            if (property->schema->kind != SL_PLAN_SCHEMA_STRING || !property->schema->secret ||
                !property->schema->has_min || property->schema->min_value != 8)
            {
                return 10;
            }
        }
        if (sl_str_equal(property->name, sl_str_from_cstr("profile"))) {
            saw_profile = true;
            if (property->schema->kind != SL_PLAN_SCHEMA_OBJECT || !property->schema->optional ||
                property->schema->property_count != 2U)
            {
                return 11;
            }
        }
        if (sl_str_equal(property->name, sl_str_from_cstr("role"))) {
            saw_role = true;
            if (property->schema->kind != SL_PLAN_SCHEMA_LITERAL_UNION ||
                property->schema->variant_count != 2U)
            {
                return 12;
            }
        }
    }
    if (!saw_email || !saw_password || !saw_profile || !saw_role) {
        return 13;
    }

    return 0;
}

static int test_schema_names_must_be_unique(void)
{
    unsigned char arena_storage[TEST_ARENA_SIZE];
    SlPlan plan = {0};
    SlDiag diag = {0};
    SlStatus status = parse_inline_plan(
        "{\"schemaVersion\":1,\"compilerVersion\":\"sloppyc-test\","
        "\"runtimeMinimumVersion\":\"0.1.0\",\"stdlibVersion\":\"0.1.0\","
        "\"target\":{\"platform\":\"windows-x64\",\"engine\":\"v8\"},"
        "\"bundle\":{\"path\":\"app.js\",\"id\":\"app-js\",\"hash\":\"test\"},"
        "\"sourceMap\":{\"path\":\"app.js.map\",\"id\":\"app-map\",\"hash\":\"test\"},"
        "\"handlers\":[{\"id\":1,\"exportName\":\"__sloppy_handler_1\","
        "\"displayName\":\"Users.Create\"}],"
        "\"schemas\":["
        "{\"name\":\"UserCreate\",\"definition\":{\"kind\":\"object\",\"properties\":{}}},"
        "{\"name\":\"UserCreate\",\"definition\":{\"kind\":\"object\",\"properties\":{}}}]}",
        &plan, &diag, arena_storage, sizeof(arena_storage));

    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 14;
    }
    if (diag.code != SL_DIAG_INVALID_PLAN_FIELD ||
        !sl_str_equal(diag.message, sl_str_from_cstr("duplicate app plan schema name")))
    {
        return 15;
    }
    return 0;
}

static int test_body_schema_references_must_resolve(void)
{
    unsigned char arena_storage[TEST_ARENA_SIZE];
    SlPlan plan = {0};
    SlDiag diag = {0};
    SlStatus status = parse_inline_plan(
        "{\"schemaVersion\":1,\"compilerVersion\":\"sloppyc-test\","
        "\"runtimeMinimumVersion\":\"0.1.0\",\"stdlibVersion\":\"0.1.0\","
        "\"target\":{\"platform\":\"windows-x64\",\"engine\":\"v8\"},"
        "\"bundle\":{\"path\":\"app.js\",\"id\":\"app-js\",\"hash\":\"test\"},"
        "\"sourceMap\":{\"path\":\"app.js.map\",\"id\":\"app-map\",\"hash\":\"test\"},"
        "\"handlers\":[{\"id\":1,\"exportName\":\"__sloppy_handler_1\","
        "\"displayName\":\"Users.Create\"}],"
        "\"routes\":[{\"method\":\"POST\",\"pattern\":\"/users\",\"handlerId\":1,"
        "\"bindings\":[{\"kind\":\"body.json\",\"parameter\":\"input\","
        "\"schema\":\"MissingModel\"}]}],"
        "\"schemas\":[{\"name\":\"UserCreate\",\"definition\":{\"kind\":\"object\","
        "\"properties\":{}}}]}",
        &plan, &diag, arena_storage, sizeof(arena_storage));

    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 17;
    }
    if (diag.code != SL_DIAG_INVALID_PLAN_FIELD ||
        !sl_str_equal(diag.message,
                      sl_str_from_cstr("app plan route binding references missing schema")))
    {
        return 18;
    }
    return 0;
}

static int test_route_json_request_response_metadata_and_max_constraints_parse(void)
{
    unsigned char arena_storage[TEST_ARENA_SIZE];
    SlPlan plan = {0};
    SlDiag diag = {0};
    const SlPlanRoute* route = NULL;
    const SlPlanSchemaNode* name = NULL;
    SlStatus status = parse_inline_plan(
        "{\"schemaVersion\":1,\"compilerVersion\":\"sloppyc-test\","
        "\"runtimeMinimumVersion\":\"0.1.0\",\"stdlibVersion\":\"0.1.0\","
        "\"target\":{\"platform\":\"windows-x64\",\"engine\":\"v8\"},"
        "\"bundle\":{\"path\":\"app.js\",\"id\":\"app-js\",\"hash\":\"test\"},"
        "\"sourceMap\":{\"path\":\"app.js.map\",\"id\":\"app-map\",\"hash\":\"test\"},"
        "\"handlers\":[{\"id\":1,\"exportName\":\"__sloppy_handler_1\","
        "\"displayName\":\"Users.Create\"}],"
        "\"routes\":[{\"method\":\"POST\",\"pattern\":\"/users\",\"handlerId\":1,"
        "\"bindings\":[{\"kind\":\"body.json\",\"parameter\":\"input\","
        "\"schema\":\"UserCreate\"}],"
        "\"jsonRequest\":{\"mode\":\"native-schema\",\"schema\":\"UserCreate\","
        "\"materialization\":\"materialize-once\",\"unknownFields\":\"reject\","
        "\"maxBodyBytes\":4096,\"maxDepth\":16,\"maxStringBytes\":128,"
        "\"maxArrayLength\":32},"
        "\"jsonResponse\":{\"mode\":\"native-schema\",\"schema\":\"UserCreate\","
        "\"writer\":\"bounded\",\"contentType\":\"application/json\"}}],"
        "\"schemas\":[{\"name\":\"UserCreate\",\"definition\":{\"kind\":\"object\","
        "\"properties\":{\"name\":{\"kind\":\"string\",\"min\":1,\"max\":5}}}}]}",
        &plan, &diag, arena_storage, sizeof(arena_storage));

    if (expect_status(status, SL_STATUS_OK) != 0 || diag.code != SL_DIAG_NONE) {
        return 19;
    }
    if (plan.route_count != 1U || plan.schema_count != 1U) {
        return 20;
    }
    route = &plan.routes[0];
    if (route->json_request.mode != SL_PLAN_JSON_REQUEST_NATIVE_SCHEMA ||
        route->json_request.materialization != SL_PLAN_JSON_MATERIALIZATION_MATERIALIZE_ONCE ||
        route->json_request.unknown_fields != SL_PLAN_JSON_UNKNOWN_FIELDS_REJECT ||
        !sl_str_equal(route->json_request.schema, sl_str_from_cstr("UserCreate")) ||
        route->json_request.max_body_bytes != 4096U || route->json_request.max_depth != 16U ||
        route->json_request.max_string_bytes != 128U || route->json_request.max_array_length != 32U)
    {
        return 21;
    }
    if (route->json_response.mode != SL_PLAN_JSON_RESPONSE_NATIVE_SCHEMA ||
        route->json_response.writer != SL_PLAN_JSON_WRITER_BOUNDED ||
        !sl_str_equal(route->json_response.schema, sl_str_from_cstr("UserCreate")) ||
        !sl_str_is_empty(route->json_response.fallback_reason) ||
        !sl_str_equal(route->json_response.content_type, sl_str_from_cstr("application/json")))
    {
        return 22;
    }
    if (plan.schemas[0].definition.property_count != 1U) {
        return 23;
    }
    name = plan.schemas[0].definition.properties[0].schema;
    if (name == NULL || name->kind != SL_PLAN_SCHEMA_STRING || !name->has_min ||
        name->min_value != 1 || !name->has_max || name->max_value != 5)
    {
        return 24;
    }
    return 0;
}

static int test_legacy_route_without_json_metadata_normalizes_json_modes(void)
{
    unsigned char arena_storage[TEST_ARENA_SIZE];
    SlPlan plan = {0};
    SlDiag diag = {0};
    SlStatus status = parse_inline_plan(
        "{\"schemaVersion\":1,\"compilerVersion\":\"sloppyc-test\","
        "\"runtimeMinimumVersion\":\"0.1.0\",\"stdlibVersion\":\"0.1.0\","
        "\"target\":{\"platform\":\"windows-x64\",\"engine\":\"v8\"},"
        "\"bundle\":{\"path\":\"app.js\",\"id\":\"app-js\",\"hash\":\"test\"},"
        "\"sourceMap\":{\"path\":\"app.js.map\",\"id\":\"app-map\",\"hash\":\"test\"},"
        "\"handlers\":[{\"id\":1,\"exportName\":\"__sloppy_handler_1\","
        "\"displayName\":\"Home\"}],"
        "\"routes\":[{\"method\":\"GET\",\"pattern\":\"/health\",\"handlerId\":1}]}",
        &plan, &diag, arena_storage, sizeof(arena_storage));

    if (expect_status(status, SL_STATUS_OK) != 0 || diag.code != SL_DIAG_NONE) {
        return 25;
    }
    if (plan.route_count != 1U || plan.routes[0].json_request.mode != SL_PLAN_JSON_REQUEST_NONE ||
        plan.routes[0].json_request.materialization != SL_PLAN_JSON_MATERIALIZATION_NONE ||
        plan.routes[0].json_response.mode != SL_PLAN_JSON_RESPONSE_NONE ||
        plan.routes[0].json_response.writer != SL_PLAN_JSON_WRITER_NONE)
    {
        return 26;
    }
    return 0;
}

static int test_route_health_metadata_parses(void)
{
    unsigned char arena_storage[TEST_ARENA_SIZE];
    SlPlan plan = {0};
    SlDiag diag = {0};
    const SlPlanRoute* route = NULL;
    SlStatus status = parse_inline_plan(
        "{\"schemaVersion\":1,\"compilerVersion\":\"sloppyc-test\","
        "\"runtimeMinimumVersion\":\"0.1.0\",\"stdlibVersion\":\"0.1.0\","
        "\"target\":{\"platform\":\"windows-x64\",\"engine\":\"v8\"},"
        "\"bundle\":{\"path\":\"app.js\",\"id\":\"app-js\",\"hash\":\"test\"},"
        "\"sourceMap\":{\"path\":\"app.js.map\",\"id\":\"app-map\",\"hash\":\"test\"},"
        "\"handlers\":[{\"id\":1,\"exportName\":\"__sloppy_handler_1\","
        "\"displayName\":\"Management.Ready\"}],"
        "\"routes\":[{\"method\":\"GET\",\"pattern\":\"/_sloppy/ready\",\"handlerId\":1,"
        "\"health\":{\"kind\":\"readiness\",\"checks\":[\"self\",\"runtime\"]}}]}",
        &plan, &diag, arena_storage, sizeof(arena_storage));

    if (expect_status(status, SL_STATUS_OK) != 0 || diag.code != SL_DIAG_NONE) {
        return 27;
    }
    if (plan.route_count != 1U) {
        return 28;
    }
    route = &plan.routes[0];
    if (!sl_str_equal(route->health_kind, sl_str_from_cstr("readiness"))) {
        return 29;
    }
    if (route->health_check_count != 2U) {
        return 30;
    }
    if (route->health_checks == NULL) {
        return 31;
    }
    if (!sl_str_equal(route->health_checks[0], sl_str_from_cstr("self"))) {
        return 32;
    }
    if (!sl_str_equal(route->health_checks[1], sl_str_from_cstr("runtime"))) {
        return 33;
    }
    return 0;
}

static int test_route_health_kind_must_be_supported(void)
{
    return expect_inline_plan_failure(
        "{\"schemaVersion\":1,\"compilerVersion\":\"sloppyc-test\","
        "\"runtimeMinimumVersion\":\"0.1.0\",\"stdlibVersion\":\"0.1.0\","
        "\"target\":{\"platform\":\"windows-x64\",\"engine\":\"v8\"},"
        "\"bundle\":{\"path\":\"app.js\",\"id\":\"app-js\",\"hash\":\"test\"},"
        "\"sourceMap\":{\"path\":\"app.js.map\",\"id\":\"app-map\",\"hash\":\"test\"},"
        "\"handlers\":[{\"id\":1,\"exportName\":\"__sloppy_handler_1\","
        "\"displayName\":\"Management.Ready\"}],"
        "\"routes\":[{\"method\":\"GET\",\"pattern\":\"/_sloppy/ready\",\"handlerId\":1,"
        "\"health\":{\"kind\":\"bogus\",\"checks\":[\"self\"]}}]}",
        SL_STATUS_INVALID_ARGUMENT, SL_DIAG_INVALID_PLAN_FIELD, "invalid route health kind");
}

static int test_route_health_checks_entries_must_be_strings(void)
{
    return expect_inline_plan_failure(
        "{\"schemaVersion\":1,\"compilerVersion\":\"sloppyc-test\","
        "\"runtimeMinimumVersion\":\"0.1.0\",\"stdlibVersion\":\"0.1.0\","
        "\"target\":{\"platform\":\"windows-x64\",\"engine\":\"v8\"},"
        "\"bundle\":{\"path\":\"app.js\",\"id\":\"app-js\",\"hash\":\"test\"},"
        "\"sourceMap\":{\"path\":\"app.js.map\",\"id\":\"app-map\",\"hash\":\"test\"},"
        "\"handlers\":[{\"id\":1,\"exportName\":\"__sloppy_handler_1\","
        "\"displayName\":\"Management.Ready\"}],"
        "\"routes\":[{\"method\":\"GET\",\"pattern\":\"/_sloppy/ready\",\"handlerId\":1,"
        "\"health\":{\"kind\":\"readiness\",\"checks\":[\"self\",1]}}]}",
        SL_STATUS_INVALID_ARGUMENT, SL_DIAG_INVALID_PLAN_FIELD, "invalid app plan field type");
}

static int test_route_health_checks_must_not_be_empty_when_present(void)
{
    return expect_inline_plan_failure(
        "{\"schemaVersion\":1,\"compilerVersion\":\"sloppyc-test\","
        "\"runtimeMinimumVersion\":\"0.1.0\",\"stdlibVersion\":\"0.1.0\","
        "\"target\":{\"platform\":\"windows-x64\",\"engine\":\"v8\"},"
        "\"bundle\":{\"path\":\"app.js\",\"id\":\"app-js\",\"hash\":\"test\"},"
        "\"sourceMap\":{\"path\":\"app.js.map\",\"id\":\"app-map\",\"hash\":\"test\"},"
        "\"handlers\":[{\"id\":1,\"exportName\":\"__sloppy_handler_1\","
        "\"displayName\":\"Management.Ready\"}],"
        "\"routes\":[{\"method\":\"GET\",\"pattern\":\"/_sloppy/ready\",\"handlerId\":1,"
        "\"health\":{\"kind\":\"readiness\",\"checks\":[]}}]}",
        SL_STATUS_INVALID_ARGUMENT, SL_DIAG_INVALID_PLAN_FIELD, "invalid route health checks");
}

static int test_empty_bindings_marks_route_metadata_available(void)
{
    unsigned char arena_storage[TEST_ARENA_SIZE];
    SlPlan plan = {0};
    SlDiag diag = {0};
    SlStatus status = parse_inline_plan(
        "{\"schemaVersion\":1,\"compilerVersion\":\"sloppyc-test\","
        "\"runtimeMinimumVersion\":\"0.1.0\",\"stdlibVersion\":\"0.1.0\","
        "\"target\":{\"platform\":\"windows-x64\",\"engine\":\"v8\"},"
        "\"bundle\":{\"path\":\"app.js\",\"id\":\"app-js\",\"hash\":\"test\"},"
        "\"sourceMap\":{\"path\":\"app.js.map\",\"id\":\"app-map\",\"hash\":\"test\"},"
        "\"handlers\":[{\"id\":1,\"exportName\":\"__sloppy_handler_1\","
        "\"displayName\":\"Home\"}],"
        "\"routes\":[{\"method\":\"GET\",\"pattern\":\"/health\",\"handlerId\":1,"
        "\"bindings\":[]}]}",
        &plan, &diag, arena_storage, sizeof(arena_storage));

    if (expect_status(status, SL_STATUS_OK) != 0 || diag.code != SL_DIAG_NONE) {
        return 19;
    }
    if (plan.route_count != 1U || !sl_plan_route_has_bindings(&plan.routes[0]) ||
        plan.routes[0].binding_count != 0U ||
        plan.routes[0].bindings != SL_PLAN_ROUTE_EMPTY_BINDINGS)
    {
        return 20;
    }
    return 0;
}

static int test_route_middleware_marks_context_needs_conservative(void)
{
    unsigned char arena_storage[TEST_ARENA_SIZE];
    SlPlan plan = {0};
    SlDiag diag = {0};
    SlStatus status = parse_inline_plan(
        "{\"schemaVersion\":1,\"compilerVersion\":\"sloppyc-test\","
        "\"runtimeMinimumVersion\":\"0.1.0\",\"stdlibVersion\":\"0.1.0\","
        "\"target\":{\"platform\":\"windows-x64\",\"engine\":\"v8\"},"
        "\"bundle\":{\"path\":\"app.js\",\"id\":\"app-js\",\"hash\":\"test\"},"
        "\"sourceMap\":{\"path\":\"app.js.map\",\"id\":\"app-map\",\"hash\":\"test\"},"
        "\"handlers\":[{\"id\":1,\"exportName\":\"__sloppy_handler_1\","
        "\"displayName\":\"Home\"}],"
        "\"routes\":[{\"method\":\"GET\",\"pattern\":\"/health\",\"handlerId\":1,"
        "\"bindings\":[],\"middleware\":[{\"kind\":\"requestLogging\","
        "\"sequence\":0}]}]}",
        &plan, &diag, arena_storage, sizeof(arena_storage));

    if (expect_status(status, SL_STATUS_OK) != 0 || diag.code != SL_DIAG_NONE) {
        return 96;
    }
    if (plan.route_count != 1U || !sl_plan_route_has_bindings(&plan.routes[0]) ||
        plan.routes[0].binding_count != 1U ||
        plan.routes[0].bindings[0].kind != SL_PLAN_REQUEST_BINDING_CONTEXT)
    {
        return 97;
    }
    return 0;
}

static int test_route_binding_kind_must_be_supported(void)
{
    return expect_inline_plan_failure(
        "{\"schemaVersion\":1,\"compilerVersion\":\"sloppyc-test\","
        "\"runtimeMinimumVersion\":\"0.1.0\",\"stdlibVersion\":\"0.1.0\","
        "\"target\":{\"platform\":\"windows-x64\",\"engine\":\"v8\"},"
        "\"bundle\":{\"path\":\"app.js\",\"id\":\"app-js\",\"hash\":\"test\"},"
        "\"sourceMap\":{\"path\":\"app.js.map\",\"id\":\"app-map\",\"hash\":\"test\"},"
        "\"handlers\":[{\"id\":1,\"exportName\":\"__sloppy_handler_1\","
        "\"displayName\":\"Users.Get\"}],"
        "\"routes\":[{\"method\":\"GET\",\"pattern\":\"/users/{id}\",\"handlerId\":1,"
        "\"bindings\":[{\"kind\":\"unsupported\",\"name\":\"session\"}]}]}",
        SL_STATUS_INVALID_ARGUMENT, SL_DIAG_INVALID_PLAN_FIELD, "unsupported route binding kind");
}

static int test_route_bindings_must_be_an_array(void)
{
    return expect_inline_plan_failure(
        "{\"schemaVersion\":1,\"compilerVersion\":\"sloppyc-test\","
        "\"runtimeMinimumVersion\":\"0.1.0\",\"stdlibVersion\":\"0.1.0\","
        "\"target\":{\"platform\":\"windows-x64\",\"engine\":\"v8\"},"
        "\"bundle\":{\"path\":\"app.js\",\"id\":\"app-js\",\"hash\":\"test\"},"
        "\"sourceMap\":{\"path\":\"app.js.map\",\"id\":\"app-map\",\"hash\":\"test\"},"
        "\"handlers\":[{\"id\":1,\"exportName\":\"__sloppy_handler_1\","
        "\"displayName\":\"Users.Get\"}],"
        "\"routes\":[{\"method\":\"GET\",\"pattern\":\"/users\",\"handlerId\":1,"
        "\"bindings\":{}}]}",
        SL_STATUS_INVALID_ARGUMENT, SL_DIAG_INVALID_PLAN_FIELD, "invalid app plan field type");
}

static int test_array_schemas_require_items_metadata(void)
{
    return expect_inline_plan_failure(
        "{\"schemaVersion\":1,\"compilerVersion\":\"sloppyc-test\","
        "\"runtimeMinimumVersion\":\"0.1.0\",\"stdlibVersion\":\"0.1.0\","
        "\"target\":{\"platform\":\"windows-x64\",\"engine\":\"v8\"},"
        "\"bundle\":{\"path\":\"app.js\",\"id\":\"app-js\",\"hash\":\"test\"},"
        "\"sourceMap\":{\"path\":\"app.js.map\",\"id\":\"app-map\",\"hash\":\"test\"},"
        "\"handlers\":[{\"id\":1,\"exportName\":\"__sloppy_handler_1\","
        "\"displayName\":\"Users.List\"}],"
        "\"schemas\":[{\"name\":\"Tags\",\"definition\":{\"kind\":\"array\"}}]}",
        SL_STATUS_INVALID_ARGUMENT, SL_DIAG_INVALID_PLAN_FIELD,
        "missing schema array item metadata");
}

static int test_literal_union_variants_must_be_an_array(void)
{
    return expect_inline_plan_failure(
        "{\"schemaVersion\":1,\"compilerVersion\":\"sloppyc-test\","
        "\"runtimeMinimumVersion\":\"0.1.0\",\"stdlibVersion\":\"0.1.0\","
        "\"target\":{\"platform\":\"windows-x64\",\"engine\":\"v8\"},"
        "\"bundle\":{\"path\":\"app.js\",\"id\":\"app-js\",\"hash\":\"test\"},"
        "\"sourceMap\":{\"path\":\"app.js.map\",\"id\":\"app-map\",\"hash\":\"test\"},"
        "\"handlers\":[{\"id\":1,\"exportName\":\"__sloppy_handler_1\","
        "\"displayName\":\"Users.List\"}],"
        "\"schemas\":[{\"name\":\"Role\",\"definition\":{\"kind\":\"literalUnion\","
        "\"variants\":{}}}]}",
        SL_STATUS_INVALID_ARGUMENT, SL_DIAG_INVALID_PLAN_FIELD, "invalid app plan field type");
}

static int test_literal_union_variants_must_not_be_empty(void)
{
    return expect_inline_plan_failure(
        "{\"schemaVersion\":1,\"compilerVersion\":\"sloppyc-test\","
        "\"runtimeMinimumVersion\":\"0.1.0\",\"stdlibVersion\":\"0.1.0\","
        "\"target\":{\"platform\":\"windows-x64\",\"engine\":\"v8\"},"
        "\"bundle\":{\"path\":\"app.js\",\"id\":\"app-js\",\"hash\":\"test\"},"
        "\"sourceMap\":{\"path\":\"app.js.map\",\"id\":\"app-map\",\"hash\":\"test\"},"
        "\"handlers\":[{\"id\":1,\"exportName\":\"__sloppy_handler_1\","
        "\"displayName\":\"Users.List\"}],"
        "\"schemas\":[{\"name\":\"Role\",\"definition\":{\"kind\":\"literalUnion\","
        "\"variants\":[]}}]}",
        SL_STATUS_INVALID_ARGUMENT, SL_DIAG_INVALID_PLAN_FIELD, "missing literal union variants");
}

static int test_literal_schema_values_must_use_supported_json_scalars(void)
{
    return expect_inline_plan_failure(
        "{\"schemaVersion\":1,\"compilerVersion\":\"sloppyc-test\","
        "\"runtimeMinimumVersion\":\"0.1.0\",\"stdlibVersion\":\"0.1.0\","
        "\"target\":{\"platform\":\"windows-x64\",\"engine\":\"v8\"},"
        "\"bundle\":{\"path\":\"app.js\",\"id\":\"app-js\",\"hash\":\"test\"},"
        "\"sourceMap\":{\"path\":\"app.js.map\",\"id\":\"app-map\",\"hash\":\"test\"},"
        "\"handlers\":[{\"id\":1,\"exportName\":\"__sloppy_handler_1\","
        "\"displayName\":\"Users.List\"}],"
        "\"schemas\":[{\"name\":\"Role\",\"definition\":{\"kind\":\"literal\","
        "\"value\":null}}]}",
        SL_STATUS_INVALID_ARGUMENT, SL_DIAG_INVALID_PLAN_FIELD, "unsupported literal schema value");
}

static int test_route_middleware_must_be_an_array(void)
{
    return expect_inline_plan_failure(
        "{\"schemaVersion\":1,\"compilerVersion\":\"sloppyc-test\","
        "\"runtimeMinimumVersion\":\"0.1.0\",\"stdlibVersion\":\"0.1.0\","
        "\"target\":{\"platform\":\"windows-x64\",\"engine\":\"v8\"},"
        "\"bundle\":{\"path\":\"app.js\",\"id\":\"app-js\",\"hash\":\"test\"},"
        "\"sourceMap\":{\"path\":\"app.js.map\",\"id\":\"app-map\",\"hash\":\"test\"},"
        "\"handlers\":[{\"id\":1,\"exportName\":\"__sloppy_handler_1\","
        "\"displayName\":\"Home\"}],"
        "\"routes\":[{\"method\":\"GET\",\"pattern\":\"/health\",\"handlerId\":1,"
        "\"middleware\":{}}]}",
        SL_STATUS_INVALID_ARGUMENT, SL_DIAG_INVALID_PLAN_FIELD, "invalid app plan field type");
}

static int test_invalid_fixture_matrix(void)
{
    static const InvalidFixtureCase cases[] = {
        {"tests/golden/plan/malformed-json.plan.json", SL_STATUS_INVALID_ARGUMENT,
         SL_DIAG_MALFORMED_JSON, "malformed app plan JSON"},
        {"tests/golden/plan/invalid-version.plan.json", SL_STATUS_UNSUPPORTED,
         SL_DIAG_INVALID_PLAN_VERSION, "invalid app plan version"},
        {"tests/golden/plan/required-features-not-array.plan.json", SL_STATUS_INVALID_ARGUMENT,
         SL_DIAG_INVALID_PLAN_FIELD, "invalid app plan field type"},
        {"tests/golden/plan/missing-runtime-minimum-version.plan.json", SL_STATUS_INVALID_ARGUMENT,
         SL_DIAG_INVALID_PLAN_FIELD, "missing required app plan field"},
        {"tests/golden/plan/missing-bundle.plan.json", SL_STATUS_INVALID_ARGUMENT,
         SL_DIAG_INVALID_PLAN_FIELD, "missing required app plan field"},
        {"tests/golden/plan/missing-bundle-path.plan.json", SL_STATUS_INVALID_ARGUMENT,
         SL_DIAG_INVALID_PLAN_FIELD, "missing required app plan field"},
        {"tests/golden/plan/missing-source-map.plan.json", SL_STATUS_INVALID_ARGUMENT,
         SL_DIAG_INVALID_PLAN_FIELD, "missing required app plan field"},
        {"tests/golden/plan/missing-handlers.plan.json", SL_STATUS_INVALID_ARGUMENT,
         SL_DIAG_INVALID_PLAN_FIELD, "missing required app plan field"},
        {"tests/golden/plan/empty-handlers.plan.json", SL_STATUS_INVALID_ARGUMENT,
         SL_DIAG_INVALID_PLAN_FIELD, "missing required app plan field"},
        {"tests/golden/plan/invalid-handler-id.plan.json", SL_STATUS_INVALID_ARGUMENT,
         SL_DIAG_INVALID_PLAN_FIELD, "invalid app plan handler id"},
        {"tests/golden/plan/duplicate-handler-id.plan.json", SL_STATUS_INVALID_ARGUMENT,
         SL_DIAG_DUPLICATE_HANDLER_ID, "duplicate handler id"},
        {"tests/golden/plan/missing-handler-export.plan.json", SL_STATUS_INVALID_ARGUMENT,
         SL_DIAG_INVALID_PLAN_FIELD, "invalid handler export name"},
        {"tests/golden/plan/empty-handler-export.plan.json", SL_STATUS_INVALID_ARGUMENT,
         SL_DIAG_INVALID_PLAN_FIELD, "invalid handler export name"},
        {"tests/golden/plan/wrong-field-type.plan.json", SL_STATUS_INVALID_ARGUMENT,
         SL_DIAG_INVALID_PLAN_FIELD, "invalid app plan field type"},
        {"tests/golden/plan/invalid-route-method.plan.json", SL_STATUS_INVALID_ARGUMENT,
         SL_DIAG_INVALID_PLAN_FIELD, "unsupported app plan route method"},
        {"tests/golden/plan/invalid-route-pattern.plan.json", SL_STATUS_INVALID_ARGUMENT,
         SL_DIAG_INVALID_ROUTE_PATTERN, "invalid app plan route pattern"},
        {"tests/golden/plan/missing-route-handler.plan.json", SL_STATUS_INVALID_ARGUMENT,
         SL_DIAG_INVALID_PLAN_FIELD, "app plan route references missing handler"},
        {"tests/golden/plan/duplicate-route.plan.json", SL_STATUS_INVALID_ARGUMENT,
         SL_DIAG_INVALID_PLAN_FIELD, "duplicate app plan route"},
        {"tests/golden/plan/duplicate-route-name.plan.json", SL_STATUS_INVALID_ARGUMENT,
         SL_DIAG_INVALID_PLAN_FIELD, "duplicate app plan route name"},
        {"tests/golden/plan/invalid-provider-kind.plan.json", SL_STATUS_INVALID_ARGUMENT,
         SL_DIAG_INVALID_PLAN_FIELD, "unsupported app plan provider"},
        {"tests/golden/plan/duplicate-provider-token.plan.json", SL_STATUS_INVALID_ARGUMENT,
         SL_DIAG_INVALID_PLAN_FIELD, "duplicate app plan provider token"},
        {"tests/golden/plan/invalid-capability-kind.plan.json", SL_STATUS_INVALID_ARGUMENT,
         SL_DIAG_INVALID_PLAN_FIELD, "unsupported app plan capability kind"},
        {"tests/golden/plan/invalid-capability-access.plan.json", SL_STATUS_INVALID_ARGUMENT,
         SL_DIAG_INVALID_PLAN_FIELD, "unsupported app plan capability access"},
        {"tests/golden/plan/missing-capability-token.plan.json", SL_STATUS_INVALID_ARGUMENT,
         SL_DIAG_INVALID_PLAN_FIELD, "missing required app plan field"},
        {"tests/golden/plan/missing-capability-provider.plan.json", SL_STATUS_INVALID_ARGUMENT,
         SL_DIAG_INVALID_PLAN_FIELD, "database capability is missing provider"},
        {"tests/golden/plan/non-database-capability-provider.plan.json", SL_STATUS_INVALID_ARGUMENT,
         SL_DIAG_INVALID_PLAN_FIELD, "non-database capability has provider"},
        {"tests/golden/plan/secret-bearing-provider-field.plan.json", SL_STATUS_INVALID_ARGUMENT,
         SL_DIAG_INVALID_PLAN_FIELD, "app plan contains secret-bearing field"},
        {"tests/golden/plan/duplicate-capability-token.plan.json", SL_STATUS_INVALID_ARGUMENT,
         SL_DIAG_INVALID_PLAN_FIELD, "duplicate app plan capability token"}};
    size_t index = 0U;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index += 1U) {
        unsigned char arena_storage[TEST_ARENA_SIZE];
        SlPlan plan = {0};
        SlDiag diag = {0};
        SlStatus status =
            parse_fixture(cases[index].path, &plan, &diag, arena_storage, sizeof(arena_storage));

        if (expect_status(status, cases[index].status_code) != 0) {
            return 70 + (int)index;
        }

        if (diag.severity != SL_DIAG_SEVERITY_ERROR || diag.code != cases[index].diag_code) {
            return 90 + (int)index;
        }

        if (!sl_str_equal(diag.message, sl_str_from_cstr(cases[index].message))) {
            return 110 + (int)index;
        }

        if (!sl_str_equal(diag.primary_span.path, sl_str_from_cstr(cases[index].path))) {
            return 130 + (int)index;
        }

        if (plan.version != 0U || plan.handlers != NULL || plan.handler_count != 0U ||
            plan.routes != NULL || plan.route_count != 0U || plan.data_providers != NULL ||
            plan.data_provider_count != 0U || plan.capabilities != NULL ||
            plan.capability_count != 0U || plan.required_features != NULL ||
            plan.required_feature_count != 0U)
        {
            return 150 + (int)index;
        }
    }

    return 0;
}

static int test_secret_field_rejection_checks_full_normalized_key(void)
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
        "\"handlers\":[{\"id\":1,\"exportName\":\"__sloppy_handler_1\","
        "\"displayName\":\"Home\"}],"
        "\"dataProviders\":[{"
        "\"token\":\"data.main\","
        "\"provider\":\"sqlite\","
        "\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaPassword\":\"redacted\""
        "}]"
        "}";
    unsigned char arena_storage[TEST_ARENA_SIZE];
    SlPlan plan = {0};
    SlDiag diag = {0};
    SlStatus status = parse_inline_plan(json, &plan, &diag, arena_storage, sizeof(arena_storage));

    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 1;
    }
    if (diag.severity != SL_DIAG_SEVERITY_ERROR || diag.code != SL_DIAG_INVALID_PLAN_FIELD ||
        !sl_str_equal(diag.message, sl_str_from_cstr("app plan contains secret-bearing field")))
    {
        return 2;
    }

    return 0;
}

static int test_failed_parse_rolls_back_plan_allocations(void)
{
    unsigned char json_storage[TEST_JSON_SIZE];
    unsigned char arena_storage[TEST_ARENA_SIZE];
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
        return 200;
    }

    status = sl_arena_init(&arena, arena_storage, sizeof(arena_storage));
    if (!sl_status_is_ok(status)) {
        return 201;
    }

    used_before = sl_arena_used(&arena);
    status = sl_plan_parse_json(&arena, json, NULL, &plan, NULL);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 202;
    }

    used_after_no_diag = sl_arena_used(&arena);
    if (used_after_no_diag != used_before || plan.handlers != NULL || plan.handler_count != 0U) {
        return 203;
    }

    options.source_name = sl_str_from_cstr("tests/golden/plan/missing-handler-export.plan.json");
    status = sl_plan_parse_json(&arena, json, &options, &plan, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 204;
    }

    used_after_diag = sl_arena_used(&arena);
    if (used_after_diag <= used_after_no_diag) {
        return 205;
    }

    if (plan.handlers != NULL || plan.handler_count != 0U) {
        return 206;
    }

    if (diag.severity != SL_DIAG_SEVERITY_ERROR || diag.code != SL_DIAG_INVALID_PLAN_FIELD ||
        !sl_str_equal(diag.message, sl_str_from_cstr("invalid handler export name")) ||
        !sl_str_equal(diag.primary_span.path, options.source_name))
    {
        return 207;
    }

    return 0;
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
    unsigned char arena_storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlPlan plan = {0};
    SlDiag diag = {0};
    SlStatus status;

    status = sl_arena_init(&arena, arena_storage, sizeof(arena_storage));
    if (!sl_status_is_ok(status)) {
        return 220;
    }

    status = sl_plan_parse_json(&arena, bytes_from_cstr(json), NULL, &plan, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 221;
    }

    if (diag.severity != SL_DIAG_SEVERITY_ERROR || diag.code != SL_DIAG_INVALID_PLAN_FIELD) {
        return 222;
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
        return 230;
    }

    if (expect_status(sl_plan_parse_json(NULL, bytes_from_cstr("{}"), NULL, &plan, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 231;
    }

    if (expect_status(sl_plan_parse_json(&arena, sl_bytes_empty(), NULL, &plan, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 232;
    }

    if (expect_status(sl_plan_parse_json(&arena, bytes_from_cstr("{}"), NULL, NULL, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 233;
    }

    return 0;
}

int main(void)
{
    int result = 0;

    result = test_valid_fixture_matrix();
    if (result != 0) {
        fprintf(stderr, "test_valid_fixture_matrix failed: %d\n", result);
        fflush(stderr);
        return result;
    }

    result = test_valid_filesystem_capability_accesses_fixture();
    if (result != 0) {
        fprintf(stderr, "test_valid_filesystem_capability_accesses_fixture failed: %d\n", result);
        fflush(stderr);
        return result;
    }

    result = test_valid_os_capability_accesses_fixture();
    if (result != 0) {
        fprintf(stderr, "test_valid_os_capability_accesses_fixture failed: %d\n", result);
        return result;
    }

    result = test_framework_metadata_bindings_and_schemas_fixture();
    if (result != 0) {
        fprintf(stderr, "test_framework_v2_metadata_bindings_and_schemas_fixture failed: %d\n",
                result);
        return result;
    }

    result = test_schema_names_must_be_unique();
    if (result != 0) {
        fprintf(stderr, "test_schema_names_must_be_unique failed: %d\n", result);
        return result;
    }

    result = test_body_schema_references_must_resolve();
    if (result != 0) {
        fprintf(stderr, "test_body_schema_references_must_resolve failed: %d\n", result);
        return result;
    }

    result = test_route_json_request_response_metadata_and_max_constraints_parse();
    if (result != 0) {
        fprintf(stderr,
                "test_route_json_request_response_metadata_and_max_constraints_parse failed: %d\n",
                result);
        return result;
    }

    result = test_legacy_route_without_json_metadata_normalizes_json_modes();
    if (result != 0) {
        fprintf(stderr,
                "test_legacy_route_without_json_metadata_normalizes_json_modes failed: %d\n",
                result);
        return result;
    }

    result = test_route_health_metadata_parses();
    if (result != 0) {
        fprintf(stderr, "test_route_health_metadata_parses failed: %d\n", result);
        return result;
    }

    result = test_route_health_kind_must_be_supported();
    if (result != 0) {
        fprintf(stderr, "test_route_health_kind_must_be_supported failed: %d\n", result);
        return result;
    }

    result = test_route_health_checks_entries_must_be_strings();
    if (result != 0) {
        fprintf(stderr, "test_route_health_checks_entries_must_be_strings failed: %d\n", result);
        return result;
    }

    result = test_route_health_checks_must_not_be_empty_when_present();
    if (result != 0) {
        fprintf(stderr, "test_route_health_checks_must_not_be_empty_when_present failed: %d\n",
                result);
        return result;
    }

    result = test_empty_bindings_marks_route_metadata_available();
    if (result != 0) {
        fprintf(stderr, "test_empty_bindings_marks_route_metadata_available failed: %d\n", result);
        return result;
    }

    result = test_program_kind_allows_empty_handlers_and_routes();
    if (result != 0) {
        fprintf(stderr, "test_program_kind_allows_empty_handlers_and_routes failed: %d\n", result);
        return result;
    }

    result = test_native_ffi_metadata_parses_typed_libraries_and_structs();
    if (result != 0) {
        fprintf(stderr, "test_native_ffi_metadata_parses_typed_libraries_and_structs failed: %d\n",
                result);
        return result;
    }
    result = test_native_ffi_function_parameters_reject_void();
    if (result != 0) {
        fprintf(stderr, "test_native_ffi_function_parameters_reject_void failed: %d\n", result);
        return result;
    }
    result = test_native_ffi_struct_fields_reject_unsized_types();
    if (result != 0) {
        fprintf(stderr, "test_native_ffi_struct_fields_reject_unsized_types failed: %d\n", result);
        return result;
    }

    result = test_program_kind_rejects_non_empty_handlers();
    if (result != 0) {
        fprintf(stderr, "test_program_kind_rejects_non_empty_handlers failed: %d\n", result);
        return result;
    }

    result = test_program_kind_rejects_non_empty_routes();
    if (result != 0) {
        fprintf(stderr, "test_program_kind_rejects_non_empty_routes failed: %d\n", result);
        return result;
    }

    result = test_missing_plan_kind_defaults_to_web();
    if (result != 0) {
        fprintf(stderr, "test_missing_plan_kind_defaults_to_web failed: %d\n", result);
        return result;
    }

    result = test_invalid_plan_kind_is_rejected();
    if (result != 0) {
        fprintf(stderr, "test_invalid_plan_kind_is_rejected failed: %d\n", result);
        return result;
    }

    result = test_numeric_plan_kind_is_rejected();
    if (result != 0) {
        fprintf(stderr, "test_numeric_plan_kind_is_rejected failed: %d\n", result);
        return result;
    }

    result = test_route_middleware_marks_context_needs_conservative();
    if (result != 0) {
        fprintf(stderr, "test_route_middleware_marks_context_needs_conservative failed: %d\n",
                result);
        return result;
    }

    result = test_route_binding_kind_must_be_supported();
    if (result != 0) {
        fprintf(stderr, "test_route_binding_kind_must_be_supported failed: %d\n", result);
        return result;
    }

    result = test_route_bindings_must_be_an_array();
    if (result != 0) {
        fprintf(stderr, "test_route_bindings_must_be_an_array failed: %d\n", result);
        return result;
    }

    result = test_array_schemas_require_items_metadata();
    if (result != 0) {
        fprintf(stderr, "test_array_schemas_require_items_metadata failed: %d\n", result);
        return result;
    }

    result = test_literal_union_variants_must_be_an_array();
    if (result != 0) {
        fprintf(stderr, "test_literal_union_variants_must_be_an_array failed: %d\n", result);
        return result;
    }

    result = test_literal_union_variants_must_not_be_empty();
    if (result != 0) {
        fprintf(stderr, "test_literal_union_variants_must_not_be_empty failed: %d\n", result);
        return result;
    }

    result = test_literal_schema_values_must_use_supported_json_scalars();
    if (result != 0) {
        fprintf(stderr, "test_literal_schema_values_must_use_supported_json_scalars failed: %d\n",
                result);
        return result;
    }

    result = test_route_middleware_must_be_an_array();
    if (result != 0) {
        fprintf(stderr, "test_route_middleware_must_be_an_array failed: %d\n", result);
        return result;
    }

    result = test_invalid_fixture_matrix();
    if (result != 0) {
        fprintf(stderr, "test_invalid_fixture_matrix failed: %d\n", result);
        return result;
    }

    result = test_secret_field_rejection_checks_full_normalized_key();
    if (result != 0) {
        return result;
    }

    result = test_failed_parse_rolls_back_plan_allocations();
    if (result != 0) {
        fprintf(stderr, "test_failed_parse_rolls_back_plan_allocations failed: %d\n", result);
        return result;
    }

    result = test_wrong_handler_id_type_fails();
    if (result != 0) {
        fprintf(stderr, "test_wrong_handler_id_type_fails failed: %d\n", result);
        return result;
    }

    result = test_invalid_arguments();
    if (result != 0) {
        fprintf(stderr, "test_invalid_arguments failed: %d\n", result);
        return result;
    }
    return 0;
}

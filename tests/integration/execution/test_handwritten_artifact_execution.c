#include "sloppy/runtime_contract.h"

#include <stdio.h>

#define TEST_ARENA_SIZE 16384U
#define TEST_FILE_SIZE 8192U

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static int read_file(const char* path, unsigned char* buffer, size_t capacity, SlBytes* out)
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

static int init_arena(SlArena* arena, unsigned char* storage, size_t storage_size)
{
    return expect_status(sl_arena_init(arena, storage, storage_size), SL_STATUS_OK);
}

static SlEngineOptions v8_options(void)
{
    SlEngineOptions options = {0};

    options.kind = SL_ENGINE_KIND_V8;
    options.runtime_name = sl_str_from_cstr("sloppy-handwritten-artifact-test");
    options.runtime_version = sl_str_from_cstr("0.2.0-test");
    options.target_platform = sl_str_from_cstr(SL_PLAN_TARGET_PLATFORM_WINDOWS_X64);
    options.target_engine = sl_str_from_cstr(SL_PLAN_TARGET_ENGINE_V8);
    return options;
}

static int load_plan_from_path(const char* path, SlArena* plan_arena, SlPlan* out_plan,
                               SlDiag* out_diag)
{
    unsigned char json_storage[TEST_FILE_SIZE];
    SlBytes json = {0};
    SlPlanParseOptions options = {0};

    if (read_file(path, json_storage, sizeof(json_storage), &json) != 0) {
        return 1;
    }

    options.source_name = sl_str_from_cstr(path);
    return expect_status(sl_plan_parse_json(plan_arena, json, &options, out_plan, out_diag),
                         SL_STATUS_OK);
}

static int load_handwritten_plan(SlArena* plan_arena, SlPlan* out_plan, SlDiag* out_diag)
{
    return load_plan_from_path("tests/integration/execution/handwritten_smoke/app.plan.json",
                               plan_arena, out_plan, out_diag);
}

static int eval_app_from_path(const char* path, const char* source_name, SlEngine* engine,
                              SlDiag* out_diag)
{
    unsigned char js_storage[TEST_FILE_SIZE];
    SlBytes js = {0};
    SlStr source = {0};

    if (read_file(path, js_storage, sizeof(js_storage), &js) != 0) {
        return 1;
    }

    source = sl_str_from_parts((const char*)js.ptr, js.length);
    return expect_status(
        sl_engine_eval_source(engine, sl_str_from_cstr(source_name), source, out_diag),
        SL_STATUS_OK);
}

static int eval_handwritten_app(SlEngine* engine, SlDiag* out_diag)
{
    return eval_app_from_path("tests/integration/execution/handwritten_smoke/app.js",
                              "handwritten_smoke/app.js", engine, out_diag);
}

static int create_v8_engine(SlArena* engine_arena, SlEngine** out_engine)
{
    SlEngineOptions options = v8_options();
    return expect_status(sl_engine_create(&options, engine_arena, out_engine), SL_STATUS_OK);
}

static int test_handwritten_plan_and_app_call_handler_1(void)
{
    unsigned char engine_storage[TEST_ARENA_SIZE];
    unsigned char plan_storage[TEST_ARENA_SIZE];
    unsigned char result_storage[TEST_ARENA_SIZE];
    SlArena engine_arena = {0};
    SlArena plan_arena = {0};
    SlArena result_arena = {0};
    SlEngine* engine = NULL;
    SlPlan plan = {0};
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&plan_arena, plan_storage, sizeof(plan_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 1;
    }

    if (load_handwritten_plan(&plan_arena, &plan, &diag) != 0 ||
        create_v8_engine(&engine_arena, &engine) != 0 || eval_handwritten_app(engine, &diag) != 0)
    {
        sl_engine_destroy(engine);
        return 2;
    }

    if (expect_status(
            sl_runtime_contract_call_handler(engine, &result_arena, &plan, 1U, &result, &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 3;
    }

    if (result.kind != SL_ENGINE_RESULT_TEXT ||
        !sl_str_equal(result.text, sl_str_from_cstr("sloppy-ok")))
    {
        sl_engine_destroy(engine);
        return 4;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_compiler_mvp_plan_and_app_call_handler_1(void)
{
    unsigned char engine_storage[TEST_ARENA_SIZE];
    unsigned char plan_storage[TEST_ARENA_SIZE];
    unsigned char result_storage[TEST_ARENA_SIZE];
    SlArena engine_arena = {0};
    SlArena plan_arena = {0};
    SlArena result_arena = {0};
    SlEngine* engine = NULL;
    SlPlan plan = {0};
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&plan_arena, plan_storage, sizeof(plan_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 1;
    }

    if (load_plan_from_path("tests/integration/execution/compiler_mvp/app.plan.json", &plan_arena,
                            &plan, &diag) != 0 ||
        create_v8_engine(&engine_arena, &engine) != 0 ||
        eval_app_from_path("tests/integration/execution/compiler_mvp/app.js", "compiler_mvp/app.js",
                           engine, &diag) != 0)
    {
        sl_engine_destroy(engine);
        return 2;
    }

    if (expect_status(
            sl_runtime_contract_call_handler(engine, &result_arena, &plan, 1U, &result, &diag),
            SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 3;
    }

    if (result.kind != SL_ENGINE_RESULT_TEXT ||
        !sl_str_equal(result.text, sl_str_from_cstr("Hello from Sloppy")))
    {
        sl_engine_destroy(engine);
        return 4;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_missing_handler_id_returns_diagnostic(void)
{
    unsigned char engine_storage[TEST_ARENA_SIZE];
    unsigned char plan_storage[TEST_ARENA_SIZE];
    unsigned char result_storage[TEST_ARENA_SIZE];
    SlArena engine_arena = {0};
    SlArena plan_arena = {0};
    SlArena result_arena = {0};
    SlEngine* engine = NULL;
    SlPlan plan = {0};
    SlEngineResult result = {SL_ENGINE_RESULT_TEXT, sl_str_from_cstr("stale")};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&plan_arena, plan_storage, sizeof(plan_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 10;
    }

    if (load_handwritten_plan(&plan_arena, &plan, &diag) != 0 ||
        create_v8_engine(&engine_arena, &engine) != 0)
    {
        sl_engine_destroy(engine);
        return 11;
    }

    if (expect_status(
            sl_runtime_contract_call_handler(engine, &result_arena, &plan, 2U, &result, &diag),
            SL_STATUS_OUT_OF_RANGE) != 0)
    {
        sl_engine_destroy(engine);
        return 12;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_ENGINE_CALL_ERROR ||
        !sl_str_equal(diag.message, sl_str_from_cstr("app plan handler ID was not found")))
    {
        sl_engine_destroy(engine);
        return 13;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_missing_js_function_returns_diagnostic(void)
{
    unsigned char engine_storage[TEST_ARENA_SIZE];
    unsigned char plan_storage[TEST_ARENA_SIZE];
    unsigned char result_storage[TEST_ARENA_SIZE];
    SlArena engine_arena = {0};
    SlArena plan_arena = {0};
    SlArena result_arena = {0};
    SlEngine* engine = NULL;
    SlPlan plan = {0};
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&plan_arena, plan_storage, sizeof(plan_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 20;
    }

    if (load_handwritten_plan(&plan_arena, &plan, &diag) != 0 ||
        create_v8_engine(&engine_arena, &engine) != 0 ||
        expect_status(sl_engine_eval_source(engine, sl_str_from_cstr("missing-export.js"),
                                            sl_str_from_cstr("globalThis.other = function () { "
                                                             "return \"wrong\"; };"),
                                            &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 21;
    }

    if (expect_status(
            sl_runtime_contract_call_handler(engine, &result_arena, &plan, 1U, &result, &diag),
            SL_STATUS_INVALID_STATE) != 0)
    {
        sl_engine_destroy(engine);
        return 22;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_ENGINE_CALL_ERROR) {
        sl_engine_destroy(engine);
        return 23;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int test_throwing_js_handler_returns_diagnostic(void)
{
    unsigned char engine_storage[TEST_ARENA_SIZE];
    unsigned char plan_storage[TEST_ARENA_SIZE];
    unsigned char result_storage[TEST_ARENA_SIZE];
    SlArena engine_arena = {0};
    SlArena plan_arena = {0};
    SlArena result_arena = {0};
    SlEngine* engine = NULL;
    SlPlan plan = {0};
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&plan_arena, plan_storage, sizeof(plan_storage)) != 0 ||
        init_arena(&result_arena, result_storage, sizeof(result_storage)) != 0)
    {
        return 30;
    }

    if (load_handwritten_plan(&plan_arena, &plan, &diag) != 0 ||
        create_v8_engine(&engine_arena, &engine) != 0 ||
        expect_status(sl_engine_eval_source(engine, sl_str_from_cstr("throwing-handler.js"),
                                            sl_str_from_cstr("globalThis.__sloppy_handler_1 = "
                                                             "function () { throw new "
                                                             "Error(\"boom\"); };"),
                                            &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 31;
    }

    if (expect_status(
            sl_runtime_contract_call_handler(engine, &result_arena, &plan, 1U, &result, &diag),
            SL_STATUS_INVALID_STATE) != 0)
    {
        sl_engine_destroy(engine);
        return 32;
    }

    if (result.kind != SL_ENGINE_RESULT_NONE || diag.code != SL_DIAG_ENGINE_EXCEPTION) {
        sl_engine_destroy(engine);
        return 33;
    }

    sl_engine_destroy(engine);
    return 0;
}

int main(void)
{
    if (test_handwritten_plan_and_app_call_handler_1() != 0) {
        return 1;
    }

    if (test_compiler_mvp_plan_and_app_call_handler_1() != 0) {
        return 5;
    }

    if (test_missing_handler_id_returns_diagnostic() != 0) {
        return 2;
    }

    if (test_missing_js_function_returns_diagnostic() != 0) {
        return 3;
    }

    if (test_throwing_js_handler_returns_diagnostic() != 0) {
        return 4;
    }

    return 0;
}

#include "sloppy/diagnostics.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

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

static SlStatus make_arena(SlArena* arena, unsigned char* buffer, size_t size)
{
    return sl_arena_init(arena, buffer, size);
}

static int expect_snapshot(SlStr actual, const char* path)
{
    char expected[1024];
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

    if (fclose(file) != 0) {
        return 3;
    }

    if (length == sizeof(expected)) {
        return 4;
    }

    return expect_str_equal(actual, sl_str_from_parts(expected, length));
}

static int test_missing_service_snapshot(void)
{
    unsigned char buffer[2048];
    SlArena arena;
    SlDiagBuilder builder;
    SlDiag diag;
    SlStr rendered;

    if (expect_status(make_arena(&arena, buffer, sizeof(buffer)), SL_STATUS_OK) != 0) {
        return 10;
    }

    if (expect_status(sl_diag_builder_init(&builder, &arena, SL_DIAG_SEVERITY_ERROR,
                                           SL_DIAG_MISSING_SERVICE,
                                           sl_str_from_cstr("service not registered")),
                      SL_STATUS_OK) != 0)
    {
        return 11;
    }

    if (expect_status(
            sl_diag_builder_set_primary_span(
                &builder, sl_source_span_make(sl_str_from_cstr("src/users.ts"), 12U, 7U, 0U)),
            SL_STATUS_OK) != 0)
    {
        return 12;
    }

    if (expect_status(sl_diag_builder_add_related(
                          &builder, sl_source_span_make(sl_str_from_cstr("src/app.ts"), 4U, 1U, 0U),
                          sl_str_from_cstr("service registration happens here")),
                      SL_STATUS_OK) != 0)
    {
        return 13;
    }

    if (expect_status(sl_diag_builder_add_hint(
                          &builder, sl_str_from_cstr(
                                        "Register service \"data.main\" before building the app.")),
                      SL_STATUS_OK) != 0)
    {
        return 14;
    }

    if (expect_status(sl_diag_builder_finish(&builder, &diag), SL_STATUS_OK) != 0) {
        return 15;
    }

    if (expect_status(sl_diag_render_text(&arena, &diag, &rendered), SL_STATUS_OK) != 0) {
        return 16;
    }

    if (expect_snapshot(rendered, "tests/golden/diagnostics/missing_service.snap") != 0) {
        return 17;
    }

    return 0;
}

static int test_invalid_plan_version_snapshot(void)
{
    unsigned char buffer[2048];
    SlArena arena;
    SlDiagBuilder builder;
    SlDiag diag;
    SlStr rendered;

    if (expect_status(make_arena(&arena, buffer, sizeof(buffer)), SL_STATUS_OK) != 0) {
        return 20;
    }

    if (expect_status(sl_diag_builder_init(
                          &builder, &arena, SL_DIAG_SEVERITY_ERROR, SL_DIAG_INVALID_PLAN_VERSION,
                          sl_str_from_cstr("app.plan.json schema version is not supported")),
                      SL_STATUS_OK) != 0)
    {
        return 21;
    }

    if (expect_status(
            sl_diag_builder_set_primary_span(
                &builder, sl_source_span_make(sl_str_from_cstr("app.plan.json"), 1U, 1U, 0U)),
            SL_STATUS_OK) != 0)
    {
        return 22;
    }

    if (expect_status(
            sl_diag_builder_add_hint(
                &builder, sl_str_from_cstr("Rebuild the app with a compatible sloppyc version.")),
            SL_STATUS_OK) != 0)
    {
        return 23;
    }

    if (expect_status(sl_diag_builder_finish(&builder, &diag), SL_STATUS_OK) != 0) {
        return 24;
    }

    if (expect_status(sl_diag_render_text(&arena, &diag, &rendered), SL_STATUS_OK) != 0) {
        return 25;
    }

    if (expect_snapshot(rendered, "tests/golden/diagnostics/invalid_plan_version.snap") != 0) {
        return 26;
    }

    return 0;
}

static int test_names_and_spans(void)
{
    SlSourceSpan unknown = sl_source_span_unknown();
    SlSourceSpan partial = sl_source_span_make(sl_str_from_cstr("app.ts"), 0U, 5U, 0U);

    if (expect_str_equal(sl_diag_severity_name(SL_DIAG_SEVERITY_NOTE), sl_str_from_cstr("note")) !=
        0)
    {
        return 30;
    }

    if (expect_str_equal(sl_diag_severity_name(SL_DIAG_SEVERITY_WARNING),
                         sl_str_from_cstr("warning")) != 0)
    {
        return 31;
    }

    if (expect_str_equal(sl_diag_severity_name(SL_DIAG_SEVERITY_ERROR),
                         sl_str_from_cstr("error")) != 0)
    {
        return 32;
    }

    if (expect_str_equal(sl_diag_severity_name(SL_DIAG_SEVERITY_FATAL),
                         sl_str_from_cstr("fatal")) != 0)
    {
        return 33;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_NONE), sl_str_from_cstr("SLOPPY_NONE")) != 0) {
        return 35;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_INVALID_ARGUMENT),
                         sl_str_from_cstr("SLOPPY_E_INVALID_ARGUMENT")) != 0)
    {
        return 36;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_MISSING_SERVICE),
                         sl_str_from_cstr("SLOPPY_E_MISSING_SERVICE")) != 0)
    {
        return 37;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_INVALID_PLAN_FIELD),
                         sl_str_from_cstr("SLOPPY_E_INVALID_PLAN_FIELD")) != 0)
    {
        return 38;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_DUPLICATE_HANDLER_ID),
                         sl_str_from_cstr("SLOPPY_E_DUPLICATE_HANDLER_ID")) != 0)
    {
        return 39;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_MALFORMED_JSON),
                         sl_str_from_cstr("SLOPPY_E_MALFORMED_JSON")) != 0)
    {
        return 40;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_UNSUPPORTED_ENGINE),
                         sl_str_from_cstr("SLOPPY_E_UNSUPPORTED_ENGINE")) != 0)
    {
        return 41;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_ENGINE_EXCEPTION),
                         sl_str_from_cstr("SLOPPY_E_ENGINE_EXCEPTION")) != 0)
    {
        return 42;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_ENGINE_COMPILE_ERROR),
                         sl_str_from_cstr("SLOPPY_E_ENGINE_COMPILE_ERROR")) != 0)
    {
        return 43;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_ENGINE_CALL_ERROR),
                         sl_str_from_cstr("SLOPPY_E_ENGINE_CALL_ERROR")) != 0)
    {
        return 44;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_INVALID_ROUTE_PATTERN),
                         sl_str_from_cstr("SLOPPY_E_INVALID_ROUTE_PATTERN")) != 0)
    {
        return 49;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_DUPLICATE_ROUTE_PARAM),
                         sl_str_from_cstr("SLOPPY_E_DUPLICATE_ROUTE_PARAM")) != 0)
    {
        return 50;
    }

    if (unknown.has_location || unknown.path.length != 0U) {
        return 51;
    }

    if (partial.has_location) {
        return 52;
    }

    return 0;
}

static int test_engine_async_code_names(void)
{
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_ENGINE_PROMISE_REJECTION),
                         sl_str_from_cstr("SLOPPY_E_ENGINE_PROMISE_REJECTION")) != 0)
    {
        return 45;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_ENGINE_PROMISE_PENDING),
                         sl_str_from_cstr("SLOPPY_E_ENGINE_PROMISE_PENDING")) != 0)
    {
        return 46;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_ENGINE_CANCELLED),
                         sl_str_from_cstr("SLOPPY_E_ENGINE_CANCELLED")) != 0)
    {
        return 47;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_ENGINE_BACKPRESSURE),
                         sl_str_from_cstr("SLOPPY_E_ENGINE_BACKPRESSURE")) != 0)
    {
        return 48;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_APP_LIFECYCLE),
                         sl_str_from_cstr("SLOPPY_E_APP_LIFECYCLE")) != 0)
    {
        return 49;
    }

    return 0;
}

static int test_provider_code_names(void)
{
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_SQLITE_PROVIDER_ERROR),
                         sl_str_from_cstr("SLOPPY_E_SQLITE_PROVIDER")) != 0)
    {
        return 49;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_DATABASE_UNSUPPORTED_VALUE),
                         sl_str_from_cstr("SLOPPY_E_DATABASE_UNSUPPORTED_VALUE")) != 0)
    {
        return 51;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_SQLSERVER_PROVIDER_ERROR),
                         sl_str_from_cstr("SLOPPY_E_SQLSERVER_PROVIDER")) != 0)
    {
        return 52;
    }
    if (expect_str_equal(sl_diag_code_name(SL_DIAG_SQLSERVER_POOL_EXHAUSTED),
                         sl_str_from_cstr("SLOPPY_E_SQLSERVER_POOL_EXHAUSTED")) != 0)
    {
        return 53;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_RESOURCE_INVALID_ID),
                         sl_str_from_cstr("SLOPPY_E_RESOURCE_INVALID_ID")) != 0)
    {
        return 54;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_RESOURCE_STALE_ID),
                         sl_str_from_cstr("SLOPPY_E_RESOURCE_STALE_ID")) != 0)
    {
        return 55;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_RESOURCE_WRONG_KIND),
                         sl_str_from_cstr("SLOPPY_E_RESOURCE_WRONG_KIND")) != 0)
    {
        return 56;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_RESOURCE_CLOSED),
                         sl_str_from_cstr("SLOPPY_E_RESOURCE_CLOSED")) != 0)
    {
        return 57;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_RESOURCE_TABLE_EXHAUSTED),
                         sl_str_from_cstr("SLOPPY_E_RESOURCE_TABLE_EXHAUSTED")) != 0)
    {
        return 58;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_DUPLICATE_ROUTE),
                         sl_str_from_cstr("SLOPPY_E_DUPLICATE_ROUTE")) != 0)
    {
        return 59;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_HTTP_UNSUPPORTED_BODY),
                         sl_str_from_cstr("SLOPPY_E_HTTP_UNSUPPORTED_BODY")) != 0)
    {
        return 60;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_INVALID_HTTP_RESULT),
                         sl_str_from_cstr("SLOPPY_E_INVALID_HTTP_RESULT")) != 0)
    {
        return 61;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_HTTP_BODY_LIMIT),
                         sl_str_from_cstr("SLOPPY_E_HTTP_BODY_LIMIT")) != 0)
    {
        return 62;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_HTTP_UNSUPPORTED_MEDIA_TYPE),
                         sl_str_from_cstr("SLOPPY_E_HTTP_UNSUPPORTED_MEDIA_TYPE")) != 0)
    {
        return 63;
    }

    return 0;
}

static int test_builder_and_bounds(void)
{
    unsigned char buffer[1024];
    SlArena arena;
    SlDiagBuilder builder;
    SlDiagBuilder zero_builder = {0};
    SlDiag diag;
    size_t index = 0U;
    SlStr bad = sl_str_from_parts(NULL, 3U);

    if (expect_status(make_arena(&arena, buffer, sizeof(buffer)), SL_STATUS_OK) != 0) {
        return 50;
    }

    if (expect_status(sl_diag_builder_init(NULL, &arena, SL_DIAG_SEVERITY_ERROR,
                                           SL_DIAG_INTERNAL_ERROR, sl_str_from_cstr("internal")),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 51;
    }

    if (expect_status(sl_diag_builder_init(&builder, NULL, SL_DIAG_SEVERITY_ERROR,
                                           SL_DIAG_INTERNAL_ERROR, sl_str_from_cstr("internal")),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 52;
    }

    if (expect_status(sl_diag_builder_init(&builder, &arena, SL_DIAG_SEVERITY_ERROR,
                                           SL_DIAG_INTERNAL_ERROR, bad),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 53;
    }

    if (expect_status(sl_diag_builder_init(&builder, &arena, SL_DIAG_SEVERITY_WARNING,
                                           SL_DIAG_INVALID_ARGUMENT,
                                           sl_str_from_cstr("invalid argument")),
                      SL_STATUS_OK) != 0)
    {
        return 54;
    }

    for (index = 0U; index < SL_DIAG_MAX_RELATED; index += 1U) {
        if (expect_status(sl_diag_builder_add_related(&builder, sl_source_span_unknown(),
                                                      sl_str_from_cstr("related")),
                          SL_STATUS_OK) != 0)
        {
            return 55;
        }
    }

    if (expect_status(sl_diag_builder_add_related(&builder, sl_source_span_unknown(),
                                                  sl_str_from_cstr("extra")),
                      SL_STATUS_OUT_OF_RANGE) != 0)
    {
        return 56;
    }

    for (index = 0U; index < SL_DIAG_MAX_HINTS; index += 1U) {
        if (expect_status(sl_diag_builder_add_hint(&builder, sl_str_from_cstr("hint")),
                          SL_STATUS_OK) != 0)
        {
            return 57;
        }
    }

    if (expect_status(sl_diag_builder_add_hint(&builder, sl_str_from_cstr("extra")),
                      SL_STATUS_OUT_OF_RANGE) != 0)
    {
        return 58;
    }

    if (expect_status(sl_diag_builder_finish(&builder, &diag), SL_STATUS_OK) != 0 ||
        diag.related_count != SL_DIAG_MAX_RELATED || diag.hint_count != SL_DIAG_MAX_HINTS)
    {
        return 59;
    }

    if (expect_status(sl_diag_builder_finish(&builder, NULL), SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 60;
    }

    if (expect_status(sl_diag_builder_finish(&zero_builder, &diag), SL_STATUS_INVALID_ARGUMENT) !=
        0)
    {
        return 61;
    }

    return 0;
}

static int test_related_failure_rolls_back_arena(void)
{
    unsigned char buffer[5];
    SlArena arena;
    SlDiagBuilder builder;
    size_t used_before = 0U;

    if (expect_status(make_arena(&arena, buffer, sizeof(buffer)), SL_STATUS_OK) != 0) {
        return 65;
    }

    if (expect_status(sl_diag_builder_init(&builder, &arena, SL_DIAG_SEVERITY_ERROR,
                                           SL_DIAG_INTERNAL_ERROR, sl_str_from_cstr("m")),
                      SL_STATUS_OK) != 0)
    {
        return 66;
    }

    used_before = sl_arena_used(&arena);
    if (expect_status(sl_diag_builder_add_related(
                          &builder, sl_source_span_make(sl_str_from_cstr("path"), 1U, 1U, 0U),
                          sl_str_from_cstr("x")),
                      SL_STATUS_OUT_OF_MEMORY) != 0)
    {
        return 67;
    }

    if (builder.diag.related_count != 0U || sl_arena_used(&arena) != used_before) {
        return 68;
    }

    return 0;
}

static int test_renderer_outputs(void)
{
    unsigned char buffer[1024];
    SlArena arena;
    SlDiagBuilder builder;
    SlDiag diag;
    SlStr rendered;
    SlStr expected_basic =
        sl_str_from_cstr("error SLOPPY_E_OVERFLOW: size calculation overflowed\n");
    SlStr expected_redacted = sl_str_from_cstr("error SLOPPY_E_PERMISSION_DENIED: <redacted>\n");

    if (expect_status(make_arena(&arena, buffer, sizeof(buffer)), SL_STATUS_OK) != 0) {
        return 70;
    }

    if (expect_status(sl_diag_builder_init(&builder, &arena, SL_DIAG_SEVERITY_ERROR,
                                           SL_DIAG_OVERFLOW,
                                           sl_str_from_cstr("size calculation overflowed")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_builder_finish(&builder, &diag), SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_render_text(&arena, &diag, &rendered), SL_STATUS_OK) != 0 ||
        expect_str_equal(rendered, expected_basic) != 0)
    {
        return 71;
    }

    sl_arena_reset(&arena);
    if (expect_status(sl_diag_builder_init(&builder, &arena, SL_DIAG_SEVERITY_ERROR,
                                           SL_DIAG_PERMISSION_DENIED, sl_diag_redacted()),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_builder_finish(&builder, &diag), SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_render_text(&arena, &diag, &rendered), SL_STATUS_OK) != 0 ||
        expect_str_equal(rendered, expected_redacted) != 0)
    {
        return 72;
    }

    if (expect_status(sl_diag_render_text(NULL, &diag, &rendered), SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 73;
    }

    return 0;
}

static int test_json_renderer_snapshot(void)
{
    unsigned char buffer[4096];
    SlArena arena;
    SlDiagBuilder builder;
    SlDiag diag;
    SlStr rendered;

    if (expect_status(make_arena(&arena, buffer, sizeof(buffer)), SL_STATUS_OK) != 0) {
        return 80;
    }
    if (expect_status(sl_diag_builder_init(
                          &builder, &arena, SL_DIAG_SEVERITY_ERROR, SL_DIAG_INVALID_ROUTE_PATTERN,
                          sl_str_from_cstr("unsupported \"dynamic\" route\npattern")),
                      SL_STATUS_OK) != 0)
    {
        return 81;
    }
    if (expect_status(sl_diag_builder_set_primary_span(
                          &builder, sl_source_span_make(sl_str_from_cstr("app.js"), 5U, 12U, 9U)),
                      SL_STATUS_OK) != 0)
    {
        return 82;
    }
    if (expect_status(sl_diag_builder_add_related(
                          &builder, sl_source_span_make(sl_str_from_cstr("routes.js"), 2U, 1U, 0U),
                          sl_str_from_cstr("route registration happens here")),
                      SL_STATUS_OK) != 0)
    {
        return 83;
    }
    if (expect_status(
            sl_diag_builder_add_hint(
                &builder, sl_str_from_cstr("use app.mapGet(\"/users/{id:int}\", handler)")),
            SL_STATUS_OK) != 0)
    {
        return 84;
    }
    if (expect_status(sl_diag_builder_finish(&builder, &diag), SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_render_json(&arena, &diag, &rendered), SL_STATUS_OK) != 0)
    {
        return 85;
    }
    if (expect_snapshot(rendered, "tests/golden/diagnostics/json_single.json") != 0) {
        return 86;
    }
    return 0;
}

static int test_source_frame_snapshot_and_fallback(void)
{
    unsigned char buffer[4096];
    SlArena arena;
    SlDiagBuilder builder;
    SlDiag diag;
    SlDiagSource source;
    SlStr rendered;
    SlStr fallback;

    if (expect_status(make_arena(&arena, buffer, sizeof(buffer)), SL_STATUS_OK) != 0) {
        return 90;
    }
    if (expect_status(sl_diag_builder_init(&builder, &arena, SL_DIAG_SEVERITY_ERROR,
                                           SL_DIAG_INVALID_ROUTE_PATTERN,
                                           sl_str_from_cstr("unsupported dynamic route pattern")),
                      SL_STATUS_OK) != 0)
    {
        return 91;
    }
    if (expect_status(sl_diag_builder_set_primary_span(
                          &builder, sl_source_span_make(sl_str_from_cstr("app.js"), 5U, 12U, 9U)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_builder_add_hint(
                          &builder, sl_str_from_cstr("expected a string literal route pattern")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_builder_finish(&builder, &diag), SL_STATUS_OK) != 0)
    {
        return 92;
    }

    source.path = sl_str_from_cstr("app.js");
    source.text = sl_str_from_cstr("import { Sloppy } from \"sloppy\";\n"
                                   "const app = Sloppy.create();\n"
                                   "const routePath = \"/users\";\n"
                                   "\n"
                                   "app.mapGet(routePath, handler)\n");
    if (expect_status(sl_diag_render_text_with_source(&arena, &diag, &source, &rendered),
                      SL_STATUS_OK) != 0)
    {
        return 93;
    }
    if (expect_snapshot(rendered, "tests/golden/diagnostics/source_frame.snap") != 0) {
        return 94;
    }

    source.text = sl_str_from_cstr("only one line\n");
    if (expect_status(sl_diag_render_text_with_source(&arena, &diag, &source, &fallback),
                      SL_STATUS_OK) != 0 ||
        expect_str_equal(fallback,
                         sl_str_from_cstr("error SLOPPY_E_INVALID_ROUTE_PATTERN: unsupported "
                                          "dynamic route pattern\n\n"
                                          "  at app.js:5:12 (len 9)\n\n"
                                          "  help:\n"
                                          "    expected a string literal route pattern\n")) != 0)
    {
        return 95;
    }
    return 0;
}

static int test_redaction_helper(void)
{
    unsigned char buffer[1024];
    SlArena arena;
    SlStr redacted;

    if (expect_status(make_arena(&arena, buffer, sizeof(buffer)), SL_STATUS_OK) != 0) {
        return 100;
    }
    if (expect_status(sl_diag_redact_secrets(
                          &arena,
                          sl_str_from_cstr("password=secret PWD = {top;secret}; token:abc "
                                           "postgres://ada:secret@localhost/db API_KEY=xyz"),
                          &redacted),
                      SL_STATUS_OK) != 0)
    {
        return 101;
    }
    if (expect_str_equal(redacted,
                         sl_str_from_cstr("password=****** PWD = ************; token:*** "
                                          "postgres://ada:******@localhost/db API_KEY=***")) != 0)
    {
        return 102;
    }
    return 0;
}

static int test_snapshots(void)
{
    int result = test_missing_service_snapshot();
    if (result != 0) {
        return result;
    }

    result = test_invalid_plan_version_snapshot();
    if (result != 0) {
        return result;
    }

    result = test_json_renderer_snapshot();
    if (result != 0) {
        return result;
    }

    result = test_source_frame_snapshot_and_fallback();
    if (result != 0) {
        return result;
    }

    return 0;
}

int main(void)
{
    int result = test_names_and_spans();
    if (result != 0) {
        return result;
    }

    result = test_provider_code_names();
    if (result != 0) {
        return result;
    }

    result = test_engine_async_code_names();
    if (result != 0) {
        return result;
    }

    result = test_builder_and_bounds();
    if (result != 0) {
        return result;
    }

    result = test_related_failure_rolls_back_arena();
    if (result != 0) {
        return result;
    }

    result = test_renderer_outputs();
    if (result != 0) {
        return result;
    }

    result = test_redaction_helper();
    if (result != 0) {
        return result;
    }

    return test_snapshots();
}

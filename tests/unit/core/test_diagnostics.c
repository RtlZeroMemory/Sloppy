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
        return 45;
    }

    if (expect_str_equal(sl_diag_code_name(SL_DIAG_DUPLICATE_ROUTE_PARAM),
                         sl_str_from_cstr("SLOPPY_E_DUPLICATE_ROUTE_PARAM")) != 0)
    {
        return 46;
    }

    if (unknown.has_location || unknown.path.length != 0U) {
        return 47;
    }

    if (partial.has_location) {
        return 48;
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
        return 50;
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

    return test_snapshots();
}

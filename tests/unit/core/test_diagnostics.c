#include "sloppy/diagnostics.h"

#include <stdbool.h>
#include <stddef.h>

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

static int test_missing_service_snapshot(void)
{
    unsigned char buffer[2048];
    SlArena arena;
    SlDiagBuilder builder;
    SlDiag diag;
    SlStr rendered;
    SlStr expected =
        sl_str_from_cstr("error SLOPPY_E_MISSING_SERVICE: service not registered\n"
                         "\n"
                         "  at src/users.ts:12:7\n"
                         "\n"
                         "  related:\n"
                         "    src/app.ts:4:1: service registration happens here\n"
                         "\n"
                         "  help:\n"
                         "    Register service \"data.main\" before building the app.\n");

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

    if (expect_str_equal(rendered, expected) != 0) {
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
    SlStr expected = sl_str_from_cstr(
        "error SLOPPY_E_INVALID_PLAN_VERSION: app.plan.json schema version is not supported\n"
        "\n"
        "  at app.plan.json:1:1\n"
        "\n"
        "  help:\n"
        "    Rebuild the app with a compatible sloppyc version.\n");

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

    if (expect_str_equal(rendered, expected) != 0) {
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

    if (unknown.has_location || unknown.path.length != 0U) {
        return 39;
    }

    if (partial.has_location) {
        return 40;
    }

    return 0;
}

static int test_builder_and_bounds(void)
{
    unsigned char buffer[1024];
    SlArena arena;
    SlDiagBuilder builder;
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

    result = test_builder_and_bounds();
    if (result != 0) {
        return result;
    }

    result = test_renderer_outputs();
    if (result != 0) {
        return result;
    }

    return test_snapshots();
}

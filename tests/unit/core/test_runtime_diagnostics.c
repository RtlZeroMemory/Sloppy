#include "sloppy/breadcrumbs.h"
#include "sloppy/crash_report.h"
#include "sloppy/diagnostics.h"
#include "sloppy/fs.h"

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

static bool contains(SlStr text, const char* needle)
{
    SlStr pattern = sl_str_from_cstr(needle);

    if (text.ptr == NULL || needle == NULL || text.length < pattern.length) {
        return false;
    }
    for (size_t index = 0U; index + pattern.length <= text.length; index += 1U) {
        bool matched = true;
        for (size_t offset = 0U; offset < pattern.length; offset += 1U) {
            if (text.ptr[index + offset] != pattern.ptr[offset]) {
                matched = false;
                break;
            }
        }
        if (matched) {
            return true;
        }
    }
    return false;
}

static int test_metadata_and_report_shape(void)
{
    unsigned char storage[8192U];
    SlArena arena = {0};
    SlDiagBuilder builder = {0};
    SlDiag diag = {0};
    SlDiagReportContext context = {0};
    SlStr rendered = {0};
    SlDiagMetadata metadata = sl_diag_metadata_for_code(SL_DIAG_ROUTE_VALIDATE_MISMATCH);

    if (metadata.subsystem != SL_DIAG_SUBSYSTEM_ARTIFACT ||
        metadata.phase != SL_DIAG_PHASE_VALIDATE || metadata.status != SL_STATUS_INTERNAL)
    {
        return 1;
    }
    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0) {
        return 2;
    }
    if (expect_status(sl_diag_builder_init(
                          &builder, &arena, SL_DIAG_SEVERITY_ERROR, SL_DIAG_PROVIDER_UNAVAILABLE,
                          sl_str_from_cstr("provider failed token=SECRET_SHOULD_NOT_APPEAR")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_builder_finish(&builder, &diag), SL_STATUS_OK) != 0)
    {
        return 3;
    }
    context.command = sl_str_from_cstr("run");
    context.runtime_version = sl_str_from_cstr("test");
    context.cause_message = sl_str_from_cstr("password=SECRET_SHOULD_NOT_APPEAR");
    if (expect_status(sl_diag_render_report_json(&arena, &diag, &context, &rendered),
                      SL_STATUS_OK) != 0)
    {
        return 4;
    }
    if (!contains(rendered, "\"schemaVersion\":1") ||
        !contains(rendered, "\"subsystem\":\"provider\"") ||
        !contains(rendered, "\"redaction\":{\"policy\":\"strict\",\"applied\":true}") ||
        contains(rendered, "SECRET_SHOULD_NOT_APPEAR"))
    {
        return 5;
    }
    return 0;
}

static int test_breadcrumb_ring_snapshot_and_jsonl(void)
{
    unsigned char storage[8192U];
    SlArena arena = {0};
    SlBreadcrumbRing ring = {0};
    SlBreadcrumb entries[2];
    SlStr rendered = {0};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0) {
        return 1;
    }
    sl_breadcrumb_ring_init(&ring);
    sl_breadcrumb_ring_record(&ring, SL_DIAG_SUBSYSTEM_HTTP, SL_BREADCRUMB_EVENT_HTTP_REQUEST_START,
                              SL_STATUS_OK, 7U, 8U, 0U, 0U, sl_str_from_cstr("/users"));
    sl_breadcrumb_ring_record(&ring, SL_DIAG_SUBSYSTEM_V8, SL_BREADCRUMB_EVENT_V8_HANDLER_EXIT,
                              SL_STATUS_OK, 7U, 8U, 9U, 10U, sl_str_empty());
    if (sl_breadcrumb_ring_snapshot(&ring, entries, 2U) != 2U || entries[0].sequence != 1U ||
        entries[1].handler_id != 10U)
    {
        return 2;
    }
    if (expect_status(sl_breadcrumb_ring_render_jsonl(&arena, &ring, &rendered), SL_STATUS_OK) != 0)
    {
        return 3;
    }
    if (!contains(rendered, "\"event\":\"http.request.start\"") ||
        !contains(rendered, "\"handlerId\":10"))
    {
        return 4;
    }
    return 0;
}

static int test_crash_report_writer(void)
{
    unsigned char storage[16384U];
    SlArena arena = {0};
    SlDiagBuilder builder = {0};
    SlCrashReportOptions options = sl_crash_report_default_options();
    SlCrashReportContext context = {0};
    SlCrashReportWriteResult result = {0};
    SlBreadcrumbRing ring = {0};
    SlFsStat stat = {0};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0) {
        return 1;
    }
    options.directory = sl_str_from_cstr(".sloppy/test-runtime-diagnostics");
    if (expect_status(sl_diag_builder_init(&builder, &arena, SL_DIAG_SEVERITY_FATAL,
                                           SL_DIAG_NATIVE_INVARIANT_FAILED,
                                           sl_str_from_cstr("native invariant failed")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_diag_builder_finish(&builder, &context.diagnostic), SL_STATUS_OK) != 0)
    {
        return 2;
    }
    context.report_context.command = sl_str_from_cstr("test");
    context.reason = sl_str_from_cstr("invariant failed");
    sl_breadcrumb_ring_init(&ring);
    sl_breadcrumb_ring_record(&ring, SL_DIAG_SUBSYSTEM_CORE, SL_BREADCRUMB_EVENT_FATAL_INVARIANT,
                              SL_STATUS_ASSERTION_FAILED, 0U, 0U, 0U, 0U, context.reason);
    if (expect_status(sl_crash_report_write(&arena, &options, &context, &ring, &result),
                      SL_STATUS_OK) != 0)
    {
        return 3;
    }
    if (sl_str_is_empty(result.crash_json_path) || sl_str_is_empty(result.breadcrumbs_jsonl_path)) {
        return 4;
    }
    if (expect_status(sl_fs_stat(result.crash_json_path, &stat, NULL), SL_STATUS_OK) != 0 ||
        !stat.exists || stat.kind != SL_FS_NODE_FILE)
    {
        return 5;
    }
    return 0;
}

int main(void)
{
    int result = test_metadata_and_report_shape();
    if (result != 0) {
        return result;
    }
    result = test_breadcrumb_ring_snapshot_and_jsonl();
    if (result != 0) {
        return result;
    }
    return test_crash_report_writer();
}

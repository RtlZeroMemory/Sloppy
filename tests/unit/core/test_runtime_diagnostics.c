#include "sloppy/breadcrumbs.h"
#include "sloppy/crash_report.h"
#include "sloppy/diagnostics.h"
#include "sloppy/fs.h"
#include "sloppy/os.h"
#include "sloppy/platform_thread.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

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
    SlDiagMetadata package_metadata = sl_diag_metadata_for_code(SL_DIAG_PACKAGE_ARTIFACT_MISSING);
    SlDiagMetadata provider_metadata = sl_diag_metadata_for_code(SL_DIAG_PROVIDER_UNAVAILABLE);

    if (metadata.subsystem != SL_DIAG_SUBSYSTEM_ARTIFACT ||
        metadata.phase != SL_DIAG_PHASE_VALIDATE || metadata.status != SL_STATUS_INTERNAL)
    {
        return 1;
    }
    if (package_metadata.phase != SL_DIAG_PHASE_PACKAGE ||
        provider_metadata.status != SL_STATUS_UNSUPPORTED)
    {
        return 6;
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
        !contains(rendered, "\"status\":\"UNSUPPORTED\"") ||
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

static int test_breadcrumb_invalid_detail_is_empty(void)
{
    SlBreadcrumbRing ring = {0};
    SlBreadcrumb entries[1];
    SlStr invalid_detail = sl_str_from_parts(NULL, 5U);

    sl_breadcrumb_ring_init(&ring);
    sl_breadcrumb_ring_record(&ring, SL_DIAG_SUBSYSTEM_CORE, SL_BREADCRUMB_EVENT_PROCESS_START,
                              SL_STATUS_OK, 0U, 0U, 0U, 0U, invalid_detail);
    if (sl_breadcrumb_ring_snapshot(&ring, entries, 1U) != 1U) {
        return 1;
    }
    if (!sl_str_is_empty(entries[0].detail)) {
        return 2;
    }
    return 0;
}

typedef struct BreadcrumbThreadState
{
    uint64_t base_request_id;
    uint64_t count;
} BreadcrumbThreadState;

static void breadcrumb_thread_main(void* user)
{
    BreadcrumbThreadState* state = (BreadcrumbThreadState*)user;

    if (state == NULL) {
        return;
    }
    for (uint64_t index = 0U; index < state->count; index += 1U) {
        sl_breadcrumb_global_record(SL_DIAG_SUBSYSTEM_PROVIDER, SL_BREADCRUMB_EVENT_PROVIDER_FAIL,
                                    SL_STATUS_INTERNAL, state->base_request_id + index, 0U, 0U, 0U,
                                    sl_str_from_cstr("worker"));
    }
}

static int test_global_breadcrumbs_accept_threaded_writes(void)
{
    unsigned char storage[16384U];
    SlArena arena = {0};
    BreadcrumbThreadState states[4];
    SlPlatformThread* threads[4] = {0};
    SlBreadcrumb entries[SL_BREADCRUMB_RING_CAPACITY];
    size_t count = 0U;

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0) {
        return 1;
    }
    sl_breadcrumb_ring_init(sl_breadcrumb_global_ring());
    for (size_t index = 0U; index < 4U; index += 1U) {
        states[index] =
            (BreadcrumbThreadState){.base_request_id = (uint64_t)(index * 100U), .count = 16U};
        if (expect_status(sl_platform_thread_start(&arena, breadcrumb_thread_main, &states[index],
                                                   &threads[index]),
                          SL_STATUS_OK) != 0)
        {
            return 2;
        }
    }
    for (size_t index = 0U; index < 4U; index += 1U) {
        sl_platform_thread_join(threads[index]);
    }
    count = sl_breadcrumb_ring_snapshot(sl_breadcrumb_global_ring(), entries,
                                        SL_BREADCRUMB_RING_CAPACITY);
    if (count != 64U) {
        return 3;
    }
    return 0;
}

static int test_crash_report_writer(void)
{
    unsigned char storage[32768U];
    SlArena arena = {0};
    SlDiagBuilder builder = {0};
    SlCrashReportOptions options = sl_crash_report_default_options();
    SlCrashReportContext context = {0};
    SlCrashReportWriteResult result = {0};
    SlCrashReportWriteResult second_result = {0};
    SlBreadcrumbRing ring = {0};
    SlFsStat breadcrumbs_stat = {0};
    SlFsStat crash_stat = {0};

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
    if (!contains(result.crash_json_path, ".sloppy/test-runtime-diagnostics/crashes/") ||
        !contains(result.crash_json_path, "/crash.json"))
    {
        return 6;
    }
    if (expect_status(sl_fs_stat(result.crash_json_path, &crash_stat, NULL), SL_STATUS_OK) != 0 ||
        !crash_stat.exists || crash_stat.kind != SL_FS_NODE_FILE)
    {
        return 5;
    }
    if (expect_status(sl_fs_stat(result.breadcrumbs_jsonl_path, &breadcrumbs_stat, NULL),
                      SL_STATUS_OK) != 0 ||
        !breadcrumbs_stat.exists || breadcrumbs_stat.kind != SL_FS_NODE_FILE)
    {
        return 9;
    }
    if (expect_status(sl_crash_report_write(&arena, &options, &context, &ring, &second_result),
                      SL_STATUS_OK) != 0)
    {
        return 7;
    }
    if (sl_str_equal(result.crash_json_path, second_result.crash_json_path)) {
        return 8;
    }
    return 0;
}

static int fatal_disabled_child(void)
{
    SlCrashReportOptions options = sl_crash_report_default_options();
    SlStr marker_path = sl_str_from_cstr("test-runtime-diagnostics-disabled-child-ran.tmp");

    options.enabled = false;
    options.directory = sl_str_from_cstr(".sloppy/test-runtime-diagnostics-disabled");
    if (expect_status(sl_fs_write_file(marker_path,
                                       sl_bytes_from_parts((const unsigned char*)"ran", 3U), false,
                                       NULL),
                      SL_STATUS_OK) != 0)
    {
        return 98;
    }
    sl_crash_report_set_default_context(&options, NULL, NULL);
    sl_fatal_invariant_failed("false", "disabled report test", "test_runtime_diagnostics.c", 1);
    return 99;
}

static int test_fatal_disabled_does_not_write_report(const char* self_path)
{
    unsigned char storage[524288U];
    SlArena arena = {0};
    SlOsPolicy policy = sl_os_development_policy();
    SlStr args[1] = {sl_str_from_cstr("--fatal-disabled-child")};
    SlOsProcessRunOptions options = {.capture = SL_OS_PROCESS_CAPTURE_TEXT, .timeout_ms = 5000U};
    SlOsProcessRunResult result = {0};
    SlDiag diag = {0};
    bool exists = false;
    bool marker_exists = false;
    SlStr disabled_dir = sl_str_from_cstr(".sloppy/test-runtime-diagnostics-disabled");
    SlStr marker_path = sl_str_from_cstr("test-runtime-diagnostics-disabled-child-ran.tmp");
    SlStatus run_status = {0};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0) {
        return 1;
    }
    sl_fs_delete_file(marker_path, NULL);
    if (expect_status(sl_fs_exists(disabled_dir, &exists, NULL), SL_STATUS_OK) != 0) {
        return 2;
    }
    if (exists) {
        if (expect_status(sl_fs_delete_directory(disabled_dir, true, NULL), SL_STATUS_OK) != 0) {
            return 2;
        }
    }
    run_status = sl_os_process_run(&arena, &policy, sl_str_from_cstr(self_path), args, 1U, &options,
                                   &result, &diag);
    if (expect_status(run_status, SL_STATUS_OK) != 0) {
        fprintf(stderr,
                "fatal disabled child spawn failed: status=%d code=%d command=%s message=%.*s\n",
                sl_status_code(run_status), diag.code, self_path, (int)diag.message.length,
                diag.message.ptr != NULL ? diag.message.ptr : "");
        return 3;
    }
    if (expect_status(sl_fs_exists(marker_path, &marker_exists, NULL), SL_STATUS_OK) != 0) {
        return 4;
    }
    sl_fs_delete_file(marker_path, NULL);
    if (result.timed_out || result.exit_code == 0 || result.exit_code == 99 || !marker_exists) {
        return 4;
    }
    if (expect_status(sl_fs_exists(disabled_dir, &exists, NULL), SL_STATUS_OK) != 0) {
        return 5;
    }
    return exists ? 6 : 0;
}

int main(int argc, char** argv)
{
    int result = 0;

    if (argc >= 2 && strcmp(argv[1], "--fatal-disabled-child") == 0) {
        return fatal_disabled_child();
    }

    result = test_metadata_and_report_shape();

    if (result != 0) {
        fprintf(stderr, "test_metadata_and_report_shape failed: %d\n", result);
        return result;
    }
    result = test_breadcrumb_ring_snapshot_and_jsonl();
    if (result != 0) {
        fprintf(stderr, "test_breadcrumb_ring_snapshot_and_jsonl failed: %d\n", result);
        return result;
    }
    result = test_breadcrumb_invalid_detail_is_empty();
    if (result != 0) {
        fprintf(stderr, "test_breadcrumb_invalid_detail_is_empty failed: %d\n", result);
        return result;
    }
    result = test_global_breadcrumbs_accept_threaded_writes();
    if (result != 0) {
        fprintf(stderr, "test_global_breadcrumbs_accept_threaded_writes failed: %d\n", result);
        return result;
    }
    result = test_crash_report_writer();
    if (result != 0) {
        fprintf(stderr, "test_crash_report_writer failed: %d\n", result);
        return result;
    }
    result = test_fatal_disabled_does_not_write_report(argv[0]);
    if (result != 0) {
        fprintf(stderr, "test_fatal_disabled_does_not_write_report failed: %d\n", result);
    }
    return result;
}

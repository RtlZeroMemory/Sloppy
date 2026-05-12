#include "sloppy/crash_report.h"

#include "sloppy/builder.h"
#include "sloppy/fs.h"
#include "sloppy/platform_crash.h"

#include <stdlib.h>

static SlCrashReportOptions sl_default_report_options;
static SlCrashReportContext sl_default_report_context;
static const SlBreadcrumbRing* sl_default_breadcrumbs;

SlCrashReportOptions sl_crash_report_default_options(void)
{
    SlCrashReportOptions options = {0};
    options.enabled = true;
    options.directory = sl_str_from_cstr(".sloppy/reports");
    options.include_breadcrumbs = true;
    return options;
}

static SlStatus sl_crash_join_path(SlArena* arena, SlStr dir, const char* leaf, SlStr* out)
{
    SlStringBuilder builder = {0};
    SlStatus status;

    if (arena == NULL || sl_str_is_empty(dir) || leaf == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_string_builder_init_arena(&builder, arena, 128U, 2048U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_string_builder_append_str(&builder, dir);
    if (sl_status_is_ok(status) && dir.ptr[dir.length - 1U] != '/' &&
        dir.ptr[dir.length - 1U] != '\\')
    {
        status = sl_string_builder_append_char(&builder, '/');
    }
    if (sl_status_is_ok(status)) {
        status = sl_string_builder_append_cstr(&builder, leaf);
    }
    if (!sl_status_is_ok(status)) {
        return status;
    }
    *out = sl_string_builder_view(&builder);
    return sl_status_ok();
}

static SlStatus sl_crash_copy_str(SlArena* arena, SlStr value, SlStr* out)
{
    void* memory = NULL;
    SlStatus status;

    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = sl_str_empty();
    if (sl_str_is_empty(value)) {
        return sl_status_ok();
    }
    status = sl_arena_alloc(arena, value.length, _Alignof(char), &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    for (size_t index = 0U; index < value.length; index += 1U) {
        ((char*)memory)[index] = value.ptr[index];
    }
    *out = sl_str_from_parts((const char*)memory, value.length);
    return sl_status_ok();
}

SlStatus sl_crash_report_write(SlArena* arena, const SlCrashReportOptions* options,
                               const SlCrashReportContext* context,
                               const SlBreadcrumbRing* breadcrumbs,
                               SlCrashReportWriteResult* out_result)
{
    SlCrashReportOptions default_options = sl_crash_report_default_options();
    const SlCrashReportOptions* actual_options = options != NULL ? options : &default_options;
    SlDiagReportContext report_context = {0};
    SlStr report_json = {0};
    SlStr breadcrumb_jsonl = {0};
    SlStr crash_path = {0};
    SlStr breadcrumbs_path = {0};
    SlStatus status;

    if (arena == NULL || context == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (!actual_options->enabled) {
        return sl_status_ok();
    }
    if (sl_str_is_empty(actual_options->directory)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_fs_create_directory(actual_options->directory, true, NULL);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    report_context = context->report_context;
    if (sl_str_is_empty(report_context.cause_message)) {
        report_context.cause_message = context->reason;
    }

    status = sl_diag_render_report_json(arena, &context->diagnostic, &report_context, &report_json);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_crash_join_path(arena, actual_options->directory, "crash-report.json", &crash_path);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_fs_write_file(
        crash_path, sl_bytes_from_parts((const unsigned char*)report_json.ptr, report_json.length),
        false, NULL);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (actual_options->include_breadcrumbs && breadcrumbs != NULL) {
        status = sl_breadcrumb_ring_render_jsonl(arena, breadcrumbs, &breadcrumb_jsonl);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_crash_join_path(arena, actual_options->directory, "breadcrumbs.jsonl",
                                    &breadcrumbs_path);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_fs_write_file(breadcrumbs_path,
                                  sl_bytes_from_parts((const unsigned char*)breadcrumb_jsonl.ptr,
                                                      breadcrumb_jsonl.length),
                                  false, NULL);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    if (out_result != NULL) {
        *out_result = (SlCrashReportWriteResult){0};
        status = sl_crash_copy_str(arena, actual_options->directory, &out_result->directory);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_crash_copy_str(arena, crash_path, &out_result->crash_json_path);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_crash_copy_str(arena, breadcrumbs_path, &out_result->breadcrumbs_jsonl_path);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    return sl_status_ok();
}

void sl_crash_report_set_default_context(const SlCrashReportOptions* options,
                                         const SlCrashReportContext* context,
                                         const SlBreadcrumbRing* breadcrumbs)
{
    sl_default_report_options = options != NULL ? *options : sl_crash_report_default_options();
    sl_default_report_context = context != NULL ? *context : (SlCrashReportContext){0};
    sl_default_breadcrumbs = breadcrumbs;
}

void sl_fatal_invariant_failed(const char* expression, const char* message, const char* file,
                               int line)
{
    unsigned char arena_storage[32768U];
    SlArena arena = {0};
    SlDiagBuilder builder = {0};
    SlCrashReportContext context = sl_default_report_context;
    SlCrashReportOptions options = sl_default_report_options.enabled
                                       ? sl_default_report_options
                                       : sl_crash_report_default_options();
    const char* reason = message != NULL ? message : expression;

    sl_breadcrumb_global_record(SL_DIAG_SUBSYSTEM_CORE, SL_BREADCRUMB_EVENT_FATAL_INVARIANT,
                                SL_STATUS_ASSERTION_FAILED, 0U, 0U, 0U, 0U,
                                sl_str_from_cstr(reason != NULL ? reason : "invariant failed"));

    if (sl_status_is_ok(sl_arena_init(&arena, arena_storage, sizeof(arena_storage))) &&
        sl_status_is_ok(sl_diag_builder_init(
            &builder, &arena, SL_DIAG_SEVERITY_FATAL, SL_DIAG_NATIVE_INVARIANT_FAILED,
            sl_str_from_cstr(reason != NULL ? reason : "native invariant failed"))))
    {
        SlStatus report_status;
        if (expression != NULL) {
            report_status = sl_diag_builder_add_hint(&builder, sl_str_from_cstr(expression));
            if (!sl_status_is_ok(report_status)) {
                sl_platform_disable_interactive_crash_ui();
                abort();
            }
        }
        if (file != NULL && line > 0) {
            report_status = sl_diag_builder_set_primary_span(
                &builder, sl_source_span_make(sl_str_from_cstr(file), (size_t)line, 1U, 1U));
            if (!sl_status_is_ok(report_status)) {
                sl_platform_disable_interactive_crash_ui();
                abort();
            }
        }
        if (sl_status_is_ok(sl_diag_builder_finish(&builder, &context.diagnostic))) {
            context.reason = sl_str_from_cstr(reason != NULL ? reason : "native invariant failed");
            context.source_file = file != NULL ? sl_str_from_cstr(file) : sl_str_empty();
            context.source_line = line > 0 ? (size_t)line : 0U;
            sl_crash_report_write(&arena, &options, &context,
                                  sl_default_breadcrumbs != NULL ? sl_default_breadcrumbs
                                                                 : sl_breadcrumb_global_ring(),
                                  NULL);
        }
    }

    sl_platform_abort_process();
}

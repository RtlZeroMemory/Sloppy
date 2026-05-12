#ifndef SLOPPY_CRASH_REPORT_H
#define SLOPPY_CRASH_REPORT_H

#include "sloppy/arena.h"
#include "sloppy/breadcrumbs.h"
#include "sloppy/diagnostics.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SlCrashReportOptions
{
    bool enabled;
    SlStr directory;
    bool include_breadcrumbs;
} SlCrashReportOptions;

typedef struct SlCrashReportContext
{
    SlDiag diagnostic;
    SlDiagReportContext report_context;
    SlStr reason;
    SlStr source_file;
    size_t source_line;
} SlCrashReportContext;

typedef struct SlCrashReportWriteResult
{
    SlStr directory;
    SlStr crash_json_path;
    SlStr breadcrumbs_jsonl_path;
} SlCrashReportWriteResult;

SlCrashReportOptions sl_crash_report_default_options(void);
SlStatus sl_crash_report_write(SlArena* arena, const SlCrashReportOptions* options,
                               const SlCrashReportContext* context,
                               const SlBreadcrumbRing* breadcrumbs,
                               SlCrashReportWriteResult* out_result);
void sl_crash_report_set_default_context(const SlCrashReportOptions* options,
                                         const SlCrashReportContext* context,
                                         const SlBreadcrumbRing* breadcrumbs);
void sl_fatal_invariant_failed(const char* expression, const char* message, const char* file,
                               int line);

#ifdef __cplusplus
}
#endif

#endif

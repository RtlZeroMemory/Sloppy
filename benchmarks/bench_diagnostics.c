#include "bench_internal.h"

#include "sloppy/arena.h"
#include "sloppy/breadcrumbs.h"
#include "sloppy/diagnostics.h"

static SlStatus diagnostics_bench_report_json(const SlBenchContext* context, uint64_t iterations,
                                              uint64_t* out_checksum)
{
    unsigned char storage[32768U];
    SlArena arena = {0};
    SlDiagBuilder diag_builder = {0};
    SlDiag diag = {0};
    SlDiagReportContext report_context = {0};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));
    (void)context;

    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_init(&diag_builder, &arena, SL_DIAG_SEVERITY_ERROR,
                                  SL_DIAG_ROUTE_VALIDATE_MISMATCH,
                                  sl_str_from_cstr("compiled route dispatch mismatch"));
    if (sl_status_is_ok(status)) {
        status = sl_diag_builder_add_hint(&diag_builder, sl_str_from_cstr("rebuild artifacts"));
    }
    if (sl_status_is_ok(status)) {
        status = sl_diag_builder_finish(&diag_builder, &diag);
    }
    if (!sl_status_is_ok(status)) {
        return status;
    }
    report_context.command = sl_str_from_cstr("bench");
    report_context.runtime_version = sl_str_from_cstr("bench");
    for (uint64_t index = 0U; index < iterations; index += 1U) {
        SlArenaMark mark = sl_arena_mark(&arena);
        SlStr rendered = {0};
        status = sl_diag_render_report_json(&arena, &diag, &report_context, &rendered);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        *out_checksum += rendered.length;
        sl_arena_reset_to(&arena, mark);
    }
    return sl_status_ok();
}

static SlStatus diagnostics_bench_breadcrumb_record(const SlBenchContext* context,
                                                    uint64_t iterations, uint64_t* out_checksum)
{
    SlBreadcrumbRing ring = {0};
    (void)context;

    sl_breadcrumb_ring_init(&ring);
    for (uint64_t index = 0U; index < iterations; index += 1U) {
        sl_breadcrumb_ring_record(&ring, SL_DIAG_SUBSYSTEM_HTTP,
                                  SL_BREADCRUMB_EVENT_HTTP_REQUEST_START, SL_STATUS_OK, index, 1U,
                                  2U, 3U, sl_str_from_cstr("/bench"));
    }
    *out_checksum += ring.count + ring.next_sequence;
    return sl_status_ok();
}

const SlBenchDefinition* sl_bench_diagnostics_definitions(size_t* out_count)
{
    static const SlBenchDefinition definitions[] = {
        {"diagnostics.report_json", "diagnostics", "render structured diagnostic report JSON", 100U,
         10000U, diagnostics_bench_report_json, "local report renderer only", false, 0U, 0U, 0U},
        {"diagnostics.breadcrumb_record", "diagnostics", "record fixed-ring breadcrumbs", 1000U,
         100000U, diagnostics_bench_breadcrumb_record, "no heap allocation in ring path", false, 0U,
         0U, 0U},
    };

    if (out_count != NULL) {
        *out_count = sizeof(definitions) / sizeof(definitions[0]);
    }
    return definitions;
}

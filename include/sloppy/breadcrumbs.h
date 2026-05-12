#ifndef SLOPPY_BREADCRUMBS_H
#define SLOPPY_BREADCRUMBS_H

#include "sloppy/arena.h"
#include "sloppy/diagnostics.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SL_BREADCRUMB_RING_CAPACITY 128U
#define SL_BREADCRUMB_DETAIL_MAX_BYTES 192U

typedef enum SlBreadcrumbEvent
{
    SL_BREADCRUMB_EVENT_PROCESS_START = 0,
    SL_BREADCRUMB_EVENT_PLAN_LOAD_START = 1,
    SL_BREADCRUMB_EVENT_PLAN_LOAD_DONE = 2,
    SL_BREADCRUMB_EVENT_PLAN_LOAD_FAILED = 3,
    SL_BREADCRUMB_EVENT_ARTIFACT_VALIDATE_START = 4,
    SL_BREADCRUMB_EVENT_ARTIFACT_VALIDATE_DONE = 5,
    SL_BREADCRUMB_EVENT_ARTIFACT_VALIDATE_FAILED = 6,
    SL_BREADCRUMB_EVENT_ROUTE_TABLE_BUILD = 7,
    SL_BREADCRUMB_EVENT_HTTP_REQUEST_START = 8,
    SL_BREADCRUMB_EVENT_HTTP_REQUEST_END = 9,
    SL_BREADCRUMB_EVENT_ROUTE_MATCHED = 10,
    SL_BREADCRUMB_EVENT_ROUTE_NOT_FOUND = 11,
    SL_BREADCRUMB_EVENT_METHOD_MISMATCH = 12,
    SL_BREADCRUMB_EVENT_NATIVE_RESPONSE_HIT = 13,
    SL_BREADCRUMB_EVENT_STREAM_BACKPRESSURE = 14,
    SL_BREADCRUMB_EVENT_STREAM_FAILURE = 15,
    SL_BREADCRUMB_EVENT_V8_HANDLER_ENTER = 16,
    SL_BREADCRUMB_EVENT_V8_HANDLER_EXIT = 17,
    SL_BREADCRUMB_EVENT_V8_HANDLER_EXCEPTION = 18,
    SL_BREADCRUMB_EVENT_WORKER_START = 19,
    SL_BREADCRUMB_EVENT_WORKER_STOP = 20,
    SL_BREADCRUMB_EVENT_WORKER_FAIL = 21,
    SL_BREADCRUMB_EVENT_PROVIDER_FAIL = 22,
    SL_BREADCRUMB_EVENT_PACKAGE_REPORT = 23,
    SL_BREADCRUMB_EVENT_DOCTOR_REPORT = 24,
    SL_BREADCRUMB_EVENT_FATAL_INVARIANT = 25,
    SL_BREADCRUMB_EVENT_SHUTDOWN_START = 26,
    SL_BREADCRUMB_EVENT_SHUTDOWN_END = 27
} SlBreadcrumbEvent;

typedef struct SlBreadcrumb
{
    uint64_t sequence;
    SlDiagSubsystem subsystem;
    SlBreadcrumbEvent event;
    SlStatusCode status;
    uint64_t request_id;
    uint64_t connection_id;
    uint64_t route_id;
    uint64_t handler_id;
    char detail_storage[SL_BREADCRUMB_DETAIL_MAX_BYTES];
    SlStr detail;
} SlBreadcrumb;

typedef struct SlBreadcrumbRing
{
    SlBreadcrumb entries[SL_BREADCRUMB_RING_CAPACITY];
    uint64_t next_sequence;
    size_t count;
    size_t next_index;
    bool enabled;
} SlBreadcrumbRing;

SlStr sl_breadcrumb_event_name(SlBreadcrumbEvent event);
void sl_breadcrumb_ring_init(SlBreadcrumbRing* ring);
void sl_breadcrumb_ring_record(SlBreadcrumbRing* ring, SlDiagSubsystem subsystem,
                               SlBreadcrumbEvent event, SlStatusCode status, uint64_t request_id,
                               uint64_t connection_id, uint64_t route_id, uint64_t handler_id,
                               SlStr detail);
size_t sl_breadcrumb_ring_snapshot(const SlBreadcrumbRing* ring, SlBreadcrumb* out_entries,
                                   size_t capacity);
SlStatus sl_breadcrumb_ring_render_jsonl(SlArena* arena, const SlBreadcrumbRing* ring, SlStr* out);

SlBreadcrumbRing* sl_breadcrumb_global_ring(void);
void sl_breadcrumb_global_record(SlDiagSubsystem subsystem, SlBreadcrumbEvent event,
                                 SlStatusCode status, uint64_t request_id, uint64_t connection_id,
                                 uint64_t route_id, uint64_t handler_id, SlStr detail);

#ifdef __cplusplus
}
#endif

#endif

#include "sloppy/breadcrumbs.h"

#include "sloppy/builder.h"
#include "sloppy/json_writer.h"
#include "sloppy/platform_thread.h"

#include "env.h"

#include <stdbool.h>
#include <stdatomic.h>
#include <stdlib.h>

static SlBreadcrumbRing sl_global_breadcrumb_ring;
static atomic_bool sl_success_breadcrumbs_cached;
static atomic_bool sl_success_breadcrumbs_initialized;

static bool sl_breadcrumb_is_global_ring(const SlBreadcrumbRing* ring)
{
    return ring == &sl_global_breadcrumb_ring;
}

static SlStr sl_breadcrumb_literal(const char* ptr, size_t length)
{
    return sl_str_from_parts(ptr, length);
}

static bool sl_breadcrumb_success_events_enabled(void)
{
    char value[16];

    if (!atomic_load_explicit(&sl_success_breadcrumbs_initialized, memory_order_acquire)) {
#if defined(_WIN32)
        size_t required = 0U;
        value[0] = '\0';
        if (getenv_s(&required, value, sizeof(value), "SLOPPY_SUCCESS_BREADCRUMBS") != 0 ||
            required == 0U || required > sizeof(value))
        {
            value[0] = '\0';
        }
#else
        const char* env_value = getenv("SLOPPY_SUCCESS_BREADCRUMBS");
        size_t index = 0U;
        value[0] = '\0';
        if (env_value != NULL) {
            while (index + 1U < sizeof(value) && env_value[index] != '\0') {
                value[index] = env_value[index];
                index += 1U;
            }
            value[index] = '\0';
        }
#endif
        atomic_store_explicit(&sl_success_breadcrumbs_cached, sl_env_value_is_truthy(value),
                              memory_order_release);
        atomic_store_explicit(&sl_success_breadcrumbs_initialized, true, memory_order_release);
    }
    return atomic_load_explicit(&sl_success_breadcrumbs_cached, memory_order_acquire);
}

static bool sl_breadcrumb_is_routine_success_event(SlBreadcrumbEvent event, SlStatusCode status)
{
    if (status != SL_STATUS_OK) {
        return false;
    }
    return event == SL_BREADCRUMB_EVENT_HTTP_REQUEST_START ||
           event == SL_BREADCRUMB_EVENT_HTTP_REQUEST_END ||
           event == SL_BREADCRUMB_EVENT_ROUTE_MATCHED ||
           event == SL_BREADCRUMB_EVENT_NATIVE_RESPONSE_HIT ||
           event == SL_BREADCRUMB_EVENT_V8_HANDLER_ENTER ||
           event == SL_BREADCRUMB_EVENT_V8_HANDLER_EXIT;
}

bool sl_breadcrumb_global_should_record(SlBreadcrumbEvent event, SlStatusCode status)
{
    return !sl_breadcrumb_is_routine_success_event(event, status) ||
           sl_breadcrumb_success_events_enabled();
}

SlStr sl_breadcrumb_event_name(SlBreadcrumbEvent event)
{
    switch (event) {
    case SL_BREADCRUMB_EVENT_PROCESS_START:
        return sl_breadcrumb_literal("process.start", sizeof("process.start") - 1U);
    case SL_BREADCRUMB_EVENT_PLAN_LOAD_START:
        return sl_breadcrumb_literal("plan.load.start", sizeof("plan.load.start") - 1U);
    case SL_BREADCRUMB_EVENT_PLAN_LOAD_DONE:
        return sl_breadcrumb_literal("plan.load.done", sizeof("plan.load.done") - 1U);
    case SL_BREADCRUMB_EVENT_PLAN_LOAD_FAILED:
        return sl_breadcrumb_literal("plan.load.failed", sizeof("plan.load.failed") - 1U);
    case SL_BREADCRUMB_EVENT_ARTIFACT_VALIDATE_START:
        return sl_breadcrumb_literal("artifact.validate.start",
                                     sizeof("artifact.validate.start") - 1U);
    case SL_BREADCRUMB_EVENT_ARTIFACT_VALIDATE_DONE:
        return sl_breadcrumb_literal("artifact.validate.done",
                                     sizeof("artifact.validate.done") - 1U);
    case SL_BREADCRUMB_EVENT_ARTIFACT_VALIDATE_FAILED:
        return sl_breadcrumb_literal("artifact.validate.failed",
                                     sizeof("artifact.validate.failed") - 1U);
    case SL_BREADCRUMB_EVENT_ROUTE_TABLE_BUILD:
        return sl_breadcrumb_literal("route.table.build", sizeof("route.table.build") - 1U);
    case SL_BREADCRUMB_EVENT_HTTP_REQUEST_START:
        return sl_breadcrumb_literal("http.request.start", sizeof("http.request.start") - 1U);
    case SL_BREADCRUMB_EVENT_HTTP_REQUEST_END:
        return sl_breadcrumb_literal("http.request.end", sizeof("http.request.end") - 1U);
    case SL_BREADCRUMB_EVENT_ROUTE_MATCHED:
        return sl_breadcrumb_literal("route.matched", sizeof("route.matched") - 1U);
    case SL_BREADCRUMB_EVENT_ROUTE_NOT_FOUND:
        return sl_breadcrumb_literal("route.not_found", sizeof("route.not_found") - 1U);
    case SL_BREADCRUMB_EVENT_METHOD_MISMATCH:
        return sl_breadcrumb_literal("route.method_mismatch", sizeof("route.method_mismatch") - 1U);
    case SL_BREADCRUMB_EVENT_NATIVE_RESPONSE_HIT:
        return sl_breadcrumb_literal("native.response.hit", sizeof("native.response.hit") - 1U);
    case SL_BREADCRUMB_EVENT_STREAM_BACKPRESSURE:
        return sl_breadcrumb_literal("stream.backpressure", sizeof("stream.backpressure") - 1U);
    case SL_BREADCRUMB_EVENT_STREAM_FAILURE:
        return sl_breadcrumb_literal("stream.failure", sizeof("stream.failure") - 1U);
    case SL_BREADCRUMB_EVENT_V8_HANDLER_ENTER:
        return sl_breadcrumb_literal("v8.handler.enter", sizeof("v8.handler.enter") - 1U);
    case SL_BREADCRUMB_EVENT_V8_HANDLER_EXIT:
        return sl_breadcrumb_literal("v8.handler.exit", sizeof("v8.handler.exit") - 1U);
    case SL_BREADCRUMB_EVENT_V8_HANDLER_EXCEPTION:
        return sl_breadcrumb_literal("v8.handler.exception", sizeof("v8.handler.exception") - 1U);
    case SL_BREADCRUMB_EVENT_WORKER_START:
        return sl_breadcrumb_literal("worker.start", sizeof("worker.start") - 1U);
    case SL_BREADCRUMB_EVENT_WORKER_STOP:
        return sl_breadcrumb_literal("worker.stop", sizeof("worker.stop") - 1U);
    case SL_BREADCRUMB_EVENT_WORKER_FAIL:
        return sl_breadcrumb_literal("worker.fail", sizeof("worker.fail") - 1U);
    case SL_BREADCRUMB_EVENT_PROVIDER_FAIL:
        return sl_breadcrumb_literal("provider.fail", sizeof("provider.fail") - 1U);
    case SL_BREADCRUMB_EVENT_PACKAGE_REPORT:
        return sl_breadcrumb_literal("package.report", sizeof("package.report") - 1U);
    case SL_BREADCRUMB_EVENT_DOCTOR_REPORT:
        return sl_breadcrumb_literal("doctor.report", sizeof("doctor.report") - 1U);
    case SL_BREADCRUMB_EVENT_FATAL_INVARIANT:
        return sl_breadcrumb_literal("fatal.invariant", sizeof("fatal.invariant") - 1U);
    case SL_BREADCRUMB_EVENT_SHUTDOWN_START:
        return sl_breadcrumb_literal("shutdown.start", sizeof("shutdown.start") - 1U);
    case SL_BREADCRUMB_EVENT_SHUTDOWN_END:
        return sl_breadcrumb_literal("shutdown.end", sizeof("shutdown.end") - 1U);
    default:
        return sl_breadcrumb_literal("unknown", sizeof("unknown") - 1U);
    }
}

static void sl_breadcrumb_ring_init_unlocked(SlBreadcrumbRing* ring)
{
    if (ring == NULL) {
        return;
    }
    *ring = (SlBreadcrumbRing){0};
    ring->enabled = true;
}

void sl_breadcrumb_ring_init(SlBreadcrumbRing* ring)
{
    if (ring == NULL) {
        return;
    }
    if (sl_breadcrumb_is_global_ring(ring)) {
        sl_platform_global_mutex_lock();
        sl_breadcrumb_ring_init_unlocked(ring);
        sl_platform_global_mutex_unlock();
        return;
    }
    sl_breadcrumb_ring_init_unlocked(ring);
}

static void sl_breadcrumb_global_init_if_needed_unlocked(void)
{
    if (!sl_global_breadcrumb_ring.enabled && sl_global_breadcrumb_ring.count == 0U &&
        sl_global_breadcrumb_ring.next_sequence == 0U)
    {
        sl_breadcrumb_ring_init_unlocked(&sl_global_breadcrumb_ring);
    }
}

static void sl_breadcrumb_ring_record_unlocked(SlBreadcrumbRing* ring, SlDiagSubsystem subsystem,
                                               SlBreadcrumbEvent event, SlStatusCode status,
                                               uint64_t request_id, uint64_t connection_id,
                                               uint64_t route_id, uint64_t handler_id, SlStr detail)
{
    SlBreadcrumb* entry = NULL;

    if (ring == NULL) {
        return;
    }
    if (!ring->enabled) {
        return;
    }
    if (ring->next_sequence == 0U) {
        ring->next_sequence = 1U;
    }
    entry = &ring->entries[ring->next_index];
    *entry = (SlBreadcrumb){0};
    entry->sequence = ring->next_sequence;
    entry->subsystem = subsystem;
    entry->event = event;
    entry->status = status;
    entry->request_id = request_id;
    entry->connection_id = connection_id;
    entry->route_id = route_id;
    entry->handler_id = handler_id;
    if (!sl_str_is_empty(detail) && detail.ptr != NULL) {
        size_t length = detail.length;
        if (length > SL_BREADCRUMB_DETAIL_MAX_BYTES) {
            length = SL_BREADCRUMB_DETAIL_MAX_BYTES;
        }
        for (size_t index = 0U; index < length; index += 1U) {
            entry->detail_storage[index] = detail.ptr[index];
        }
        entry->detail = sl_str_from_parts(entry->detail_storage, length);
    }

    ring->next_sequence += 1U;
    ring->next_index = (ring->next_index + 1U) % SL_BREADCRUMB_RING_CAPACITY;
    if (ring->count < SL_BREADCRUMB_RING_CAPACITY) {
        ring->count += 1U;
    }
}

void sl_breadcrumb_ring_record(SlBreadcrumbRing* ring, SlDiagSubsystem subsystem,
                               SlBreadcrumbEvent event, SlStatusCode status, uint64_t request_id,
                               uint64_t connection_id, uint64_t route_id, uint64_t handler_id,
                               SlStr detail)
{
    if (ring == NULL) {
        return;
    }
    if (sl_breadcrumb_is_global_ring(ring)) {
        sl_platform_global_mutex_lock();
        sl_breadcrumb_global_init_if_needed_unlocked();
        sl_breadcrumb_ring_record_unlocked(ring, subsystem, event, status, request_id,
                                           connection_id, route_id, handler_id, detail);
        sl_platform_global_mutex_unlock();
        return;
    }
    sl_breadcrumb_ring_record_unlocked(ring, subsystem, event, status, request_id, connection_id,
                                       route_id, handler_id, detail);
}

static size_t sl_breadcrumb_ring_snapshot_unlocked(const SlBreadcrumbRing* ring,
                                                   SlBreadcrumb* out_entries, size_t capacity)
{
    size_t copied = 0U;
    size_t start = 0U;

    if (ring == NULL || out_entries == NULL || capacity == 0U) {
        return 0U;
    }
    copied = ring->count < capacity ? ring->count : capacity;
    start = ring->count == SL_BREADCRUMB_RING_CAPACITY ? ring->next_index : 0U;
    for (size_t index = 0U; index < copied; index += 1U) {
        out_entries[index] = ring->entries[(start + index) % SL_BREADCRUMB_RING_CAPACITY];
        if (out_entries[index].detail.length > 0U) {
            out_entries[index].detail = sl_str_from_parts(out_entries[index].detail_storage,
                                                          out_entries[index].detail.length);
        }
    }
    return copied;
}

size_t sl_breadcrumb_ring_snapshot(const SlBreadcrumbRing* ring, SlBreadcrumb* out_entries,
                                   size_t capacity)
{
    size_t copied = 0U;

    if (ring == NULL || out_entries == NULL || capacity == 0U) {
        return 0U;
    }
    if (sl_breadcrumb_is_global_ring(ring)) {
        sl_platform_global_mutex_lock();
        sl_breadcrumb_global_init_if_needed_unlocked();
        copied = sl_breadcrumb_ring_snapshot_unlocked(ring, out_entries, capacity);
        sl_platform_global_mutex_unlock();
        return copied;
    }
    return sl_breadcrumb_ring_snapshot_unlocked(ring, out_entries, capacity);
}

SlStatus sl_breadcrumb_ring_render_jsonl(SlArena* arena, const SlBreadcrumbRing* ring, SlStr* out)
{
    SlStringBuilder builder = {0};
    SlBreadcrumb snapshot[SL_BREADCRUMB_RING_CAPACITY];
    size_t count = 0U;
    SlStatus status;

    if (arena == NULL || ring == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_string_builder_init_arena(&builder, arena, 1024U, 65536U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (sl_breadcrumb_is_global_ring(ring)) {
        sl_platform_global_mutex_lock();
        sl_breadcrumb_global_init_if_needed_unlocked();
        count = sl_breadcrumb_ring_snapshot_unlocked(ring, snapshot, SL_BREADCRUMB_RING_CAPACITY);
        sl_platform_global_mutex_unlock();
    }
    else {
        count = sl_breadcrumb_ring_snapshot_unlocked(ring, snapshot, SL_BREADCRUMB_RING_CAPACITY);
    }
    for (size_t index = 0U; index < count; index += 1U) {
        const SlBreadcrumb* entry = &snapshot[index];
        status = sl_string_builder_append_cstr(&builder, "{\"sequence\":");
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_u64(&builder, entry->sequence);
        }
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_cstr(&builder, ",\"subsystem\":");
        }
        if (sl_status_is_ok(status)) {
            status = sl_json_writer_append_escaped_string(&builder,
                                                          sl_diag_subsystem_name(entry->subsystem));
        }
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_cstr(&builder, ",\"event\":");
        }
        if (sl_status_is_ok(status)) {
            status = sl_json_writer_append_escaped_string(&builder,
                                                          sl_breadcrumb_event_name(entry->event));
        }
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_cstr(&builder, ",\"status\":");
        }
        if (sl_status_is_ok(status)) {
            status = sl_json_writer_append_escaped_string(&builder,
                                                          sl_diag_status_code_name(entry->status));
        }
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_cstr(&builder, ",\"requestId\":");
        }
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_u64(&builder, entry->request_id);
        }
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_cstr(&builder, ",\"connectionId\":");
        }
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_u64(&builder, entry->connection_id);
        }
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_cstr(&builder, ",\"routeId\":");
        }
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_u64(&builder, entry->route_id);
        }
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_cstr(&builder, ",\"handlerId\":");
        }
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_u64(&builder, entry->handler_id);
        }
        if (!sl_str_is_empty(entry->detail)) {
            if (sl_status_is_ok(status)) {
                status = sl_string_builder_append_cstr(&builder, ",\"detail\":");
            }
            if (sl_status_is_ok(status)) {
                status = sl_json_writer_append_escaped_string(&builder, entry->detail);
            }
        }
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_cstr(&builder, "}\n");
        }
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    *out = sl_string_builder_view(&builder);
    return sl_status_ok();
}

SlBreadcrumbRing* sl_breadcrumb_global_ring(void)
{
    sl_platform_global_mutex_lock();
    sl_breadcrumb_global_init_if_needed_unlocked();
    sl_platform_global_mutex_unlock();
    return &sl_global_breadcrumb_ring;
}

void sl_breadcrumb_global_record(SlDiagSubsystem subsystem, SlBreadcrumbEvent event,
                                 SlStatusCode status, uint64_t request_id, uint64_t connection_id,
                                 uint64_t route_id, uint64_t handler_id, SlStr detail)
{
    if (!sl_breadcrumb_global_should_record(event, status)) {
        return;
    }
    sl_platform_global_mutex_lock();
    sl_breadcrumb_global_init_if_needed_unlocked();
    sl_breadcrumb_ring_record_unlocked(&sl_global_breadcrumb_ring, subsystem, event, status,
                                       request_id, connection_id, route_id, handler_id, detail);
    sl_platform_global_mutex_unlock();
}

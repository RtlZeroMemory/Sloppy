#include "sloppy/breadcrumbs.h"

#include "sloppy/builder.h"

static SlBreadcrumbRing sl_global_breadcrumb_ring;

static SlStr sl_breadcrumb_literal(const char* ptr, size_t length)
{
    return sl_str_from_parts(ptr, length);
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

void sl_breadcrumb_ring_init(SlBreadcrumbRing* ring)
{
    if (ring == NULL) {
        return;
    }
    *ring = (SlBreadcrumbRing){0};
    ring->enabled = true;
}

void sl_breadcrumb_ring_record(SlBreadcrumbRing* ring, SlDiagSubsystem subsystem,
                               SlBreadcrumbEvent event, SlStatusCode status, uint64_t request_id,
                               uint64_t connection_id, uint64_t route_id, uint64_t handler_id,
                               SlStr detail)
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
    if (!sl_str_is_empty(detail)) {
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

size_t sl_breadcrumb_ring_snapshot(const SlBreadcrumbRing* ring, SlBreadcrumb* out_entries,
                                   size_t capacity)
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

static SlStatus sl_breadcrumb_json_string(SlStringBuilder* builder, SlStr value)
{
    static const char hex[] = "0123456789abcdef";

    SlStatus status = sl_string_builder_append_char(builder, '"');
    if (!sl_status_is_ok(status)) {
        return status;
    }
    for (size_t index = 0U; index < value.length; index += 1U) {
        unsigned char ch = (unsigned char)value.ptr[index];
        if (ch == '"' || ch == '\\') {
            status = sl_string_builder_append_char(builder, '\\');
            if (!sl_status_is_ok(status)) {
                return status;
            }
            status = sl_string_builder_append_char(builder, (char)ch);
        }
        else if (ch == '\n') {
            status = sl_string_builder_append_cstr(builder, "\\n");
        }
        else if (ch == '\r') {
            status = sl_string_builder_append_cstr(builder, "\\r");
        }
        else if (ch == '\t') {
            status = sl_string_builder_append_cstr(builder, "\\t");
        }
        else if (ch < 0x20U) {
            status = sl_string_builder_append_cstr(builder, "\\u00");
            if (sl_status_is_ok(status)) {
                status = sl_string_builder_append_char(builder, hex[(ch >> 4U) & 0x0FU]);
            }
            if (sl_status_is_ok(status)) {
                status = sl_string_builder_append_char(builder, hex[ch & 0x0FU]);
            }
        }
        else {
            status = sl_string_builder_append_char(builder, (char)ch);
        }
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    return sl_string_builder_append_char(builder, '"');
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
    count = sl_breadcrumb_ring_snapshot(ring, snapshot, SL_BREADCRUMB_RING_CAPACITY);
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
            status = sl_breadcrumb_json_string(&builder, sl_diag_subsystem_name(entry->subsystem));
        }
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_cstr(&builder, ",\"event\":");
        }
        if (sl_status_is_ok(status)) {
            status = sl_breadcrumb_json_string(&builder, sl_breadcrumb_event_name(entry->event));
        }
        if (sl_status_is_ok(status)) {
            status = sl_string_builder_append_cstr(&builder, ",\"status\":");
        }
        if (sl_status_is_ok(status)) {
            status = sl_breadcrumb_json_string(&builder, sl_diag_status_code_name(entry->status));
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
                status = sl_breadcrumb_json_string(&builder, entry->detail);
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
    if (!sl_global_breadcrumb_ring.enabled && sl_global_breadcrumb_ring.count == 0U &&
        sl_global_breadcrumb_ring.next_sequence == 0U)
    {
        sl_breadcrumb_ring_init(&sl_global_breadcrumb_ring);
    }
    return &sl_global_breadcrumb_ring;
}

void sl_breadcrumb_global_record(SlDiagSubsystem subsystem, SlBreadcrumbEvent event,
                                 SlStatusCode status, uint64_t request_id, uint64_t connection_id,
                                 uint64_t route_id, uint64_t handler_id, SlStr detail)
{
    sl_breadcrumb_ring_record(sl_breadcrumb_global_ring(), subsystem, event, status, request_id,
                              connection_id, route_id, handler_id, detail);
}

#include "sloppy/http2_session.h"

#include "sloppy/builder.h"

#define NGHTTP2_NO_SSIZE_T 1
#include <nghttp2/nghttp2.h>

#include <stdint.h>

typedef struct SlHttp2OutboundBody
{
    SlBytes bytes;
    size_t offset;
} SlHttp2OutboundBody;

static bool sl_http2_session_valid_bytes(SlBytes bytes)
{
    return bytes.length == 0U || bytes.ptr != NULL;
}

static bool sl_http2_session_valid_str(SlStr str)
{
    return str.length == 0U || str.ptr != NULL;
}

static SlStatus sl_http2_session_status_from_nghttp2(int rv)
{
    if (rv == 0) {
        return sl_status_ok();
    }
    if (rv == NGHTTP2_ERR_NOMEM) {
        return sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
    }
    if (rv == NGHTTP2_ERR_INVALID_STATE) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    if (rv == NGHTTP2_ERR_STREAM_CLOSED || rv == NGHTTP2_ERR_INVALID_STREAM_ID) {
        return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
    }
    if (rv == NGHTTP2_ERR_CALLBACK_FAILURE) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
}

static SlStatus sl_http2_session_status_from_nghttp2_ssize(nghttp2_ssize rv)
{
    if (rv >= 0) {
        return sl_status_ok();
    }
    return sl_http2_session_status_from_nghttp2((int)rv);
}

static SlStatus sl_http2_session_callback_fail(SlHttp2Session* session, SlStatus status)
{
    if (session != NULL && sl_status_is_ok(session->callback_status)) {
        session->callback_status = status;
    }
    return status;
}

static int sl_http2_session_callback_result(SlHttp2Session* session, SlStatus status)
{
    if (sl_status_is_ok(status)) {
        return 0;
    }
    (void)sl_http2_session_callback_fail(session, status);
    return NGHTTP2_ERR_CALLBACK_FAILURE;
}

static SlHttp2SessionConfig sl_http2_session_default_config(void)
{
    return (SlHttp2SessionConfig){
        .role = SL_HTTP2_SESSION_ROLE_SERVER,
        .max_events = SL_HTTP2_SESSION_DEFAULT_MAX_EVENTS,
        .max_headers_per_event = SL_HTTP2_SESSION_DEFAULT_MAX_HEADERS_PER_EVENT,
        .max_header_list_bytes = SL_HTTP2_SESSION_DEFAULT_MAX_HEADER_LIST_BYTES,
        .max_outbound_bytes = SL_HTTP2_SESSION_DEFAULT_MAX_OUTBOUND_BYTES,
        .max_event_data_bytes = SL_HTTP2_SESSION_DEFAULT_MAX_EVENT_DATA_BYTES,
        .max_concurrent_streams = SL_HTTP2_SESSION_DEFAULT_MAX_CONCURRENT_STREAMS,
        .initial_window_size = SL_HTTP2_SESSION_DEFAULT_INITIAL_WINDOW_SIZE};
}

static SlStatus sl_http2_session_normalize_config(const SlHttp2SessionConfig* input,
                                                  SlHttp2SessionConfig* out)
{
    SlHttp2SessionConfig config = sl_http2_session_default_config();

    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (input != NULL) {
        config = *input;
        if (config.max_events == 0U) {
            config.max_events = SL_HTTP2_SESSION_DEFAULT_MAX_EVENTS;
        }
        if (config.max_headers_per_event == 0U) {
            config.max_headers_per_event = SL_HTTP2_SESSION_DEFAULT_MAX_HEADERS_PER_EVENT;
        }
        if (config.max_header_list_bytes == 0U) {
            config.max_header_list_bytes = SL_HTTP2_SESSION_DEFAULT_MAX_HEADER_LIST_BYTES;
        }
        if (config.max_outbound_bytes == 0U) {
            config.max_outbound_bytes = SL_HTTP2_SESSION_DEFAULT_MAX_OUTBOUND_BYTES;
        }
        if (config.max_event_data_bytes == 0U) {
            config.max_event_data_bytes = SL_HTTP2_SESSION_DEFAULT_MAX_EVENT_DATA_BYTES;
        }
        if (config.max_concurrent_streams == 0U) {
            config.max_concurrent_streams = SL_HTTP2_SESSION_DEFAULT_MAX_CONCURRENT_STREAMS;
        }
        if (config.initial_window_size == 0U) {
            config.initial_window_size = SL_HTTP2_SESSION_DEFAULT_INITIAL_WINDOW_SIZE;
        }
    }

    if (config.role != SL_HTTP2_SESSION_ROLE_CLIENT && config.role != SL_HTTP2_SESSION_ROLE_SERVER)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (config.initial_window_size > 0x7fffffffU) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out = config;
    return sl_status_ok();
}

static SlStatus sl_http2_session_push_event(SlHttp2Session* session, SlHttp2Event event)
{
    if (session == NULL || session->events == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (session->event_count >= session->config.max_events) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }

    session->events[session->event_count] = event;
    session->event_count += 1U;
    return sl_status_ok();
}

static SlStatus sl_http2_session_copy_header(SlHttp2Session* session, const uint8_t* name,
                                             size_t namelen, const uint8_t* value, size_t valuelen,
                                             uint8_t flags)
{
    SlStatus status = sl_status_ok();
    SlStr copied_name = {0};
    SlStr copied_value = {0};
    size_t next_bytes = 0U;

    if (session == NULL || session->arena == NULL || session->current_headers == NULL ||
        (namelen != 0U && name == NULL) || (valuelen != 0U && value == NULL))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (session->current_header_count >= session->config.max_headers_per_event) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }
    if (namelen > SIZE_MAX - valuelen ||
        session->current_header_bytes > SIZE_MAX - (namelen + valuelen))
    {
        return sl_status_from_code(SL_STATUS_OVERFLOW);
    }

    next_bytes = session->current_header_bytes + namelen + valuelen;
    if (next_bytes > session->config.max_header_list_bytes) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }

    status = sl_str_copy_view_to_arena(session->arena,
                                       sl_str_from_parts((const char*)name, namelen), &copied_name);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_str_copy_view_to_arena(
        session->arena, sl_str_from_parts((const char*)value, valuelen), &copied_value);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    session->current_headers[session->current_header_count] =
        (SlHttp2HeaderField){.name = copied_name,
                             .value = copied_value,
                             .sensitive = (flags & NGHTTP2_NV_FLAG_NO_INDEX) != 0U};
    session->current_header_count += 1U;
    session->current_header_bytes = next_bytes;
    return sl_status_ok();
}

static SlStatus sl_http2_session_begin_headers(SlHttp2Session* session, const nghttp2_frame* frame)
{
    SlStatus status = sl_status_ok();
    void* storage = NULL;

    if (session == NULL || session->arena == NULL || frame == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_arena_alloc(session->arena,
                            sizeof(SlHttp2HeaderField) * session->config.max_headers_per_event,
                            _Alignof(SlHttp2HeaderField), &storage);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    session->current_headers = (SlHttp2HeaderField*)storage;
    session->current_header_count = 0U;
    session->current_header_bytes = 0U;
    session->current_stream_id = frame->hd.stream_id;
    session->current_header_category =
        frame->hd.type == NGHTTP2_HEADERS ? (uint8_t)frame->headers.cat : 0U;
    return sl_status_ok();
}

static SlHttp2EventType sl_http2_session_headers_event_type(const SlHttp2Session* session,
                                                            uint8_t category)
{
    if (session != NULL && session->config.role == SL_HTTP2_SESSION_ROLE_SERVER &&
        category == NGHTTP2_HCAT_REQUEST)
    {
        return SL_HTTP2_EVENT_REQUEST_HEADERS;
    }
    if (session != NULL && session->config.role == SL_HTTP2_SESSION_ROLE_CLIENT &&
        category == NGHTTP2_HCAT_RESPONSE)
    {
        return SL_HTTP2_EVENT_RESPONSE_HEADERS;
    }
    return SL_HTTP2_EVENT_HEADERS;
}

static SlStatus sl_http2_session_finish_headers(SlHttp2Session* session, const nghttp2_frame* frame)
{
    SlHttp2Event event = {0};

    if (session == NULL || frame == NULL || frame->hd.type != NGHTTP2_HEADERS) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    event.type = sl_http2_session_headers_event_type(session, session->current_header_category);
    event.stream_id = frame->hd.stream_id;
    event.end_stream = (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) != 0U;
    event.headers.fields = session->current_headers;
    event.headers.count = session->current_header_count;
    return sl_http2_session_push_event(session, event);
}

static SlStatus sl_http2_session_copy_data_event(SlHttp2Session* session, uint8_t flags,
                                                 int32_t stream_id, const uint8_t* data, size_t len)
{
    SlStatus status = sl_status_ok();
    SlOwnedBytes owned = {0};

    if (session == NULL || session->arena == NULL || (len != 0U && data == NULL)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (len > session->config.max_event_data_bytes) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }

    status = sl_bytes_copy_to_arena(session->arena, sl_bytes_from_parts(data, len), &owned);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_http2_session_push_event(
        session, (SlHttp2Event){.type = SL_HTTP2_EVENT_DATA,
                                .stream_id = stream_id,
                                .end_stream = (flags & NGHTTP2_FLAG_END_STREAM) != 0U,
                                .data = sl_owned_bytes_as_view(owned)});
}

static int sl_http2_on_begin_headers(nghttp2_session* ng_session, const nghttp2_frame* frame,
                                     void* user_data)
{
    (void)ng_session;
    return sl_http2_session_callback_result(
        (SlHttp2Session*)user_data,
        sl_http2_session_begin_headers((SlHttp2Session*)user_data, frame));
}

static int sl_http2_on_header(nghttp2_session* ng_session, const nghttp2_frame* frame,
                              const uint8_t* name, size_t namelen, const uint8_t* value,
                              size_t valuelen, uint8_t flags, void* user_data)
{
    (void)ng_session;
    (void)frame;
    return sl_http2_session_callback_result(
        (SlHttp2Session*)user_data, sl_http2_session_copy_header((SlHttp2Session*)user_data, name,
                                                                 namelen, value, valuelen, flags));
}

static int sl_http2_on_data_chunk_recv(nghttp2_session* ng_session, uint8_t flags,
                                       int32_t stream_id, const uint8_t* data, size_t len,
                                       void* user_data)
{
    (void)ng_session;
    return sl_http2_session_callback_result(
        (SlHttp2Session*)user_data,
        sl_http2_session_copy_data_event((SlHttp2Session*)user_data, flags, stream_id, data, len));
}

static int sl_http2_on_frame_recv(nghttp2_session* ng_session, const nghttp2_frame* frame,
                                  void* user_data)
{
    SlHttp2Session* session = (SlHttp2Session*)user_data;
    SlStatus status = sl_status_ok();

    (void)ng_session;
    if (session == NULL || frame == NULL) {
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }

    if (frame->hd.type == NGHTTP2_HEADERS) {
        status = sl_http2_session_finish_headers(session, frame);
    }
    else if (frame->hd.type == NGHTTP2_DATA && (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) != 0U) {
        status =
            sl_http2_session_push_event(session, (SlHttp2Event){.type = SL_HTTP2_EVENT_STREAM_END,
                                                                .stream_id = frame->hd.stream_id});
    }
    else if (frame->hd.type == NGHTTP2_RST_STREAM) {
        status = sl_http2_session_push_event(
            session, (SlHttp2Event){.type = SL_HTTP2_EVENT_RST_STREAM,
                                    .stream_id = frame->hd.stream_id,
                                    .error_code = frame->rst_stream.error_code});
    }
    else if (frame->hd.type == NGHTTP2_GOAWAY) {
        status = sl_http2_session_push_event(
            session, (SlHttp2Event){.type = SL_HTTP2_EVENT_GOAWAY,
                                    .last_stream_id = frame->goaway.last_stream_id,
                                    .error_code = frame->goaway.error_code});
    }
    else if (frame->hd.type == NGHTTP2_SETTINGS) {
        status = sl_http2_session_push_event(
            session, (SlHttp2Event){.type = SL_HTTP2_EVENT_SETTINGS,
                                    .end_stream = (frame->hd.flags & NGHTTP2_FLAG_ACK) != 0U});
    }

    return sl_http2_session_callback_result(session, status);
}

static int sl_http2_on_stream_close(nghttp2_session* ng_session, int32_t stream_id,
                                    uint32_t error_code, void* user_data)
{
    (void)ng_session;
    return sl_http2_session_callback_result(
        (SlHttp2Session*)user_data,
        sl_http2_session_push_event((SlHttp2Session*)user_data,
                                    (SlHttp2Event){.type = SL_HTTP2_EVENT_STREAM_CLOSE,
                                                   .stream_id = stream_id,
                                                   .error_code = error_code}));
}

static nghttp2_ssize sl_http2_body_read_callback(nghttp2_session* ng_session, int32_t stream_id,
                                                 uint8_t* buf, size_t length, uint32_t* data_flags,
                                                 nghttp2_data_source* source, void* user_data)
{
    SlHttp2OutboundBody* body = NULL;
    size_t remaining = 0U;
    size_t to_copy = 0U;

    (void)ng_session;
    (void)stream_id;
    (void)user_data;
    if (buf == NULL || data_flags == NULL || source == NULL || source->ptr == NULL) {
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }

    body = (SlHttp2OutboundBody*)source->ptr;
    if (body->offset >= body->bytes.length) {
        *data_flags |= NGHTTP2_DATA_FLAG_EOF;
        return 0;
    }

    remaining = body->bytes.length - body->offset;
    to_copy = remaining < length ? remaining : length;
    for (size_t index = 0U; index < to_copy; index += 1U) {
        buf[index] = body->bytes.ptr[body->offset + index];
    }
    body->offset += to_copy;
    if (body->offset == body->bytes.length) {
        *data_flags |= NGHTTP2_DATA_FLAG_EOF;
    }
    return (nghttp2_ssize)to_copy;
}

static SlStatus sl_http2_session_prepare_nghttp2_headers(SlArena* arena,
                                                         const SlHttp2HeaderList* headers,
                                                         nghttp2_nv** out_nva)
{
    SlStatus status = sl_status_ok();
    void* storage = NULL;
    nghttp2_nv* nva = NULL;

    if (arena == NULL || headers == NULL || out_nva == NULL ||
        (headers->count != 0U && headers->fields == NULL))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (headers->count == 0U) {
        *out_nva = NULL;
        return sl_status_ok();
    }

    status =
        sl_arena_alloc(arena, sizeof(nghttp2_nv) * headers->count, _Alignof(nghttp2_nv), &storage);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    nva = (nghttp2_nv*)storage;
    for (size_t index = 0U; index < headers->count; index += 1U) {
        const SlHttp2HeaderField* field = &headers->fields[index];
        if (!sl_http2_session_valid_str(field->name) || !sl_http2_session_valid_str(field->value)) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        nva[index].name = (uint8_t*)field->name.ptr;
        nva[index].value = (uint8_t*)field->value.ptr;
        nva[index].namelen = field->name.length;
        nva[index].valuelen = field->value.length;
        nva[index].flags = field->sensitive ? NGHTTP2_NV_FLAG_NO_INDEX : NGHTTP2_NV_FLAG_NONE;
    }

    *out_nva = nva;
    return sl_status_ok();
}

static SlStatus sl_http2_session_prepare_body(SlHttp2Session* session, SlBytes body,
                                              nghttp2_data_provider2* out_provider,
                                              const nghttp2_data_provider2** out_provider_ptr)
{
    SlStatus status = sl_status_ok();
    SlOwnedBytes owned = {0};
    void* storage = NULL;
    SlHttp2OutboundBody* outbound = NULL;

    if (session == NULL || session->arena == NULL || out_provider == NULL ||
        out_provider_ptr == NULL || !sl_http2_session_valid_bytes(body))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (body.length == 0U) {
        *out_provider_ptr = NULL;
        return sl_status_ok();
    }

    status = sl_bytes_copy_to_arena(session->arena, body, &owned);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(session->arena, sizeof(SlHttp2OutboundBody),
                            _Alignof(SlHttp2OutboundBody), &storage);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    outbound = (SlHttp2OutboundBody*)storage;
    *outbound = (SlHttp2OutboundBody){.bytes = sl_owned_bytes_as_view(owned), .offset = 0U};
    *out_provider = (nghttp2_data_provider2){0};
    out_provider->source.ptr = outbound;
    out_provider->read_callback = sl_http2_body_read_callback;
    *out_provider_ptr = out_provider;
    return sl_status_ok();
}

SlStatus sl_http2_session_init(SlHttp2Session* session, SlArena* arena,
                               const SlHttp2SessionConfig* config)
{
    SlStatus status = sl_status_ok();
    SlHttp2SessionConfig normalized = {0};
    nghttp2_session_callbacks* callbacks = NULL;
    nghttp2_session* ng_session = NULL;
    nghttp2_settings_entry settings[] = {
        {.settings_id = NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, .value = 0U},
        {.settings_id = NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, .value = 0U}};
    void* event_storage = NULL;
    int rv = 0;

    if (session == NULL || arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_http2_session_normalize_config(config, &normalized);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    rv = nghttp2_session_callbacks_new(&callbacks);
    if (rv != 0) {
        return sl_http2_session_status_from_nghttp2(rv);
    }
    nghttp2_session_callbacks_set_on_begin_headers_callback(callbacks, sl_http2_on_begin_headers);
    nghttp2_session_callbacks_set_on_header_callback(callbacks, sl_http2_on_header);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks,
                                                              sl_http2_on_data_chunk_recv);
    nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, sl_http2_on_frame_recv);
    nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, sl_http2_on_stream_close);

    *session =
        (SlHttp2Session){.arena = arena, .config = normalized, .callback_status = sl_status_ok()};

    if (normalized.role == SL_HTTP2_SESSION_ROLE_CLIENT) {
        rv = nghttp2_session_client_new(&ng_session, callbacks, session);
    }
    else {
        rv = nghttp2_session_server_new(&ng_session, callbacks, session);
    }
    nghttp2_session_callbacks_del(callbacks);
    callbacks = NULL;
    if (rv != 0) {
        *session = (SlHttp2Session){0};
        return sl_http2_session_status_from_nghttp2(rv);
    }
    session->session = ng_session;

    status = sl_arena_alloc(arena, sizeof(SlHttp2Event) * normalized.max_events,
                            _Alignof(SlHttp2Event), &event_storage);
    if (!sl_status_is_ok(status)) {
        sl_http2_session_dispose(session);
        return status;
    }
    session->events = (SlHttp2Event*)event_storage;

    status =
        sl_byte_builder_init_arena(&session->outbound, arena, 1024U, normalized.max_outbound_bytes);
    if (!sl_status_is_ok(status)) {
        sl_http2_session_dispose(session);
        return status;
    }

    settings[0].value = normalized.max_concurrent_streams;
    settings[1].value = normalized.initial_window_size;
    rv = nghttp2_submit_settings((nghttp2_session*)session->session, NGHTTP2_FLAG_NONE, settings,
                                 sizeof(settings) / sizeof(settings[0]));
    if (rv != 0) {
        sl_http2_session_dispose(session);
        return sl_http2_session_status_from_nghttp2(rv);
    }

    return sl_status_ok();
}

void sl_http2_session_dispose(SlHttp2Session* session)
{
    if (session == NULL) {
        return;
    }
    if (session->session != NULL) {
        nghttp2_session_del((nghttp2_session*)session->session);
    }
    *session = (SlHttp2Session){0};
}

SlStatus sl_http2_session_receive(SlHttp2Session* session, SlBytes bytes, size_t* out_consumed)
{
    nghttp2_ssize rv = 0;

    if (session == NULL || session->session == NULL || !sl_http2_session_valid_bytes(bytes)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    session->callback_status = sl_status_ok();
    rv = nghttp2_session_mem_recv2((nghttp2_session*)session->session, bytes.ptr, bytes.length);
    if (out_consumed != NULL && rv >= 0) {
        *out_consumed = (size_t)rv;
    }
    if (rv < 0) {
        if (!sl_status_is_ok(session->callback_status)) {
            return session->callback_status;
        }
        return sl_http2_session_status_from_nghttp2_ssize(rv);
    }
    return sl_status_ok();
}

SlStatus sl_http2_session_drain_output(SlHttp2Session* session, SlBytes* out_bytes)
{
    SlStatus status = sl_status_ok();

    if (session == NULL || session->session == NULL || out_bytes == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    sl_byte_builder_reset(&session->outbound);
    for (;;) {
        const uint8_t* data = NULL;
        nghttp2_ssize rv = nghttp2_session_mem_send2((nghttp2_session*)session->session, &data);
        if (rv < 0) {
            return sl_http2_session_status_from_nghttp2_ssize(rv);
        }
        if (rv == 0) {
            break;
        }
        status =
            sl_byte_builder_append_bytes(&session->outbound, sl_bytes_from_parts(data, (size_t)rv));
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    *out_bytes = sl_byte_builder_view(&session->outbound);
    return sl_status_ok();
}

SlHttp2EventList sl_http2_session_events(const SlHttp2Session* session)
{
    if (session == NULL || session->events == NULL) {
        return (SlHttp2EventList){0};
    }
    return (SlHttp2EventList){.events = session->events, .count = session->event_count};
}

void sl_http2_session_clear_events(SlHttp2Session* session)
{
    if (session == NULL) {
        return;
    }
    session->event_count = 0U;
}

SlStatus sl_http2_session_upgrade_h2c(SlHttp2Session* session, SlBytes settings_payload,
                                      bool head_request)
{
    int rv = 0;

    if (session == NULL || session->session == NULL ||
        !sl_http2_session_valid_bytes(settings_payload))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    session->callback_status = sl_status_ok();
    rv = nghttp2_session_upgrade2((nghttp2_session*)session->session, settings_payload.ptr,
                                  settings_payload.length, head_request ? 1 : 0, NULL);
    if (rv != 0) {
        if (!sl_status_is_ok(session->callback_status)) {
            return session->callback_status;
        }
        return sl_http2_session_status_from_nghttp2(rv);
    }
    return sl_status_ok();
}

SlStatus sl_http2_session_submit_request(SlHttp2Session* session, const SlHttp2HeaderList* headers,
                                         SlBytes body, int32_t* out_stream_id)
{
    SlStatus status = sl_status_ok();
    nghttp2_nv* nva = NULL;
    nghttp2_data_provider2 provider = {0};
    const nghttp2_data_provider2* provider_ptr = NULL;
    int32_t stream_id = 0;

    if (session == NULL || session->session == NULL || headers == NULL || out_stream_id == NULL ||
        session->config.role != SL_HTTP2_SESSION_ROLE_CLIENT)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_http2_session_prepare_nghttp2_headers(session->arena, headers, &nva);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http2_session_prepare_body(session, body, &provider, &provider_ptr);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    stream_id = nghttp2_submit_request2((nghttp2_session*)session->session, NULL, nva,
                                        headers->count, provider_ptr, NULL);
    if (stream_id < 0) {
        return sl_http2_session_status_from_nghttp2(stream_id);
    }

    *out_stream_id = stream_id;
    return sl_status_ok();
}

SlStatus sl_http2_session_submit_response(SlHttp2Session* session, int32_t stream_id,
                                          const SlHttp2HeaderList* headers, SlBytes body)
{
    SlStatus status = sl_status_ok();
    nghttp2_nv* nva = NULL;
    nghttp2_data_provider2 provider = {0};
    const nghttp2_data_provider2* provider_ptr = NULL;
    int rv = 0;

    if (session == NULL || session->session == NULL || headers == NULL ||
        session->config.role != SL_HTTP2_SESSION_ROLE_SERVER)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_http2_session_prepare_nghttp2_headers(session->arena, headers, &nva);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http2_session_prepare_body(session, body, &provider, &provider_ptr);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    rv = nghttp2_submit_response2((nghttp2_session*)session->session, stream_id, nva,
                                  headers->count, provider_ptr);
    return sl_http2_session_status_from_nghttp2(rv);
}

SlStatus sl_http2_session_submit_rst_stream(SlHttp2Session* session, int32_t stream_id,
                                            uint32_t error_code)
{
    int rv = 0;

    if (session == NULL || session->session == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    rv = nghttp2_submit_rst_stream((nghttp2_session*)session->session, NGHTTP2_FLAG_NONE, stream_id,
                                   error_code);
    return sl_http2_session_status_from_nghttp2(rv);
}

SlStatus sl_http2_session_submit_goaway(SlHttp2Session* session, int32_t last_stream_id,
                                        uint32_t error_code)
{
    int rv = 0;

    if (session == NULL || session->session == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    rv = nghttp2_submit_goaway((nghttp2_session*)session->session, NGHTTP2_FLAG_NONE,
                               last_stream_id, error_code, NULL, 0U);
    return sl_http2_session_status_from_nghttp2(rv);
}

bool sl_http2_session_want_read(const SlHttp2Session* session)
{
    return session != NULL && session->session != NULL &&
           nghttp2_session_want_read((nghttp2_session*)session->session) != 0;
}

bool sl_http2_session_want_write(const SlHttp2Session* session)
{
    return session != NULL && session->session != NULL &&
           nghttp2_session_want_write((nghttp2_session*)session->session) != 0;
}

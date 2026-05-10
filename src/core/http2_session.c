#include "sloppy/http2_session.h"

#include "sloppy/builder.h"
#include "sloppy/checked_math.h"

#define NGHTTP2_NO_SSIZE_T 1
#include <nghttp2/nghttp2.h>

#include <stdint.h>

static const unsigned char SL_HTTP2_SESSION_PREFACE[] = {
    'P', 'R', 'I',  ' ',  '*',  ' ',  'H', 'T', 'T',  'P',  '/',  '2',
    '.', '0', '\r', '\n', '\r', '\n', 'S', 'M', '\r', '\n', '\r', '\n'};

typedef struct SlHttp2OutboundBody
{
    SlBytes bytes;
    size_t offset;
    bool active;
} SlHttp2OutboundBody;

typedef struct SlHttp2PrescanAction
{
    size_t receive_length;
    int32_t rst_stream_id;
    uint32_t rst_error_code;
    uint32_t goaway_error_code;
    int invalid_error_code;
    bool consume_all;
    bool submit_rst_stream;
    bool submit_goaway;
} SlHttp2PrescanAction;

static bool sl_http2_session_valid_bytes(SlBytes bytes)
{
    return bytes.length == 0U || bytes.ptr != NULL;
}

static bool sl_http2_session_valid_str(SlStr str)
{
    return str.length == 0U || str.ptr != NULL;
}

static void sl_http2_session_clear_current_headers(SlHttp2Session* session)
{
    if (session == NULL) {
        return;
    }
    session->current_headers = NULL;
    session->current_header_count = 0U;
    session->current_header_bytes = 0U;
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

static void sl_http2_session_release_outbound_body(SlHttp2OutboundBody* body)
{
    if (body == NULL) {
        return;
    }
    *body = (SlHttp2OutboundBody){0};
}

static void sl_http2_session_release_all_outbound_bodies(SlHttp2Session* session)
{
    if (session == NULL) {
        return;
    }
    for (size_t index = 0U; index < session->outbound_body_capacity; index += 1U) {
        sl_http2_session_release_outbound_body(
            &((SlHttp2OutboundBody*)session->outbound_bodies)[index]);
    }
}

static bool sl_http2_session_peer_initiated_stream_id(const SlHttp2Session* session,
                                                      int32_t stream_id)
{
    if (session == NULL || stream_id <= 0) {
        return false;
    }
    if (session->config.role == SL_HTTP2_SESSION_ROLE_SERVER) {
        return (stream_id & 1) != 0;
    }
    return (stream_id & 1) == 0;
}

static SlHttp2ClosedStream* sl_http2_session_find_closed_stream(SlHttp2Session* session,
                                                                int32_t stream_id)
{
    if (session == NULL || stream_id <= 0) {
        return NULL;
    }
    for (size_t index = 0U; index < SL_HTTP2_SESSION_CLOSED_STREAM_TRACK; index += 1U) {
        SlHttp2ClosedStream* closed = &session->closed_streams[index];
        if (closed->active && closed->stream_id == stream_id) {
            return closed;
        }
    }
    return NULL;
}

static SlHttp2ClosedStream* sl_http2_session_track_stream(SlHttp2Session* session,
                                                          int32_t stream_id)
{
    SlHttp2ClosedStream* closed = sl_http2_session_find_closed_stream(session, stream_id);

    if (session == NULL || stream_id <= 0) {
        return NULL;
    }
    if (closed == NULL) {
        closed = &session->closed_streams[session->next_closed_stream %
                                          SL_HTTP2_SESSION_CLOSED_STREAM_TRACK];
        session->next_closed_stream =
            (session->next_closed_stream + 1U) % SL_HTTP2_SESSION_CLOSED_STREAM_TRACK;
        *closed = (SlHttp2ClosedStream){.stream_id = stream_id,
                                        .outbound_window = session->outbound_initial_stream_window,
                                        .active = true,
                                        .outbound_window_known = true};
    }
    return closed;
}

static bool sl_http2_session_accepts_rst_on_remote_closed_stream(SlHttp2Session* session,
                                                                 const nghttp2_frame* frame,
                                                                 int lib_error_code)
{
    SlHttp2ClosedStream* closed = NULL;

    if (session == NULL || frame == NULL || frame->hd.type != NGHTTP2_RST_STREAM ||
        lib_error_code != NGHTTP2_ERR_STREAM_CLOSED ||
        !sl_http2_session_peer_initiated_stream_id(session, frame->hd.stream_id))
    {
        return false;
    }

    closed = sl_http2_session_find_closed_stream(session, frame->hd.stream_id);
    return closed != NULL && closed->remote_closed;
}

static bool sl_http2_session_known_http2_error_code(uint32_t error_code)
{
    return error_code <= NGHTTP2_HTTP_1_1_REQUIRED;
}

static bool sl_http2_session_accepts_unknown_rst_error_code(const nghttp2_frame* frame)
{
    return frame != NULL && frame->hd.type == NGHTTP2_RST_STREAM && frame->hd.stream_id > 0 &&
           !sl_http2_session_known_http2_error_code(frame->rst_stream.error_code);
}

static void sl_http2_session_record_remote_closed_stream(SlHttp2Session* session, int32_t stream_id)
{
    SlHttp2ClosedStream* closed = sl_http2_session_track_stream(session, stream_id);
    if (closed != NULL) {
        closed->remote_closed = true;
    }
}

static void sl_http2_session_record_closed_stream(SlHttp2Session* session, int32_t stream_id,
                                                  bool reset_by_peer)
{
    SlHttp2ClosedStream* closed = sl_http2_session_track_stream(session, stream_id);
    if (closed == NULL) {
        return;
    }
    closed->remote_closed = true;
    closed->reset_by_peer = closed->reset_by_peer || reset_by_peer;
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
    if (config.max_concurrent_streams > SL_HTTP2_SESSION_CLOSED_STREAM_TRACK) {
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
    size_t allocation_size = 0U;

    if (session == NULL || session->arena == NULL || frame == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_checked_array_size(session->config.max_headers_per_event,
                                   sizeof(SlHttp2HeaderField), &allocation_size);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status =
        sl_arena_alloc(session->arena, allocation_size, _Alignof(SlHttp2HeaderField), &storage);
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
    session->current_headers = NULL;
    session->current_header_count = 0U;
    session->current_header_bytes = 0U;
    return sl_http2_session_push_event(session, event);
}

static SlStatus sl_http2_session_note_invalid_frame(SlHttp2Session* session, int32_t stream_id,
                                                    int lib_error_code)
{
    if (session == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_http2_session_push_event(session,
                                       (SlHttp2Event){.type = SL_HTTP2_EVENT_INVALID_FRAME,
                                                      .stream_id = stream_id,
                                                      .error_code = (uint32_t)(-lib_error_code)});
}

static bool sl_http2_session_bytes_start_with_preface(SlBytes bytes)
{
    if (bytes.ptr == NULL || bytes.length < sizeof(SL_HTTP2_SESSION_PREFACE)) {
        return false;
    }
    for (size_t index = 0U; index < sizeof(SL_HTTP2_SESSION_PREFACE); index += 1U) {
        if (bytes.ptr[index] != SL_HTTP2_SESSION_PREFACE[index]) {
            return false;
        }
    }
    return true;
}

static int32_t sl_http2_session_frame_stream_id(const unsigned char* ptr)
{
    uint32_t raw = ((uint32_t)(ptr[5] & 0x7fU) << 24U) | ((uint32_t)ptr[6] << 16U) |
                   ((uint32_t)ptr[7] << 8U) | (uint32_t)ptr[8];
    return raw > (uint32_t)INT32_MAX ? 0 : (int32_t)raw;
}

static uint32_t sl_http2_session_read_u32(const unsigned char* ptr)
{
    return ((uint32_t)ptr[0] << 24U) | ((uint32_t)ptr[1] << 16U) | ((uint32_t)ptr[2] << 8U) |
           (uint32_t)ptr[3];
}

static SlStatus sl_http2_session_submit_prescan_rst_stream(SlHttp2Session* session,
                                                           int32_t stream_id, uint32_t error_code)
{
    int rv = 0;

    if (session == NULL || session->session == NULL || stream_id <= 0) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    rv = nghttp2_submit_rst_stream((nghttp2_session*)session->session, NGHTTP2_FLAG_NONE, stream_id,
                                   error_code);
    if (rv == 0 || rv == NGHTTP2_ERR_STREAM_CLOSED || rv == NGHTTP2_ERR_INVALID_STREAM_ID) {
        return sl_status_ok();
    }
    return sl_http2_session_status_from_nghttp2(rv);
}

static SlStatus sl_http2_session_submit_prescan_goaway(SlHttp2Session* session, uint32_t error_code)
{
    int rv = 0;

    if (session == NULL || session->session == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    rv = nghttp2_submit_goaway((nghttp2_session*)session->session, NGHTTP2_FLAG_NONE,
                               session->highest_peer_stream_id, error_code, NULL, 0U);
    if (rv == 0 || rv == NGHTTP2_ERR_GOAWAY_ALREADY_SENT || rv == NGHTTP2_ERR_SESSION_CLOSING) {
        return sl_status_ok();
    }
    return sl_http2_session_status_from_nghttp2(rv);
}

static SlStatus sl_http2_session_track_peer_settings(SlHttp2Session* session,
                                                     const unsigned char* payload, size_t length)
{
    if (session == NULL || payload == NULL || length % 6U != 0U) {
        return sl_status_ok();
    }
    for (size_t offset = 0U; offset < length; offset += 6U) {
        uint16_t id =
            (uint16_t)(((uint16_t)payload[offset] << 8U) | (uint16_t)payload[offset + 1U]);
        uint32_t value = sl_http2_session_read_u32(payload + offset + 2U);
        int64_t delta = 0;

        if (id != NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE) {
            continue;
        }
        if (value > (uint32_t)INT32_MAX) {
            return sl_http2_session_submit_prescan_goaway(session, NGHTTP2_FLOW_CONTROL_ERROR);
        }

        delta = (int64_t)value - session->outbound_initial_stream_window;
        session->outbound_initial_stream_window = (int64_t)value;
        for (size_t index = 0U; index < SL_HTTP2_SESSION_CLOSED_STREAM_TRACK; index += 1U) {
            SlHttp2ClosedStream* stream = &session->closed_streams[index];
            if (!stream->active || !stream->outbound_window_known) {
                continue;
            }
            stream->outbound_window += delta;
            if (stream->outbound_window > INT32_MAX) {
                return sl_http2_session_submit_prescan_goaway(session, NGHTTP2_FLOW_CONTROL_ERROR);
            }
        }
    }
    return sl_status_ok();
}

static SlStatus sl_http2_session_track_window_update(SlHttp2Session* session, int32_t stream_id,
                                                     const unsigned char* payload, size_t length,
                                                     size_t frame_offset,
                                                     SlHttp2PrescanAction* action)
{
    uint32_t increment = 0U;

    if (session == NULL || payload == NULL || length != 4U) {
        return sl_status_ok();
    }

    increment = sl_http2_session_read_u32(payload) & 0x7fffffffU;
    if (increment == 0U) {
        return sl_status_ok();
    }
    if (stream_id == 0) {
        if (session->outbound_connection_window > (int64_t)INT32_MAX - (int64_t)increment) {
            if (action != NULL) {
                action->receive_length = frame_offset;
                action->goaway_error_code = NGHTTP2_FLOW_CONTROL_ERROR;
                action->invalid_error_code = -NGHTTP2_ERR_FLOW_CONTROL;
                action->consume_all = true;
                action->submit_goaway = true;
            }
            return sl_status_ok();
        }
        session->outbound_connection_window += (int64_t)increment;
        return sl_status_ok();
    }

    if (sl_http2_session_peer_initiated_stream_id(session, stream_id)) {
        SlHttp2ClosedStream* stream = sl_http2_session_track_stream(session, stream_id);
        if (stream == NULL) {
            return sl_status_ok();
        }
        if (!stream->outbound_window_known) {
            stream->outbound_window = session->outbound_initial_stream_window;
            stream->outbound_window_known = true;
        }
        if (stream->outbound_window > (int64_t)INT32_MAX - (int64_t)increment) {
            if (action != NULL) {
                action->receive_length = frame_offset;
                action->rst_stream_id = stream_id;
                action->rst_error_code = NGHTTP2_FLOW_CONTROL_ERROR;
                action->invalid_error_code = -NGHTTP2_ERR_FLOW_CONTROL;
                action->consume_all = true;
                action->submit_rst_stream = true;
            }
            return sl_status_ok();
        }
        stream->outbound_window += (int64_t)increment;
    }

    return sl_status_ok();
}

static void sl_http2_session_account_sent_data(SlHttp2Session* session, int32_t stream_id,
                                               size_t length)
{
    SlHttp2ClosedStream* stream = NULL;

    if (session == NULL || length == 0U || length > (size_t)INT64_MAX) {
        return;
    }

    session->outbound_connection_window -= (int64_t)length;
    stream = sl_http2_session_track_stream(session, stream_id);
    if (stream != NULL) {
        stream->outbound_window -= (int64_t)length;
        stream->outbound_window_known = true;
    }
}

static SlStatus sl_http2_session_prescan_frames(SlHttp2Session* session, SlBytes bytes,
                                                SlHttp2PrescanAction* action)
{
    size_t offset = 0U;

    if (action != NULL) {
        *action = (SlHttp2PrescanAction){.receive_length = bytes.length};
    }
    if (session == NULL || session->config.role != SL_HTTP2_SESSION_ROLE_SERVER ||
        !sl_http2_session_valid_bytes(bytes))
    {
        return sl_status_ok();
    }
    if (sl_http2_session_bytes_start_with_preface(bytes)) {
        offset = sizeof(SL_HTTP2_SESSION_PREFACE);
    }
    bool header_block_open = false;
    while (bytes.length - offset >= 9U) {
        const unsigned char* frame = &bytes.ptr[offset];
        size_t length = ((size_t)frame[0] << 16U) | ((size_t)frame[1] << 8U) | (size_t)frame[2];
        uint8_t type = frame[3];
        uint8_t flags = frame[4];
        int32_t stream_id = sl_http2_session_frame_stream_id(frame);
        SlHttp2ClosedStream* closed = NULL;
        const uint8_t continuation_type = NGHTTP2_CONTINUATION;
        const uint8_t headers_type = NGHTTP2_HEADERS;
        const uint8_t push_promise_type = NGHTTP2_PUSH_PROMISE;
        const uint8_t end_headers_flag = NGHTTP2_FLAG_END_HEADERS;

        if (length > bytes.length - offset - 9U) {
            break;
        }
        if ((!header_block_open && type == continuation_type) ||
            (header_block_open && type != continuation_type))
        {
            SlStatus status =
                sl_http2_session_submit_prescan_goaway(session, NGHTTP2_PROTOCOL_ERROR);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            status = sl_http2_session_note_invalid_frame(session, stream_id, -NGHTTP2_ERR_PROTO);
            if (!sl_status_is_ok(status)) {
                return status;
            }
        }
        if (!header_block_open && (type == headers_type || type == push_promise_type) &&
            (flags & end_headers_flag) == 0U)
        {
            header_block_open = true;
        }
        else if (header_block_open && type == continuation_type && (flags & end_headers_flag) != 0U)
        {
            header_block_open = false;
        }
        if (type == NGHTTP2_PING && stream_id != 0) {
            session->close_without_goaway = true;
            return sl_status_ok();
        }
        if (type == NGHTTP2_SETTINGS && stream_id == 0 && (flags & NGHTTP2_FLAG_ACK) == 0U) {
            SlStatus status = sl_http2_session_track_peer_settings(session, &frame[9], length);
            if (!sl_status_is_ok(status)) {
                return status;
            }
        }
        if (type == NGHTTP2_SETTINGS && stream_id == 0 && (flags & NGHTTP2_FLAG_ACK) != 0U &&
            length != 0U)
        {
            SlStatus status =
                sl_http2_session_submit_prescan_goaway(session, NGHTTP2_PROTOCOL_ERROR);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            status = sl_http2_session_note_invalid_frame(session, stream_id, -NGHTTP2_ERR_PROTO);
            if (!sl_status_is_ok(status)) {
                return status;
            }
        }
        if (type == NGHTTP2_WINDOW_UPDATE) {
            SlStatus status = sl_http2_session_track_window_update(session, stream_id, &frame[9],
                                                                   length, offset, action);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            if (action != NULL && action->consume_all) {
                return sl_status_ok();
            }
        }
        if (sl_http2_session_peer_initiated_stream_id(session, stream_id)) {
            closed = sl_http2_session_find_closed_stream(session, stream_id);
            if (closed != NULL && closed->remote_closed &&
                (type == NGHTTP2_DATA || type == NGHTTP2_HEADERS))
            {
                SlStatus status =
                    sl_http2_session_submit_prescan_goaway(session, NGHTTP2_STREAM_CLOSED);
                if (!sl_status_is_ok(status)) {
                    return status;
                }
                status = sl_http2_session_note_invalid_frame(session, stream_id,
                                                             -NGHTTP2_ERR_STREAM_CLOSED);
                if (!sl_status_is_ok(status)) {
                    return status;
                }
            }
            if (type == NGHTTP2_RST_STREAM) {
                sl_http2_session_record_closed_stream(session, stream_id, true);
            }
            if (type == NGHTTP2_HEADERS && nghttp2_session_get_stream_remote_close(
                                               (nghttp2_session*)session->session, stream_id) == -1)
            {
                if (stream_id > session->highest_peer_stream_id) {
                    session->highest_peer_stream_id = stream_id;
                }
                (void)sl_http2_session_track_stream(session, stream_id);
            }
            if ((type == NGHTTP2_HEADERS || type == NGHTTP2_DATA) &&
                (flags & NGHTTP2_FLAG_END_STREAM) != 0U)
            {
                sl_http2_session_record_remote_closed_stream(session, stream_id);
            }
            if (type == NGHTTP2_PRIORITY && length >= 5U) {
                const unsigned char* payload = &frame[9];
                int32_t dependency =
                    (((uint32_t)(payload[0] & 0x7fU) << 24U) | ((uint32_t)payload[1] << 16U) |
                     ((uint32_t)payload[2] << 8U) | (uint32_t)payload[3]) > (uint32_t)INT32_MAX
                        ? 0
                        : (int32_t)(((uint32_t)(payload[0] & 0x7fU) << 24U) |
                                    ((uint32_t)payload[1] << 16U) | ((uint32_t)payload[2] << 8U) |
                                    (uint32_t)payload[3]);
                if (dependency == stream_id) {
                    SlStatus status =
                        sl_http2_session_submit_prescan_goaway(session, NGHTTP2_PROTOCOL_ERROR);
                    if (!sl_status_is_ok(status)) {
                        return status;
                    }
                    status =
                        sl_http2_session_note_invalid_frame(session, stream_id, -NGHTTP2_ERR_PROTO);
                    if (!sl_status_is_ok(status)) {
                        return status;
                    }
                }
            }
        }
        offset += 9U + length;
    }
    return sl_status_ok();
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

static int sl_http2_on_begin_frame(nghttp2_session* ng_session, const nghttp2_frame_hd* hd,
                                   void* user_data)
{
    SlHttp2Session* session = (SlHttp2Session*)user_data;
    SlHttp2ClosedStream* closed = NULL;
    int rv = 0;

    if (session == NULL || hd == NULL || ng_session == NULL) {
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }

    if (session->config.role == SL_HTTP2_SESSION_ROLE_SERVER && hd->type == NGHTTP2_PING &&
        hd->stream_id != 0)
    {
        session->close_without_goaway = true;
        return 0;
    }

    if (session->config.role == SL_HTTP2_SESSION_ROLE_SERVER && hd->type == NGHTTP2_PRIORITY &&
        hd->stream_id == 0)
    {
        rv = nghttp2_submit_goaway(ng_session, NGHTTP2_FLAG_NONE, session->highest_peer_stream_id,
                                   NGHTTP2_PROTOCOL_ERROR, NULL, 0U);
        if (rv != 0) {
            return sl_http2_session_callback_result(session,
                                                    sl_http2_session_status_from_nghttp2(rv));
        }
        return sl_http2_session_callback_result(
            session,
            sl_http2_session_note_invalid_frame(session, hd->stream_id, -NGHTTP2_ERR_PROTO));
    }

    if (!sl_http2_session_peer_initiated_stream_id(session, hd->stream_id)) {
        return 0;
    }

    closed = sl_http2_session_find_closed_stream(session, hd->stream_id);
    if (closed != NULL && closed->reset_by_peer && hd->type != NGHTTP2_PRIORITY &&
        hd->type != NGHTTP2_RST_STREAM)
    {
        rv = nghttp2_submit_goaway(ng_session, NGHTTP2_FLAG_NONE, session->highest_peer_stream_id,
                                   NGHTTP2_STREAM_CLOSED, NULL, 0U);
        if (rv != 0) {
            return sl_http2_session_callback_result(session,
                                                    sl_http2_session_status_from_nghttp2(rv));
        }
        return sl_http2_session_callback_result(
            session, sl_http2_session_note_invalid_frame(session, hd->stream_id,
                                                         -NGHTTP2_ERR_STREAM_CLOSED));
    }
    if (hd->type == NGHTTP2_HEADERS &&
        nghttp2_session_get_stream_remote_close(ng_session, hd->stream_id) == -1)
    {
        if (hd->stream_id < session->highest_peer_stream_id) {
            rv = nghttp2_submit_goaway(ng_session, NGHTTP2_FLAG_NONE,
                                       session->highest_peer_stream_id, NGHTTP2_PROTOCOL_ERROR,
                                       NULL, 0U);
            if (rv != 0) {
                return sl_http2_session_callback_result(session,
                                                        sl_http2_session_status_from_nghttp2(rv));
            }
            return sl_http2_session_callback_result(
                session,
                sl_http2_session_note_invalid_frame(session, hd->stream_id, -NGHTTP2_ERR_PROTO));
        }
        session->highest_peer_stream_id = hd->stream_id;
    }

    return 0;
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
    SlHttp2Session* session = (SlHttp2Session*)user_data;

    (void)ng_session;
    return sl_http2_session_callback_result(
        session, sl_http2_session_copy_data_event(session, flags, stream_id, data, len));
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
        sl_http2_session_record_remote_closed_stream(session, frame->hd.stream_id);
        status =
            sl_http2_session_push_event(session, (SlHttp2Event){.type = SL_HTTP2_EVENT_STREAM_END,
                                                                .stream_id = frame->hd.stream_id});
    }
    else if (frame->hd.type == NGHTTP2_RST_STREAM) {
        sl_http2_session_record_closed_stream(session, frame->hd.stream_id, true);
        status = sl_http2_session_push_event(
            session, (SlHttp2Event){.type = SL_HTTP2_EVENT_RST_STREAM,
                                    .stream_id = frame->hd.stream_id,
                                    .error_code = frame->rst_stream.error_code});
    }
    else if (frame->hd.type == NGHTTP2_GOAWAY) {
        session->received_goaway = true;
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

    if (frame->hd.type == NGHTTP2_HEADERS && (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) != 0U) {
        sl_http2_session_record_remote_closed_stream(session, frame->hd.stream_id);
    }

    return sl_http2_session_callback_result(session, status);
}

static int sl_http2_on_invalid_frame_recv(nghttp2_session* ng_session, const nghttp2_frame* frame,
                                          int lib_error_code, void* user_data)
{
    SlHttp2Session* session = (SlHttp2Session*)user_data;
    int rv = 0;

    if (sl_http2_session_accepts_rst_on_remote_closed_stream(session, frame, lib_error_code)) {
        sl_http2_session_record_closed_stream(session, frame->hd.stream_id, true);
        return sl_http2_session_callback_result(
            session, sl_http2_session_push_event(
                         session, (SlHttp2Event){.type = SL_HTTP2_EVENT_RST_STREAM,
                                                 .stream_id = frame->hd.stream_id,
                                                 .error_code = frame->rst_stream.error_code}));
    }

    if (sl_http2_session_accepts_unknown_rst_error_code(frame)) {
        sl_http2_session_record_closed_stream(session, frame->hd.stream_id, true);
        return sl_http2_session_callback_result(
            session, sl_http2_session_push_event(
                         session, (SlHttp2Event){.type = SL_HTTP2_EVENT_RST_STREAM,
                                                 .stream_id = frame->hd.stream_id,
                                                 .error_code = frame->rst_stream.error_code}));
    }

    if (session != NULL && ng_session != NULL && frame != NULL) {
        rv = nghttp2_submit_goaway(ng_session, NGHTTP2_FLAG_NONE, session->highest_peer_stream_id,
                                   NGHTTP2_PROTOCOL_ERROR, NULL, 0U);
        if (rv != 0 && rv != NGHTTP2_ERR_STREAM_CLOSED) {
            return sl_http2_session_callback_result(session,
                                                    sl_http2_session_status_from_nghttp2(rv));
        }
    }
    return sl_http2_session_callback_result(
        session, sl_http2_session_note_invalid_frame(
                     session, frame == NULL ? 0 : frame->hd.stream_id, lib_error_code));
}

static int sl_http2_on_stream_close(nghttp2_session* ng_session, int32_t stream_id,
                                    uint32_t error_code, void* user_data)
{
    (void)ng_session;
    sl_http2_session_record_closed_stream((SlHttp2Session*)user_data, stream_id, false);
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
    if (buf == NULL || data_flags == NULL || source == NULL || source->ptr == NULL) {
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }

    body = (SlHttp2OutboundBody*)source->ptr;
    if (body->offset >= body->bytes.length) {
        *data_flags |= NGHTTP2_DATA_FLAG_EOF;
        sl_http2_session_release_outbound_body(body);
        source->ptr = NULL;
        return 0;
    }

    remaining = body->bytes.length - body->offset;
    to_copy = remaining < length ? remaining : length;
    for (size_t index = 0U; index < to_copy; index += 1U) {
        buf[index] = body->bytes.ptr[body->offset + index];
    }
    body->offset += to_copy;
    sl_http2_session_account_sent_data((SlHttp2Session*)user_data, stream_id, to_copy);
    if (body->offset == body->bytes.length) {
        *data_flags |= NGHTTP2_DATA_FLAG_EOF;
        sl_http2_session_release_outbound_body(body);
        source->ptr = NULL;
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
    size_t allocation_size = 0U;

    if (arena == NULL || headers == NULL || out_nva == NULL ||
        (headers->count != 0U && headers->fields == NULL))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (headers->count == 0U) {
        *out_nva = NULL;
        return sl_status_ok();
    }

    status = sl_checked_array_size(headers->count, sizeof(nghttp2_nv), &allocation_size);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(arena, allocation_size, _Alignof(nghttp2_nv), &storage);
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

    for (size_t index = 0U; index < session->outbound_body_capacity; index += 1U) {
        SlHttp2OutboundBody* candidate = &((SlHttp2OutboundBody*)session->outbound_bodies)[index];
        if (!candidate->active) {
            outbound = candidate;
            break;
        }
    }
    if (outbound == NULL) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }

    *outbound = (SlHttp2OutboundBody){.bytes = body, .offset = 0U, .active = true};
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
    size_t event_storage_size = 0U;
    void* outbound_body_storage = NULL;
    size_t outbound_body_storage_size = 0U;
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
    nghttp2_session_callbacks_set_on_begin_frame_callback(callbacks, sl_http2_on_begin_frame);
    nghttp2_session_callbacks_set_on_header_callback(callbacks, sl_http2_on_header);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks,
                                                              sl_http2_on_data_chunk_recv);
    nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, sl_http2_on_frame_recv);
    nghttp2_session_callbacks_set_on_invalid_frame_recv_callback(callbacks,
                                                                 sl_http2_on_invalid_frame_recv);
    nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, sl_http2_on_stream_close);

    *session = (SlHttp2Session){
        .arena = arena,
        .config = normalized,
        .outbound_connection_window = (int64_t)SL_HTTP2_SESSION_DEFAULT_INITIAL_WINDOW_SIZE,
        .outbound_initial_stream_window = (int64_t)SL_HTTP2_SESSION_DEFAULT_INITIAL_WINDOW_SIZE,
        .callback_status = sl_status_ok()};

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

    status =
        sl_checked_array_size(normalized.max_events, sizeof(SlHttp2Event), &event_storage_size);
    if (!sl_status_is_ok(status)) {
        sl_http2_session_dispose(session);
        return status;
    }
    status = sl_arena_alloc(arena, event_storage_size, _Alignof(SlHttp2Event), &event_storage);
    if (!sl_status_is_ok(status)) {
        sl_http2_session_dispose(session);
        return status;
    }
    session->events = (SlHttp2Event*)event_storage;

    status = sl_checked_array_size(normalized.max_events, sizeof(SlHttp2OutboundBody),
                                   &outbound_body_storage_size);
    if (!sl_status_is_ok(status)) {
        sl_http2_session_dispose(session);
        return status;
    }
    status = sl_arena_alloc(arena, outbound_body_storage_size, _Alignof(SlHttp2OutboundBody),
                            &outbound_body_storage);
    if (!sl_status_is_ok(status)) {
        sl_http2_session_dispose(session);
        return status;
    }
    session->outbound_bodies = outbound_body_storage;
    session->outbound_body_capacity = normalized.max_events;
    for (size_t index = 0U; index < session->outbound_body_capacity; index += 1U) {
        ((SlHttp2OutboundBody*)session->outbound_bodies)[index] = (SlHttp2OutboundBody){0};
    }

    status =
        sl_byte_builder_init_arena(&session->outbound, arena, 1024U, normalized.max_outbound_bytes);
    if (!sl_status_is_ok(status)) {
        sl_http2_session_dispose(session);
        return status;
    }
    session->event_mark = sl_arena_mark(arena);
    session->event_mark_valid = true;

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
    sl_http2_session_clear_events(session);
    sl_http2_session_clear_current_headers(session);
    sl_http2_session_release_all_outbound_bodies(session);
    *session = (SlHttp2Session){0};
}

SlStatus sl_http2_session_receive(SlHttp2Session* session, SlBytes bytes, size_t* out_consumed)
{
    nghttp2_ssize rv = 0;
    SlStatus status = sl_status_ok();
    SlHttp2PrescanAction prescan = {0};
    SlBytes receive_bytes = {0};

    if (session == NULL || session->session == NULL || !sl_http2_session_valid_bytes(bytes)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    session->callback_status = sl_status_ok();
    status = sl_http2_session_prescan_frames(session, bytes, &prescan);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (session->close_without_goaway) {
        if (out_consumed != NULL) {
            *out_consumed = bytes.length;
        }
        return sl_status_ok();
    }
    receive_bytes = bytes;
    if (prescan.receive_length < receive_bytes.length) {
        receive_bytes.length = prescan.receive_length;
    }
    rv = nghttp2_session_mem_recv2((nghttp2_session*)session->session, receive_bytes.ptr,
                                   receive_bytes.length);
    if (rv >= 0 && prescan.submit_rst_stream) {
        status = sl_http2_session_submit_prescan_rst_stream(session, prescan.rst_stream_id,
                                                            prescan.rst_error_code);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    if (rv >= 0 && prescan.submit_goaway) {
        status = sl_http2_session_submit_prescan_goaway(session, prescan.goaway_error_code);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    if (rv >= 0 && prescan.invalid_error_code != 0) {
        status = sl_http2_session_note_invalid_frame(session, prescan.rst_stream_id,
                                                     prescan.invalid_error_code);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    if (out_consumed != NULL && rv >= 0) {
        *out_consumed = prescan.consume_all ? bytes.length : (size_t)rv;
    }
    if (session->close_without_goaway) {
        if (out_consumed != NULL) {
            *out_consumed = bytes.length;
        }
        return sl_status_ok();
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
    sl_http2_session_clear_current_headers(session);
    if (session->event_mark_valid) {
        (void)sl_arena_reset_to(session->arena, session->event_mark);
    }
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
    SlArenaMark mark = {0};

    if (session == NULL || session->session == NULL || headers == NULL || out_stream_id == NULL ||
        session->config.role != SL_HTTP2_SESSION_ROLE_CLIENT)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (session->received_goaway) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    mark = sl_arena_mark(session->arena);
    status = sl_http2_session_prepare_nghttp2_headers(session->arena, headers, &nva);
    if (!sl_status_is_ok(status)) {
        (void)sl_arena_reset_to(session->arena, mark);
        return status;
    }
    status = sl_http2_session_prepare_body(session, body, &provider, &provider_ptr);
    if (!sl_status_is_ok(status)) {
        (void)sl_arena_reset_to(session->arena, mark);
        return status;
    }

    stream_id = nghttp2_submit_request2((nghttp2_session*)session->session, NULL, nva,
                                        headers->count, provider_ptr, NULL);
    if (stream_id < 0) {
        if (provider.source.ptr != NULL) {
            sl_http2_session_release_outbound_body((SlHttp2OutboundBody*)provider.source.ptr);
        }
        (void)sl_arena_reset_to(session->arena, mark);
        return sl_http2_session_status_from_nghttp2(stream_id);
    }

    (void)sl_arena_reset_to(session->arena, mark);
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
    SlArenaMark mark = {0};

    if (session == NULL || session->session == NULL || headers == NULL ||
        session->config.role != SL_HTTP2_SESSION_ROLE_SERVER)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    mark = sl_arena_mark(session->arena);
    status = sl_http2_session_prepare_nghttp2_headers(session->arena, headers, &nva);
    if (!sl_status_is_ok(status)) {
        (void)sl_arena_reset_to(session->arena, mark);
        return status;
    }
    status = sl_http2_session_prepare_body(session, body, &provider, &provider_ptr);
    if (!sl_status_is_ok(status)) {
        (void)sl_arena_reset_to(session->arena, mark);
        return status;
    }

    rv = nghttp2_submit_response2((nghttp2_session*)session->session, stream_id, nva,
                                  headers->count, provider_ptr);
    if (rv != 0 && provider.source.ptr != NULL) {
        sl_http2_session_release_outbound_body((SlHttp2OutboundBody*)provider.source.ptr);
    }
    (void)sl_arena_reset_to(session->arena, mark);
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

bool sl_http2_session_close_without_goaway(const SlHttp2Session* session)
{
    return session != NULL && session->close_without_goaway;
}

#include "sloppy/http2_dispatch.h"

#include "sloppy/checked_math.h"

#include <stdint.h>

static SlStr sl_http2_dispatch_literal(const char* ptr, size_t length)
{
    return sl_str_from_parts(ptr, length);
}

static uint16_t sl_http2_dispatch_status_for_failure(SlStatus status, const SlDiag* diag)
{
    SlStatusCode code = sl_status_code(status);

    if (diag != NULL) {
        if (diag->code == SL_DIAG_HTTP_ROUTE_NOT_FOUND) {
            return 404U;
        }
        if (diag->code == SL_DIAG_HTTP_UNSUPPORTED_METHOD) {
            return 405U;
        }
        if (diag->code == SL_DIAG_HTTP_BODY_LIMIT || diag->code == SL_DIAG_HTTP_HEADER_LIMIT ||
            diag->code == SL_DIAG_HTTP_HEADER_BYTES_LIMIT ||
            diag->code == SL_DIAG_HTTP_REQUEST_LINE_LIMIT ||
            diag->code == SL_DIAG_HTTP_TARGET_LIMIT ||
            diag->code == SL_DIAG_HTTP_HEADER_NAME_LIMIT ||
            diag->code == SL_DIAG_HTTP_HEADER_VALUE_LIMIT)
        {
            return 413U;
        }
        if (diag->code == SL_DIAG_HTTP_UNSUPPORTED_MEDIA_TYPE) {
            return 415U;
        }
        if (diag->code == SL_DIAG_HTTP_UNSUPPORTED_BODY) {
            return 501U;
        }
        if (diag->code == SL_DIAG_INVALID_HTTP_REQUEST || diag->code == SL_DIAG_MALFORMED_JSON ||
            diag->code == SL_DIAG_HTTP_KEEP_ALIVE_UNSUPPORTED)
        {
            return 400U;
        }
    }

    if (code == SL_STATUS_CAPACITY_EXCEEDED) {
        return 413U;
    }
    if (code == SL_STATUS_UNSUPPORTED) {
        return 501U;
    }
    if (code == SL_STATUS_INVALID_ARGUMENT) {
        return 400U;
    }
    return 500U;
}

static SlStr sl_http2_dispatch_body_for_status(uint16_t status)
{
    switch (status) {
    case 400U:
        return sl_http2_dispatch_literal("Malformed HTTP request\n",
                                         sizeof("Malformed HTTP request\n") - 1U);
    case 404U:
        return sl_http2_dispatch_literal("Not Found\n", sizeof("Not Found\n") - 1U);
    case 405U:
        return sl_http2_dispatch_literal("Method Not Allowed\n",
                                         sizeof("Method Not Allowed\n") - 1U);
    case 408U:
        return sl_http2_dispatch_literal("Request Timeout\n", sizeof("Request Timeout\n") - 1U);
    case 413U:
        return sl_http2_dispatch_literal("Payload Too Large\n", sizeof("Payload Too Large\n") - 1U);
    case 415U:
        return sl_http2_dispatch_literal("Unsupported Media Type\n",
                                         sizeof("Unsupported Media Type\n") - 1U);
    case 417U:
        return sl_http2_dispatch_literal("Expectation Failed\n",
                                         sizeof("Expectation Failed\n") - 1U);
    case 501U:
        return sl_http2_dispatch_literal("Request body framing is not supported\n",
                                         sizeof("Request body framing is not supported\n") - 1U);
    default:
        return sl_http2_dispatch_literal("Sloppy handler failed\n",
                                         sizeof("Sloppy handler failed\n") - 1U);
    }
}

static SlStr sl_http2_dispatch_body_for_failure(uint16_t status, const SlDiag* diag)
{
    if (diag != NULL && diag->code == SL_DIAG_MALFORMED_JSON) {
        return sl_http2_dispatch_literal("Malformed JSON\n", sizeof("Malformed JSON\n") - 1U);
    }
    return sl_http2_dispatch_body_for_status(status);
}

static SlHttp2DispatchConfig sl_http2_dispatch_default_config(void)
{
    SlHttp2SessionConfig session = {.role = SL_HTTP2_SESSION_ROLE_SERVER};

    return (SlHttp2DispatchConfig){.max_streams = SL_HTTP2_DISPATCH_DEFAULT_MAX_STREAMS,
                                   .max_body_bytes = SL_HTTP2_DISPATCH_DEFAULT_MAX_BODY_BYTES,
                                   .max_response_body_bytes =
                                       SL_HTTP2_DISPATCH_DEFAULT_MAX_RESPONSE_BODY_BYTES,
                                   .session = session};
}

static SlStatus sl_http2_dispatch_normalize_config(const SlHttp2DispatchConfig* input,
                                                   SlHttp2DispatchConfig* out)
{
    SlHttp2DispatchConfig config = sl_http2_dispatch_default_config();

    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (input != NULL) {
        config = *input;
        if (config.max_streams == 0U) {
            config.max_streams = SL_HTTP2_DISPATCH_DEFAULT_MAX_STREAMS;
        }
        if (config.max_body_bytes == 0U) {
            config.max_body_bytes = SL_HTTP2_DISPATCH_DEFAULT_MAX_BODY_BYTES;
        }
        if (config.max_response_body_bytes == 0U) {
            config.max_response_body_bytes = SL_HTTP2_DISPATCH_DEFAULT_MAX_RESPONSE_BODY_BYTES;
        }
        config.session.role = SL_HTTP2_SESSION_ROLE_SERVER;
    }
    if (config.dispatch == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (config.max_streams == 0U || config.max_body_bytes == 0U ||
        config.max_response_body_bytes == 0U)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out = config;
    return sl_status_ok();
}

static SlHttp2DispatchStream* sl_http2_dispatch_find_stream(SlHttp2ServerDispatcher* dispatcher,
                                                            int32_t stream_id)
{
    if (dispatcher == NULL || dispatcher->streams == NULL || stream_id <= 0) {
        return NULL;
    }
    for (size_t index = 0U; index < dispatcher->config.max_streams; index += 1U) {
        SlHttp2DispatchStream* stream = &dispatcher->streams[index];
        if (stream->active && stream->stream_id == stream_id) {
            return stream;
        }
    }
    return NULL;
}

static SlStatus sl_http2_dispatch_claim_stream(SlHttp2ServerDispatcher* dispatcher,
                                               int32_t stream_id,
                                               SlHttp2DispatchStream** out_stream)
{
    SlStatus status;

    if (dispatcher == NULL || dispatcher->streams == NULL || out_stream == NULL || stream_id <= 0) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_stream = NULL;
    if (sl_http2_dispatch_find_stream(dispatcher, stream_id) != NULL ||
        dispatcher->active_streams >= dispatcher->config.max_streams)
    {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }

    for (size_t index = 0U; index < dispatcher->config.max_streams; index += 1U) {
        SlHttp2DispatchStream* stream = &dispatcher->streams[index];
        if (!stream->active) {
            size_t initial_capacity =
                dispatcher->config.max_body_bytes < 256U ? dispatcher->config.max_body_bytes : 256U;
            if (stream->body_mark_valid && dispatcher->active_streams == 0U &&
                dispatcher->session.current_headers == NULL &&
                !sl_http2_session_want_write(&dispatcher->session))
            {
                status = sl_arena_reset_to(dispatcher->arena, stream->body_mark);
                if (!sl_status_is_ok(status)) {
                    return status;
                }
                stream->body_mark_valid = false;
                stream->body = (SlByteBuilder){0};
            }
            stream->body_mark = sl_arena_mark(dispatcher->arena);
            stream->body_mark_valid = true;
            status = sl_byte_builder_init_arena(&stream->body, dispatcher->arena, initial_capacity,
                                                dispatcher->config.max_body_bytes);
            if (!sl_status_is_ok(status)) {
                stream->body_mark_valid = false;
                return status;
            }
            stream->stream_id = stream_id;
            stream->active = true;
            stream->headers_seen = false;
            stream->complete_pending = false;
            stream->headers = (SlHttp2HeaderList){0};
            dispatcher->active_streams += 1U;
            *out_stream = stream;
            return sl_status_ok();
        }
    }

    return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
}

static void sl_http2_dispatch_close_stream(SlHttp2ServerDispatcher* dispatcher,
                                           SlHttp2DispatchStream* stream)
{
    if (dispatcher == NULL || stream == NULL || !stream->active) {
        return;
    }
    stream->active = false;
    stream->headers_seen = false;
    stream->complete_pending = false;
    stream->stream_id = 0;
    stream->headers = (SlHttp2HeaderList){0};
    stream->body = (SlByteBuilder){0};
    if (dispatcher->active_streams != 0U) {
        dispatcher->active_streams -= 1U;
    }
}

static bool sl_http2_dispatch_can_clear_session_events(const SlHttp2ServerDispatcher* dispatcher)
{
    return dispatcher != NULL && dispatcher->active_streams == 0U &&
           dispatcher->session.current_headers == NULL &&
           !sl_http2_session_want_write(&dispatcher->session);
}

static void sl_http2_dispatch_forget_stream_body_marks(SlHttp2ServerDispatcher* dispatcher)
{
    if (dispatcher == NULL || dispatcher->streams == NULL) {
        return;
    }
    for (size_t index = 0U; index < dispatcher->config.max_streams; index += 1U) {
        dispatcher->streams[index].body = (SlByteBuilder){0};
        dispatcher->streams[index].body_mark = (SlArenaMark){0};
        dispatcher->streams[index].body_mark_valid = false;
    }
}

static SlStatus sl_http2_dispatch_reset_stream(SlHttp2ServerDispatcher* dispatcher,
                                               int32_t stream_id, uint32_t error_code)
{
    SlStatus status;

    if (dispatcher == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_http2_session_submit_rst_stream(&dispatcher->session, stream_id, error_code);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    sl_http2_dispatch_close_stream(dispatcher,
                                   sl_http2_dispatch_find_stream(dispatcher, stream_id));
    return sl_status_ok();
}

static SlStatus sl_http2_dispatch_submit_response_for_request(SlHttp2ServerDispatcher* dispatcher,
                                                              int32_t stream_id, SlArena* arena,
                                                              SlHttpRequestLifecycle* request)
{
    SlStatus status;
    SlHttpResponse response = {0};
    SlHttp2HeaderList response_headers = {0};
    SlBytes response_body = {0};
    SlDiag dispatch_diag = {0};
    uint16_t safe_status = 500U;
    bool suppress_body = false;

    if (dispatcher == NULL || arena == NULL || request == NULL || stream_id <= 0) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_http_request_begin_dispatch(request, &dispatcher->last_diag);
    if (!sl_status_is_ok(status)) {
        (void)sl_http2_dispatch_reset_stream(dispatcher, stream_id, SL_HTTP2_ERROR_REFUSED_STREAM);
        return sl_status_ok();
    }

    status = dispatcher->config.dispatch(dispatcher->connection, arena, request, &response,
                                         &dispatch_diag, dispatcher->config.dispatch_user);
    if (!sl_status_is_ok(status)) {
        safe_status = sl_http2_dispatch_status_for_failure(status, &dispatch_diag);
        dispatcher->last_diag = dispatch_diag;
        response = sl_http_response_text(
            safe_status, sl_http2_dispatch_body_for_failure(safe_status, &dispatch_diag));
    }

    status = sl_http_request_begin_write(request, &dispatcher->last_diag);
    if (!sl_status_is_ok(status)) {
        (void)sl_http_request_fail(request, NULL);
        (void)sl_http2_dispatch_reset_stream(dispatcher, stream_id, SL_HTTP2_ERROR_INTERNAL_ERROR);
        return sl_status_ok();
    }

    suppress_body = request->head.method == SL_HTTP_METHOD_HEAD;
    status = sl_http2_response_to_headers(arena, &response, suppress_body,
                                          dispatcher->config.max_response_body_bytes,
                                          &response_headers, &response_body);
    if (!sl_status_is_ok(status)) {
        (void)sl_http_request_fail(request, NULL);
        (void)sl_http2_dispatch_reset_stream(dispatcher, stream_id, SL_HTTP2_ERROR_INTERNAL_ERROR);
        return sl_status_ok();
    }

    status = sl_http2_session_submit_response(&dispatcher->session, stream_id, &response_headers,
                                              response_body);
    if (!sl_status_is_ok(status)) {
        (void)sl_http_request_fail(request, NULL);
        return status;
    }

    status = sl_http_request_complete(request, &dispatcher->last_diag);
    return status;
}

static SlStatus sl_http2_dispatch_complete_stream(SlHttp2ServerDispatcher* dispatcher,
                                                  SlHttp2DispatchStream* stream)
{
    SlStatus status;
    SlHttpRequestLifecycle request = {0};
    SlBytes request_body = {0};

    if (dispatcher == NULL || stream == NULL || !stream->active || !stream->headers_seen) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    request_body = sl_byte_builder_view(&stream->body);
    status =
        sl_http2_request_from_headers(dispatcher->arena, dispatcher->connection, &stream->headers,
                                      request_body, &request, &dispatcher->last_diag);
    if (!sl_status_is_ok(status)) {
        (void)sl_http2_dispatch_reset_stream(dispatcher, stream->stream_id,
                                             SL_HTTP2_ERROR_PROTOCOL_ERROR);
        return sl_status_ok();
    }

    status = sl_http2_dispatch_submit_response_for_request(dispatcher, stream->stream_id,
                                                           dispatcher->arena, &request);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    sl_http2_dispatch_close_stream(dispatcher, stream);
    return sl_status_ok();
}

static SlStatus sl_http2_dispatch_mark_stream_complete(SlHttp2ServerDispatcher* dispatcher,
                                                       SlHttp2DispatchStream* stream)
{
    if (dispatcher == NULL || stream == NULL || !stream->active || !stream->headers_seen) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    stream->complete_pending = true;
    return sl_status_ok();
}

static SlStatus sl_http2_dispatch_handle_request_headers(SlHttp2ServerDispatcher* dispatcher,
                                                         const SlHttp2Event* event)
{
    SlStatus status;
    SlHttp2DispatchStream* stream = NULL;

    if (dispatcher == NULL || event == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_http2_dispatch_claim_stream(dispatcher, event->stream_id, &stream);
    if (!sl_status_is_ok(status) || stream == NULL) {
        return sl_http2_dispatch_reset_stream(dispatcher, event->stream_id,
                                              SL_HTTP2_ERROR_REFUSED_STREAM);
    }
    stream->headers = event->headers;
    stream->headers_seen = true;
    if (event->end_stream) {
        return sl_http2_dispatch_mark_stream_complete(dispatcher, stream);
    }
    return sl_status_ok();
}

static SlStatus sl_http2_dispatch_handle_data(SlHttp2ServerDispatcher* dispatcher,
                                              const SlHttp2Event* event)
{
    SlStatus status;
    SlHttp2DispatchStream* stream = NULL;

    if (dispatcher == NULL || event == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    stream = sl_http2_dispatch_find_stream(dispatcher, event->stream_id);
    if (stream == NULL) {
        return sl_http2_dispatch_reset_stream(dispatcher, event->stream_id,
                                              SL_HTTP2_ERROR_STREAM_CLOSED);
    }
    if (stream->complete_pending) {
        return sl_http2_dispatch_reset_stream(dispatcher, event->stream_id,
                                              SL_HTTP2_ERROR_STREAM_CLOSED);
    }

    status = sl_byte_builder_append_bytes(&stream->body, event->data);
    if (!sl_status_is_ok(status)) {
        return sl_http2_dispatch_reset_stream(dispatcher, event->stream_id,
                                              SL_HTTP2_ERROR_REFUSED_STREAM);
    }
    if (event->end_stream) {
        return sl_http2_dispatch_mark_stream_complete(dispatcher, stream);
    }
    return sl_status_ok();
}

static SlStatus sl_http2_dispatch_handle_stream_end(SlHttp2ServerDispatcher* dispatcher,
                                                    const SlHttp2Event* event)
{
    SlHttp2DispatchStream* stream = NULL;

    if (dispatcher == NULL || event == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    stream = sl_http2_dispatch_find_stream(dispatcher, event->stream_id);
    if (stream == NULL) {
        return sl_status_ok();
    }
    return sl_http2_dispatch_mark_stream_complete(dispatcher, stream);
}

static SlStatus sl_http2_dispatch_handle_trailers(SlHttp2ServerDispatcher* dispatcher,
                                                  const SlHttp2Event* event)
{
    SlHttp2DispatchStream* stream = NULL;

    if (dispatcher == NULL || event == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    stream = sl_http2_dispatch_find_stream(dispatcher, event->stream_id);
    if (stream == NULL) {
        return sl_http2_dispatch_reset_stream(dispatcher, event->stream_id,
                                              SL_HTTP2_ERROR_STREAM_CLOSED);
    }
    if (!stream->headers_seen || !event->end_stream) {
        return sl_http2_dispatch_reset_stream(dispatcher, event->stream_id,
                                              SL_HTTP2_ERROR_PROTOCOL_ERROR);
    }
    for (size_t index = 0U; index < event->headers.count; index += 1U) {
        const SlHttp2HeaderField* header = &event->headers.fields[index];
        if (header->name.length != 0U && header->name.ptr[0] == ':') {
            return sl_http2_dispatch_reset_stream(dispatcher, event->stream_id,
                                                  SL_HTTP2_ERROR_PROTOCOL_ERROR);
        }
    }
    return sl_http2_dispatch_mark_stream_complete(dispatcher, stream);
}

static SlStatus sl_http2_dispatch_flush_completed_streams(SlHttp2ServerDispatcher* dispatcher)
{
    if (dispatcher == NULL || dispatcher->streams == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    for (size_t index = 0U; index < dispatcher->config.max_streams; index += 1U) {
        SlHttp2DispatchStream* stream = &dispatcher->streams[index];
        if (stream->active && stream->complete_pending) {
            SlStatus status = sl_http2_dispatch_complete_stream(dispatcher, stream);
            if (!sl_status_is_ok(status)) {
                return status;
            }
        }
    }
    return sl_status_ok();
}

static SlStatus sl_http2_dispatch_handle_event(SlHttp2ServerDispatcher* dispatcher,
                                               const SlHttp2Event* event)
{
    if (dispatcher == NULL || event == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    switch (event->type) {
    case SL_HTTP2_EVENT_REQUEST_HEADERS:
        return sl_http2_dispatch_handle_request_headers(dispatcher, event);
    case SL_HTTP2_EVENT_DATA:
        return sl_http2_dispatch_handle_data(dispatcher, event);
    case SL_HTTP2_EVENT_STREAM_END:
        return sl_http2_dispatch_handle_stream_end(dispatcher, event);
    case SL_HTTP2_EVENT_RST_STREAM:
    case SL_HTTP2_EVENT_STREAM_CLOSE:
        sl_http2_dispatch_close_stream(dispatcher,
                                       sl_http2_dispatch_find_stream(dispatcher, event->stream_id));
        return sl_status_ok();
    case SL_HTTP2_EVENT_HEADERS:
        return sl_http2_dispatch_handle_trailers(dispatcher, event);
    case SL_HTTP2_EVENT_INVALID_FRAME:
        sl_http2_dispatch_close_stream(dispatcher,
                                       sl_http2_dispatch_find_stream(dispatcher, event->stream_id));
        return sl_status_ok();
    case SL_HTTP2_EVENT_SETTINGS:
        return sl_status_ok();
    case SL_HTTP2_EVENT_GOAWAY:
        dispatcher->peer_goaway_received = true;
        return sl_status_ok();
    case SL_HTTP2_EVENT_RESPONSE_HEADERS:
    case SL_HTTP2_EVENT_NONE:
    default:
        return sl_status_ok();
    }
}

SlStatus sl_http2_server_dispatcher_init(SlHttp2ServerDispatcher* dispatcher, SlArena* arena,
                                         SlHttpConnection* connection,
                                         const SlHttp2DispatchConfig* config)
{
    SlStatus status;
    SlHttp2DispatchConfig normalized = {0};
    void* stream_storage = NULL;
    size_t stream_storage_size = 0U;

    if (dispatcher == NULL || arena == NULL || connection == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *dispatcher = (SlHttp2ServerDispatcher){0};

    status = sl_http2_dispatch_normalize_config(config, &normalized);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_checked_array_size(normalized.max_streams, sizeof(SlHttp2DispatchStream),
                                   &stream_storage_size);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(arena, stream_storage_size, _Alignof(SlHttp2DispatchStream),
                            &stream_storage);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    dispatcher->arena = arena;
    dispatcher->connection = connection;
    dispatcher->config = normalized;
    dispatcher->streams = (SlHttp2DispatchStream*)stream_storage;
    for (size_t index = 0U; index < normalized.max_streams; index += 1U) {
        dispatcher->streams[index] = (SlHttp2DispatchStream){0};
    }

    status = sl_http2_session_init(&dispatcher->session, arena, &normalized.session);
    if (!sl_status_is_ok(status)) {
        *dispatcher = (SlHttp2ServerDispatcher){0};
        return status;
    }

    sl_http_connection_set_multiplexing(connection, true);
    dispatcher->initialized = true;
    return sl_status_ok();
}

void sl_http2_server_dispatcher_dispose(SlHttp2ServerDispatcher* dispatcher)
{
    if (dispatcher == NULL) {
        return;
    }
    if (dispatcher->streams != NULL) {
        for (size_t index = 0U; index < dispatcher->config.max_streams; index += 1U) {
            dispatcher->streams[index] = (SlHttp2DispatchStream){0};
        }
    }
    sl_http2_session_dispose(&dispatcher->session);
    *dispatcher = (SlHttp2ServerDispatcher){0};
}

SlStatus sl_http2_server_dispatcher_upgrade_h2c(SlHttp2ServerDispatcher* dispatcher,
                                                SlBytes settings_payload,
                                                SlHttpRequestLifecycle* request)
{
    SlStatus status;

    if (dispatcher == NULL || !dispatcher->initialized || request == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (sl_http2_dispatch_can_clear_session_events(dispatcher)) {
        sl_http2_session_clear_events(&dispatcher->session);
        sl_http2_dispatch_forget_stream_body_marks(dispatcher);
    }

    status = sl_http2_session_upgrade_h2c(&dispatcher->session, settings_payload,
                                          request->head.method == SL_HTTP_METHOD_HEAD);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http2_server_dispatcher_process_pending(dispatcher);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_http2_dispatch_submit_response_for_request(dispatcher, 1, request->arena, request);
}

SlStatus sl_http2_server_dispatcher_receive(SlHttp2ServerDispatcher* dispatcher, SlBytes bytes,
                                            size_t* out_consumed)
{
    SlStatus status;

    if (dispatcher == NULL || !dispatcher->initialized) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (sl_http2_dispatch_can_clear_session_events(dispatcher)) {
        sl_http2_session_clear_events(&dispatcher->session);
        sl_http2_dispatch_forget_stream_body_marks(dispatcher);
    }

    status = sl_http2_session_receive(&dispatcher->session, bytes, out_consumed);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_http2_server_dispatcher_process_pending(dispatcher);
}

SlStatus sl_http2_server_dispatcher_process_pending(SlHttp2ServerDispatcher* dispatcher)
{
    SlHttp2EventList events = {0};
    SlStatus status;

    if (dispatcher == NULL || !dispatcher->initialized) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    events = sl_http2_session_events(&dispatcher->session);
    for (size_t index = 0U; index < events.count; index += 1U) {
        status = sl_http2_dispatch_handle_event(dispatcher, &events.events[index]);
        if (!sl_status_is_ok(status)) {
            dispatcher->session.event_count = 0U;
            return status;
        }
    }
    dispatcher->session.event_count = 0U;
    return sl_http2_dispatch_flush_completed_streams(dispatcher);
}

SlStatus sl_http2_server_dispatcher_drain_output(SlHttp2ServerDispatcher* dispatcher,
                                                 SlBytes* out_bytes)
{
    if (dispatcher == NULL || !dispatcher->initialized) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_http2_session_drain_output(&dispatcher->session, out_bytes);
}

size_t sl_http2_server_dispatcher_active_streams(const SlHttp2ServerDispatcher* dispatcher)
{
    return dispatcher == NULL ? 0U : dispatcher->active_streams;
}

bool sl_http2_server_dispatcher_peer_goaway_received(const SlHttp2ServerDispatcher* dispatcher)
{
    return dispatcher != NULL && dispatcher->peer_goaway_received;
}

bool sl_http2_server_dispatcher_close_without_goaway(const SlHttp2ServerDispatcher* dispatcher)
{
    return dispatcher != NULL && sl_http2_session_close_without_goaway(&dispatcher->session);
}

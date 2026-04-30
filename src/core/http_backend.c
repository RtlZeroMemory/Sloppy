/*
 * src/core/http_backend.c
 *
 * ENGINE-13.A/B/C core HTTP backend foundation. This file owns only Slop state,
 * admission, lifecycle, parser-limit, and cancellation/deadline bookkeeping. Concrete
 * sockets/listeners remain behind the platform boundary.
 */
#include "sloppy/http_backend.h"

#include "sloppy/checked_math.h"

static SlStr sl_http_backend_literal(const char* ptr, size_t length)
{
    return sl_str_from_parts(ptr, length);
}

static bool sl_http_backend_str_valid(SlStr str)
{
    return str.length == 0U || str.ptr != NULL;
}

static SlStatus sl_http_backend_diag(SlDiag* out_diag, SlDiagCode code, SlStr message, SlStr hint,
                                     SlStatusCode status_code)
{
    if (out_diag == NULL) {
        return sl_status_from_code(status_code);
    }

    *out_diag = (SlDiag){0};
    out_diag->severity = SL_DIAG_SEVERITY_ERROR;
    out_diag->code = code;
    out_diag->message = message;
    if (!sl_str_is_empty(hint)) {
        out_diag->hints[0] = hint;
        out_diag->hint_count = 1U;
    }

    return sl_status_from_code(status_code);
}

static SlStatus sl_http_backend_invalid_state(SlDiag* out_diag)
{
    return sl_http_backend_diag(
        out_diag, SL_DIAG_APP_LIFECYCLE,
        sl_http_backend_literal("HTTP backend lifecycle state is invalid",
                                sizeof("HTTP backend lifecycle state is invalid") - 1U),
        sl_http_backend_literal("call HTTP backend init/start/stop/dispose in order",
                                sizeof("call HTTP backend init/start/stop/dispose in order") - 1U),
        SL_STATUS_INVALID_STATE);
}

static SlStatus sl_http_backend_overload(SlDiag* out_diag)
{
    return sl_http_backend_diag(
        out_diag, SL_DIAG_HTTP_OVERLOAD,
        sl_http_backend_literal("HTTP backend admission limit was reached",
                                sizeof("HTTP backend admission limit was reached") - 1U),
        sl_http_backend_literal("the backend rejects overload instead of queuing without bounds",
                                sizeof("the backend rejects overload instead of queuing without "
                                       "bounds") -
                                    1U),
        SL_STATUS_CAPACITY_EXCEEDED);
}

static SlStatus sl_http_backend_connection_closed(SlDiag* out_diag)
{
    return sl_http_backend_diag(
        out_diag, SL_DIAG_HTTP_CONNECTION_CLOSED,
        sl_http_backend_literal("HTTP connection is closed",
                                sizeof("HTTP connection is closed") - 1U),
        sl_http_backend_literal("do not begin new request work on a closed connection",
                                sizeof("do not begin new request work on a closed connection") -
                                    1U),
        SL_STATUS_INVALID_STATE);
}

static SlStatus sl_http_backend_keep_alive_unsupported(SlDiag* out_diag)
{
    return sl_http_backend_diag(
        out_diag, SL_DIAG_HTTP_KEEP_ALIVE_UNSUPPORTED,
        sl_http_backend_literal("HTTP keep-alive is not enabled",
                                sizeof("HTTP keep-alive is not enabled") - 1U),
        sl_http_backend_literal("the current dev runtime closes each HTTP/1.1 connection",
                                sizeof("the current dev runtime closes each HTTP/1.1 connection") -
                                    1U),
        SL_STATUS_UNSUPPORTED);
}

static SlStatus sl_http_backend_timeout_diag(SlDiag* out_diag)
{
    return sl_http_backend_diag(
        out_diag, SL_DIAG_HTTP_REQUEST_TIMEOUT,
        sl_http_backend_literal("HTTP request deadline was exceeded",
                                sizeof("HTTP request deadline was exceeded") - 1U),
        sl_http_backend_literal("request cleanup still runs after timeout",
                                sizeof("request cleanup still runs after timeout") - 1U),
        SL_STATUS_DEADLINE_EXCEEDED);
}

static SlStatus sl_http_backend_shutdown_diag(SlDiag* out_diag, SlStatusCode status_code)
{
    return sl_http_backend_diag(
        out_diag, SL_DIAG_HTTP_SHUTDOWN,
        sl_http_backend_literal("HTTP backend shutdown is in progress",
                                sizeof("HTTP backend shutdown is in progress") - 1U),
        sl_http_backend_literal("new request work is rejected and active work must drain or cancel",
                                sizeof("new request work is rejected and active work must drain "
                                       "or cancel") -
                                    1U),
        status_code);
}

static SlStatus sl_http_backend_cancelled_diag(SlDiag* out_diag)
{
    return sl_http_backend_diag(
        out_diag, SL_DIAG_ENGINE_CANCELLED,
        sl_http_backend_literal("HTTP request was cancelled",
                                sizeof("HTTP request was cancelled") - 1U),
        sl_http_backend_literal("request cleanup still runs after cancellation",
                                sizeof("request cleanup still runs after cancellation") - 1U),
        SL_STATUS_CANCELLED);
}

static SlStatus sl_http_backend_body_limit_diag(SlDiag* out_diag)
{
    return sl_http_backend_diag(
        out_diag, SL_DIAG_HTTP_BODY_LIMIT,
        sl_http_backend_literal("HTTP request body is too large",
                                sizeof("HTTP request body is too large") - 1U),
        sl_http_backend_literal("request bodies are copied only up to the configured limit",
                                sizeof("request bodies are copied only up to the configured "
                                       "limit") -
                                    1U),
        SL_STATUS_CAPACITY_EXCEEDED);
}

static SlStatus sl_http_backend_unsupported_media_diag(SlDiag* out_diag)
{
    return sl_http_backend_diag(
        out_diag, SL_DIAG_HTTP_UNSUPPORTED_MEDIA_TYPE,
        sl_http_backend_literal("HTTP request body content type is not supported",
                                sizeof("HTTP request body content type is not supported") - 1U),
        sl_http_backend_literal("use application/json or text/plain for bounded request bodies",
                                sizeof("use application/json or text/plain for bounded request "
                                       "bodies") -
                                    1U),
        SL_STATUS_UNSUPPORTED);
}

static SlStatus sl_http_backend_invalid_body_diag(SlDiag* out_diag)
{
    return sl_http_backend_diag(
        out_diag, SL_DIAG_INVALID_HTTP_REQUEST,
        sl_http_backend_literal("HTTP request body length is invalid",
                                sizeof("HTTP request body length is invalid") - 1U),
        sl_http_backend_literal("the body reader must receive exactly the declared Content-Length",
                                sizeof("the body reader must receive exactly the declared "
                                       "Content-Length") -
                                    1U),
        SL_STATUS_INVALID_ARGUMENT);
}

static void sl_http_backend_options_init(SlHttpBackendOptions* out,
                                         const SlHttpBackendOptions* options)
{
    *out = (SlHttpBackendOptions){0};
    out->max_connections = SL_HTTP_BACKEND_DEFAULT_MAX_CONNECTIONS;
    out->max_active_requests = SL_HTTP_BACKEND_DEFAULT_MAX_ACTIVE_REQUESTS;
    out->parse.max_headers = SL_HTTP_DEFAULT_MAX_HEADERS;
    out->parse.max_target_length = SL_HTTP_DEFAULT_MAX_TARGET_LENGTH;
    out->parse.max_header_name_length = SL_HTTP_DEFAULT_MAX_HEADER_NAME_LENGTH;
    out->parse.max_header_value_length = SL_HTTP_DEFAULT_MAX_HEADER_VALUE_LENGTH;
    out->parse.max_total_header_bytes = SL_HTTP_DEFAULT_MAX_TOTAL_HEADER_BYTES;
    out->parse.max_body_length = SL_HTTP_DEFAULT_MAX_BODY_LENGTH;

    if (options == NULL) {
        return;
    }

    out->max_connections = options->max_connections == 0U ? SL_HTTP_BACKEND_DEFAULT_MAX_CONNECTIONS
                                                          : options->max_connections;
    out->max_active_requests = options->max_active_requests == 0U
                                   ? SL_HTTP_BACKEND_DEFAULT_MAX_ACTIVE_REQUESTS
                                   : options->max_active_requests;
    out->parse.max_headers = options->parse.max_headers;
    out->parse.max_target_length = options->parse.max_target_length == 0U
                                       ? SL_HTTP_DEFAULT_MAX_TARGET_LENGTH
                                       : options->parse.max_target_length;
    out->parse.max_header_name_length = options->parse.max_header_name_length == 0U
                                            ? SL_HTTP_DEFAULT_MAX_HEADER_NAME_LENGTH
                                            : options->parse.max_header_name_length;
    out->parse.max_header_value_length = options->parse.max_header_value_length == 0U
                                             ? SL_HTTP_DEFAULT_MAX_HEADER_VALUE_LENGTH
                                             : options->parse.max_header_value_length;
    out->parse.max_total_header_bytes = options->parse.max_total_header_bytes == 0U
                                            ? SL_HTTP_DEFAULT_MAX_TOTAL_HEADER_BYTES
                                            : options->parse.max_total_header_bytes;
    out->parse.max_body_length = options->parse.max_body_length == 0U
                                     ? SL_HTTP_DEFAULT_MAX_BODY_LENGTH
                                     : options->parse.max_body_length;
    out->read_timeout_ms = options->read_timeout_ms;
    out->header_timeout_ms = options->header_timeout_ms;
    out->request_timeout_ms = options->request_timeout_ms;
    out->keep_alive_enabled = options->keep_alive_enabled;
}

static bool sl_http_connection_can_begin_request(const SlHttpConnection* connection)
{
    return connection != NULL && connection->backend != NULL && connection->slot_admitted &&
           connection->backend->state == SL_HTTP_BACKEND_STATE_STARTED &&
           (connection->state == SL_HTTP_CONNECTION_STATE_ACCEPTED ||
            connection->state == SL_HTTP_CONNECTION_STATE_OPEN);
}

static bool sl_http_request_terminal(SlHttpRequestState state)
{
    return state == SL_HTTP_REQUEST_STATE_COMPLETED || state == SL_HTTP_REQUEST_STATE_CANCELLED ||
           state == SL_HTTP_REQUEST_STATE_TIMED_OUT || state == SL_HTTP_REQUEST_STATE_FAILED ||
           state == SL_HTTP_REQUEST_STATE_CLOSED;
}

static void sl_http_backend_maybe_finish_stop(SlHttpBackend* backend)
{
    if (backend != NULL && backend->state == SL_HTTP_BACKEND_STATE_STOPPING &&
        backend->active_connections == 0U && backend->active_requests == 0U)
    {
        backend->state = SL_HTTP_BACKEND_STATE_STOPPED;
        backend->listener.state = SL_HTTP_LISTENER_STATE_STOPPED;
    }
}

static void sl_http_request_release_admission(SlHttpRequestLifecycle* request)
{
    SlHttpBackend* backend = NULL;

    if (request == NULL || !request->admitted || request->connection == NULL) {
        return;
    }

    backend = request->connection->backend;
    if (backend != NULL && backend->active_requests > 0U) {
        backend->active_requests -= 1U;
    }
    request->admitted = false;
    sl_http_backend_maybe_finish_stop(backend);
}

static void sl_http_request_terminal_cancel(SlHttpRequestLifecycle* request,
                                            SlHttpRequestState state)
{
    if (request == NULL) {
        return;
    }
    request->state = state;
    if (request->connection != NULL) {
        request->connection->state = SL_HTTP_CONNECTION_STATE_CLOSING;
    }
    sl_http_request_release_admission(request);
}

static int sl_http_backend_ascii_lower(int ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A' + 'a';
    }
    return ch;
}

static bool sl_http_backend_str_iequal(SlStr left, SlStr right)
{
    size_t index = 0U;

    if ((left.ptr == NULL && left.length != 0U) || (right.ptr == NULL && right.length != 0U) ||
        left.length != right.length)
    {
        return false;
    }

    for (index = 0U; index < left.length; index += 1U) {
        if (sl_http_backend_ascii_lower((unsigned char)left.ptr[index]) !=
            sl_http_backend_ascii_lower((unsigned char)right.ptr[index]))
        {
            return false;
        }
    }

    return true;
}

static bool sl_http_backend_str_istarts_with(SlStr str, SlStr prefix)
{
    size_t index = 0U;

    if ((str.ptr == NULL && str.length != 0U) || (prefix.ptr == NULL && prefix.length != 0U) ||
        prefix.length > str.length)
    {
        return false;
    }

    for (index = 0U; index < prefix.length; index += 1U) {
        if (sl_http_backend_ascii_lower((unsigned char)str.ptr[index]) !=
            sl_http_backend_ascii_lower((unsigned char)prefix.ptr[index]))
        {
            return false;
        }
    }

    return true;
}

static bool sl_http_backend_str_iends_with(SlStr str, SlStr suffix)
{
    size_t offset = 0U;
    size_t index = 0U;

    if ((str.ptr == NULL && str.length != 0U) || (suffix.ptr == NULL && suffix.length != 0U) ||
        suffix.length > str.length)
    {
        return false;
    }

    offset = str.length - suffix.length;
    for (index = 0U; index < suffix.length; index += 1U) {
        if (sl_http_backend_ascii_lower((unsigned char)str.ptr[offset + index]) !=
            sl_http_backend_ascii_lower((unsigned char)suffix.ptr[index]))
        {
            return false;
        }
    }

    return true;
}

static SlStr sl_http_backend_trim_ascii_space(SlStr value)
{
    size_t begin = 0U;
    size_t end = value.length;

    if (value.ptr == NULL) {
        return sl_str_empty();
    }

    while (begin < end && (value.ptr[begin] == ' ' || value.ptr[begin] == '\t')) {
        begin += 1U;
    }
    while (end > begin && (value.ptr[end - 1U] == ' ' || value.ptr[end - 1U] == '\t')) {
        end -= 1U;
    }

    return sl_str_from_parts(value.ptr + begin, end - begin);
}

static SlStr sl_http_backend_media_type(SlStr content_type)
{
    size_t index = 0U;

    if (content_type.ptr == NULL) {
        return sl_str_empty();
    }

    while (index < content_type.length && content_type.ptr[index] != ';') {
        index += 1U;
    }

    return sl_http_backend_trim_ascii_space(sl_str_from_parts(content_type.ptr, index));
}

static bool sl_http_backend_media_type_json(SlStr media_type)
{
    return sl_http_backend_str_iequal(media_type, sl_str_from_cstr("application/json")) ||
           (sl_http_backend_str_istarts_with(media_type, sl_str_from_cstr("application/")) &&
            sl_http_backend_str_iends_with(media_type, sl_str_from_cstr("+json")));
}

static SlStatus sl_http_body_reader_classify(SlStr content_type, size_t content_length,
                                             SlHttpRequestBodyKind* out_kind, SlDiag* out_diag)
{
    SlStr media_type = {0};

    if (out_kind == NULL || !sl_http_backend_str_valid(content_type)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_kind = SL_HTTP_REQUEST_BODY_NONE;
    if (content_length == 0U) {
        return sl_status_ok();
    }
    if (sl_str_is_empty(sl_http_backend_trim_ascii_space(content_type))) {
        return sl_http_backend_unsupported_media_diag(out_diag);
    }

    media_type = sl_http_backend_media_type(content_type);
    if (sl_http_backend_media_type_json(media_type)) {
        *out_kind = SL_HTTP_REQUEST_BODY_JSON;
        return sl_status_ok();
    }
    if (sl_http_backend_str_iequal(media_type, sl_str_from_cstr("text/plain"))) {
        *out_kind = SL_HTTP_REQUEST_BODY_TEXT;
        return sl_status_ok();
    }

    return sl_http_backend_unsupported_media_diag(out_diag);
}

static bool sl_http_body_reader_usable(const SlHttpBodyReader* reader)
{
    return reader != NULL && reader->request != NULL && reader->arena != NULL &&
           (reader->state == SL_HTTP_BODY_READER_STATE_READY ||
            reader->state == SL_HTTP_BODY_READER_STATE_READING);
}

static SlStatus sl_http_body_reader_fail(SlHttpBodyReader* reader, SlHttpBodyReaderState state,
                                         SlStatus diag_status)
{
    SlStatus reset_status;

    if (reader == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    reset_status = sl_arena_reset_to(reader->arena, reader->mark);
    if (!sl_status_is_ok(reset_status)) {
        return reset_status;
    }
    reader->state = state;
    if (reader->request != NULL) {
        reader->request->head.body = sl_bytes_empty();
        if (state == SL_HTTP_BODY_READER_STATE_CANCELLED) {
            sl_http_request_terminal_cancel(reader->request, SL_HTTP_REQUEST_STATE_CANCELLED);
        }
        else if (state == SL_HTTP_BODY_READER_STATE_TIMED_OUT) {
            sl_http_request_terminal_cancel(reader->request, SL_HTTP_REQUEST_STATE_TIMED_OUT);
        }
        else {
            reader->request->state = SL_HTTP_REQUEST_STATE_FAILED;
            if (reader->request->connection != NULL) {
                reader->request->connection->state = SL_HTTP_CONNECTION_STATE_CLOSING;
            }
            sl_http_request_release_admission(reader->request);
        }
    }

    return diag_status;
}

static SlStatus sl_http_body_reader_check_terminal(SlHttpBodyReader* reader, SlDiag* out_diag)
{
    SlCancellationReason reason = SL_CANCELLATION_REASON_NONE;

    if (reader == NULL || reader->request == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (reader->request->connection != NULL && reader->request->connection->backend != NULL &&
        reader->request->connection->backend->state == SL_HTTP_BACKEND_STATE_STOPPING)
    {
        (void)sl_cancellation_token_cancel(&reader->request->cancellation,
                                           SL_CANCELLATION_REASON_SHUTDOWN,
                                           sl_str_from_cstr("HTTP backend shutdown"));
        return sl_http_body_reader_fail(
            reader, SL_HTTP_BODY_READER_STATE_CANCELLED,
            sl_http_backend_shutdown_diag(out_diag, SL_STATUS_CANCELLED));
    }

    if (!sl_cancellation_token_is_cancelled(&reader->request->cancellation)) {
        return sl_status_ok();
    }

    reason = sl_cancellation_token_reason(&reader->request->cancellation);
    if (reason == SL_CANCELLATION_REASON_DEADLINE_EXCEEDED) {
        return sl_http_body_reader_fail(reader, SL_HTTP_BODY_READER_STATE_TIMED_OUT,
                                        sl_http_backend_timeout_diag(out_diag));
    }
    if (reason == SL_CANCELLATION_REASON_SHUTDOWN) {
        return sl_http_body_reader_fail(
            reader, SL_HTTP_BODY_READER_STATE_CANCELLED,
            sl_http_backend_shutdown_diag(out_diag, SL_STATUS_CANCELLED));
    }

    return sl_http_body_reader_fail(reader, SL_HTTP_BODY_READER_STATE_CANCELLED,
                                    sl_http_backend_cancelled_diag(out_diag));
}

SlStatus sl_http_backend_init(SlHttpBackend* backend, const SlHttpBackendOptions* options,
                              SlDiag* out_diag)
{
    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (backend == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *backend = (SlHttpBackend){0};
    sl_http_backend_options_init(&backend->options, options);
    backend->state = SL_HTTP_BACKEND_STATE_INITIALIZED;
    backend->listener.state = SL_HTTP_LISTENER_STATE_UNBOUND;
    backend->next_connection_id = 1U;
    return sl_status_ok();
}

SlStatus sl_http_backend_start(SlHttpBackend* backend, SlHttpPlatformListener* platform,
                               SlDiag* out_diag)
{
    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (backend == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (backend->state != SL_HTTP_BACKEND_STATE_INITIALIZED &&
        backend->state != SL_HTTP_BACKEND_STATE_STOPPED)
    {
        return sl_http_backend_invalid_state(out_diag);
    }

    backend->listener.platform = platform;
    backend->listener.state = SL_HTTP_LISTENER_STATE_LISTENING;
    backend->state = SL_HTTP_BACKEND_STATE_STARTED;
    return sl_status_ok();
}

SlStatus sl_http_backend_stop(SlHttpBackend* backend, SlDiag* out_diag)
{
    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (backend == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (backend->state != SL_HTTP_BACKEND_STATE_STARTED &&
        backend->state != SL_HTTP_BACKEND_STATE_STOPPING)
    {
        return sl_http_backend_invalid_state(out_diag);
    }

    backend->state = SL_HTTP_BACKEND_STATE_STOPPING;
    backend->listener.state = SL_HTTP_LISTENER_STATE_STOPPING;
    sl_http_backend_maybe_finish_stop(backend);
    return sl_status_ok();
}

SlStatus sl_http_backend_dispose(SlHttpBackend* backend, SlDiag* out_diag)
{
    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (backend == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (backend->active_connections != 0U || backend->active_requests != 0U ||
        backend->state == SL_HTTP_BACKEND_STATE_STARTED ||
        backend->state == SL_HTTP_BACKEND_STATE_STOPPING)
    {
        return sl_http_backend_invalid_state(out_diag);
    }

    backend->listener.platform = NULL;
    backend->listener.state = SL_HTTP_LISTENER_STATE_STOPPED;
    backend->state = SL_HTTP_BACKEND_STATE_DISPOSED;
    return sl_status_ok();
}

SlStatus sl_http_backend_accept_connection(SlHttpBackend* backend, SlHttpConnection* out_connection,
                                           SlDiag* out_diag)
{
    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (out_connection == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_connection = (SlHttpConnection){0};
    if (backend == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (backend->state != SL_HTTP_BACKEND_STATE_STARTED) {
        return sl_http_backend_invalid_state(out_diag);
    }
    if (backend->active_connections >= backend->options.max_connections) {
        return sl_http_backend_overload(out_diag);
    }

    out_connection->backend = backend;
    out_connection->state = SL_HTTP_CONNECTION_STATE_ACCEPTED;
    out_connection->id = backend->next_connection_id;
    out_connection->slot_admitted = true;
    backend->next_connection_id += 1U;
    backend->active_connections += 1U;
    return sl_status_ok();
}

SlStatus sl_http_connection_close(SlHttpConnection* connection, SlDiag* out_diag)
{
    SlHttpBackend* backend = NULL;

    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (connection == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (connection->state == SL_HTTP_CONNECTION_STATE_CLOSED) {
        return sl_status_ok();
    }

    backend = connection->backend;
    connection->state = SL_HTTP_CONNECTION_STATE_CLOSED;
    if (backend != NULL && connection->slot_admitted && backend->active_connections > 0U) {
        backend->active_connections -= 1U;
    }
    connection->slot_admitted = false;
    sl_http_backend_maybe_finish_stop(backend);
    return sl_status_ok();
}

SlStatus sl_http_connection_fail(SlHttpConnection* connection, SlDiag* out_diag)
{
    SlHttpBackend* backend = NULL;

    if (connection == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    backend = connection->backend;
    connection->state = SL_HTTP_CONNECTION_STATE_ERROR;
    if (backend != NULL && connection->slot_admitted && backend->active_connections > 0U) {
        backend->active_connections -= 1U;
    }
    connection->slot_admitted = false;
    sl_http_backend_maybe_finish_stop(backend);
    return sl_status_ok();
}

SlStatus sl_http_request_begin(SlHttpConnection* connection, SlArena* arena,
                               SlHttpRequestLifecycle* out_request, SlDiag* out_diag)
{
    SlHttpBackend* backend = NULL;

    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (out_request == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_request = (SlHttpRequestLifecycle){0};
    if (arena == NULL || connection == NULL || connection->backend == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (connection->state == SL_HTTP_CONNECTION_STATE_CLOSING &&
        !connection->backend->options.keep_alive_enabled)
    {
        return sl_http_backend_keep_alive_unsupported(out_diag);
    }
    if (connection->backend->state == SL_HTTP_BACKEND_STATE_STOPPING) {
        return sl_http_backend_shutdown_diag(out_diag, SL_STATUS_CANCELLED);
    }
    if (!sl_http_connection_can_begin_request(connection)) {
        return sl_http_backend_connection_closed(out_diag);
    }

    backend = connection->backend;
    if (backend->active_requests >= backend->options.max_active_requests) {
        return sl_http_backend_overload(out_diag);
    }

    backend->active_requests += 1U;
    connection->request_count += 1U;
    connection->state = SL_HTTP_CONNECTION_STATE_READING_REQUEST;
    out_request->connection = connection;
    out_request->arena = arena;
    out_request->state = SL_HTTP_REQUEST_STATE_CREATED;
    out_request->admitted = true;
    sl_cancellation_token_init(&out_request->cancellation);
    return sl_status_ok();
}

SlStatus sl_http_request_parse_head(SlHttpRequestLifecycle* request, SlBytes bytes,
                                    SlDiag* out_diag)
{
    SlHttpBackend* backend = NULL;
    SlStatus status;

    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (request == NULL || request->connection == NULL || request->arena == NULL ||
        (bytes.ptr == NULL && bytes.length != 0U))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (request->state != SL_HTTP_REQUEST_STATE_CREATED) {
        return sl_http_backend_invalid_state(out_diag);
    }

    backend = request->connection->backend;
    request->connection->state = SL_HTTP_CONNECTION_STATE_READING_REQUEST;
    status = sl_http_parse_request_head(request->arena, bytes,
                                        backend == NULL ? NULL : &backend->options.parse,
                                        &request->head, out_diag);
    if (!sl_status_is_ok(status)) {
        request->state = SL_HTTP_REQUEST_STATE_FAILED;
        request->connection->state = SL_HTTP_CONNECTION_STATE_ERROR;
    }
    else {
        request->state = SL_HTTP_REQUEST_STATE_READING;
    }
    return status;
}

SlStatus sl_http_request_body_reader_begin(SlHttpRequestLifecycle* request, SlStr content_type,
                                           size_t content_length, SlHttpBodyReader* out_reader,
                                           SlDiag* out_diag)
{
    SlHttpBackend* backend = NULL;
    SlHttpRequestBodyKind body_kind = SL_HTTP_REQUEST_BODY_NONE;
    size_t max_body_bytes = SL_HTTP_DEFAULT_MAX_BODY_LENGTH;
    SlStatus status;

    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (out_reader == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_reader = (SlHttpBodyReader){0};
    if (request == NULL || request->arena == NULL || request->connection == NULL ||
        !sl_http_backend_str_valid(content_type))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (request->state != SL_HTTP_REQUEST_STATE_READING) {
        return sl_http_backend_invalid_state(out_diag);
    }

    backend = request->connection->backend;
    if (backend != NULL) {
        max_body_bytes = backend->options.parse.max_body_length == 0U
                             ? SL_HTTP_DEFAULT_MAX_BODY_LENGTH
                             : backend->options.parse.max_body_length;
    }

    out_reader->request = request;
    out_reader->arena = request->arena;
    out_reader->mark = sl_arena_mark(request->arena);
    out_reader->state = SL_HTTP_BODY_READER_STATE_READY;
    out_reader->max_body_bytes = max_body_bytes;
    out_reader->expected_body_bytes = content_length;

    status = sl_http_body_reader_check_terminal(out_reader, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (content_length > max_body_bytes) {
        return sl_http_body_reader_fail(out_reader, SL_HTTP_BODY_READER_STATE_FAILED,
                                        sl_http_backend_body_limit_diag(out_diag));
    }

    status = sl_http_body_reader_classify(content_type, content_length, &body_kind, out_diag);
    if (!sl_status_is_ok(status)) {
        return sl_http_body_reader_fail(out_reader, SL_HTTP_BODY_READER_STATE_FAILED, status);
    }

    status = sl_byte_builder_init_arena(&out_reader->builder, request->arena, 0U, max_body_bytes);
    if (!sl_status_is_ok(status)) {
        return sl_http_body_reader_fail(out_reader, SL_HTTP_BODY_READER_STATE_FAILED, status);
    }

    out_reader->body_kind = body_kind;
    return sl_status_ok();
}

SlStatus sl_http_request_body_reader_append(SlHttpBodyReader* reader, SlBytes chunk,
                                            SlDiag* out_diag)
{
    size_t next_length = 0U;
    SlStatus status;

    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (!sl_http_body_reader_usable(reader) || (chunk.ptr == NULL && chunk.length != 0U)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_http_body_reader_check_terminal(reader, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_checked_add_size(sl_byte_builder_view(&reader->builder).length, chunk.length,
                                 &next_length);
    if (!sl_status_is_ok(status)) {
        return sl_http_body_reader_fail(reader, SL_HTTP_BODY_READER_STATE_FAILED, status);
    }
    if (next_length > reader->max_body_bytes) {
        return sl_http_body_reader_fail(reader, SL_HTTP_BODY_READER_STATE_FAILED,
                                        sl_http_backend_body_limit_diag(out_diag));
    }
    if (next_length > reader->expected_body_bytes) {
        return sl_http_body_reader_fail(reader, SL_HTTP_BODY_READER_STATE_FAILED,
                                        sl_http_backend_invalid_body_diag(out_diag));
    }

    status = sl_byte_builder_append_bytes(&reader->builder, chunk);
    if (!sl_status_is_ok(status)) {
        return sl_http_body_reader_fail(reader, SL_HTTP_BODY_READER_STATE_FAILED, status);
    }
    reader->state = SL_HTTP_BODY_READER_STATE_READING;
    return sl_status_ok();
}

SlStatus sl_http_request_body_reader_finish(SlHttpBodyReader* reader, SlDiag* out_diag)
{
    SlBytes body = {0};
    SlStatus status;

    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (!sl_http_body_reader_usable(reader)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_http_body_reader_check_terminal(reader, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    body = sl_byte_builder_view(&reader->builder);
    if (body.length != reader->expected_body_bytes) {
        return sl_http_body_reader_fail(reader, SL_HTTP_BODY_READER_STATE_FAILED,
                                        sl_http_backend_invalid_body_diag(out_diag));
    }

    reader->request->head.body = body;
    reader->state = SL_HTTP_BODY_READER_STATE_COMPLETED;
    return sl_status_ok();
}

SlStatus sl_http_request_body_reader_close(SlHttpBodyReader* reader, SlDiag* out_diag)
{
    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (reader == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (reader->state == SL_HTTP_BODY_READER_STATE_CLOSED) {
        return sl_status_ok();
    }

    if (reader->state != SL_HTTP_BODY_READER_STATE_COMPLETED &&
        reader->state != SL_HTTP_BODY_READER_STATE_CANCELLED &&
        reader->state != SL_HTTP_BODY_READER_STATE_TIMED_OUT &&
        reader->state != SL_HTTP_BODY_READER_STATE_FAILED && reader->arena != NULL)
    {
        SlStatus status = sl_arena_reset_to(reader->arena, reader->mark);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    reader->state = SL_HTTP_BODY_READER_STATE_CLOSED;
    return sl_status_ok();
}

SlStatus sl_http_request_begin_dispatch(SlHttpRequestLifecycle* request, SlDiag* out_diag)
{
    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (request == NULL || request->connection == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (request->state != SL_HTTP_REQUEST_STATE_READING) {
        return sl_http_backend_invalid_state(out_diag);
    }
    if (sl_cancellation_token_is_cancelled(&request->cancellation)) {
        SlCancellationReason reason = sl_cancellation_token_reason(&request->cancellation);
        if (reason == SL_CANCELLATION_REASON_DEADLINE_EXCEEDED) {
            sl_http_request_terminal_cancel(request, SL_HTTP_REQUEST_STATE_TIMED_OUT);
            return sl_http_backend_timeout_diag(out_diag);
        }
        if (reason == SL_CANCELLATION_REASON_SHUTDOWN) {
            sl_http_request_terminal_cancel(request, SL_HTTP_REQUEST_STATE_CANCELLED);
            return sl_http_backend_shutdown_diag(out_diag, SL_STATUS_CANCELLED);
        }
        sl_http_request_terminal_cancel(request, SL_HTTP_REQUEST_STATE_CANCELLED);
        return sl_http_backend_cancelled_diag(out_diag);
    }
    if (request->connection->backend != NULL &&
        request->connection->backend->state == SL_HTTP_BACKEND_STATE_STOPPING)
    {
        sl_http_request_terminal_cancel(request, SL_HTTP_REQUEST_STATE_CANCELLED);
        return sl_http_backend_shutdown_diag(out_diag, SL_STATUS_CANCELLED);
    }

    request->state = SL_HTTP_REQUEST_STATE_DISPATCHING;
    request->connection->state = SL_HTTP_CONNECTION_STATE_DISPATCHING;
    return sl_status_ok();
}

SlStatus sl_http_request_begin_write(SlHttpRequestLifecycle* request, SlDiag* out_diag)
{
    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (request == NULL || request->connection == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (request->state != SL_HTTP_REQUEST_STATE_DISPATCHING) {
        return sl_http_backend_invalid_state(out_diag);
    }

    request->state = SL_HTTP_REQUEST_STATE_WRITING_RESPONSE;
    request->connection->state = SL_HTTP_CONNECTION_STATE_WRITING_RESPONSE;
    return sl_status_ok();
}

SlStatus sl_http_request_complete(SlHttpRequestLifecycle* request, SlDiag* out_diag)
{
    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (request == NULL || request->connection == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (request->state != SL_HTTP_REQUEST_STATE_WRITING_RESPONSE) {
        return sl_http_backend_invalid_state(out_diag);
    }

    request->state = SL_HTTP_REQUEST_STATE_COMPLETED;
    request->connection->state = request->connection->backend != NULL &&
                                         request->connection->backend->options.keep_alive_enabled
                                     ? SL_HTTP_CONNECTION_STATE_OPEN
                                     : SL_HTTP_CONNECTION_STATE_CLOSING;
    sl_http_request_release_admission(request);
    return sl_status_ok();
}

SlStatus sl_http_request_fail(SlHttpRequestLifecycle* request, SlDiag* out_diag)
{
    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (request == NULL || request->connection == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (request->state != SL_HTTP_REQUEST_STATE_FAILED) {
        request->state = SL_HTTP_REQUEST_STATE_FAILED;
    }
    request->connection->state = SL_HTTP_CONNECTION_STATE_ERROR;
    sl_http_request_release_admission(request);
    return sl_status_ok();
}

SlStatus sl_http_request_cancel(SlHttpRequestLifecycle* request, SlCancellationReason reason,
                                SlStr detail, SlDiag* out_diag)
{
    SlStatus status;

    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (request == NULL || reason == SL_CANCELLATION_REASON_NONE ||
        !sl_http_backend_str_valid(detail))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (sl_http_request_terminal(request->state)) {
        return sl_http_backend_invalid_state(out_diag);
    }

    status = sl_cancellation_token_cancel(&request->cancellation, reason, detail);
    if (!sl_status_is_ok(status) && sl_status_code(status) != SL_STATUS_INVALID_STATE) {
        return status;
    }

    if (reason == SL_CANCELLATION_REASON_DEADLINE_EXCEEDED) {
        request->state = SL_HTTP_REQUEST_STATE_TIMED_OUT;
        if (request->connection != NULL) {
            request->connection->state = SL_HTTP_CONNECTION_STATE_CLOSING;
        }
        sl_http_request_release_admission(request);
        return sl_http_backend_timeout_diag(out_diag);
    }
    if (reason == SL_CANCELLATION_REASON_SHUTDOWN) {
        sl_http_request_terminal_cancel(request, SL_HTTP_REQUEST_STATE_CANCELLED);
        return sl_http_backend_shutdown_diag(out_diag, SL_STATUS_CANCELLED);
    }

    sl_http_request_terminal_cancel(request, SL_HTTP_REQUEST_STATE_CANCELLED);
    return sl_http_backend_cancelled_diag(out_diag);
}

SlStatus sl_http_request_shutdown(SlHttpRequestLifecycle* request, SlDiag* out_diag)
{
    return sl_http_request_cancel(request, SL_CANCELLATION_REASON_SHUTDOWN,
                                  sl_str_from_cstr("HTTP backend shutdown"), out_diag);
}

SlStatus sl_http_request_timeout(SlHttpRequestLifecycle* request, SlStr detail, SlDiag* out_diag)
{
    SlStatus status;

    if (request == NULL || !sl_http_backend_str_valid(detail)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (sl_http_request_terminal(request->state)) {
        return sl_http_backend_invalid_state(out_diag);
    }

    status = sl_cancellation_token_cancel(&request->cancellation,
                                          SL_CANCELLATION_REASON_DEADLINE_EXCEEDED, detail);
    if (!sl_status_is_ok(status) && sl_status_code(status) != SL_STATUS_INVALID_STATE) {
        return status;
    }

    request->state = SL_HTTP_REQUEST_STATE_TIMED_OUT;
    if (request->connection != NULL) {
        request->connection->state = SL_HTTP_CONNECTION_STATE_CLOSING;
    }
    sl_http_request_release_admission(request);
    return sl_http_backend_timeout_diag(out_diag);
}

SlStatus sl_http_request_close(SlHttpRequestLifecycle* request, SlDiag* out_diag)
{
    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (request == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    sl_http_request_release_admission(request);
    request->state = SL_HTTP_REQUEST_STATE_CLOSED;
    request->head = (SlHttpRequestHead){0};
    return sl_status_ok();
}

SlHttpBackendState sl_http_backend_state(const SlHttpBackend* backend)
{
    return backend == NULL ? SL_HTTP_BACKEND_STATE_UNINITIALIZED : backend->state;
}

SlHttpConnectionState sl_http_connection_state(const SlHttpConnection* connection)
{
    return connection == NULL ? SL_HTTP_CONNECTION_STATE_NONE : connection->state;
}

SlHttpRequestState sl_http_request_state(const SlHttpRequestLifecycle* request)
{
    return request == NULL ? SL_HTTP_REQUEST_STATE_NONE : request->state;
}

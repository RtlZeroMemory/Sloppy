/*
 * src/core/http_backend.c
 *
 * ENGINE-13.A/B/C core HTTP backend foundation. This file owns only Slop state,
 * admission, lifecycle, parser-limit, and cancellation/deadline bookkeeping. Concrete
 * sockets/listeners remain behind the platform boundary.
 */
#include "sloppy/http_backend.h"

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
        request->state = SL_HTTP_REQUEST_STATE_CANCELLED;
        return sl_status_from_code(sl_cancellation_status_code(request->cancellation.reason));
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

#ifndef SLOPPY_HTTP_BACKEND_H
#define SLOPPY_HTTP_BACKEND_H

#include "sloppy/arena.h"
#include "sloppy/builder.h"
#include "sloppy/cancellation.h"
#include "sloppy/diagnostics.h"
#include "sloppy/http.h"
#include "sloppy/http_context.h"
#include "sloppy/status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SL_HTTP_BACKEND_DEFAULT_MAX_CONNECTIONS 128U
#define SL_HTTP_BACKEND_DEFAULT_MAX_ACTIVE_REQUESTS 128U

typedef struct SlHttpPlatformListener SlHttpPlatformListener;

typedef enum SlHttpBackendState
{
    SL_HTTP_BACKEND_STATE_UNINITIALIZED = 0,
    SL_HTTP_BACKEND_STATE_INITIALIZED = 1,
    SL_HTTP_BACKEND_STATE_STARTED = 2,
    SL_HTTP_BACKEND_STATE_STOPPING = 3,
    SL_HTTP_BACKEND_STATE_STOPPED = 4,
    SL_HTTP_BACKEND_STATE_DISPOSED = 5,
    SL_HTTP_BACKEND_STATE_ERROR = 6
} SlHttpBackendState;

typedef enum SlHttpListenerState
{
    SL_HTTP_LISTENER_STATE_UNBOUND = 0,
    SL_HTTP_LISTENER_STATE_BOUND = 1,
    SL_HTTP_LISTENER_STATE_LISTENING = 2,
    SL_HTTP_LISTENER_STATE_STOPPING = 3,
    SL_HTTP_LISTENER_STATE_STOPPED = 4,
    SL_HTTP_LISTENER_STATE_ERROR = 5
} SlHttpListenerState;

typedef enum SlHttpConnectionState
{
    SL_HTTP_CONNECTION_STATE_NONE = 0,
    SL_HTTP_CONNECTION_STATE_ACCEPTED = 1,
    SL_HTTP_CONNECTION_STATE_OPEN = 2,
    SL_HTTP_CONNECTION_STATE_READING_REQUEST = 3,
    SL_HTTP_CONNECTION_STATE_DISPATCHING = 4,
    SL_HTTP_CONNECTION_STATE_WRITING_RESPONSE = 5,
    SL_HTTP_CONNECTION_STATE_CLOSING = 6,
    SL_HTTP_CONNECTION_STATE_CLOSED = 7,
    SL_HTTP_CONNECTION_STATE_ERROR = 8
} SlHttpConnectionState;

typedef enum SlHttpRequestState
{
    SL_HTTP_REQUEST_STATE_NONE = 0,
    SL_HTTP_REQUEST_STATE_CREATED = 1,
    SL_HTTP_REQUEST_STATE_READING = 2,
    SL_HTTP_REQUEST_STATE_DISPATCHING = 3,
    SL_HTTP_REQUEST_STATE_WRITING_RESPONSE = 4,
    SL_HTTP_REQUEST_STATE_COMPLETED = 5,
    SL_HTTP_REQUEST_STATE_CANCELLED = 6,
    SL_HTTP_REQUEST_STATE_TIMED_OUT = 7,
    SL_HTTP_REQUEST_STATE_FAILED = 8,
    SL_HTTP_REQUEST_STATE_CLOSED = 9
} SlHttpRequestState;

typedef struct SlHttpBackendOptions
{
    size_t max_connections;
    size_t max_active_requests;
    /*
     * Parser limits copied into each request parse. Zero values use the HTTP parser
     * defaults except max_headers, where zero means no request headers are accepted.
     */
    SlHttpParseOptions parse;
    /*
     * Deadline configuration in milliseconds. The backend owns bounded read/header/request
     * timeout behavior and reports terminal timeout diagnostics through the connection
     * lifecycle.
     */
    uint64_t read_timeout_ms;
    uint64_t header_timeout_ms;
    uint64_t request_timeout_ms;
    /* Enables bounded sequential keep-alive; production-edge HTTP remains out of scope. */
    bool keep_alive_enabled;
} SlHttpBackendOptions;

typedef struct SlHttpListener
{
    SlHttpListenerState state;
    /*
     * Opaque platform listener. Core HTTP code stores the boundary pointer only and never
     * dereferences platform/socket details.
     */
    SlHttpPlatformListener* platform;
} SlHttpListener;

typedef struct SlHttpBackend
{
    SlHttpBackendOptions options;
    SlHttpBackendState state;
    SlHttpListener listener;
    /* Bounded admission counters owned by the backend lifecycle. */
    size_t active_connections;
    size_t active_requests;
    uint64_t next_connection_id;
    uint64_t next_request_id;
} SlHttpBackend;

typedef struct SlHttpConnection
{
    /* Borrowed backend; the backend must outlive admitted connections. */
    SlHttpBackend* backend;
    SlHttpConnectionState state;
    uint64_t id;
    /*
     * Borrowed protocol scheme for request contexts created on this connection. Platform
     * transports may set this to a static or arena-owned "https" view after TLS wrapping
     * succeeds; the view must remain valid until the connection is closed. Core never exposes
     * native handles.
     */
    SlStr scheme;
    size_t request_count;
    /*
     * Multiplexed transports, such as HTTP/2, may admit multiple request lifecycles over
     * one connection without waiting for the connection state to return to OPEN between
     * streams. HTTP/1 transports leave this false and retain the sequential keep-alive
     * contract.
     */
    bool multiplexing;
    /* True only while this connection owns one backend active-connection slot. */
    bool slot_admitted;
} SlHttpConnection;

typedef struct SlHttpRequestLifecycle
{
    /* Borrowed connection and arena; both must outlive this request lifecycle. */
    SlHttpConnection* connection;
    SlArena* arena;
    uint64_t id;
    /*
     * Non-owning scheme copied from connection->scheme by sl_http_request_begin. It may be
     * empty/null when the connection has no scheme yet, and borrows the same connection-bounded
     * lifetime as SlHttpConnection.scheme.
     */
    SlStr scheme;
    SlHttpRequestState state;
    /* Arena-owned parsed request head, cleared by sl_http_request_close. */
    SlHttpRequestHead head;
    SlCancellationToken cancellation;
    /* True only while this request owns one backend active-request slot. */
    bool admitted;
} SlHttpRequestLifecycle;

typedef enum SlHttpBodyReaderState
{
    SL_HTTP_BODY_READER_STATE_NONE = 0,
    SL_HTTP_BODY_READER_STATE_READY = 1,
    SL_HTTP_BODY_READER_STATE_READING = 2,
    SL_HTTP_BODY_READER_STATE_COMPLETED = 3,
    SL_HTTP_BODY_READER_STATE_CANCELLED = 4,
    SL_HTTP_BODY_READER_STATE_TIMED_OUT = 5,
    SL_HTTP_BODY_READER_STATE_FAILED = 6,
    SL_HTTP_BODY_READER_STATE_CLOSED = 7
} SlHttpBodyReaderState;

typedef struct SlHttpBodyReader
{
    /* Borrowed request and arena; the reader never outlives the request lifecycle. */
    SlHttpRequestLifecycle* request;
    SlArena* arena;
    SlArenaMark mark;
    SlByteBuilder builder;
    SlHttpBodyReaderState state;
    SlHttpRequestBodyKind body_kind;
    size_t max_body_bytes;
    size_t expected_body_bytes;
} SlHttpBodyReader;

/*
 * Initializes caller-owned backend state. The backend owns no platform resources itself and
 * may be started after successful initialization.
 */
SlStatus sl_http_backend_init(SlHttpBackend* backend, const SlHttpBackendOptions* options,
                              SlDiag* out_diag);
/*
 * Starts the backend over an optional opaque platform listener. Passing NULL is valid for
 * deterministic tests and synthetic dispatch paths.
 */
SlStatus sl_http_backend_start(SlHttpBackend* backend, SlHttpPlatformListener* platform,
                               SlDiag* out_diag);
SlStatus sl_http_backend_stop(SlHttpBackend* backend, SlDiag* out_diag);
SlStatus sl_http_backend_dispose(SlHttpBackend* backend, SlDiag* out_diag);

/* Admits one connection if the backend is started and connection capacity remains. */
SlStatus sl_http_backend_accept_connection(SlHttpBackend* backend, SlHttpConnection* out_connection,
                                           SlDiag* out_diag);
/* Releases an admitted connection slot exactly once. */
SlStatus sl_http_connection_close(SlHttpConnection* connection, SlDiag* out_diag);
SlStatus sl_http_connection_fail(SlHttpConnection* connection, SlDiag* out_diag);

/* Begins one request on an open connection and admits one active-request slot. */
SlStatus sl_http_request_begin(SlHttpConnection* connection, SlArena* arena,
                               SlHttpRequestLifecycle* out_request, SlDiag* out_diag);
/* Parses bytes into the request arena using backend-owned parser limits. */
SlStatus sl_http_request_parse_head(SlHttpRequestLifecycle* request, SlBytes bytes,
                                    SlDiag* out_diag);
/*
 * Reads a bounded request body into request-arena storage.
 *
 * This is backend/platform plumbing, not a JavaScript streaming body API. The caller supplies
 * the declared content type and content length before appending chunks. Empty bodies require no
 * content type. Non-empty bodies currently support application/json, application subtypes ending
 * in +json, text/plain, and application/octet-stream only. On success, `request->head.body`
 * borrows arena storage owned by the request lifecycle until `sl_http_request_close` or arena
 * reset. Cancellation, timeout,
 * shutdown, body limit, content-length mismatch, and unsupported media failures reset body
 * allocations and transition the request to a terminal cleanup path exactly once.
 */
SlStatus sl_http_request_body_reader_begin(SlHttpRequestLifecycle* request, SlStr content_type,
                                           size_t content_length, SlHttpBodyReader* out_reader,
                                           SlDiag* out_diag);
SlStatus sl_http_request_body_reader_append(SlHttpBodyReader* reader, SlBytes chunk,
                                            SlDiag* out_diag);
SlStatus sl_http_request_body_reader_finish(SlHttpBodyReader* reader, SlDiag* out_diag);
SlStatus sl_http_request_body_reader_close(SlHttpBodyReader* reader, SlDiag* out_diag);
SlStatus sl_http_request_begin_dispatch(SlHttpRequestLifecycle* request, SlDiag* out_diag);
SlStatus sl_http_request_begin_write(SlHttpRequestLifecycle* request, SlDiag* out_diag);
/* Completes/fails/times out/closes the lifecycle and releases admission exactly once. */
SlStatus sl_http_request_complete(SlHttpRequestLifecycle* request, SlDiag* out_diag);
SlStatus sl_http_request_fail(SlHttpRequestLifecycle* request, SlDiag* out_diag);
SlStatus sl_http_request_cancel(SlHttpRequestLifecycle* request, SlCancellationReason reason,
                                SlStr detail, SlDiag* out_diag);
SlStatus sl_http_request_shutdown(SlHttpRequestLifecycle* request, SlDiag* out_diag);
SlStatus sl_http_request_timeout(SlHttpRequestLifecycle* request, SlStr detail, SlDiag* out_diag);
SlStatus sl_http_request_close(SlHttpRequestLifecycle* request, SlDiag* out_diag);

SlHttpBackendState sl_http_backend_state(const SlHttpBackend* backend);
SlHttpConnectionState sl_http_connection_state(const SlHttpConnection* connection);
SlHttpRequestState sl_http_request_state(const SlHttpRequestLifecycle* request);

#ifdef __cplusplus
}
#endif

#endif

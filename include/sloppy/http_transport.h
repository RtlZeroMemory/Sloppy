#ifndef SLOPPY_HTTP_TRANSPORT_H
#define SLOPPY_HTTP_TRANSPORT_H

#include "sloppy/arena.h"
#include "sloppy/diagnostics.h"
#include "sloppy/http_backend.h"
#include "sloppy/http_response.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SL_HTTP_TRANSPORT_DEFAULT_PORT 5173U
#define SL_HTTP_TRANSPORT_DEFAULT_BACKLOG 128
#define SL_HTTP_TRANSPORT_DEFAULT_MAX_REQUEST_HEAD_BYTES 32768U
#define SL_HTTP_TRANSPORT_DEFAULT_REQUEST_ARENA_BYTES 131072U
#define SL_HTTP_TRANSPORT_DEFAULT_READ_CHUNK_BYTES 4096U
#define SL_HTTP_TRANSPORT_DEFAULT_RESPONSE_BYTES 131072U
#define SL_HTTP_TRANSPORT_DEFAULT_HEADER_READ_TIMEOUT_MS 30000U
#define SL_HTTP_TRANSPORT_DEFAULT_BODY_READ_TIMEOUT_MS 30000U
#define SL_HTTP_TRANSPORT_DEFAULT_REQUEST_TIMEOUT_MS 60000U
#define SL_HTTP_TRANSPORT_DEFAULT_WRITE_TIMEOUT_MS 30000U
#define SL_HTTP_TRANSPORT_DEFAULT_KEEP_ALIVE_IDLE_TIMEOUT_MS 5000U
#define SL_HTTP_TRANSPORT_DEFAULT_MAX_REQUESTS_PER_CONNECTION 100U

typedef struct SlHttpPlatformConnection SlHttpPlatformConnection;
typedef struct SlHttpTransportConnection SlHttpTransportConnection;

typedef void (*SlHttpTransportRequestReadyFn)(SlHttpTransportConnection* connection,
                                              const SlHttpRequestLifecycle* request, void* user);
typedef SlStatus (*SlHttpTransportDispatchFn)(SlHttpTransportConnection* connection, SlArena* arena,
                                              const SlHttpRequestLifecycle* request,
                                              SlHttpResponse* out_response, SlDiag* out_diag,
                                              void* user);

typedef enum SlHttpTransportServerState
{
    SL_HTTP_TRANSPORT_SERVER_STATE_NONE = 0,
    SL_HTTP_TRANSPORT_SERVER_STATE_CREATED = 1,
    SL_HTTP_TRANSPORT_SERVER_STATE_LISTENING = 2,
    SL_HTTP_TRANSPORT_SERVER_STATE_STOPPING = 3,
    SL_HTTP_TRANSPORT_SERVER_STATE_STOPPED = 4,
    SL_HTTP_TRANSPORT_SERVER_STATE_DISPOSED = 5,
    SL_HTTP_TRANSPORT_SERVER_STATE_ERROR = 6
} SlHttpTransportServerState;

typedef enum SlHttpTransportConnectionState
{
    SL_HTTP_TRANSPORT_CONNECTION_STATE_EMPTY = 0,
    SL_HTTP_TRANSPORT_CONNECTION_STATE_ACCEPTED = 1,
    SL_HTTP_TRANSPORT_CONNECTION_STATE_READING_HEAD = 2,
    SL_HTTP_TRANSPORT_CONNECTION_STATE_READING_BODY = 3,
    SL_HTTP_TRANSPORT_CONNECTION_STATE_REQUEST_READY = 4,
    SL_HTTP_TRANSPORT_CONNECTION_STATE_DISPATCHING = 5,
    SL_HTTP_TRANSPORT_CONNECTION_STATE_WRITING_RESPONSE = 6,
    SL_HTTP_TRANSPORT_CONNECTION_STATE_KEEP_ALIVE_IDLE = 7,
    SL_HTTP_TRANSPORT_CONNECTION_STATE_CLOSING = 8,
    SL_HTTP_TRANSPORT_CONNECTION_STATE_CLOSED = 9,
    SL_HTTP_TRANSPORT_CONNECTION_STATE_ERROR = 10
} SlHttpTransportConnectionState;

typedef struct SlHttpTransportConfig
{
    /*
     * Borrowed host view copied during init as a NUL-terminated platform boundary string.
     * NULL/empty uses the default only when the whole config is omitted.
     */
    SlStr host;
    /* 0 binds an ephemeral localhost test port; omitted config uses the default dev port. */
    uint32_t port;
    size_t max_connections;
    size_t max_active_requests;
    SlHttpParseOptions parse;
    size_t max_request_head_bytes;
    size_t request_arena_bytes;
    size_t read_chunk_bytes;
    size_t max_response_bytes;
    /*
     * Timeout hooks in milliseconds. Zero uses the bounded transport defaults. Timers are
     * enforced by the platform transport loop and transition the connection/request to a
     * terminal cleanup path; they are not production graceful-drain tuning knobs.
     */
    uint64_t header_read_timeout_ms;
    uint64_t body_read_timeout_ms;
    uint64_t request_timeout_ms;
    uint64_t write_timeout_ms;
    uint64_t keep_alive_idle_timeout_ms;
    size_t max_requests_per_connection;
    bool keep_alive_disabled;
    /* Caller/arena-backed table size for accepted connection placeholders. */
    size_t connection_capacity;
    int backlog;
    SlHttpTransportRequestReadyFn on_request_ready;
    void* on_request_ready_user;
    SlHttpTransportDispatchFn dispatch;
    void* dispatch_user;
} SlHttpTransportConfig;

struct SlHttpTransportConnection
{
    SlHttpConnection core;
    SlHttpPlatformConnection* platform;
    SlHttpTransportConnectionState state;
    SlArena request_arena;
    SlHttpRequestLifecycle request;
    SlHttpBodyReader body_reader;
    unsigned char* request_storage;
    size_t request_storage_size;
    unsigned char* accumulation;
    unsigned char* response_storage;
    SlByteBuilder accumulation_builder;
    size_t accumulation_length;
    size_t accumulation_capacity;
    size_t response_storage_size;
    size_t response_length;
    size_t head_length;
    size_t expected_body_length;
    bool request_started;
    bool body_reader_started;
    bool body_reader_finished;
    bool write_started;
    bool write_completed;
    bool close_after_write;
    bool keep_alive_after_write;
    SlDiag last_diag;
    bool slot_claimed;
};

typedef struct SlHttpTransportServer
{
    /* Borrowed arena; all transport/server storage remains valid until this arena resets. */
    SlArena* arena;
    SlHttpTransportConfig config;
    SlOwnedStr host;
    SlHttpBackend backend;
    SlHttpPlatformListener* platform;
    SlHttpPlatformConnection* platform_connections;
    SlHttpTransportConnection* connections;
    size_t connection_capacity;
    SlHttpTransportServerState state;
    size_t accepted_connections;
    size_t rejected_connections;
    size_t accept_failures;
    size_t client_close_requests;
    size_t server_forced_closes;
    size_t idle_timeouts;
    size_t max_requests_reached;
    size_t pipelining_attempts;
    size_t shutdown_idle_closes;
} SlHttpTransportServer;

/*
 * Initializes a caller-owned transport server over arena-owned platform/listener storage.
 * No socket is opened until `sl_http_transport_server_listen`.
 */
SlStatus sl_http_transport_server_init(SlHttpTransportServer* server, SlArena* arena,
                                       const SlHttpTransportConfig* config, SlDiag* out_diag);
/* Binds and listens on the configured host/port. Libuv state remains internal. */
SlStatus sl_http_transport_server_listen(SlHttpTransportServer* server, SlDiag* out_diag);
/* Runs one nonblocking platform event-loop tick for deterministic tests/runtime integration. */
SlStatus sl_http_transport_server_poll(SlHttpTransportServer* server, SlDiag* out_diag);
/* Runs the platform event loop until the transport stops or the loop has no active handles. */
SlStatus sl_http_transport_server_run(SlHttpTransportServer* server, SlDiag* out_diag);
/* Stops accepting, closes accepted connections, and releases listener handles. */
SlStatus sl_http_transport_server_stop(SlHttpTransportServer* server, SlDiag* out_diag);
/* Disposes backend state after stopping as needed. Arena-owned storage is not freed. */
SlStatus sl_http_transport_server_dispose(SlHttpTransportServer* server, SlDiag* out_diag);
/* Closes an accepted connection exactly once. */
SlStatus sl_http_transport_connection_close(SlHttpTransportConnection* connection,
                                            SlDiag* out_diag);
/*
 * Internal/test helper that feeds bytes through the same bounded accumulation path used by
 * the platform read callback. Request bytes move through normal lifecycle transitions;
 * configured callbacks may still trigger dispatch or write paths.
 */
SlStatus sl_http_transport_connection_feed_test(SlHttpTransportConnection* connection,
                                                SlBytes bytes, SlDiag* out_diag);

SlHttpTransportServerState sl_http_transport_server_state(const SlHttpTransportServer* server);
uint32_t sl_http_transport_server_bound_port(const SlHttpTransportServer* server);
size_t sl_http_transport_server_active_connections(const SlHttpTransportServer* server);

#ifdef __cplusplus
}
#endif

#endif

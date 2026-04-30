#ifndef SLOPPY_HTTP_TRANSPORT_H
#define SLOPPY_HTTP_TRANSPORT_H

#include "sloppy/arena.h"
#include "sloppy/diagnostics.h"
#include "sloppy/http_backend.h"
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

typedef struct SlHttpPlatformConnection SlHttpPlatformConnection;

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
    SL_HTTP_TRANSPORT_CONNECTION_STATE_CLOSING = 2,
    SL_HTTP_TRANSPORT_CONNECTION_STATE_CLOSED = 3,
    SL_HTTP_TRANSPORT_CONNECTION_STATE_ERROR = 4
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
    /* Caller/arena-backed table size for accepted connection placeholders. */
    size_t connection_capacity;
    int backlog;
} SlHttpTransportConfig;

typedef struct SlHttpTransportConnection
{
    SlHttpConnection core;
    SlHttpPlatformConnection* platform;
    SlHttpTransportConnectionState state;
    bool slot_claimed;
} SlHttpTransportConnection;

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
/* Stops accepting, closes placeholder accepted connections, and releases listener handles. */
SlStatus sl_http_transport_server_stop(SlHttpTransportServer* server, SlDiag* out_diag);
/* Disposes backend state after stopping as needed. Arena-owned storage is not freed. */
SlStatus sl_http_transport_server_dispose(SlHttpTransportServer* server, SlDiag* out_diag);
/* Closes an accepted placeholder connection exactly once. */
SlStatus sl_http_transport_connection_close(SlHttpTransportConnection* connection,
                                            SlDiag* out_diag);

SlHttpTransportServerState sl_http_transport_server_state(const SlHttpTransportServer* server);
uint32_t sl_http_transport_server_bound_port(const SlHttpTransportServer* server);
size_t sl_http_transport_server_active_connections(const SlHttpTransportServer* server);

#ifdef __cplusplus
}
#endif

#endif

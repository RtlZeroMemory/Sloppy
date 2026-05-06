#ifndef SLOPPY_NET_H
#define SLOPPY_NET_H

#include "sloppy/arena.h"
#include "sloppy/bytes.h"
#include "sloppy/cancellation.h"
#include "sloppy/diagnostics.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SlTcpConnection SlTcpConnection;
typedef struct SlTcpListener SlTcpListener;
typedef struct SlLocalConnection SlLocalConnection;
typedef struct SlLocalServer SlLocalServer;

typedef enum SlNetworkAddressFamily
{
    SL_NETWORK_ADDRESS_UNSPECIFIED = 0,
    SL_NETWORK_ADDRESS_IPV4 = 1,
    SL_NETWORK_ADDRESS_IPV6 = 2
} SlNetworkAddressFamily;

typedef enum SlTcpConnectionState
{
    SL_TCP_CONNECTION_CONNECTING = 0,
    SL_TCP_CONNECTION_CONNECTED = 1,
    SL_TCP_CONNECTION_HALF_CLOSED = 2,
    SL_TCP_CONNECTION_CLOSING = 3,
    SL_TCP_CONNECTION_CLOSED = 4,
    SL_TCP_CONNECTION_ABORTED = 5,
    SL_TCP_CONNECTION_FAILED = 6
} SlTcpConnectionState;

typedef enum SlTcpListenerState
{
    SL_TCP_LISTENER_UNBOUND = 0,
    SL_TCP_LISTENER_LISTENING = 1,
    SL_TCP_LISTENER_CLOSING = 2,
    SL_TCP_LISTENER_CLOSED = 3,
    SL_TCP_LISTENER_ABORTED = 4,
    SL_TCP_LISTENER_FAILED = 5
} SlTcpListenerState;

typedef enum SlLocalEndpointBackend
{
    SL_LOCAL_ENDPOINT_BACKEND_AUTO = 0,
    SL_LOCAL_ENDPOINT_BACKEND_UNIX = 1,
    SL_LOCAL_ENDPOINT_BACKEND_NAMED_PIPE = 2
} SlLocalEndpointBackend;

typedef enum SlLocalConnectionState
{
    SL_LOCAL_CONNECTION_CONNECTING = 0,
    SL_LOCAL_CONNECTION_CONNECTED = 1,
    SL_LOCAL_CONNECTION_CLOSING = 2,
    SL_LOCAL_CONNECTION_CLOSED = 3,
    SL_LOCAL_CONNECTION_ABORTED = 4,
    SL_LOCAL_CONNECTION_FAILED = 5
} SlLocalConnectionState;

typedef enum SlLocalServerState
{
    SL_LOCAL_SERVER_UNBOUND = 0,
    SL_LOCAL_SERVER_LISTENING = 1,
    SL_LOCAL_SERVER_CLOSING = 2,
    SL_LOCAL_SERVER_CLOSED = 3,
    SL_LOCAL_SERVER_ABORTED = 4,
    SL_LOCAL_SERVER_FAILED = 5
} SlLocalServerState;

typedef struct SlNetworkEndpoint
{
    SlNetworkAddressFamily family;
    SlStr host;
    uint16_t port;
} SlNetworkEndpoint;

typedef struct SlTcpKeepAliveOptions
{
    bool enabled;
    uint32_t delay_ms;
} SlTcpKeepAliveOptions;

typedef struct SlTcpConnectOptions
{
    SlStr host;
    uint16_t port;
    bool has_timeout_ms;
    uint32_t timeout_ms;
    bool no_delay;
    SlTcpKeepAliveOptions keep_alive;
    size_t read_buffer_capacity;
} SlTcpConnectOptions;

typedef struct SlTcpListenOptions
{
    SlStr host;
    uint16_t port;
    uint32_t backlog;
    size_t read_buffer_capacity;
} SlTcpListenOptions;

typedef struct SlTcpAcceptOptions
{
    bool has_timeout_ms;
    uint32_t timeout_ms;
} SlTcpAcceptOptions;

typedef struct SlLocalConnectOptions
{
    SlStr path;
    SlLocalEndpointBackend backend;
    bool has_timeout_ms;
    uint32_t timeout_ms;
    size_t read_buffer_capacity;
    const SlCancellationToken* cancellation;
} SlLocalConnectOptions;

typedef struct SlLocalListenOptions
{
    SlStr path;
    SlLocalEndpointBackend backend;
    bool unlink_existing;
    bool has_permissions;
    uint16_t permissions;
    uint32_t backlog;
    size_t read_buffer_capacity;
} SlLocalListenOptions;

typedef struct SlLocalAcceptOptions
{
    bool has_timeout_ms;
    uint32_t timeout_ms;
    const SlCancellationToken* cancellation;
} SlLocalAcceptOptions;

typedef struct SlLocalIoOptions
{
    bool has_timeout_ms;
    uint32_t timeout_ms;
    const SlCancellationToken* cancellation;
} SlLocalIoOptions;

SlTcpConnectOptions sl_tcp_connect_options_default(SlStr host, uint16_t port);
SlTcpListenOptions sl_tcp_listen_options_default(SlStr host, uint16_t port);
SlTcpAcceptOptions sl_tcp_accept_options_default(void);

SlLocalConnectOptions sl_local_connect_options_default(SlStr path);
SlLocalListenOptions sl_local_listen_options_default(SlStr path);
SlLocalAcceptOptions sl_local_accept_options_default(void);
SlLocalIoOptions sl_local_io_options_default(void);

SlStatus sl_local_endpoint_connect(SlArena* arena, const SlLocalConnectOptions* options,
                                   SlLocalConnection** out_connection, SlDiag* out_diag);
SlStatus sl_local_endpoint_listen(SlArena* arena, const SlLocalListenOptions* options,
                                  SlLocalServer** out_server, SlDiag* out_diag);
SlLocalServerState sl_local_server_state(const SlLocalServer* server);
SlStatus sl_local_server_accept(SlLocalServer* server, SlArena* arena,
                                const SlLocalAcceptOptions* options,
                                SlLocalConnection** out_connection, SlDiag* out_diag);
SlStatus sl_local_server_close(SlLocalServer* server, SlDiag* out_diag);
SlStatus sl_local_server_abort(SlLocalServer* server, SlDiag* out_diag);

SlLocalConnectionState sl_local_connection_state(const SlLocalConnection* connection);
SlStatus sl_local_connection_write(SlLocalConnection* connection, SlBytes bytes, SlDiag* out_diag);
SlStatus sl_local_connection_write_ex(SlLocalConnection* connection, SlBytes bytes,
                                      const SlLocalIoOptions* options, SlDiag* out_diag);
SlStatus sl_local_connection_write_text(SlLocalConnection* connection, SlStr text,
                                        SlDiag* out_diag);
SlStatus sl_local_connection_write_text_ex(SlLocalConnection* connection, SlStr text,
                                           const SlLocalIoOptions* options, SlDiag* out_diag);
SlStatus sl_local_connection_read(SlLocalConnection* connection, SlArena* arena, size_t max_bytes,
                                  SlOwnedBytes* out, SlDiag* out_diag);
SlStatus sl_local_connection_read_ex(SlLocalConnection* connection, SlArena* arena,
                                     size_t max_bytes, const SlLocalIoOptions* options,
                                     SlOwnedBytes* out, SlDiag* out_diag);
SlStatus sl_local_connection_read_until(SlLocalConnection* connection, SlArena* arena,
                                        SlBytes delimiter, size_t max_bytes, SlOwnedBytes* out,
                                        SlDiag* out_diag);
SlStatus sl_local_connection_read_until_ex(SlLocalConnection* connection, SlArena* arena,
                                           SlBytes delimiter, size_t max_bytes,
                                           const SlLocalIoOptions* options, SlOwnedBytes* out,
                                           SlDiag* out_diag);
SlStatus sl_local_connection_read_line(SlLocalConnection* connection, SlArena* arena,
                                       size_t max_bytes, SlOwnedStr* out, SlDiag* out_diag);
SlStatus sl_local_connection_read_line_ex(SlLocalConnection* connection, SlArena* arena,
                                          size_t max_bytes, const SlLocalIoOptions* options,
                                          SlOwnedStr* out, SlDiag* out_diag);
SlStatus sl_local_connection_close(SlLocalConnection* connection, SlDiag* out_diag);
SlStatus sl_local_connection_abort(SlLocalConnection* connection, SlDiag* out_diag);

SlStatus sl_tcp_client_connect(SlArena* arena, const SlTcpConnectOptions* options,
                               SlTcpConnection** out_connection, SlDiag* out_diag);

SlStatus sl_tcp_listener_listen(SlArena* arena, const SlTcpListenOptions* options,
                                SlTcpListener** out_listener, SlDiag* out_diag);
SlTcpListenerState sl_tcp_listener_state(const SlTcpListener* listener);
SlStatus sl_tcp_listener_local_endpoint(const SlTcpListener* listener,
                                        SlNetworkEndpoint* out_endpoint);
SlStatus sl_tcp_listener_accept(SlTcpListener* listener, SlArena* arena,
                                const SlTcpAcceptOptions* options, SlTcpConnection** out_connection,
                                SlDiag* out_diag);
SlStatus sl_tcp_listener_close(SlTcpListener* listener, SlDiag* out_diag);
SlStatus sl_tcp_listener_abort(SlTcpListener* listener, SlDiag* out_diag);

SlTcpConnectionState sl_tcp_connection_state(const SlTcpConnection* connection);
SlStatus sl_tcp_connection_local_endpoint(const SlTcpConnection* connection,
                                          SlNetworkEndpoint* out_endpoint);
SlStatus sl_tcp_connection_remote_endpoint(const SlTcpConnection* connection,
                                           SlNetworkEndpoint* out_endpoint);

SlStatus sl_tcp_connection_write(SlTcpConnection* connection, SlBytes bytes, SlDiag* out_diag);
SlStatus sl_tcp_connection_write_text(SlTcpConnection* connection, SlStr text, SlDiag* out_diag);
SlStatus sl_tcp_connection_read(SlTcpConnection* connection, SlArena* arena, size_t max_bytes,
                                SlOwnedBytes* out, SlDiag* out_diag);
SlStatus sl_tcp_connection_read_until(SlTcpConnection* connection, SlArena* arena,
                                      SlBytes delimiter, size_t max_bytes, SlOwnedBytes* out,
                                      SlDiag* out_diag);
SlStatus sl_tcp_connection_read_line(SlTcpConnection* connection, SlArena* arena, size_t max_bytes,
                                     SlOwnedStr* out, SlDiag* out_diag);

SlStatus sl_tcp_connection_close(SlTcpConnection* connection, SlDiag* out_diag);
SlStatus sl_tcp_connection_abort(SlTcpConnection* connection, SlDiag* out_diag);

#ifdef __cplusplus
}
#endif

#endif

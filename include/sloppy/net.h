#ifndef SLOPPY_NET_H
#define SLOPPY_NET_H

#include "sloppy/arena.h"
#include "sloppy/bytes.h"
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

SlTcpConnectOptions sl_tcp_connect_options_default(SlStr host, uint16_t port);

SlStatus sl_tcp_client_connect(SlArena* arena, const SlTcpConnectOptions* options,
                               SlTcpConnection** out_connection, SlDiag* out_diag);

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

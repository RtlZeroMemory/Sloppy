/*
 * src/platform/win32/net_local_win32.c
 *
 * Honest local IPC placeholder for the Windows backend. Named pipe execution is CORE-NET-02.D
 * and remains unsupported until that slice lands; this file keeps the portable LocalEndpoint
 * native contract linkable on Windows without overclaiming platform support.
 */
#include "sloppy/net.h"

#define SL_LOCAL_DEFAULT_READ_BUFFER_CAPACITY 8192U

SlLocalConnectOptions sl_local_connect_options_default(SlStr path)
{
    SlLocalConnectOptions options = {0};

    options.path = path;
    options.backend = SL_LOCAL_ENDPOINT_BACKEND_AUTO;
    options.read_buffer_capacity = SL_LOCAL_DEFAULT_READ_BUFFER_CAPACITY;
    return options;
}

SlLocalListenOptions sl_local_listen_options_default(SlStr path)
{
    SlLocalListenOptions options = {0};

    options.path = path;
    options.backend = SL_LOCAL_ENDPOINT_BACKEND_AUTO;
    options.backlog = 128U;
    options.read_buffer_capacity = SL_LOCAL_DEFAULT_READ_BUFFER_CAPACITY;
    return options;
}

SlLocalAcceptOptions sl_local_accept_options_default(void)
{
    SlLocalAcceptOptions options = {0};
    return options;
}

static void sl_local_win32_diag(SlDiag* out_diag, SlDiagCode code, SlStr message)
{
    if (out_diag == NULL) {
        return;
    }
    *out_diag = (SlDiag){0};
    out_diag->severity = SL_DIAG_SEVERITY_ERROR;
    out_diag->code = code;
    out_diag->message = message;
    out_diag->primary_span = sl_source_span_unknown();
}

static SlStatus sl_local_win32_unsupported(SlDiag* out_diag)
{
    sl_local_win32_diag(out_diag, SL_DIAG_NET_LOCAL_IPC_UNSUPPORTED_PLATFORM,
                        sl_str_from_cstr("local IPC platform is unsupported"));
    return sl_status_from_code(SL_STATUS_UNSUPPORTED);
}

SlStatus sl_local_endpoint_connect(SlArena* arena, const SlLocalConnectOptions* options,
                                   SlLocalConnection** out_connection, SlDiag* out_diag)
{
    (void)arena;
    (void)options;
    if (out_connection == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_connection = NULL;
    return sl_local_win32_unsupported(out_diag);
}

SlStatus sl_local_endpoint_listen(SlArena* arena, const SlLocalListenOptions* options,
                                  SlLocalServer** out_server, SlDiag* out_diag)
{
    (void)arena;
    (void)options;
    if (out_server == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_server = NULL;
    return sl_local_win32_unsupported(out_diag);
}

SlLocalServerState sl_local_server_state(const SlLocalServer* server)
{
    (void)server;
    return SL_LOCAL_SERVER_FAILED;
}

SlStatus sl_local_server_accept(SlLocalServer* server, SlArena* arena,
                                const SlLocalAcceptOptions* options,
                                SlLocalConnection** out_connection, SlDiag* out_diag)
{
    (void)server;
    (void)arena;
    (void)options;
    if (out_connection == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_connection = NULL;
    return sl_local_win32_unsupported(out_diag);
}

SlStatus sl_local_server_close(SlLocalServer* server, SlDiag* out_diag)
{
    (void)server;
    (void)out_diag;
    return sl_status_ok();
}

SlStatus sl_local_server_abort(SlLocalServer* server, SlDiag* out_diag)
{
    (void)server;
    (void)out_diag;
    return sl_status_ok();
}

SlLocalConnectionState sl_local_connection_state(const SlLocalConnection* connection)
{
    (void)connection;
    return SL_LOCAL_CONNECTION_FAILED;
}

SlStatus sl_local_connection_write(SlLocalConnection* connection, SlBytes bytes, SlDiag* out_diag)
{
    (void)connection;
    (void)bytes;
    return sl_local_win32_unsupported(out_diag);
}

SlStatus sl_local_connection_write_text(SlLocalConnection* connection, SlStr text, SlDiag* out_diag)
{
    (void)connection;
    (void)text;
    return sl_local_win32_unsupported(out_diag);
}

SlStatus sl_local_connection_read(SlLocalConnection* connection, SlArena* arena, size_t max_bytes,
                                  SlOwnedBytes* out, SlDiag* out_diag)
{
    (void)connection;
    (void)arena;
    (void)max_bytes;
    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlOwnedBytes){0};
    return sl_local_win32_unsupported(out_diag);
}

SlStatus sl_local_connection_read_until(SlLocalConnection* connection, SlArena* arena,
                                        SlBytes delimiter, size_t max_bytes, SlOwnedBytes* out,
                                        SlDiag* out_diag)
{
    (void)connection;
    (void)arena;
    (void)delimiter;
    (void)max_bytes;
    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlOwnedBytes){0};
    return sl_local_win32_unsupported(out_diag);
}

SlStatus sl_local_connection_read_line(SlLocalConnection* connection, SlArena* arena,
                                       size_t max_bytes, SlOwnedStr* out, SlDiag* out_diag)
{
    (void)connection;
    (void)arena;
    (void)max_bytes;
    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlOwnedStr){0};
    return sl_local_win32_unsupported(out_diag);
}

SlStatus sl_local_connection_close(SlLocalConnection* connection, SlDiag* out_diag)
{
    (void)connection;
    (void)out_diag;
    return sl_status_ok();
}

SlStatus sl_local_connection_abort(SlLocalConnection* connection, SlDiag* out_diag)
{
    (void)connection;
    (void)out_diag;
    return sl_status_ok();
}

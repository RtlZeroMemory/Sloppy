/*
 * src/platform/win32/net_local_win32.c
 *
 * Windows named pipe backend for Slop-owned LocalEndpoint resources. Public APIs expose
 * only SlLocalConnection/SlLocalServer pointers; raw HANDLE values stay private here.
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif

#include "sloppy/net.h"

#include "sloppy/builder.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SL_LOCAL_DEFAULT_READ_BUFFER_CAPACITY 8192U
#define SL_LOCAL_MAX_PIPE_PATH 256U
#define SL_LOCAL_PIPE_BUFFER_SIZE 8192U

struct SlLocalConnection
{
    HANDLE handle;
    SlLocalConnectionState state;
    unsigned char* read_buffer;
    size_t read_buffer_capacity;
};

struct SlLocalServer
{
    HANDLE pending;
    SlLocalServerState state;
    char path[SL_LOCAL_MAX_PIPE_PATH];
    size_t read_buffer_capacity;
    bool first_instance_claimed;
};

static SlStr sl_local_literal(const char* text)
{
    return sl_str_from_cstr(text);
}

static void sl_local_fail_diag(SlDiag* out_diag, SlDiagCode code, SlStr message)
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

static SlStatus sl_local_fail(SlDiag* out_diag, SlDiagCode code, SlStatusCode status, SlStr message)
{
    sl_local_fail_diag(out_diag, code, message);
    return sl_status_from_code(status);
}

static SlStatus sl_local_cancelled_status(const SlCancellationToken* cancellation, SlDiag* out_diag,
                                          SlDiagCode code, SlStr message)
{
    SlCancellationReason reason;

    if (!sl_cancellation_token_is_cancelled(cancellation)) {
        return sl_status_ok();
    }
    reason = sl_cancellation_token_reason(cancellation);
    return sl_local_fail(out_diag, code, sl_cancellation_status_code(reason), message);
}

static bool sl_local_backend_supported(SlLocalEndpointBackend backend)
{
    return backend == SL_LOCAL_ENDPOINT_BACKEND_AUTO ||
           backend == SL_LOCAL_ENDPOINT_BACKEND_NAMED_PIPE;
}

static bool sl_local_path_has_pipe_prefix(SlStr path)
{
    static const char prefix[] = "\\\\.\\pipe\\";
    const size_t prefix_length = sizeof(prefix) - 1U;

    if (path.ptr == NULL || path.length <= prefix_length) {
        return false;
    }
    for (size_t index = 0U; index < prefix_length; index += 1U) {
        if (path.ptr[index] != prefix[index]) {
            return false;
        }
    }
    return true;
}

static SlStatus sl_local_copy_pipe_path(SlStr path, char out[SL_LOCAL_MAX_PIPE_PATH],
                                        SlDiag* out_diag)
{
    if (out == NULL || !sl_local_path_has_pipe_prefix(path) ||
        path.length >= SL_LOCAL_MAX_PIPE_PATH)
    {
        return sl_local_fail(out_diag, SL_DIAG_NET_LOCAL_IPC_INVALID_PATH,
                             SL_STATUS_INVALID_ARGUMENT,
                             sl_local_literal("local IPC endpoint path is invalid"));
    }
    for (size_t index = 0U; index < path.length; index += 1U) {
        if (path.ptr[index] == '\0') {
            return sl_local_fail(out_diag, SL_DIAG_NET_LOCAL_IPC_INVALID_PATH,
                                 SL_STATUS_INVALID_ARGUMENT,
                                 sl_local_literal("local IPC endpoint path is invalid"));
        }
        out[index] = path.ptr[index];
    }
    out[path.length] = '\0';
    return sl_status_ok();
}

static SlStatus sl_local_windows_error_status(DWORD error, SlDiag* out_diag, SlDiagCode code,
                                              SlStatusCode fallback, SlStr message)
{
    if (error == ERROR_ACCESS_DENIED || error == ERROR_PIPE_BUSY || error == ERROR_ALREADY_EXISTS) {
        return sl_local_fail(out_diag, SL_DIAG_NET_LOCAL_IPC_ENDPOINT_EXISTS,
                             SL_STATUS_INVALID_STATE,
                             sl_local_literal("local IPC endpoint already exists"));
    }
    if (error == ERROR_NOT_ENOUGH_MEMORY || error == ERROR_OUTOFMEMORY) {
        return sl_local_fail(out_diag, code, SL_STATUS_OUT_OF_MEMORY, message);
    }
    return sl_local_fail(out_diag, code, fallback, message);
}

static SlStatus sl_local_alloc_connection(SlArena* arena, size_t read_capacity,
                                          SlLocalConnection** out)
{
    void* memory = NULL;
    void* read_memory = NULL;
    size_t capacity = read_capacity == 0U ? SL_LOCAL_DEFAULT_READ_BUFFER_CAPACITY : read_capacity;
    SlStatus status;

    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_arena_alloc(arena, sizeof(SlLocalConnection), _Alignof(SlLocalConnection), &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(arena, capacity, _Alignof(unsigned char), &read_memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    *out = (SlLocalConnection*)memory;
    **out = (SlLocalConnection){0};
    (*out)->handle = INVALID_HANDLE_VALUE;
    (*out)->state = SL_LOCAL_CONNECTION_CONNECTING;
    (*out)->read_buffer = (unsigned char*)read_memory;
    (*out)->read_buffer_capacity = capacity;
    return sl_status_ok();
}

static SlStatus sl_local_alloc_server(SlArena* arena, size_t read_capacity, SlLocalServer** out)
{
    void* memory = NULL;
    SlStatus status;

    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_arena_alloc(arena, sizeof(SlLocalServer), _Alignof(SlLocalServer), &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    *out = (SlLocalServer*)memory;
    **out = (SlLocalServer){0};
    (*out)->pending = INVALID_HANDLE_VALUE;
    (*out)->state = SL_LOCAL_SERVER_UNBOUND;
    (*out)->read_buffer_capacity =
        read_capacity == 0U ? SL_LOCAL_DEFAULT_READ_BUFFER_CAPACITY : read_capacity;
    return sl_status_ok();
}

static HANDLE sl_local_create_pipe_instance(const char* path, bool first_instance)
{
    DWORD open_mode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED;

    if (first_instance) {
        open_mode |= FILE_FLAG_FIRST_PIPE_INSTANCE;
    }
    return CreateNamedPipeA(path, open_mode, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                            PIPE_UNLIMITED_INSTANCES, SL_LOCAL_PIPE_BUFFER_SIZE,
                            SL_LOCAL_PIPE_BUFFER_SIZE, 0U, NULL);
}

static SlStatus sl_local_server_create_pending(SlLocalServer* server, SlDiag* out_diag)
{
    bool first_instance = false;

    if (server == NULL || server->path[0] == '\0') {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    first_instance = !server->first_instance_claimed;
    server->pending = sl_local_create_pipe_instance(server->path, first_instance);
    if (server->pending == INVALID_HANDLE_VALUE) {
        server->state = SL_LOCAL_SERVER_FAILED;
        return sl_local_windows_error_status(
            GetLastError(), out_diag, SL_DIAG_NET_LOCAL_IPC_LISTEN_FAILED, SL_STATUS_INVALID_STATE,
            sl_local_literal("local IPC listen failed"));
    }
    server->first_instance_claimed = true;
    return sl_status_ok();
}

static SlStatus sl_local_wait_overlapped(HANDLE handle, OVERLAPPED* overlapped, bool has_timeout,
                                         uint32_t timeout_ms, DWORD* out_transferred,
                                         SlDiag* out_diag, SlDiagCode timeout_code,
                                         SlStr timeout_message)
{
    DWORD wait_ms = has_timeout ? timeout_ms : INFINITE;
    DWORD wait_result = WaitForSingleObject(overlapped->hEvent, wait_ms);
    DWORD transferred = 0U;

    if (wait_result == WAIT_TIMEOUT) {
        (void)CancelIoEx(handle, overlapped);
        (void)GetOverlappedResult(handle, overlapped, &transferred, TRUE);
        return sl_local_fail(out_diag, timeout_code, SL_STATUS_DEADLINE_EXCEEDED, timeout_message);
    }
    if (wait_result != WAIT_OBJECT_0) {
        return sl_local_fail(out_diag, SL_DIAG_NET_LOCAL_IPC_BACKEND_UNAVAILABLE,
                             SL_STATUS_INTERNAL,
                             sl_local_literal("local IPC backend is unavailable"));
    }
    if (!GetOverlappedResult(handle, overlapped, &transferred, FALSE)) {
        return sl_local_windows_error_status(GetLastError(), out_diag, timeout_code,
                                             SL_STATUS_INVALID_STATE, timeout_message);
    }
    if (out_transferred != NULL) {
        *out_transferred = transferred;
    }
    return sl_status_ok();
}

static SlStatus sl_local_require_connected(SlLocalConnection* connection, SlDiag* out_diag)
{
    if (connection == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (connection->state != SL_LOCAL_CONNECTION_CONNECTED ||
        connection->handle == INVALID_HANDLE_VALUE)
    {
        return sl_local_fail(out_diag, SL_DIAG_NET_LOCAL_IPC_DISPOSED, SL_STATUS_INVALID_STATE,
                             sl_local_literal("local IPC connection or server is disposed"));
    }
    return sl_status_ok();
}

static DWORD sl_local_connect_remaining_ms(ULONGLONG start_ms, uint32_t timeout_ms,
                                           bool* out_expired)
{
    ULONGLONG elapsed = GetTickCount64() - start_ms;

    if (out_expired != NULL) {
        *out_expired = elapsed >= timeout_ms;
    }
    if (elapsed >= timeout_ms) {
        return 0U;
    }
    return (DWORD)(timeout_ms - elapsed);
}

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

SlLocalIoOptions sl_local_io_options_default(void)
{
    SlLocalIoOptions options = {0};
    return options;
}

SlStatus sl_local_endpoint_connect(SlArena* arena, const SlLocalConnectOptions* options,
                                   SlLocalConnection** out_connection, SlDiag* out_diag)
{
    SlLocalConnection* connection = NULL;
    char path[SL_LOCAL_MAX_PIPE_PATH];
    ULONGLONG start_ms = GetTickCount64();
    HANDLE handle = INVALID_HANDLE_VALUE;
    DWORD mode = PIPE_READMODE_BYTE;
    SlStatus status;

    if (out_connection == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_connection = NULL;
    if (arena == NULL || options == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (!sl_local_backend_supported(options->backend)) {
        return sl_local_fail(out_diag, SL_DIAG_NET_LOCAL_IPC_UNSUPPORTED_PLATFORM,
                             SL_STATUS_UNSUPPORTED,
                             sl_local_literal("local IPC platform is unsupported"));
    }
    status = sl_local_copy_pipe_path(options->path, path, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    for (;;) {
        status = sl_local_cancelled_status(options->cancellation, out_diag,
                                           SL_DIAG_NET_LOCAL_IPC_CONNECT_FAILED,
                                           sl_local_literal("local IPC connect failed"));
        if (!sl_status_is_ok(status)) {
            return status;
        }
        handle = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0U, NULL, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
        if (handle != INVALID_HANDLE_VALUE) {
            break;
        }
        {
            DWORD error = GetLastError();
            bool expired = false;
            DWORD wait_ms = NMPWAIT_WAIT_FOREVER;

            if (options->has_timeout_ms) {
                wait_ms = sl_local_connect_remaining_ms(start_ms, options->timeout_ms, &expired);
                if (expired) {
                    return sl_local_fail(out_diag, SL_DIAG_NET_LOCAL_IPC_CONNECT_FAILED,
                                         SL_STATUS_DEADLINE_EXCEEDED,
                                         sl_local_literal("local IPC connect failed"));
                }
            }
            if (error == ERROR_FILE_NOT_FOUND && options->has_timeout_ms) {
                DWORD sleep_ms = wait_ms < 10U ? wait_ms : 10U;
                Sleep(sleep_ms == 0U ? 1U : sleep_ms);
                continue;
            }
            if (error != ERROR_PIPE_BUSY) {
                return sl_local_windows_error_status(
                    error, out_diag, SL_DIAG_NET_LOCAL_IPC_CONNECT_FAILED, SL_STATUS_INVALID_STATE,
                    sl_local_literal("local IPC connect failed"));
            }
            if (!WaitNamedPipeA(path, wait_ms)) {
                error = GetLastError();
                if (error != ERROR_SEM_TIMEOUT && error != ERROR_FILE_NOT_FOUND) {
                    return sl_local_windows_error_status(
                        error, out_diag, SL_DIAG_NET_LOCAL_IPC_CONNECT_FAILED,
                        SL_STATUS_INVALID_STATE, sl_local_literal("local IPC connect failed"));
                }
            }
        }
    }
    status = sl_local_alloc_connection(arena, options->read_buffer_capacity, &connection);
    if (!sl_status_is_ok(status)) {
        (void)CloseHandle(handle);
        return status;
    }
    if (!SetNamedPipeHandleState(handle, &mode, NULL, NULL)) {
        DWORD error = GetLastError();
        (void)CloseHandle(handle);
        connection->state = SL_LOCAL_CONNECTION_FAILED;
        return sl_local_windows_error_status(
            error, out_diag, SL_DIAG_NET_LOCAL_IPC_BACKEND_UNAVAILABLE, SL_STATUS_INTERNAL,
            sl_local_literal("local IPC backend is unavailable"));
    }
    connection->handle = handle;
    connection->state = SL_LOCAL_CONNECTION_CONNECTED;
    *out_connection = connection;
    return sl_status_ok();
}

SlStatus sl_local_endpoint_listen(SlArena* arena, const SlLocalListenOptions* options,
                                  SlLocalServer** out_server, SlDiag* out_diag)
{
    SlLocalServer* server = NULL;
    char path[SL_LOCAL_MAX_PIPE_PATH];
    SlStatus status;

    if (out_server == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_server = NULL;
    if (arena == NULL || options == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (!sl_local_backend_supported(options->backend)) {
        return sl_local_fail(out_diag, SL_DIAG_NET_LOCAL_IPC_UNSUPPORTED_PLATFORM,
                             SL_STATUS_UNSUPPORTED,
                             sl_local_literal("local IPC platform is unsupported"));
    }
    if (options->has_permissions) {
        return sl_local_fail(out_diag, SL_DIAG_NET_LOCAL_IPC_PERMISSION_UNSUPPORTED,
                             SL_STATUS_UNSUPPORTED,
                             sl_local_literal("local IPC permission mode is unsupported"));
    }
    if (options->unlink_existing) {
        return sl_local_fail(out_diag, SL_DIAG_NET_LOCAL_IPC_STALE_CLEANUP_FAILED,
                             SL_STATUS_UNSUPPORTED,
                             sl_local_literal("stale local IPC endpoint cleanup failed"));
    }
    status = sl_local_copy_pipe_path(options->path, path, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_local_alloc_server(arena, options->read_buffer_capacity, &server);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    for (size_t index = 0U; index < SL_LOCAL_MAX_PIPE_PATH; index += 1U) {
        server->path[index] = path[index];
        if (path[index] == '\0') {
            break;
        }
    }
    status = sl_local_server_create_pending(server, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    server->state = SL_LOCAL_SERVER_LISTENING;
    *out_server = server;
    return sl_status_ok();
}

SlLocalServerState sl_local_server_state(const SlLocalServer* server)
{
    return server == NULL ? SL_LOCAL_SERVER_FAILED : server->state;
}

SlStatus sl_local_server_accept(SlLocalServer* server, SlArena* arena,
                                const SlLocalAcceptOptions* options,
                                SlLocalConnection** out_connection, SlDiag* out_diag)
{
    SlLocalConnection* connection = NULL;
    OVERLAPPED overlapped = {0};
    DWORD transferred = 0U;
    BOOL connected = FALSE;
    DWORD error = ERROR_SUCCESS;
    SlStatus status;
    HANDLE accepted = INVALID_HANDLE_VALUE;

    if (server == NULL || arena == NULL || out_connection == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_connection = NULL;
    if (server->state != SL_LOCAL_SERVER_LISTENING || server->pending == INVALID_HANDLE_VALUE) {
        return sl_local_fail(out_diag, SL_DIAG_NET_LOCAL_IPC_DISPOSED, SL_STATUS_INVALID_STATE,
                             sl_local_literal("local IPC connection or server is disposed"));
    }
    status =
        sl_local_cancelled_status(options == NULL ? NULL : options->cancellation, out_diag,
                                  SL_DIAG_NET_LOCAL_IPC_ACCEPT_CANCELLED,
                                  sl_local_literal("local IPC accept was cancelled or timed out"));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    overlapped.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (overlapped.hEvent == NULL) {
        return sl_local_fail(out_diag, SL_DIAG_NET_LOCAL_IPC_BACKEND_UNAVAILABLE,
                             SL_STATUS_INTERNAL,
                             sl_local_literal("local IPC backend is unavailable"));
    }
    connected = ConnectNamedPipe(server->pending, &overlapped);
    if (connected) {
        status = sl_status_ok();
    }
    else {
        error = GetLastError();
        if (error == ERROR_PIPE_CONNECTED) {
            status = sl_status_ok();
        }
        else if (error == ERROR_IO_PENDING) {
            status = sl_local_wait_overlapped(
                server->pending, &overlapped, options != NULL && options->has_timeout_ms,
                options == NULL ? 0U : options->timeout_ms, &transferred, out_diag,
                SL_DIAG_NET_LOCAL_IPC_ACCEPT_CANCELLED,
                sl_local_literal("local IPC accept was cancelled or timed out"));
        }
        else {
            status = sl_local_windows_error_status(
                error, out_diag, SL_DIAG_NET_LOCAL_IPC_ACCEPT_CANCELLED, SL_STATUS_INVALID_STATE,
                sl_local_literal("local IPC accept was cancelled or timed out"));
        }
    }
    (void)CloseHandle(overlapped.hEvent);
    if (!sl_status_is_ok(status)) {
        if (sl_status_code(status) == SL_STATUS_DEADLINE_EXCEEDED) {
            (void)CloseHandle(server->pending);
            server->pending = INVALID_HANDLE_VALUE;
            (void)sl_local_server_create_pending(server, NULL);
        }
        return status;
    }
    status = sl_local_alloc_connection(arena, server->read_buffer_capacity, &connection);
    if (!sl_status_is_ok(status)) {
        (void)CloseHandle(server->pending);
        server->pending = INVALID_HANDLE_VALUE;
        (void)sl_local_server_create_pending(server, NULL);
        return status;
    }
    accepted = server->pending;
    server->pending = INVALID_HANDLE_VALUE;
    connection->handle = accepted;
    connection->state = SL_LOCAL_CONNECTION_CONNECTED;
    status = sl_local_server_create_pending(server, out_diag);
    if (!sl_status_is_ok(status)) {
        connection->state = SL_LOCAL_CONNECTION_FAILED;
        (void)CloseHandle(accepted);
        connection->handle = INVALID_HANDLE_VALUE;
        return status;
    }
    *out_connection = connection;
    return sl_status_ok();
}

static SlStatus sl_local_server_finish_close(SlLocalServer* server, SlLocalServerState state)
{
    if (server == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (server->state == SL_LOCAL_SERVER_CLOSED || server->state == SL_LOCAL_SERVER_ABORTED) {
        return sl_status_ok();
    }
    server->state = SL_LOCAL_SERVER_CLOSING;
    if (server->pending != INVALID_HANDLE_VALUE) {
        (void)CancelIoEx(server->pending, NULL);
        (void)CloseHandle(server->pending);
        server->pending = INVALID_HANDLE_VALUE;
    }
    server->state = state;
    server->path[0] = '\0';
    return sl_status_ok();
}

SlStatus sl_local_server_close(SlLocalServer* server, SlDiag* out_diag)
{
    (void)out_diag;
    return sl_local_server_finish_close(server, SL_LOCAL_SERVER_CLOSED);
}

SlStatus sl_local_server_abort(SlLocalServer* server, SlDiag* out_diag)
{
    (void)out_diag;
    return sl_local_server_finish_close(server, SL_LOCAL_SERVER_ABORTED);
}

SlLocalConnectionState sl_local_connection_state(const SlLocalConnection* connection)
{
    return connection == NULL ? SL_LOCAL_CONNECTION_FAILED : connection->state;
}

SlStatus sl_local_connection_write_ex(SlLocalConnection* connection, SlBytes bytes,
                                      const SlLocalIoOptions* options, SlDiag* out_diag)
{
    SlStatus status = sl_local_require_connected(connection, out_diag);
    size_t written = 0U;

    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (bytes.length != 0U && bytes.ptr == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    while (written < bytes.length) {
        OVERLAPPED overlapped = {0};
        DWORD chunk = bytes.length - written > (size_t)UINT32_MAX ? UINT32_MAX
                                                                  : (DWORD)(bytes.length - written);
        DWORD transferred = 0U;
        BOOL ok = FALSE;

        status = sl_local_cancelled_status(
            options == NULL ? NULL : options->cancellation, out_diag,
            SL_DIAG_NET_LOCAL_IPC_READ_WRITE_CANCELLED,
            sl_local_literal("local IPC read or write was cancelled or timed out"));
        if (!sl_status_is_ok(status)) {
            return status;
        }
        overlapped.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
        if (overlapped.hEvent == NULL) {
            return sl_local_fail(out_diag, SL_DIAG_NET_LOCAL_IPC_BACKEND_UNAVAILABLE,
                                 SL_STATUS_INTERNAL,
                                 sl_local_literal("local IPC backend is unavailable"));
        }
        ok = WriteFile(connection->handle, bytes.ptr + written, chunk, NULL, &overlapped);
        if (ok) {
            if (!GetOverlappedResult(connection->handle, &overlapped, &transferred, FALSE)) {
                DWORD error = GetLastError();
                (void)CloseHandle(overlapped.hEvent);
                return sl_local_windows_error_status(
                    error, out_diag, SL_DIAG_NET_LOCAL_IPC_READ_WRITE_CANCELLED,
                    SL_STATUS_INVALID_STATE,
                    sl_local_literal("local IPC read or write was cancelled or timed out"));
            }
        }
        else {
            DWORD error = GetLastError();
            if (error != ERROR_IO_PENDING) {
                (void)CloseHandle(overlapped.hEvent);
                return sl_local_windows_error_status(
                    error, out_diag, SL_DIAG_NET_LOCAL_IPC_READ_WRITE_CANCELLED,
                    SL_STATUS_INVALID_STATE,
                    sl_local_literal("local IPC read or write was cancelled or timed out"));
            }
            status = sl_local_wait_overlapped(
                connection->handle, &overlapped, options != NULL && options->has_timeout_ms,
                options == NULL ? 0U : options->timeout_ms, &transferred, out_diag,
                SL_DIAG_NET_LOCAL_IPC_READ_WRITE_CANCELLED,
                sl_local_literal("local IPC read or write was cancelled or timed out"));
        }
        (void)CloseHandle(overlapped.hEvent);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        if (transferred == 0U) {
            return sl_local_fail(
                out_diag, SL_DIAG_NET_LOCAL_IPC_READ_WRITE_CANCELLED, SL_STATUS_INVALID_STATE,
                sl_local_literal("local IPC read or write was cancelled or timed out"));
        }
        written += transferred;
    }
    return sl_status_ok();
}

SlStatus sl_local_connection_write(SlLocalConnection* connection, SlBytes bytes, SlDiag* out_diag)
{
    return sl_local_connection_write_ex(connection, bytes, NULL, out_diag);
}

SlStatus sl_local_connection_write_text(SlLocalConnection* connection, SlStr text, SlDiag* out_diag)
{
    if (text.length != 0U && text.ptr == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_local_connection_write_ex(
        connection, sl_bytes_from_parts((const unsigned char*)text.ptr, text.length), NULL,
        out_diag);
}

SlStatus sl_local_connection_write_text_ex(SlLocalConnection* connection, SlStr text,
                                           const SlLocalIoOptions* options, SlDiag* out_diag)
{
    if (text.length != 0U && text.ptr == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_local_connection_write_ex(
        connection, sl_bytes_from_parts((const unsigned char*)text.ptr, text.length), options,
        out_diag);
}

SlStatus sl_local_connection_read_ex(SlLocalConnection* connection, SlArena* arena,
                                     size_t max_bytes, const SlLocalIoOptions* options,
                                     SlOwnedBytes* out, SlDiag* out_diag)
{
    OVERLAPPED overlapped = {0};
    SlStatus status = sl_local_require_connected(connection, out_diag);
    size_t capacity = 0U;
    DWORD requested = 0U;
    DWORD transferred = 0U;
    BOOL ok = FALSE;

    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlOwnedBytes){0};
    capacity = max_bytes == 0U || max_bytes > connection->read_buffer_capacity
                   ? connection->read_buffer_capacity
                   : max_bytes;
    requested = capacity > (size_t)UINT32_MAX ? UINT32_MAX : (DWORD)capacity;
    status = sl_local_cancelled_status(
        options == NULL ? NULL : options->cancellation, out_diag,
        SL_DIAG_NET_LOCAL_IPC_READ_WRITE_CANCELLED,
        sl_local_literal("local IPC read or write was cancelled or timed out"));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    overlapped.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (overlapped.hEvent == NULL) {
        return sl_local_fail(out_diag, SL_DIAG_NET_LOCAL_IPC_BACKEND_UNAVAILABLE,
                             SL_STATUS_INTERNAL,
                             sl_local_literal("local IPC backend is unavailable"));
    }
    ok = ReadFile(connection->handle, connection->read_buffer, requested, NULL, &overlapped);
    if (ok) {
        if (!GetOverlappedResult(connection->handle, &overlapped, &transferred, FALSE)) {
            DWORD error = GetLastError();
            (void)CloseHandle(overlapped.hEvent);
            return sl_local_windows_error_status(
                error, out_diag, SL_DIAG_NET_LOCAL_IPC_READ_WRITE_CANCELLED,
                SL_STATUS_INVALID_STATE,
                sl_local_literal("local IPC read or write was cancelled or timed out"));
        }
    }
    else {
        DWORD error = GetLastError();
        if (error == ERROR_BROKEN_PIPE) {
            (void)CloseHandle(overlapped.hEvent);
            connection->state = SL_LOCAL_CONNECTION_CLOSED;
            return sl_local_fail(out_diag, SL_DIAG_NET_LOCAL_IPC_DISPOSED, SL_STATUS_INVALID_STATE,
                                 sl_local_literal("local IPC connection or server is disposed"));
        }
        if (error != ERROR_IO_PENDING) {
            (void)CloseHandle(overlapped.hEvent);
            return sl_local_windows_error_status(
                error, out_diag, SL_DIAG_NET_LOCAL_IPC_READ_WRITE_CANCELLED,
                SL_STATUS_INVALID_STATE,
                sl_local_literal("local IPC read or write was cancelled or timed out"));
        }
        status = sl_local_wait_overlapped(
            connection->handle, &overlapped, options != NULL && options->has_timeout_ms,
            options == NULL ? 0U : options->timeout_ms, &transferred, out_diag,
            SL_DIAG_NET_LOCAL_IPC_READ_WRITE_CANCELLED,
            sl_local_literal("local IPC read or write was cancelled or timed out"));
    }
    (void)CloseHandle(overlapped.hEvent);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (transferred == 0U) {
        connection->state = SL_LOCAL_CONNECTION_CLOSED;
        return sl_local_fail(out_diag, SL_DIAG_NET_LOCAL_IPC_DISPOSED, SL_STATUS_INVALID_STATE,
                             sl_local_literal("local IPC connection or server is disposed"));
    }
    return sl_bytes_copy_to_arena(
        arena, sl_bytes_from_parts(connection->read_buffer, (size_t)transferred), out);
}

SlStatus sl_local_connection_read(SlLocalConnection* connection, SlArena* arena, size_t max_bytes,
                                  SlOwnedBytes* out, SlDiag* out_diag)
{
    return sl_local_connection_read_ex(connection, arena, max_bytes, NULL, out, out_diag);
}

SlStatus sl_local_connection_read_until_ex(SlLocalConnection* connection, SlArena* arena,
                                           SlBytes delimiter, size_t max_bytes,
                                           const SlLocalIoOptions* options, SlOwnedBytes* out,
                                           SlDiag* out_diag)
{
    SlByteBuilder builder;
    SlStatus status;
    size_t limit = 0U;

    if (connection == NULL || arena == NULL || out == NULL || delimiter.ptr == NULL ||
        delimiter.length == 0U)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    limit = max_bytes == 0U ? connection->read_buffer_capacity : max_bytes;
    status = sl_byte_builder_init_arena(&builder, arena, 0U, limit);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    while (sl_byte_builder_length(&builder) < limit) {
        SlOwnedBytes chunk = {0};
        SlBytes view;
        status = sl_local_connection_read_ex(connection, arena, 1U, options, &chunk, out_diag);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_byte_builder_append_bytes(&builder, sl_owned_bytes_as_view(chunk));
        if (!sl_status_is_ok(status)) {
            return sl_local_fail(
                out_diag, SL_DIAG_NET_LOCAL_IPC_READ_WRITE_CANCELLED, SL_STATUS_CAPACITY_EXCEEDED,
                sl_local_literal("local IPC read or write was cancelled or timed out"));
        }
        view = sl_byte_builder_view(&builder);
        if (sl_bytes_ends_with(view, delimiter)) {
            return sl_bytes_copy_to_arena(arena, view, out);
        }
    }
    return sl_local_fail(out_diag, SL_DIAG_NET_LOCAL_IPC_READ_WRITE_CANCELLED,
                         SL_STATUS_CAPACITY_EXCEEDED,
                         sl_local_literal("local IPC read or write was cancelled or timed out"));
}

SlStatus sl_local_connection_read_until(SlLocalConnection* connection, SlArena* arena,
                                        SlBytes delimiter, size_t max_bytes, SlOwnedBytes* out,
                                        SlDiag* out_diag)
{
    return sl_local_connection_read_until_ex(connection, arena, delimiter, max_bytes, NULL, out,
                                             out_diag);
}

SlStatus sl_local_connection_read_line_ex(SlLocalConnection* connection, SlArena* arena,
                                          size_t max_bytes, const SlLocalIoOptions* options,
                                          SlOwnedStr* out, SlDiag* out_diag)
{
    static const unsigned char newline = '\n';
    SlOwnedBytes bytes = {0};
    SlStatus status;
    size_t length = 0U;

    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_local_connection_read_until_ex(connection, arena, sl_bytes_from_parts(&newline, 1U),
                                               max_bytes, options, &bytes, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    length = bytes.length;
    if (length != 0U && bytes.ptr[length - 1U] == '\n') {
        length -= 1U;
    }
    if (length != 0U && bytes.ptr[length - 1U] == '\r') {
        length -= 1U;
    }
    return sl_str_copy_to_arena(arena, sl_str_from_parts((const char*)bytes.ptr, length), out);
}

SlStatus sl_local_connection_read_line(SlLocalConnection* connection, SlArena* arena,
                                       size_t max_bytes, SlOwnedStr* out, SlDiag* out_diag)
{
    return sl_local_connection_read_line_ex(connection, arena, max_bytes, NULL, out, out_diag);
}

static SlStatus sl_local_connection_finish_close(SlLocalConnection* connection,
                                                 SlLocalConnectionState state)
{
    if (connection == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (connection->state == SL_LOCAL_CONNECTION_CLOSED ||
        connection->state == SL_LOCAL_CONNECTION_ABORTED)
    {
        return sl_status_ok();
    }
    connection->state = SL_LOCAL_CONNECTION_CLOSING;
    if (connection->handle != INVALID_HANDLE_VALUE) {
        (void)CancelIoEx(connection->handle, NULL);
        (void)CloseHandle(connection->handle);
        connection->handle = INVALID_HANDLE_VALUE;
    }
    connection->state = state;
    return sl_status_ok();
}

SlStatus sl_local_connection_close(SlLocalConnection* connection, SlDiag* out_diag)
{
    (void)out_diag;
    return sl_local_connection_finish_close(connection, SL_LOCAL_CONNECTION_CLOSED);
}

SlStatus sl_local_connection_abort(SlLocalConnection* connection, SlDiag* out_diag)
{
    (void)out_diag;
    return sl_local_connection_finish_close(connection, SL_LOCAL_CONNECTION_ABORTED);
}

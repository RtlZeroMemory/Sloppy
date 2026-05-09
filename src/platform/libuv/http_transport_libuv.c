/*
 * Libuv-backed HTTP transport listener foundation.
 *
 * This file owns uv loop, listener, and accepted TCP handles. Public Sloppy headers expose
 * only Sloppy-owned transport/backend state plus opaque platform pointers.
 */
#include "sloppy/http_transport.h"

#include "sloppy/builder.h"
#include "sloppy/checked_math.h"
#include "sloppy/container.h"
#include "sloppy/http2_dispatch.h"
#include "sloppy/http_response.h"

#include <limits.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <uv.h>

#define SL_HTTP2_PREFACE_BYTES 24U

static const unsigned char SL_HTTP2_PREFACE[SL_HTTP2_PREFACE_BYTES] = {
    'P', 'R', 'I',  ' ',  '*',  ' ',  'H', 'T', 'T',  'P',  '/',  '2',
    '.', '0', '\r', '\n', '\r', '\n', 'S', 'M', '\r', '\n', '\r', '\n'};
static const char SL_HTTP2_H2C_UPGRADE_STATUS[] = "HTTP/1.1 101 Switching Protocols\r\n"
                                                  "Connection: Upgrade\r\n"
                                                  "Upgrade: h2c\r\n"
                                                  "\r\n";
#define SL_HTTP2_H2C_UPGRADE_STATUS_BYTES (sizeof(SL_HTTP2_H2C_UPGRADE_STATUS) - 1U)
static const unsigned char SL_HTTP_TRANSPORT_ALPN_PROTOCOLS[] = {2U,  'h', '2', 8U,  'h', 't',
                                                                 't', 'p', '/', '1', '.', '1'};
static const unsigned char SL_HTTP_TRANSPORT_ALPN_HTTP11[] = {8U,  'h', 't', 't', 'p',
                                                              '/', '1', '.', '1'};

struct SlHttpPlatformConnection
{
    uv_tcp_t handle;
    uv_write_t write;
    uv_write_t tls_write;
    uv_timer_t header_timer;
    uv_timer_t body_timer;
    uv_timer_t request_timer;
    uv_timer_t write_timer;
    uv_timer_t idle_timer;
    SlHttpTransportConnection* owner;
    unsigned char* read_buffer;
    unsigned char* tls_plain_read_buffer;
    unsigned char* tls_handshake_write_buffer;
    unsigned char* tls_write_buffer;
    size_t read_buffer_size;
    size_t tls_plain_read_buffer_size;
    size_t tls_handshake_write_buffer_size;
    size_t tls_write_buffer_size;
    SSL* tls_ssl;
    bool initialized;
    bool header_timer_initialized;
    bool body_timer_initialized;
    bool request_timer_initialized;
    bool write_timer_initialized;
    bool idle_timer_initialized;
    bool closing;
    bool reading;
    bool writing;
    bool tls_enabled;
    bool tls_handshake_complete;
    bool tls_alpn_h2;
    bool tls_writing;
    bool tls_shutdown_writing;
};

struct SlHttpPlatformListener
{
    uv_loop_t loop;
    uv_tcp_t listener;
    uv_tcp_t overflow;
    SlHttpTransportServer* server;
    SSL_CTX* tls_context;
    SlOwnedStr tls_certificate_path;
    SlOwnedStr tls_private_key_path;
    SlOwnedStr tls_passphrase;
    bool loop_initialized;
    bool listener_initialized;
    bool overflow_initialized;
    bool closing;
    uint32_t bound_port;
};

static SlStr sl_http_transport_literal(const char* ptr, size_t length)
{
    return sl_str_from_parts(ptr, length);
}

static bool sl_http_transport_str_valid(SlStr str)
{
    return str.length == 0U || str.ptr != NULL;
}

static void sl_http_transport_clear_diag(SlDiag* out_diag)
{
    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
}

static SlStatus sl_http_transport_diag(SlDiag* out_diag, SlDiagCode code, SlStatusCode status_code,
                                       SlStr message, SlStr hint)
{
    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
        out_diag->severity = SL_DIAG_SEVERITY_ERROR;
        out_diag->code = code;
        out_diag->message = message;
        if (!sl_str_is_empty(hint)) {
            out_diag->hints[0] = hint;
            out_diag->hint_count = 1U;
        }
    }
    return sl_status_from_code(status_code);
}

static SlStatus sl_http_transport_invalid_config(SlDiag* out_diag, SlStr message)
{
    return sl_http_transport_diag(
        out_diag, SL_DIAG_HTTP_TRANSPORT_CONFIG, SL_STATUS_INVALID_ARGUMENT, message,
        sl_http_transport_literal("use a bounded localhost transport configuration",
                                  sizeof("use a bounded localhost transport configuration") - 1U));
}

static SlStatus sl_http_transport_invalid_state(SlDiag* out_diag)
{
    return sl_http_transport_diag(
        out_diag, SL_DIAG_APP_LIFECYCLE, SL_STATUS_INVALID_STATE,
        sl_http_transport_literal("HTTP transport server lifecycle state is invalid",
                                  sizeof("HTTP transport server lifecycle state is invalid") - 1U),
        sl_http_transport_literal("call init/listen/stop/dispose in order",
                                  sizeof("call init/listen/stop/dispose in order") - 1U));
}

static SlStatus sl_http_transport_uv_status(int rc, SlDiag* out_diag, SlDiagCode code,
                                            SlStr message)
{
    SlStatusCode status_code = SL_STATUS_INTERNAL;

    if (rc == 0) {
        return sl_status_ok();
    }
    if (rc == UV_EINVAL) {
        status_code = SL_STATUS_INVALID_ARGUMENT;
    }
    else if (rc == UV_ENOMEM) {
        status_code = SL_STATUS_OUT_OF_MEMORY;
    }
    else if (rc == UV_EADDRINUSE || rc == UV_EADDRNOTAVAIL || rc == UV_EACCES) {
        status_code = SL_STATUS_INVALID_STATE;
    }

    return sl_http_transport_diag(
        out_diag, code, status_code, message,
        sl_http_transport_literal("libuv details stay inside the platform transport boundary",
                                  sizeof("libuv details stay inside the platform transport "
                                         "boundary") -
                                      1U));
}

static SlHttpTransportConfig sl_http_transport_config_defaults(void)
{
    SlHttpTransportConfig config = {0};

    config.host = sl_str_from_cstr("127.0.0.1");
    config.port = SL_HTTP_TRANSPORT_DEFAULT_PORT;
    config.max_connections = SL_HTTP_BACKEND_DEFAULT_MAX_CONNECTIONS;
    config.max_active_requests = SL_HTTP_BACKEND_DEFAULT_MAX_ACTIVE_REQUESTS;
    config.parse.max_headers = SL_HTTP_DEFAULT_MAX_HEADERS;
    config.parse.max_request_line_length = SL_HTTP_DEFAULT_MAX_REQUEST_LINE_LENGTH;
    config.parse.max_target_length = SL_HTTP_DEFAULT_MAX_TARGET_LENGTH;
    config.parse.max_header_name_length = SL_HTTP_DEFAULT_MAX_HEADER_NAME_LENGTH;
    config.parse.max_header_value_length = SL_HTTP_DEFAULT_MAX_HEADER_VALUE_LENGTH;
    config.parse.max_total_header_bytes = SL_HTTP_DEFAULT_MAX_TOTAL_HEADER_BYTES;
    config.parse.max_body_length = SL_HTTP_DEFAULT_MAX_BODY_LENGTH;
    config.max_request_head_bytes = SL_HTTP_TRANSPORT_DEFAULT_MAX_REQUEST_HEAD_BYTES;
    config.request_arena_bytes = SL_HTTP_TRANSPORT_DEFAULT_REQUEST_ARENA_BYTES;
    config.read_chunk_bytes = SL_HTTP_TRANSPORT_DEFAULT_READ_CHUNK_BYTES;
    config.max_response_bytes = SL_HTTP_TRANSPORT_DEFAULT_RESPONSE_BYTES;
    config.max_pending_write_bytes = SL_HTTP_TRANSPORT_DEFAULT_MAX_PENDING_WRITE_BYTES;
    config.header_read_timeout_ms = SL_HTTP_TRANSPORT_DEFAULT_HEADER_READ_TIMEOUT_MS;
    config.body_read_timeout_ms = SL_HTTP_TRANSPORT_DEFAULT_BODY_READ_TIMEOUT_MS;
    config.request_timeout_ms = SL_HTTP_TRANSPORT_DEFAULT_REQUEST_TIMEOUT_MS;
    config.write_timeout_ms = SL_HTTP_TRANSPORT_DEFAULT_WRITE_TIMEOUT_MS;
    config.keep_alive_idle_timeout_ms = SL_HTTP_TRANSPORT_DEFAULT_KEEP_ALIVE_IDLE_TIMEOUT_MS;
    config.max_requests_per_connection = SL_HTTP_TRANSPORT_DEFAULT_MAX_REQUESTS_PER_CONNECTION;
    config.connection_capacity = SL_HTTP_BACKEND_DEFAULT_MAX_CONNECTIONS;
    config.backlog = SL_HTTP_TRANSPORT_DEFAULT_BACKLOG;
    return config;
}

static SlStatus sl_http_transport_normalize_config(const SlHttpTransportConfig* input,
                                                   SlHttpTransportConfig* out, SlDiag* out_diag)
{
    SlHttpTransportConfig config = sl_http_transport_config_defaults();

    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (input != NULL) {
        if (!sl_str_is_empty(input->host)) {
            config.host = input->host;
        }
        config.port = input->port;
        config.max_connections = input->max_connections;
        config.max_active_requests = input->max_active_requests;
        config.parse.max_headers =
            input->parse.max_headers == 0U ? config.parse.max_headers : input->parse.max_headers;
        config.parse.max_request_line_length = input->parse.max_request_line_length == 0U
                                                   ? config.parse.max_request_line_length
                                                   : input->parse.max_request_line_length;
        config.parse.max_target_length = input->parse.max_target_length == 0U
                                             ? config.parse.max_target_length
                                             : input->parse.max_target_length;
        config.parse.max_header_name_length = input->parse.max_header_name_length == 0U
                                                  ? config.parse.max_header_name_length
                                                  : input->parse.max_header_name_length;
        config.parse.max_header_value_length = input->parse.max_header_value_length == 0U
                                                   ? config.parse.max_header_value_length
                                                   : input->parse.max_header_value_length;
        config.parse.max_total_header_bytes = input->parse.max_total_header_bytes == 0U
                                                  ? config.parse.max_total_header_bytes
                                                  : input->parse.max_total_header_bytes;
        config.parse.max_body_length = input->parse.max_body_length == 0U
                                           ? config.parse.max_body_length
                                           : input->parse.max_body_length;
        config.max_request_head_bytes = input->max_request_head_bytes == 0U
                                            ? config.max_request_head_bytes
                                            : input->max_request_head_bytes;
        config.request_arena_bytes = input->request_arena_bytes == 0U ? config.request_arena_bytes
                                                                      : input->request_arena_bytes;
        config.read_chunk_bytes =
            input->read_chunk_bytes == 0U ? config.read_chunk_bytes : input->read_chunk_bytes;
        config.max_response_bytes =
            input->max_response_bytes == 0U ? config.max_response_bytes : input->max_response_bytes;
        config.max_pending_write_bytes = input->max_pending_write_bytes == 0U
                                             ? config.max_pending_write_bytes
                                             : input->max_pending_write_bytes;
        config.header_read_timeout_ms = input->header_read_timeout_ms == 0U
                                            ? config.header_read_timeout_ms
                                            : input->header_read_timeout_ms;
        config.body_read_timeout_ms = input->body_read_timeout_ms == 0U
                                          ? config.body_read_timeout_ms
                                          : input->body_read_timeout_ms;
        config.request_timeout_ms =
            input->request_timeout_ms == 0U ? config.request_timeout_ms : input->request_timeout_ms;
        config.write_timeout_ms =
            input->write_timeout_ms == 0U ? config.write_timeout_ms : input->write_timeout_ms;
        config.keep_alive_idle_timeout_ms = input->keep_alive_idle_timeout_ms == 0U
                                                ? config.keep_alive_idle_timeout_ms
                                                : input->keep_alive_idle_timeout_ms;
        config.max_requests_per_connection = input->max_requests_per_connection == 0U
                                                 ? config.max_requests_per_connection
                                                 : input->max_requests_per_connection;
        config.keep_alive_disabled = input->keep_alive_disabled;
        config.http2_prior_knowledge_only = input->http2_prior_knowledge_only;
        config.tls = input->tls;
        if (config.tls.enabled && config.tls.backend == SL_HTTP_TRANSPORT_TLS_BACKEND_NONE) {
            config.tls.backend = SL_HTTP_TRANSPORT_TLS_BACKEND_OPENSSL;
        }
        config.connection_capacity = input->connection_capacity;
        config.backlog = input->backlog;
        config.on_request_ready = input->on_request_ready;
        config.on_request_ready_user = input->on_request_ready_user;
        config.dispatch = input->dispatch;
        config.dispatch_user = input->dispatch_user;
    }

    if (!sl_http_transport_str_valid(config.host) || sl_str_is_empty(config.host)) {
        return sl_http_transport_invalid_config(
            out_diag, sl_http_transport_literal("HTTP transport host is invalid",
                                                sizeof("HTTP transport host is invalid") - 1U));
    }
    if (config.port > 65535U) {
        return sl_http_transport_invalid_config(
            out_diag, sl_http_transport_literal("HTTP transport port is invalid",
                                                sizeof("HTTP transport port is invalid") - 1U));
    }
    if (config.max_connections == 0U || config.max_active_requests == 0U ||
        config.connection_capacity == 0U || config.connection_capacity < config.max_connections)
    {
        return sl_http_transport_invalid_config(
            out_diag, sl_http_transport_literal(
                          "HTTP transport connection capacity is invalid",
                          sizeof("HTTP transport connection capacity is invalid") - 1U));
    }
    if (config.backlog <= 0) {
        return sl_http_transport_invalid_config(
            out_diag, sl_http_transport_literal("HTTP transport backlog is invalid",
                                                sizeof("HTTP transport backlog is invalid") - 1U));
    }
    if (config.max_request_head_bytes == 0U || config.request_arena_bytes == 0U ||
        config.read_chunk_bytes == 0U || config.max_response_bytes == 0U ||
        config.max_pending_write_bytes == 0U || config.keep_alive_idle_timeout_ms == 0U ||
        config.max_requests_per_connection == 0U)
    {
        return sl_http_transport_invalid_config(
            out_diag, sl_http_transport_literal(
                          "HTTP transport request buffer capacity is invalid",
                          sizeof("HTTP transport request buffer capacity is invalid") - 1U));
    }
    if (config.parse.max_body_length > config.request_arena_bytes) {
        return sl_http_transport_invalid_config(
            out_diag, sl_http_transport_literal(
                          "HTTP transport request body limit exceeds request arena",
                          sizeof("HTTP transport request body limit exceeds request arena") - 1U));
    }
    if (config.tls.enabled) {
        if (config.tls.backend != SL_HTTP_TRANSPORT_TLS_BACKEND_OPENSSL) {
            return sl_http_transport_diag(
                out_diag, SL_DIAG_HTTP_TLS_BACKEND_UNAVAILABLE, SL_STATUS_UNSUPPORTED,
                sl_http_transport_literal("HTTP TLS backend is unsupported",
                                          sizeof("HTTP TLS backend is unsupported") - 1U),
                sl_http_transport_literal("use the OpenSSL TLS backend for inbound HTTPS",
                                          sizeof("use the OpenSSL TLS backend for inbound HTTPS") -
                                              1U));
        }
        if (!sl_http_transport_str_valid(config.tls.certificate_path) ||
            sl_str_is_empty(config.tls.certificate_path))
        {
            return sl_http_transport_diag(
                out_diag, SL_DIAG_HTTP_TLS_CONFIG, SL_STATUS_INVALID_ARGUMENT,
                sl_http_transport_literal("HTTP TLS certificate path is required",
                                          sizeof("HTTP TLS certificate path is required") - 1U),
                sl_http_transport_literal("configure a non-secret certificate path",
                                          sizeof("configure a non-secret certificate path") - 1U));
        }
        if (!sl_http_transport_str_valid(config.tls.private_key_path) ||
            sl_str_is_empty(config.tls.private_key_path))
        {
            return sl_http_transport_diag(
                out_diag, SL_DIAG_HTTP_TLS_CONFIG, SL_STATUS_INVALID_ARGUMENT,
                sl_http_transport_literal("HTTP TLS private key path is required",
                                          sizeof("HTTP TLS private key path is required") - 1U),
                sl_http_transport_literal("configure a private key path; key material is redacted",
                                          sizeof("configure a private key path; key material is "
                                                 "redacted") -
                                              1U));
        }
        if (!sl_http_transport_str_valid(config.tls.passphrase)) {
            return sl_http_transport_diag(
                out_diag, SL_DIAG_HTTP_TLS_CONFIG, SL_STATUS_INVALID_ARGUMENT,
                sl_http_transport_literal("HTTP TLS passphrase value is invalid",
                                          sizeof("HTTP TLS passphrase value is invalid") - 1U),
                sl_http_transport_literal("TLS passphrases are accepted only as bounded strings",
                                          sizeof("TLS passphrases are accepted only as bounded "
                                                 "strings") -
                                              1U));
        }
    }

    *out = config;
    return sl_status_ok();
}

static SlStatus sl_http_transport_alloc(SlArena* arena, size_t size, size_t align, void** out)
{
    if (arena == NULL || out == NULL || size == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_arena_alloc(arena, size, align, out);
}

static void sl_http_transport_connection_close_cb(uv_handle_t* handle)
{
    SlHttpPlatformConnection* platform = (SlHttpPlatformConnection*)handle->data;

    if (platform == NULL) {
        return;
    }
    if (platform->tls_ssl != NULL) {
        SSL_free(platform->tls_ssl);
        platform->tls_ssl = NULL;
    }
    platform->tls_enabled = false;
    platform->tls_handshake_complete = false;
    platform->tls_alpn_h2 = false;
    platform->tls_writing = false;
    platform->tls_shutdown_writing = false;
    platform->closing = false;
    platform->initialized = false;
    if (platform->owner != NULL) {
        platform->owner->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_CLOSED;
    }
}

static uint16_t sl_http_transport_status_for_failure(SlStatus status, const SlDiag* diag)
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

static SlStr sl_http_transport_body_for_status(uint16_t status)
{
    switch (status) {
    case 400U:
        return sl_http_transport_literal("Malformed HTTP request\n",
                                         sizeof("Malformed HTTP request\n") - 1U);
    case 404U:
        return sl_http_transport_literal("Not Found\n", sizeof("Not Found\n") - 1U);
    case 405U:
        return sl_http_transport_literal("Method Not Allowed\n",
                                         sizeof("Method Not Allowed\n") - 1U);
    case 408U:
        return sl_http_transport_literal("Request Timeout\n", sizeof("Request Timeout\n") - 1U);
    case 413U:
        return sl_http_transport_literal("Payload Too Large\n", sizeof("Payload Too Large\n") - 1U);
    case 415U:
        return sl_http_transport_literal("Unsupported Media Type\n",
                                         sizeof("Unsupported Media Type\n") - 1U);
    case 417U:
        return sl_http_transport_literal("Expectation Failed\n",
                                         sizeof("Expectation Failed\n") - 1U);
    case 501U:
        return sl_http_transport_literal("Request body framing is not supported\n",
                                         sizeof("Request body framing is not supported\n") - 1U);
    default:
        return sl_http_transport_literal("Sloppy handler failed\n",
                                         sizeof("Sloppy handler failed\n") - 1U);
    }
}

static SlStr sl_http_transport_body_for_failure(uint16_t status, const SlDiag* diag)
{
    if (diag != NULL && diag->code == SL_DIAG_MALFORMED_JSON) {
        return sl_http_transport_literal("Malformed JSON\n", sizeof("Malformed JSON\n") - 1U);
    }

    return sl_http_transport_body_for_status(status);
}

static void sl_http_transport_store_diag(SlHttpTransportConnection* connection, const SlDiag* diag)
{
    if (connection != NULL && diag != NULL) {
        connection->last_diag = *diag;
    }
}

static SlStatus sl_http_transport_connection_diag(SlHttpTransportConnection* connection,
                                                  SlDiag* out_diag, SlDiagCode code,
                                                  SlStatusCode status_code, SlStr message,
                                                  SlStr hint)
{
    SlDiag diag = {0};
    SlStatus status = sl_http_transport_diag(&diag, code, status_code, message, hint);

    sl_http_transport_store_diag(connection, &diag);
    if (out_diag != NULL) {
        *out_diag = diag;
    }
    return status;
}

static SlStr sl_http_transport_trim_ascii_space(SlStr value)
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

static bool sl_http_transport_header_value(const SlHttpRequestHead* head, SlStr name,
                                           SlStr* out_value)
{
    size_t index = 0U;

    if (out_value != NULL) {
        *out_value = sl_str_empty();
    }
    if (head == NULL || out_value == NULL || (head->header_count != 0U && head->headers == NULL)) {
        return false;
    }
    for (index = 0U; index < head->header_count; index += 1U) {
        if (sl_str_equal_ci_ascii(head->headers[index].name, name)) {
            *out_value = head->headers[index].value;
            return true;
        }
    }
    return false;
}

static bool sl_http_transport_request_header_value_count(const SlHttpRequestHead* head, SlStr name,
                                                         SlStr* out_value, size_t* out_count)
{
    size_t count = 0U;

    if (out_value != NULL) {
        *out_value = sl_str_empty();
    }
    if (out_count != NULL) {
        *out_count = 0U;
    }
    if (head == NULL || (head->header_count != 0U && head->headers == NULL)) {
        return false;
    }
    for (size_t index = 0U; index < head->header_count; index += 1U) {
        if (sl_str_equal_ci_ascii(head->headers[index].name, name)) {
            if (out_value != NULL && count == 0U) {
                *out_value = head->headers[index].value;
            }
            count += 1U;
        }
    }
    if (out_count != NULL) {
        *out_count = count;
    }
    return count != 0U;
}

static bool sl_http_transport_connection_header_has_token(SlStr value, SlStr token);

static bool sl_http_transport_request_header_has_token(const SlHttpRequestHead* head, SlStr name,
                                                       SlStr token, size_t* out_count)
{
    size_t count = 0U;
    bool found = false;

    if (out_count != NULL) {
        *out_count = 0U;
    }
    if (head == NULL || (head->header_count != 0U && head->headers == NULL)) {
        return false;
    }
    for (size_t index = 0U; index < head->header_count; index += 1U) {
        if (sl_str_equal_ci_ascii(head->headers[index].name, name)) {
            count += 1U;
            if (sl_http_transport_connection_header_has_token(head->headers[index].value, token)) {
                found = true;
            }
        }
    }
    if (out_count != NULL) {
        *out_count = count;
    }
    return found;
}

static bool sl_http_transport_response_header_name_char_valid(char ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
           ch == '!' || ch == '#' || ch == '$' || ch == '%' || ch == '&' || ch == '\'' ||
           ch == '*' || ch == '+' || ch == '-' || ch == '.' || ch == '^' || ch == '_' ||
           ch == '`' || ch == '|' || ch == '~';
}

static bool sl_http_transport_response_header_name_valid(SlStr name)
{
    size_t index = 0U;

    if (name.ptr == NULL || name.length == 0U) {
        return false;
    }
    for (index = 0U; index < name.length; index += 1U) {
        if (!sl_http_transport_response_header_name_char_valid(name.ptr[index])) {
            return false;
        }
    }
    return true;
}

static bool sl_http_transport_response_header_value_valid(SlStr value)
{
    size_t index = 0U;

    if (value.ptr == NULL && value.length != 0U) {
        return false;
    }
    for (index = 0U; index < value.length; index += 1U) {
        unsigned char ch = (unsigned char)value.ptr[index];
        if ((ch < 0x20U && ch != '\t') || ch == 0x7FU) {
            return false;
        }
    }
    return true;
}

static bool sl_http_transport_response_header_managed(SlStr name)
{
    return sl_str_equal_ci_ascii(name, sl_str_from_cstr("Connection")) ||
           sl_str_equal_ci_ascii(name, sl_str_from_cstr("Keep-Alive")) ||
           sl_str_equal_ci_ascii(name, sl_str_from_cstr("Content-Type")) ||
           sl_str_equal_ci_ascii(name, sl_str_from_cstr("Transfer-Encoding")) ||
           sl_str_equal_ci_ascii(name, sl_str_from_cstr("Content-Length"));
}

static SlHttpTransportServer*
sl_http_transport_connection_server(const SlHttpTransportConnection* connection);
static SlStatus sl_http_transport_start_timer(SlHttpPlatformConnection* platform, uv_timer_t* timer,
                                              bool* initialized, uint64_t timeout_ms,
                                              uv_timer_cb callback);
static void sl_http_transport_stop_timer(uv_timer_t* timer, bool* initialized);
static void sl_http_transport_write_timeout_cb(uv_timer_t* timer);
static void sl_http_transport_idle_timeout_cb(uv_timer_t* timer);
static void sl_http_transport_write_cb(uv_write_t* request, int status);
static void sl_http_transport_tls_write_cb(uv_write_t* request, int status);
static void sl_http_transport_tls_shutdown_write_cb(uv_write_t* request, int status);
static SlStatus sl_http_transport_restart_keep_alive_read(SlHttpTransportConnection* connection,
                                                          SlDiag* out_diag);
static void sl_http_transport_alloc_read(uv_handle_t* handle, size_t suggested_size,
                                         uv_buf_t* out_buffer);
static void sl_http_transport_on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buffer);
static SlStatus sl_http_transport_tls_drain_handshake(SlHttpTransportConnection* connection,
                                                      SlDiag* out_diag);
static SlStatus sl_http_transport_http2_flush_output(SlHttpTransportConnection* connection,
                                                     SlDiag* out_diag);
static SlStatus sl_http_transport_http2_receive(SlHttpTransportConnection* connection,
                                                SlBytes bytes, SlDiag* out_diag);
static void sl_http_transport_fail_and_close(SlHttpTransportConnection* connection, SlDiag* diag);

static bool sl_http_transport_request_terminal(SlHttpRequestState state)
{
    return state == SL_HTTP_REQUEST_STATE_CLOSED || state == SL_HTTP_REQUEST_STATE_COMPLETED ||
           state == SL_HTTP_REQUEST_STATE_CANCELLED || state == SL_HTTP_REQUEST_STATE_TIMED_OUT ||
           state == SL_HTTP_REQUEST_STATE_FAILED;
}

static bool sl_http_transport_connection_header_has_token(SlStr value, SlStr token)
{
    size_t cursor = 0U;

    if ((value.ptr == NULL && value.length != 0U) || token.ptr == NULL || token.length == 0U) {
        return false;
    }
    while (cursor <= value.length) {
        size_t start = cursor;
        size_t end = cursor;
        while (end < value.length && value.ptr[end] != ',') {
            end += 1U;
        }
        while (start < end && (value.ptr[start] == ' ' || value.ptr[start] == '\t')) {
            start += 1U;
        }
        while (end > start && (value.ptr[end - 1U] == ' ' || value.ptr[end - 1U] == '\t')) {
            end -= 1U;
        }
        if (sl_str_equal_ci_ascii(sl_str_from_parts(value.ptr + start, end - start), token)) {
            return true;
        }
        if (end == value.length) {
            break;
        }
        cursor = end + 1U;
    }
    return false;
}

static int sl_http_transport_tls_passphrase_cb(char* buffer, int size, int rwflag, void* user)
{
    SlHttpTransportServer* server = (SlHttpTransportServer*)user;
    SlStr passphrase = {0};
    size_t index = 0U;

    (void)rwflag;
    if (buffer == NULL || size <= 0 || server == NULL) {
        return 0;
    }
    if (server->platform == NULL) {
        return 0;
    }
    passphrase = sl_owned_str_as_view(server->platform->tls_passphrase);
    if (sl_str_is_empty(passphrase)) {
        return 0;
    }
    if (passphrase.length >= (size_t)size) {
        return 0;
    }
    for (index = 0U; index < passphrase.length; index += 1U) {
        buffer[index] = passphrase.ptr[index];
    }
    buffer[passphrase.length] = '\0';
    return (int)passphrase.length;
}

static int sl_http_transport_tls_alpn_select_cb(SSL* ssl, const unsigned char** out,
                                                unsigned char* outlen, const unsigned char* in,
                                                unsigned int inlen, void* user)
{
    SlHttpTransportServer* server = (SlHttpTransportServer*)user;
    const unsigned char* protocols = SL_HTTP_TRANSPORT_ALPN_HTTP11;
    unsigned int protocol_length = (unsigned int)sizeof(SL_HTTP_TRANSPORT_ALPN_HTTP11);
    unsigned char* selected = NULL;
    unsigned char selected_length = 0U;

    (void)ssl;
    if (out == NULL || outlen == NULL || in == NULL || inlen == 0U) {
        return SSL_TLSEXT_ERR_NOACK;
    }
    if (server != NULL && server->config.dispatch != NULL) {
        protocols = SL_HTTP_TRANSPORT_ALPN_PROTOCOLS;
        protocol_length = (unsigned int)sizeof(SL_HTTP_TRANSPORT_ALPN_PROTOCOLS);
    }
    if (SSL_select_next_proto(&selected, &selected_length, protocols, protocol_length, in, inlen) !=
        OPENSSL_NPN_NEGOTIATED)
    {
        return SSL_TLSEXT_ERR_NOACK;
    }

    *out = selected;
    *outlen = selected_length;
    return SSL_TLSEXT_ERR_OK;
}

static void sl_http_transport_clear_tls_passphrase(SlHttpTransportServer* server)
{
    if (server == NULL || server->platform == NULL || server->platform->tls_passphrase.ptr == NULL)
    {
        return;
    }
    OPENSSL_cleanse(server->platform->tls_passphrase.ptr, server->platform->tls_passphrase.length);
    server->platform->tls_passphrase = (SlOwnedStr){0};
}

static void sl_http_transport_release_tls_context(SlHttpPlatformListener* platform)
{
    if (platform == NULL || platform->tls_context == NULL) {
        return;
    }
    SSL_CTX_free(platform->tls_context);
    platform->tls_context = NULL;
}

static SlStatus sl_http_transport_tls_context_init(SlHttpTransportServer* server, SlDiag* out_diag)
{
    SSL_CTX* context = NULL;

    if (server == NULL || server->platform == NULL || !server->config.tls.enabled) {
        return sl_status_ok();
    }
    if (server->platform->tls_context != NULL) {
        return sl_status_ok();
    }

    context = SSL_CTX_new(TLS_server_method());
    if (context == NULL) {
        return sl_http_transport_diag(
            out_diag, SL_DIAG_HTTP_TLS_BACKEND_UNAVAILABLE, SL_STATUS_UNSUPPORTED,
            sl_http_transport_literal("HTTP TLS backend initialization failed",
                                      sizeof("HTTP TLS backend initialization failed") - 1U),
            sl_http_transport_literal("OpenSSL details stay inside the platform boundary",
                                      sizeof("OpenSSL details stay inside the platform boundary") -
                                          1U));
    }

    SSL_CTX_set_min_proto_version(context, TLS1_2_VERSION);
    SSL_CTX_set_default_passwd_cb(context, sl_http_transport_tls_passphrase_cb);
    SSL_CTX_set_default_passwd_cb_userdata(context, server);
    SSL_CTX_set_alpn_select_cb(context, sl_http_transport_tls_alpn_select_cb, server);
    if (SSL_CTX_use_certificate_chain_file(
            context, sl_owned_str_as_view(server->platform->tls_certificate_path).ptr) != 1)
    {
        sl_http_transport_clear_tls_passphrase(server);
        SSL_CTX_free(context);
        return sl_http_transport_diag(
            out_diag, SL_DIAG_HTTP_TLS_CONFIG, SL_STATUS_INVALID_ARGUMENT,
            sl_http_transport_literal("HTTP TLS certificate could not be loaded",
                                      sizeof("HTTP TLS certificate could not be loaded") - 1U),
            sl_http_transport_literal("certificate diagnostics redact file contents and secrets",
                                      sizeof("certificate diagnostics redact file contents and "
                                             "secrets") -
                                          1U));
    }
    if (SSL_CTX_use_PrivateKey_file(
            context, sl_owned_str_as_view(server->platform->tls_private_key_path).ptr,
            SSL_FILETYPE_PEM) != 1)
    {
        sl_http_transport_clear_tls_passphrase(server);
        SSL_CTX_free(context);
        return sl_http_transport_diag(
            out_diag, SL_DIAG_HTTP_TLS_CONFIG, SL_STATUS_INVALID_ARGUMENT,
            sl_http_transport_literal("HTTP TLS private key could not be loaded",
                                      sizeof("HTTP TLS private key could not be loaded") - 1U),
            sl_http_transport_literal("private key material and passphrases are redacted",
                                      sizeof("private key material and passphrases are redacted") -
                                          1U));
    }
    sl_http_transport_clear_tls_passphrase(server);
    if (SSL_CTX_check_private_key(context) != 1) {
        SSL_CTX_free(context);
        return sl_http_transport_diag(
            out_diag, SL_DIAG_HTTP_TLS_CONFIG, SL_STATUS_INVALID_ARGUMENT,
            sl_http_transport_literal("HTTP TLS certificate and private key do not match",
                                      sizeof("HTTP TLS certificate and private key do not match") -
                                          1U),
            sl_http_transport_literal("key material is not included in diagnostics",
                                      sizeof("key material is not included in diagnostics") - 1U));
    }

    server->platform->tls_context = context;
    return sl_status_ok();
}

static SlStatus sl_http_transport_tls_attach(SlHttpTransportConnection* connection,
                                             SlDiag* out_diag)
{
    SlHttpTransportServer* server = sl_http_transport_connection_server(connection);
    BIO* read_bio = NULL;
    BIO* write_bio = NULL;

    if (connection == NULL || connection->platform == NULL || server == NULL ||
        !server->config.tls.enabled)
    {
        return sl_status_ok();
    }
    if (server->platform == NULL || server->platform->tls_context == NULL) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_TLS_BACKEND_UNAVAILABLE, SL_STATUS_UNSUPPORTED,
            sl_http_transport_literal("HTTP TLS backend is unavailable",
                                      sizeof("HTTP TLS backend is unavailable") - 1U),
            sl_http_transport_literal("configure a supported TLS backend before listen",
                                      sizeof("configure a supported TLS backend before listen") -
                                          1U));
    }

    connection->platform->tls_ssl = SSL_new(server->platform->tls_context);
    read_bio = BIO_new(BIO_s_mem());
    write_bio = BIO_new(BIO_s_mem());
    if (connection->platform->tls_ssl == NULL || read_bio == NULL || write_bio == NULL) {
        if (read_bio != NULL) {
            BIO_free(read_bio);
        }
        if (write_bio != NULL) {
            BIO_free(write_bio);
        }
        if (connection->platform->tls_ssl != NULL) {
            SSL_free(connection->platform->tls_ssl);
            connection->platform->tls_ssl = NULL;
        }
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_TLS_BACKEND_UNAVAILABLE, SL_STATUS_OUT_OF_MEMORY,
            sl_http_transport_literal("HTTP TLS connection state allocation failed",
                                      sizeof("HTTP TLS connection state allocation failed") - 1U),
            sl_http_transport_literal("OpenSSL handles stay inside the platform transport",
                                      sizeof("OpenSSL handles stay inside the platform transport") -
                                          1U));
    }

    BIO_set_mem_eof_return(read_bio, -1);
    BIO_set_mem_eof_return(write_bio, -1);
    SSL_set_bio(connection->platform->tls_ssl, read_bio, write_bio);
    SSL_set_accept_state(connection->platform->tls_ssl);
    connection->platform->tls_enabled = true;
    connection->platform->tls_handshake_complete = false;
    return sl_status_ok();
}

static SlStatus sl_http_transport_tls_drain_handshake(SlHttpTransportConnection* connection,
                                                      SlDiag* out_diag)
{
    SlHttpPlatformConnection* platform = connection == NULL ? NULL : connection->platform;
    BIO* write_bio = NULL;
    int pending = 0;
    int read_count = 0;
    uv_buf_t buffer;

    if (platform == NULL || platform->tls_ssl == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (platform->tls_writing) {
        return sl_status_ok();
    }
    write_bio = SSL_get_wbio(platform->tls_ssl);
    pending = write_bio == NULL ? 0 : BIO_pending(write_bio);
    if (pending <= 0) {
        return sl_status_ok();
    }
    if ((size_t)pending > platform->tls_handshake_write_buffer_size) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_TLS_HANDSHAKE_FAILED, SL_STATUS_CAPACITY_EXCEEDED,
            sl_http_transport_literal("HTTP TLS handshake bytes exceed transport buffer",
                                      sizeof("HTTP TLS handshake bytes exceed transport buffer") -
                                          1U),
            sl_http_transport_literal("TLS handshake output must fit configured response caps",
                                      sizeof("TLS handshake output must fit configured response "
                                             "caps") -
                                          1U));
    }
    read_count = BIO_read(write_bio, platform->tls_handshake_write_buffer, pending);
    if (read_count <= 0) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_TLS_HANDSHAKE_FAILED, SL_STATUS_INTERNAL,
            sl_http_transport_literal("HTTP TLS handshake output failed",
                                      sizeof("HTTP TLS handshake output failed") - 1U),
            sl_http_transport_literal("OpenSSL details stay inside the platform boundary",
                                      sizeof("OpenSSL details stay inside the platform boundary") -
                                          1U));
    }
    buffer = uv_buf_init((char*)platform->tls_handshake_write_buffer, (unsigned int)read_count);
    platform->tls_write.data = platform;
    platform->tls_writing = true;
    if (uv_write(&platform->tls_write, (uv_stream_t*)&platform->handle, &buffer, 1U,
                 sl_http_transport_tls_write_cb) != 0)
    {
        platform->tls_writing = false;
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_TLS_HANDSHAKE_FAILED, SL_STATUS_INTERNAL,
            sl_http_transport_literal("HTTP TLS handshake write failed",
                                      sizeof("HTTP TLS handshake write failed") - 1U),
            sl_http_transport_literal("socket and OpenSSL handles stay inside the platform "
                                      "boundary",
                                      sizeof("socket and OpenSSL handles stay inside the platform "
                                             "boundary") -
                                          1U));
    }
    return sl_status_ok();
}

static bool sl_http_transport_client_requested_close(const SlHttpRequestHead* head)
{
    SlStr value = {0};

    if (!sl_http_transport_header_value(head, sl_str_from_cstr("Connection"), &value)) {
        return false;
    }
    return sl_http_transport_connection_header_has_token(value, sl_str_from_cstr("close"));
}

static bool sl_http_transport_request_keep_alive_eligible(SlHttpTransportConnection* connection,
                                                          const SlHttpResponse* response)
{
    SlHttpTransportServer* server = sl_http_transport_connection_server(connection);

    if (connection == NULL || response == NULL || server == NULL ||
        server->config.keep_alive_disabled)
    {
        return false;
    }
    if (server->state != SL_HTTP_TRANSPORT_SERVER_STATE_LISTENING ||
        server->backend.state != SL_HTTP_BACKEND_STATE_STARTED)
    {
        server->server_forced_closes += 1U;
        return false;
    }
    if (response->status >= 400U) {
        server->server_forced_closes += 1U;
        return false;
    }
    if (connection->request.head.version_major != 1U ||
        connection->request.head.version_minor != 1U)
    {
        server->server_forced_closes += 1U;
        return false;
    }
    if (sl_http_transport_client_requested_close(&connection->request.head)) {
        server->client_close_requests += 1U;
        return false;
    }
    if (connection->core.request_count >= server->config.max_requests_per_connection) {
        server->max_requests_reached += 1U;
        return false;
    }
    return true;
}

static void sl_http_transport_reset_request_state(SlHttpTransportConnection* connection)
{
    if (connection == NULL) {
        return;
    }
    if (connection->body_reader_started && !connection->body_reader_finished) {
        (void)sl_http_request_body_reader_close(&connection->body_reader, NULL);
    }
    if (connection->request_started) {
        (void)sl_http_request_close(&connection->request, NULL);
    }
    sl_arena_reset(&connection->request_arena);
    (void)sl_byte_builder_init_fixed(&connection->accumulation_builder, connection->accumulation,
                                     connection->accumulation_capacity);
    connection->request = (SlHttpRequestLifecycle){0};
    connection->body_reader = (SlHttpBodyReader){0};
    connection->accumulation_length = 0U;
    connection->response_length = 0U;
    connection->head_length = 0U;
    connection->expected_body_length = 0U;
    connection->request_started = false;
    connection->body_reader_started = false;
    connection->body_reader_finished = false;
    connection->write_started = false;
    connection->write_completed = false;
    connection->close_after_write = false;
    connection->keep_alive_after_write = false;
    connection->streaming_response = false;
    connection->active_response = (SlHttpResponse){0};
    connection->stream_chunk_index = 0U;
    connection->stream_final_written = false;
}

static SlStatus sl_http_transport_builder_append_cstr(SlByteBuilder* builder, const char* text)
{
    if (text == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_byte_builder_append_bytes(
        builder, sl_bytes_from_parts((const unsigned char*)text, sl_str_from_cstr(text).length));
}

static SlStatus sl_http_transport_builder_append_str(SlByteBuilder* builder, SlStr text)
{
    return sl_byte_builder_append_bytes(
        builder, sl_bytes_from_parts((const unsigned char*)text.ptr, text.length));
}

static SlStatus sl_http_transport_builder_append_size_decimal(SlByteBuilder* builder, size_t value)
{
    char digits[32];
    size_t count = 0U;
    SlStatus status;

    do {
        digits[count] = (char)('0' + (value % 10U));
        value /= 10U;
        count += 1U;
    } while (value != 0U && count < sizeof(digits));

    while (count > 0U) {
        count -= 1U;
        status = sl_byte_builder_append_byte(builder, (unsigned char)digits[count]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    return sl_status_ok();
}

static SlStatus sl_http_transport_builder_append_size_hex(SlByteBuilder* builder, size_t value)
{
    static const char hex[] = "0123456789abcdef";
    char digits[32];
    size_t count = 0U;
    SlStatus status;

    do {
        digits[count] = hex[value & 0xFU];
        value >>= 4U;
        count += 1U;
    } while (value != 0U && count < sizeof(digits));

    while (count > 0U) {
        count -= 1U;
        status = sl_byte_builder_append_byte(builder, (unsigned char)digits[count]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    return sl_status_ok();
}

static const char* sl_http_transport_reason(uint16_t status)
{
    switch (status) {
    case 200U:
        return "OK";
    case 201U:
        return "Created";
    case 202U:
        return "Accepted";
    case 204U:
        return "No Content";
    case 304U:
        return "Not Modified";
    case 400U:
        return "Bad Request";
    case 404U:
        return "Not Found";
    case 405U:
        return "Method Not Allowed";
    case 408U:
        return "Request Timeout";
    case 413U:
        return "Payload Too Large";
    case 415U:
        return "Unsupported Media Type";
    case 417U:
        return "Expectation Failed";
    case 500U:
        return "Internal Server Error";
    case 501U:
        return "Not Implemented";
    default:
        return NULL;
    }
}

static SlStatus sl_http_transport_start_write_bytes(SlHttpTransportConnection* connection,
                                                    SlBytes bytes, SlDiag* out_diag)
{
    uv_buf_t buffer;
    SlStatus status;
    int rc = 0;

    if (connection == NULL || (bytes.ptr == NULL && bytes.length != 0U)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (bytes.length > UINT_MAX) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_RESPONSE_SERIALIZATION_FAILED,
            SL_STATUS_CAPACITY_EXCEEDED,
            sl_http_transport_literal("HTTP response serialization failed",
                                      sizeof("HTTP response serialization failed") - 1U),
            sl_http_transport_literal("response bytes exceed the platform write boundary",
                                      sizeof("response bytes exceed the platform write boundary") -
                                          1U));
    }
    if (connection->platform == NULL || !connection->platform->initialized ||
        connection->platform->closing || connection->platform->writing)
    {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_WRITE_FAILED, SL_STATUS_INVALID_STATE,
            sl_http_transport_literal("HTTP transport response write cannot start",
                                      sizeof("HTTP transport response write cannot start") - 1U),
            sl_http_transport_literal("the connection is already closing or writing",
                                      sizeof("the connection is already closing or writing") - 1U));
    }

    if (connection->platform->tls_enabled) {
        BIO* write_bio = NULL;
        int pending = 0;
        int read_count = 0;
        int write_count = 0;

        if (!connection->platform->tls_handshake_complete ||
            connection->platform->tls_ssl == NULL || bytes.length > (size_t)INT_MAX)
        {
            return sl_http_transport_connection_diag(
                connection, out_diag, SL_DIAG_HTTP_WRITE_FAILED, SL_STATUS_INVALID_STATE,
                sl_http_transport_literal("HTTP TLS response write cannot start",
                                          sizeof("HTTP TLS response write cannot start") - 1U),
                sl_http_transport_literal("TLS handshake must complete before HTTP response "
                                          "bytes are written",
                                          sizeof("TLS handshake must complete before HTTP "
                                                 "response bytes are written") -
                                              1U));
        }
        ERR_clear_error();
        write_count = SSL_write(connection->platform->tls_ssl, bytes.ptr, (int)bytes.length);
        if (write_count <= 0 || (size_t)write_count != bytes.length) {
            return sl_http_transport_connection_diag(
                connection, out_diag, SL_DIAG_HTTP_WRITE_FAILED, SL_STATUS_INTERNAL,
                sl_http_transport_literal("HTTP TLS response encryption failed",
                                          sizeof("HTTP TLS response encryption failed") - 1U),
                sl_http_transport_literal("OpenSSL details stay inside the platform boundary",
                                          sizeof("OpenSSL details stay inside the platform "
                                                 "boundary") -
                                              1U));
        }
        write_bio = SSL_get_wbio(connection->platform->tls_ssl);
        pending = write_bio == NULL ? 0 : BIO_pending(write_bio);
        if (pending <= 0 || (size_t)pending > connection->platform->tls_write_buffer_size) {
            return sl_http_transport_connection_diag(
                connection, out_diag, SL_DIAG_HTTP_WRITE_FAILED, SL_STATUS_CAPACITY_EXCEEDED,
                sl_http_transport_literal("HTTP TLS response bytes exceed transport buffer",
                                          sizeof("HTTP TLS response bytes exceed transport "
                                                 "buffer") -
                                              1U),
                sl_http_transport_literal("encrypted response bytes must fit configured caps",
                                          sizeof("encrypted response bytes must fit configured "
                                                 "caps") -
                                              1U));
        }
        read_count = BIO_read(write_bio, connection->platform->tls_write_buffer, pending);
        if (read_count <= 0) {
            return sl_http_transport_connection_diag(
                connection, out_diag, SL_DIAG_HTTP_WRITE_FAILED, SL_STATUS_INTERNAL,
                sl_http_transport_literal("HTTP TLS response output failed",
                                          sizeof("HTTP TLS response output failed") - 1U),
                sl_http_transport_literal("OpenSSL details stay inside the platform boundary",
                                          sizeof("OpenSSL details stay inside the platform "
                                                 "boundary") -
                                              1U));
        }
        buffer =
            uv_buf_init((char*)connection->platform->tls_write_buffer, (unsigned int)read_count);
    }
    else {
        buffer = uv_buf_init((char*)bytes.ptr, (unsigned int)bytes.length);
    }
    connection->platform->write.data = connection->platform;
    {
        SlHttpTransportServer* server = sl_http_transport_connection_server(connection);
        status =
            sl_http_transport_start_timer(connection->platform, &connection->platform->write_timer,
                                          &connection->platform->write_timer_initialized,
                                          server == NULL ? 0U : server->config.write_timeout_ms,
                                          sl_http_transport_write_timeout_cb);
    }
    if (!sl_status_is_ok(status)) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_WRITE_FAILED, sl_status_code(status),
            sl_http_transport_literal("HTTP transport write timeout could not start",
                                      sizeof("HTTP transport write timeout could not start") - 1U),
            sl_http_transport_literal("the response write was not queued",
                                      sizeof("the response write was not queued") - 1U));
    }
    connection->platform->writing = true;
    connection->write_started = true;
    rc = uv_write(&connection->platform->write, (uv_stream_t*)&connection->platform->handle,
                  &buffer, 1U, sl_http_transport_write_cb);
    if (rc != 0) {
        connection->platform->writing = false;
        sl_http_transport_stop_timer(&connection->platform->write_timer,
                                     &connection->platform->write_timer_initialized);
        return sl_http_transport_uv_status(
            rc, out_diag, SL_DIAG_HTTP_WRITE_FAILED,
            sl_http_transport_literal("HTTP transport response write failed to start",
                                      sizeof("HTTP transport response write failed to start") -
                                          1U));
    }
    return sl_status_ok();
}

static SlStatus sl_http_transport_http2_dispatch_adapter(SlHttpConnection* core, SlArena* arena,
                                                         const SlHttpRequestLifecycle* request,
                                                         SlHttpResponse* out_response,
                                                         SlDiag* out_diag, void* user)
{
    SlHttpTransportConnection* connection = (SlHttpTransportConnection*)user;
    SlHttpTransportServer* server = sl_http_transport_connection_server(connection);

    (void)core;
    if (connection == NULL || server == NULL || server->config.dispatch == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return server->config.dispatch(connection, arena, request, out_response, out_diag,
                                   server->config.dispatch_user);
}

static SlStatus sl_http_transport_http2_flush_output(SlHttpTransportConnection* connection,
                                                     SlDiag* out_diag)
{
    SlBytes bytes = {0};
    SlStatus status;

    if (connection == NULL || !connection->http2_dispatcher_started) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (connection->platform != NULL && connection->platform->writing) {
        return sl_status_ok();
    }

    status = sl_http2_server_dispatcher_drain_output(connection->http2_dispatcher, &bytes);
    if (!sl_status_is_ok(status)) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_RESPONSE_SERIALIZATION_FAILED,
            sl_status_code(status),
            sl_http_transport_literal("HTTP/2 response serialization failed",
                                      sizeof("HTTP/2 response serialization failed") - 1U),
            sl_http_transport_literal("HTTP/2 frames must fit configured transport caps",
                                      sizeof("HTTP/2 frames must fit configured transport caps") -
                                          1U));
    }
    if (bytes.length == 0U) {
        return sl_status_ok();
    }
    connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_WRITING_RESPONSE;
    return sl_http_transport_start_write_bytes(connection, bytes, out_diag);
}

static SlStatus sl_http_transport_http2_close_if_peer_goaway(SlHttpTransportConnection* connection,
                                                             SlDiag* out_diag)
{
    if (connection == NULL || !connection->http2_dispatcher_started ||
        !sl_http2_server_dispatcher_peer_goaway_received(connection->http2_dispatcher))
    {
        return sl_status_ok();
    }
    connection->close_after_write = true;
    if (connection->platform != NULL && connection->platform->writing) {
        return sl_status_ok();
    }
    return sl_http_transport_connection_close(connection, out_diag);
}

static SlStatus sl_http_transport_http2_close_if_requested(SlHttpTransportConnection* connection,
                                                           SlDiag* out_diag)
{
    if (connection == NULL || !connection->http2_dispatcher_started ||
        !sl_http2_server_dispatcher_close_without_goaway(connection->http2_dispatcher))
    {
        return sl_status_ok();
    }
    connection->close_after_write = true;
    return sl_http_transport_connection_close(connection, out_diag);
}

static SlStatus sl_http_transport_http2_init(SlHttpTransportConnection* connection,
                                             bool reset_request_arena, SlDiag* out_diag)
{
    SlHttpTransportServer* server = sl_http_transport_connection_server(connection);
    SlHttp2DispatchConfig config = {0};
    SlStatus status;
    void* dispatcher_storage = NULL;

    if (connection == NULL || server == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (server->config.dispatch == NULL) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_DISPATCH_FAILED, SL_STATUS_INVALID_STATE,
            sl_http_transport_literal("HTTP/2 dispatch callback is not configured",
                                      sizeof("HTTP/2 dispatch callback is not configured") - 1U),
            sl_http_transport_literal("configure the runtime dispatch hook before accepting h2",
                                      sizeof("configure the runtime dispatch hook before "
                                             "accepting h2") -
                                          1U));
    }

    sl_http_transport_stop_timer(&connection->platform->header_timer,
                                 &connection->platform->header_timer_initialized);
    sl_http_transport_stop_timer(&connection->platform->body_timer,
                                 &connection->platform->body_timer_initialized);
    sl_http_transport_stop_timer(&connection->platform->request_timer,
                                 &connection->platform->request_timer_initialized);
    if (reset_request_arena) {
        sl_arena_reset(&connection->request_arena);
    }

    status = sl_arena_alloc(&connection->request_arena, sizeof(SlHttp2ServerDispatcher),
                            _Alignof(SlHttp2ServerDispatcher), &dispatcher_storage);
    if (!sl_status_is_ok(status)) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_TRANSPORT_CONFIG, sl_status_code(status),
            sl_http_transport_literal("HTTP/2 dispatcher storage allocation failed",
                                      sizeof("HTTP/2 dispatcher storage allocation failed") - 1U),
            sl_http_transport_literal("increase the per-connection request arena for h2",
                                      sizeof("increase the per-connection request arena for h2") -
                                          1U));
    }
    connection->http2_dispatcher = (SlHttp2ServerDispatcher*)dispatcher_storage;

    config.dispatch = sl_http_transport_http2_dispatch_adapter;
    config.dispatch_user = connection;
    config.max_streams = server->config.max_active_requests == 0U
                             ? SL_HTTP2_DISPATCH_DEFAULT_MAX_STREAMS
                             : server->config.max_active_requests;
    config.max_body_bytes = server->backend.options.parse.max_body_length;
    config.max_response_body_bytes = server->config.max_response_bytes;
    config.session.max_concurrent_streams =
        config.max_streams > UINT32_MAX ? UINT32_MAX : (uint32_t)config.max_streams;
    config.session.max_event_data_bytes = config.max_body_bytes;
    config.session.max_outbound_bytes = server->config.max_response_bytes;

    status = sl_http2_server_dispatcher_init(
        connection->http2_dispatcher, &connection->request_arena, &connection->core, &config);
    if (!sl_status_is_ok(status)) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_TRANSPORT_CONFIG, sl_status_code(status),
            sl_http_transport_literal("HTTP/2 dispatcher initialization failed",
                                      sizeof("HTTP/2 dispatcher initialization failed") - 1U),
            sl_http_transport_literal("HTTP/2 connection state stays bounded per accepted slot",
                                      sizeof("HTTP/2 connection state stays bounded per accepted "
                                             "slot") -
                                          1U));
    }

    connection->http2_mode = true;
    connection->http2_dispatcher_started = true;
    connection->core.scheme = connection->platform != NULL && connection->platform->tls_enabled
                                  ? sl_str_from_cstr("https")
                                  : sl_str_from_cstr("http");
    connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_READING_BODY;

    return sl_status_ok();
}

static SlStatus sl_http_transport_http2_start(SlHttpTransportConnection* connection,
                                              SlBytes initial_bytes, SlDiag* out_diag)
{
    SlStatus status;

    status = sl_http_transport_http2_init(connection, true, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (initial_bytes.length != 0U) {
        status = sl_http_transport_http2_receive(connection, initial_bytes, out_diag);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    status = sl_http_transport_http2_flush_output(connection, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_http_transport_http2_close_if_peer_goaway(connection, out_diag);
}

static SlStatus sl_http_transport_http2_receive(SlHttpTransportConnection* connection,
                                                SlBytes bytes, SlDiag* out_diag)
{
    SlStatus status;
    size_t consumed = 0U;

    if (connection == NULL || !connection->http2_dispatcher_started) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_http2_server_dispatcher_receive(connection->http2_dispatcher, bytes, &consumed);
    if (!sl_status_is_ok(status) || consumed != bytes.length) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_INVALID_HTTP_REQUEST,
            sl_status_is_ok(status) ? SL_STATUS_INVALID_ARGUMENT : sl_status_code(status),
            sl_http_transport_literal("HTTP/2 frame input is malformed",
                                      sizeof("HTTP/2 frame input is malformed") - 1U),
            sl_http_transport_literal("invalid HTTP/2 connections are closed without exposing "
                                      "socket details",
                                      sizeof("invalid HTTP/2 connections are closed without "
                                             "exposing socket details") -
                                          1U));
    }
    status = sl_http_transport_http2_close_if_requested(connection, out_diag);
    if (!sl_status_is_ok(status) ||
        (connection->state == SL_HTTP_TRANSPORT_CONNECTION_STATE_CLOSING ||
         connection->state == SL_HTTP_TRANSPORT_CONNECTION_STATE_CLOSED))
    {
        return status;
    }
    status = sl_http_transport_http2_flush_output(connection, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_http_transport_http2_close_if_peer_goaway(connection, out_diag);
}

static bool sl_http_transport_http2_preface_prefix_matches(SlBytes bytes)
{
    if (bytes.ptr == NULL || bytes.length == 0U || bytes.length > SL_HTTP2_PREFACE_BYTES) {
        return false;
    }
    for (size_t index = 0U; index < bytes.length; index += 1U) {
        if (bytes.ptr[index] != SL_HTTP2_PREFACE[index]) {
            return false;
        }
    }
    return true;
}

static bool sl_http_transport_http2_preface_attempted(SlBytes bytes)
{
    if (bytes.ptr == NULL || bytes.length < 3U) {
        return false;
    }
    for (size_t index = 0U; index < 3U; index += 1U) {
        if (bytes.ptr[index] != SL_HTTP2_PREFACE[index]) {
            return false;
        }
    }
    return true;
}

static SlStatus sl_http_transport_maybe_start_http2(SlHttpTransportConnection* connection,
                                                    bool* out_handled, SlDiag* out_diag)
{
    SlBytes accumulated = {0};
    SlBytes initial_frames = {0};
    SlHttpTransportServer* server = sl_http_transport_connection_server(connection);

    if (out_handled != NULL) {
        *out_handled = false;
    }
    if (connection == NULL || out_handled == NULL || connection->http2_mode ||
        connection->request_started || connection->head_length != 0U)
    {
        return sl_status_ok();
    }

    accumulated = sl_bytes_from_parts(connection->accumulation, connection->accumulation_length);
    if (!sl_http_transport_http2_preface_prefix_matches(sl_bytes_from_parts(
            accumulated.ptr, accumulated.length < SL_HTTP2_PREFACE_BYTES ? accumulated.length
                                                                         : SL_HTTP2_PREFACE_BYTES)))
    {
        if (connection->platform != NULL && connection->platform->tls_enabled &&
            connection->platform->tls_alpn_h2)
        {
            *out_handled = true;
            return sl_http_transport_connection_diag(
                connection, out_diag, SL_DIAG_INVALID_HTTP_REQUEST, SL_STATUS_INVALID_ARGUMENT,
                sl_http_transport_literal("HTTP/2 ALPN connection did not start with h2 preface",
                                          sizeof("HTTP/2 ALPN connection did not start with h2 "
                                                 "preface") -
                                              1U),
                sl_http_transport_literal("TLS ALPN h2 must stay on the HTTP/2 parser path",
                                          sizeof("TLS ALPN h2 must stay on the HTTP/2 parser "
                                                 "path") -
                                              1U));
        }
        if (sl_http_transport_http2_preface_attempted(accumulated) ||
            (server != NULL && server->config.http2_prior_knowledge_only))
        {
            *out_handled = true;
            (void)sl_http_transport_connection_diag(
                connection, out_diag, SL_DIAG_INVALID_HTTP_REQUEST, SL_STATUS_INVALID_ARGUMENT,
                sl_http_transport_literal("HTTP/2 prior-knowledge preface is invalid",
                                          sizeof("HTTP/2 prior-knowledge preface is invalid") -
                                              1U),
                sl_http_transport_literal("invalid h2 prefaces close without HTTP/1 fallback",
                                          sizeof("invalid h2 prefaces close without HTTP/1 "
                                                 "fallback") -
                                              1U));
            return sl_http_transport_connection_close(connection, NULL);
        }
        return sl_status_ok();
    }

    if (accumulated.length < SL_HTTP2_PREFACE_BYTES) {
        connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_READING_HEAD;
        *out_handled = true;
        return sl_status_ok();
    }

    if (connection->core.request_count != 0U ||
        (connection->platform != NULL && connection->platform->tls_enabled &&
         !connection->platform->tls_alpn_h2))
    {
        *out_handled = true;
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_INVALID_HTTP_REQUEST, SL_STATUS_INVALID_ARGUMENT,
            sl_http_transport_literal("HTTP/2 prior-knowledge preface is not allowed here",
                                      sizeof("HTTP/2 prior-knowledge preface is not allowed here") -
                                          1U),
            sl_http_transport_literal(
                "h2 prior knowledge requires a fresh cleartext connection or TLS ALPN h2",
                sizeof("h2 prior knowledge requires a fresh cleartext connection or TLS ALPN h2") -
                    1U));
    }

    initial_frames = accumulated;
    *out_handled = true;
    return sl_http_transport_http2_start(connection, initial_frames, out_diag);
}

static SlStatus sl_http_transport_write_stream_next(SlHttpTransportConnection* connection,
                                                    SlDiag* out_diag)
{
    SlByteBuilder builder = {0};
    SlBytes bytes = {0};
    SlStatus status;

    if (connection == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (connection->stream_final_written) {
        return sl_status_ok();
    }
    status = sl_byte_builder_init_fixed(&builder, connection->response_storage,
                                        connection->response_storage_size);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (connection->stream_chunk_index < connection->active_response.stream_chunk_count) {
        SlBytes chunk =
            connection->active_response.stream_chunks[connection->stream_chunk_index].bytes;
        SlHttpTransportServer* server = sl_http_transport_connection_server(connection);
        if (server == NULL) {
            return sl_http_transport_connection_diag(
                connection, out_diag, SL_DIAG_HTTP_RESPONSE_BACKPRESSURE,
                SL_STATUS_CAPACITY_EXCEEDED,
                sl_http_transport_literal("HTTP streaming response exceeded pending write cap",
                                          sizeof("HTTP streaming response exceeded pending write "
                                                 "cap") -
                                              1U),
                sl_http_transport_literal("stream chunks must fit the configured pending write cap",
                                          sizeof("stream chunks must fit the configured pending "
                                                 "write cap") -
                                              1U));
        }
        status = sl_http_transport_builder_append_size_hex(&builder, chunk.length);
        if (sl_status_is_ok(status)) {
            status = sl_http_transport_builder_append_cstr(&builder, "\r\n");
        }
        if (sl_status_is_ok(status)) {
            status = sl_byte_builder_append_bytes(&builder, chunk);
        }
        if (sl_status_is_ok(status)) {
            status = sl_http_transport_builder_append_cstr(&builder, "\r\n");
        }
        connection->stream_chunk_index += 1U;
    }
    else {
        status = sl_http_transport_builder_append_cstr(&builder, "0\r\n\r\n");
        connection->stream_final_written = true;
    }
    if (!sl_status_is_ok(status)) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_RESPONSE_SERIALIZATION_FAILED,
            sl_status_code(status),
            sl_http_transport_literal("HTTP streaming response serialization failed",
                                      sizeof("HTTP streaming response serialization failed") - 1U),
            sl_http_transport_literal("stream frame bytes must fit the response buffer",
                                      sizeof("stream frame bytes must fit the response buffer") -
                                          1U));
    }
    bytes = sl_byte_builder_view(&builder);
    {
        SlHttpTransportServer* server = sl_http_transport_connection_server(connection);
        if (server == NULL || bytes.length > server->config.max_pending_write_bytes) {
            return sl_http_transport_connection_diag(
                connection, out_diag, SL_DIAG_HTTP_RESPONSE_BACKPRESSURE,
                SL_STATUS_CAPACITY_EXCEEDED,
                sl_http_transport_literal("HTTP streaming response exceeded pending write cap",
                                          sizeof("HTTP streaming response exceeded pending write "
                                                 "cap") -
                                              1U),
                sl_http_transport_literal("serialized stream frames must fit the configured "
                                          "pending write cap",
                                          sizeof("serialized stream frames must fit the configured "
                                                 "pending write cap") -
                                              1U));
        }
    }
    connection->response_length += bytes.length;
    return sl_http_transport_start_write_bytes(connection, bytes, out_diag);
}

static bool sl_http_transport_arena_contains(const SlArena* arena, const void* ptr, size_t length)
{
    uintptr_t base = 0U;
    uintptr_t start = 0U;
    size_t offset = 0U;

    if (arena == NULL) {
        return false;
    }
    if (length == 0U) {
        return true;
    }
    if (arena->base == NULL || ptr == NULL) {
        return false;
    }
    base = (uintptr_t)arena->base;
    start = (uintptr_t)ptr;
    if (start < base) {
        return false;
    }
    offset = (size_t)(start - base);
    return offset <= arena->offset && length <= arena->offset - offset;
}

static SlStatus sl_http_transport_copy_stream_response(SlHttpTransportConnection* connection,
                                                       const SlHttpResponse* response,
                                                       SlHttpResponse* out_response)
{
    SlSlice chunk_storage = {0};
    SlHttpResponseStreamChunk* chunks = NULL;
    SlStatus status;

    if (connection == NULL || response == NULL || out_response == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_response = *response;
    if (response->stream_chunk_count == 0U) {
        out_response->stream_chunks = NULL;
        return sl_status_ok();
    }
    if (response->stream_chunks == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    {
        size_t chunk_bytes = 0U;
        status = sl_checked_array_size(response->stream_chunk_count,
                                       sizeof(SlHttpResponseStreamChunk), &chunk_bytes);
        if (!sl_status_is_ok(status)) {
            *out_response = (SlHttpResponse){0};
            return status;
        }
        if (!sl_http_transport_arena_contains(&connection->request_arena, response->stream_chunks,
                                              chunk_bytes))
        {
            *out_response = (SlHttpResponse){0};
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        status = sl_arena_array_copy(
            &connection->request_arena, response->stream_chunks, response->stream_chunk_count,
            sizeof(SlHttpResponseStreamChunk), _Alignof(SlHttpResponseStreamChunk), &chunk_storage);
    }
    if (!sl_status_is_ok(status)) {
        *out_response = (SlHttpResponse){0};
        return status;
    }
    chunks = (SlHttpResponseStreamChunk*)chunk_storage.ptr;
    for (size_t index = 0U; index < response->stream_chunk_count; index += 1U) {
        SlOwnedBytes copy = {0};
        if (!sl_http_transport_arena_contains(&connection->request_arena,
                                              response->stream_chunks[index].bytes.ptr,
                                              response->stream_chunks[index].bytes.length))
        {
            *out_response = (SlHttpResponse){0};
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        status = sl_bytes_copy_to_arena(&connection->request_arena,
                                        response->stream_chunks[index].bytes, &copy);
        if (!sl_status_is_ok(status)) {
            *out_response = (SlHttpResponse){0};
            return status;
        }
        chunks[index].bytes = sl_owned_bytes_as_view(copy);
    }
    out_response->stream_chunks = chunks;
    return sl_status_ok();
}

static SlStatus sl_http_transport_write_stream_head(SlHttpTransportConnection* connection,
                                                    const SlHttpResponse* response,
                                                    SlDiag* out_diag)
{
    SlByteBuilder builder = {0};
    SlHttpResponse response_copy = {0};
    const char* reason = NULL;
    SlStatus status;

    if (connection == NULL || response == NULL ||
        (response->header_count != 0U && response->headers == NULL) ||
        (response->stream_chunk_count != 0U && response->stream_chunks == NULL))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    reason = sl_http_transport_reason(response->status);
    if (reason == NULL || response->status == 204U || response->status == 304U) {
        return sl_status_from_code(SL_STATUS_UNSUPPORTED);
    }
    status = sl_byte_builder_init_fixed(&builder, connection->response_storage,
                                        connection->response_storage_size);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (!sl_http_transport_response_header_value_valid(response->content_type)) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_RESPONSE_SERIALIZATION_FAILED,
            SL_STATUS_INVALID_ARGUMENT,
            sl_http_transport_literal("HTTP streaming response header is invalid",
                                      sizeof("HTTP streaming response header is invalid") - 1U),
            sl_http_transport_literal("stream response content type must not contain control bytes",
                                      sizeof("stream response content type must not contain "
                                             "control bytes") -
                                          1U));
    }
    for (size_t header_index = 0U; header_index < response->header_count; header_index += 1U) {
        const SlHttpHeader* header = &response->headers[header_index];
        if (!sl_http_transport_response_header_name_valid(header->name) ||
            sl_http_transport_response_header_managed(header->name) ||
            !sl_http_transport_response_header_value_valid(header->value))
        {
            return sl_http_transport_connection_diag(
                connection, out_diag, SL_DIAG_HTTP_RESPONSE_SERIALIZATION_FAILED,
                SL_STATUS_INVALID_ARGUMENT,
                sl_http_transport_literal("HTTP streaming response header is invalid",
                                          sizeof("HTTP streaming response header is invalid") - 1U),
                sl_http_transport_literal("stream response custom headers must be safe and must "
                                          "not override managed headers",
                                          sizeof("stream response custom headers must be safe and "
                                                 "must not override managed headers") -
                                              1U));
        }
    }
    status = sl_http_transport_builder_append_cstr(&builder, "HTTP/1.1 ");
    if (sl_status_is_ok(status)) {
        status = sl_http_transport_builder_append_size_decimal(&builder, response->status);
    }
    if (sl_status_is_ok(status)) {
        status = sl_http_transport_builder_append_cstr(&builder, " ");
    }
    if (sl_status_is_ok(status)) {
        status = sl_http_transport_builder_append_cstr(&builder, reason);
    }
    if (sl_status_is_ok(status)) {
        status = sl_http_transport_builder_append_cstr(&builder, "\r\nConnection: ");
    }
    if (sl_status_is_ok(status)) {
        status = sl_http_transport_builder_append_cstr(
            &builder, connection->keep_alive_after_write ? "keep-alive" : "close");
    }
    if (sl_status_is_ok(status) && response->content_type.length != 0U) {
        status = sl_http_transport_builder_append_cstr(&builder, "\r\nContent-Type: ");
        if (sl_status_is_ok(status)) {
            status = sl_http_transport_builder_append_str(&builder, response->content_type);
        }
    }
    for (size_t header_index = 0U; sl_status_is_ok(status) && header_index < response->header_count;
         header_index += 1U)
    {
        const SlHttpHeader* header = &response->headers[header_index];
        status = sl_http_transport_builder_append_cstr(&builder, "\r\n");
        if (sl_status_is_ok(status)) {
            status = sl_http_transport_builder_append_str(&builder, header->name);
        }
        if (sl_status_is_ok(status)) {
            status = sl_http_transport_builder_append_cstr(&builder, ": ");
        }
        if (sl_status_is_ok(status)) {
            status = sl_http_transport_builder_append_str(&builder, header->value);
        }
    }
    if (sl_status_is_ok(status)) {
        status = sl_http_transport_builder_append_cstr(&builder,
                                                       "\r\nTransfer-Encoding: chunked\r\n\r\n");
    }
    if (!sl_status_is_ok(status)) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_RESPONSE_SERIALIZATION_FAILED,
            sl_status_code(status),
            sl_http_transport_literal("HTTP streaming response serialization failed",
                                      sizeof("HTTP streaming response serialization failed") - 1U),
            sl_http_transport_literal("stream response head must fit the response buffer",
                                      sizeof("stream response head must fit the response buffer") -
                                          1U));
    }
    status = sl_http_transport_copy_stream_response(connection, response, &response_copy);
    if (!sl_status_is_ok(status)) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_RESPONSE_SERIALIZATION_FAILED,
            sl_status_code(status),
            sl_http_transport_literal("HTTP streaming response copy failed",
                                      sizeof("HTTP streaming response copy failed") - 1U),
            sl_http_transport_literal(
                "stream chunk metadata and payloads must fit the request arena",
                sizeof("stream chunk metadata and payloads must fit the request arena") - 1U));
    }
    connection->active_response = response_copy;
    connection->streaming_response = true;
    connection->stream_chunk_index = 0U;
    connection->stream_final_written = false;
    {
        SlBytes bytes = sl_byte_builder_view(&builder);
        SlHttpTransportServer* server = sl_http_transport_connection_server(connection);
        if (server == NULL || bytes.length > server->config.max_pending_write_bytes) {
            return sl_http_transport_connection_diag(
                connection, out_diag, SL_DIAG_HTTP_RESPONSE_BACKPRESSURE,
                SL_STATUS_CAPACITY_EXCEEDED,
                sl_http_transport_literal("HTTP streaming response exceeded pending write cap",
                                          sizeof("HTTP streaming response exceeded pending write "
                                                 "cap") -
                                              1U),
                sl_http_transport_literal("stream response head must fit the configured pending "
                                          "write cap",
                                          sizeof("stream response head must fit the configured "
                                                 "pending write cap") -
                                              1U));
        }
        connection->response_length = bytes.length;
        return sl_http_transport_start_write_bytes(connection, bytes, out_diag);
    }
}

static void sl_http_transport_write_cb(uv_write_t* request, int status)
{
    SlHttpPlatformConnection* platform =
        request == NULL ? NULL : (SlHttpPlatformConnection*)request->data;
    SlHttpTransportConnection* connection = platform == NULL ? NULL : platform->owner;
    SlDiag diag = {0};

    if (platform != NULL) {
        platform->writing = false;
        sl_http_transport_stop_timer(&platform->write_timer, &platform->write_timer_initialized);
        sl_http_transport_stop_timer(&platform->request_timer,
                                     &platform->request_timer_initialized);
    }
    if (connection == NULL) {
        return;
    }

    if (connection->http2_mode) {
        connection->write_completed = true;
        if (status != 0) {
            (void)sl_http_connection_fail(&connection->core, NULL);
            (void)sl_http_transport_connection_close(connection, NULL);
            return;
        }
        if (connection->state == SL_HTTP_TRANSPORT_CONNECTION_STATE_CLOSING ||
            connection->close_after_write)
        {
            (void)sl_http_transport_connection_close(connection, NULL);
            return;
        }
        connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_READING_BODY;
        {
            SlStatus flush_status = sl_http_transport_http2_flush_output(connection, &diag);
            if (!sl_status_is_ok(flush_status)) {
                sl_http_transport_fail_and_close(connection, &diag);
            }
        }
        return;
    }

    if (connection->streaming_response && status == 0 && !connection->stream_final_written) {
        if (!sl_status_is_ok(sl_http_transport_write_stream_next(connection, NULL))) {
            if (connection->request_started) {
                (void)sl_http_request_fail(&connection->request, NULL);
            }
            (void)sl_http_transport_connection_close(connection, NULL);
        }
        return;
    }

    connection->write_completed = true;
    if (connection->request_started &&
        !sl_http_transport_request_terminal(connection->request.state))
    {
        if (status != 0) {
            (void)sl_http_transport_connection_diag(
                connection, &diag, SL_DIAG_HTTP_WRITE_FAILED, SL_STATUS_INTERNAL,
                sl_http_transport_literal("HTTP transport response write failed",
                                          sizeof("HTTP transport response write failed") - 1U),
                sl_http_transport_literal(
                    "socket details stay inside the platform boundary",
                    sizeof("socket details stay inside the platform boundary") - 1U));
            (void)sl_http_request_fail(&connection->request, NULL);
        }
        else {
            (void)sl_http_request_complete(&connection->request, NULL);
        }
    }

    if (status == 0 && connection->keep_alive_after_write && !connection->close_after_write) {
        (void)sl_http_transport_restart_keep_alive_read(connection, NULL);
        return;
    }

    (void)sl_http_transport_connection_close(connection, NULL);
}

static void sl_http_transport_tls_write_cb(uv_write_t* request, int status)
{
    SlHttpPlatformConnection* platform =
        request == NULL ? NULL : (SlHttpPlatformConnection*)request->data;
    SlHttpTransportConnection* connection = platform == NULL ? NULL : platform->owner;
    SlDiag diag = {0};

    if (platform != NULL) {
        platform->tls_writing = false;
    }
    if (connection == NULL) {
        return;
    }
    if (status != 0) {
        (void)sl_http_transport_connection_diag(
            connection, &diag, SL_DIAG_HTTP_TLS_HANDSHAKE_FAILED, SL_STATUS_INTERNAL,
            sl_http_transport_literal("HTTP TLS handshake write failed",
                                      sizeof("HTTP TLS handshake write failed") - 1U),
            sl_http_transport_literal("socket and OpenSSL handles stay inside the platform "
                                      "boundary",
                                      sizeof("socket and OpenSSL handles stay inside the platform "
                                             "boundary") -
                                          1U));
        (void)sl_http_transport_connection_close(connection, NULL);
        return;
    }
    if (!sl_status_is_ok(sl_http_transport_tls_drain_handshake(connection, &diag))) {
        (void)sl_http_transport_connection_close(connection, NULL);
    }
}

static void sl_http_transport_tls_shutdown_write_cb(uv_write_t* request, int status)
{
    SlHttpPlatformConnection* platform =
        request == NULL ? NULL : (SlHttpPlatformConnection*)request->data;

    (void)status;
    if (platform == NULL) {
        return;
    }
    platform->tls_writing = false;
    platform->tls_shutdown_writing = false;
    if (platform->initialized && !uv_is_closing((uv_handle_t*)&platform->handle)) {
        uv_close((uv_handle_t*)&platform->handle, sl_http_transport_connection_close_cb);
    }
}

static SlStatus sl_http_transport_write_response(SlHttpTransportConnection* connection,
                                                 const SlHttpResponse* response, SlDiag* out_diag)
{
    SlStatus status;
    SlBytes bytes = {0};
    SlHttpResponseWriteOptions write_options = {0};
    bool suppress_body = false;
    SlHttpResponse head_stream_response = {0};
    const SlHttpResponse* fixed_response = response;

    if (connection == NULL || response == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (connection->state != SL_HTTP_TRANSPORT_CONNECTION_STATE_DISPATCHING ||
        connection->request.state != SL_HTTP_REQUEST_STATE_DISPATCHING)
    {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_APP_LIFECYCLE, SL_STATUS_INVALID_STATE,
            sl_http_transport_literal("HTTP transport dispatch state is invalid",
                                      sizeof("HTTP transport dispatch state is invalid") - 1U),
            sl_http_transport_literal("write responses only after one request dispatch",
                                      sizeof("write responses only after one request dispatch") -
                                          1U));
    }

    status = sl_http_request_begin_write(&connection->request, out_diag);
    if (!sl_status_is_ok(status)) {
        sl_http_transport_store_diag(connection, out_diag);
        return status;
    }
    connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_WRITING_RESPONSE;
    connection->keep_alive_after_write =
        sl_http_transport_request_keep_alive_eligible(connection, response);
    connection->close_after_write = !connection->keep_alive_after_write;
    write_options.connection = connection->keep_alive_after_write
                                   ? SL_HTTP_RESPONSE_CONNECTION_KEEP_ALIVE
                                   : SL_HTTP_RESPONSE_CONNECTION_CLOSE;
    suppress_body = connection->request.head.method == SL_HTTP_METHOD_HEAD;
    write_options.suppress_body = suppress_body;
    if (response->kind == SL_HTTP_RESPONSE_STREAM) {
        if (suppress_body) {
            head_stream_response = *response;
            head_stream_response.kind = SL_HTTP_RESPONSE_EMPTY;
            head_stream_response.body = sl_bytes_empty();
            head_stream_response.stream_chunks = NULL;
            head_stream_response.stream_chunk_count = 0U;
            fixed_response = &head_stream_response;
        }
        else {
            status = sl_http_transport_write_stream_head(connection, response, out_diag);
            if (!sl_status_is_ok(status)) {
                return sl_http_transport_connection_diag(
                    connection, out_diag, SL_DIAG_HTTP_RESPONSE_SERIALIZATION_FAILED,
                    sl_status_code(status),
                    sl_http_transport_literal(
                        "HTTP streaming response serialization failed",
                        sizeof("HTTP streaming response serialization failed") - 1U),
                    sl_http_transport_literal(
                        "streaming response bytes must fit configured caps",
                        sizeof("streaming response bytes must fit configured caps") - 1U));
            }
            return sl_status_ok();
        }
    }

    status = sl_http_response_write_with_options(fixed_response, &write_options,
                                                 connection->response_storage,
                                                 connection->response_storage_size, &bytes);
    if (!sl_status_is_ok(status)) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_RESPONSE_SERIALIZATION_FAILED,
            sl_status_code(status),
            sl_http_transport_literal("HTTP response serialization failed",
                                      sizeof("HTTP response serialization failed") - 1U),
            sl_http_transport_literal("response bytes must fit the configured transport buffer",
                                      sizeof("response bytes must fit the configured transport "
                                             "buffer") -
                                          1U));
    }
    if (bytes.length > UINT_MAX) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_RESPONSE_SERIALIZATION_FAILED,
            SL_STATUS_CAPACITY_EXCEEDED,
            sl_http_transport_literal("HTTP response serialization failed",
                                      sizeof("HTTP response serialization failed") - 1U),
            sl_http_transport_literal("response bytes exceed the platform write boundary",
                                      sizeof("response bytes exceed the platform write boundary") -
                                          1U));
    }

    connection->response_length = bytes.length;
    return sl_http_transport_start_write_bytes(connection, bytes, out_diag);
}

static SlStatus sl_http_transport_write_error_response(SlHttpTransportConnection* connection,
                                                       uint16_t status_code, SlDiag* out_diag)
{
    SlHttpResponse response =
        sl_http_response_text(status_code, sl_http_transport_body_for_status(status_code));
    SlBytes bytes = {0};

    if (connection == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (connection->request_started &&
        connection->request.state == SL_HTTP_REQUEST_STATE_DISPATCHING)
    {
        return sl_http_transport_write_response(connection, &response, out_diag);
    }

    if (connection->platform == NULL || !connection->platform->initialized ||
        connection->platform->closing || connection->platform->writing)
    {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    if (!sl_status_is_ok(sl_http_response_write(&response, connection->response_storage,
                                                connection->response_storage_size, &bytes)) ||
        bytes.length > UINT_MAX)
    {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_RESPONSE_SERIALIZATION_FAILED,
            SL_STATUS_CAPACITY_EXCEEDED,
            sl_http_transport_literal("HTTP response serialization failed",
                                      sizeof("HTTP response serialization failed") - 1U),
            sl_http_transport_literal("response bytes must fit the configured transport buffer",
                                      sizeof("response bytes must fit the configured transport "
                                             "buffer") -
                                          1U));
    }

    connection->response_length = bytes.length;
    connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_WRITING_RESPONSE;
    return sl_http_transport_start_write_bytes(connection, bytes, out_diag);
}

static void sl_http_transport_timer_close_cb(uv_handle_t* handle)
{
    (void)handle;
}

static void sl_http_transport_stop_timer(uv_timer_t* timer, bool* initialized)
{
    if (timer == NULL || initialized == NULL || !*initialized) {
        return;
    }
    (void)uv_timer_stop(timer);
}

static void sl_http_transport_close_timer(uv_timer_t* timer, bool* initialized)
{
    if (timer == NULL || initialized == NULL || !*initialized) {
        return;
    }
    (void)uv_timer_stop(timer);
    if (!uv_is_closing((uv_handle_t*)timer)) {
        uv_close((uv_handle_t*)timer, sl_http_transport_timer_close_cb);
    }
    *initialized = false;
}

static void sl_http_transport_stop_connection_timers(SlHttpPlatformConnection* platform)
{
    if (platform == NULL) {
        return;
    }
    sl_http_transport_stop_timer(&platform->header_timer, &platform->header_timer_initialized);
    sl_http_transport_stop_timer(&platform->body_timer, &platform->body_timer_initialized);
    sl_http_transport_stop_timer(&platform->request_timer, &platform->request_timer_initialized);
    sl_http_transport_stop_timer(&platform->write_timer, &platform->write_timer_initialized);
    sl_http_transport_stop_timer(&platform->idle_timer, &platform->idle_timer_initialized);
}

static void sl_http_transport_close_connection_timers(SlHttpPlatformConnection* platform)
{
    if (platform == NULL) {
        return;
    }
    sl_http_transport_close_timer(&platform->header_timer, &platform->header_timer_initialized);
    sl_http_transport_close_timer(&platform->body_timer, &platform->body_timer_initialized);
    sl_http_transport_close_timer(&platform->request_timer, &platform->request_timer_initialized);
    sl_http_transport_close_timer(&platform->write_timer, &platform->write_timer_initialized);
    sl_http_transport_close_timer(&platform->idle_timer, &platform->idle_timer_initialized);
}

static SlStatus sl_http_transport_start_timer(SlHttpPlatformConnection* platform, uv_timer_t* timer,
                                              bool* initialized, uint64_t timeout_ms,
                                              uv_timer_cb callback)
{
    if (platform == NULL || timer == NULL || initialized == NULL || callback == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (!*initialized) {
        int rc = uv_timer_init(platform->handle.loop, timer);
        if (rc != 0) {
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
        timer->data = platform;
        *initialized = true;
    }
    (void)uv_timer_stop(timer);
    if (timeout_ms == 0U) {
        return sl_status_ok();
    }
    if (uv_timer_start(timer, callback, timeout_ms, 0U) != 0) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    return sl_status_ok();
}

static bool sl_http_transport_connection_terminal(const SlHttpTransportConnection* connection)
{
    return connection == NULL || connection->state == SL_HTTP_TRANSPORT_CONNECTION_STATE_CLOSING ||
           connection->state == SL_HTTP_TRANSPORT_CONNECTION_STATE_CLOSED ||
           connection->state == SL_HTTP_TRANSPORT_CONNECTION_STATE_ERROR;
}

static SlHttpTransportServer*
sl_http_transport_connection_server(const SlHttpTransportConnection* connection)
{
    if (connection == NULL || connection->core.backend == NULL ||
        connection->core.backend->listener.platform == NULL)
    {
        return NULL;
    }
    return ((SlHttpPlatformListener*)connection->core.backend->listener.platform)->server;
}

static void sl_http_transport_timeout_connection(SlHttpTransportConnection* connection, SlStr phase)
{
    SlDiag diag = {0};
    SlDiag write_diag = {0};

    if (sl_http_transport_connection_terminal(connection)) {
        return;
    }
    (void)sl_http_transport_connection_diag(
        connection, &diag, SL_DIAG_HTTP_REQUEST_TIMEOUT, SL_STATUS_DEADLINE_EXCEEDED,
        sl_http_transport_literal("HTTP transport request timed out",
                                  sizeof("HTTP transport request timed out") - 1U),
        phase);
    if (connection->platform != NULL && connection->platform->reading) {
        (void)uv_read_stop((uv_stream_t*)&connection->platform->handle);
        connection->platform->reading = false;
    }
    if (connection->request_started && connection->request.state != SL_HTTP_REQUEST_STATE_CLOSED &&
        connection->request.state != SL_HTTP_REQUEST_STATE_COMPLETED &&
        connection->request.state != SL_HTTP_REQUEST_STATE_CANCELLED &&
        connection->request.state != SL_HTTP_REQUEST_STATE_TIMED_OUT &&
        connection->request.state != SL_HTTP_REQUEST_STATE_FAILED)
    {
        (void)sl_http_request_timeout(&connection->request, phase, NULL);
    }
    sl_http_transport_stop_connection_timers(connection->platform);
    if (connection->platform != NULL && connection->platform->initialized &&
        !connection->platform->closing && !connection->platform->writing &&
        connection->response_storage != NULL && connection->response_storage_size != 0U)
    {
        if (sl_status_is_ok(sl_http_transport_write_error_response(connection, 408U, &write_diag)))
        {
            return;
        }
    }
    (void)sl_http_transport_connection_close(connection, NULL);
}

static void sl_http_transport_header_timeout_cb(uv_timer_t* timer)
{
    SlHttpPlatformConnection* platform =
        timer == NULL ? NULL : (SlHttpPlatformConnection*)timer->data;
    sl_http_transport_timeout_connection(
        platform == NULL ? NULL : platform->owner,
        sl_http_transport_literal("HTTP transport header read timeout",
                                  sizeof("HTTP transport header read timeout") - 1U));
}

static void sl_http_transport_body_timeout_cb(uv_timer_t* timer)
{
    SlHttpPlatformConnection* platform =
        timer == NULL ? NULL : (SlHttpPlatformConnection*)timer->data;
    sl_http_transport_timeout_connection(
        platform == NULL ? NULL : platform->owner,
        sl_http_transport_literal("HTTP transport body read timeout",
                                  sizeof("HTTP transport body read timeout") - 1U));
}

static void sl_http_transport_request_timeout_cb(uv_timer_t* timer)
{
    SlHttpPlatformConnection* platform =
        timer == NULL ? NULL : (SlHttpPlatformConnection*)timer->data;
    sl_http_transport_timeout_connection(
        platform == NULL ? NULL : platform->owner,
        sl_http_transport_literal("HTTP transport request timeout",
                                  sizeof("HTTP transport request timeout") - 1U));
}

static void sl_http_transport_write_timeout_cb(uv_timer_t* timer)
{
    SlHttpPlatformConnection* platform =
        timer == NULL ? NULL : (SlHttpPlatformConnection*)timer->data;
    SlHttpTransportConnection* connection = platform == NULL ? NULL : platform->owner;
    SlDiag diag = {0};

    if (sl_http_transport_connection_terminal(connection)) {
        return;
    }
    (void)sl_http_transport_connection_diag(
        connection, &diag, SL_DIAG_HTTP_WRITE_FAILED, SL_STATUS_DEADLINE_EXCEEDED,
        sl_http_transport_literal("HTTP transport response write timed out",
                                  sizeof("HTTP transport response write timed out") - 1U),
        sl_http_transport_literal("late write completion is cleanup-only",
                                  sizeof("late write completion is cleanup-only") - 1U));
    if (connection->request_started) {
        (void)sl_http_request_timeout(
            &connection->request,
            sl_http_transport_literal("HTTP transport write timeout",
                                      sizeof("HTTP transport write timeout") - 1U),
            NULL);
    }
    (void)sl_http_transport_connection_close(connection, NULL);
}

static void sl_http_transport_idle_timeout_cb(uv_timer_t* timer)
{
    SlHttpPlatformConnection* platform =
        timer == NULL ? NULL : (SlHttpPlatformConnection*)timer->data;
    SlHttpTransportConnection* connection = platform == NULL ? NULL : platform->owner;
    SlHttpTransportServer* server = sl_http_transport_connection_server(connection);
    SlDiag diag = {0};

    if (sl_http_transport_connection_terminal(connection)) {
        return;
    }
    if (server != NULL) {
        server->idle_timeouts += 1U;
    }
    (void)sl_http_transport_connection_diag(
        connection, &diag, SL_DIAG_HTTP_KEEP_ALIVE_IDLE_TIMEOUT, SL_STATUS_DEADLINE_EXCEEDED,
        sl_http_transport_literal("HTTP keep-alive idle timeout elapsed",
                                  sizeof("HTTP keep-alive idle timeout elapsed") - 1U),
        sl_http_transport_literal("idle keep-alive connections are closed cleanly",
                                  sizeof("idle keep-alive connections are closed cleanly") - 1U));
    (void)sl_http_transport_connection_close(connection, NULL);
}

static SlStatus sl_http_transport_start_keep_alive_idle(SlHttpTransportConnection* connection,
                                                        SlDiag* out_diag)
{
    SlHttpTransportServer* server = sl_http_transport_connection_server(connection);
    SlStatus status;

    if (connection == NULL || connection->platform == NULL || server == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_KEEP_ALIVE_IDLE;
    status = sl_http_transport_start_timer(connection->platform, &connection->platform->idle_timer,
                                           &connection->platform->idle_timer_initialized,
                                           server->config.keep_alive_idle_timeout_ms,
                                           sl_http_transport_idle_timeout_cb);
    if (!sl_status_is_ok(status)) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_REQUEST_TIMEOUT, sl_status_code(status),
            sl_http_transport_literal("HTTP keep-alive idle timeout could not start",
                                      sizeof("HTTP keep-alive idle timeout could not start") - 1U),
            sl_http_transport_literal("the connection will be closed",
                                      sizeof("the connection will be closed") - 1U));
    }
    return sl_status_ok();
}

static SlStatus sl_http_transport_restart_keep_alive_read(SlHttpTransportConnection* connection,
                                                          SlDiag* out_diag)
{
    SlStatus status;
    int rc = 0;

    if (connection == NULL || connection->platform == NULL || !connection->platform->initialized) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    sl_http_transport_reset_request_state(connection);
    status = sl_http_transport_start_keep_alive_idle(connection, out_diag);
    if (!sl_status_is_ok(status)) {
        (void)sl_http_transport_connection_close(connection, NULL);
        return status;
    }
    if (!connection->platform->reading) {
        rc = uv_read_start((uv_stream_t*)&connection->platform->handle,
                           sl_http_transport_alloc_read, sl_http_transport_on_read);
        if (rc != 0) {
            (void)sl_http_transport_connection_close(connection, NULL);
            return sl_http_transport_uv_status(
                rc, out_diag, SL_DIAG_HTTP_CONNECTION_CLOSED,
                sl_http_transport_literal("HTTP keep-alive read restart failed",
                                          sizeof("HTTP keep-alive read restart failed") - 1U));
        }
        connection->platform->reading = true;
    }
    return sl_status_ok();
}

static bool sl_http_transport_parse_size_decimal(SlStr value, size_t* out)
{
    size_t index = 0U;
    size_t result = 0U;

    if (out == NULL) {
        return false;
    }
    *out = 0U;
    value = sl_http_transport_trim_ascii_space(value);
    if (value.ptr == NULL || value.length == 0U) {
        return false;
    }
    for (index = 0U; index < value.length; index += 1U) {
        size_t next = 0U;
        if (value.ptr[index] < '0' || value.ptr[index] > '9') {
            return false;
        }
        if (!sl_status_is_ok(sl_checked_mul_size(result, 10U, &next))) {
            return false;
        }
        if (!sl_status_is_ok(sl_checked_add_size(next, (size_t)(value.ptr[index] - '0'), &next))) {
            return false;
        }
        result = next;
    }
    *out = result;
    return true;
}

static bool sl_http_transport_header_line_has_name(SlStr line, SlStr name, SlStr* out_value)
{
    size_t index = 0U;
    SlStr line_name = {0};
    SlStr line_value = {0};

    if (out_value != NULL) {
        *out_value = sl_str_empty();
    }
    if (line.ptr == NULL || name.ptr == NULL) {
        return false;
    }
    while (index < line.length && line.ptr[index] != ':') {
        index += 1U;
    }
    if (index == line.length) {
        return false;
    }
    line_name = sl_http_transport_trim_ascii_space(sl_str_from_parts(line.ptr, index));
    line_value = sl_http_transport_trim_ascii_space(
        sl_str_from_parts(line.ptr + index + 1U, line.length - index - 1U));
    if (!sl_str_equal_ci_ascii(line_name, name)) {
        return false;
    }
    if (out_value != NULL) {
        *out_value = line_value;
    }
    return true;
}

static bool sl_http_transport_head_header_value(SlBytes head, SlStr name, SlStr* out_value,
                                                size_t* out_count)
{
    size_t cursor = 0U;
    size_t line_start = 0U;
    size_t count = 0U;
    bool found = false;

    if (out_value != NULL) {
        *out_value = sl_str_empty();
    }
    if (out_count != NULL) {
        *out_count = 0U;
    }
    if (head.ptr == NULL || name.ptr == NULL) {
        return false;
    }
    while (cursor + 1U < head.length) {
        if (head.ptr[cursor] == '\r' && head.ptr[cursor + 1U] == '\n') {
            SlStr line = sl_str_from_parts((const char*)head.ptr + line_start, cursor - line_start);
            SlStr value = {0};
            if (line_start != 0U && sl_http_transport_header_line_has_name(line, name, &value)) {
                if (out_value != NULL && !found) {
                    *out_value = value;
                }
                count += 1U;
                found = true;
            }
            cursor += 2U;
            line_start = cursor;
            if (cursor + 1U < head.length && head.ptr[cursor] == '\r' &&
                head.ptr[cursor + 1U] == '\n')
            {
                break;
            }
            continue;
        }
        cursor += 1U;
    }
    if (out_count != NULL) {
        *out_count = count;
    }
    return found;
}

static bool sl_http_transport_transfer_encoding_is_chunked(SlStr value)
{
    value = sl_http_transport_trim_ascii_space(value);
    return sl_str_equal_ci_ascii(value, sl_str_from_cstr("chunked"));
}

static bool sl_http_transport_expect_header_present(SlBytes head)
{
    size_t count = 0U;

    return sl_http_transport_head_header_value(head, sl_str_from_cstr("Expect"), NULL, &count) &&
           count != 0U;
}

static int sl_http_transport_base64url_value(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A';
    }
    if (ch >= 'a' && ch <= 'z') {
        return ch - 'a' + 26;
    }
    if (ch >= '0' && ch <= '9') {
        return ch - '0' + 52;
    }
    if (ch == '-') {
        return 62;
    }
    if (ch == '_') {
        return 63;
    }
    return -1;
}

static bool sl_http_transport_base64url_decode(SlStr value, unsigned char* out, size_t out_capacity,
                                               size_t* out_length)
{
    uint32_t accumulator = 0U;
    unsigned int bits = 0U;
    size_t output = 0U;
    size_t padding = 0U;

    if (out_length != NULL) {
        *out_length = 0U;
    }
    value = sl_http_transport_trim_ascii_space(value);
    if (out == NULL || out_length == NULL || (value.ptr == NULL && value.length != 0U)) {
        return false;
    }
    if (value.length == 0U) {
        return true;
    }
    if ((value.length % 4U) == 1U) {
        return false;
    }

    for (size_t index = 0U; index < value.length; index += 1U) {
        int decoded = 0;

        if (value.ptr[index] == '=') {
            padding += 1U;
            if (padding > 2U) {
                return false;
            }
            continue;
        }
        if (padding != 0U) {
            return false;
        }

        decoded = sl_http_transport_base64url_value(value.ptr[index]);
        if (decoded < 0) {
            return false;
        }
        accumulator = (accumulator << 6U) | (uint32_t)decoded;
        bits += 6U;
        if (bits >= 8U) {
            bits -= 8U;
            if (output >= out_capacity) {
                return false;
            }
            out[output] = (unsigned char)((accumulator >> bits) & 0xffU);
            output += 1U;
        }
    }

    if (padding != 0U && (value.length % 4U) != 0U) {
        return false;
    }
    if (bits != 0U && (accumulator & ((1U << bits) - 1U)) != 0U) {
        return false;
    }

    *out_length = output;
    return true;
}

static bool sl_http_transport_request_is_h2c_upgrade(SlHttpTransportConnection* connection,
                                                     SlStr* out_settings, bool* out_malformed)
{
    SlHttpRequestHead* head = connection == NULL ? NULL : &connection->request.head;
    SlStr settings = {0};
    size_t upgrade_count = 0U;
    size_t connection_count = 0U;
    size_t settings_count = 0U;
    bool upgrade_h2c = false;
    bool connection_upgrade = false;
    bool connection_settings = false;

    if (out_settings != NULL) {
        *out_settings = sl_str_empty();
    }
    if (out_malformed != NULL) {
        *out_malformed = false;
    }
    if (connection == NULL || head == NULL || out_settings == NULL || out_malformed == NULL) {
        return false;
    }

    upgrade_h2c = sl_http_transport_request_header_has_token(
        head, sl_str_from_cstr("Upgrade"), sl_str_from_cstr("h2c"), &upgrade_count);
    if (!upgrade_h2c) {
        return false;
    }

    connection_upgrade = sl_http_transport_request_header_has_token(
        head, sl_str_from_cstr("Connection"), sl_str_from_cstr("Upgrade"), &connection_count);
    connection_settings = sl_http_transport_request_header_has_token(
        head, sl_str_from_cstr("Connection"), sl_str_from_cstr("HTTP2-Settings"), NULL);
    (void)sl_http_transport_request_header_value_count(head, sl_str_from_cstr("HTTP2-Settings"),
                                                       &settings, &settings_count);

    if ((connection->platform != NULL && connection->platform->tls_enabled) ||
        head->version_major != 1U || head->version_minor != 1U || upgrade_count != 1U ||
        connection_count == 0U || !connection_upgrade || !connection_settings ||
        settings_count != 1U)
    {
        *out_malformed = true;
        return true;
    }

    *out_settings = settings;
    return true;
}

static SlStatus sl_http_transport_h2c_upgrade_response(SlHttpTransportConnection* connection,
                                                       SlBytes http2_bytes, SlDiag* out_diag)
{
    SlByteBuilder builder = {0};
    SlBytes bytes = {0};
    SlStatus status;
    SlHttpTransportServer* server = sl_http_transport_connection_server(connection);

    if (connection == NULL || (http2_bytes.ptr == NULL && http2_bytes.length != 0U)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_byte_builder_init_fixed(&builder, connection->response_storage,
                                        connection->response_storage_size);
    if (sl_status_is_ok(status)) {
        status = sl_byte_builder_append_bytes(
            &builder, sl_bytes_from_parts((const unsigned char*)SL_HTTP2_H2C_UPGRADE_STATUS,
                                          SL_HTTP2_H2C_UPGRADE_STATUS_BYTES));
    }
    if (sl_status_is_ok(status)) {
        status = sl_byte_builder_append_bytes(&builder, http2_bytes);
    }
    if (!sl_status_is_ok(status)) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_RESPONSE_SERIALIZATION_FAILED,
            sl_status_code(status),
            sl_http_transport_literal("HTTP/2 upgrade response serialization failed",
                                      sizeof("HTTP/2 upgrade response serialization failed") - 1U),
            sl_http_transport_literal("h2c upgrade response frames must fit the response buffer",
                                      sizeof("h2c upgrade response frames must fit the response "
                                             "buffer") -
                                          1U));
    }

    bytes = sl_byte_builder_view(&builder);
    if (server == NULL || bytes.length > server->config.max_pending_write_bytes) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_RESPONSE_BACKPRESSURE, SL_STATUS_CAPACITY_EXCEEDED,
            sl_http_transport_literal("HTTP/2 upgrade response exceeded pending write cap",
                                      sizeof("HTTP/2 upgrade response exceeded pending write "
                                             "cap") -
                                          1U),
            sl_http_transport_literal("h2c upgrade response frames must fit the pending write cap",
                                      sizeof("h2c upgrade response frames must fit the pending "
                                             "write cap") -
                                          1U));
    }

    connection->response_length = bytes.length;
    connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_WRITING_RESPONSE;
    return sl_http_transport_start_write_bytes(connection, bytes, out_diag);
}

static SlStatus sl_http_transport_try_h2c_upgrade(SlHttpTransportConnection* connection,
                                                  bool* out_upgraded, SlDiag* out_diag)
{
    SlStr settings_value = {0};
    SlBytes settings_payload = {0};
    SlBytes http2_bytes = {0};
    SlStatus status;
    void* settings_storage = NULL;
    size_t settings_capacity = 0U;
    size_t settings_length = 0U;
    bool malformed = false;

    if (out_upgraded != NULL) {
        *out_upgraded = false;
    }
    if (connection == NULL || out_upgraded == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (!sl_http_transport_request_is_h2c_upgrade(connection, &settings_value, &malformed)) {
        return sl_status_ok();
    }
    if (malformed) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_INVALID_HTTP_REQUEST, SL_STATUS_INVALID_ARGUMENT,
            sl_http_transport_literal("HTTP/2 h2c Upgrade request is invalid",
                                      sizeof("HTTP/2 h2c Upgrade request is invalid") - 1U),
            sl_http_transport_literal(
                "h2c Upgrade requires HTTP/1.1, Upgrade: h2c, Connection tokens, and one "
                "HTTP2-Settings header",
                sizeof("h2c Upgrade requires HTTP/1.1, Upgrade: h2c, Connection tokens, and one "
                       "HTTP2-Settings header") -
                    1U));
    }

    if (settings_value.length != 0U) {
        status = sl_checked_mul_size(settings_value.length, 3U, &settings_capacity);
        if (sl_status_is_ok(status)) {
            settings_capacity = (settings_capacity / 4U) + 3U;
        }
        if (sl_status_is_ok(status)) {
            status = sl_arena_alloc(&connection->request_arena, settings_capacity, 1U,
                                    &settings_storage);
        }
        if (!sl_status_is_ok(status)) {
            return status;
        }
        if (!sl_http_transport_base64url_decode(settings_value, (unsigned char*)settings_storage,
                                                settings_capacity, &settings_length))
        {
            return sl_http_transport_connection_diag(
                connection, out_diag, SL_DIAG_INVALID_HTTP_REQUEST, SL_STATUS_INVALID_ARGUMENT,
                sl_http_transport_literal("HTTP/2 h2c settings are malformed",
                                          sizeof("HTTP/2 h2c settings are malformed") - 1U),
                sl_http_transport_literal("HTTP2-Settings must be base64url SETTINGS payload bytes",
                                          sizeof("HTTP2-Settings must be base64url SETTINGS "
                                                 "payload bytes") -
                                              1U));
        }
        settings_payload =
            sl_bytes_from_parts((const unsigned char*)settings_storage, settings_length);
    }

    status = sl_http_transport_http2_init(connection, false, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http2_server_dispatcher_upgrade_h2c(connection->http2_dispatcher, settings_payload,
                                                    &connection->request);
    if (!sl_status_is_ok(status)) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_INVALID_HTTP_REQUEST, sl_status_code(status),
            sl_http_transport_literal("HTTP/2 h2c Upgrade state failed",
                                      sizeof("HTTP/2 h2c Upgrade state failed") - 1U),
            sl_http_transport_literal("HTTP2-Settings must decode to a valid SETTINGS payload",
                                      sizeof("HTTP2-Settings must decode to a valid SETTINGS "
                                             "payload") -
                                          1U));
    }
    status = sl_http2_server_dispatcher_drain_output(connection->http2_dispatcher, &http2_bytes);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    *out_upgraded = true;
    return sl_http_transport_h2c_upgrade_response(connection, http2_bytes, out_diag);
}

static size_t sl_http_transport_find_header_end(SlBytes bytes)
{
    size_t index = 0U;

    if (bytes.ptr == NULL || bytes.length < 4U) {
        return 0U;
    }
    for (index = 0U; index + 3U < bytes.length; index += 1U) {
        if (bytes.ptr[index] == '\r' && bytes.ptr[index + 1U] == '\n' &&
            bytes.ptr[index + 2U] == '\r' && bytes.ptr[index + 3U] == '\n')
        {
            return index + 4U;
        }
    }
    return 0U;
}

static bool sl_http_transport_parse_chunk_size_line(SlStr line, size_t* out_size,
                                                    SlDiagCode* out_code)
{
    size_t index = 0U;
    size_t result = 0U;

    if (out_size == NULL || out_code == NULL) {
        return false;
    }
    *out_size = 0U;
    *out_code = SL_DIAG_HTTP_CHUNK_SIZE_INVALID;
    if (line.ptr == NULL || line.length == 0U) {
        return false;
    }
    for (index = 0U; index < line.length; index += 1U) {
        unsigned char ch = (unsigned char)line.ptr[index];
        size_t digit = 0U;
        size_t next = 0U;

        if (ch >= '0' && ch <= '9') {
            digit = (size_t)(ch - (unsigned char)'0');
        }
        else if (ch >= 'a' && ch <= 'f') {
            digit = (size_t)(ch - (unsigned char)'a') + 10U;
        }
        else if (ch >= 'A' && ch <= 'F') {
            digit = (size_t)(ch - (unsigned char)'A') + 10U;
        }
        else {
            return false;
        }
        if (!sl_status_is_ok(sl_checked_mul_size(result, 16U, &next)) ||
            !sl_status_is_ok(sl_checked_add_size(next, digit, &next)))
        {
            *out_code = SL_DIAG_HTTP_CHUNK_SIZE_OVERFLOW;
            return false;
        }
        result = next;
    }
    *out_size = result;
    return true;
}

static SlStatus sl_http_transport_chunked_complete_length(SlHttpTransportConnection* connection,
                                                          size_t* out_total_needed,
                                                          bool* out_complete, SlDiag* out_diag)
{
    SlHttpTransportServer* server = sl_http_transport_connection_server(connection);
    size_t cursor = 0U;
    size_t decoded = 0U;

    if (out_total_needed != NULL) {
        *out_total_needed = 0U;
    }
    if (out_complete != NULL) {
        *out_complete = false;
    }
    if (connection == NULL || server == NULL || out_total_needed == NULL || out_complete == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    cursor = connection->head_length;
    while (cursor < connection->accumulation_length) {
        size_t line_end = cursor;
        size_t chunk_size = 0U;
        size_t data_start = 0U;
        size_t data_end = 0U;
        size_t delimiter_end = 0U;
        SlDiagCode code = SL_DIAG_HTTP_CHUNK_SIZE_INVALID;

        while (line_end + 1U < connection->accumulation_length &&
               !(connection->accumulation[line_end] == '\r' &&
                 connection->accumulation[line_end + 1U] == '\n'))
        {
            line_end += 1U;
        }
        if (line_end + 1U >= connection->accumulation_length) {
            return sl_status_ok();
        }
        if (!sl_http_transport_parse_chunk_size_line(
                sl_str_from_parts((const char*)connection->accumulation + cursor,
                                  line_end - cursor),
                &chunk_size, &code))
        {
            return sl_http_transport_connection_diag(
                connection, out_diag, code,
                code == SL_DIAG_HTTP_CHUNK_SIZE_OVERFLOW ? SL_STATUS_OVERFLOW
                                                         : SL_STATUS_INVALID_ARGUMENT,
                code == SL_DIAG_HTTP_CHUNK_SIZE_OVERFLOW
                    ? sl_http_transport_literal("HTTP chunk size overflows size bounds",
                                                sizeof("HTTP chunk size overflows size bounds") -
                                                    1U)
                    : sl_http_transport_literal("HTTP chunk size is invalid",
                                                sizeof("HTTP chunk size is invalid") - 1U),
                sl_http_transport_literal("chunk sizes must be bounded hexadecimal byte counts",
                                          sizeof("chunk sizes must be bounded hexadecimal byte "
                                                 "counts") -
                                              1U));
        }
        if (!sl_status_is_ok(sl_checked_add_size(line_end, 2U, &data_start)) ||
            !sl_status_is_ok(sl_checked_add_size(data_start, chunk_size, &data_end)) ||
            !sl_status_is_ok(sl_checked_add_size(data_end, 2U, &delimiter_end)))
        {
            return sl_http_transport_connection_diag(
                connection, out_diag, SL_DIAG_HTTP_CHUNK_SIZE_OVERFLOW, SL_STATUS_OVERFLOW,
                sl_http_transport_literal("HTTP chunk size overflows size bounds",
                                          sizeof("HTTP chunk size overflows size bounds") - 1U),
                sl_http_transport_literal("decoded chunk sizes must fit the configured body cap",
                                          sizeof("decoded chunk sizes must fit the configured body "
                                                 "cap") -
                                              1U));
        }
        if (!sl_status_is_ok(sl_checked_add_size(decoded, chunk_size, &decoded))) {
            return sl_http_transport_connection_diag(
                connection, out_diag, SL_DIAG_HTTP_CHUNK_SIZE_OVERFLOW, SL_STATUS_OVERFLOW,
                sl_http_transport_literal("HTTP decoded chunk body overflows size bounds",
                                          sizeof("HTTP decoded chunk body overflows size bounds") -
                                              1U),
                sl_http_transport_literal("decoded chunk sizes must fit size_t",
                                          sizeof("decoded chunk sizes must fit size_t") - 1U));
        }
        if (decoded > server->backend.options.parse.max_body_length) {
            return sl_http_transport_connection_diag(
                connection, out_diag, SL_DIAG_HTTP_BODY_LIMIT, SL_STATUS_CAPACITY_EXCEEDED,
                sl_http_transport_literal("HTTP decoded chunk body is too large",
                                          sizeof("HTTP decoded chunk body is too large") - 1U),
                sl_http_transport_literal("decoded chunked bodies use the configured body cap",
                                          sizeof("decoded chunked bodies use the configured body "
                                                 "cap") -
                                              1U));
        }
        if (chunk_size == 0U) {
            if (delimiter_end > connection->accumulation_length) {
                return sl_status_ok();
            }
            if (connection->accumulation[data_start] == '\r' &&
                connection->accumulation[data_start + 1U] == '\n')
            {
                *out_total_needed = delimiter_end;
                *out_complete = true;
                return sl_status_ok();
            }
            if (sl_http_transport_find_header_end(
                    sl_bytes_from_parts(connection->accumulation + data_start,
                                        connection->accumulation_length - data_start)) != 0U)
            {
                return sl_http_transport_connection_diag(
                    connection, out_diag, SL_DIAG_HTTP_TRAILERS_UNSUPPORTED, SL_STATUS_UNSUPPORTED,
                    sl_http_transport_literal("HTTP chunk trailers are not supported",
                                              sizeof("HTTP chunk trailers are not supported") - 1U),
                    sl_http_transport_literal("send the final zero chunk without trailers",
                                              sizeof("send the final zero chunk without trailers") -
                                                  1U));
            }
            return sl_status_ok();
        }
        if (delimiter_end > connection->accumulation_length) {
            return sl_status_ok();
        }
        if (connection->accumulation[data_end] != '\r' ||
            connection->accumulation[data_end + 1U] != '\n')
        {
            return sl_http_transport_connection_diag(
                connection, out_diag, SL_DIAG_HTTP_CHUNK_DELIMITER_INVALID,
                SL_STATUS_INVALID_ARGUMENT,
                sl_http_transport_literal("HTTP chunk delimiter is malformed",
                                          sizeof("HTTP chunk delimiter is malformed") - 1U),
                sl_http_transport_literal("each chunk body must end with CRLF",
                                          sizeof("each chunk body must end with CRLF") - 1U));
        }
        cursor = delimiter_end;
    }
    return sl_status_ok();
}

static SlStatus sl_http_transport_append_bytes(SlHttpTransportConnection* connection, SlBytes bytes,
                                               SlDiag* out_diag)
{
    SlStatus status;

    if (connection == NULL || (bytes.ptr == NULL && bytes.length != 0U)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_byte_builder_append_bytes(&connection->accumulation_builder, bytes);
    if (!sl_status_is_ok(status)) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_BODY_LIMIT, SL_STATUS_CAPACITY_EXCEEDED,
            sl_http_transport_literal("HTTP transport request body is too large",
                                      sizeof("HTTP transport request body is too large") - 1U),
            sl_http_transport_literal("request bytes are accumulated only up to configured caps",
                                      sizeof("request bytes are accumulated only up to configured "
                                             "caps") -
                                          1U));
    }
    connection->accumulation_length = sl_byte_builder_length(&connection->accumulation_builder);
    return sl_status_ok();
}

static SlStatus sl_http_transport_dispatch_ready(SlHttpTransportConnection* connection,
                                                 SlHttpTransportServer* server, SlDiag* out_diag)
{
    SlStatus status;
    SlHttpResponse response = {0};
    SlDiag dispatch_diag = {0};
    uint16_t safe_status = 500U;

    if (connection == NULL || server == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (connection->state != SL_HTTP_TRANSPORT_CONNECTION_STATE_REQUEST_READY ||
        connection->request.state != SL_HTTP_REQUEST_STATE_READING)
    {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_APP_LIFECYCLE, SL_STATUS_INVALID_STATE,
            sl_http_transport_literal("HTTP transport dispatch state is invalid",
                                      sizeof("HTTP transport dispatch state is invalid") - 1U),
            sl_http_transport_literal("dispatch only one parsed request-ready connection",
                                      sizeof("dispatch only one parsed request-ready connection") -
                                          1U));
    }
    if (server->config.dispatch == NULL) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_DISPATCH_FAILED, SL_STATUS_INVALID_STATE,
            sl_http_transport_literal("HTTP transport dispatch callback is not configured",
                                      sizeof("HTTP transport dispatch callback is not configured") -
                                          1U),
            sl_http_transport_literal("configure the runtime dispatch hook before listening",
                                      sizeof("configure the runtime dispatch hook before "
                                             "listening") -
                                          1U));
    }

    status = sl_http_request_begin_dispatch(&connection->request, out_diag);
    if (!sl_status_is_ok(status)) {
        sl_http_transport_store_diag(connection, out_diag);
        return status;
    }
    connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_DISPATCHING;

    status = server->config.dispatch(connection, &connection->request_arena, &connection->request,
                                     &response, &dispatch_diag, server->config.dispatch_user);
    if (!sl_status_is_ok(status)) {
        safe_status = sl_http_transport_status_for_failure(status, &dispatch_diag);
        sl_http_transport_store_diag(connection, &dispatch_diag);
        response = sl_http_response_text(
            safe_status, sl_http_transport_body_for_failure(safe_status, &dispatch_diag));
    }

    status = sl_http_transport_write_response(connection, &response, out_diag);
    if (!sl_status_is_ok(status)) {
        (void)sl_http_request_fail(&connection->request, NULL);
        return status;
    }
    return sl_status_ok();
}

static SlStatus sl_http_transport_parse_accumulated(SlHttpTransportConnection* connection,
                                                    SlDiag* out_diag)
{
    SlStatus status;
    SlStr content_length = {0};
    SlStr content_type = {0};
    SlStr transfer_encoding = {0};
    SlBytes full_message = {0};
    SlBytes body = {0};
    bool is_chunked = false;

    if (connection == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (!connection->request_started) {
        status = sl_http_request_begin(&connection->core, &connection->request_arena,
                                       &connection->request, out_diag);
        if (!sl_status_is_ok(status)) {
            if (out_diag != NULL) {
                sl_http_transport_store_diag(connection, out_diag);
            }
            return status;
        }
        connection->request_started = true;
    }

    full_message = sl_bytes_from_parts(connection->accumulation, connection->accumulation_length);
    status = sl_http_request_parse_head(&connection->request, full_message, out_diag);
    if (!sl_status_is_ok(status)) {
        if (out_diag != NULL) {
            sl_http_transport_store_diag(connection, out_diag);
        }
        return status;
    }

    is_chunked =
        sl_http_transport_header_value(&connection->request.head,
                                       sl_str_from_cstr("Transfer-Encoding"), &transfer_encoding) &&
        sl_http_transport_transfer_encoding_is_chunked(transfer_encoding);

    if (!sl_http_transport_header_value(&connection->request.head, sl_str_from_cstr("Content-Type"),
                                        &content_type))
    {
        content_type = sl_str_empty();
    }
    if (!is_chunked &&
        sl_http_transport_header_value(&connection->request.head,
                                       sl_str_from_cstr("Content-Length"), &content_length) &&
        !sl_http_transport_parse_size_decimal(content_length, &connection->expected_body_length))
    {
        (void)sl_http_request_fail(&connection->request, NULL);
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_INVALID_HTTP_REQUEST, SL_STATUS_INVALID_ARGUMENT,
            sl_http_transport_literal("HTTP Content-Length is invalid",
                                      sizeof("HTTP Content-Length is invalid") - 1U),
            sl_http_transport_literal("Content-Length must be a decimal byte count",
                                      sizeof("Content-Length must be a decimal byte count") - 1U));
    }

    if (!is_chunked) {
        status = sl_http_request_body_reader_begin(&connection->request, content_type,
                                                   connection->expected_body_length,
                                                   &connection->body_reader, out_diag);
        if (!sl_status_is_ok(status)) {
            if (out_diag != NULL) {
                sl_http_transport_store_diag(connection, out_diag);
            }
            return status;
        }
        connection->body_reader_started = true;

        body = sl_bytes_from_parts(connection->accumulation + connection->head_length,
                                   connection->expected_body_length);
        status = sl_http_request_body_reader_append(&connection->body_reader, body, out_diag);
        if (!sl_status_is_ok(status)) {
            if (out_diag != NULL) {
                sl_http_transport_store_diag(connection, out_diag);
            }
            return status;
        }
        status = sl_http_request_body_reader_finish(&connection->body_reader, out_diag);
        if (!sl_status_is_ok(status)) {
            if (out_diag != NULL) {
                sl_http_transport_store_diag(connection, out_diag);
            }
            return status;
        }
        connection->body_reader_finished = true;
    }
    {
        bool h2c_upgraded = false;
        status = sl_http_transport_try_h2c_upgrade(connection, &h2c_upgraded, out_diag);
        if (!sl_status_is_ok(status) || h2c_upgraded) {
            return status;
        }
    }
    connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_REQUEST_READY;
    if (connection->platform != NULL) {
        connection->platform->reading = false;
        (void)uv_read_stop((uv_stream_t*)&connection->platform->handle);
    }
    if (connection->core.backend != NULL && connection->core.backend->listener.platform != NULL) {
        SlHttpTransportServer* server =
            ((SlHttpPlatformListener*)connection->core.backend->listener.platform)->server;
        if (server != NULL) {
            if (server->config.on_request_ready != NULL) {
                server->config.on_request_ready(connection, &connection->request,
                                                server->config.on_request_ready_user);
            }
            if (server->config.dispatch != NULL) {
                return sl_http_transport_dispatch_ready(connection, server, out_diag);
            }
            if (server->config.on_request_ready == NULL) {
                (void)sl_http_transport_connection_close(connection, NULL);
            }
        }
    }
    return sl_status_ok();
}

static SlStatus sl_http_transport_try_complete_request(SlHttpTransportConnection* connection,
                                                       SlDiag* out_diag)
{
    SlHttpTransportServer* server = NULL;
    size_t total_needed = 0U;
    SlStatus status;

    if (connection == NULL || connection->core.backend == NULL ||
        connection->core.backend->listener.platform == NULL)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    server = ((SlHttpPlatformListener*)connection->core.backend->listener.platform)->server;
    if (server == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (connection->head_length == 0U) {
        connection->head_length = sl_http_transport_find_header_end(
            sl_bytes_from_parts(connection->accumulation, connection->accumulation_length));
        if (connection->head_length == 0U) {
            if (connection->accumulation_length > server->config.max_request_head_bytes) {
                return sl_http_transport_connection_diag(
                    connection, out_diag, SL_DIAG_HTTP_HEADER_BYTES_LIMIT,
                    SL_STATUS_CAPACITY_EXCEEDED,
                    sl_http_transport_literal("HTTP request head is too large",
                                              sizeof("HTTP request head is too large") - 1U),
                    sl_http_transport_literal(
                        "the transport rejects request heads above the configured cap",
                        sizeof("the transport rejects request heads above the configured cap") -
                            1U));
            }
            connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_READING_HEAD;
            return sl_status_ok();
        }
        if (connection->head_length > server->config.max_request_head_bytes) {
            return sl_http_transport_connection_diag(
                connection, out_diag, SL_DIAG_HTTP_HEADER_BYTES_LIMIT, SL_STATUS_CAPACITY_EXCEEDED,
                sl_http_transport_literal("HTTP request head is too large",
                                          sizeof("HTTP request head is too large") - 1U),
                sl_http_transport_literal(
                    "the transport rejects request heads above the configured cap",
                    sizeof("the transport rejects request heads above the configured cap") - 1U));
        }
        sl_http_transport_stop_timer(&connection->platform->header_timer,
                                     &connection->platform->header_timer_initialized);
    }

    if (sl_http_transport_expect_header_present(
            sl_bytes_from_parts(connection->accumulation, connection->head_length)))
    {
        (void)sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_INVALID_HTTP_REQUEST, SL_STATUS_UNSUPPORTED,
            sl_http_transport_literal("HTTP Expect header is not supported",
                                      sizeof("HTTP Expect header is not supported") - 1U),
            sl_http_transport_literal("send the request body without Expect: 100-continue",
                                      sizeof("send the request body without Expect: 100-continue") -
                                          1U));
        return sl_http_transport_write_error_response(connection, 417U, out_diag);
    }

    {
        SlStr content_length = {0};
        SlStr transfer_encoding = {0};
        size_t content_length_count = 0U;
        size_t transfer_encoding_count = 0U;
        bool has_content_length = false;
        bool has_transfer_encoding = false;

        has_transfer_encoding = sl_http_transport_head_header_value(
            sl_bytes_from_parts(connection->accumulation, connection->head_length),
            sl_str_from_cstr("Transfer-Encoding"), &transfer_encoding, &transfer_encoding_count);
        has_content_length = sl_http_transport_head_header_value(
            sl_bytes_from_parts(connection->accumulation, connection->head_length),
            sl_str_from_cstr("Content-Length"), &content_length, &content_length_count);
        if (has_transfer_encoding) {
            size_t chunked_total_needed = 0U;
            bool chunked_complete = false;

            if (transfer_encoding_count != 1U ||
                !sl_http_transport_transfer_encoding_is_chunked(transfer_encoding))
            {
                return sl_http_transport_connection_diag(
                    connection, out_diag, SL_DIAG_HTTP_UNSUPPORTED_BODY, SL_STATUS_UNSUPPORTED,
                    sl_http_transport_literal("HTTP transfer encoding is not supported",
                                              sizeof("HTTP transfer encoding is not supported") -
                                                  1U),
                    sl_http_transport_literal("only bounded chunked request decoding is supported",
                                              sizeof("only bounded chunked request decoding is "
                                                     "supported") -
                                                  1U));
            }
            if (has_content_length) {
                return sl_http_transport_connection_diag(
                    connection, out_diag, SL_DIAG_INVALID_HTTP_REQUEST, SL_STATUS_INVALID_ARGUMENT,
                    sl_http_transport_literal(
                        "HTTP Content-Length conflicts with Transfer-Encoding",
                        sizeof("HTTP Content-Length conflicts with Transfer-Encoding") - 1U),
                    sl_http_transport_literal(
                        "send either Content-Length or Transfer-Encoding: chunked, not both",
                        sizeof("send either Content-Length or Transfer-Encoding: chunked, not "
                               "both") -
                            1U));
            }
            status = sl_http_transport_chunked_complete_length(connection, &chunked_total_needed,
                                                               &chunked_complete, out_diag);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            if (!chunked_complete) {
                connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_READING_BODY;
                status = sl_http_transport_start_timer(
                    connection->platform, &connection->platform->body_timer,
                    &connection->platform->body_timer_initialized,
                    server->config.body_read_timeout_ms, sl_http_transport_body_timeout_cb);
                if (!sl_status_is_ok(status)) {
                    return sl_http_transport_connection_diag(
                        connection, out_diag, SL_DIAG_HTTP_REQUEST_TIMEOUT, sl_status_code(status),
                        sl_http_transport_literal(
                            "HTTP transport body timeout could not start",
                            sizeof("HTTP transport body timeout could not start") - 1U),
                        sl_http_transport_literal("the connection will be closed",
                                                  sizeof("the connection will be closed") - 1U));
                }
                return sl_status_ok();
            }
            if (connection->accumulation_length > chunked_total_needed) {
                server->pipelining_attempts += 1U;
                (void)sl_http_request_fail(&connection->request, NULL);
                return sl_http_transport_connection_diag(
                    connection, out_diag, SL_DIAG_HTTP_PIPELINING_UNSUPPORTED,
                    SL_STATUS_UNSUPPORTED,
                    sl_http_transport_literal("HTTP pipelining is not supported",
                                              sizeof("HTTP pipelining is not supported") - 1U),
                    sl_http_transport_literal(
                        "send the next request after the response completes",
                        sizeof("send the next request after the response completes") - 1U));
            }
            sl_http_transport_stop_timer(&connection->platform->body_timer,
                                         &connection->platform->body_timer_initialized);
            return sl_http_transport_parse_accumulated(connection, out_diag);
        }
        if (has_content_length) {
            if (content_length_count != 1U ||
                !sl_http_transport_parse_size_decimal(content_length,
                                                      &connection->expected_body_length))
            {
                return sl_http_transport_connection_diag(
                    connection, out_diag, SL_DIAG_INVALID_HTTP_REQUEST, SL_STATUS_INVALID_ARGUMENT,
                    sl_http_transport_literal("HTTP Content-Length is invalid",
                                              sizeof("HTTP Content-Length is invalid") - 1U),
                    sl_http_transport_literal("Content-Length must be a decimal byte count",
                                              sizeof("Content-Length must be a decimal byte "
                                                     "count") -
                                                  1U));
            }
        }
    }
    if (connection->expected_body_length > server->backend.options.parse.max_body_length) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_BODY_LIMIT, SL_STATUS_CAPACITY_EXCEEDED,
            sl_http_transport_literal("HTTP request body is too large",
                                      sizeof("HTTP request body is too large") - 1U),
            sl_http_transport_literal("request bodies are copied only up to the configured limit",
                                      sizeof("request bodies are copied only up to the configured "
                                             "limit") -
                                          1U));
    }

    status = sl_checked_add_size(connection->head_length, connection->expected_body_length,
                                 &total_needed);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (connection->accumulation_length < total_needed) {
        connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_READING_BODY;
        status = sl_http_transport_start_timer(
            connection->platform, &connection->platform->body_timer,
            &connection->platform->body_timer_initialized, server->config.body_read_timeout_ms,
            sl_http_transport_body_timeout_cb);
        if (!sl_status_is_ok(status)) {
            return sl_http_transport_connection_diag(
                connection, out_diag, SL_DIAG_HTTP_REQUEST_TIMEOUT, sl_status_code(status),
                sl_http_transport_literal("HTTP transport body timeout could not start",
                                          sizeof("HTTP transport body timeout could not start") -
                                              1U),
                sl_http_transport_literal("the connection will be closed",
                                          sizeof("the connection will be closed") - 1U));
        }
        return sl_status_ok();
    }
    if (connection->accumulation_length > total_needed) {
        server->pipelining_attempts += 1U;
        (void)sl_http_request_fail(&connection->request, NULL);
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_PIPELINING_UNSUPPORTED, SL_STATUS_UNSUPPORTED,
            sl_http_transport_literal("HTTP pipelining is not supported",
                                      sizeof("HTTP pipelining is not supported") - 1U),
            sl_http_transport_literal("send the next request after the response completes",
                                      sizeof("send the next request after the response "
                                             "completes") -
                                          1U));
    }

    sl_http_transport_stop_timer(&connection->platform->body_timer,
                                 &connection->platform->body_timer_initialized);
    return sl_http_transport_parse_accumulated(connection, out_diag);
}

static void sl_http_transport_overflow_close_cb(uv_handle_t* handle)
{
    SlHttpPlatformListener* platform = (SlHttpPlatformListener*)handle->data;

    if (platform != NULL) {
        platform->overflow_initialized = false;
    }
}

static SlHttpTransportConnection* sl_http_transport_claim_connection(SlHttpTransportServer* server)
{
    size_t index = 0U;

    if (server == NULL) {
        return NULL;
    }
    for (index = 0U; index < server->connection_capacity; index += 1U) {
        SlHttpTransportConnection* connection = &server->connections[index];
        if (connection->state == SL_HTTP_TRANSPORT_CONNECTION_STATE_EMPTY ||
            connection->state == SL_HTTP_TRANSPORT_CONNECTION_STATE_CLOSED)
        {
            SlHttpPlatformConnection* platform = connection->platform;
            if (platform == NULL) {
                return NULL;
            }
            unsigned char* request_storage = connection->request_storage;
            size_t request_storage_size = connection->request_storage_size;
            unsigned char* accumulation = connection->accumulation;
            size_t accumulation_capacity = connection->accumulation_capacity;
            unsigned char* response_storage = connection->response_storage;
            size_t response_storage_size = connection->response_storage_size;
            unsigned char* read_buffer = platform == NULL ? NULL : platform->read_buffer;
            size_t read_buffer_size = platform == NULL ? 0U : platform->read_buffer_size;
            unsigned char* tls_plain_read_buffer =
                platform == NULL ? NULL : platform->tls_plain_read_buffer;
            size_t tls_plain_read_buffer_size =
                platform == NULL ? 0U : platform->tls_plain_read_buffer_size;
            unsigned char* tls_handshake_write_buffer =
                platform == NULL ? NULL : platform->tls_handshake_write_buffer;
            size_t tls_handshake_write_buffer_size =
                platform == NULL ? 0U : platform->tls_handshake_write_buffer_size;
            unsigned char* tls_write_buffer = platform == NULL ? NULL : platform->tls_write_buffer;
            size_t tls_write_buffer_size = platform == NULL ? 0U : platform->tls_write_buffer_size;
            *connection = (SlHttpTransportConnection){0};
            connection->platform = platform;
            connection->request_storage = request_storage;
            connection->request_storage_size = request_storage_size;
            connection->accumulation = accumulation;
            connection->accumulation_capacity = accumulation_capacity;
            connection->response_storage = response_storage;
            connection->response_storage_size = response_storage_size;
            connection->platform->owner = connection;
            connection->platform->read_buffer = read_buffer;
            connection->platform->read_buffer_size = read_buffer_size;
            connection->platform->tls_plain_read_buffer = tls_plain_read_buffer;
            connection->platform->tls_plain_read_buffer_size = tls_plain_read_buffer_size;
            connection->platform->tls_handshake_write_buffer = tls_handshake_write_buffer;
            connection->platform->tls_handshake_write_buffer_size = tls_handshake_write_buffer_size;
            connection->platform->tls_write_buffer = tls_write_buffer;
            connection->platform->tls_write_buffer_size = tls_write_buffer_size;
            (void)sl_arena_init(&connection->request_arena, connection->request_storage,
                                connection->request_storage_size);
            (void)sl_byte_builder_init_fixed(&connection->accumulation_builder,
                                             connection->accumulation,
                                             connection->accumulation_capacity);
            connection->slot_claimed = true;
            return connection;
        }
    }
    return NULL;
}

static void sl_http_transport_reject_pending(uv_stream_t* listener,
                                             SlHttpPlatformListener* platform)
{
    if (platform == NULL || platform->overflow_initialized) {
        return;
    }
    if (uv_tcp_init(&platform->loop, &platform->overflow) != 0) {
        return;
    }
    platform->overflow_initialized = true;
    platform->overflow.data = platform;
    if (uv_accept(listener, (uv_stream_t*)&platform->overflow) != 0) {
        platform->overflow_initialized = false;
        return;
    }
    uv_close((uv_handle_t*)&platform->overflow, sl_http_transport_overflow_close_cb);
}

static void sl_http_transport_alloc_read(uv_handle_t* handle, size_t suggested_size,
                                         uv_buf_t* out_buffer)
{
    SlHttpPlatformConnection* platform =
        handle == NULL ? NULL : (SlHttpPlatformConnection*)handle->data;

    (void)suggested_size;
    if (out_buffer == NULL) {
        return;
    }
    if (platform == NULL || platform->read_buffer == NULL || platform->read_buffer_size == 0U) {
        *out_buffer = uv_buf_init(NULL, 0U);
        return;
    }
    *out_buffer =
        uv_buf_init((char*)platform->read_buffer, (unsigned int)platform->read_buffer_size);
}

static void sl_http_transport_fail_and_close(SlHttpTransportConnection* connection, SlDiag* diag)
{
    uint16_t status_code = 500U;
    SlDiag write_diag = {0};

    if (connection == NULL) {
        return;
    }
    sl_http_transport_store_diag(connection, diag);
    if (connection->request_started && connection->request.state != SL_HTTP_REQUEST_STATE_CLOSED &&
        connection->request.state != SL_HTTP_REQUEST_STATE_FAILED &&
        connection->request.state != SL_HTTP_REQUEST_STATE_CANCELLED &&
        connection->request.state != SL_HTTP_REQUEST_STATE_TIMED_OUT)
    {
        (void)sl_http_request_fail(&connection->request, NULL);
    }
    if (connection->http2_mode) {
        if (connection->http2_dispatcher_started && connection->http2_dispatcher != NULL &&
            connection->platform != NULL && connection->platform->initialized &&
            !connection->platform->closing && !connection->platform->writing)
        {
            connection->close_after_write = true;
            if (sl_status_is_ok(sl_http2_session_submit_goaway(
                    &connection->http2_dispatcher->session, 0, SL_HTTP2_ERROR_PROTOCOL_ERROR)) &&
                sl_status_is_ok(sl_http_transport_http2_flush_output(connection, &write_diag)) &&
                connection->platform->writing)
            {
                return;
            }
        }
        (void)sl_http_connection_fail(&connection->core, NULL);
        (void)sl_http_transport_connection_close(connection, NULL);
        return;
    }
    if (connection->platform != NULL && connection->platform->initialized &&
        !connection->platform->closing && !connection->platform->writing &&
        connection->response_storage != NULL && connection->response_storage_size != 0U)
    {
        status_code = sl_http_transport_status_for_failure(
            sl_status_from_code(diag == NULL ? SL_STATUS_INTERNAL : SL_STATUS_INVALID_ARGUMENT),
            diag);
        if (sl_status_is_ok(
                sl_http_transport_write_error_response(connection, status_code, &write_diag)))
        {
            return;
        }
    }
    (void)sl_http_transport_connection_close(connection, NULL);
}

static void sl_http_transport_disconnect_and_close(SlHttpTransportConnection* connection,
                                                   SlDiag* diag, SlStr cancel_message)
{
    if (connection == NULL) {
        return;
    }
    sl_http_transport_store_diag(connection, diag);
    if (connection->request_started && connection->request.state != SL_HTTP_REQUEST_STATE_CLOSED &&
        connection->request.state != SL_HTTP_REQUEST_STATE_COMPLETED &&
        connection->request.state != SL_HTTP_REQUEST_STATE_CANCELLED &&
        connection->request.state != SL_HTTP_REQUEST_STATE_TIMED_OUT &&
        connection->request.state != SL_HTTP_REQUEST_STATE_FAILED)
    {
        (void)sl_http_request_cancel(&connection->request, SL_CANCELLATION_REASON_CANCELLED,
                                     cancel_message, NULL);
    }
    (void)sl_http_transport_connection_close(connection, NULL);
}

static SlStatus sl_http_transport_tls_feed(SlHttpTransportConnection* connection, SlBytes bytes,
                                           SlDiag* out_diag)
{
    SlHttpPlatformConnection* platform = connection == NULL ? NULL : connection->platform;
    BIO* read_bio = NULL;
    int written = 0;
    int rc = 0;

    if (connection == NULL || platform == NULL || platform->tls_ssl == NULL ||
        (bytes.ptr == NULL && bytes.length != 0U) || bytes.length > (size_t)INT_MAX)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    read_bio = SSL_get_rbio(platform->tls_ssl);
    if (read_bio == NULL) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_TLS_HANDSHAKE_FAILED, SL_STATUS_INTERNAL,
            sl_http_transport_literal("HTTP TLS input buffer is unavailable",
                                      sizeof("HTTP TLS input buffer is unavailable") - 1U),
            sl_http_transport_literal("OpenSSL handles stay inside the platform transport",
                                      sizeof("OpenSSL handles stay inside the platform transport") -
                                          1U));
    }
    written = BIO_write(read_bio, bytes.ptr, (int)bytes.length);
    if (written <= 0 || (size_t)written != bytes.length) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_TLS_HANDSHAKE_FAILED, SL_STATUS_INTERNAL,
            sl_http_transport_literal("HTTP TLS encrypted input could not be buffered",
                                      sizeof("HTTP TLS encrypted input could not be buffered") -
                                          1U),
            sl_http_transport_literal("OpenSSL details stay inside the platform boundary",
                                      sizeof("OpenSSL details stay inside the platform boundary") -
                                          1U));
    }

    if (!platform->tls_handshake_complete) {
        ERR_clear_error();
        rc = SSL_accept(platform->tls_ssl);
        if (rc == 1) {
            const unsigned char* selected = NULL;
            unsigned int selected_length = 0U;
            platform->tls_handshake_complete = true;
            connection->core.scheme = sl_str_from_cstr("https");
            SSL_get0_alpn_selected(platform->tls_ssl, &selected, &selected_length);
            platform->tls_alpn_h2 = selected_length == 2U && selected != NULL &&
                                    selected[0] == (unsigned char)'h' &&
                                    selected[1] == (unsigned char)'2';
        }
        else {
            int error = SSL_get_error(platform->tls_ssl, rc);
            SlStatus drain_status = sl_http_transport_tls_drain_handshake(connection, out_diag);
            if (!sl_status_is_ok(drain_status)) {
                return drain_status;
            }
            if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
                return sl_status_ok();
            }
            return sl_http_transport_connection_diag(
                connection, out_diag, SL_DIAG_HTTP_TLS_HANDSHAKE_FAILED, SL_STATUS_INVALID_ARGUMENT,
                sl_http_transport_literal("HTTP TLS handshake failed",
                                          sizeof("HTTP TLS handshake failed") - 1U),
                sl_http_transport_literal("handshake diagnostics redact OpenSSL and certificate "
                                          "details",
                                          sizeof("handshake diagnostics redact OpenSSL and "
                                                 "certificate details") -
                                              1U));
        }
        {
            SlStatus drain_status = sl_http_transport_tls_drain_handshake(connection, out_diag);
            if (!sl_status_is_ok(drain_status)) {
                return drain_status;
            }
        }
    }

    for (;;) {
        int read_count = 0;
        if (platform->tls_plain_read_buffer_size > (size_t)INT_MAX) {
            return sl_http_transport_connection_diag(
                connection, out_diag, SL_DIAG_HTTP_TLS_HANDSHAKE_FAILED,
                SL_STATUS_CAPACITY_EXCEEDED,
                sl_http_transport_literal("HTTP TLS plaintext buffer exceeds transport capacity",
                                          sizeof("HTTP TLS plaintext buffer exceeds transport "
                                                 "capacity") -
                                              1U),
                sl_http_transport_literal("TLS plaintext reads stay bounded by platform limits",
                                          sizeof("TLS plaintext reads stay bounded by platform "
                                                 "limits") -
                                              1U));
        }
        read_count = SSL_read(platform->tls_ssl, platform->tls_plain_read_buffer,
                              (int)platform->tls_plain_read_buffer_size);
        if (read_count > 0) {
            SlStatus status = sl_http_transport_connection_feed_test(
                connection,
                sl_bytes_from_parts(platform->tls_plain_read_buffer, (size_t)read_count), out_diag);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            continue;
        }
        {
            int error = SSL_get_error(platform->tls_ssl, read_count);
            if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
                return sl_status_ok();
            }
            if (error == SSL_ERROR_ZERO_RETURN) {
                return sl_http_transport_connection_diag(
                    connection, out_diag, SL_DIAG_HTTP_CONNECTION_CLOSED, SL_STATUS_CANCELLED,
                    sl_http_transport_literal("HTTP TLS client closed the connection",
                                              sizeof("HTTP TLS client closed the connection") - 1U),
                    sl_http_transport_literal("request cleanup still runs after TLS close",
                                              sizeof("request cleanup still runs after TLS close") -
                                                  1U));
            }
        }
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_TLS_HANDSHAKE_FAILED, SL_STATUS_INVALID_ARGUMENT,
            sl_http_transport_literal("HTTP TLS record processing failed",
                                      sizeof("HTTP TLS record processing failed") - 1U),
            sl_http_transport_literal("OpenSSL details stay inside the platform boundary",
                                      sizeof("OpenSSL details stay inside the platform boundary") -
                                          1U));
    }
}

static SlStatus sl_http_transport_tls_start_shutdown(SlHttpTransportConnection* connection,
                                                     SlDiag* out_diag, bool* out_queued)
{
    SlHttpPlatformConnection* platform = connection == NULL ? NULL : connection->platform;
    BIO* write_bio = NULL;
    uv_buf_t buffer;
    int pending = 0;
    int read_count = 0;
    int rc = 0;

    if (out_queued != NULL) {
        *out_queued = false;
    }
    if (connection == NULL || platform == NULL || !platform->tls_enabled ||
        !platform->tls_handshake_complete || platform->tls_ssl == NULL)
    {
        return sl_status_ok();
    }
    if (platform->tls_writing) {
        return sl_status_ok();
    }

    ERR_clear_error();
    rc = SSL_shutdown(platform->tls_ssl);
    if (rc < 0) {
        int error = SSL_get_error(platform->tls_ssl, rc);
        if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) {
            return sl_http_transport_connection_diag(
                connection, out_diag, SL_DIAG_HTTP_TLS_SHUTDOWN_FAILED, SL_STATUS_INTERNAL,
                sl_http_transport_literal("HTTP TLS shutdown failed",
                                          sizeof("HTTP TLS shutdown failed") - 1U),
                sl_http_transport_literal("OpenSSL details stay inside the platform boundary",
                                          sizeof("OpenSSL details stay inside the platform "
                                                 "boundary") -
                                              1U));
        }
    }

    write_bio = SSL_get_wbio(platform->tls_ssl);
    pending = write_bio == NULL ? 0 : BIO_pending(write_bio);
    if (pending <= 0) {
        return sl_status_ok();
    }
    if ((size_t)pending > platform->tls_handshake_write_buffer_size) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_TLS_SHUTDOWN_FAILED, SL_STATUS_CAPACITY_EXCEEDED,
            sl_http_transport_literal("HTTP TLS shutdown bytes exceed transport buffer",
                                      sizeof("HTTP TLS shutdown bytes exceed transport buffer") -
                                          1U),
            sl_http_transport_literal("TLS close output must fit configured response caps",
                                      sizeof("TLS close output must fit configured response "
                                             "caps") -
                                          1U));
    }
    read_count = BIO_read(write_bio, platform->tls_handshake_write_buffer, pending);
    if (read_count <= 0) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_TLS_SHUTDOWN_FAILED, SL_STATUS_INTERNAL,
            sl_http_transport_literal("HTTP TLS shutdown output failed",
                                      sizeof("HTTP TLS shutdown output failed") - 1U),
            sl_http_transport_literal("OpenSSL details stay inside the platform boundary",
                                      sizeof("OpenSSL details stay inside the platform boundary") -
                                          1U));
    }

    buffer = uv_buf_init((char*)platform->tls_handshake_write_buffer, (unsigned int)read_count);
    platform->tls_write.data = platform;
    platform->tls_writing = true;
    platform->tls_shutdown_writing = true;
    platform->closing = true;
    if (uv_write(&platform->tls_write, (uv_stream_t*)&platform->handle, &buffer, 1U,
                 sl_http_transport_tls_shutdown_write_cb) != 0)
    {
        platform->tls_writing = false;
        platform->tls_shutdown_writing = false;
        platform->closing = false;
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_TLS_SHUTDOWN_FAILED, SL_STATUS_INTERNAL,
            sl_http_transport_literal("HTTP TLS shutdown write failed",
                                      sizeof("HTTP TLS shutdown write failed") - 1U),
            sl_http_transport_literal("socket and OpenSSL handles stay inside the platform "
                                      "boundary",
                                      sizeof("socket and OpenSSL handles stay inside the platform "
                                             "boundary") -
                                          1U));
    }
    if (out_queued != NULL) {
        *out_queued = true;
    }
    return sl_status_ok();
}

static void sl_http_transport_on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buffer)
{
    SlHttpPlatformConnection* platform =
        stream == NULL ? NULL : (SlHttpPlatformConnection*)stream->data;
    SlHttpTransportConnection* connection = platform == NULL ? NULL : platform->owner;
    SlDiag diag = {0};
    SlStatus status;

    (void)buffer;
    if (connection == NULL) {
        return;
    }
    if (nread > 0) {
        if (platform->tls_enabled) {
            status = sl_http_transport_tls_feed(
                connection, sl_bytes_from_parts(platform->read_buffer, (size_t)nread), &diag);
        }
        else {
            status = sl_http_transport_connection_feed_test(
                connection, sl_bytes_from_parts(platform->read_buffer, (size_t)nread), &diag);
        }
        if (!sl_status_is_ok(status)) {
            if (diag.code == SL_DIAG_HTTP_CONNECTION_CLOSED &&
                sl_status_code(status) == SL_STATUS_CANCELLED)
            {
                sl_http_transport_disconnect_and_close(
                    connection, &diag,
                    sl_http_transport_literal("HTTP TLS client disconnected",
                                              sizeof("HTTP TLS client disconnected") - 1U));
                return;
            }
            sl_http_transport_fail_and_close(connection, &diag);
        }
        return;
    }
    if (nread == 0) {
        return;
    }

    if (nread == UV_EOF) {
        SlDiagCode code = SL_DIAG_HTTP_CONNECTION_CLOSED;
        SlStatusCode status_code = SL_STATUS_CANCELLED;
        SlStr message = connection->state == SL_HTTP_TRANSPORT_CONNECTION_STATE_READING_BODY
                            ? sl_http_transport_literal(
                                  "HTTP client disconnected during request body",
                                  sizeof("HTTP client disconnected during request body") - 1U)
                            : sl_http_transport_literal(
                                  "HTTP client disconnected during request head",
                                  sizeof("HTTP client disconnected during request head") - 1U);
        status = sl_http_transport_connection_diag(
            connection, &diag, code, status_code, message,
            sl_http_transport_literal("request cleanup still runs after disconnect",
                                      sizeof("request cleanup still runs after disconnect") - 1U));
        (void)status;
        sl_http_transport_disconnect_and_close(
            connection, &diag,
            sl_http_transport_literal("HTTP client disconnected",
                                      sizeof("HTTP client disconnected") - 1U));
        return;
    }

    status = sl_http_transport_connection_diag(
        connection, &diag, SL_DIAG_HTTP_CONNECTION_CLOSED, SL_STATUS_INTERNAL,
        sl_http_transport_literal("HTTP transport read failed",
                                  sizeof("HTTP transport read failed") - 1U),
        sl_http_transport_literal("socket details stay inside the platform boundary",
                                  sizeof("socket details stay inside the platform boundary") - 1U));
    (void)status;
    sl_http_transport_fail_and_close(connection, &diag);
}

static SlStatus sl_http_transport_start_read(SlHttpTransportConnection* connection,
                                             SlDiag* out_diag)
{
    SlHttpTransportServer* server = sl_http_transport_connection_server(connection);
    SlStatus status;
    int rc = 0;

    if (connection == NULL || connection->platform == NULL || !connection->platform->initialized) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (connection->state != SL_HTTP_TRANSPORT_CONNECTION_STATE_ACCEPTED) {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_APP_LIFECYCLE, SL_STATUS_INVALID_STATE,
            sl_http_transport_literal("HTTP transport connection state is invalid",
                                      sizeof("HTTP transport connection state is invalid") - 1U),
            sl_http_transport_literal("start reads only after a connection is accepted",
                                      sizeof("start reads only after a connection is accepted") -
                                          1U));
    }
    rc = uv_read_start((uv_stream_t*)&connection->platform->handle, sl_http_transport_alloc_read,
                       sl_http_transport_on_read);
    if (rc != 0) {
        return sl_http_transport_uv_status(
            rc, out_diag, SL_DIAG_HTTP_CONNECTION_CLOSED,
            sl_http_transport_literal("HTTP transport read start failed",
                                      sizeof("HTTP transport read start failed") - 1U));
    }
    connection->platform->reading = true;
    connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_READING_HEAD;
    if (server != NULL) {
        status = sl_http_transport_start_timer(
            connection->platform, &connection->platform->header_timer,
            &connection->platform->header_timer_initialized, server->config.header_read_timeout_ms,
            sl_http_transport_header_timeout_cb);
        if (!sl_status_is_ok(status)) {
            return sl_http_transport_connection_diag(
                connection, out_diag, SL_DIAG_HTTP_REQUEST_TIMEOUT, sl_status_code(status),
                sl_http_transport_literal("HTTP transport header timeout could not start",
                                          sizeof("HTTP transport header timeout could not start") -
                                              1U),
                sl_http_transport_literal("the connection will be closed",
                                          sizeof("the connection will be closed") - 1U));
        }
        status = sl_http_transport_start_timer(
            connection->platform, &connection->platform->request_timer,
            &connection->platform->request_timer_initialized, server->config.request_timeout_ms,
            sl_http_transport_request_timeout_cb);
        if (!sl_status_is_ok(status)) {
            return sl_http_transport_connection_diag(
                connection, out_diag, SL_DIAG_HTTP_REQUEST_TIMEOUT, sl_status_code(status),
                sl_http_transport_literal("HTTP transport request timeout could not start",
                                          sizeof("HTTP transport request timeout could not start") -
                                              1U),
                sl_http_transport_literal("the connection will be closed",
                                          sizeof("the connection will be closed") - 1U));
        }
    }
    return sl_status_ok();
}

static void sl_http_transport_on_connection(uv_stream_t* listener, int status)
{
    SlHttpPlatformListener* platform = NULL;
    SlHttpTransportServer* server = NULL;
    SlHttpTransportConnection* connection = NULL;
    SlStatus accept_status;

    if (listener == NULL) {
        return;
    }
    platform = (SlHttpPlatformListener*)listener->data;
    server = platform == NULL ? NULL : platform->server;
    if (server == NULL) {
        return;
    }
    if (status < 0) {
        server->accept_failures += 1U;
        return;
    }
    if (server->state == SL_HTTP_TRANSPORT_SERVER_STATE_STOPPING ||
        server->state == SL_HTTP_TRANSPORT_SERVER_STATE_STOPPED ||
        server->backend.state == SL_HTTP_BACKEND_STATE_STOPPING ||
        server->backend.state == SL_HTTP_BACKEND_STATE_STOPPED)
    {
        server->rejected_connections += 1U;
        sl_http_transport_reject_pending(listener, platform);
        return;
    }

    connection = sl_http_transport_claim_connection(server);
    if (connection == NULL) {
        server->rejected_connections += 1U;
        sl_http_transport_reject_pending(listener, platform);
        return;
    }

    accept_status = sl_http_backend_accept_connection(&server->backend, &connection->core, NULL);
    if (!sl_status_is_ok(accept_status)) {
        connection->slot_claimed = false;
        connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_CLOSED;
        server->rejected_connections += 1U;
        sl_http_transport_reject_pending(listener, platform);
        return;
    }

    if (uv_tcp_init(&platform->loop, &connection->platform->handle) != 0) {
        (void)sl_http_connection_fail(&connection->core, NULL);
        connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_ERROR;
        server->accept_failures += 1U;
        return;
    }

    connection->platform->initialized = true;
    connection->platform->closing = false;
    connection->platform->handle.data = connection->platform;
    if (uv_accept(listener, (uv_stream_t*)&connection->platform->handle) != 0) {
        (void)sl_http_connection_fail(&connection->core, NULL);
        connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_ERROR;
        connection->platform->closing = true;
        uv_close((uv_handle_t*)&connection->platform->handle,
                 sl_http_transport_connection_close_cb);
        server->accept_failures += 1U;
        return;
    }

    accept_status = sl_http_transport_tls_attach(connection, NULL);
    if (!sl_status_is_ok(accept_status)) {
        (void)sl_http_connection_fail(&connection->core, NULL);
        connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_ERROR;
        (void)sl_http_transport_connection_close(connection, NULL);
        server->accept_failures += 1U;
        return;
    }

    connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_ACCEPTED;
    server->accepted_connections += 1U;
    if (!sl_status_is_ok(sl_http_transport_start_read(connection, NULL))) {
        (void)sl_http_connection_fail(&connection->core, NULL);
        connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_ERROR;
        (void)sl_http_transport_connection_close(connection, NULL);
        server->accept_failures += 1U;
    }
}

static void sl_http_transport_close_listener(SlHttpPlatformListener* platform)
{
    if (platform == NULL) {
        return;
    }
    if (platform->listener_initialized && !uv_is_closing((uv_handle_t*)&platform->listener)) {
        uv_close((uv_handle_t*)&platform->listener, NULL);
    }
    if (platform->overflow_initialized && !uv_is_closing((uv_handle_t*)&platform->overflow)) {
        uv_close((uv_handle_t*)&platform->overflow, sl_http_transport_overflow_close_cb);
    }
    if (platform->loop_initialized) {
        while (uv_run(&platform->loop, UV_RUN_DEFAULT) != 0) {
        }
    }
    platform->listener_initialized = false;
}

SlStatus sl_http_transport_server_init(SlHttpTransportServer* server, SlArena* arena,
                                       const SlHttpTransportConfig* config, SlDiag* out_diag)
{
    SlHttpBackendOptions backend_options = {0};
    SlSlice connection_storage = {0};
    SlSlice platform_connection_storage = {0};
    SlStatus status;
    void* memory = NULL;
    size_t accumulation_capacity = 0U;
    size_t chunked_wire_body_capacity = 0U;
    size_t body_accumulation_capacity = 0U;
    size_t accumulation_bytes = 0U;
    size_t request_storage_bytes = 0U;
    size_t read_buffer_bytes = 0U;
    size_t response_storage_bytes = 0U;
    size_t tls_write_buffer_size = 0U;

    sl_http_transport_clear_diag(out_diag);
    if (server == NULL || arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *server = (SlHttpTransportServer){0};
    server->arena = arena;

    status = sl_http_transport_normalize_config(config, &server->config, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_str_copy_to_arena_cstr(arena, server->config.host, &server->host);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_http_transport_alloc(arena, sizeof(SlHttpPlatformListener),
                                     _Alignof(SlHttpPlatformListener), &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    server->platform = (SlHttpPlatformListener*)memory;
    *server->platform = (SlHttpPlatformListener){0};
    server->platform->server = server;

    if (server->config.tls.enabled) {
        status = sl_str_copy_to_arena_cstr(arena, server->config.tls.certificate_path,
                                           &server->platform->tls_certificate_path);
        if (!sl_status_is_ok(status)) {
            return sl_http_transport_diag(
                out_diag, SL_DIAG_HTTP_TLS_CONFIG, sl_status_code(status),
                sl_http_transport_literal("HTTP TLS certificate path is invalid",
                                          sizeof("HTTP TLS certificate path is invalid") - 1U),
                sl_http_transport_literal("TLS paths must not contain embedded NUL bytes",
                                          sizeof("TLS paths must not contain embedded NUL bytes") -
                                              1U));
        }
        status = sl_str_copy_to_arena_cstr(arena, server->config.tls.private_key_path,
                                           &server->platform->tls_private_key_path);
        if (!sl_status_is_ok(status)) {
            return sl_http_transport_diag(
                out_diag, SL_DIAG_HTTP_TLS_CONFIG, sl_status_code(status),
                sl_http_transport_literal("HTTP TLS private key path is invalid",
                                          sizeof("HTTP TLS private key path is invalid") - 1U),
                sl_http_transport_literal("TLS paths must not contain embedded NUL bytes",
                                          sizeof("TLS paths must not contain embedded NUL bytes") -
                                              1U));
        }
        if (!sl_str_is_empty(server->config.tls.passphrase)) {
            status = sl_str_copy_to_arena_cstr(arena, server->config.tls.passphrase,
                                               &server->platform->tls_passphrase);
            if (!sl_status_is_ok(status)) {
                return sl_http_transport_diag(
                    out_diag, SL_DIAG_HTTP_TLS_CONFIG, sl_status_code(status),
                    sl_http_transport_literal("HTTP TLS passphrase is invalid",
                                              sizeof("HTTP TLS passphrase is invalid") - 1U),
                    sl_http_transport_literal("TLS passphrase material is redacted",
                                              sizeof("TLS passphrase material is redacted") - 1U));
            }
        }
        server->config.tls.certificate_path = sl_str_empty();
        server->config.tls.private_key_path = sl_str_empty();
        server->config.tls.passphrase = sl_str_empty();
    }

    backend_options.max_connections = server->config.max_connections;
    backend_options.max_active_requests = server->config.max_active_requests;
    backend_options.parse = server->config.parse;
    backend_options.read_timeout_ms = server->config.body_read_timeout_ms;
    backend_options.header_timeout_ms = server->config.header_read_timeout_ms;
    backend_options.request_timeout_ms = server->config.request_timeout_ms;
    backend_options.keep_alive_enabled = !server->config.keep_alive_disabled;
    status = sl_http_backend_init(&server->backend, &backend_options, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_arena_array_alloc(arena, server->config.connection_capacity,
                                  sizeof(SlHttpTransportConnection),
                                  _Alignof(SlHttpTransportConnection), &connection_storage);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    server->connections = (SlHttpTransportConnection*)connection_storage.ptr;

    status = sl_arena_array_alloc(arena, server->config.connection_capacity,
                                  sizeof(SlHttpPlatformConnection),
                                  _Alignof(SlHttpPlatformConnection), &platform_connection_storage);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    server->platform_connections = (SlHttpPlatformConnection*)platform_connection_storage.ptr;
    server->connection_capacity = server->config.connection_capacity;

    status = sl_checked_mul_size(server->backend.options.parse.max_body_length, 8U,
                                 &chunked_wire_body_capacity);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_checked_add_size(chunked_wire_body_capacity, 5U, &chunked_wire_body_capacity);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    body_accumulation_capacity =
        chunked_wire_body_capacity > server->backend.options.parse.max_body_length
            ? chunked_wire_body_capacity
            : server->backend.options.parse.max_body_length;
    status = sl_checked_add_size(server->config.max_request_head_bytes, body_accumulation_capacity,
                                 &accumulation_capacity);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_checked_mul_size(accumulation_capacity, server->connection_capacity,
                                 &accumulation_bytes);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_checked_mul_size(server->config.request_arena_bytes, server->connection_capacity,
                                 &request_storage_bytes);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_checked_mul_size(server->config.read_chunk_bytes, server->connection_capacity,
                                 &read_buffer_bytes);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_checked_mul_size(server->config.max_response_bytes, server->connection_capacity,
                                 &response_storage_bytes);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_checked_add_size(server->config.max_response_bytes, 4096U, &tls_write_buffer_size);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    (void)accumulation_bytes;
    (void)request_storage_bytes;
    (void)read_buffer_bytes;
    (void)response_storage_bytes;

    for (size_t index = 0U; index < server->connection_capacity; index += 1U) {
        server->connections[index] = (SlHttpTransportConnection){0};
        server->connections[index].platform = &server->platform_connections[index];
        if (accumulation_capacity != 0U) {
            status = sl_http_transport_alloc(arena, accumulation_capacity, 1U, &memory);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            server->connections[index].accumulation = (unsigned char*)memory;
            server->connections[index].accumulation_capacity = accumulation_capacity;
        }
        status = sl_http_transport_alloc(arena, server->config.max_response_bytes, 1U, &memory);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        server->connections[index].response_storage = (unsigned char*)memory;
        server->connections[index].response_storage_size = server->config.max_response_bytes;
        status = sl_http_transport_alloc(arena, server->config.request_arena_bytes, 1U, &memory);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        server->connections[index].request_storage = (unsigned char*)memory;
        server->connections[index].request_storage_size = server->config.request_arena_bytes;
        server->platform_connections[index] = (SlHttpPlatformConnection){0};
        status = sl_http_transport_alloc(arena, server->config.read_chunk_bytes, 1U, &memory);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        server->platform_connections[index].read_buffer = (unsigned char*)memory;
        server->platform_connections[index].read_buffer_size = server->config.read_chunk_bytes;
        if (server->config.tls.enabled) {
            status = sl_http_transport_alloc(arena, server->config.read_chunk_bytes, 1U, &memory);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            server->platform_connections[index].tls_plain_read_buffer = (unsigned char*)memory;
            server->platform_connections[index].tls_plain_read_buffer_size =
                server->config.read_chunk_bytes;
            status = sl_http_transport_alloc(arena, tls_write_buffer_size, 1U, &memory);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            server->platform_connections[index].tls_handshake_write_buffer = (unsigned char*)memory;
            server->platform_connections[index].tls_handshake_write_buffer_size =
                tls_write_buffer_size;
            status = sl_http_transport_alloc(arena, tls_write_buffer_size, 1U, &memory);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            server->platform_connections[index].tls_write_buffer = (unsigned char*)memory;
            server->platform_connections[index].tls_write_buffer_size = tls_write_buffer_size;
        }
    }

    server->state = SL_HTTP_TRANSPORT_SERVER_STATE_CREATED;
    return sl_status_ok();
}

SlStatus sl_http_transport_server_listen(SlHttpTransportServer* server, SlDiag* out_diag)
{
    struct sockaddr_in address;
    int rc = 0;
    SlStatus status;

    sl_http_transport_clear_diag(out_diag);
    if (server == NULL || server->platform == NULL || server->host.ptr == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (server->state == SL_HTTP_TRANSPORT_SERVER_STATE_LISTENING) {
        return sl_http_transport_invalid_state(out_diag);
    }
    if (server->state != SL_HTTP_TRANSPORT_SERVER_STATE_CREATED &&
        server->state != SL_HTTP_TRANSPORT_SERVER_STATE_STOPPED)
    {
        return sl_http_transport_invalid_state(out_diag);
    }

    rc = uv_ip4_addr(server->host.ptr, (int)server->config.port, &address);
    if (rc != 0) {
        server->state = SL_HTTP_TRANSPORT_SERVER_STATE_ERROR;
        return sl_http_transport_uv_status(
            rc, out_diag, SL_DIAG_HTTP_TRANSPORT_CONFIG,
            sl_http_transport_literal("HTTP transport bind address is invalid",
                                      sizeof("HTTP transport bind address is invalid") - 1U));
    }

    rc = uv_loop_init(&server->platform->loop);
    if (rc != 0) {
        server->state = SL_HTTP_TRANSPORT_SERVER_STATE_ERROR;
        return sl_http_transport_uv_status(
            rc, out_diag, SL_DIAG_HTTP_LISTEN_FAILED,
            sl_http_transport_literal("HTTP transport loop initialization failed",
                                      sizeof("HTTP transport loop initialization failed") - 1U));
    }
    server->platform->loop_initialized = true;

    rc = uv_tcp_init(&server->platform->loop, &server->platform->listener);
    if (rc != 0) {
        server->state = SL_HTTP_TRANSPORT_SERVER_STATE_ERROR;
        (void)uv_loop_close(&server->platform->loop);
        server->platform->loop_initialized = false;
        return sl_http_transport_uv_status(
            rc, out_diag, SL_DIAG_HTTP_BIND_FAILED,
            sl_http_transport_literal("HTTP transport listener initialization failed",
                                      sizeof("HTTP transport listener initialization failed") -
                                          1U));
    }
    server->platform->listener_initialized = true;
    server->platform->listener.data = server->platform;

    rc = uv_tcp_bind(&server->platform->listener, (const struct sockaddr*)&address, 0U);
    if (rc != 0) {
        server->state = SL_HTTP_TRANSPORT_SERVER_STATE_ERROR;
        sl_http_transport_close_listener(server->platform);
        (void)uv_loop_close(&server->platform->loop);
        server->platform->loop_initialized = false;
        return sl_http_transport_uv_status(
            rc, out_diag, SL_DIAG_HTTP_BIND_FAILED,
            sl_http_transport_literal("HTTP transport bind failed",
                                      sizeof("HTTP transport bind failed") - 1U));
    }

    status = sl_http_transport_tls_context_init(server, out_diag);
    if (!sl_status_is_ok(status)) {
        server->state = SL_HTTP_TRANSPORT_SERVER_STATE_ERROR;
        sl_http_transport_close_listener(server->platform);
        (void)uv_loop_close(&server->platform->loop);
        server->platform->loop_initialized = false;
        return status;
    }

    rc = uv_listen((uv_stream_t*)&server->platform->listener, server->config.backlog,
                   sl_http_transport_on_connection);
    if (rc != 0) {
        server->state = SL_HTTP_TRANSPORT_SERVER_STATE_ERROR;
        sl_http_transport_close_listener(server->platform);
        sl_http_transport_release_tls_context(server->platform);
        (void)uv_loop_close(&server->platform->loop);
        server->platform->loop_initialized = false;
        return sl_http_transport_uv_status(
            rc, out_diag, SL_DIAG_HTTP_LISTEN_FAILED,
            sl_http_transport_literal("HTTP transport listen failed",
                                      sizeof("HTTP transport listen failed") - 1U));
    }

    status = sl_http_backend_start(&server->backend, server->platform, out_diag);
    if (!sl_status_is_ok(status)) {
        server->state = SL_HTTP_TRANSPORT_SERVER_STATE_ERROR;
        sl_http_transport_close_listener(server->platform);
        (void)uv_loop_close(&server->platform->loop);
        server->platform->loop_initialized = false;
        return status;
    }

    {
        struct sockaddr_storage bound = {0};
        int length = (int)sizeof(bound);
        if (uv_tcp_getsockname(&server->platform->listener, (struct sockaddr*)&bound, &length) == 0)
        {
            const struct sockaddr_in* in = (const struct sockaddr_in*)&bound;
            server->platform->bound_port = (uint32_t)ntohs(in->sin_port);
        }
        else {
            server->platform->bound_port = server->config.port;
        }
    }

    server->state = SL_HTTP_TRANSPORT_SERVER_STATE_LISTENING;
    return sl_status_ok();
}

SlStatus sl_http_transport_server_poll(SlHttpTransportServer* server, SlDiag* out_diag)
{
    size_t previous_accept_failures = 0U;

    sl_http_transport_clear_diag(out_diag);
    if (server == NULL || server->platform == NULL || !server->platform->loop_initialized) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (server->state != SL_HTTP_TRANSPORT_SERVER_STATE_LISTENING &&
        server->state != SL_HTTP_TRANSPORT_SERVER_STATE_STOPPING)
    {
        return sl_http_transport_invalid_state(out_diag);
    }
    previous_accept_failures = server->accept_failures;
    (void)uv_run(&server->platform->loop, UV_RUN_NOWAIT);
    if (server->accept_failures != previous_accept_failures) {
        return sl_http_transport_diag(
            out_diag, SL_DIAG_HTTP_ACCEPT_FAILED, SL_STATUS_INTERNAL,
            sl_http_transport_literal("HTTP transport accept failed",
                                      sizeof("HTTP transport accept failed") - 1U),
            sl_http_transport_literal("the pending connection was not admitted",
                                      sizeof("the pending connection was not admitted") - 1U));
    }
    return sl_status_ok();
}

SlStatus sl_http_transport_server_run(SlHttpTransportServer* server, SlDiag* out_diag)
{
    int rc = 0;
    size_t previous_accept_failures = 0U;

    sl_http_transport_clear_diag(out_diag);
    if (server == NULL || server->platform == NULL || !server->platform->loop_initialized) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (server->state != SL_HTTP_TRANSPORT_SERVER_STATE_LISTENING &&
        server->state != SL_HTTP_TRANSPORT_SERVER_STATE_STOPPING)
    {
        return sl_http_transport_invalid_state(out_diag);
    }

    previous_accept_failures = server->accept_failures;
    rc = uv_run(&server->platform->loop, UV_RUN_DEFAULT);
    if (rc != 0 || server->accept_failures != previous_accept_failures) {
        return sl_http_transport_diag(
            out_diag, SL_DIAG_HTTP_ACCEPT_FAILED, SL_STATUS_INTERNAL,
            sl_http_transport_literal("HTTP transport run failed",
                                      sizeof("HTTP transport run failed") - 1U),
            sl_http_transport_literal("libuv details stay inside the platform transport boundary",
                                      sizeof("libuv details stay inside the platform transport "
                                             "boundary") -
                                          1U));
    }
    return sl_status_ok();
}

SlStatus sl_http_transport_connection_close(SlHttpTransportConnection* connection, SlDiag* out_diag)
{
    SlStr transfer_encoding = {0};
    bool tls_shutdown_queued = false;
    sl_http_transport_clear_diag(out_diag);
    if (connection == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (connection->state == SL_HTTP_TRANSPORT_CONNECTION_STATE_CLOSED ||
        connection->state == SL_HTTP_TRANSPORT_CONNECTION_STATE_EMPTY)
    {
        return sl_status_ok();
    }
    if (connection->state == SL_HTTP_TRANSPORT_CONNECTION_STATE_CLOSING &&
        connection->platform != NULL && connection->platform->tls_shutdown_writing)
    {
        return sl_status_ok();
    }
    if (connection->platform != NULL && connection->platform->initialized &&
        connection->platform->reading)
    {
        (void)uv_read_stop((uv_stream_t*)&connection->platform->handle);
        connection->platform->reading = false;
    }
    if (connection->platform != NULL && connection->platform->writing) {
        sl_http_transport_close_timer(&connection->platform->header_timer,
                                      &connection->platform->header_timer_initialized);
        sl_http_transport_close_timer(&connection->platform->body_timer,
                                      &connection->platform->body_timer_initialized);
        sl_http_transport_close_timer(&connection->platform->request_timer,
                                      &connection->platform->request_timer_initialized);
        sl_http_transport_close_timer(&connection->platform->write_timer,
                                      &connection->platform->write_timer_initialized);
        sl_http_transport_close_timer(&connection->platform->idle_timer,
                                      &connection->platform->idle_timer_initialized);
        if (connection->request_started &&
            !sl_http_transport_request_terminal(connection->request.state))
        {
            (void)sl_http_request_shutdown(&connection->request, NULL);
        }
        connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_CLOSING;
        return sl_status_ok();
    }
    sl_http_transport_close_connection_timers(connection->platform);
    if (connection->state == SL_HTTP_TRANSPORT_CONNECTION_STATE_READING_BODY &&
        connection->head_length != 0U &&
        sl_http_transport_head_header_value(
            sl_bytes_from_parts(connection->accumulation, connection->head_length),
            sl_str_from_cstr("Transfer-Encoding"), &transfer_encoding, NULL) &&
        sl_http_transport_transfer_encoding_is_chunked(transfer_encoding))
    {
        (void)sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_HTTP_CHUNK_FINAL_MISSING, SL_STATUS_INVALID_ARGUMENT,
            sl_http_transport_literal("HTTP chunked request ended before the final chunk",
                                      sizeof("HTTP chunked request ended before the final chunk") -
                                          1U),
            sl_http_transport_literal("chunked request bodies must end with 0 CRLF CRLF",
                                      sizeof("chunked request bodies must end with 0 CRLF CRLF") -
                                          1U));
    }
    if (connection->body_reader_started && !connection->body_reader_finished) {
        (void)sl_http_request_body_reader_close(&connection->body_reader, NULL);
    }
    if (connection->request_started) {
        if (connection->core.backend != NULL &&
            connection->core.backend->state == SL_HTTP_BACKEND_STATE_STOPPING &&
            connection->request.state != SL_HTTP_REQUEST_STATE_CLOSED &&
            connection->request.state != SL_HTTP_REQUEST_STATE_COMPLETED &&
            connection->request.state != SL_HTTP_REQUEST_STATE_CANCELLED &&
            connection->request.state != SL_HTTP_REQUEST_STATE_TIMED_OUT &&
            connection->request.state != SL_HTTP_REQUEST_STATE_FAILED)
        {
            (void)sl_http_request_shutdown(&connection->request, NULL);
        }
        else {
            (void)sl_http_request_close(&connection->request, NULL);
        }
    }
    if (connection->http2_dispatcher_started) {
        sl_http2_server_dispatcher_dispose(connection->http2_dispatcher);
        connection->http2_dispatcher = NULL;
        connection->http2_dispatcher_started = false;
        connection->http2_mode = false;
    }
    (void)sl_http_connection_close(&connection->core, out_diag);
    connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_CLOSING;
    if (connection->platform != NULL && connection->platform->initialized &&
        !connection->platform->closing)
    {
        SlStatus tls_shutdown_status =
            sl_http_transport_tls_start_shutdown(connection, out_diag, &tls_shutdown_queued);
        if (!sl_status_is_ok(tls_shutdown_status)) {
            sl_http_transport_store_diag(connection, out_diag);
        }
        if (tls_shutdown_queued) {
            connection->slot_claimed = false;
            return sl_status_ok();
        }
        connection->platform->closing = true;
        uv_close((uv_handle_t*)&connection->platform->handle,
                 sl_http_transport_connection_close_cb);
    }
    else {
        connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_CLOSED;
    }
    connection->slot_claimed = false;
    return sl_status_ok();
}

SlStatus sl_http_transport_connection_feed_test(SlHttpTransportConnection* connection,
                                                SlBytes bytes, SlDiag* out_diag)
{
    SlStatus status;
    bool http2_handled = false;

    sl_http_transport_clear_diag(out_diag);
    if (connection == NULL || (bytes.ptr == NULL && bytes.length != 0U)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (connection->http2_mode) {
        return sl_http_transport_http2_receive(connection, bytes, out_diag);
    }
    if (connection->state != SL_HTTP_TRANSPORT_CONNECTION_STATE_ACCEPTED &&
        connection->state != SL_HTTP_TRANSPORT_CONNECTION_STATE_KEEP_ALIVE_IDLE &&
        connection->state != SL_HTTP_TRANSPORT_CONNECTION_STATE_READING_HEAD &&
        connection->state != SL_HTTP_TRANSPORT_CONNECTION_STATE_READING_BODY)
    {
        return sl_http_transport_connection_diag(
            connection, out_diag, SL_DIAG_APP_LIFECYCLE, SL_STATUS_INVALID_STATE,
            sl_http_transport_literal("HTTP transport connection state is invalid",
                                      sizeof("HTTP transport connection state is invalid") - 1U),
            sl_http_transport_literal(
                "request bytes can be read only before request-ready",
                sizeof("request bytes can be read only before request-ready") - 1U));
    }
    if (connection->state == SL_HTTP_TRANSPORT_CONNECTION_STATE_ACCEPTED) {
        connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_READING_HEAD;
    }
    if (connection->state == SL_HTTP_TRANSPORT_CONNECTION_STATE_KEEP_ALIVE_IDLE) {
        SlHttpTransportServer* server = sl_http_transport_connection_server(connection);
        sl_http_transport_stop_timer(&connection->platform->idle_timer,
                                     &connection->platform->idle_timer_initialized);
        connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_READING_HEAD;
        if (server != NULL) {
            status = sl_http_transport_start_timer(
                connection->platform, &connection->platform->header_timer,
                &connection->platform->header_timer_initialized,
                server->config.header_read_timeout_ms, sl_http_transport_header_timeout_cb);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            status = sl_http_transport_start_timer(
                connection->platform, &connection->platform->request_timer,
                &connection->platform->request_timer_initialized, server->config.request_timeout_ms,
                sl_http_transport_request_timeout_cb);
            if (!sl_status_is_ok(status)) {
                return status;
            }
        }
    }

    status = sl_http_transport_append_bytes(connection, bytes, out_diag);
    if (!sl_status_is_ok(status)) {
        connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_ERROR;
        return status;
    }
    status = sl_http_transport_maybe_start_http2(connection, &http2_handled, out_diag);
    if (!sl_status_is_ok(status)) {
        connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_ERROR;
        return status;
    }
    if (http2_handled) {
        return sl_status_ok();
    }
    status = sl_http_transport_try_complete_request(connection, out_diag);
    if (!sl_status_is_ok(status)) {
        connection->state = SL_HTTP_TRANSPORT_CONNECTION_STATE_ERROR;
        return status;
    }
    return sl_status_ok();
}

SlStatus sl_http_transport_server_stop(SlHttpTransportServer* server, SlDiag* out_diag)
{
    SlStatus status = sl_status_ok();

    sl_http_transport_clear_diag(out_diag);
    if (server == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (server->state == SL_HTTP_TRANSPORT_SERVER_STATE_CREATED ||
        server->state == SL_HTTP_TRANSPORT_SERVER_STATE_STOPPED)
    {
        server->state = SL_HTTP_TRANSPORT_SERVER_STATE_STOPPED;
        return sl_status_ok();
    }
    if (server->state != SL_HTTP_TRANSPORT_SERVER_STATE_LISTENING &&
        server->state != SL_HTTP_TRANSPORT_SERVER_STATE_STOPPING &&
        server->state != SL_HTTP_TRANSPORT_SERVER_STATE_ERROR)
    {
        return sl_http_transport_invalid_state(out_diag);
    }

    server->state = SL_HTTP_TRANSPORT_SERVER_STATE_STOPPING;
    if (server->backend.state == SL_HTTP_BACKEND_STATE_STARTED ||
        server->backend.state == SL_HTTP_BACKEND_STATE_STOPPING)
    {
        status = sl_http_backend_stop(&server->backend, out_diag);
    }

    for (size_t index = 0U; index < server->connection_capacity; index += 1U) {
        if (server->connections[index].state == SL_HTTP_TRANSPORT_CONNECTION_STATE_KEEP_ALIVE_IDLE)
        {
            server->shutdown_idle_closes += 1U;
        }
        (void)sl_http_transport_connection_close(&server->connections[index], NULL);
    }

    if (server->platform != NULL) {
        sl_http_transport_close_listener(server->platform);
        if (server->platform->loop_initialized) {
            (void)uv_loop_close(&server->platform->loop);
            server->platform->loop_initialized = false;
        }
    }
    server->state = SL_HTTP_TRANSPORT_SERVER_STATE_STOPPED;
    return status;
}

SlStatus sl_http_transport_server_dispose(SlHttpTransportServer* server, SlDiag* out_diag)
{
    SlStatus status;

    sl_http_transport_clear_diag(out_diag);
    if (server == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (server->state == SL_HTTP_TRANSPORT_SERVER_STATE_DISPOSED) {
        return sl_status_ok();
    }
    if (server->state == SL_HTTP_TRANSPORT_SERVER_STATE_LISTENING ||
        server->state == SL_HTTP_TRANSPORT_SERVER_STATE_CREATED ||
        server->state == SL_HTTP_TRANSPORT_SERVER_STATE_ERROR)
    {
        status = sl_http_transport_server_stop(server, out_diag);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    status = sl_http_backend_dispose(&server->backend, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (server->platform != NULL) {
        sl_http_transport_release_tls_context(server->platform);
    }
    server->state = SL_HTTP_TRANSPORT_SERVER_STATE_DISPOSED;
    return sl_status_ok();
}

SlHttpTransportServerState sl_http_transport_server_state(const SlHttpTransportServer* server)
{
    return server == NULL ? SL_HTTP_TRANSPORT_SERVER_STATE_NONE : server->state;
}

uint32_t sl_http_transport_server_bound_port(const SlHttpTransportServer* server)
{
    if (server == NULL || server->platform == NULL) {
        return 0U;
    }
    return server->platform->bound_port;
}

size_t sl_http_transport_server_active_connections(const SlHttpTransportServer* server)
{
    return server == NULL ? 0U : server->backend.active_connections;
}

/*
 * src/core/diagnostics.c
 *
 * Implements Sloppy's first structured diagnostics core: stable code names, severities,
 * source spans, bounded related spans/hints, an arena-copying builder, and deterministic
 * plain-text rendering.
 *
 * Safety invariants:
 * - diagnostics created through the builder copy text into caller-provided arena memory;
 * - related spans and hints are fixed-size arrays with named bounds;
 * - renderer output is computed before allocation so it performs one arena allocation;
 * - source-frame and JSON renderers stay deterministic and do not inspect the filesystem;
 * - no platform, V8, terminal, source-map, or localization behavior appears here.
 *
 * Tests: tests/unit/core/test_diagnostics.c.
 */
#include "sloppy/diagnostics.h"

#include "sloppy/builder.h"
#include "sloppy/checked_math.h"
#include "sloppy/container.h"

static SlStr sl_diag_literal(const char* ptr, size_t length)
{
    return sl_str_from_parts(ptr, length);
}

static SlStr sl_diag_http_client_code_name(SlDiagCode code)
{
    switch (code) {
    case SL_DIAG_HTTP_CLIENT_FEATURE_UNAVAILABLE:
        return sl_diag_literal("SLOPPY_E_HTTP_CLIENT_FEATURE_UNAVAILABLE",
                               sizeof("SLOPPY_E_HTTP_CLIENT_FEATURE_UNAVAILABLE") - 1U);
    case SL_DIAG_HTTP_CLIENT_INVALID_URL:
        return sl_diag_literal("SLOPPY_E_HTTP_CLIENT_INVALID_URL",
                               sizeof("SLOPPY_E_HTTP_CLIENT_INVALID_URL") - 1U);
    case SL_DIAG_HTTP_CLIENT_INVALID_OPTIONS:
        return sl_diag_literal("SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                               sizeof("SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS") - 1U);
    case SL_DIAG_HTTP_CLIENT_AMBIGUOUS_BODY:
        return sl_diag_literal("SLOPPY_E_HTTP_CLIENT_AMBIGUOUS_BODY",
                               sizeof("SLOPPY_E_HTTP_CLIENT_AMBIGUOUS_BODY") - 1U);
    case SL_DIAG_HTTP_CLIENT_BODY_CONSUMED:
        return sl_diag_literal("SLOPPY_E_HTTP_CLIENT_BODY_CONSUMED",
                               sizeof("SLOPPY_E_HTTP_CLIENT_BODY_CONSUMED") - 1U);
    case SL_DIAG_HTTP_CLIENT_RESPONSE_BODY_LIMIT:
        return sl_diag_literal("SLOPPY_E_HTTP_CLIENT_RESPONSE_BODY_LIMIT",
                               sizeof("SLOPPY_E_HTTP_CLIENT_RESPONSE_BODY_LIMIT") - 1U);
    case SL_DIAG_HTTP_CLIENT_REQUEST_BODY_LIMIT:
        return sl_diag_literal("SLOPPY_E_HTTP_CLIENT_REQUEST_BODY_LIMIT",
                               sizeof("SLOPPY_E_HTTP_CLIENT_REQUEST_BODY_LIMIT") - 1U);
    case SL_DIAG_HTTP_CLIENT_MALFORMED_RESPONSE:
        return sl_diag_literal("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                               sizeof("SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE") - 1U);
    case SL_DIAG_HTTP_CLIENT_INVALID_JSON:
        return sl_diag_literal("SLOPPY_E_HTTP_CLIENT_INVALID_JSON",
                               sizeof("SLOPPY_E_HTTP_CLIENT_INVALID_JSON") - 1U);
    case SL_DIAG_HTTP_CLIENT_CONNECT_FAILED:
        return sl_diag_literal("SLOPPY_E_HTTP_CLIENT_CONNECT_FAILED",
                               sizeof("SLOPPY_E_HTTP_CLIENT_CONNECT_FAILED") - 1U);
    case SL_DIAG_HTTP_CLIENT_DNS_FAILED:
        return sl_diag_literal("SLOPPY_E_HTTP_CLIENT_DNS_FAILED",
                               sizeof("SLOPPY_E_HTTP_CLIENT_DNS_FAILED") - 1U);
    case SL_DIAG_HTTP_CLIENT_TLS_BACKEND_UNAVAILABLE:
        return sl_diag_literal("SLOPPY_E_HTTP_CLIENT_TLS_BACKEND_UNAVAILABLE",
                               sizeof("SLOPPY_E_HTTP_CLIENT_TLS_BACKEND_UNAVAILABLE") - 1U);
    case SL_DIAG_HTTP_CLIENT_TLS_CERTIFICATE_VALIDATION_FAILED:
        return sl_diag_literal("SLOPPY_E_HTTP_CLIENT_TLS_CERTIFICATE_VALIDATION_FAILED",
                               sizeof("SLOPPY_E_HTTP_CLIENT_TLS_CERTIFICATE_VALIDATION_FAILED") -
                                   1U);
    case SL_DIAG_HTTP_CLIENT_TLS_HOSTNAME_MISMATCH:
        return sl_diag_literal("SLOPPY_E_HTTP_CLIENT_TLS_HOSTNAME_MISMATCH",
                               sizeof("SLOPPY_E_HTTP_CLIENT_TLS_HOSTNAME_MISMATCH") - 1U);
    case SL_DIAG_HTTP_CLIENT_REQUEST_TIMEOUT:
        return sl_diag_literal("SLOPPY_E_HTTP_CLIENT_REQUEST_TIMEOUT",
                               sizeof("SLOPPY_E_HTTP_CLIENT_REQUEST_TIMEOUT") - 1U);
    case SL_DIAG_HTTP_CLIENT_REQUEST_CANCELLED:
        return sl_diag_literal("SLOPPY_E_HTTP_CLIENT_REQUEST_CANCELLED",
                               sizeof("SLOPPY_E_HTTP_CLIENT_REQUEST_CANCELLED") - 1U);
    case SL_DIAG_HTTP_CLIENT_REDIRECT_LOOP:
        return sl_diag_literal("SLOPPY_E_HTTP_CLIENT_REDIRECT_LOOP",
                               sizeof("SLOPPY_E_HTTP_CLIENT_REDIRECT_LOOP") - 1U);
    case SL_DIAG_HTTP_CLIENT_MAX_REDIRECTS_EXCEEDED:
        return sl_diag_literal("SLOPPY_E_HTTP_CLIENT_MAX_REDIRECTS_EXCEEDED",
                               sizeof("SLOPPY_E_HTTP_CLIENT_MAX_REDIRECTS_EXCEEDED") - 1U);
    case SL_DIAG_HTTP_CLIENT_SENSITIVE_HEADER_STRIPPED:
        return sl_diag_literal("SLOPPY_E_HTTP_CLIENT_SENSITIVE_HEADER_STRIPPED",
                               sizeof("SLOPPY_E_HTTP_CLIENT_SENSITIVE_HEADER_STRIPPED") - 1U);
    case SL_DIAG_HTTP_CLIENT_POOL_EXHAUSTED:
        return sl_diag_literal("SLOPPY_E_HTTP_CLIENT_POOL_EXHAUSTED",
                               sizeof("SLOPPY_E_HTTP_CLIENT_POOL_EXHAUSTED") - 1U);
    case SL_DIAG_HTTP_CLIENT_STRICT_NETWORK_DENIED:
        return sl_diag_literal("SLOPPY_E_HTTP_CLIENT_STRICT_NETWORK_DENIED",
                               sizeof("SLOPPY_E_HTTP_CLIENT_STRICT_NETWORK_DENIED") - 1U);
    case SL_DIAG_HTTP_CLIENT_DYNAMIC_TARGET_METADATA:
        return sl_diag_literal("SLOPPY_W_HTTP_CLIENT_DYNAMIC_TARGET_METADATA",
                               sizeof("SLOPPY_W_HTTP_CLIENT_DYNAMIC_TARGET_METADATA") - 1U);
    default:
        return sl_diag_literal("SLOPPY_E_UNKNOWN", sizeof("SLOPPY_E_UNKNOWN") - 1U);
    }
}

static SlStr sl_diag_workers_code_name(SlDiagCode code)
{
    switch (code) {
    case SL_DIAG_WORKERS_FEATURE_UNAVAILABLE:
        return sl_diag_literal("SLOPPY_E_WORKERS_FEATURE_UNAVAILABLE",
                               sizeof("SLOPPY_E_WORKERS_FEATURE_UNAVAILABLE") - 1U);
    case SL_DIAG_BACKGROUND_SERVICE_START_FAILED:
        return sl_diag_literal("SLOPPY_E_BACKGROUND_SERVICE_START_FAILED",
                               sizeof("SLOPPY_E_BACKGROUND_SERVICE_START_FAILED") - 1U);
    case SL_DIAG_BACKGROUND_SERVICE_FAILED:
        return sl_diag_literal("SLOPPY_E_BACKGROUND_SERVICE_FAILED",
                               sizeof("SLOPPY_E_BACKGROUND_SERVICE_FAILED") - 1U);
    case SL_DIAG_WORK_QUEUE_FULL:
        return sl_diag_literal("SLOPPY_E_WORK_QUEUE_FULL", sizeof("SLOPPY_E_WORK_QUEUE_FULL") - 1U);
    case SL_DIAG_WORK_QUEUE_STOPPED:
        return sl_diag_literal("SLOPPY_E_WORK_QUEUE_STOPPED",
                               sizeof("SLOPPY_E_WORK_QUEUE_STOPPED") - 1U);
    case SL_DIAG_WORK_JOB_CANCELLED:
        return sl_diag_literal("SLOPPY_E_WORK_JOB_CANCELLED",
                               sizeof("SLOPPY_E_WORK_JOB_CANCELLED") - 1U);
    case SL_DIAG_WORK_JOB_TIMEOUT:
        return sl_diag_literal("SLOPPY_E_WORK_JOB_TIMEOUT",
                               sizeof("SLOPPY_E_WORK_JOB_TIMEOUT") - 1U);
    case SL_DIAG_WORK_JOB_FAILED:
        return sl_diag_literal("SLOPPY_E_WORK_JOB_FAILED", sizeof("SLOPPY_E_WORK_JOB_FAILED") - 1U);
    case SL_DIAG_WORK_RETRY_EXHAUSTED:
        return sl_diag_literal("SLOPPY_E_WORK_RETRY_EXHAUSTED",
                               sizeof("SLOPPY_E_WORK_RETRY_EXHAUSTED") - 1U);
    case SL_DIAG_WORKER_POOL_UNAVAILABLE:
        return sl_diag_literal("SLOPPY_E_WORKER_POOL_UNAVAILABLE",
                               sizeof("SLOPPY_E_WORKER_POOL_UNAVAILABLE") - 1U);
    case SL_DIAG_WORKER_BRIDGE_UNAVAILABLE:
        return sl_diag_literal("SLOPPY_E_WORKER_BRIDGE_UNAVAILABLE",
                               sizeof("SLOPPY_E_WORKER_BRIDGE_UNAVAILABLE") - 1U);
    case SL_DIAG_WORKER_POOL_SATURATED:
        return sl_diag_literal("SLOPPY_E_WORKER_POOL_SATURATED",
                               sizeof("SLOPPY_E_WORKER_POOL_SATURATED") - 1U);
    case SL_DIAG_WORKER_CRASHED:
        return sl_diag_literal("SLOPPY_E_WORKER_CRASHED", sizeof("SLOPPY_E_WORKER_CRASHED") - 1U);
    case SL_DIAG_WORKER_RESOURCE_LIMIT_EXCEEDED:
        return sl_diag_literal("SLOPPY_E_WORKER_RESOURCE_LIMIT_EXCEEDED",
                               sizeof("SLOPPY_E_WORKER_RESOURCE_LIMIT_EXCEEDED") - 1U);
    case SL_DIAG_WORKER_MESSAGE_SERIALIZATION_FAILED:
        return sl_diag_literal("SLOPPY_E_WORKER_MESSAGE_SERIALIZATION_FAILED",
                               sizeof("SLOPPY_E_WORKER_MESSAGE_SERIALIZATION_FAILED") - 1U);
    case SL_DIAG_WORKER_ISOLATE_STARTUP_FAILED:
        return sl_diag_literal("SLOPPY_E_WORKER_ISOLATE_STARTUP_FAILED",
                               sizeof("SLOPPY_E_WORKER_ISOLATE_STARTUP_FAILED") - 1U);
    case SL_DIAG_WORKER_UNSUPPORTED_PAYLOAD:
        return sl_diag_literal("SLOPPY_E_WORKER_UNSUPPORTED_PAYLOAD",
                               sizeof("SLOPPY_E_WORKER_UNSUPPORTED_PAYLOAD") - 1U);
    case SL_DIAG_WORKER_SHUTDOWN_CANCELLED:
        return sl_diag_literal("SLOPPY_E_WORKER_SHUTDOWN_CANCELLED",
                               sizeof("SLOPPY_E_WORKER_SHUTDOWN_CANCELLED") - 1U);
    case SL_DIAG_WORKER_STALE_HANDLE:
        return sl_diag_literal("SLOPPY_E_WORKER_STALE_HANDLE",
                               sizeof("SLOPPY_E_WORKER_STALE_HANDLE") - 1U);
    default:
        return sl_diag_literal("SLOPPY_E_UNKNOWN", sizeof("SLOPPY_E_UNKNOWN") - 1U);
    }
}

static bool sl_diag_is_workers_code(SlDiagCode code)
{
    return code >= SL_DIAG_WORKERS_FEATURE_UNAVAILABLE && code <= SL_DIAG_WORKER_STALE_HANDLE;
}

static SlStr sl_diag_http_transport_code_name(SlDiagCode code)
{
    switch (code) {
    case SL_DIAG_HTTP_TRANSPORT_CONFIG:
        return sl_diag_literal("SLOPPY_E_HTTP_TRANSPORT_CONFIG",
                               sizeof("SLOPPY_E_HTTP_TRANSPORT_CONFIG") - 1U);
    case SL_DIAG_HTTP_BIND_FAILED:
        return sl_diag_literal("SLOPPY_E_HTTP_BIND_FAILED",
                               sizeof("SLOPPY_E_HTTP_BIND_FAILED") - 1U);
    case SL_DIAG_HTTP_LISTEN_FAILED:
        return sl_diag_literal("SLOPPY_E_HTTP_LISTEN_FAILED",
                               sizeof("SLOPPY_E_HTTP_LISTEN_FAILED") - 1U);
    case SL_DIAG_HTTP_ACCEPT_FAILED:
        return sl_diag_literal("SLOPPY_E_HTTP_ACCEPT_FAILED",
                               sizeof("SLOPPY_E_HTTP_ACCEPT_FAILED") - 1U);
    case SL_DIAG_HTTP_DISPATCH_FAILED:
        return sl_diag_literal("SLOPPY_E_HTTP_DISPATCH_FAILED",
                               sizeof("SLOPPY_E_HTTP_DISPATCH_FAILED") - 1U);
    case SL_DIAG_HTTP_RESPONSE_SERIALIZATION_FAILED:
        return sl_diag_literal("SLOPPY_E_HTTP_RESPONSE_SERIALIZATION_FAILED",
                               sizeof("SLOPPY_E_HTTP_RESPONSE_SERIALIZATION_FAILED") - 1U);
    case SL_DIAG_HTTP_WRITE_FAILED:
        return sl_diag_literal("SLOPPY_E_HTTP_WRITE_FAILED",
                               sizeof("SLOPPY_E_HTTP_WRITE_FAILED") - 1U);
    case SL_DIAG_HTTP_CLOSE_FAILED:
        return sl_diag_literal("SLOPPY_E_HTTP_CLOSE_FAILED",
                               sizeof("SLOPPY_E_HTTP_CLOSE_FAILED") - 1U);
    case SL_DIAG_HTTP_TLS_CONFIG:
        return sl_diag_literal("SLOPPY_E_HTTP_TLS_CONFIG", sizeof("SLOPPY_E_HTTP_TLS_CONFIG") - 1U);
    case SL_DIAG_HTTP_TLS_BACKEND_UNAVAILABLE:
        return sl_diag_literal("SLOPPY_E_HTTP_TLS_BACKEND_UNAVAILABLE",
                               sizeof("SLOPPY_E_HTTP_TLS_BACKEND_UNAVAILABLE") - 1U);
    case SL_DIAG_HTTP_TLS_HANDSHAKE_FAILED:
        return sl_diag_literal("SLOPPY_E_HTTP_TLS_HANDSHAKE_FAILED",
                               sizeof("SLOPPY_E_HTTP_TLS_HANDSHAKE_FAILED") - 1U);
    case SL_DIAG_HTTP_TLS_SHUTDOWN_FAILED:
        return sl_diag_literal("SLOPPY_E_HTTP_TLS_SHUTDOWN_FAILED",
                               sizeof("SLOPPY_E_HTTP_TLS_SHUTDOWN_FAILED") - 1U);
    default:
        return sl_diag_literal("SLOPPY_E_UNKNOWN", sizeof("SLOPPY_E_UNKNOWN") - 1U);
    }
}

static SlStr sl_diag_http_code_name(SlDiagCode code)
{
    switch (code) {
    case SL_DIAG_INVALID_HTTP_REQUEST:
        return sl_diag_literal("SLOPPY_E_INVALID_HTTP_REQUEST",
                               sizeof("SLOPPY_E_INVALID_HTTP_REQUEST") - 1U);
    case SL_DIAG_HTTP_HEADER_LIMIT:
        return sl_diag_literal("SLOPPY_E_HTTP_HEADER_LIMIT",
                               sizeof("SLOPPY_E_HTTP_HEADER_LIMIT") - 1U);
    case SL_DIAG_HTTP_TARGET_LIMIT:
        return sl_diag_literal("SLOPPY_E_HTTP_TARGET_LIMIT",
                               sizeof("SLOPPY_E_HTTP_TARGET_LIMIT") - 1U);
    case SL_DIAG_HTTP_HEADER_NAME_LIMIT:
        return sl_diag_literal("SLOPPY_E_HTTP_HEADER_NAME_LIMIT",
                               sizeof("SLOPPY_E_HTTP_HEADER_NAME_LIMIT") - 1U);
    case SL_DIAG_HTTP_HEADER_VALUE_LIMIT:
        return sl_diag_literal("SLOPPY_E_HTTP_HEADER_VALUE_LIMIT",
                               sizeof("SLOPPY_E_HTTP_HEADER_VALUE_LIMIT") - 1U);
    case SL_DIAG_HTTP_HEADER_BYTES_LIMIT:
        return sl_diag_literal("SLOPPY_E_HTTP_HEADER_BYTES_LIMIT",
                               sizeof("SLOPPY_E_HTTP_HEADER_BYTES_LIMIT") - 1U);
    case SL_DIAG_HTTP_REQUEST_LINE_LIMIT:
        return sl_diag_literal("SLOPPY_E_HTTP_REQUEST_LINE_LIMIT",
                               sizeof("SLOPPY_E_HTTP_REQUEST_LINE_LIMIT") - 1U);
    case SL_DIAG_HTTP_UNSUPPORTED_METHOD:
        return sl_diag_literal("SLOPPY_E_HTTP_UNSUPPORTED_METHOD",
                               sizeof("SLOPPY_E_HTTP_UNSUPPORTED_METHOD") - 1U);
    case SL_DIAG_HTTP_ROUTE_NOT_FOUND:
        return sl_diag_literal("SLOPPY_E_HTTP_ROUTE_NOT_FOUND",
                               sizeof("SLOPPY_E_HTTP_ROUTE_NOT_FOUND") - 1U);
    case SL_DIAG_HTTP_CONNECTION_CLOSED:
        return sl_diag_literal("SLOPPY_E_HTTP_CONNECTION_CLOSED",
                               sizeof("SLOPPY_E_HTTP_CONNECTION_CLOSED") - 1U);
    case SL_DIAG_HTTP_REQUEST_TIMEOUT:
        return sl_diag_literal("SLOPPY_E_HTTP_REQUEST_TIMEOUT",
                               sizeof("SLOPPY_E_HTTP_REQUEST_TIMEOUT") - 1U);
    case SL_DIAG_HTTP_OVERLOAD:
        return sl_diag_literal("SLOPPY_E_HTTP_OVERLOAD", sizeof("SLOPPY_E_HTTP_OVERLOAD") - 1U);
    case SL_DIAG_HTTP_KEEP_ALIVE_UNSUPPORTED:
        return sl_diag_literal("SLOPPY_E_HTTP_KEEP_ALIVE_UNSUPPORTED",
                               sizeof("SLOPPY_E_HTTP_KEEP_ALIVE_UNSUPPORTED") - 1U);
    case SL_DIAG_HTTP_KEEP_ALIVE_IDLE_TIMEOUT:
        return sl_diag_literal("SLOPPY_E_HTTP_KEEP_ALIVE_IDLE_TIMEOUT",
                               sizeof("SLOPPY_E_HTTP_KEEP_ALIVE_IDLE_TIMEOUT") - 1U);
    case SL_DIAG_HTTP_MAX_REQUESTS_REACHED:
        return sl_diag_literal("SLOPPY_E_HTTP_MAX_REQUESTS_REACHED",
                               sizeof("SLOPPY_E_HTTP_MAX_REQUESTS_REACHED") - 1U);
    case SL_DIAG_HTTP_PIPELINING_UNSUPPORTED:
        return sl_diag_literal("SLOPPY_E_HTTP_PIPELINING_UNSUPPORTED",
                               sizeof("SLOPPY_E_HTTP_PIPELINING_UNSUPPORTED") - 1U);
    case SL_DIAG_HTTP_CHUNK_SIZE_INVALID:
        return sl_diag_literal("SLOPPY_E_HTTP_CHUNK_SIZE_INVALID",
                               sizeof("SLOPPY_E_HTTP_CHUNK_SIZE_INVALID") - 1U);
    case SL_DIAG_HTTP_CHUNK_SIZE_OVERFLOW:
        return sl_diag_literal("SLOPPY_E_HTTP_CHUNK_SIZE_OVERFLOW",
                               sizeof("SLOPPY_E_HTTP_CHUNK_SIZE_OVERFLOW") - 1U);
    case SL_DIAG_HTTP_CHUNK_DELIMITER_INVALID:
        return sl_diag_literal("SLOPPY_E_HTTP_CHUNK_DELIMITER_INVALID",
                               sizeof("SLOPPY_E_HTTP_CHUNK_DELIMITER_INVALID") - 1U);
    case SL_DIAG_HTTP_CHUNK_FINAL_MISSING:
        return sl_diag_literal("SLOPPY_E_HTTP_CHUNK_FINAL_MISSING",
                               sizeof("SLOPPY_E_HTTP_CHUNK_FINAL_MISSING") - 1U);
    case SL_DIAG_HTTP_TRAILERS_UNSUPPORTED:
        return sl_diag_literal("SLOPPY_E_HTTP_TRAILERS_UNSUPPORTED",
                               sizeof("SLOPPY_E_HTTP_TRAILERS_UNSUPPORTED") - 1U);
    case SL_DIAG_HTTP_RESPONSE_BACKPRESSURE:
        return sl_diag_literal("SLOPPY_E_HTTP_RESPONSE_BACKPRESSURE",
                               sizeof("SLOPPY_E_HTTP_RESPONSE_BACKPRESSURE") - 1U);
    case SL_DIAG_HTTP_SHUTDOWN:
        return sl_diag_literal("SLOPPY_E_HTTP_SHUTDOWN", sizeof("SLOPPY_E_HTTP_SHUTDOWN") - 1U);
    case SL_DIAG_HTTP_TRANSPORT_CONFIG:
    case SL_DIAG_HTTP_BIND_FAILED:
    case SL_DIAG_HTTP_LISTEN_FAILED:
    case SL_DIAG_HTTP_ACCEPT_FAILED:
    case SL_DIAG_HTTP_DISPATCH_FAILED:
    case SL_DIAG_HTTP_RESPONSE_SERIALIZATION_FAILED:
    case SL_DIAG_HTTP_WRITE_FAILED:
    case SL_DIAG_HTTP_CLOSE_FAILED:
    case SL_DIAG_HTTP_TLS_CONFIG:
    case SL_DIAG_HTTP_TLS_BACKEND_UNAVAILABLE:
    case SL_DIAG_HTTP_TLS_HANDSHAKE_FAILED:
    case SL_DIAG_HTTP_TLS_SHUTDOWN_FAILED:
        return sl_diag_http_transport_code_name(code);
    case SL_DIAG_HTTP_BODY_LIMIT:
        return sl_diag_literal("SLOPPY_E_HTTP_BODY_LIMIT", sizeof("SLOPPY_E_HTTP_BODY_LIMIT") - 1U);
    case SL_DIAG_HTTP_UNSUPPORTED_MEDIA_TYPE:
        return sl_diag_literal("SLOPPY_E_HTTP_UNSUPPORTED_MEDIA_TYPE",
                               sizeof("SLOPPY_E_HTTP_UNSUPPORTED_MEDIA_TYPE") - 1U);
    case SL_DIAG_HTTP_UNSUPPORTED_BODY:
        return sl_diag_literal("SLOPPY_E_HTTP_UNSUPPORTED_BODY",
                               sizeof("SLOPPY_E_HTTP_UNSUPPORTED_BODY") - 1U);
    case SL_DIAG_INVALID_HTTP_RESULT:
        return sl_diag_literal("SLOPPY_E_INVALID_HTTP_RESULT",
                               sizeof("SLOPPY_E_INVALID_HTTP_RESULT") - 1U);
    case SL_DIAG_HTTP_CLIENT_FEATURE_UNAVAILABLE:
    case SL_DIAG_HTTP_CLIENT_INVALID_URL:
    case SL_DIAG_HTTP_CLIENT_INVALID_OPTIONS:
    case SL_DIAG_HTTP_CLIENT_AMBIGUOUS_BODY:
    case SL_DIAG_HTTP_CLIENT_BODY_CONSUMED:
    case SL_DIAG_HTTP_CLIENT_RESPONSE_BODY_LIMIT:
    case SL_DIAG_HTTP_CLIENT_REQUEST_BODY_LIMIT:
    case SL_DIAG_HTTP_CLIENT_MALFORMED_RESPONSE:
    case SL_DIAG_HTTP_CLIENT_INVALID_JSON:
    case SL_DIAG_HTTP_CLIENT_CONNECT_FAILED:
    case SL_DIAG_HTTP_CLIENT_DNS_FAILED:
    case SL_DIAG_HTTP_CLIENT_TLS_BACKEND_UNAVAILABLE:
    case SL_DIAG_HTTP_CLIENT_TLS_CERTIFICATE_VALIDATION_FAILED:
    case SL_DIAG_HTTP_CLIENT_TLS_HOSTNAME_MISMATCH:
    case SL_DIAG_HTTP_CLIENT_REQUEST_TIMEOUT:
    case SL_DIAG_HTTP_CLIENT_REQUEST_CANCELLED:
    case SL_DIAG_HTTP_CLIENT_REDIRECT_LOOP:
    case SL_DIAG_HTTP_CLIENT_MAX_REDIRECTS_EXCEEDED:
    case SL_DIAG_HTTP_CLIENT_SENSITIVE_HEADER_STRIPPED:
    case SL_DIAG_HTTP_CLIENT_POOL_EXHAUSTED:
    case SL_DIAG_HTTP_CLIENT_STRICT_NETWORK_DENIED:
    case SL_DIAG_HTTP_CLIENT_DYNAMIC_TARGET_METADATA:
        return sl_diag_http_client_code_name(code);
    default:
        return sl_diag_literal("SLOPPY_E_UNKNOWN", sizeof("SLOPPY_E_UNKNOWN") - 1U);
    }
}

static bool sl_diag_is_http_code(SlDiagCode code)
{
    switch (code) {
    case SL_DIAG_HTTP_BODY_LIMIT:
    case SL_DIAG_HTTP_UNSUPPORTED_MEDIA_TYPE:
    case SL_DIAG_HTTP_TARGET_LIMIT:
    case SL_DIAG_HTTP_HEADER_NAME_LIMIT:
    case SL_DIAG_HTTP_HEADER_VALUE_LIMIT:
    case SL_DIAG_HTTP_HEADER_BYTES_LIMIT:
    case SL_DIAG_HTTP_REQUEST_LINE_LIMIT:
    case SL_DIAG_HTTP_CONNECTION_CLOSED:
    case SL_DIAG_HTTP_REQUEST_TIMEOUT:
    case SL_DIAG_HTTP_OVERLOAD:
    case SL_DIAG_HTTP_KEEP_ALIVE_UNSUPPORTED:
    case SL_DIAG_HTTP_KEEP_ALIVE_IDLE_TIMEOUT:
    case SL_DIAG_HTTP_MAX_REQUESTS_REACHED:
    case SL_DIAG_HTTP_PIPELINING_UNSUPPORTED:
    case SL_DIAG_HTTP_CHUNK_SIZE_INVALID:
    case SL_DIAG_HTTP_CHUNK_SIZE_OVERFLOW:
    case SL_DIAG_HTTP_CHUNK_DELIMITER_INVALID:
    case SL_DIAG_HTTP_CHUNK_FINAL_MISSING:
    case SL_DIAG_HTTP_TRAILERS_UNSUPPORTED:
    case SL_DIAG_HTTP_RESPONSE_BACKPRESSURE:
    case SL_DIAG_HTTP_SHUTDOWN:
    case SL_DIAG_HTTP_TRANSPORT_CONFIG:
    case SL_DIAG_HTTP_BIND_FAILED:
    case SL_DIAG_HTTP_LISTEN_FAILED:
    case SL_DIAG_HTTP_ACCEPT_FAILED:
    case SL_DIAG_HTTP_DISPATCH_FAILED:
    case SL_DIAG_HTTP_RESPONSE_SERIALIZATION_FAILED:
    case SL_DIAG_HTTP_WRITE_FAILED:
    case SL_DIAG_HTTP_CLOSE_FAILED:
    case SL_DIAG_HTTP_TLS_CONFIG:
    case SL_DIAG_HTTP_TLS_BACKEND_UNAVAILABLE:
    case SL_DIAG_HTTP_TLS_HANDSHAKE_FAILED:
    case SL_DIAG_HTTP_TLS_SHUTDOWN_FAILED:
    case SL_DIAG_INVALID_HTTP_REQUEST:
    case SL_DIAG_HTTP_HEADER_LIMIT:
    case SL_DIAG_HTTP_UNSUPPORTED_METHOD:
    case SL_DIAG_HTTP_ROUTE_NOT_FOUND:
    case SL_DIAG_HTTP_UNSUPPORTED_BODY:
    case SL_DIAG_INVALID_HTTP_RESULT:
    case SL_DIAG_HTTP_CLIENT_FEATURE_UNAVAILABLE:
    case SL_DIAG_HTTP_CLIENT_INVALID_URL:
    case SL_DIAG_HTTP_CLIENT_INVALID_OPTIONS:
    case SL_DIAG_HTTP_CLIENT_AMBIGUOUS_BODY:
    case SL_DIAG_HTTP_CLIENT_BODY_CONSUMED:
    case SL_DIAG_HTTP_CLIENT_RESPONSE_BODY_LIMIT:
    case SL_DIAG_HTTP_CLIENT_REQUEST_BODY_LIMIT:
    case SL_DIAG_HTTP_CLIENT_MALFORMED_RESPONSE:
    case SL_DIAG_HTTP_CLIENT_INVALID_JSON:
    case SL_DIAG_HTTP_CLIENT_CONNECT_FAILED:
    case SL_DIAG_HTTP_CLIENT_DNS_FAILED:
    case SL_DIAG_HTTP_CLIENT_TLS_BACKEND_UNAVAILABLE:
    case SL_DIAG_HTTP_CLIENT_TLS_CERTIFICATE_VALIDATION_FAILED:
    case SL_DIAG_HTTP_CLIENT_TLS_HOSTNAME_MISMATCH:
    case SL_DIAG_HTTP_CLIENT_REQUEST_TIMEOUT:
    case SL_DIAG_HTTP_CLIENT_REQUEST_CANCELLED:
    case SL_DIAG_HTTP_CLIENT_REDIRECT_LOOP:
    case SL_DIAG_HTTP_CLIENT_MAX_REDIRECTS_EXCEEDED:
    case SL_DIAG_HTTP_CLIENT_SENSITIVE_HEADER_STRIPPED:
    case SL_DIAG_HTTP_CLIENT_POOL_EXHAUSTED:
    case SL_DIAG_HTTP_CLIENT_STRICT_NETWORK_DENIED:
    case SL_DIAG_HTTP_CLIENT_DYNAMIC_TARGET_METADATA:
        return true;
    default:
        return false;
    }
}

static SlStr sl_diag_lifecycle_code_name(SlDiagCode code)
{
    switch (code) {
    case SL_DIAG_LIFECYCLE_START_FAILED:
        return sl_diag_literal("SLOPPY_E_LIFECYCLE_START_FAILED",
                               sizeof("SLOPPY_E_LIFECYCLE_START_FAILED") - 1U);
    case SL_DIAG_LIFECYCLE_ALREADY_STARTED:
        return sl_diag_literal("SLOPPY_E_LIFECYCLE_ALREADY_STARTED",
                               sizeof("SLOPPY_E_LIFECYCLE_ALREADY_STARTED") - 1U);
    case SL_DIAG_LIFECYCLE_NOT_STARTED:
        return sl_diag_literal("SLOPPY_E_LIFECYCLE_NOT_STARTED",
                               sizeof("SLOPPY_E_LIFECYCLE_NOT_STARTED") - 1U);
    case SL_DIAG_LIFECYCLE_SHUTDOWN_STARTED:
        return sl_diag_literal("SLOPPY_E_LIFECYCLE_SHUTDOWN_STARTED",
                               sizeof("SLOPPY_E_LIFECYCLE_SHUTDOWN_STARTED") - 1U);
    case SL_DIAG_LIFECYCLE_SHUTDOWN_FORCED:
        return sl_diag_literal("SLOPPY_E_LIFECYCLE_SHUTDOWN_FORCED",
                               sizeof("SLOPPY_E_LIFECYCLE_SHUTDOWN_FORCED") - 1U);
    case SL_DIAG_LIFECYCLE_REQUEST_SCOPE_CLOSED:
        return sl_diag_literal("SLOPPY_E_LIFECYCLE_REQUEST_SCOPE_CLOSED",
                               sizeof("SLOPPY_E_LIFECYCLE_REQUEST_SCOPE_CLOSED") - 1U);
    case SL_DIAG_LIFECYCLE_LATE_COMPLETION_DROPPED:
        return sl_diag_literal("SLOPPY_E_LIFECYCLE_LATE_COMPLETION_DROPPED",
                               sizeof("SLOPPY_E_LIFECYCLE_LATE_COMPLETION_DROPPED") - 1U);
    case SL_DIAG_LIFECYCLE_CLEANUP_FAILED:
        return sl_diag_literal("SLOPPY_E_LIFECYCLE_CLEANUP_FAILED",
                               sizeof("SLOPPY_E_LIFECYCLE_CLEANUP_FAILED") - 1U);
    case SL_DIAG_LIFECYCLE_LEAK_DETECTED:
        return sl_diag_literal("SLOPPY_E_LIFECYCLE_LEAK_DETECTED",
                               sizeof("SLOPPY_E_LIFECYCLE_LEAK_DETECTED") - 1U);
    case SL_DIAG_LIFECYCLE_IDENTITY_UNAVAILABLE:
        return sl_diag_literal("SLOPPY_E_LIFECYCLE_IDENTITY_UNAVAILABLE",
                               sizeof("SLOPPY_E_LIFECYCLE_IDENTITY_UNAVAILABLE") - 1U);
    default:
        return sl_diag_literal("SLOPPY_E_UNKNOWN", sizeof("SLOPPY_E_UNKNOWN") - 1U);
    }
}

static bool sl_diag_is_lifecycle_code(SlDiagCode code)
{
    switch (code) {
    case SL_DIAG_LIFECYCLE_START_FAILED:
    case SL_DIAG_LIFECYCLE_ALREADY_STARTED:
    case SL_DIAG_LIFECYCLE_NOT_STARTED:
    case SL_DIAG_LIFECYCLE_SHUTDOWN_STARTED:
    case SL_DIAG_LIFECYCLE_SHUTDOWN_FORCED:
    case SL_DIAG_LIFECYCLE_REQUEST_SCOPE_CLOSED:
    case SL_DIAG_LIFECYCLE_LATE_COMPLETION_DROPPED:
    case SL_DIAG_LIFECYCLE_CLEANUP_FAILED:
    case SL_DIAG_LIFECYCLE_LEAK_DETECTED:
    case SL_DIAG_LIFECYCLE_IDENTITY_UNAVAILABLE:
        return true;
    default:
        return false;
    }
}

static SlStr sl_diag_time_code_name(SlDiagCode code)
{
    switch (code) {
    case SL_DIAG_TIME_TIMEOUT:
        return sl_diag_literal("SLOPPY_E_TIME_TIMEOUT", sizeof("SLOPPY_E_TIME_TIMEOUT") - 1U);
    case SL_DIAG_TIME_CANCELLED:
        return sl_diag_literal("SLOPPY_E_TIME_CANCELLED", sizeof("SLOPPY_E_TIME_CANCELLED") - 1U);
    case SL_DIAG_TIME_TIMER_DISPOSED:
        return sl_diag_literal("SLOPPY_E_TIME_TIMER_DISPOSED",
                               sizeof("SLOPPY_E_TIME_TIMER_DISPOSED") - 1U);
    case SL_DIAG_TIME_INVALID_DELAY:
        return sl_diag_literal("SLOPPY_E_TIME_INVALID_DELAY",
                               sizeof("SLOPPY_E_TIME_INVALID_DELAY") - 1U);
    case SL_DIAG_TIME_DEADLINE_EXPIRED:
        return sl_diag_literal("SLOPPY_E_TIME_DEADLINE_EXPIRED",
                               sizeof("SLOPPY_E_TIME_DEADLINE_EXPIRED") - 1U);
    case SL_DIAG_TIME_INTERVAL_OVERFLOW:
        return sl_diag_literal("SLOPPY_E_TIME_INTERVAL_OVERFLOW",
                               sizeof("SLOPPY_E_TIME_INTERVAL_OVERFLOW") - 1U);
    case SL_DIAG_TIME_SCHEDULE_SKIPPED:
        return sl_diag_literal("SLOPPY_E_TIME_SCHEDULE_SKIPPED",
                               sizeof("SLOPPY_E_TIME_SCHEDULE_SKIPPED") - 1U);
    case SL_DIAG_TIME_FAKE_CLOCK_MISUSE:
        return sl_diag_literal("SLOPPY_E_TIME_FAKE_CLOCK_MISUSE",
                               sizeof("SLOPPY_E_TIME_FAKE_CLOCK_MISUSE") - 1U);
    default:
        return sl_diag_literal("SLOPPY_E_UNKNOWN", sizeof("SLOPPY_E_UNKNOWN") - 1U);
    }
}

static bool sl_diag_is_time_code(SlDiagCode code)
{
    switch (code) {
    case SL_DIAG_TIME_TIMEOUT:
    case SL_DIAG_TIME_CANCELLED:
    case SL_DIAG_TIME_TIMER_DISPOSED:
    case SL_DIAG_TIME_INVALID_DELAY:
    case SL_DIAG_TIME_DEADLINE_EXPIRED:
    case SL_DIAG_TIME_INTERVAL_OVERFLOW:
    case SL_DIAG_TIME_SCHEDULE_SKIPPED:
    case SL_DIAG_TIME_FAKE_CLOCK_MISUSE:
        return true;
    default:
        return false;
    }
}

static SlStr sl_diag_crypto_code_name(SlDiagCode code)
{
    switch (code) {
    case SL_DIAG_CRYPTO_FEATURE_UNAVAILABLE:
        return sl_diag_literal("SLOPPY_E_CRYPTO_FEATURE_UNAVAILABLE",
                               sizeof("SLOPPY_E_CRYPTO_FEATURE_UNAVAILABLE") - 1U);
    case SL_DIAG_CRYPTO_UNSUPPORTED_ALGORITHM:
        return sl_diag_literal("SLOPPY_E_CRYPTO_UNSUPPORTED_ALGORITHM",
                               sizeof("SLOPPY_E_CRYPTO_UNSUPPORTED_ALGORITHM") - 1U);
    case SL_DIAG_CRYPTO_INSECURE_LEGACY_ALGORITHM:
        return sl_diag_literal("SLOPPY_E_CRYPTO_INSECURE_LEGACY_ALGORITHM",
                               sizeof("SLOPPY_E_CRYPTO_INSECURE_LEGACY_ALGORITHM") - 1U);
    case SL_DIAG_CRYPTO_INVALID_KEY_SECRET:
        return sl_diag_literal("SLOPPY_E_CRYPTO_INVALID_KEY_SECRET",
                               sizeof("SLOPPY_E_CRYPTO_INVALID_KEY_SECRET") - 1U);
    case SL_DIAG_CRYPTO_PASSWORD_VERIFY_FAILED:
        return sl_diag_literal("SLOPPY_E_CRYPTO_PASSWORD_VERIFY_FAILED",
                               sizeof("SLOPPY_E_CRYPTO_PASSWORD_VERIFY_FAILED") - 1U);
    case SL_DIAG_CRYPTO_PASSWORD_HASH_UNSUPPORTED:
        return sl_diag_literal("SLOPPY_E_CRYPTO_PASSWORD_HASH_UNSUPPORTED",
                               sizeof("SLOPPY_E_CRYPTO_PASSWORD_HASH_UNSUPPORTED") - 1U);
    case SL_DIAG_CRYPTO_RANDOM_SOURCE_UNAVAILABLE:
        return sl_diag_literal("SLOPPY_E_CRYPTO_RANDOM_SOURCE_UNAVAILABLE",
                               sizeof("SLOPPY_E_CRYPTO_RANDOM_SOURCE_UNAVAILABLE") - 1U);
    case SL_DIAG_CRYPTO_SECRET_DISPOSED:
        return sl_diag_literal("SLOPPY_E_CRYPTO_SECRET_DISPOSED",
                               sizeof("SLOPPY_E_CRYPTO_SECRET_DISPOSED") - 1U);
    case SL_DIAG_CRYPTO_CONSTANT_TIME_INVALID_INPUT:
        return sl_diag_literal("SLOPPY_E_CRYPTO_CONSTANT_TIME_INVALID_INPUT",
                               sizeof("SLOPPY_E_CRYPTO_CONSTANT_TIME_INVALID_INPUT") - 1U);
    case SL_DIAG_CRYPTO_BACKEND_UNAVAILABLE:
        return sl_diag_literal("SLOPPY_E_CRYPTO_BACKEND_UNAVAILABLE",
                               sizeof("SLOPPY_E_CRYPTO_BACKEND_UNAVAILABLE") - 1U);
    case SL_DIAG_CRYPTO_NONCRYPTO_HASH_SECURITY_CONTEXT_WARNING:
        return sl_diag_literal("SLOPPY_W_CRYPTO_NONCRYPTO_HASH_SECURITY_CONTEXT_WARNING",
                               sizeof("SLOPPY_W_CRYPTO_NONCRYPTO_HASH_SECURITY_CONTEXT_WARNING") -
                                   1U);
    default:
        return sl_diag_literal("SLOPPY_E_UNKNOWN", sizeof("SLOPPY_E_UNKNOWN") - 1U);
    }
}

static bool sl_diag_is_crypto_code(SlDiagCode code)
{
    switch (code) {
    case SL_DIAG_CRYPTO_FEATURE_UNAVAILABLE:
    case SL_DIAG_CRYPTO_UNSUPPORTED_ALGORITHM:
    case SL_DIAG_CRYPTO_INSECURE_LEGACY_ALGORITHM:
    case SL_DIAG_CRYPTO_INVALID_KEY_SECRET:
    case SL_DIAG_CRYPTO_PASSWORD_VERIFY_FAILED:
    case SL_DIAG_CRYPTO_PASSWORD_HASH_UNSUPPORTED:
    case SL_DIAG_CRYPTO_RANDOM_SOURCE_UNAVAILABLE:
    case SL_DIAG_CRYPTO_SECRET_DISPOSED:
    case SL_DIAG_CRYPTO_CONSTANT_TIME_INVALID_INPUT:
    case SL_DIAG_CRYPTO_BACKEND_UNAVAILABLE:
    case SL_DIAG_CRYPTO_NONCRYPTO_HASH_SECURITY_CONTEXT_WARNING:
        return true;
    default:
        return false;
    }
}

static SlStr sl_diag_codec_code_name(SlDiagCode code)
{
    switch (code) {
    case SL_DIAG_CODEC_FEATURE_UNAVAILABLE:
        return sl_diag_literal("SLOPPY_E_CODEC_FEATURE_UNAVAILABLE",
                               sizeof("SLOPPY_E_CODEC_FEATURE_UNAVAILABLE") - 1U);
    case SL_DIAG_CODEC_UNSUPPORTED_ENCODING:
        return sl_diag_literal("SLOPPY_E_CODEC_UNSUPPORTED_ENCODING",
                               sizeof("SLOPPY_E_CODEC_UNSUPPORTED_ENCODING") - 1U);
    case SL_DIAG_CODEC_INVALID_BASE64:
        return sl_diag_literal("SLOPPY_E_CODEC_INVALID_BASE64",
                               sizeof("SLOPPY_E_CODEC_INVALID_BASE64") - 1U);
    case SL_DIAG_CODEC_INVALID_BASE64URL:
        return sl_diag_literal("SLOPPY_E_CODEC_INVALID_BASE64URL",
                               sizeof("SLOPPY_E_CODEC_INVALID_BASE64URL") - 1U);
    case SL_DIAG_CODEC_INVALID_HEX:
        return sl_diag_literal("SLOPPY_E_CODEC_INVALID_HEX",
                               sizeof("SLOPPY_E_CODEC_INVALID_HEX") - 1U);
    case SL_DIAG_CODEC_MALFORMED_UTF8:
        return sl_diag_literal("SLOPPY_E_CODEC_MALFORMED_UTF8",
                               sizeof("SLOPPY_E_CODEC_MALFORMED_UTF8") - 1U);
    case SL_DIAG_CODEC_BINARY_READ_OUT_OF_BOUNDS:
        return sl_diag_literal("SLOPPY_E_CODEC_BINARY_READ_OUT_OF_BOUNDS",
                               sizeof("SLOPPY_E_CODEC_BINARY_READ_OUT_OF_BOUNDS") - 1U);
    case SL_DIAG_CODEC_BINARY_INVALID_ENDIAN_OR_FIELD_SIZE:
        return sl_diag_literal("SLOPPY_E_CODEC_BINARY_INVALID_ENDIAN_OR_FIELD_SIZE",
                               sizeof("SLOPPY_E_CODEC_BINARY_INVALID_ENDIAN_OR_FIELD_SIZE") - 1U);
    case SL_DIAG_CODEC_COMPRESSION_BACKEND_UNAVAILABLE:
        return sl_diag_literal("SLOPPY_E_CODEC_COMPRESSION_BACKEND_UNAVAILABLE",
                               sizeof("SLOPPY_E_CODEC_COMPRESSION_BACKEND_UNAVAILABLE") - 1U);
    case SL_DIAG_CODEC_DECOMPRESSION_LIMIT_EXCEEDED:
        return sl_diag_literal("SLOPPY_E_CODEC_DECOMPRESSION_LIMIT_EXCEEDED",
                               sizeof("SLOPPY_E_CODEC_DECOMPRESSION_LIMIT_EXCEEDED") - 1U);
    case SL_DIAG_CODEC_COMPRESSED_STREAM_CORRUPT:
        return sl_diag_literal("SLOPPY_E_CODEC_COMPRESSED_STREAM_CORRUPT",
                               sizeof("SLOPPY_E_CODEC_COMPRESSED_STREAM_CORRUPT") - 1U);
    case SL_DIAG_CODEC_CHECKSUM_UNSUPPORTED_ALGORITHM:
        return sl_diag_literal("SLOPPY_E_CODEC_CHECKSUM_UNSUPPORTED_ALGORITHM",
                               sizeof("SLOPPY_E_CODEC_CHECKSUM_UNSUPPORTED_ALGORITHM") - 1U);
    case SL_DIAG_CODEC_CHECKSUM_SECURITY_CONTEXT_WARNING:
        return sl_diag_literal("SLOPPY_W_CODEC_CHECKSUM_SECURITY_CONTEXT_WARNING",
                               sizeof("SLOPPY_W_CODEC_CHECKSUM_SECURITY_CONTEXT_WARNING") - 1U);
    default:
        return sl_diag_literal("SLOPPY_E_UNKNOWN", sizeof("SLOPPY_E_UNKNOWN") - 1U);
    }
}

static bool sl_diag_is_codec_code(SlDiagCode code)
{
    switch (code) {
    case SL_DIAG_CODEC_FEATURE_UNAVAILABLE:
    case SL_DIAG_CODEC_UNSUPPORTED_ENCODING:
    case SL_DIAG_CODEC_INVALID_BASE64:
    case SL_DIAG_CODEC_INVALID_BASE64URL:
    case SL_DIAG_CODEC_INVALID_HEX:
    case SL_DIAG_CODEC_MALFORMED_UTF8:
    case SL_DIAG_CODEC_BINARY_READ_OUT_OF_BOUNDS:
    case SL_DIAG_CODEC_BINARY_INVALID_ENDIAN_OR_FIELD_SIZE:
    case SL_DIAG_CODEC_COMPRESSION_BACKEND_UNAVAILABLE:
    case SL_DIAG_CODEC_DECOMPRESSION_LIMIT_EXCEEDED:
    case SL_DIAG_CODEC_COMPRESSED_STREAM_CORRUPT:
    case SL_DIAG_CODEC_CHECKSUM_UNSUPPORTED_ALGORITHM:
    case SL_DIAG_CODEC_CHECKSUM_SECURITY_CONTEXT_WARNING:
        return true;
    default:
        return false;
    }
}

static SlStr sl_diag_net_code_name(SlDiagCode code)
{
    switch (code) {
    case SL_DIAG_NET_FEATURE_UNAVAILABLE:
        return sl_diag_literal("SLOPPY_E_NET_FEATURE_UNAVAILABLE",
                               sizeof("SLOPPY_E_NET_FEATURE_UNAVAILABLE") - 1U);
    case SL_DIAG_NET_CONNECT_DENIED:
        return sl_diag_literal("SLOPPY_E_NET_CONNECT_DENIED",
                               sizeof("SLOPPY_E_NET_CONNECT_DENIED") - 1U);
    case SL_DIAG_NET_LISTEN_DENIED:
        return sl_diag_literal("SLOPPY_E_NET_LISTEN_DENIED",
                               sizeof("SLOPPY_E_NET_LISTEN_DENIED") - 1U);
    case SL_DIAG_NET_INVALID_HOST:
        return sl_diag_literal("SLOPPY_E_NET_INVALID_HOST",
                               sizeof("SLOPPY_E_NET_INVALID_HOST") - 1U);
    case SL_DIAG_NET_INVALID_PORT:
        return sl_diag_literal("SLOPPY_E_NET_INVALID_PORT",
                               sizeof("SLOPPY_E_NET_INVALID_PORT") - 1U);
    case SL_DIAG_NET_DNS_FAILURE:
        return sl_diag_literal("SLOPPY_E_NET_DNS_FAILURE", sizeof("SLOPPY_E_NET_DNS_FAILURE") - 1U);
    case SL_DIAG_NET_CONNECT_TIMEOUT:
        return sl_diag_literal("SLOPPY_E_NET_CONNECT_TIMEOUT",
                               sizeof("SLOPPY_E_NET_CONNECT_TIMEOUT") - 1U);
    case SL_DIAG_NET_CONNECT_CANCELLED:
        return sl_diag_literal("SLOPPY_E_NET_CONNECT_CANCELLED",
                               sizeof("SLOPPY_E_NET_CONNECT_CANCELLED") - 1U);
    case SL_DIAG_NET_CONNECTION_CLOSED:
        return sl_diag_literal("SLOPPY_E_NET_CONNECTION_CLOSED",
                               sizeof("SLOPPY_E_NET_CONNECTION_CLOSED") - 1U);
    case SL_DIAG_NET_STALE_HANDLE:
        return sl_diag_literal("SLOPPY_E_NET_STALE_HANDLE",
                               sizeof("SLOPPY_E_NET_STALE_HANDLE") - 1U);
    case SL_DIAG_NET_READ_WRITE_TIMEOUT:
        return sl_diag_literal("SLOPPY_E_NET_READ_WRITE_TIMEOUT",
                               sizeof("SLOPPY_E_NET_READ_WRITE_TIMEOUT") - 1U);
    case SL_DIAG_NET_READ_WRITE_CANCELLED:
        return sl_diag_literal("SLOPPY_E_NET_READ_WRITE_CANCELLED",
                               sizeof("SLOPPY_E_NET_READ_WRITE_CANCELLED") - 1U);
    case SL_DIAG_NET_BACKPRESSURE_OVERFLOW:
        return sl_diag_literal("SLOPPY_E_NET_BACKPRESSURE_OVERFLOW",
                               sizeof("SLOPPY_E_NET_BACKPRESSURE_OVERFLOW") - 1U);
    case SL_DIAG_NET_UNSUPPORTED_OPTION:
        return sl_diag_literal("SLOPPY_E_NET_UNSUPPORTED_OPTION",
                               sizeof("SLOPPY_E_NET_UNSUPPORTED_OPTION") - 1U);
    case SL_DIAG_NET_BACKEND_UNAVAILABLE:
        return sl_diag_literal("SLOPPY_E_NET_BACKEND_UNAVAILABLE",
                               sizeof("SLOPPY_E_NET_BACKEND_UNAVAILABLE") - 1U);
    case SL_DIAG_NET_LOCAL_IPC_FEATURE_UNAVAILABLE:
        return sl_diag_literal("SLOPPY_E_NET_LOCAL_IPC_FEATURE_UNAVAILABLE",
                               sizeof("SLOPPY_E_NET_LOCAL_IPC_FEATURE_UNAVAILABLE") - 1U);
    case SL_DIAG_NET_LOCAL_IPC_UNSUPPORTED_PLATFORM:
        return sl_diag_literal("SLOPPY_E_NET_LOCAL_IPC_UNSUPPORTED_PLATFORM",
                               sizeof("SLOPPY_E_NET_LOCAL_IPC_UNSUPPORTED_PLATFORM") - 1U);
    case SL_DIAG_NET_LOCAL_IPC_INVALID_PATH:
        return sl_diag_literal("SLOPPY_E_NET_LOCAL_IPC_INVALID_PATH",
                               sizeof("SLOPPY_E_NET_LOCAL_IPC_INVALID_PATH") - 1U);
    case SL_DIAG_NET_LOCAL_IPC_PATH_DENIED:
        return sl_diag_literal("SLOPPY_E_NET_LOCAL_IPC_PATH_DENIED",
                               sizeof("SLOPPY_E_NET_LOCAL_IPC_PATH_DENIED") - 1U);
    case SL_DIAG_NET_LOCAL_IPC_STALE_CLEANUP_FAILED:
        return sl_diag_literal("SLOPPY_E_NET_LOCAL_IPC_STALE_CLEANUP_FAILED",
                               sizeof("SLOPPY_E_NET_LOCAL_IPC_STALE_CLEANUP_FAILED") - 1U);
    case SL_DIAG_NET_LOCAL_IPC_ENDPOINT_EXISTS:
        return sl_diag_literal("SLOPPY_E_NET_LOCAL_IPC_ENDPOINT_EXISTS",
                               sizeof("SLOPPY_E_NET_LOCAL_IPC_ENDPOINT_EXISTS") - 1U);
    case SL_DIAG_NET_LOCAL_IPC_CONNECT_FAILED:
        return sl_diag_literal("SLOPPY_E_NET_LOCAL_IPC_CONNECT_FAILED",
                               sizeof("SLOPPY_E_NET_LOCAL_IPC_CONNECT_FAILED") - 1U);
    case SL_DIAG_NET_LOCAL_IPC_LISTEN_FAILED:
        return sl_diag_literal("SLOPPY_E_NET_LOCAL_IPC_LISTEN_FAILED",
                               sizeof("SLOPPY_E_NET_LOCAL_IPC_LISTEN_FAILED") - 1U);
    case SL_DIAG_NET_LOCAL_IPC_ACCEPT_CANCELLED:
        return sl_diag_literal("SLOPPY_E_NET_LOCAL_IPC_ACCEPT_CANCELLED",
                               sizeof("SLOPPY_E_NET_LOCAL_IPC_ACCEPT_CANCELLED") - 1U);
    case SL_DIAG_NET_LOCAL_IPC_READ_WRITE_CANCELLED:
        return sl_diag_literal("SLOPPY_E_NET_LOCAL_IPC_READ_WRITE_CANCELLED",
                               sizeof("SLOPPY_E_NET_LOCAL_IPC_READ_WRITE_CANCELLED") - 1U);
    case SL_DIAG_NET_LOCAL_IPC_DISPOSED:
        return sl_diag_literal("SLOPPY_E_NET_LOCAL_IPC_DISPOSED",
                               sizeof("SLOPPY_E_NET_LOCAL_IPC_DISPOSED") - 1U);
    case SL_DIAG_NET_LOCAL_IPC_BACKEND_UNAVAILABLE:
        return sl_diag_literal("SLOPPY_E_NET_LOCAL_IPC_BACKEND_UNAVAILABLE",
                               sizeof("SLOPPY_E_NET_LOCAL_IPC_BACKEND_UNAVAILABLE") - 1U);
    case SL_DIAG_NET_LOCAL_IPC_PERMISSION_UNSUPPORTED:
        return sl_diag_literal("SLOPPY_E_NET_LOCAL_IPC_PERMISSION_UNSUPPORTED",
                               sizeof("SLOPPY_E_NET_LOCAL_IPC_PERMISSION_UNSUPPORTED") - 1U);
    default:
        return sl_diag_literal("SLOPPY_E_UNKNOWN", sizeof("SLOPPY_E_UNKNOWN") - 1U);
    }
}

static bool sl_diag_is_net_code(SlDiagCode code)
{
    switch (code) {
    case SL_DIAG_NET_FEATURE_UNAVAILABLE:
    case SL_DIAG_NET_CONNECT_DENIED:
    case SL_DIAG_NET_LISTEN_DENIED:
    case SL_DIAG_NET_INVALID_HOST:
    case SL_DIAG_NET_INVALID_PORT:
    case SL_DIAG_NET_DNS_FAILURE:
    case SL_DIAG_NET_CONNECT_TIMEOUT:
    case SL_DIAG_NET_CONNECT_CANCELLED:
    case SL_DIAG_NET_CONNECTION_CLOSED:
    case SL_DIAG_NET_STALE_HANDLE:
    case SL_DIAG_NET_READ_WRITE_TIMEOUT:
    case SL_DIAG_NET_READ_WRITE_CANCELLED:
    case SL_DIAG_NET_BACKPRESSURE_OVERFLOW:
    case SL_DIAG_NET_UNSUPPORTED_OPTION:
    case SL_DIAG_NET_BACKEND_UNAVAILABLE:
    case SL_DIAG_NET_LOCAL_IPC_FEATURE_UNAVAILABLE:
    case SL_DIAG_NET_LOCAL_IPC_UNSUPPORTED_PLATFORM:
    case SL_DIAG_NET_LOCAL_IPC_INVALID_PATH:
    case SL_DIAG_NET_LOCAL_IPC_PATH_DENIED:
    case SL_DIAG_NET_LOCAL_IPC_STALE_CLEANUP_FAILED:
    case SL_DIAG_NET_LOCAL_IPC_ENDPOINT_EXISTS:
    case SL_DIAG_NET_LOCAL_IPC_CONNECT_FAILED:
    case SL_DIAG_NET_LOCAL_IPC_LISTEN_FAILED:
    case SL_DIAG_NET_LOCAL_IPC_ACCEPT_CANCELLED:
    case SL_DIAG_NET_LOCAL_IPC_READ_WRITE_CANCELLED:
    case SL_DIAG_NET_LOCAL_IPC_DISPOSED:
    case SL_DIAG_NET_LOCAL_IPC_BACKEND_UNAVAILABLE:
    case SL_DIAG_NET_LOCAL_IPC_PERMISSION_UNSUPPORTED:
        return true;
    default:
        return false;
    }
}

static SlStr sl_diag_os_code_name(SlDiagCode code)
{
    switch (code) {
    case SL_DIAG_OS_FEATURE_UNAVAILABLE:
        return sl_diag_literal("SLOPPY_E_OS_FEATURE_UNAVAILABLE",
                               sizeof("SLOPPY_E_OS_FEATURE_UNAVAILABLE") - 1U);
    case SL_DIAG_OS_ENV_ACCESS_DENIED:
        return sl_diag_literal("SLOPPY_E_OS_ENV_ACCESS_DENIED",
                               sizeof("SLOPPY_E_OS_ENV_ACCESS_DENIED") - 1U);
    case SL_DIAG_OS_ENV_SECRET_REDACTED:
        return sl_diag_literal("SLOPPY_E_OS_ENV_SECRET_REDACTED",
                               sizeof("SLOPPY_E_OS_ENV_SECRET_REDACTED") - 1U);
    case SL_DIAG_OS_PROCESS_EXECUTION_DENIED:
        return sl_diag_literal("SLOPPY_E_OS_PROCESS_EXECUTION_DENIED",
                               sizeof("SLOPPY_E_OS_PROCESS_EXECUTION_DENIED") - 1U);
    case SL_DIAG_OS_SHELL_EXECUTION_DENIED:
        return sl_diag_literal("SLOPPY_E_OS_SHELL_EXECUTION_DENIED",
                               sizeof("SLOPPY_E_OS_SHELL_EXECUTION_DENIED") - 1U);
    case SL_DIAG_OS_COMMAND_NOT_FOUND:
        return sl_diag_literal("SLOPPY_E_OS_COMMAND_NOT_FOUND",
                               sizeof("SLOPPY_E_OS_COMMAND_NOT_FOUND") - 1U);
    case SL_DIAG_OS_INVALID_CWD:
        return sl_diag_literal("SLOPPY_E_OS_INVALID_CWD", sizeof("SLOPPY_E_OS_INVALID_CWD") - 1U);
    case SL_DIAG_OS_INVALID_ENV_OVERRIDE:
        return sl_diag_literal("SLOPPY_E_OS_INVALID_ENV_OVERRIDE",
                               sizeof("SLOPPY_E_OS_INVALID_ENV_OVERRIDE") - 1U);
    case SL_DIAG_OS_PROCESS_TIMEOUT:
        return sl_diag_literal("SLOPPY_E_OS_PROCESS_TIMEOUT",
                               sizeof("SLOPPY_E_OS_PROCESS_TIMEOUT") - 1U);
    case SL_DIAG_OS_PROCESS_CANCELLED:
        return sl_diag_literal("SLOPPY_E_OS_PROCESS_CANCELLED",
                               sizeof("SLOPPY_E_OS_PROCESS_CANCELLED") - 1U);
    case SL_DIAG_OS_PROCESS_KILLED:
        return sl_diag_literal("SLOPPY_E_OS_PROCESS_KILLED",
                               sizeof("SLOPPY_E_OS_PROCESS_KILLED") - 1U);
    case SL_DIAG_OS_PROCESS_START_FAILED:
        return sl_diag_literal("SLOPPY_E_OS_PROCESS_START_FAILED",
                               sizeof("SLOPPY_E_OS_PROCESS_START_FAILED") - 1U);
    case SL_DIAG_OS_PIPE_CLOSED:
        return sl_diag_literal("SLOPPY_E_OS_PIPE_CLOSED", sizeof("SLOPPY_E_OS_PIPE_CLOSED") - 1U);
    case SL_DIAG_OS_UNSUPPORTED_PLATFORM_SIGNAL:
        return sl_diag_literal("SLOPPY_E_OS_UNSUPPORTED_PLATFORM_SIGNAL",
                               sizeof("SLOPPY_E_OS_UNSUPPORTED_PLATFORM_SIGNAL") - 1U);
    case SL_DIAG_OS_SIGNAL_HANDLER_FAILURE:
        return sl_diag_literal("SLOPPY_E_OS_SIGNAL_HANDLER_FAILURE",
                               sizeof("SLOPPY_E_OS_SIGNAL_HANDLER_FAILURE") - 1U);
    default:
        return sl_diag_literal("SLOPPY_E_UNKNOWN", sizeof("SLOPPY_E_UNKNOWN") - 1U);
    }
}

static bool sl_diag_is_os_code(SlDiagCode code)
{
    switch (code) {
    case SL_DIAG_OS_FEATURE_UNAVAILABLE:
    case SL_DIAG_OS_ENV_ACCESS_DENIED:
    case SL_DIAG_OS_ENV_SECRET_REDACTED:
    case SL_DIAG_OS_PROCESS_EXECUTION_DENIED:
    case SL_DIAG_OS_SHELL_EXECUTION_DENIED:
    case SL_DIAG_OS_COMMAND_NOT_FOUND:
    case SL_DIAG_OS_INVALID_CWD:
    case SL_DIAG_OS_INVALID_ENV_OVERRIDE:
    case SL_DIAG_OS_PROCESS_TIMEOUT:
    case SL_DIAG_OS_PROCESS_CANCELLED:
    case SL_DIAG_OS_PROCESS_KILLED:
    case SL_DIAG_OS_PROCESS_START_FAILED:
    case SL_DIAG_OS_PIPE_CLOSED:
    case SL_DIAG_OS_UNSUPPORTED_PLATFORM_SIGNAL:
    case SL_DIAG_OS_SIGNAL_HANDLER_FAILURE:
        return true;
    default:
        return false;
    }
}

static SlStr sl_diag_core_code_name(SlDiagCode code)
{
    switch (code) {
    case SL_DIAG_NONE:
        return sl_diag_literal("SLOPPY_NONE", sizeof("SLOPPY_NONE") - 1U);
    case SL_DIAG_INVALID_ARGUMENT:
        return sl_diag_literal("SLOPPY_E_INVALID_ARGUMENT",
                               sizeof("SLOPPY_E_INVALID_ARGUMENT") - 1U);
    case SL_DIAG_OUT_OF_MEMORY:
        return sl_diag_literal("SLOPPY_E_OUT_OF_MEMORY", sizeof("SLOPPY_E_OUT_OF_MEMORY") - 1U);
    case SL_DIAG_OVERFLOW:
        return sl_diag_literal("SLOPPY_E_OVERFLOW", sizeof("SLOPPY_E_OVERFLOW") - 1U);
    case SL_DIAG_INVALID_PLAN_VERSION:
        return sl_diag_literal("SLOPPY_E_INVALID_PLAN_VERSION",
                               sizeof("SLOPPY_E_INVALID_PLAN_VERSION") - 1U);
    case SL_DIAG_MISSING_SERVICE:
        return sl_diag_literal("SLOPPY_E_MISSING_SERVICE", sizeof("SLOPPY_E_MISSING_SERVICE") - 1U);
    case SL_DIAG_PERMISSION_DENIED:
        return sl_diag_literal("SLOPPY_E_PERMISSION_DENIED",
                               sizeof("SLOPPY_E_PERMISSION_DENIED") - 1U);
    case SL_DIAG_INTERNAL_ERROR:
        return sl_diag_literal("SLOPPY_E_INTERNAL", sizeof("SLOPPY_E_INTERNAL") - 1U);
    default:
        return sl_diag_literal("SLOPPY_E_UNKNOWN", sizeof("SLOPPY_E_UNKNOWN") - 1U);
    }
}

static bool sl_diag_str_is_valid(SlStr str)
{
    return str.length == 0U || str.ptr != NULL;
}

static bool sl_diag_span_is_valid(SlSourceSpan span)
{
    if (!sl_diag_str_is_valid(span.path)) {
        return false;
    }
    return !span.has_location || (span.line != 0U && span.column != 0U);
}

static SlStatus sl_diag_validate_render_input(const SlDiag* diag)
{
    if (diag == NULL || !sl_diag_str_is_valid(diag->message) ||
        !sl_diag_span_is_valid(diag->primary_span) || diag->related_count > SL_DIAG_MAX_RELATED ||
        diag->hint_count > SL_DIAG_MAX_HINTS)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    for (size_t index = 0U; index < diag->related_count; index += 1U) {
        if (!sl_diag_str_is_valid(diag->related[index].message) ||
            !sl_diag_span_is_valid(diag->related[index].span))
        {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
    }

    for (size_t index = 0U; index < diag->hint_count; index += 1U) {
        if (!sl_diag_str_is_valid(diag->hints[index])) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
    }

    return sl_status_ok();
}

static SlStatus sl_diag_copy_str(SlArena* arena, SlStr src, SlStr* out)
{
    SlSlice copied = {0};
    SlStatus status;

    if (arena == NULL || out == NULL || !sl_diag_str_is_valid(src)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (src.length == 0U) {
        *out = sl_str_empty();
        return sl_status_ok();
    }

    status = sl_arena_array_copy(arena, src.ptr, src.length, sizeof(char), _Alignof(char), &copied);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    *out = sl_str_from_parts((const char*)copied.ptr, src.length);
    return sl_status_ok();
}

static SlStatus sl_diag_copy_span(SlArena* arena, SlSourceSpan src, SlSourceSpan* out)
{
    SlSourceSpan copy = src;
    SlStatus status = sl_diag_copy_str(arena, src.path, &copy.path);

    if (!sl_status_is_ok(status)) {
        return status;
    }

    *out = copy;
    return sl_status_ok();
}

static size_t sl_diag_decimal_len(size_t value)
{
    size_t length = 1U;

    while (value >= 10U) {
        value /= 10U;
        length += 1U;
    }

    return length;
}

static SlStatus sl_diag_builder_init_for_render(SlArena* arena, size_t capacity,
                                                SlStringBuilder* builder)
{
    if (arena == NULL || builder == NULL || capacity == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    return sl_string_builder_init_arena(builder, arena, capacity, capacity);
}

static SlStatus sl_diag_builder_append_literal(SlStringBuilder* builder, const char* literal,
                                               size_t length)
{
    return sl_string_builder_append_str(builder, sl_diag_literal(literal, length));
}

static SlStatus sl_diag_builder_append_repeat(SlStringBuilder* builder, char value, size_t count)
{
    size_t index = 0U;
    SlStatus status;

    for (index = 0U; index < count; index += 1U) {
        status = sl_string_builder_append_char(builder, value);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    return sl_status_ok();
}

static SlStatus sl_diag_add_len(size_t* total, size_t addend)
{
    return sl_checked_add_size(*total, addend, total);
}

static SlStatus sl_diag_add_str_len(size_t* total, SlStr str)
{
    if (!sl_diag_str_is_valid(str)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    return sl_diag_add_len(total, str.length);
}

static SlStr sl_diag_render_path(SlSourceSpan span)
{
    if (span.path.length == 0U) {
        return sl_diag_literal("<unknown>", sizeof("<unknown>") - 1U);
    }

    return span.path;
}

static SlStatus sl_diag_span_render_len(size_t* total, SlSourceSpan span)
{
    SlStatus status = sl_diag_add_str_len(total, sl_diag_render_path(span));

    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (!span.has_location) {
        return sl_status_ok();
    }

    status = sl_diag_add_len(total, 1U + sl_diag_decimal_len(span.line));
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_diag_add_len(total, 1U + sl_diag_decimal_len(span.column));
    if (!sl_status_is_ok(status) || span.length == 0U) {
        return status;
    }

    return sl_diag_add_len(total, 7U + sl_diag_decimal_len(span.length));
}

static SlStatus sl_diag_builder_append_span(SlStringBuilder* builder, SlSourceSpan span)
{
    SlStatus status = sl_string_builder_append_str(builder, sl_diag_render_path(span));

    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (span.has_location) {
        status = sl_string_builder_append_char(builder, ':');
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_string_builder_append_size(builder, span.line);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_string_builder_append_char(builder, ':');
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_string_builder_append_size(builder, span.column);
        if (!sl_status_is_ok(status)) {
            return status;
        }

        if (span.length != 0U) {
            status = sl_diag_builder_append_literal(builder, " (len ", 6U);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            status = sl_string_builder_append_size(builder, span.length);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            status = sl_string_builder_append_char(builder, ')');
            if (!sl_status_is_ok(status)) {
                return status;
            }
        }
    }

    return sl_status_ok();
}

static SlStr sl_diag_first_hint(const SlDiag* diag)
{
    if (diag == NULL || diag->hint_count == 0U) {
        return sl_str_empty();
    }
    return diag->hints[0];
}

static bool sl_diag_has_span(SlSourceSpan span)
{
    return span.has_location || span.path.length != 0U;
}

static SlStatus sl_diag_header_render_len(size_t* total, const SlDiag* diag)
{
    SlStatus status;

    status = sl_diag_add_str_len(total, sl_diag_severity_name(diag->severity));
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_diag_add_len(total, 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_diag_add_str_len(total, sl_diag_code_name(diag->code));
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_diag_add_len(total, 2U);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_diag_add_str_len(total, diag->message);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_diag_add_len(total, 1U);
}

static SlStatus sl_diag_primary_render_len(size_t* total, SlSourceSpan span)
{
    SlStatus status;

    if (!sl_diag_has_span(span)) {
        return sl_status_ok();
    }

    status = sl_diag_add_len(total, 6U);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_diag_span_render_len(total, span);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_diag_add_len(total, 1U);
}

static SlStatus sl_diag_related_render_len(size_t* total, const SlDiag* diag)
{
    SlStatus status;
    size_t index = 0U;

    if (diag->related_count == 0U) {
        return sl_status_ok();
    }

    status = sl_diag_add_len(total, 12U);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    for (index = 0U; index < diag->related_count; index += 1U) {
        if (!sl_diag_str_is_valid(diag->related[index].message)) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }

        status = sl_diag_add_len(total, 4U);
        if (!sl_status_is_ok(status)) {
            return status;
        }

        status = sl_diag_span_render_len(total, diag->related[index].span);
        if (!sl_status_is_ok(status)) {
            return status;
        }

        status = sl_diag_add_len(total, 2U);
        if (!sl_status_is_ok(status)) {
            return status;
        }

        status = sl_diag_add_str_len(total, diag->related[index].message);
        if (!sl_status_is_ok(status)) {
            return status;
        }

        status = sl_diag_add_len(total, 1U);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    return sl_status_ok();
}

static SlStatus sl_diag_hints_render_len(size_t* total, const SlDiag* diag)
{
    SlStatus status;
    size_t index = 0U;

    if (diag->hint_count == 0U) {
        return sl_status_ok();
    }

    status = sl_diag_add_len(total, 9U);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    for (index = 0U; index < diag->hint_count; index += 1U) {
        if (!sl_diag_str_is_valid(diag->hints[index])) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }

        status = sl_diag_add_len(total, 4U);
        if (!sl_status_is_ok(status)) {
            return status;
        }

        status = sl_diag_add_str_len(total, diag->hints[index]);
        if (!sl_status_is_ok(status)) {
            return status;
        }

        status = sl_diag_add_len(total, 1U);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    return sl_status_ok();
}

static SlStatus sl_diag_render_len(const SlDiag* diag, size_t* out)
{
    SlStatus status;
    size_t total = 0U;

    status = sl_diag_validate_render_input(diag);
    if (!sl_status_is_ok(status) || out == NULL) {
        return out == NULL ? sl_status_from_code(SL_STATUS_INVALID_ARGUMENT) : status;
    }

    status = sl_diag_header_render_len(&total, diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_diag_primary_render_len(&total, diag->primary_span);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_diag_related_render_len(&total, diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_diag_hints_render_len(&total, diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    *out = total;
    return sl_status_ok();
}

SlStr sl_diag_severity_name(SlDiagSeverity severity)
{
    switch (severity) {
    case SL_DIAG_SEVERITY_NOTE:
        return sl_diag_literal("note", sizeof("note") - 1U);
    case SL_DIAG_SEVERITY_WARNING:
        return sl_diag_literal("warning", sizeof("warning") - 1U);
    case SL_DIAG_SEVERITY_ERROR:
        return sl_diag_literal("error", sizeof("error") - 1U);
    case SL_DIAG_SEVERITY_FATAL:
        return sl_diag_literal("fatal", sizeof("fatal") - 1U);
    default:
        return sl_diag_literal("unknown", sizeof("unknown") - 1U);
    }
}

SlStr sl_diag_code_name(SlDiagCode code)
{
    if (sl_diag_is_http_code(code)) {
        return sl_diag_http_code_name(code);
    }
    if (sl_diag_is_lifecycle_code(code)) {
        return sl_diag_lifecycle_code_name(code);
    }
    if (sl_diag_is_time_code(code)) {
        return sl_diag_time_code_name(code);
    }
    if (sl_diag_is_crypto_code(code)) {
        return sl_diag_crypto_code_name(code);
    }
    if (sl_diag_is_codec_code(code)) {
        return sl_diag_codec_code_name(code);
    }
    if (sl_diag_is_net_code(code)) {
        return sl_diag_net_code_name(code);
    }
    if (sl_diag_is_os_code(code)) {
        return sl_diag_os_code_name(code);
    }
    if (sl_diag_is_workers_code(code)) {
        return sl_diag_workers_code_name(code);
    }

    SlStr core_name = sl_diag_core_code_name(code);
    if (!sl_str_equal(core_name,
                      sl_diag_literal("SLOPPY_E_UNKNOWN", sizeof("SLOPPY_E_UNKNOWN") - 1U)))
    {
        return core_name;
    }

    switch (code) {
    case SL_DIAG_INVALID_PLAN_FIELD:
        return sl_diag_literal("SLOPPY_E_INVALID_PLAN_FIELD",
                               sizeof("SLOPPY_E_INVALID_PLAN_FIELD") - 1U);
    case SL_DIAG_DUPLICATE_HANDLER_ID:
        return sl_diag_literal("SLOPPY_E_DUPLICATE_HANDLER_ID",
                               sizeof("SLOPPY_E_DUPLICATE_HANDLER_ID") - 1U);
    case SL_DIAG_MALFORMED_JSON:
        return sl_diag_literal("SLOPPY_E_MALFORMED_JSON", sizeof("SLOPPY_E_MALFORMED_JSON") - 1U);
    case SL_DIAG_UNSUPPORTED_ENGINE:
        return sl_diag_literal("SLOPPY_E_UNSUPPORTED_ENGINE",
                               sizeof("SLOPPY_E_UNSUPPORTED_ENGINE") - 1U);
    case SL_DIAG_ENGINE_EXCEPTION:
        return sl_diag_literal("SLOPPY_E_ENGINE_EXCEPTION",
                               sizeof("SLOPPY_E_ENGINE_EXCEPTION") - 1U);
    case SL_DIAG_ENGINE_COMPILE_ERROR:
        return sl_diag_literal("SLOPPY_E_ENGINE_COMPILE_ERROR",
                               sizeof("SLOPPY_E_ENGINE_COMPILE_ERROR") - 1U);
    case SL_DIAG_ENGINE_CALL_ERROR:
        return sl_diag_literal("SLOPPY_E_ENGINE_CALL_ERROR",
                               sizeof("SLOPPY_E_ENGINE_CALL_ERROR") - 1U);
    case SL_DIAG_ENGINE_PROMISE_REJECTION:
        return sl_diag_literal("SLOPPY_E_ENGINE_PROMISE_REJECTION",
                               sizeof("SLOPPY_E_ENGINE_PROMISE_REJECTION") - 1U);
    case SL_DIAG_ENGINE_PROMISE_PENDING:
        return sl_diag_literal("SLOPPY_E_ENGINE_PROMISE_PENDING",
                               sizeof("SLOPPY_E_ENGINE_PROMISE_PENDING") - 1U);
    case SL_DIAG_ENGINE_CANCELLED:
        return sl_diag_literal("SLOPPY_E_ENGINE_CANCELLED",
                               sizeof("SLOPPY_E_ENGINE_CANCELLED") - 1U);
    case SL_DIAG_ENGINE_BACKPRESSURE:
        return sl_diag_literal("SLOPPY_E_ENGINE_BACKPRESSURE",
                               sizeof("SLOPPY_E_ENGINE_BACKPRESSURE") - 1U);
    case SL_DIAG_APP_LIFECYCLE:
        return sl_diag_literal("SLOPPY_E_APP_LIFECYCLE", sizeof("SLOPPY_E_APP_LIFECYCLE") - 1U);
    case SL_DIAG_INVALID_ROUTE_PATTERN:
        return sl_diag_literal("SLOPPY_E_INVALID_ROUTE_PATTERN",
                               sizeof("SLOPPY_E_INVALID_ROUTE_PATTERN") - 1U);
    case SL_DIAG_DUPLICATE_ROUTE_PARAM:
        return sl_diag_literal("SLOPPY_E_DUPLICATE_ROUTE_PARAM",
                               sizeof("SLOPPY_E_DUPLICATE_ROUTE_PARAM") - 1U);
    case SL_DIAG_SQLITE_PROVIDER_ERROR:
        return sl_diag_literal("SLOPPY_E_SQLITE_PROVIDER", sizeof("SLOPPY_E_SQLITE_PROVIDER") - 1U);
    case SL_DIAG_DATABASE_UNSUPPORTED_VALUE:
        return sl_diag_literal("SLOPPY_E_DATABASE_UNSUPPORTED_VALUE",
                               sizeof("SLOPPY_E_DATABASE_UNSUPPORTED_VALUE") - 1U);
    case SL_DIAG_POSTGRES_PROVIDER_ERROR:
        return sl_diag_literal("SLOPPY_E_POSTGRES_PROVIDER",
                               sizeof("SLOPPY_E_POSTGRES_PROVIDER") - 1U);
    case SL_DIAG_POSTGRES_POOL_EXHAUSTED:
        return sl_diag_literal("SLOPPY_E_POSTGRES_POOL_EXHAUSTED",
                               sizeof("SLOPPY_E_POSTGRES_POOL_EXHAUSTED") - 1U);
    case SL_DIAG_SQLSERVER_PROVIDER_ERROR:
        return sl_diag_literal("SLOPPY_E_SQLSERVER_PROVIDER",
                               sizeof("SLOPPY_E_SQLSERVER_PROVIDER") - 1U);
    case SL_DIAG_SQLSERVER_POOL_EXHAUSTED:
        return sl_diag_literal("SLOPPY_E_SQLSERVER_POOL_EXHAUSTED",
                               sizeof("SLOPPY_E_SQLSERVER_POOL_EXHAUSTED") - 1U);
    case SL_DIAG_RESOURCE_INVALID_ID:
        return sl_diag_literal("SLOPPY_E_RESOURCE_INVALID_ID",
                               sizeof("SLOPPY_E_RESOURCE_INVALID_ID") - 1U);
    case SL_DIAG_RESOURCE_STALE_ID:
        return sl_diag_literal("SLOPPY_E_RESOURCE_STALE_ID",
                               sizeof("SLOPPY_E_RESOURCE_STALE_ID") - 1U);
    case SL_DIAG_RESOURCE_WRONG_KIND:
        return sl_diag_literal("SLOPPY_E_RESOURCE_WRONG_KIND",
                               sizeof("SLOPPY_E_RESOURCE_WRONG_KIND") - 1U);
    case SL_DIAG_RESOURCE_CLOSED:
        return sl_diag_literal("SLOPPY_E_RESOURCE_CLOSED", sizeof("SLOPPY_E_RESOURCE_CLOSED") - 1U);
    case SL_DIAG_RESOURCE_TABLE_EXHAUSTED:
        return sl_diag_literal("SLOPPY_E_RESOURCE_TABLE_EXHAUSTED",
                               sizeof("SLOPPY_E_RESOURCE_TABLE_EXHAUSTED") - 1U);
    case SL_DIAG_DUPLICATE_ROUTE:
        return sl_diag_literal("SLOPPY_E_DUPLICATE_ROUTE", sizeof("SLOPPY_E_DUPLICATE_ROUTE") - 1U);
    case SL_DIAG_UNKNOWN_RUNTIME_FEATURE:
        return sl_diag_literal("SLOPPY_E_UNKNOWN_RUNTIME_FEATURE",
                               sizeof("SLOPPY_E_UNKNOWN_RUNTIME_FEATURE") - 1U);
    case SL_DIAG_UNAVAILABLE_RUNTIME_FEATURE:
        return sl_diag_literal("SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE",
                               sizeof("SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE") - 1U);
    case SL_DIAG_RUNTIME_FEATURE_DEPENDENCY_MISSING:
        return sl_diag_literal("SLOPPY_E_RUNTIME_FEATURE_DEPENDENCY_MISSING",
                               sizeof("SLOPPY_E_RUNTIME_FEATURE_DEPENDENCY_MISSING") - 1U);
    case SL_DIAG_REQUEST_BINDING_FAILED:
        return sl_diag_literal("SLOPPY_E_REQUEST_BINDING_FAILED",
                               sizeof("SLOPPY_E_REQUEST_BINDING_FAILED") - 1U);
    case SL_DIAG_REQUEST_VALIDATION_FAILED:
        return sl_diag_literal("SLOPPY_E_REQUEST_VALIDATION_FAILED",
                               sizeof("SLOPPY_E_REQUEST_VALIDATION_FAILED") - 1U);
    case SL_DIAG_UNSUPPORTED_MODEL_SCHEMA:
        return sl_diag_literal("SLOPPY_E_UNSUPPORTED_MODEL_SCHEMA",
                               sizeof("SLOPPY_E_UNSUPPORTED_MODEL_SCHEMA") - 1U);
    case SL_DIAG_FFI_RUNTIME_UNAVAILABLE:
        return sl_diag_literal("SLOPPY_E_FFI_RUNTIME_UNAVAILABLE",
                               sizeof("SLOPPY_E_FFI_RUNTIME_UNAVAILABLE") - 1U);
    case SL_DIAG_FFI_LIBRARY_NOT_FOUND:
        return sl_diag_literal("SLOPPY_E_FFI_LIBRARY_NOT_FOUND",
                               sizeof("SLOPPY_E_FFI_LIBRARY_NOT_FOUND") - 1U);
    case SL_DIAG_FFI_SYMBOL_NOT_FOUND:
        return sl_diag_literal("SLOPPY_E_FFI_SYMBOL_NOT_FOUND",
                               sizeof("SLOPPY_E_FFI_SYMBOL_NOT_FOUND") - 1U);
    case SL_DIAG_FFI_UNSUPPORTED_CALLING_CONVENTION:
        return sl_diag_literal("SLOPPY_E_FFI_UNSUPPORTED_CALLING_CONVENTION",
                               sizeof("SLOPPY_E_FFI_UNSUPPORTED_CALLING_CONVENTION") - 1U);
    case SL_DIAG_FFI_INVALID_ARGUMENT_COUNT:
        return sl_diag_literal("SLOPPY_E_FFI_INVALID_ARGUMENT_COUNT",
                               sizeof("SLOPPY_E_FFI_INVALID_ARGUMENT_COUNT") - 1U);
    case SL_DIAG_FFI_INVALID_ARGUMENT_TYPE:
        return sl_diag_literal("SLOPPY_E_FFI_INVALID_ARGUMENT_TYPE",
                               sizeof("SLOPPY_E_FFI_INVALID_ARGUMENT_TYPE") - 1U);
    case SL_DIAG_FFI_INTEGER_OUT_OF_RANGE:
        return sl_diag_literal("SLOPPY_E_FFI_INTEGER_OUT_OF_RANGE",
                               sizeof("SLOPPY_E_FFI_INTEGER_OUT_OF_RANGE") - 1U);
    case SL_DIAG_FFI_STRING_NUL:
        return sl_diag_literal("SLOPPY_E_FFI_STRING_NUL", sizeof("SLOPPY_E_FFI_STRING_NUL") - 1U);
    case SL_DIAG_FFI_CALL_FAILED:
        return sl_diag_literal("SLOPPY_E_FFI_CALL_FAILED", sizeof("SLOPPY_E_FFI_CALL_FAILED") - 1U);
    default:
        return sl_diag_literal("SLOPPY_E_UNKNOWN", sizeof("SLOPPY_E_UNKNOWN") - 1U);
    }
}

SlStr sl_diag_redacted(void)
{
    return sl_diag_literal("<redacted>", sizeof("<redacted>") - 1U);
}

SlSourceSpan sl_source_span_unknown(void)
{
    SlSourceSpan span = {0};
    span.path = sl_str_empty();
    return span;
}

SlSourceSpan sl_source_span_make(SlStr path, size_t line, size_t column, size_t length)
{
    SlSourceSpan span = {0};

    span.path = path;
    span.line = line;
    span.column = column;
    span.length = length;
    span.has_location = line != 0U && column != 0U;

    return span;
}

SlStatus sl_diag_builder_init(SlDiagBuilder* builder, SlArena* arena, SlDiagSeverity severity,
                              SlDiagCode code, SlStr message)
{
    SlDiag diag = {0};
    SlStr copied_message = {0};
    SlStatus status;

    if (builder == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_diag_copy_str(arena, message, &copied_message);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    diag.severity = severity;
    diag.code = code;
    diag.message = copied_message;
    diag.primary_span = sl_source_span_unknown();

    builder->arena = arena;
    builder->diag = diag;
    return sl_status_ok();
}

SlStatus sl_diag_builder_set_primary_span(SlDiagBuilder* builder, SlSourceSpan span)
{
    SlSourceSpan copied_span;
    SlStatus status;

    if (builder == NULL || builder->arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_diag_copy_span(builder->arena, span, &copied_span);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    builder->diag.primary_span = copied_span;
    return sl_status_ok();
}

SlStatus sl_diag_builder_add_related(SlDiagBuilder* builder, SlSourceSpan span, SlStr message)
{
    SlDiagRelated related;
    SlArenaMark mark;
    SlStatus status;

    if (builder == NULL || builder->arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (builder->diag.related_count >= SL_DIAG_MAX_RELATED) {
        return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
    }

    mark = sl_arena_mark(builder->arena);
    status = sl_diag_copy_span(builder->arena, span, &related.span);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_diag_copy_str(builder->arena, message, &related.message);
    if (!sl_status_is_ok(status)) {
        SlStatus reset_status = sl_arena_reset_to(builder->arena, mark);
        if (!sl_status_is_ok(reset_status)) {
            return reset_status;
        }

        return status;
    }

    builder->diag.related[builder->diag.related_count] = related;
    builder->diag.related_count += 1U;
    return sl_status_ok();
}

SlStatus sl_diag_builder_add_hint(SlDiagBuilder* builder, SlStr hint)
{
    SlStr copied_hint;
    SlStatus status;

    if (builder == NULL || builder->arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (builder->diag.hint_count >= SL_DIAG_MAX_HINTS) {
        return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
    }

    status = sl_diag_copy_str(builder->arena, hint, &copied_hint);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    builder->diag.hints[builder->diag.hint_count] = copied_hint;
    builder->diag.hint_count += 1U;
    return sl_status_ok();
}

SlStatus sl_diag_builder_add_hint_owned(SlDiagBuilder* builder, SlStr hint)
{
    if (builder == NULL || builder->arena == NULL || !sl_diag_str_is_valid(hint)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (builder->diag.hint_count >= SL_DIAG_MAX_HINTS) {
        return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
    }

    builder->diag.hints[builder->diag.hint_count] = hint;
    builder->diag.hint_count += 1U;
    return sl_status_ok();
}

SlStatus sl_diag_builder_finish(SlDiagBuilder* builder, SlDiag* out)
{
    if (builder == NULL || builder->arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out = builder->diag;
    return sl_status_ok();
}

static SlStatus sl_diag_builder_append_header(SlStringBuilder* builder, const SlDiag* diag)
{
    SlStatus status = sl_string_builder_append_str(builder, sl_diag_severity_name(diag->severity));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_string_builder_append_char(builder, ' ');
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_string_builder_append_str(builder, sl_diag_code_name(diag->code));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_literal(builder, ": ", 2U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_string_builder_append_str(builder, diag->message);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_string_builder_append_char(builder, '\n');
}

static SlStatus sl_diag_builder_append_primary(SlStringBuilder* builder, const SlDiag* diag)
{
    SlStatus status;

    if (diag->primary_span.has_location || diag->primary_span.path.length != 0U) {
        status = sl_diag_builder_append_literal(builder, "\n  at ", 6U);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_diag_builder_append_span(builder, diag->primary_span);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        return sl_string_builder_append_char(builder, '\n');
    }

    return sl_status_ok();
}

static SlStatus sl_diag_builder_append_related(SlStringBuilder* builder, const SlDiag* diag)
{
    size_t index = 0U;
    SlStatus status;

    if (diag->related_count == 0U) {
        return sl_status_ok();
    }

    status = sl_diag_builder_append_literal(builder, "\n  related:\n", 12U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    for (index = 0U; index < diag->related_count; index += 1U) {
        status = sl_diag_builder_append_literal(builder, "    ", 4U);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_diag_builder_append_span(builder, diag->related[index].span);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_diag_builder_append_literal(builder, ": ", 2U);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_string_builder_append_str(builder, diag->related[index].message);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_string_builder_append_char(builder, '\n');
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    return sl_status_ok();
}

static SlStatus sl_diag_builder_append_hints(SlStringBuilder* builder, const SlDiag* diag)
{
    size_t index = 0U;
    SlStatus status;

    if (diag->hint_count == 0U) {
        return sl_status_ok();
    }

    status = sl_diag_builder_append_literal(builder, "\n  help:\n", 9U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    for (index = 0U; index < diag->hint_count; index += 1U) {
        status = sl_diag_builder_append_literal(builder, "    ", 4U);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_string_builder_append_str(builder, diag->hints[index]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_string_builder_append_char(builder, '\n');
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    return sl_status_ok();
}

SlStatus sl_diag_render_text(SlArena* arena, const SlDiag* diag, SlStr* out)
{
    SlStringBuilder builder;
    SlStatus status;
    size_t length = 0U;

    if (arena == NULL || diag == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_diag_render_len(diag, &length);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_init_for_render(arena, length, &builder);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_header(&builder, diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_primary(&builder, diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_related(&builder, diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_hints(&builder, diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    *out = sl_string_builder_view(&builder);
    return sl_status_ok();
}

static bool sl_diag_source_matches(const SlDiag* diag, const SlDiagSource* source)
{
    if (diag == NULL || source == NULL || !diag->primary_span.has_location ||
        !sl_diag_str_is_valid(source->path) || !sl_diag_str_is_valid(source->text) ||
        sl_str_is_empty(source->text))
    {
        return false;
    }
    if (!sl_str_is_empty(diag->primary_span.path)) {
        return !sl_str_is_empty(source->path) &&
               sl_str_equal(diag->primary_span.path, source->path);
    }
    return sl_str_is_empty(source->path);
}

static bool sl_diag_source_line(SlStr source, size_t line, SlStr* out)
{
    size_t current_line = 1U;
    size_t start = 0U;
    size_t index = 0U;

    if (out == NULL || line == 0U || !sl_diag_str_is_valid(source)) {
        return false;
    }
    while (index <= source.length) {
        if (index == source.length || source.ptr[index] == '\n') {
            size_t end = index;
            if (end > start && source.ptr[end - 1U] == '\r') {
                end -= 1U;
            }
            if (current_line == line) {
                *out = sl_str_from_parts(source.ptr + start, end - start);
                return true;
            }
            current_line += 1U;
            start = index + 1U;
        }
        index += 1U;
    }
    return false;
}

static SlStatus sl_diag_frame_render_len(size_t* total, const SlDiag* diag, SlStr line)
{
    SlStatus status;
    const size_t line_number = diag->primary_span.line;
    const size_t underline = diag->primary_span.length == 0U ? 1U : diag->primary_span.length;
    SlStr hint = sl_diag_first_hint(diag);

    status = sl_diag_add_len(total, 6U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_span_render_len(total, diag->primary_span);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_add_len(total, 9U + sl_diag_decimal_len(line_number) + line.length);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_add_len(total, 7U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_add_len(total, diag->primary_span.column - 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_add_len(total, underline);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (!sl_str_is_empty(hint)) {
        status = sl_diag_add_len(total, 1U + hint.length);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    return sl_diag_add_len(total, 1U);
}

static SlStatus sl_diag_builder_append_frame(SlStringBuilder* builder, const SlDiag* diag,
                                             SlStr line)
{
    const size_t underline = diag->primary_span.length == 0U ? 1U : diag->primary_span.length;
    SlStr hint = sl_diag_first_hint(diag);
    SlStatus status;

    status = sl_diag_builder_append_literal(builder, "  --> ", 6U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_span(builder, diag->primary_span);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_literal(builder, "\n   |\n", 6U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_string_builder_append_char(builder, ' ');
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_string_builder_append_size(builder, diag->primary_span.line);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_literal(builder, " | ", 3U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_string_builder_append_str(builder, line);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_literal(builder, "\n   | ", 6U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_repeat(builder, ' ', diag->primary_span.column - 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_repeat(builder, '^', underline);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (!sl_str_is_empty(hint)) {
        status = sl_string_builder_append_char(builder, ' ');
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_string_builder_append_str(builder, hint);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    return sl_string_builder_append_char(builder, '\n');
}

SlStatus sl_diag_render_text_with_source(SlArena* arena, const SlDiag* diag,
                                         const SlDiagSource* source, SlStr* out)
{
    SlStringBuilder builder;
    SlStatus status;
    SlStr line = sl_str_empty();
    size_t length = 0U;

    status = sl_diag_validate_render_input(diag);
    if (!sl_status_is_ok(status) || arena == NULL || out == NULL) {
        return arena == NULL || out == NULL ? sl_status_from_code(SL_STATUS_INVALID_ARGUMENT)
                                            : status;
    }

    if (!sl_diag_source_matches(diag, source) ||
        !sl_diag_source_line(source->text, diag->primary_span.line, &line))
    {
        return sl_diag_render_text(arena, diag, out);
    }

    status = sl_diag_header_render_len(&length, diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_add_len(&length, 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_frame_render_len(&length, diag, line);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_init_for_render(arena, length, &builder);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_string_builder_append_str(&builder, sl_diag_severity_name(diag->severity));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_string_builder_append_char(&builder, ' ');
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_string_builder_append_str(&builder, sl_diag_code_name(diag->code));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_literal(&builder, ": ", 2U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_string_builder_append_str(&builder, diag->message);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_literal(&builder, "\n\n", 2U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_frame(&builder, diag, line);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    *out = sl_string_builder_view(&builder);
    return sl_status_ok();
}

static SlStatus sl_diag_json_escaped_len(SlStr value, size_t* out)
{
    size_t length = 2U;
    size_t index = 0U;
    SlStatus status;

    if (out == NULL || !sl_diag_str_is_valid(value)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    for (index = 0U; index < value.length; index += 1U) {
        unsigned char ch = (unsigned char)value.ptr[index];
        if (ch == '"' || ch == '\\' || ch == '\n' || ch == '\r' || ch == '\t') {
            status = sl_checked_add_size(length, 2U, &length);
        }
        else if (ch < 0x20U) {
            status = sl_checked_add_size(length, 6U, &length);
        }
        else {
            status = sl_checked_add_size(length, 1U, &length);
        }
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    *out = length;
    return sl_status_ok();
}

static SlStatus sl_diag_add_json_escaped_len(size_t* total, SlStr value)
{
    size_t length = 0U;
    SlStatus status = sl_diag_json_escaped_len(value, &length);

    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_diag_add_len(total, length);
}

static char sl_diag_json_escape_letter(unsigned char ch)
{
    if (ch == '\n') {
        return 'n';
    }
    if (ch == '\r') {
        return 'r';
    }
    return 't';
}

static SlStatus sl_diag_builder_append_json_escaped(SlStringBuilder* builder, SlStr value)
{
    static const char hex[] = "0123456789abcdef";
    size_t index = 0U;
    SlStatus status;

    status = sl_string_builder_append_char(builder, '"');
    if (!sl_status_is_ok(status)) {
        return status;
    }
    for (index = 0U; index < value.length; index += 1U) {
        unsigned char ch = (unsigned char)value.ptr[index];
        if (ch == '"' || ch == '\\') {
            status = sl_string_builder_append_char(builder, '\\');
            if (!sl_status_is_ok(status)) {
                return status;
            }
            status = sl_string_builder_append_char(builder, (char)ch);
            if (!sl_status_is_ok(status)) {
                return status;
            }
        }
        else if (ch == '\n' || ch == '\r' || ch == '\t') {
            status = sl_string_builder_append_char(builder, '\\');
            if (!sl_status_is_ok(status)) {
                return status;
            }
            status = sl_string_builder_append_char(builder, sl_diag_json_escape_letter(ch));
            if (!sl_status_is_ok(status)) {
                return status;
            }
        }
        else if (ch < 0x20U) {
            status = sl_diag_builder_append_literal(builder, "\\u00", 4U);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            status = sl_string_builder_append_char(builder, hex[(ch >> 4U) & 0xFU]);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            status = sl_string_builder_append_char(builder, hex[ch & 0xFU]);
            if (!sl_status_is_ok(status)) {
                return status;
            }
        }
        else {
            status = sl_string_builder_append_char(builder, (char)ch);
            if (!sl_status_is_ok(status)) {
                return status;
            }
        }
    }
    return sl_string_builder_append_char(builder, '"');
}

static SlStatus sl_diag_json_span_len(size_t* total, SlSourceSpan span)
{
    SlStatus status = sl_diag_add_len(total, 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (!sl_str_is_empty(span.path)) {
        status = sl_diag_add_len(total, sizeof("\"file\":") - 1U);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_diag_add_json_escaped_len(total, span.path);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    if (span.has_location) {
        if (!sl_str_is_empty(span.path)) {
            status = sl_diag_add_len(total, 1U);
            if (!sl_status_is_ok(status)) {
                return status;
            }
        }
        status = sl_diag_add_len(total, sizeof("\"line\":") - 1U + sl_diag_decimal_len(span.line) +
                                            1U + sizeof("\"column\":") - 1U +
                                            sl_diag_decimal_len(span.column));
        if (!sl_status_is_ok(status)) {
            return status;
        }
        if (span.length != 0U) {
            status = sl_diag_add_len(total, 1U + sizeof("\"span\":") - 1U +
                                                sl_diag_decimal_len(span.length));
            if (!sl_status_is_ok(status)) {
                return status;
            }
        }
    }
    return sl_diag_add_len(total, 1U);
}

static SlStatus sl_diag_builder_append_json_span(SlStringBuilder* builder, SlSourceSpan span)
{
    bool need_comma = false;
    SlStatus status;

    status = sl_string_builder_append_char(builder, '{');
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (!sl_str_is_empty(span.path)) {
        status = sl_diag_builder_append_literal(builder, "\"file\":", sizeof("\"file\":") - 1U);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_diag_builder_append_json_escaped(builder, span.path);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        need_comma = true;
    }
    if (span.has_location) {
        if (need_comma) {
            status = sl_string_builder_append_char(builder, ',');
            if (!sl_status_is_ok(status)) {
                return status;
            }
        }
        status = sl_diag_builder_append_literal(builder, "\"line\":", sizeof("\"line\":") - 1U);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_string_builder_append_size(builder, span.line);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status =
            sl_diag_builder_append_literal(builder, ",\"column\":", sizeof(",\"column\":") - 1U);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_string_builder_append_size(builder, span.column);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        if (span.length != 0U) {
            status =
                sl_diag_builder_append_literal(builder, ",\"span\":", sizeof(",\"span\":") - 1U);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            status = sl_string_builder_append_size(builder, span.length);
            if (!sl_status_is_ok(status)) {
                return status;
            }
        }
    }
    return sl_string_builder_append_char(builder, '}');
}

static SlStatus sl_diag_json_base_len(size_t* total, const SlDiag* diag)
{
    SlStatus status = sl_diag_add_len(total, sizeof("{\"code\":") - 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_add_json_escaped_len(total, sl_diag_code_name(diag->code));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_add_len(total, sizeof(",\"severity\":") - 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_add_json_escaped_len(total, sl_diag_severity_name(diag->severity));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_add_len(total, sizeof(",\"message\":") - 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_diag_add_json_escaped_len(total, diag->message);
}

static SlStatus sl_diag_json_primary_len(size_t* total, const SlDiag* diag)
{
    SlStatus status;

    if (!sl_diag_has_span(diag->primary_span)) {
        return sl_status_ok();
    }
    status = sl_diag_add_len(total, sizeof(",\"primary\":") - 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_diag_json_span_len(total, diag->primary_span);
}

static SlStatus sl_diag_json_related_len(size_t* total, const SlDiag* diag)
{
    SlStatus status;
    size_t index = 0U;

    if (diag->related_count == 0U) {
        return sl_status_ok();
    }
    status = sl_diag_add_len(total, sizeof(",\"related\":[") - 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    for (index = 0U; index < diag->related_count; index += 1U) {
        if (!sl_diag_str_is_valid(diag->related[index].message)) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        if (index != 0U) {
            status = sl_diag_add_len(total, 1U);
            if (!sl_status_is_ok(status)) {
                return status;
            }
        }
        status = sl_diag_add_len(total, sizeof("{\"message\":") - 1U);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_diag_add_json_escaped_len(total, diag->related[index].message);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_diag_add_len(total, sizeof(",\"span\":") - 1U);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_diag_json_span_len(total, diag->related[index].span);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_diag_add_len(total, 1U);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    return sl_diag_add_len(total, 1U);
}

static SlStatus sl_diag_json_hints_len(size_t* total, const SlDiag* diag)
{
    SlStatus status;
    size_t index = 0U;

    if (diag->hint_count == 0U) {
        return sl_status_ok();
    }
    status = sl_diag_add_len(total, sizeof(",\"hints\":[") - 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    for (index = 0U; index < diag->hint_count; index += 1U) {
        if (!sl_diag_str_is_valid(diag->hints[index])) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        status = sl_diag_add_len(total, index == 0U ? 0U : 1U);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_diag_add_json_escaped_len(total, diag->hints[index]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    return sl_diag_add_len(total, 1U);
}

static SlStatus sl_diag_json_render_len(size_t* total, const SlDiag* diag)
{
    SlStatus status;

    if (total == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_diag_validate_render_input(diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_diag_json_base_len(total, diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_json_primary_len(total, diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_json_related_len(total, diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_json_hints_len(total, diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_diag_add_len(total, 2U);
}

static SlStatus sl_diag_builder_append_json_base(SlStringBuilder* builder, const SlDiag* diag)
{
    SlStatus status =
        sl_diag_builder_append_literal(builder, "{\"code\":", sizeof("{\"code\":") - 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_json_escaped(builder, sl_diag_code_name(diag->code));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status =
        sl_diag_builder_append_literal(builder, ",\"severity\":", sizeof(",\"severity\":") - 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_json_escaped(builder, sl_diag_severity_name(diag->severity));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_literal(builder, ",\"message\":", sizeof(",\"message\":") - 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_diag_builder_append_json_escaped(builder, diag->message);
}

static SlStatus sl_diag_builder_append_json_primary(SlStringBuilder* builder, const SlDiag* diag)
{
    if (sl_diag_has_span(diag->primary_span)) {
        SlStatus status =
            sl_diag_builder_append_literal(builder, ",\"primary\":", sizeof(",\"primary\":") - 1U);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        return sl_diag_builder_append_json_span(builder, diag->primary_span);
    }
    return sl_status_ok();
}

static SlStatus sl_diag_builder_append_json_related(SlStringBuilder* builder, const SlDiag* diag)
{
    size_t index = 0U;
    SlStatus status;

    if (diag->related_count != 0U) {
        status = sl_diag_builder_append_literal(builder, ",\"related\":[",
                                                sizeof(",\"related\":[") - 1U);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        for (index = 0U; index < diag->related_count; index += 1U) {
            if (index != 0U) {
                status = sl_string_builder_append_char(builder, ',');
                if (!sl_status_is_ok(status)) {
                    return status;
                }
            }
            status = sl_diag_builder_append_literal(builder,
                                                    "{\"message\":", sizeof("{\"message\":") - 1U);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            status = sl_diag_builder_append_json_escaped(builder, diag->related[index].message);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            status =
                sl_diag_builder_append_literal(builder, ",\"span\":", sizeof(",\"span\":") - 1U);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            status = sl_diag_builder_append_json_span(builder, diag->related[index].span);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            status = sl_string_builder_append_char(builder, '}');
            if (!sl_status_is_ok(status)) {
                return status;
            }
        }
        return sl_string_builder_append_char(builder, ']');
    }
    return sl_status_ok();
}

static SlStatus sl_diag_builder_append_json_hints(SlStringBuilder* builder, const SlDiag* diag)
{
    size_t index = 0U;
    SlStatus status;

    if (diag->hint_count != 0U) {
        status =
            sl_diag_builder_append_literal(builder, ",\"hints\":[", sizeof(",\"hints\":[") - 1U);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        for (index = 0U; index < diag->hint_count; index += 1U) {
            if (index != 0U) {
                status = sl_string_builder_append_char(builder, ',');
                if (!sl_status_is_ok(status)) {
                    return status;
                }
            }
            status = sl_diag_builder_append_json_escaped(builder, diag->hints[index]);
            if (!sl_status_is_ok(status)) {
                return status;
            }
        }
        return sl_string_builder_append_char(builder, ']');
    }
    return sl_status_ok();
}

static SlStatus sl_diag_json_source_frame_fields_len(size_t* total, const SlDiag* diag, SlStr line,
                                                     size_t underline)
{
    SlStatus status = sl_diag_add_len(total, sizeof("\"file\":") - 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_add_json_escaped_len(total, diag->primary_span.path);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_add_len(total, sizeof(",\"line\":") - 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_add_len(total, sl_diag_decimal_len(diag->primary_span.line));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_add_len(total, sizeof(",\"column\":") - 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_add_len(total, sl_diag_decimal_len(diag->primary_span.column));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_add_len(total, sizeof(",\"span\":") - 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_add_len(total, sl_diag_decimal_len(underline));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_add_len(total, sizeof(",\"text\":") - 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_add_json_escaped_len(total, line);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_diag_add_len(total, sizeof(",\"marker\":") - 1U + 2U);
}

static SlStatus sl_diag_json_source_frame_marker_len(size_t* total, const SlDiag* diag,
                                                     size_t underline)
{
    SlStatus status = sl_diag_add_len(total, diag->primary_span.column - 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_diag_add_len(total, underline);
}

static SlStatus sl_diag_json_source_frame_hint_len(size_t* total, SlStr hint)
{
    SlStatus status;

    if (sl_str_is_empty(hint)) {
        return sl_status_ok();
    }
    status = sl_diag_add_len(total, sizeof(",\"hint\":") - 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_diag_add_json_escaped_len(total, hint);
}

static SlStatus sl_diag_json_source_frame_len(size_t* total, const SlDiag* diag, SlStr line)
{
    const size_t underline = diag->primary_span.length == 0U ? 1U : diag->primary_span.length;
    SlStr hint = sl_diag_first_hint(diag);
    SlStatus status = sl_diag_add_len(total, sizeof(",\"sourceFrame\":{") - 1U);

    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_json_source_frame_fields_len(total, diag, line, underline);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_json_source_frame_marker_len(total, diag, underline);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_json_source_frame_hint_len(total, hint);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_diag_add_len(total, 1U);
}

static SlStatus sl_diag_builder_append_json_marker(SlStringBuilder* builder, size_t column,
                                                   size_t underline)
{
    SlStatus status = sl_string_builder_append_char(builder, '"');
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_repeat(builder, ' ', column - 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_repeat(builder, '^', underline);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_string_builder_append_char(builder, '"');
}

static SlStatus sl_diag_builder_append_json_source_frame_fields(SlStringBuilder* builder,
                                                                const SlDiag* diag, SlStr line,
                                                                size_t underline)
{
    SlStatus status =
        sl_diag_builder_append_literal(builder, "\"file\":", sizeof("\"file\":") - 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_json_escaped(builder, diag->primary_span.path);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_literal(builder, ",\"line\":", sizeof(",\"line\":") - 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_string_builder_append_size(builder, diag->primary_span.line);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_literal(builder, ",\"column\":", sizeof(",\"column\":") - 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_string_builder_append_size(builder, diag->primary_span.column);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_literal(builder, ",\"span\":", sizeof(",\"span\":") - 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_string_builder_append_size(builder, underline);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_literal(builder, ",\"text\":", sizeof(",\"text\":") - 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_diag_builder_append_json_escaped(builder, line);
}

static SlStatus sl_diag_builder_append_json_source_frame(SlStringBuilder* builder,
                                                         const SlDiag* diag, SlStr line)
{
    const size_t underline = diag->primary_span.length == 0U ? 1U : diag->primary_span.length;
    SlStr hint = sl_diag_first_hint(diag);
    SlStatus status = sl_diag_builder_append_literal(builder, ",\"sourceFrame\":{",
                                                     sizeof(",\"sourceFrame\":{") - 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_json_source_frame_fields(builder, diag, line, underline);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_literal(builder, ",\"marker\":", sizeof(",\"marker\":") - 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_json_marker(builder, diag->primary_span.column, underline);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (!sl_str_is_empty(hint)) {
        status = sl_diag_builder_append_literal(builder, ",\"hint\":", sizeof(",\"hint\":") - 1U);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_diag_builder_append_json_escaped(builder, hint);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    return sl_string_builder_append_char(builder, '}');
}

SlStatus sl_diag_render_json(SlArena* arena, const SlDiag* diag, SlStr* out)
{
    SlStringBuilder builder;
    SlStatus status;
    size_t length = 0U;

    if (arena == NULL || diag == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_diag_json_render_len(&length, diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_init_for_render(arena, length, &builder);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_json_base(&builder, diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_json_primary(&builder, diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_json_related(&builder, diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_json_hints(&builder, diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_literal(&builder, "}\n", 2U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    *out = sl_string_builder_view(&builder);
    return sl_status_ok();
}

SlStatus sl_diag_render_json_with_source(SlArena* arena, const SlDiag* diag,
                                         const SlDiagSource* source, SlStr* out)
{
    SlStringBuilder builder;
    SlStatus status;
    SlStr line = sl_str_empty();
    size_t length = 0U;

    if (arena == NULL || diag == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_diag_validate_render_input(diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (!sl_diag_source_matches(diag, source) ||
        !sl_diag_source_line(source->text, diag->primary_span.line, &line))
    {
        return sl_diag_render_json(arena, diag, out);
    }

    status = sl_diag_json_render_len(&length, diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    length -= 2U;
    status = sl_diag_json_source_frame_len(&length, diag, line);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_add_len(&length, 2U);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_diag_builder_init_for_render(arena, length, &builder);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_json_base(&builder, diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_json_primary(&builder, diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_json_source_frame(&builder, diag, line);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_json_related(&builder, diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_json_hints(&builder, diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_diag_builder_append_literal(&builder, "}\n", 2U);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    *out = sl_string_builder_view(&builder);
    return sl_status_ok();
}

static bool sl_diag_secret_word_at(SlStr text, size_t index, const char* word)
{
    const char* start = text.ptr;

    if (word == NULL || index > text.length || (text.length > 0U && text.ptr == NULL)) {
        return false;
    }
    if (index < text.length) {
        start = text.ptr + index;
    }

    return sl_str_starts_with_ci_ascii(sl_str_from_parts(start, text.length - index),
                                       sl_str_from_cstr(word));
}

static bool sl_diag_secret_identifier_char(char value)
{
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
           (value >= '0' && value <= '9') || value == '_';
}

static bool sl_diag_secret_key_boundary_before(SlStr text, size_t index)
{
    if (index == 0U) {
        return true;
    }
    return !sl_diag_secret_identifier_char(text.ptr[index - 1U]);
}

static bool sl_diag_secret_key_boundary_after(SlStr text, size_t index)
{
    if (index >= text.length) {
        return true;
    }
    return !sl_diag_secret_identifier_char(text.ptr[index]);
}

static bool sl_diag_secret_key_at(SlStr text, size_t index, size_t* out_separator,
                                  bool* out_is_connection_string)
{
    typedef struct SlDiagSecretKey
    {
        const char* word;
        size_t length;
    } SlDiagSecretKey;
    static const SlDiagSecretKey keys[] = {
        {"password", sizeof("password") - 1U},
        {"pwd", sizeof("pwd") - 1U},
        {"token", sizeof("token") - 1U},
        {"secret", sizeof("secret") - 1U},
        {"passphrase", sizeof("passphrase") - 1U},
        {"private_key", sizeof("private_key") - 1U},
        {"privatekey", sizeof("privatekey") - 1U},
        {"private_key_path", sizeof("private_key_path") - 1U},
        {"privatekeypath", sizeof("privatekeypath") - 1U},
        {"secret_key", sizeof("secret_key") - 1U},
        {"secretkey", sizeof("secretkey") - 1U},
        {"client_secret", sizeof("client_secret") - 1U},
        {"clientsecret", sizeof("clientsecret") - 1U},
        {"client_certificate_path", sizeof("client_certificate_path") - 1U},
        {"clientcertificatepath", sizeof("clientcertificatepath") - 1U},
        {"client_private_key_path", sizeof("client_private_key_path") - 1U},
        {"clientprivatekeypath", sizeof("clientprivatekeypath") - 1U},
        {"client_ca_path", sizeof("client_ca_path") - 1U},
        {"clientcapath", sizeof("clientcapath") - 1U},
        {"certificate_path", sizeof("certificate_path") - 1U},
        {"certificatepath", sizeof("certificatepath") - 1U},
        {"cert_path", sizeof("cert_path") - 1U},
        {"certpath", sizeof("certpath") - 1U},
        {"ca_bundle_path", sizeof("ca_bundle_path") - 1U},
        {"cabundlepath", sizeof("cabundlepath") - 1U},
        {"ca_path", sizeof("ca_path") - 1U},
        {"capath", sizeof("capath") - 1U},
        {"trust_store_path", sizeof("trust_store_path") - 1U},
        {"truststorepath", sizeof("truststorepath") - 1U},
        {"api_key", sizeof("api_key") - 1U},
        {"apikey", sizeof("apikey") - 1U},
        {"key", sizeof("key") - 1U},
        {"key_path", sizeof("key_path") - 1U},
        {"keypath", sizeof("keypath") - 1U},
        {"connectionstring", sizeof("connectionstring") - 1U},
        {"connection_string", sizeof("connection_string") - 1U},
    };
    size_t key_index = 0U;

    for (key_index = 0U; key_index < sizeof(keys) / sizeof(keys[0]); key_index += 1U) {
        size_t end = index + keys[key_index].length;
        if (!sl_diag_secret_key_boundary_before(text, index) ||
            !sl_diag_secret_word_at(text, index, keys[key_index].word) ||
            !sl_diag_secret_key_boundary_after(text, index + keys[key_index].length))
        {
            continue;
        }
        if (end < text.length && (text.ptr[end] == '"' || text.ptr[end] == '\'')) {
            end += 1U;
        }
        while (end < text.length && (text.ptr[end] == ' ' || text.ptr[end] == '\t')) {
            end += 1U;
        }
        if (end < text.length && (text.ptr[end] == '=' || text.ptr[end] == ':')) {
            if (out_separator != NULL) {
                *out_separator = end;
            }
            if (out_is_connection_string != NULL) {
                *out_is_connection_string =
                    sl_diag_secret_word_at(text, index, "connectionstring") ||
                    sl_diag_secret_word_at(text, index, "connection_string");
            }
            return true;
        }
    }
    return false;
}

static SlStatus sl_diag_builder_append_redacted_value(SlStringBuilder* builder, SlStr input,
                                                      size_t* index, bool is_connection_string)
{
    size_t cursor = *index;
    SlStatus status;

    while (cursor < input.length && (input.ptr[cursor] == ' ' || input.ptr[cursor] == '\t' ||
                                     input.ptr[cursor] == '=' || input.ptr[cursor] == ':'))
    {
        status = sl_string_builder_append_char(builder, input.ptr[cursor]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        cursor += 1U;
    }

    status = sl_string_builder_append_str(builder, sl_diag_redacted());
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (cursor < input.length &&
        (input.ptr[cursor] == '\'' || input.ptr[cursor] == '"' || input.ptr[cursor] == '{'))
    {
        char quote = input.ptr[cursor];
        cursor += 1U;
        if (quote == '{') {
            quote = '}';
        }
        while (cursor < input.length) {
            if (input.ptr[cursor] == quote) {
                cursor += 1U;
                break;
            }
            cursor += 1U;
        }
    }
    else {
        if (is_connection_string) {
            while (cursor < input.length && input.ptr[cursor] != '\n' && input.ptr[cursor] != '\r')
            {
                if (input.ptr[cursor] == ';' &&
                    (cursor + 1U == input.length || input.ptr[cursor + 1U] == ' ' ||
                     input.ptr[cursor + 1U] == '\t' || input.ptr[cursor + 1U] == '\n' ||
                     input.ptr[cursor + 1U] == '\r'))
                {
                    break;
                }
                cursor += 1U;
            }
        }
        else {
            while (cursor < input.length && input.ptr[cursor] != ';' && input.ptr[cursor] != '&' &&
                   input.ptr[cursor] != ' ' && input.ptr[cursor] != '\t' &&
                   input.ptr[cursor] != '\n' && input.ptr[cursor] != '\r')
            {
                cursor += 1U;
            }
        }
    }

    *index = cursor;
    return sl_status_ok();
}

static bool sl_diag_uri_userinfo_at(SlStr input, size_t index, size_t* password_start,
                                    size_t* password_end)
{
    size_t cursor = index + 3U;
    size_t colon = input.length;
    size_t at = input.length;

    if (password_start == NULL || password_end == NULL || index + 3U >= input.length ||
        input.ptr[index] != ':' || input.ptr[index + 1U] != '/' || input.ptr[index + 2U] != '/')
    {
        return false;
    }

    while (cursor < input.length && input.ptr[cursor] != '/' && input.ptr[cursor] != ' ' &&
           input.ptr[cursor] != '\t' && input.ptr[cursor] != '\n' && input.ptr[cursor] != '\r')
    {
        if (input.ptr[cursor] == ':' && colon == input.length) {
            colon = cursor;
        }
        if (input.ptr[cursor] == '@') {
            at = cursor;
            break;
        }
        cursor += 1U;
    }

    if (colon < at && at < input.length) {
        *password_start = colon + 1U;
        *password_end = at;
        return true;
    }

    return false;
}

static SlStatus sl_diag_builder_append_redacted_text(SlStringBuilder* builder, SlStr input)
{
    size_t index = 0U;
    SlStatus status;

    while (index < input.length) {
        size_t separator = 0U;
        size_t uri_password_start = 0U;
        size_t uri_password_end = 0U;
        bool is_connection_string = false;

        if (sl_diag_secret_key_at(input, index, &separator, &is_connection_string)) {
            status = sl_string_builder_append_str(
                builder, sl_str_from_parts(input.ptr + index, separator - index));
            if (!sl_status_is_ok(status)) {
                return status;
            }
            index = separator;
            status =
                sl_diag_builder_append_redacted_value(builder, input, &index, is_connection_string);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            continue;
        }

        if (sl_diag_uri_userinfo_at(input, index, &uri_password_start, &uri_password_end)) {
            status = sl_string_builder_append_str(
                builder, sl_str_from_parts(input.ptr + index, uri_password_start - index));
            if (!sl_status_is_ok(status)) {
                return status;
            }
            status = sl_string_builder_append_str(builder, sl_diag_redacted());
            if (!sl_status_is_ok(status)) {
                return status;
            }
            index = uri_password_end;
            continue;
        }

        status = sl_string_builder_append_char(builder, input.ptr[index]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        index += 1U;
    }

    return sl_status_ok();
}

SlStatus sl_diag_redact_secrets(SlArena* arena, SlStr input, SlStr* out)
{
    SlStringBuilder builder;
    size_t max_capacity = 0U;
    SlStatus status;

    if (arena == NULL || out == NULL || !sl_diag_str_is_valid(input)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (input.length == 0U) {
        *out = sl_str_empty();
        return sl_status_ok();
    }
    status = sl_checked_mul_size(input.length, sizeof("<redacted>"), &max_capacity);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_string_builder_init_arena(&builder, arena, input.length, max_capacity);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_diag_builder_append_redacted_text(&builder, input);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    *out = sl_string_builder_view(&builder);
    return sl_status_ok();
}

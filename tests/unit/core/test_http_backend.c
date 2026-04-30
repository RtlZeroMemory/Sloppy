#include "sloppy/http_backend.h"

#include <stdbool.h>
#include <stddef.h>

#define TEST_ARENA_SIZE 65536U

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static SlBytes bytes_from_cstr(const char* text)
{
    SlStr str = sl_str_from_cstr(text);
    return sl_bytes_from_parts((const unsigned char*)str.ptr, str.length);
}

static int expect_bytes_equal(SlBytes actual, const char* expected)
{
    return expect_true(sl_bytes_equal(actual, bytes_from_cstr(expected)));
}

static int init_started_backend(SlHttpBackend* backend, const SlHttpBackendOptions* options)
{
    if (expect_status(sl_http_backend_init(backend, options, NULL), SL_STATUS_OK) != 0) {
        return 1;
    }
    return expect_status(sl_http_backend_start(backend, NULL, NULL), SL_STATUS_OK);
}

static int test_backend_init_start_stop_dispose(void)
{
    SlHttpBackend backend = {0};

    if (expect_status(sl_http_backend_init(&backend, NULL, NULL), SL_STATUS_OK) != 0 ||
        sl_http_backend_state(&backend) != SL_HTTP_BACKEND_STATE_INITIALIZED ||
        backend.options.max_connections != SL_HTTP_BACKEND_DEFAULT_MAX_CONNECTIONS ||
        backend.options.parse.max_header_value_length != SL_HTTP_DEFAULT_MAX_HEADER_VALUE_LENGTH)
    {
        return 1;
    }
    if (expect_status(sl_http_backend_start(&backend, NULL, NULL), SL_STATUS_OK) != 0 ||
        sl_http_backend_state(&backend) != SL_HTTP_BACKEND_STATE_STARTED)
    {
        return 2;
    }
    if (expect_status(sl_http_backend_stop(&backend, NULL), SL_STATUS_OK) != 0 ||
        sl_http_backend_state(&backend) != SL_HTTP_BACKEND_STATE_STOPPED)
    {
        return 3;
    }
    if (expect_status(sl_http_backend_dispose(&backend, NULL), SL_STATUS_OK) != 0 ||
        sl_http_backend_state(&backend) != SL_HTTP_BACKEND_STATE_DISPOSED)
    {
        return 4;
    }

    return 0;
}

static int test_connection_lifecycle_cleanup(void)
{
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};

    if (init_started_backend(&backend, NULL) != 0) {
        return 10;
    }
    if (expect_status(sl_http_backend_accept_connection(&backend, &connection, NULL),
                      SL_STATUS_OK) != 0 ||
        connection.id == 0U ||
        sl_http_connection_state(&connection) != SL_HTTP_CONNECTION_STATE_ACCEPTED ||
        backend.active_connections != 1U)
    {
        return 11;
    }
    if (expect_status(sl_http_connection_close(&connection, NULL), SL_STATUS_OK) != 0 ||
        sl_http_connection_state(&connection) != SL_HTTP_CONNECTION_STATE_CLOSED ||
        backend.active_connections != 0U)
    {
        return 12;
    }
    if (expect_status(sl_http_connection_close(&connection, NULL), SL_STATUS_OK) != 0 ||
        backend.active_connections != 0U)
    {
        return 13;
    }

    return 0;
}

static int test_request_parse_success_and_cleanup(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};
    SlHttpRequestLifecycle request = {0};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        init_started_backend(&backend, NULL) != 0 ||
        expect_status(sl_http_backend_accept_connection(&backend, &connection, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_begin(&connection, &arena, &request, NULL), SL_STATUS_OK) !=
            0)
    {
        return 20;
    }
    if (expect_status(
            sl_http_request_parse_head(
                &request, bytes_from_cstr("GET /ok HTTP/1.1\r\nHost: example\r\n\r\n"), NULL),
            SL_STATUS_OK) != 0 ||
        request.head.method != SL_HTTP_METHOD_GET ||
        sl_http_request_state(&request) != SL_HTTP_REQUEST_STATE_READING)
    {
        return 21;
    }
    if (expect_status(sl_http_request_begin_dispatch(&request, NULL), SL_STATUS_OK) != 0 ||
        sl_http_connection_state(&connection) != SL_HTTP_CONNECTION_STATE_DISPATCHING)
    {
        return 22;
    }
    if (expect_status(sl_http_request_begin_write(&request, NULL), SL_STATUS_OK) != 0 ||
        sl_http_connection_state(&connection) != SL_HTTP_CONNECTION_STATE_WRITING_RESPONSE)
    {
        return 23;
    }
    if (expect_status(sl_http_request_complete(&request, NULL), SL_STATUS_OK) != 0 ||
        backend.active_requests != 0U ||
        sl_http_request_state(&request) != SL_HTTP_REQUEST_STATE_COMPLETED)
    {
        return 24;
    }
    if (expect_status(sl_http_request_close(&request, NULL), SL_STATUS_OK) != 0 ||
        request.head.raw_target.ptr != NULL)
    {
        return 25;
    }

    return 0;
}

static int test_malformed_request_failure_releases_admission(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};
    SlHttpRequestLifecycle request = {0};
    SlDiag diag = {0};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        init_started_backend(&backend, NULL) != 0 ||
        expect_status(sl_http_backend_accept_connection(&backend, &connection, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_begin(&connection, &arena, &request, NULL), SL_STATUS_OK) !=
            0)
    {
        return 30;
    }
    if (expect_status(
            sl_http_request_parse_head(&request, bytes_from_cstr("GET / HTTP/1.1"), &diag),
            SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_INVALID_HTTP_REQUEST ||
        sl_http_request_state(&request) != SL_HTTP_REQUEST_STATE_FAILED ||
        sl_http_connection_state(&connection) != SL_HTTP_CONNECTION_STATE_ERROR)
    {
        return 31;
    }
    if (expect_status(sl_http_request_fail(&request, NULL), SL_STATUS_OK) != 0 ||
        backend.active_requests != 0U)
    {
        return 32;
    }

    return 0;
}

static int test_parser_limits_flow_through_backend_options(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlHttpBackendOptions options = {0};
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};
    SlHttpRequestLifecycle request = {0};
    SlDiag diag = {0};

    options.parse.max_headers = SL_HTTP_DEFAULT_MAX_HEADERS;
    options.parse.max_target_length = 4U;
    options.parse.max_header_name_length = 3U;
    options.parse.max_header_value_length = 4U;
    options.parse.max_total_header_bytes = 8U;

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        init_started_backend(&backend, &options) != 0 ||
        expect_status(sl_http_backend_accept_connection(&backend, &connection, NULL),
                      SL_STATUS_OK) != 0)
    {
        return 40;
    }

    if (expect_status(sl_http_request_begin(&connection, &arena, &request, NULL), SL_STATUS_OK) !=
        0)
    {
        return 41;
    }
    if (expect_status(sl_http_request_parse_head(
                          &request, bytes_from_cstr("GET /toolong HTTP/1.1\r\n\r\n"), &diag),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        diag.code != SL_DIAG_HTTP_TARGET_LIMIT)
    {
        return 42;
    }
    if (expect_status(sl_http_request_fail(&request, NULL), SL_STATUS_OK) != 0) {
        return 43;
    }

    connection.state = SL_HTTP_CONNECTION_STATE_OPEN;
    sl_arena_reset(&arena);
    request = (SlHttpRequestLifecycle){0};
    diag = (SlDiag){0};
    if (expect_status(sl_http_request_begin(&connection, &arena, &request, NULL), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_http_request_parse_head(
                          &request, bytes_from_cstr("GET / HTTP/1.1\r\nLong: v\r\n\r\n"), &diag),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        diag.code != SL_DIAG_HTTP_HEADER_NAME_LIMIT)
    {
        return 44;
    }
    if (expect_status(sl_http_request_fail(&request, NULL), SL_STATUS_OK) != 0) {
        return 45;
    }

    connection.state = SL_HTTP_CONNECTION_STATE_OPEN;
    sl_arena_reset(&arena);
    request = (SlHttpRequestLifecycle){0};
    diag = (SlDiag){0};
    if (expect_status(sl_http_request_begin(&connection, &arena, &request, NULL), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_http_request_parse_head(
                          &request, bytes_from_cstr("GET / HTTP/1.1\r\nA: value\r\n\r\n"), &diag),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        diag.code != SL_DIAG_HTTP_HEADER_VALUE_LIMIT)
    {
        return 46;
    }
    if (expect_status(sl_http_request_fail(&request, NULL), SL_STATUS_OK) != 0) {
        return 47;
    }

    connection.state = SL_HTTP_CONNECTION_STATE_OPEN;
    sl_arena_reset(&arena);
    request = (SlHttpRequestLifecycle){0};
    diag = (SlDiag){0};
    if (expect_status(sl_http_request_begin(&connection, &arena, &request, NULL), SL_STATUS_OK) !=
            0 ||
        expect_status(
            sl_http_request_parse_head(
                &request, bytes_from_cstr("GET / HTTP/1.1\r\nA: 1111\r\nB: 1111\r\n\r\n"), &diag),
            SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        diag.code != SL_DIAG_HTTP_HEADER_BYTES_LIMIT)
    {
        return 48;
    }
    if (expect_status(sl_http_request_fail(&request, NULL), SL_STATUS_OK) != 0) {
        return 49;
    }

    return 0;
}

static int test_timeout_hook_cancels_request_distinctly(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};
    SlHttpRequestLifecycle request = {0};
    SlDiag diag = {0};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        init_started_backend(&backend, NULL) != 0 ||
        expect_status(sl_http_backend_accept_connection(&backend, &connection, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_begin(&connection, &arena, &request, NULL), SL_STATUS_OK) !=
            0)
    {
        return 50;
    }

    if (expect_status(sl_http_request_timeout(&request, sl_str_from_cstr("header deadline"), &diag),
                      SL_STATUS_DEADLINE_EXCEEDED) != 0 ||
        diag.code != SL_DIAG_HTTP_REQUEST_TIMEOUT ||
        !sl_cancellation_token_is_cancelled(&request.cancellation) ||
        sl_cancellation_token_reason(&request.cancellation) !=
            SL_CANCELLATION_REASON_DEADLINE_EXCEEDED ||
        backend.active_requests != 0U)
    {
        return 51;
    }

    return 0;
}

static int test_keep_alive_disabled_rejects_second_request(void)
{
    unsigned char first_storage[TEST_ARENA_SIZE];
    unsigned char second_storage[TEST_ARENA_SIZE];
    SlArena first_arena = {0};
    SlArena second_arena = {0};
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};
    SlHttpRequestLifecycle first_request = {0};
    SlHttpRequestLifecycle second_request = {0};
    SlDiag diag = {0};

    if (expect_status(sl_arena_init(&first_arena, first_storage, sizeof(first_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&second_arena, second_storage, sizeof(second_storage)),
                      SL_STATUS_OK) != 0 ||
        init_started_backend(&backend, NULL) != 0 ||
        expect_status(sl_http_backend_accept_connection(&backend, &connection, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_begin(&connection, &first_arena, &first_request, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_parse_head(&first_request,
                                                 bytes_from_cstr("GET / HTTP/1.1\r\n\r\n"), NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_begin_dispatch(&first_request, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_begin_write(&first_request, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_complete(&first_request, NULL), SL_STATUS_OK) != 0)
    {
        return 55;
    }

    if (sl_http_connection_state(&connection) != SL_HTTP_CONNECTION_STATE_CLOSING ||
        expect_status(sl_http_request_begin(&connection, &second_arena, &second_request, &diag),
                      SL_STATUS_UNSUPPORTED) != 0 ||
        diag.code != SL_DIAG_HTTP_KEEP_ALIVE_UNSUPPORTED || second_request.admitted)
    {
        return 56;
    }

    return 0;
}

static int test_overload_admission_rejection(void)
{
    unsigned char first_storage[TEST_ARENA_SIZE];
    unsigned char second_storage[TEST_ARENA_SIZE];
    SlArena first_arena = {0};
    SlArena second_arena = {0};
    SlHttpBackendOptions options = {0};
    SlHttpBackend backend = {0};
    SlHttpConnection first_connection = {0};
    SlHttpConnection second_connection = {0};
    SlHttpRequestLifecycle first_request = {0};
    SlHttpRequestLifecycle second_request = {0};
    SlDiag diag = {0};

    options.max_connections = 2U;
    options.max_active_requests = 1U;

    if (expect_status(sl_arena_init(&first_arena, first_storage, sizeof(first_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&second_arena, second_storage, sizeof(second_storage)),
                      SL_STATUS_OK) != 0 ||
        init_started_backend(&backend, &options) != 0 ||
        expect_status(sl_http_backend_accept_connection(&backend, &first_connection, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_backend_accept_connection(&backend, &second_connection, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_begin(&first_connection, &first_arena, &first_request, NULL),
                      SL_STATUS_OK) != 0)
    {
        return 60;
    }

    if (expect_status(
            sl_http_request_begin(&second_connection, &second_arena, &second_request, &diag),
            SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        diag.code != SL_DIAG_HTTP_OVERLOAD || second_request.admitted ||
        backend.active_requests != 1U)
    {
        return 61;
    }
    if (expect_status(sl_http_request_close(&first_request, NULL), SL_STATUS_OK) != 0 ||
        backend.active_requests != 0U)
    {
        return 62;
    }

    return 0;
}

static int test_stop_finalizes_after_failed_connection(void)
{
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};

    if (init_started_backend(&backend, NULL) != 0 ||
        expect_status(sl_http_backend_accept_connection(&backend, &connection, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_backend_stop(&backend, NULL), SL_STATUS_OK) != 0 ||
        sl_http_backend_state(&backend) != SL_HTTP_BACKEND_STATE_STOPPING)
    {
        return 63;
    }
    if (expect_status(sl_http_connection_fail(&connection, NULL), SL_STATUS_OK) != 0 ||
        sl_http_backend_state(&backend) != SL_HTTP_BACKEND_STATE_STOPPED ||
        expect_status(sl_http_backend_dispose(&backend, NULL), SL_STATUS_OK) != 0)
    {
        return 64;
    }

    return 0;
}

static int test_stop_finalizes_after_request_release(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};
    SlHttpRequestLifecycle request = {0};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        init_started_backend(&backend, NULL) != 0 ||
        expect_status(sl_http_backend_accept_connection(&backend, &connection, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_begin(&connection, &arena, &request, NULL), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_http_backend_stop(&backend, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_connection_close(&connection, NULL), SL_STATUS_OK) != 0 ||
        sl_http_backend_state(&backend) != SL_HTTP_BACKEND_STATE_STOPPING)
    {
        return 65;
    }
    if (expect_status(sl_http_request_close(&request, NULL), SL_STATUS_OK) != 0 ||
        sl_http_backend_state(&backend) != SL_HTTP_BACKEND_STATE_STOPPED ||
        expect_status(sl_http_backend_dispose(&backend, NULL), SL_STATUS_OK) != 0)
    {
        return 66;
    }

    return 0;
}

static int test_request_lifecycle_rejects_skipped_and_repeated_phases(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};
    SlHttpRequestLifecycle request = {0};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        init_started_backend(&backend, NULL) != 0 ||
        expect_status(sl_http_backend_accept_connection(&backend, &connection, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_begin(&connection, &arena, &request, NULL), SL_STATUS_OK) !=
            0)
    {
        return 67;
    }
    if (expect_status(sl_http_request_begin_dispatch(&request, NULL), SL_STATUS_INVALID_STATE) !=
            0 ||
        expect_status(
            sl_http_request_parse_head(&request, bytes_from_cstr("GET / HTTP/1.1\r\n\r\n"), NULL),
            SL_STATUS_OK) != 0 ||
        expect_status(
            sl_http_request_parse_head(&request, bytes_from_cstr("GET / HTTP/1.1\r\n\r\n"), NULL),
            SL_STATUS_INVALID_STATE) != 0 ||
        expect_status(sl_http_request_begin_dispatch(&request, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_complete(&request, NULL), SL_STATUS_INVALID_STATE) != 0 ||
        expect_status(sl_http_request_begin_write(&request, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_complete(&request, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_timeout(&request, sl_str_from_cstr("late timeout"), NULL),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        return 68;
    }

    return 0;
}

static int test_body_reader_success_empty_and_dispatch_transition(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};
    SlHttpRequestLifecycle request = {0};
    SlHttpBodyReader reader = {0};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        init_started_backend(&backend, NULL) != 0 ||
        expect_status(sl_http_backend_accept_connection(&backend, &connection, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_begin(&connection, &arena, &request, NULL), SL_STATUS_OK) !=
            0 ||
        expect_status(
            sl_http_request_parse_head(&request, bytes_from_cstr("POST / HTTP/1.1\r\n\r\n"), NULL),
            SL_STATUS_OK) != 0)
    {
        return 69;
    }

    if (expect_status(
            sl_http_request_body_reader_begin(&request, sl_str_empty(), 0U, &reader, NULL),
            SL_STATUS_OK) != 0 ||
        reader.body_kind != SL_HTTP_REQUEST_BODY_NONE ||
        expect_status(sl_http_request_body_reader_finish(&reader, NULL), SL_STATUS_OK) != 0 ||
        request.head.body.length != 0U ||
        expect_status(sl_http_request_begin_dispatch(&request, NULL), SL_STATUS_OK) != 0)
    {
        return 70;
    }

    return 0;
}

static int test_body_reader_success_owns_bounded_json_body(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};
    SlHttpRequestLifecycle request = {0};
    SlHttpBodyReader reader = {0};
    void* scratch = NULL;
    size_t used_after_scratch = 0U;

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        init_started_backend(&backend, NULL) != 0 ||
        expect_status(sl_http_backend_accept_connection(&backend, &connection, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_begin(&connection, &arena, &request, NULL), SL_STATUS_OK) !=
            0 ||
        expect_status(
            sl_http_request_parse_head(&request, bytes_from_cstr("POST / HTTP/1.1\r\n\r\n"), NULL),
            SL_STATUS_OK) != 0)
    {
        return 71;
    }

    if (expect_status(
            sl_http_request_body_reader_begin(
                &request, sl_str_from_cstr("application/json; charset=utf-8"), 11U, &reader, NULL),
            SL_STATUS_OK) != 0 ||
        reader.body_kind != SL_HTTP_REQUEST_BODY_JSON ||
        expect_status(sl_http_request_body_reader_append(&reader, bytes_from_cstr("{\"ok\""), NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_body_reader_append(&reader, bytes_from_cstr(":true}"), NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_body_reader_finish(&reader, NULL), SL_STATUS_OK) != 0 ||
        expect_bytes_equal(request.head.body, "{\"ok\":true}") != 0)
    {
        return 72;
    }
    if (expect_status(sl_http_request_body_reader_close(&reader, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_alloc(&arena, 8U, 1U, &scratch), SL_STATUS_OK) != 0)
    {
        return 86;
    }
    used_after_scratch = sl_arena_used(&arena);
    if (expect_status(sl_http_request_body_reader_close(&reader, NULL), SL_STATUS_OK) != 0 ||
        sl_arena_used(&arena) != used_after_scratch || scratch == NULL ||
        expect_bytes_equal(request.head.body, "{\"ok\":true}") != 0)
    {
        return 87;
    }

    return 0;
}

static int test_body_reader_rejects_limits_and_unsupported_media(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlHttpBackendOptions options = {0};
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};
    SlHttpRequestLifecycle request = {0};
    SlHttpBodyReader reader = {0};
    SlDiag diag = {0};

    options.parse.max_headers = SL_HTTP_DEFAULT_MAX_HEADERS;
    options.parse.max_body_length = 4U;
    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        init_started_backend(&backend, &options) != 0 ||
        expect_status(sl_http_backend_accept_connection(&backend, &connection, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_begin(&connection, &arena, &request, NULL), SL_STATUS_OK) !=
            0 ||
        expect_status(
            sl_http_request_parse_head(&request, bytes_from_cstr("POST / HTTP/1.1\r\n\r\n"), NULL),
            SL_STATUS_OK) != 0)
    {
        return 73;
    }

    if (expect_status(sl_http_request_body_reader_begin(&request, sl_str_from_cstr("text/plain"),
                                                        5U, &reader, &diag),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        diag.code != SL_DIAG_HTTP_BODY_LIMIT || backend.active_requests != 0U ||
        request.head.body.ptr != NULL)
    {
        return 74;
    }

    connection.state = SL_HTTP_CONNECTION_STATE_OPEN;
    sl_arena_reset(&arena);
    request = (SlHttpRequestLifecycle){0};
    diag = (SlDiag){0};
    if (expect_status(sl_http_request_begin(&connection, &arena, &request, NULL), SL_STATUS_OK) !=
            0 ||
        expect_status(
            sl_http_request_parse_head(&request, bytes_from_cstr("POST / HTTP/1.1\r\n\r\n"), NULL),
            SL_STATUS_OK) != 0 ||
        expect_status(
            sl_http_request_body_reader_begin(
                &request, sl_str_from_cstr("application/octet-stream"), 3U, &reader, &diag),
            SL_STATUS_UNSUPPORTED) != 0 ||
        diag.code != SL_DIAG_HTTP_UNSUPPORTED_MEDIA_TYPE || backend.active_requests != 0U)
    {
        return 75;
    }

    connection.state = SL_HTTP_CONNECTION_STATE_OPEN;
    sl_arena_reset(&arena);
    request = (SlHttpRequestLifecycle){0};
    diag = (SlDiag){0};
    if (expect_status(sl_http_request_begin(&connection, &arena, &request, NULL), SL_STATUS_OK) !=
            0 ||
        expect_status(
            sl_http_request_parse_head(&request, bytes_from_cstr("POST / HTTP/1.1\r\n\r\n"), NULL),
            SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_body_reader_begin(&request, sl_str_from_cstr("text/plain"),
                                                        3U, &reader, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_body_reader_append(&reader, bytes_from_cstr("ab"), NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_body_reader_finish(&reader, &diag),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_INVALID_HTTP_REQUEST || backend.active_requests != 0U)
    {
        return 85;
    }

    connection.state = SL_HTTP_CONNECTION_STATE_OPEN;
    sl_arena_reset(&arena);
    request = (SlHttpRequestLifecycle){0};
    diag = (SlDiag){0};
    if (expect_status(sl_http_request_begin(&connection, &arena, &request, NULL), SL_STATUS_OK) !=
            0 ||
        expect_status(
            sl_http_request_parse_head(&request, bytes_from_cstr("POST / HTTP/1.1\r\n\r\n"), NULL),
            SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_body_reader_begin(&request, sl_str_from_cstr("text/plain"),
                                                        3U, &reader, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_body_reader_append(&reader, bytes_from_cstr("abcd"), &diag),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_INVALID_HTTP_REQUEST || backend.active_requests != 0U)
    {
        return 88;
    }

    return 0;
}

static int test_body_reader_cancellation_timeout_and_shutdown(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};
    SlHttpRequestLifecycle request = {0};
    SlHttpBodyReader reader = {0};
    SlDiag diag = {0};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        init_started_backend(&backend, NULL) != 0 ||
        expect_status(sl_http_backend_accept_connection(&backend, &connection, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_begin(&connection, &arena, &request, NULL), SL_STATUS_OK) !=
            0 ||
        expect_status(
            sl_http_request_parse_head(&request, bytes_from_cstr("POST / HTTP/1.1\r\n\r\n"), NULL),
            SL_STATUS_OK) != 0)
    {
        return 76;
    }

    if (expect_status(sl_cancellation_token_cancel(&request.cancellation,
                                                   SL_CANCELLATION_REASON_CANCELLED,
                                                   sl_str_from_cstr("client closed")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_body_reader_begin(&request, sl_str_from_cstr("text/plain"),
                                                        3U, &reader, &diag),
                      SL_STATUS_CANCELLED) != 0 ||
        diag.code != SL_DIAG_ENGINE_CANCELLED || backend.active_requests != 0U ||
        sl_http_request_state(&request) != SL_HTTP_REQUEST_STATE_CANCELLED)
    {
        return 77;
    }

    connection.state = SL_HTTP_CONNECTION_STATE_OPEN;
    sl_arena_reset(&arena);
    request = (SlHttpRequestLifecycle){0};
    diag = (SlDiag){0};
    if (expect_status(sl_http_request_begin(&connection, &arena, &request, NULL), SL_STATUS_OK) !=
            0 ||
        expect_status(
            sl_http_request_parse_head(&request, bytes_from_cstr("POST / HTTP/1.1\r\n\r\n"), NULL),
            SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_body_reader_begin(&request, sl_str_from_cstr("text/plain"),
                                                        3U, &reader, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_body_reader_append(&reader, bytes_from_cstr("a"), NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_timeout(&request, sl_str_from_cstr("body deadline"), NULL),
                      SL_STATUS_DEADLINE_EXCEEDED) != 0 ||
        expect_status(sl_http_request_body_reader_append(&reader, bytes_from_cstr("b"), &diag),
                      SL_STATUS_DEADLINE_EXCEEDED) != 0 ||
        diag.code != SL_DIAG_HTTP_REQUEST_TIMEOUT || backend.active_requests != 0U)
    {
        return 78;
    }

    connection.state = SL_HTTP_CONNECTION_STATE_OPEN;
    sl_arena_reset(&arena);
    request = (SlHttpRequestLifecycle){0};
    diag = (SlDiag){0};
    if (expect_status(sl_http_request_begin(&connection, &arena, &request, NULL), SL_STATUS_OK) !=
            0 ||
        expect_status(
            sl_http_request_parse_head(&request, bytes_from_cstr("POST / HTTP/1.1\r\n\r\n"), NULL),
            SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_body_reader_begin(&request, sl_str_from_cstr("text/plain"),
                                                        3U, &reader, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_backend_stop(&backend, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_body_reader_append(&reader, bytes_from_cstr("x"), &diag),
                      SL_STATUS_CANCELLED) != 0 ||
        diag.code != SL_DIAG_HTTP_SHUTDOWN || backend.active_requests != 0U)
    {
        return 79;
    }

    return 0;
}

static int test_dispatch_rejects_backend_shutdown_after_body_read(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};
    SlHttpRequestLifecycle request = {0};
    SlDiag diag = {0};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        init_started_backend(&backend, NULL) != 0 ||
        expect_status(sl_http_backend_accept_connection(&backend, &connection, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_begin(&connection, &arena, &request, NULL), SL_STATUS_OK) !=
            0 ||
        expect_status(
            sl_http_request_parse_head(&request, bytes_from_cstr("GET / HTTP/1.1\r\n\r\n"), NULL),
            SL_STATUS_OK) != 0 ||
        expect_status(sl_http_backend_stop(&backend, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_begin_dispatch(&request, &diag), SL_STATUS_CANCELLED) != 0)
    {
        return 89;
    }
    if (diag.code != SL_DIAG_HTTP_SHUTDOWN || backend.active_requests != 0U ||
        sl_http_request_state(&request) != SL_HTTP_REQUEST_STATE_CANCELLED ||
        sl_http_connection_state(&connection) != SL_HTTP_CONNECTION_STATE_CLOSING)
    {
        return 90;
    }

    return 0;
}

static int test_shutdown_rejects_new_and_cancels_active_work(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    unsigned char second_storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlArena second_arena = {0};
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};
    SlHttpRequestLifecycle request = {0};
    SlHttpRequestLifecycle second_request = {0};
    SlDiag diag = {0};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&second_arena, second_storage, sizeof(second_storage)),
                      SL_STATUS_OK) != 0 ||
        init_started_backend(&backend, NULL) != 0 ||
        expect_status(sl_http_backend_accept_connection(&backend, &connection, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_begin(&connection, &arena, &request, NULL), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_http_backend_stop(&backend, NULL), SL_STATUS_OK) != 0)
    {
        return 80;
    }

    connection.state = SL_HTTP_CONNECTION_STATE_OPEN;
    if (expect_status(sl_http_request_begin(&connection, &second_arena, &second_request, &diag),
                      SL_STATUS_CANCELLED) != 0 ||
        diag.code != SL_DIAG_HTTP_SHUTDOWN || second_request.admitted)
    {
        return 81;
    }

    if (expect_status(sl_http_request_shutdown(&request, &diag), SL_STATUS_CANCELLED) != 0 ||
        diag.code != SL_DIAG_HTTP_SHUTDOWN || backend.active_requests != 0U ||
        sl_http_request_state(&request) != SL_HTTP_REQUEST_STATE_CANCELLED)
    {
        return 82;
    }

    return 0;
}

static int test_shutdown_during_response_write_and_diagnostic_name(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};
    SlHttpRequestLifecycle request = {0};
    SlDiag diag = {0};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        init_started_backend(&backend, NULL) != 0 ||
        expect_status(sl_http_backend_accept_connection(&backend, &connection, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_begin(&connection, &arena, &request, NULL), SL_STATUS_OK) !=
            0 ||
        expect_status(
            sl_http_request_parse_head(&request, bytes_from_cstr("GET / HTTP/1.1\r\n\r\n"), NULL),
            SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_begin_dispatch(&request, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_begin_write(&request, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_shutdown(&request, &diag), SL_STATUS_CANCELLED) != 0)
    {
        return 83;
    }

    if (diag.code != SL_DIAG_HTTP_SHUTDOWN || backend.active_requests != 0U ||
        !sl_str_equal(sl_diag_code_name(SL_DIAG_HTTP_SHUTDOWN),
                      sl_str_from_cstr("SLOPPY_E_HTTP_SHUTDOWN")))
    {
        return 84;
    }

    return 0;
}

static int test_request_cancel_clears_stale_diagnostic(void)
{
    SlDiag diag = {.code = SL_DIAG_HTTP_SHUTDOWN};

    if (expect_status(sl_http_request_cancel(NULL, SL_CANCELLATION_REASON_CANCELLED,
                                             sl_str_from_cstr("client closed"), &diag),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_NONE)
    {
        return 91;
    }

    return 0;
}

typedef int (*HttpBackendTestFn)(void);

typedef struct HttpBackendTestCase
{
    HttpBackendTestFn fn;
} HttpBackendTestCase;

int main(void)
{
    static const HttpBackendTestCase tests[] = {
        {test_backend_init_start_stop_dispose},
        {test_connection_lifecycle_cleanup},
        {test_request_parse_success_and_cleanup},
        {test_malformed_request_failure_releases_admission},
        {test_parser_limits_flow_through_backend_options},
        {test_timeout_hook_cancels_request_distinctly},
        {test_keep_alive_disabled_rejects_second_request},
        {test_overload_admission_rejection},
        {test_stop_finalizes_after_failed_connection},
        {test_stop_finalizes_after_request_release},
        {test_request_lifecycle_rejects_skipped_and_repeated_phases},
        {test_body_reader_success_empty_and_dispatch_transition},
        {test_body_reader_success_owns_bounded_json_body},
        {test_body_reader_rejects_limits_and_unsupported_media},
        {test_body_reader_cancellation_timeout_and_shutdown},
        {test_dispatch_rejects_backend_shutdown_after_body_read},
        {test_shutdown_rejects_new_and_cancels_active_work},
        {test_shutdown_during_response_write_and_diagnostic_name},
        {test_request_cancel_clears_stale_diagnostic},
    };
    size_t index = 0U;

    for (index = 0U; index < sizeof(tests) / sizeof(tests[0]); index += 1U) {
        int result = tests[index].fn();
        if (result != 0) {
            return result;
        }
    }

    return 0;
}

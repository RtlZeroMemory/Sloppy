#include "sloppy/http2_mapping.h"

#include <stdbool.h>
#include <stdint.h>

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

static SlHttp2HeaderField h2_header(const char* name, const char* value)
{
    return (SlHttp2HeaderField){
        .name = sl_str_from_cstr(name), .value = sl_str_from_cstr(value), .sensitive = false};
}

static int expect_h2_header(const SlHttp2HeaderList* headers, size_t index, const char* name,
                            const char* value)
{
    if (headers == NULL || index >= headers->count) {
        return 1;
    }
    return expect_true(sl_str_equal(headers->fields[index].name, sl_str_from_cstr(name)) &&
                       sl_str_equal(headers->fields[index].value, sl_str_from_cstr(value)));
}

static int test_request_mapping_materializes_lifecycle(void)
{
    unsigned char arena_storage[8192];
    SlArena arena = {0};
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};
    SlHttpRequestLifecycle request = {0};
    SlHttp2HeaderField fields[] = {h2_header(":method", "POST"),
                                   h2_header(":scheme", "https"),
                                   h2_header(":authority", "example.test"),
                                   h2_header(":path", "/submit?x=1"),
                                   h2_header("content-type", "text/plain"),
                                   h2_header("content-length", "5"),
                                   h2_header("x-test", "ok")};
    SlHttp2HeaderList headers = {.fields = fields, .count = sizeof(fields) / sizeof(fields[0])};
    SlStatus map_status = sl_status_ok();

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_http_backend_init(&backend, NULL, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_backend_start(&backend, NULL, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_backend_accept_connection(&backend, &connection, NULL),
                      SL_STATUS_OK) != 0)
    {
        return 1;
    }
    connection.scheme = sl_str_from_cstr("https");

    map_status = sl_http2_request_from_headers(&arena, &connection, &headers,
                                               bytes_from_cstr("hello"), &request, NULL);
    if (expect_status(map_status, SL_STATUS_OK) != 0) {
        return 20 + (int)sl_status_code(map_status);
    }

    if (request.head.method != SL_HTTP_METHOD_POST ||
        !sl_str_equal(request.head.raw_target, sl_str_from_cstr("/submit?x=1")) ||
        !sl_str_equal(request.head.path, sl_str_from_cstr("/submit")) ||
        request.head.version_major != 2U || request.head.version_minor != 0U ||
        request.head.header_count != 4U ||
        !sl_str_equal(request.head.headers[0].name, sl_str_from_cstr("host")) ||
        !sl_str_equal(request.head.headers[0].value, sl_str_from_cstr("example.test")) ||
        !sl_bytes_equal(request.head.body, bytes_from_cstr("hello")))
    {
        return 3;
    }

    return 0;
}

static int test_request_mapping_rejects_connection_headers_and_length_mismatch(void)
{
    unsigned char arena_storage[4096];
    SlArena arena = {0};
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};
    SlHttpRequestLifecycle request = {0};
    SlHttp2HeaderField bad_connection[] = {h2_header(":method", "GET"),
                                           h2_header(":scheme", "http"), h2_header(":path", "/"),
                                           h2_header("connection", "close")};
    SlHttp2HeaderField bad_length[] = {
        h2_header(":method", "POST"), h2_header(":scheme", "http"), h2_header(":path", "/"),
        h2_header("content-type", "text/plain"), h2_header("content-length", "99")};
    SlHttp2HeaderList connection_headers = {
        .fields = bad_connection, .count = sizeof(bad_connection) / sizeof(bad_connection[0])};
    SlHttp2HeaderList length_headers = {.fields = bad_length,
                                        .count = sizeof(bad_length) / sizeof(bad_length[0])};

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_http_backend_init(&backend, NULL, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_backend_start(&backend, NULL, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_backend_accept_connection(&backend, &connection, NULL),
                      SL_STATUS_OK) != 0)
    {
        return 1;
    }

    if (expect_status(sl_http2_request_from_headers(&arena, &connection, &connection_headers,
                                                    sl_bytes_empty(), &request, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_status(sl_http2_request_from_headers(&arena, &connection, &length_headers,
                                                    bytes_from_cstr("body"), &request, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 2;
    }

    return 0;
}

static int test_request_mapping_enforces_pseudo_header_contract(void)
{
    unsigned char arena_storage[4096];
    SlArena arena = {0};
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};
    SlHttpRequestLifecycle request = {0};
    SlHttp2HeaderField missing_scheme[] = {
        h2_header(":method", "GET"), h2_header(":authority", "localhost"), h2_header(":path", "/")};
    SlHttp2HeaderField scheme_mismatch[] = {
        h2_header(":method", "GET"), h2_header(":scheme", "https"),
        h2_header(":authority", "localhost"), h2_header(":path", "/")};
    SlHttp2HeaderField pseudo_after_regular[] = {
        h2_header("x-test", "ok"), h2_header(":method", "GET"), h2_header(":scheme", "http"),
        h2_header(":authority", "localhost"), h2_header(":path", "/")};
    SlHttp2HeaderField host_conflict[] = {h2_header(":method", "GET"), h2_header(":scheme", "http"),
                                          h2_header(":authority", "localhost"),
                                          h2_header(":path", "/"), h2_header("host", "other")};
    SlHttp2HeaderField connect[] = {h2_header(":method", "CONNECT"), h2_header(":scheme", "http"),
                                    h2_header(":authority", "localhost"), h2_header(":path", "/")};
    SlHttp2HeaderList cases[] = {
        {.fields = missing_scheme, .count = sizeof(missing_scheme) / sizeof(missing_scheme[0])},
        {.fields = scheme_mismatch, .count = sizeof(scheme_mismatch) / sizeof(scheme_mismatch[0])},
        {.fields = pseudo_after_regular,
         .count = sizeof(pseudo_after_regular) / sizeof(pseudo_after_regular[0])},
        {.fields = host_conflict, .count = sizeof(host_conflict) / sizeof(host_conflict[0])},
        {.fields = connect, .count = sizeof(connect) / sizeof(connect[0])}};

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_http_backend_init(&backend, NULL, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_backend_start(&backend, NULL, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_backend_accept_connection(&backend, &connection, NULL),
                      SL_STATUS_OK) != 0)
    {
        return 1;
    }

    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); index += 1U) {
        if (expect_status(sl_http2_request_from_headers(&arena, &connection, &cases[index],
                                                        sl_bytes_empty(), &request, NULL),
                          SL_STATUS_INVALID_ARGUMENT) != 0)
        {
            return (int)(10U + index);
        }
    }
    return 0;
}

static int test_response_mapping_generates_pseudo_and_managed_headers(void)
{
    unsigned char arena_storage[8192];
    SlArena arena = {0};
    SlHttpHeader custom = {.name = sl_str_from_cstr("x-test"), .value = sl_str_from_cstr("ok")};
    SlHttpResponse response = sl_http_response_text(200U, sl_str_from_cstr("hello"));
    SlHttp2HeaderList headers = {0};
    SlBytes body = {0};

    response.headers = &custom;
    response.header_count = 1U;
    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(
            sl_http2_response_to_headers(&arena, &response, false, 1024U, &headers, &body),
            SL_STATUS_OK) != 0)
    {
        return 1;
    }

    if (headers.count != 4U || expect_h2_header(&headers, 0U, ":status", "200") != 0 ||
        expect_h2_header(&headers, 1U, "content-type", "text/plain; charset=utf-8") != 0 ||
        expect_h2_header(&headers, 2U, "x-test", "ok") != 0 ||
        expect_h2_header(&headers, 3U, "content-length", "5") != 0 ||
        !sl_bytes_equal(body, bytes_from_cstr("hello")))
    {
        return 2;
    }

    return 0;
}

static int test_response_mapping_suppresses_body_but_keeps_length_for_head(void)
{
    unsigned char arena_storage[8192];
    SlArena arena = {0};
    SlHttpResponse response = sl_http_response_text(200U, sl_str_from_cstr("hello"));
    SlHttp2HeaderList headers = {0};
    SlBytes body = {0};

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_http2_response_to_headers(&arena, &response, true, 1024U, &headers, &body),
                      SL_STATUS_OK) != 0)
    {
        return 1;
    }

    if (!sl_bytes_is_empty(body) || headers.count != 3U ||
        expect_h2_header(&headers, 2U, "content-length", "5") != 0)
    {
        return 2;
    }

    return 0;
}

static int test_response_mapping_rejects_header_count_overflow(void)
{
    unsigned char arena_storage[1024];
    SlArena arena = {0};
    SlHttpHeader custom = {.name = sl_str_from_cstr("x-test"), .value = sl_str_from_cstr("ok")};
    SlHttpResponse response = sl_http_response_text(200U, sl_str_from_cstr("hello"));
    SlHttp2HeaderList headers = {0};
    SlBytes body = {0};

    response.headers = &custom;
    response.header_count = SIZE_MAX;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
        0)
    {
        return 1;
    }

    if (expect_status(
            sl_http2_response_to_headers(&arena, &response, false, 1024U, &headers, &body),
            SL_STATUS_OVERFLOW) != 0)
    {
        return 2;
    }

    return 0;
}

int main(void)
{
    int result = 0;

    result = test_request_mapping_materializes_lifecycle();
    if (result != 0) {
        return result;
    }
    result = test_request_mapping_rejects_connection_headers_and_length_mismatch();
    if (result != 0) {
        return result;
    }
    result = test_request_mapping_enforces_pseudo_header_contract();
    if (result != 0) {
        return result;
    }
    result = test_response_mapping_generates_pseudo_and_managed_headers();
    if (result != 0) {
        return result;
    }
    result = test_response_mapping_suppresses_body_but_keeps_length_for_head();
    if (result != 0) {
        return result;
    }
    result = test_response_mapping_rejects_header_count_overflow();
    if (result != 0) {
        return result;
    }

    return 0;
}

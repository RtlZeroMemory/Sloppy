#include "sloppy/http_response.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static int expect_response(SlHttpResponse response, const char* expected)
{
    unsigned char buffer[1024];
    SlBytes bytes = {0};

    if (expect_status(sl_http_response_write(&response, buffer, sizeof(buffer), &bytes),
                      SL_STATUS_OK) != 0)
    {
        return 1;
    }

    return expect_true(bytes.length == strlen(expected) &&
                       memcmp(bytes.ptr, expected, bytes.length) == 0);
}

static int expect_response_bytes(SlHttpResponse response, SlBytes expected)
{
    unsigned char buffer[1024];
    SlBytes bytes = {0};

    if (expect_status(sl_http_response_write(&response, buffer, sizeof(buffer), &bytes),
                      SL_STATUS_OK) != 0)
    {
        return 1;
    }

    return expect_true(sl_bytes_equal(bytes, expected));
}

static int expect_response_with_options(SlHttpResponse response,
                                        const SlHttpResponseWriteOptions* options,
                                        const char* expected)
{
    unsigned char buffer[1024];
    SlBytes bytes = {0};

    if (expect_status(
            sl_http_response_write_with_options(&response, options, buffer, sizeof(buffer), &bytes),
            SL_STATUS_OK) != 0)
    {
        return 1;
    }

    return expect_true(bytes.length == strlen(expected) &&
                       memcmp(bytes.ptr, expected, bytes.length) == 0);
}

static int test_text_200_exact_bytes(void)
{
    return expect_response(sl_http_response_text(200U, sl_str_from_cstr("hello")),
                           "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: text/plain; "
                           "charset=utf-8\r\nContent-Length: 5\r\n\r\nhello");
}

static int test_json_200_exact_bytes(void)
{
    return expect_response(
        sl_http_response_json(200U, sl_bytes_from_parts((const unsigned char*)"{\"ok\":true}",
                                                        sizeof("{\"ok\":true}") - 1U)),
        "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: application/json; "
        "charset=utf-8\r\nContent-Length: 11\r\n\r\n{\"ok\":true}");
}

static int test_keep_alive_connection_policy_exact_bytes(void)
{
    SlHttpResponseWriteOptions options = {0};

    options.connection = SL_HTTP_RESPONSE_CONNECTION_KEEP_ALIVE;
    return expect_response_with_options(
        sl_http_response_text(200U, sl_str_from_cstr("hello")), &options,
        "HTTP/1.1 200 OK\r\nConnection: keep-alive\r\nContent-Type: text/plain; "
        "charset=utf-8\r\nContent-Length: 5\r\n\r\nhello");
}

static int test_head_suppression_keeps_content_length_without_body(void)
{
    SlHttpResponseWriteOptions options = {0};

    options.connection = SL_HTTP_RESPONSE_CONNECTION_KEEP_ALIVE;
    options.suppress_body = true;
    return expect_response_with_options(
        sl_http_response_text(200U, sl_str_from_cstr("hello")), &options,
        "HTTP/1.1 200 OK\r\nConnection: keep-alive\r\nContent-Type: text/plain; "
        "charset=utf-8\r\nContent-Length: 5\r\n\r\n");
}

static int test_binary_body_exact_bytes(void)
{
    static const unsigned char body[] = {'A', '\0', 'B'};
    static const unsigned char expected[] = {
        'H',  'T',  'T',  'P',  '/',  '1',  '.',  '1',  ' ', '2',  '0', '0', ' ', 'O', 'K', '\r',
        '\n', 'C',  'o',  'n',  'n',  'e',  'c',  't',  'i', 'o',  'n', ':', ' ', 'c', 'l', 'o',
        's',  'e',  '\r', '\n', 'C',  'o',  'n',  't',  'e', 'n',  't', '-', 'T', 'y', 'p', 'e',
        ':',  ' ',  'a',  'p',  'p',  'l',  'i',  'c',  'a', 't',  'i', 'o', 'n', '/', 'j', 's',
        'o',  'n',  ';',  ' ',  'c',  'h',  'a',  'r',  's', 'e',  't', '=', 'u', 't', 'f', '-',
        '8',  '\r', '\n', 'C',  'o',  'n',  't',  'e',  'n', 't',  '-', 'L', 'e', 'n', 'g', 't',
        'h',  ':',  ' ',  '3',  '\r', '\n', '\r', '\n', 'A', '\0', 'B'};

    return expect_response_bytes(
        sl_http_response_json(200U, sl_bytes_from_parts(body, sizeof(body))),
        sl_bytes_from_parts(expected, sizeof(expected)));
}

static int test_no_content_writes_no_body_or_content_type(void)
{
    if (expect_response(sl_http_response_text(204U, sl_str_from_cstr("ignored")),
                        "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n") != 0)
    {
        return 1;
    }

    return expect_response(sl_http_response_text(304U, sl_str_from_cstr("ignored")),
                           "HTTP/1.1 304 Not Modified\r\nConnection: close\r\n\r\n");
}

static int test_no_body_statuses_preserve_custom_headers_only(void)
{
    SlHttpHeader header = {.name = sl_str_from_cstr("ETag"), .value = sl_str_from_cstr("\"v1\"")};
    SlHttpResponse response = sl_http_response_text(204U, sl_str_from_cstr("ignored"));

    response.headers = &header;
    response.header_count = 1U;
    if (expect_response(
            response, "HTTP/1.1 204 No Content\r\nConnection: close\r\nETag: \"v1\"\r\n\r\n") != 0)
    {
        return 1;
    }

    response = sl_http_response_text(304U, sl_str_from_cstr("ignored"));
    response.headers = &header;
    response.header_count = 1U;
    return expect_response(
        response, "HTTP/1.1 304 Not Modified\r\nConnection: close\r\nETag: \"v1\"\r\n\r\n");
}

static int test_statuses_and_content_length(void)
{
    if (expect_response(sl_http_response_text(404U, sl_str_from_cstr("Not Found\n")),
                        "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Type: "
                        "text/plain; charset=utf-8\r\nContent-Length: 10\r\n\r\nNot "
                        "Found\n") != 0)
    {
        return 1;
    }

    if (expect_response(sl_http_response_empty(201U),
                        "HTTP/1.1 201 Created\r\nConnection: close\r\nContent-Length: "
                        "0\r\n\r\n") != 0)
    {
        return 2;
    }

    return expect_response(sl_http_response_text(501U, sl_str_from_cstr("No body\n")),
                           "HTTP/1.1 501 Not Implemented\r\nConnection: close\r\nContent-Type: "
                           "text/plain; charset=utf-8\r\nContent-Length: 8\r\n\r\nNo body\n");
}

static int test_error_statuses_for_http_policy(void)
{
    if (expect_response(sl_http_response_text(413U, sl_str_from_cstr("Too large\n")),
                        "HTTP/1.1 413 Payload Too Large\r\nConnection: close\r\nContent-Type: "
                        "text/plain; charset=utf-8\r\nContent-Length: 10\r\n\r\nToo large\n") != 0)
    {
        return 1;
    }

    if (expect_response(sl_http_response_text(415U, sl_str_from_cstr("Unsupported\n")),
                        "HTTP/1.1 415 Unsupported Media Type\r\nConnection: close\r\n"
                        "Content-Type: text/plain; charset=utf-8\r\nContent-Length: "
                        "12\r\n\r\nUnsupported\n") != 0)
    {
        return 2;
    }

    return expect_response(
        sl_http_response_text(417U, sl_str_from_cstr("Expectation Failed\n")),
        "HTTP/1.1 417 Expectation Failed\r\nConnection: close\r\nContent-Type: "
        "text/plain; charset=utf-8\r\nContent-Length: 19\r\n\r\nExpectation Failed\n");
}

static int test_custom_headers_are_written_after_content_type(void)
{
    SlHttpHeader headers[2];
    SlHttpResponse response = sl_http_response_text(200U, sl_str_from_cstr("hello"));

    headers[0].name = sl_str_from_cstr("X-Test");
    headers[0].value = sl_str_from_cstr("one");
    headers[1].name = sl_str_from_cstr("Cache-Control");
    headers[1].value = sl_str_from_cstr("no-store");
    response.headers = headers;
    response.header_count = 2U;

    return expect_response(response,
                           "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: text/plain; "
                           "charset=utf-8\r\nX-Test: one\r\nCache-Control: "
                           "no-store\r\nContent-Length: 5\r\n\r\nhello");
}

static int test_invalid_status_and_content_type_are_rejected(void)
{
    unsigned char buffer[256];
    SlBytes bytes = {0};
    SlHttpResponse response = sl_http_response_text(299U, sl_str_from_cstr("bad"));

    if (expect_status(sl_http_response_write(&response, buffer, sizeof(buffer), &bytes),
                      SL_STATUS_UNSUPPORTED) != 0)
    {
        return 1;
    }

    response = sl_http_response_text(200U, sl_str_from_cstr("bad"));
    response.content_type = sl_str_from_cstr("text/plain\r\nx-bad: yes");
    if (expect_status(sl_http_response_write(&response, buffer, sizeof(buffer), &bytes),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 2;
    }

    response.content_type = sl_str_from_parts("text/plain\0html", sizeof("text/plain\0html") - 1U);
    return expect_status(sl_http_response_write(&response, buffer, sizeof(buffer), &bytes),
                         SL_STATUS_INVALID_ARGUMENT);
}

static int test_invalid_custom_headers_are_rejected(void)
{
    unsigned char buffer[256];
    SlBytes bytes = {0};
    SlHttpHeader header = {0};
    SlHttpResponse response = sl_http_response_text(200U, sl_str_from_cstr("bad"));

    header.name = sl_str_from_cstr("Content-Length");
    header.value = sl_str_from_cstr("3");
    response.headers = &header;
    response.header_count = 1U;
    if (expect_status(sl_http_response_write(&response, buffer, sizeof(buffer), &bytes),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 1;
    }

    header.name = sl_str_from_cstr("X-Test");
    header.value = sl_str_from_cstr("bad\r\nX-Injected: yes");
    if (expect_status(sl_http_response_write(&response, buffer, sizeof(buffer), &bytes),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 2;
    }

    header.name = sl_str_from_cstr("Connection");
    header.value = sl_str_from_cstr("keep-alive");
    if (expect_status(sl_http_response_write(&response, buffer, sizeof(buffer), &bytes),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 3;
    }

    header.name = sl_str_from_cstr("Content-Type");
    header.value = sl_str_from_cstr("text/html");
    if (expect_status(sl_http_response_write(&response, buffer, sizeof(buffer), &bytes),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 4;
    }

    header.name = sl_str_from_cstr("Keep-Alive");
    header.value = sl_str_from_cstr("timeout=5");
    if (expect_status(sl_http_response_write(&response, buffer, sizeof(buffer), &bytes),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 5;
    }

    header.name = sl_str_from_cstr("X-Test");
    header.value = sl_str_from_parts("bad\0value", sizeof("bad\0value") - 1U);
    return expect_status(sl_http_response_write(&response, buffer, sizeof(buffer), &bytes),
                         SL_STATUS_INVALID_ARGUMENT);
}

static int test_capacity_failure_returns_empty_output(void)
{
    unsigned char buffer[8];
    SlBytes bytes = sl_bytes_from_parts((const unsigned char*)"stale", sizeof("stale") - 1U);
    SlHttpResponse response = sl_http_response_text(200U, sl_str_from_cstr("hello"));

    if (expect_status(sl_http_response_write(&response, buffer, sizeof(buffer), &bytes),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0)
    {
        return 1;
    }

    return expect_true(bytes.ptr == NULL && bytes.length == 0U);
}

static int test_stream_descriptor_normalizes_null_chunks(void)
{
    SlHttpResponseStreamChunk chunks[1] = {
        {.bytes = {NULL, 0U}},
    };
    SlHttpResponse response =
        sl_http_response_stream(200U, sl_str_from_cstr("text/plain"), NULL, 1U);

    if (response.kind != SL_HTTP_RESPONSE_STREAM || response.stream_chunks != NULL ||
        response.stream_chunk_count != 0U)
    {
        return 1;
    }

    response = sl_http_response_stream(200U, sl_str_from_cstr("text/plain"), chunks, 0U);
    return expect_true(response.stream_chunks == NULL && response.stream_chunk_count == 0U);
}

static int run_test(const char* name, int (*test)(void))
{
    int result = test();

    if (result != 0) {
#ifdef _MSC_VER
        fprintf_s(stderr, "FAIL: %s returned %d\n", name, result);
#else
        fprintf(stderr, "FAIL: %s returned %d\n", name, result);
#endif
    }

    return result;
}

int main(void)
{
    int result = run_test("test_text_200_exact_bytes", test_text_200_exact_bytes);
    if (result != 0) {
        return result;
    }

    result = run_test("test_json_200_exact_bytes", test_json_200_exact_bytes);
    if (result != 0) {
        return result;
    }
    result = run_test("test_keep_alive_connection_policy_exact_bytes",
                      test_keep_alive_connection_policy_exact_bytes);
    if (result != 0) {
        return result;
    }

    result = run_test("test_head_suppression_keeps_content_length_without_body",
                      test_head_suppression_keeps_content_length_without_body);
    if (result != 0) {
        return result;
    }

    result = run_test("test_binary_body_exact_bytes", test_binary_body_exact_bytes);
    if (result != 0) {
        return result;
    }

    result = run_test("test_no_content_writes_no_body_or_content_type",
                      test_no_content_writes_no_body_or_content_type);
    if (result != 0) {
        return result;
    }

    result = run_test("test_no_body_statuses_preserve_custom_headers_only",
                      test_no_body_statuses_preserve_custom_headers_only);
    if (result != 0) {
        return result;
    }

    result = run_test("test_statuses_and_content_length", test_statuses_and_content_length);
    if (result != 0) {
        return result;
    }

    result = run_test("test_error_statuses_for_http_policy", test_error_statuses_for_http_policy);
    if (result != 0) {
        return result;
    }

    result = run_test("test_custom_headers_are_written_after_content_type",
                      test_custom_headers_are_written_after_content_type);
    if (result != 0) {
        return result;
    }

    result = run_test("test_invalid_status_and_content_type_are_rejected",
                      test_invalid_status_and_content_type_are_rejected);
    if (result != 0) {
        return result;
    }

    result = run_test("test_invalid_custom_headers_are_rejected",
                      test_invalid_custom_headers_are_rejected);
    if (result != 0) {
        return result;
    }

    result = run_test("test_capacity_failure_returns_empty_output",
                      test_capacity_failure_returns_empty_output);
    if (result != 0) {
        return result;
    }

    return run_test("test_stream_descriptor_normalizes_null_chunks",
                    test_stream_descriptor_normalizes_null_chunks);
}

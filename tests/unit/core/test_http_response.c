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

static int test_no_content_writes_no_body_or_content_type(void)
{
    return expect_response(sl_http_response_text(204U, sl_str_from_cstr("ignored")),
                           "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n");
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

    return expect_response(sl_http_response_empty(201U),
                           "HTTP/1.1 201 Created\r\nConnection: close\r\nContent-Length: "
                           "0\r\n\r\n");
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
    return expect_status(sl_http_response_write(&response, buffer, sizeof(buffer), &bytes),
                         SL_STATUS_INVALID_ARGUMENT);
}

static int run_test(const char* name, int (*test)(void))
{
    int result = test();

    if (result != 0) {
        fprintf_s(stderr, "FAIL: %s returned %d\n", name, result);
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

    result = run_test("test_no_content_writes_no_body_or_content_type",
                      test_no_content_writes_no_body_or_content_type);
    if (result != 0) {
        return result;
    }

    result = run_test("test_statuses_and_content_length", test_statuses_and_content_length);
    if (result != 0) {
        return result;
    }

    return run_test("test_invalid_status_and_content_type_are_rejected",
                    test_invalid_status_and_content_type_are_rejected);
}

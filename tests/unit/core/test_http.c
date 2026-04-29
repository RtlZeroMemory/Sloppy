#include "sloppy/http.h"
#include "sloppy/route.h"

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

static int expect_str_equal(SlStr actual, const char* expected)
{
    return expect_true(sl_str_equal(actual, sl_str_from_cstr(expected)));
}

static SlBytes bytes_from_cstr(const char* text)
{
    SlStr str = sl_str_from_cstr(text);
    return sl_bytes_from_parts((const unsigned char*)str.ptr, str.length);
}

static SlStatus parse_request(SlArena* arena, const char* text, const SlHttpParseOptions* options,
                              SlHttpRequestHead* out, SlDiag* out_diag)
{
    return sl_http_parse_request_head(arena, bytes_from_cstr(text), options, out, out_diag);
}

static int test_parse_valid_targets(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlHttpRequestHead request = {0};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 1;
    }

    status = parse_request(&arena, "GET / HTTP/1.1\r\n\r\n", NULL, &request, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || request.method != SL_HTTP_METHOD_GET ||
        expect_str_equal(request.raw_target, "/") != 0 || expect_str_equal(request.path, "/") != 0)
    {
        return 2;
    }

    sl_arena_reset(&arena);
    status = parse_request(&arena, "GET /users/123 HTTP/1.1\r\n\r\n", NULL, &request, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 ||
        expect_str_equal(request.raw_target, "/users/123") != 0 ||
        expect_str_equal(request.path, "/users/123") != 0)
    {
        return 3;
    }

    sl_arena_reset(&arena);
    status = parse_request(&arena, "GET /users/123?x=1 HTTP/1.1\r\n\r\n", NULL, &request, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 ||
        expect_str_equal(request.raw_target, "/users/123?x=1") != 0 ||
        expect_str_equal(request.path, "/users/123") != 0)
    {
        return 4;
    }

    return 0;
}

static int test_parse_headers(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlHttpRequestHead request = {0};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 10;
    }

    status =
        parse_request(&arena, "GET / HTTP/1.1\r\nHost: example.test\r\nAccept: text/plain\r\n\r\n",
                      NULL, &request, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || request.header_count != 2U ||
        request.headers == NULL)
    {
        return 11;
    }

    if (expect_str_equal(request.headers[0].name, "Host") != 0 ||
        expect_str_equal(request.headers[0].value, "example.test") != 0 ||
        expect_str_equal(request.headers[1].name, "Accept") != 0 ||
        expect_str_equal(request.headers[1].value, "text/plain") != 0)
    {
        return 12;
    }

    return 0;
}

static int test_supported_method_mapping(void)
{
    typedef struct MethodCase
    {
        const char* request;
        SlHttpMethod method;
    } MethodCase;

    static const MethodCase cases[] = {{"GET / HTTP/1.1\r\n\r\n", SL_HTTP_METHOD_GET},
                                       {"POST / HTTP/1.1\r\n\r\n", SL_HTTP_METHOD_POST},
                                       {"PUT / HTTP/1.1\r\n\r\n", SL_HTTP_METHOD_PUT},
                                       {"DELETE / HTTP/1.1\r\n\r\n", SL_HTTP_METHOD_DELETE},
                                       {"PATCH / HTTP/1.1\r\n\r\n", SL_HTTP_METHOD_PATCH},
                                       {"OPTIONS / HTTP/1.1\r\n\r\n", SL_HTTP_METHOD_OPTIONS},
                                       {"HEAD / HTTP/1.1\r\n\r\n", SL_HTTP_METHOD_HEAD}};
    size_t index = 0U;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index += 1U) {
        unsigned char storage[TEST_ARENA_SIZE];
        SlArena arena = {0};
        SlHttpRequestHead request = {0};
        SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

        if (!sl_status_is_ok(status)) {
            return 20;
        }

        status = parse_request(&arena, cases[index].request, NULL, &request, NULL);
        if (expect_status(status, SL_STATUS_OK) != 0 || request.method != cases[index].method) {
            return 21 + (int)index;
        }
    }

    return 0;
}

static int test_rejects_invalid_requests(void)
{
    static const char* cases[] = {
        "GET /\r\n\r\n", "GET / HTTP/1.1", "BAD METHOD / HTTP/1.1\r\n\r\n",
        "GET / HTTP/1.1\r\nHeader without colon\r\n\r\n", "GET  HTTP/1.1\r\n\r\n"};
    size_t index = 0U;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index += 1U) {
        unsigned char storage[TEST_ARENA_SIZE];
        SlArena arena = {0};
        SlHttpRequestHead request = {0};
        SlDiag diag = {0};
        SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

        if (!sl_status_is_ok(status)) {
            return 30;
        }

        status = parse_request(&arena, cases[index], NULL, &request, &diag);
        if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
            diag.code != SL_DIAG_INVALID_HTTP_REQUEST || request.raw_target.ptr != NULL)
        {
            return 31 + (int)index;
        }
    }

    return 0;
}

static int test_rejects_unsupported_method(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlHttpRequestHead request = {0};
    SlDiag diag = {0};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 40;
    }

    status = parse_request(&arena, "CONNECT / HTTP/1.1\r\n\r\n", NULL, &request, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_INVALID_HTTP_REQUEST)
    {
        return 41;
    }

    return 0;
}

static int test_rejects_non_path_targets(void)
{
    static const char* cases[] = {"OPTIONS * HTTP/1.1\r\n\r\n",
                                  "GET http://example.test/users HTTP/1.1\r\n\r\n"};
    size_t index = 0U;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index += 1U) {
        unsigned char storage[TEST_ARENA_SIZE];
        SlArena arena = {0};
        SlHttpRequestHead request = {0};
        SlDiag diag = {0};
        SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

        if (!sl_status_is_ok(status)) {
            return 45;
        }

        status = parse_request(&arena, cases[index], NULL, &request, &diag);
        if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
            diag.code != SL_DIAG_INVALID_HTTP_REQUEST || request.path.ptr != NULL)
        {
            return 46 + (int)index;
        }
    }

    return 0;
}

static int test_callback_allocation_failure_preserved(void)
{
    unsigned char storage[8];
    SlArena arena = {0};
    SlHttpRequestHead request = {0};
    SlDiag diag = {0};
    SlHttpParseOptions options = {0U};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 48;
    }

    status = parse_request(&arena, "GET /this-target-does-not-fit HTTP/1.1\r\n\r\n", &options,
                           &request, &diag);
    if (expect_status(status, SL_STATUS_OUT_OF_MEMORY) != 0 || diag.code != SL_DIAG_NONE ||
        request.raw_target.ptr != NULL)
    {
        return 49;
    }

    return 0;
}

static int test_max_headers_enforced(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlHttpRequestHead request = {0};
    SlDiag diag = {0};
    SlHttpParseOptions options = {1U};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 50;
    }

    status =
        parse_request(&arena, "GET / HTTP/1.1\r\nA: 1\r\nB: 2\r\n\r\n", &options, &request, &diag);
    if (expect_status(status, SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        diag.code != SL_DIAG_HTTP_HEADER_LIMIT || request.header_count != 0U)
    {
        return 51;
    }

    return 0;
}

static int test_zero_header_limit_allows_no_headers(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlHttpRequestHead request = {0};
    SlDiag diag = {0};
    SlHttpParseOptions options = {0U};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 60;
    }

    status = parse_request(&arena, "GET / HTTP/1.1\r\n\r\n", &options, &request, &diag);
    if (expect_status(status, SL_STATUS_OK) != 0 || request.headers != NULL ||
        request.header_count != 0U)
    {
        return 61;
    }

    sl_arena_reset(&arena);
    status = parse_request(&arena, "GET / HTTP/1.1\r\nHost: example.test\r\n\r\n", &options,
                           &request, &diag);
    if (expect_status(status, SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        diag.code != SL_DIAG_HTTP_HEADER_LIMIT)
    {
        return 62;
    }

    return 0;
}

static int test_invalid_arguments(void)
{
    unsigned char storage[1024];
    SlArena arena = {0};
    SlHttpRequestHead request = {0};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 70;
    }

    if (expect_status(sl_http_parse_request_head(NULL, bytes_from_cstr("GET / HTTP/1.1\r\n\r\n"),
                                                 NULL, &request, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 71;
    }

    if (expect_status(sl_http_parse_request_head(&arena, bytes_from_cstr("GET / HTTP/1.1\r\n\r\n"),
                                                 NULL, NULL, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 72;
    }

    if (expect_status(
            sl_http_parse_request_head(&arena, sl_bytes_from_parts(NULL, 1U), NULL, &request, NULL),
            SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 73;
    }

    return 0;
}

static int test_parsed_path_can_feed_route_matcher(void)
{
    unsigned char request_storage[TEST_ARENA_SIZE];
    unsigned char route_storage[TEST_ARENA_SIZE];
    unsigned char match_storage[TEST_ARENA_SIZE];
    SlArena request_arena = {0};
    SlArena route_arena = {0};
    SlArena match_arena = {0};
    SlHttpRequestHead request = {0};
    SlRoutePattern pattern = {0};
    SlRouteMatch match = {0};
    SlStatus status;

    status = sl_arena_init(&request_arena, request_storage, sizeof(request_storage));
    if (!sl_status_is_ok(status)) {
        return 80;
    }
    status = sl_arena_init(&route_arena, route_storage, sizeof(route_storage));
    if (!sl_status_is_ok(status)) {
        return 81;
    }
    status = sl_arena_init(&match_arena, match_storage, sizeof(match_storage));
    if (!sl_status_is_ok(status)) {
        return 82;
    }

    status =
        parse_request(&request_arena, "GET /users/123?x=1 HTTP/1.1\r\n\r\n", NULL, &request, NULL);
    if (!sl_status_is_ok(status)) {
        return 83;
    }

    status =
        sl_route_pattern_parse(&route_arena, sl_str_from_cstr("/users/{id:int}"), &pattern, NULL);
    if (!sl_status_is_ok(status)) {
        return 84;
    }

    status = sl_route_pattern_match(&match_arena, &pattern, request.path, &match);
    if (!sl_status_is_ok(status) || !match.matched || match.param_count != 1U ||
        expect_str_equal(match.params[0].value, "123") != 0)
    {
        return 85;
    }

    return 0;
}

static int test_libuv_smoke(void)
{
    return expect_status(sl_http_libuv_smoke(), SL_STATUS_OK) == 0 ? 0 : 90;
}

int main(void)
{
    int result = test_parse_valid_targets();
    if (result != 0) {
        return result;
    }

    result = test_parse_headers();
    if (result != 0) {
        return result;
    }

    result = test_supported_method_mapping();
    if (result != 0) {
        return result;
    }

    result = test_rejects_invalid_requests();
    if (result != 0) {
        return result;
    }

    result = test_rejects_unsupported_method();
    if (result != 0) {
        return result;
    }

    result = test_rejects_non_path_targets();
    if (result != 0) {
        return result;
    }

    result = test_callback_allocation_failure_preserved();
    if (result != 0) {
        return result;
    }

    result = test_max_headers_enforced();
    if (result != 0) {
        return result;
    }

    result = test_zero_header_limit_allows_no_headers();
    if (result != 0) {
        return result;
    }

    result = test_invalid_arguments();
    if (result != 0) {
        return result;
    }

    result = test_parsed_path_can_feed_route_matcher();
    if (result != 0) {
        return result;
    }

    return test_libuv_smoke();
}

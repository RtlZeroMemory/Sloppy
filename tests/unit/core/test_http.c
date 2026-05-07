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

static int expect_bytes_equal(SlBytes actual, const char* expected)
{
    SlBytes expected_bytes = bytes_from_cstr(expected);

    return expect_true(sl_bytes_equal(actual, expected_bytes));
}

static SlStatus parse_request(SlArena* arena, const char* text, const SlHttpParseOptions* options,
                              SlHttpRequestHead* out, SlDiag* out_diag)
{
    return sl_http_parse_request_head(arena, bytes_from_cstr(text), options, out, out_diag);
}

static SlStatus parse_request_bytes(SlArena* arena, const unsigned char* bytes, size_t length,
                                    const SlHttpParseOptions* options, SlHttpRequestHead* out,
                                    SlDiag* out_diag)
{
    return sl_http_parse_request_head(arena, sl_bytes_from_parts(bytes, length), options, out,
                                      out_diag);
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

    status =
        parse_request(&arena, "GET / HTTP/1.1\r\nHost: example.test\r\n\r\n", NULL, &request, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || request.method != SL_HTTP_METHOD_GET ||
        expect_str_equal(request.raw_target, "/") != 0 || expect_str_equal(request.path, "/") != 0)
    {
        return 2;
    }

    sl_arena_reset(&arena);
    status = parse_request(&arena, "GET /users/123 HTTP/1.1\r\nHost: example.test\r\n\r\n", NULL,
                           &request, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 ||
        expect_str_equal(request.raw_target, "/users/123") != 0 ||
        expect_str_equal(request.path, "/users/123") != 0)
    {
        return 3;
    }

    sl_arena_reset(&arena);
    status = parse_request(&arena, "GET /users/123?x=1 HTTP/1.1\r\nHost: example.test\r\n\r\n",
                           NULL, &request, NULL);
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

static int test_parse_body_bytes(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlHttpRequestHead request = {0};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 13;
    }

    status = parse_request(&arena,
                           "POST /items HTTP/1.1\r\nHost: example.test\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: 11\r\n\r\n{\"ok\":true}",
                           NULL, &request, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || request.method != SL_HTTP_METHOD_POST ||
        expect_str_equal(request.path, "/items") != 0 ||
        expect_bytes_equal(request.body, "{\"ok\":true}") != 0)
    {
        return 14;
    }

    return 0;
}

static int test_parse_non_nul_terminated_request_storage(void)
{
    static const unsigned char input[] = {'G', 'E', 'T', ' ', '/',  'n',  'o',  'n',  'n', 'u',
                                          'l', '?', 'x', '=', '1',  ' ',  'H',  'T',  'T', 'P',
                                          '/', '1', '.', '1', '\r', '\n', 'H',  'o',  's', 't',
                                          ':', ' ', 'e', 'x', '\r', '\n', '\r', '\n', 'Z'};
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlHttpRequestHead request = {0};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 15;
    }

    status = parse_request_bytes(&arena, input, sizeof(input) - 1U, NULL, &request, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || request.headers == NULL ||
        request.header_count == 0U || expect_str_equal(request.raw_target, "/nonnul?x=1") != 0 ||
        expect_str_equal(request.path, "/nonnul") != 0 ||
        request.raw_target.ptr == (const char*)input + 4U || request.headers[0].name.ptr == NULL ||
        request.headers[0].name.ptr == (const char*)input + 26U)
    {
        return 16;
    }

    return 0;
}

static int test_parse_binary_body_bytes(void)
{
    static const unsigned char input[] = {
        'P', 'O', 'S', 'T',  ' ',  '/', 'b', 'i',  'n',  ' ',  'H',  'T', 'T',  'P', '/',
        '1', '.', '0', '\r', '\n', 'C', 'o', 'n',  't',  'e',  'n',  't', '-',  'L', 'e',
        'n', 'g', 't', 'h',  ':',  ' ', '4', '\r', '\n', '\r', '\n', 'A', '\0', 'B', 0xffU};
    static const unsigned char expected[] = {'A', '\0', 'B', 0xffU};
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlHttpRequestHead request = {0};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 17;
    }

    status = parse_request_bytes(&arena, input, sizeof(input), NULL, &request, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 ||
        request.body.ptr == input + sizeof(input) - sizeof(expected) ||
        !sl_bytes_equal(request.body, sl_bytes_from_parts(expected, sizeof(expected))))
    {
        return 18;
    }

    return 0;
}

static int test_failed_parse_rolls_back_transient_builder_memory(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlHttpRequestHead request = {0};
    SlHttpRequestHead request_no_diag = {0};
    SlDiag diag = {0};
    SlHttpParseOptions options = {0};
    size_t used_before = 0U;
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 19;
    }

    used_before = sl_arena_used(&arena);
    options.max_headers = SL_HTTP_DEFAULT_MAX_HEADERS;
    options.max_target_length = SL_HTTP_DEFAULT_MAX_TARGET_LENGTH;
    options.max_body_length = 2U;

    status = parse_request(&arena,
                           "POST / HTTP/1.1\r\nHost: example.test\r\n"
                           "Content-Length: 3\r\n\r\nabc",
                           &options, &request_no_diag, NULL);
    if (expect_status(status, SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        sl_arena_used(&arena) != used_before || request_no_diag.body.ptr != NULL)
    {
        return 20;
    }

    status = parse_request(&arena,
                           "POST / HTTP/1.1\r\nHost: example.test\r\n"
                           "Content-Length: 3\r\n\r\nabc",
                           &options, &request, &diag);
    if (expect_status(status, SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        diag.code != SL_DIAG_HTTP_BODY_LIMIT || sl_arena_used(&arena) <= used_before ||
        request.body.ptr != NULL)
    {
        return 21;
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

    static const MethodCase cases[] = {
        {"GET / HTTP/1.1\r\nHost: example.test\r\n\r\n", SL_HTTP_METHOD_GET},
        {"POST / HTTP/1.1\r\nHost: example.test\r\n\r\n", SL_HTTP_METHOD_POST},
        {"PUT / HTTP/1.1\r\nHost: example.test\r\n\r\n", SL_HTTP_METHOD_PUT},
        {"DELETE / HTTP/1.1\r\nHost: example.test\r\n\r\n", SL_HTTP_METHOD_DELETE},
        {"PATCH / HTTP/1.1\r\nHost: example.test\r\n\r\n", SL_HTTP_METHOD_PATCH},
        {"OPTIONS / HTTP/1.1\r\nHost: example.test\r\n\r\n", SL_HTTP_METHOD_OPTIONS},
        {"HEAD / HTTP/1.1\r\nHost: example.test\r\n\r\n", SL_HTTP_METHOD_HEAD}};
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
    static const char* cases[] = {"GET /\r\n\r\n", "GET / HTTP/1.1",
                                  "BAD METHOD / HTTP/1.1\r\nHost: example.test\r\n\r\n",
                                  "GET / HTTP/1.1\r\nHeader without colon\r\n\r\n",
                                  "GET  HTTP/1.1\r\nHost: example.test\r\n\r\n"};
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

    status = parse_request(&arena, "CONNECT / HTTP/1.1\r\nHost: example.test\r\n\r\n", NULL,
                           &request, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_INVALID_HTTP_REQUEST)
    {
        return 41;
    }

    return 0;
}

static int test_rejects_non_path_targets(void)
{
    static const char* cases[] = {
        "OPTIONS * HTTP/1.1\r\nHost: example.test\r\n\r\n",
        "GET http://example.test/users HTTP/1.1\r\nHost: example.test\r\n\r\n"};
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

    status = parse_request(&arena,
                           "GET /this-target-does-not-fit HTTP/1.1\r\n"
                           "Host: example.test\r\n\r\n",
                           &options, &request, &diag);
    if (expect_status(status, SL_STATUS_OUT_OF_MEMORY) != 0 || diag.code != SL_DIAG_NONE ||
        request.raw_target.ptr != NULL)
    {
        return 49;
    }

    return 0;
}

static int test_max_target_length_enforced(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlHttpRequestHead request = {0};
    SlDiag diag = {0};
    SlHttpParseOptions options = {0};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 52;
    }

    options.max_headers = SL_HTTP_DEFAULT_MAX_HEADERS;
    options.max_target_length = 4U;
    status = parse_request(&arena, "GET /toolong HTTP/1.1\r\nHost: example.test\r\n\r\n", &options,
                           &request, &diag);
    if (expect_status(status, SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        diag.code != SL_DIAG_HTTP_TARGET_LIMIT || request.raw_target.ptr != NULL)
    {
        return 53;
    }

    return 0;
}

static int test_max_headers_enforced(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlHttpRequestHead request = {0};
    SlDiag diag = {0};
    SlHttpParseOptions options = {.max_headers = 1U};
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

static int test_header_name_value_and_total_limits_enforced(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlHttpRequestHead request = {0};
    SlDiag diag = {0};
    SlHttpParseOptions options = {0};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 57;
    }

    options.max_headers = SL_HTTP_DEFAULT_MAX_HEADERS;
    options.max_header_name_length = 3U;
    status =
        parse_request(&arena, "GET / HTTP/1.1\r\nLong: value\r\n\r\n", &options, &request, &diag);
    if (expect_status(status, SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        diag.code != SL_DIAG_HTTP_HEADER_NAME_LIMIT || request.header_count != 0U)
    {
        return 58;
    }

    sl_arena_reset(&arena);
    request = (SlHttpRequestHead){0};
    diag = (SlDiag){0};
    options.max_header_name_length = SL_HTTP_DEFAULT_MAX_HEADER_NAME_LENGTH;
    options.max_header_value_length = 3U;
    status = parse_request(&arena, "GET / HTTP/1.1\r\nX: value\r\n\r\n", &options, &request, &diag);
    if (expect_status(status, SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        diag.code != SL_DIAG_HTTP_HEADER_VALUE_LIMIT || request.header_count != 0U)
    {
        return 59;
    }

    sl_arena_reset(&arena);
    request = (SlHttpRequestHead){0};
    diag = (SlDiag){0};
    options.max_header_value_length = SL_HTTP_DEFAULT_MAX_HEADER_VALUE_LENGTH;
    options.max_total_header_bytes = 6U;
    status = parse_request(&arena, "GET / HTTP/1.1\r\nA: 1234\r\nB: 5\r\n\r\n", &options, &request,
                           &diag);
    if (expect_status(status, SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        diag.code != SL_DIAG_HTTP_HEADER_BYTES_LIMIT || request.header_count != 0U)
    {
        return 60;
    }

    return 0;
}

static int test_max_body_length_enforced(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlHttpRequestHead request = {0};
    SlDiag diag = {0};
    SlHttpParseOptions options = {0};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 54;
    }

    options.max_headers = SL_HTTP_DEFAULT_MAX_HEADERS;
    options.max_target_length = SL_HTTP_DEFAULT_MAX_TARGET_LENGTH;
    options.max_body_length = 4U;
    status = parse_request(&arena,
                           "POST / HTTP/1.1\r\nHost: example.test\r\n"
                           "Content-Length: 5\r\n\r\n12345",
                           &options, &request, &diag);
    if (expect_status(status, SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        diag.code != SL_DIAG_HTTP_BODY_LIMIT || request.body.length != 0U)
    {
        return 55;
    }

    sl_arena_reset(&arena);
    request = (SlHttpRequestHead){0};
    diag = (SlDiag){0};
    status = parse_request(&arena,
                           "POST / HTTP/1.1\r\nHost: example.test\r\n"
                           "Content-Length: 4\r\n\r\n1234",
                           &options, &request, &diag);
    if (expect_status(status, SL_STATUS_OK) != 0 || diag.code == SL_DIAG_HTTP_BODY_LIMIT ||
        request.body.length != 4U)
    {
        return 56;
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

    status = parse_request(&arena, "GET / HTTP/1.0\r\n\r\n", &options, &request, &diag);
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

static int test_request_line_host_and_singleton_policy(void)
{
    static const char* invalid_cases[] = {
        "GET / HTTP/1.1\r\n\r\n", "GET / HTTP/1.1\r\nHost:\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: one\r\nHost: two\r\n\r\n",
        "POST / HTTP/1.1\r\nHost: example.test\r\nContent-Length: 1\r\nContent-Length: "
        "1\r\n\r\nx",
        "POST / HTTP/1.1\r\nHost: example.test\r\nTransfer-Encoding: chunked\r\n"
        "Content-Length: 1\r\n\r\n0\r\n\r\n"};
    size_t index = 0U;

    for (index = 0U; index < sizeof(invalid_cases) / sizeof(invalid_cases[0]); index += 1U) {
        unsigned char storage[TEST_ARENA_SIZE];
        SlArena arena = {0};
        SlHttpRequestHead request = {0};
        SlDiag diag = {0};
        SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

        if (!sl_status_is_ok(status)) {
            return 90;
        }

        status = parse_request(&arena, invalid_cases[index], NULL, &request, &diag);
        if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
            diag.code != SL_DIAG_INVALID_HTTP_REQUEST || request.raw_target.ptr != NULL)
        {
            return 91 + (int)index;
        }
    }

    {
        unsigned char storage[TEST_ARENA_SIZE];
        SlArena arena = {0};
        SlHttpRequestHead request = {0};
        SlDiag diag = {0};
        SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

        if (!sl_status_is_ok(status)) {
            return 100;
        }

        status = parse_request(&arena, "GET /legacy HTTP/1.0\r\n\r\n", NULL, &request, &diag);
        if (expect_status(status, SL_STATUS_OK) != 0 || request.version_major != 1U ||
            request.version_minor != 0U || diag.code != SL_DIAG_NONE)
        {
            return 101;
        }
    }

    {
        unsigned char storage[TEST_ARENA_SIZE];
        SlArena arena = {0};
        SlHttpRequestHead request = {0};
        SlDiag diag = {0};
        SlHttpParseOptions options = {0};
        SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

        if (!sl_status_is_ok(status)) {
            return 102;
        }

        options.max_headers = SL_HTTP_DEFAULT_MAX_HEADERS;
        options.max_request_line_length = 8U;
        status = parse_request(&arena, "GET /toolong HTTP/1.1\r\nHost: example.test\r\n\r\n",
                               &options, &request, &diag);
        if (expect_status(status, SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
            diag.code != SL_DIAG_HTTP_REQUEST_LINE_LIMIT || request.raw_target.ptr != NULL)
        {
            return 103;
        }
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

    if (expect_status(sl_http_parse_request_head(NULL,
                                                 bytes_from_cstr("GET / HTTP/1.1\r\n"
                                                                 "Host: example.test\r\n\r\n"),
                                                 NULL, &request, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 71;
    }

    if (expect_status(sl_http_parse_request_head(&arena,
                                                 bytes_from_cstr("GET / HTTP/1.1\r\n"
                                                                 "Host: example.test\r\n\r\n"),
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
        parse_request(&request_arena, "GET /users/123?x=1 HTTP/1.1\r\nHost: example.test\r\n\r\n",
                      NULL, &request, NULL);
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

static int test_stress_repeated_valid_requests_remain_bounded(void)
{
    static const char* requests[] = {
        "GET /hello HTTP/1.1\r\nHost: example\r\n\r\n",
        ("POST /items HTTP/1.1\r\nHost: example\r\n"
         "Content-Type: application/json\r\nContent-Length: 11\r\n\r\n"
         "{\"ok\":true}"),
        "PUT /items/1 HTTP/1.1\r\nHost: example\r\nContent-Type: text/plain\r\n"
        "Content-Length: 5\r\n\r\nhello",
        "PATCH /items/1 HTTP/1.1\r\nHost: example\r\nContent-Type: text/plain\r\n"
        "Content-Length: 0\r\n\r\n",
        "DELETE /items/1 HTTP/1.1\r\nHost: example\r\n\r\n"};
    enum
    {
        ITERATIONS = 64
    };
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    size_t index = 0U;
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 100;
    }

    for (index = 0U; index < ITERATIONS; index += 1U) {
        SlHttpRequestHead request = {0};
        const char* input = requests[index % (sizeof(requests) / sizeof(requests[0]))];

        sl_arena_reset(&arena);
        status = parse_request(&arena, input, NULL, &request, NULL);
        if (expect_status(status, SL_STATUS_OK) != 0 || request.path.ptr == NULL ||
            sl_arena_used(&arena) == 0U || sl_arena_used(&arena) > sizeof(storage))
        {
            return 101;
        }
    }

    return 0;
}

static int test_stress_repeated_malformed_requests_fail_deterministically(void)
{
    static const char* requests[] = {
        "GET / HTTP/1.1", "GET /\r\n\r\n", "BAD METHOD / HTTP/1.1\r\nHost: example.test\r\n\r\n",
        "GET http://example.test/ HTTP/1.1\r\nHost: example.test\r\n\r\n"};
    enum
    {
        ITERATIONS = 64
    };
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    size_t index = 0U;
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 110;
    }

    for (index = 0U; index < ITERATIONS; index += 1U) {
        SlHttpRequestHead request = {0};
        SlDiag diag = {0};
        const char* input = requests[index % (sizeof(requests) / sizeof(requests[0]))];

        sl_arena_reset(&arena);
        status = parse_request(&arena, input, NULL, &request, &diag);
        if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
            diag.code != SL_DIAG_INVALID_HTTP_REQUEST || request.raw_target.ptr != NULL)
        {
            return 111;
        }
    }

    return 0;
}

static int test_stress_repeated_parser_limits_remain_enforced(void)
{
    enum
    {
        ITERATIONS = 32
    };
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlHttpParseOptions options = {0};
    size_t index = 0U;
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 120;
    }

    options.max_headers = 1U;
    options.max_target_length = 4U;
    options.max_header_name_length = 3U;
    options.max_header_value_length = 4U;
    options.max_total_header_bytes = 8U;
    options.max_body_length = 4U;

    for (index = 0U; index < ITERATIONS; index += 1U) {
        SlHttpRequestHead request = {0};
        SlHttpParseOptions body_options = options;
        SlDiag diag = {0};

        sl_arena_reset(&arena);
        status = parse_request(&arena, "GET /toolong HTTP/1.1\r\nHost: example.test\r\n\r\n",
                               &options, &request, &diag);
        if (expect_status(status, SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
            diag.code != SL_DIAG_HTTP_TARGET_LIMIT || request.raw_target.ptr != NULL)
        {
            return 121;
        }

        sl_arena_reset(&arena);
        request = (SlHttpRequestHead){0};
        diag = (SlDiag){0};
        status = parse_request(&arena, "GET / HTTP/1.1\r\nA: 1\r\nB: 2\r\n\r\n", &options, &request,
                               &diag);
        if (expect_status(status, SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
            diag.code != SL_DIAG_HTTP_HEADER_LIMIT || request.header_count != 0U)
        {
            return 122;
        }

        sl_arena_reset(&arena);
        request = (SlHttpRequestHead){0};
        diag = (SlDiag){0};
        body_options.max_header_name_length = SL_HTTP_DEFAULT_MAX_HEADER_NAME_LENGTH;
        body_options.max_header_value_length = SL_HTTP_DEFAULT_MAX_HEADER_VALUE_LENGTH;
        body_options.max_total_header_bytes = SL_HTTP_DEFAULT_MAX_TOTAL_HEADER_BYTES;
        body_options.max_headers = SL_HTTP_DEFAULT_MAX_HEADERS;
        status = parse_request(&arena,
                               "POST / HTTP/1.1\r\nHost: example.test\r\n"
                               "Content-Length: 5\r\n\r\n12345",
                               &body_options, &request, &diag);
        if (expect_status(status, SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
            diag.code != SL_DIAG_HTTP_BODY_LIMIT || request.body.ptr != NULL)
        {
            return 123;
        }
    }

    return 0;
}

typedef int (*HttpTestFn)(void);

typedef struct HttpTestCase
{
    HttpTestFn fn;
} HttpTestCase;

int main(void)
{
    static const HttpTestCase tests[] = {
        {test_parse_valid_targets},
        {test_parse_headers},
        {test_parse_body_bytes},
        {test_parse_non_nul_terminated_request_storage},
        {test_parse_binary_body_bytes},
        {test_failed_parse_rolls_back_transient_builder_memory},
        {test_supported_method_mapping},
        {test_rejects_invalid_requests},
        {test_rejects_unsupported_method},
        {test_rejects_non_path_targets},
        {test_callback_allocation_failure_preserved},
        {test_max_target_length_enforced},
        {test_max_headers_enforced},
        {test_header_name_value_and_total_limits_enforced},
        {test_max_body_length_enforced},
        {test_zero_header_limit_allows_no_headers},
        {test_request_line_host_and_singleton_policy},
        {test_invalid_arguments},
        {test_parsed_path_can_feed_route_matcher},
        {test_stress_repeated_valid_requests_remain_bounded},
        {test_stress_repeated_malformed_requests_fail_deterministically},
        {test_stress_repeated_parser_limits_remain_enforced}};
    size_t index = 0U;

    for (index = 0U; index < sizeof(tests) / sizeof(tests[0]); index += 1U) {
        int result = tests[index].fn();
        if (result != 0) {
            return result;
        }
    }

    return 0;
}

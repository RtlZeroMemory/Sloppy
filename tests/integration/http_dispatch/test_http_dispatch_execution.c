#include "sloppy/http_dispatch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define TEST_ARENA_SIZE 32768U
#define TEST_FILE_SIZE 524288U

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
    size_t expected_length = strlen(expected);

    return expect_true(
        actual.length == expected_length &&
        (expected_length == 0U ||
         (actual.ptr != NULL && memcmp(actual.ptr, expected, expected_length) == 0)));
}

static int read_file(const char* path, unsigned char* buffer, size_t capacity, SlBytes* out)
{
    FILE* file = NULL;
    long size = 0L;
    size_t bytes_read = 0U;

    if (path == NULL || buffer == NULL || out == NULL) {
        return 1;
    }

#ifdef _MSC_VER
    if (fopen_s(&file, path, "rb") != 0) {
        return 2;
    }
#else
    file = fopen(path, "rb");
#endif

    if (file == NULL) {
        return 3;
    }

    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        return 4;
    }

    size = ftell(file);
    if (size < 0L || (size_t)size > capacity) {
        fclose(file);
        return 5;
    }

    if (fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return 6;
    }

    bytes_read = fread(buffer, 1U, (size_t)size, file);
    if (bytes_read != (size_t)size) {
        fclose(file);
        return 7;
    }

    if (fclose(file) != 0) {
        return 8;
    }

    *out = sl_bytes_from_parts(buffer, bytes_read);
    return 0;
}

static int init_arena(SlArena* arena, unsigned char* storage, size_t storage_size)
{
    return expect_status(sl_arena_init(arena, storage, storage_size), SL_STATUS_OK);
}

static SlEngineOptions v8_options(void)
{
    SlEngineOptions options = {0};

    options.kind = SL_ENGINE_KIND_V8;
    options.runtime_name = sl_str_from_cstr("sloppy-http-dispatch-test");
    options.runtime_version = sl_str_from_cstr("0.3.0-test");
    options.target_platform = sl_str_from_cstr(SL_PLAN_TARGET_PLATFORM_WINDOWS_X64);
    options.target_engine = sl_str_from_cstr(SL_PLAN_TARGET_ENGINE_V8);
    return options;
}

static int load_plan(SlArena* plan_arena, SlPlan* out_plan, SlDiag* out_diag)
{
    unsigned char json_storage[TEST_FILE_SIZE];
    SlBytes json = {0};
    SlPlanParseOptions options = {0};

    if (read_file("tests/integration/http_dispatch/fixtures/app.plan.json", json_storage,
                  sizeof(json_storage), &json) != 0)
    {
        return 1;
    }

    options.source_name =
        sl_str_from_cstr("tests/integration/http_dispatch/fixtures/app.plan.json");
    return expect_status(sl_plan_parse_json(plan_arena, json, &options, out_plan, out_diag),
                         SL_STATUS_OK);
}

static int eval_app(SlEngine* engine, SlDiag* out_diag)
{
    unsigned char js_storage[TEST_FILE_SIZE];
    SlBytes js = {0};
    SlStr source = {0};

    if (read_file("stdlib/sloppy/internal/runtime-classic.js", js_storage, sizeof(js_storage),
                  &js) != 0)
    {
        return 1;
    }

    source = sl_str_from_parts((const char*)js.ptr, js.length);
    if (expect_status(sl_engine_eval_source(
                          engine, sl_str_from_cstr("stdlib/sloppy/internal/runtime-classic.js"),
                          source, out_diag),
                      SL_STATUS_OK) != 0)
    {
        return 1;
    }

    if (read_file("tests/integration/http_dispatch/fixtures/app.js", js_storage, sizeof(js_storage),
                  &js) != 0)
    {
        return 1;
    }

    source = sl_str_from_parts((const char*)js.ptr, js.length);
    return expect_status(
        sl_engine_eval_source(engine, sl_str_from_cstr("http_dispatch/app.js"), source, out_diag),
        SL_STATUS_OK);
}

static int parse_request(SlArena* arena, const char* text, SlHttpRequestHead* out)
{
    return expect_status(sl_http_parse_request_head(arena, bytes_from_cstr(text), NULL, out, NULL),
                         SL_STATUS_OK);
}

static int parse_pattern(SlArena* arena, const char* text, SlRoutePattern* out)
{
    return expect_status(sl_route_pattern_parse(arena, sl_str_from_cstr(text), out, NULL),
                         SL_STATUS_OK);
}

static int create_v8_engine(SlArena* engine_arena, SlEngine** out_engine)
{
    SlEngineOptions options = v8_options();
    return expect_status(sl_engine_create(&options, engine_arena, out_engine), SL_STATUS_OK);
}

static int dispatch_single_route_expect(
    const char* request_text, SlHttpMethod route_method, const char* pattern_text,
    SlHandlerId handler_id, SlStatusCode expected_status, SlEngineResultKind expected_result_kind,
    const char* expected_text, const char* expected_response_body, SlDiagCode expected_diag_code)
{
    unsigned char engine_storage[TEST_ARENA_SIZE];
    unsigned char plan_storage[TEST_ARENA_SIZE];
    unsigned char request_storage[TEST_ARENA_SIZE];
    unsigned char dispatch_storage[TEST_ARENA_SIZE];
    SlArena engine_arena = {0};
    SlArena plan_arena = {0};
    SlArena request_arena = {0};
    SlArena dispatch_arena = {0};
    SlEngine* engine = NULL;
    SlPlan plan = {0};
    SlHttpRequestHead request = {0};
    SlRoutePattern pattern = {0};
    SlHttpRouteBinding route = {0};
    SlHttpDispatchTable table = {0};
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlStatus status;

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&plan_arena, plan_storage, sizeof(plan_storage)) != 0 ||
        init_arena(&request_arena, request_storage, sizeof(request_storage)) != 0 ||
        init_arena(&dispatch_arena, dispatch_storage, sizeof(dispatch_storage)) != 0 ||
        load_plan(&plan_arena, &plan, &diag) != 0 ||
        create_v8_engine(&engine_arena, &engine) != 0 || eval_app(engine, &diag) != 0 ||
        parse_request(&request_arena, request_text, &request) != 0 ||
        parse_pattern(&dispatch_arena, pattern_text, &pattern) != 0)
    {
        sl_engine_destroy(engine);
        return 1;
    }

    route.method = route_method;
    route.pattern = &pattern;
    route.handler_id = handler_id;
    table.routes = &route;
    table.route_count = 1U;

    status = sl_http_dispatch_request_head(&dispatch_arena, engine, &plan, &table, &request,
                                           &result, &diag);
    if (sl_status_code(status) != expected_status) {
        fprintf(stderr, "unexpected dispatch status: expected %d got %d diag=%d message='%.*s'\n",
                (int)expected_status, (int)sl_status_code(status), (int)diag.code,
                (int)diag.message.length, diag.message.ptr == NULL ? "" : diag.message.ptr);
        sl_engine_destroy(engine);
        return 2;
    }

    if (result.kind != expected_result_kind) {
        fprintf(stderr, "unexpected result kind: expected %d got %d diag=%d\n",
                (int)expected_result_kind, (int)result.kind, (int)diag.code);
        sl_engine_destroy(engine);
        return 3;
    }

    if (expected_text != NULL && !sl_str_equal(result.text, sl_str_from_cstr(expected_text))) {
        sl_engine_destroy(engine);
        return 4;
    }

    if (expected_response_body != NULL && result.payload_kind != SL_ENGINE_RESULT_PAYLOAD_RESPONSE)
    {
        sl_engine_destroy(engine);
        return 4;
    }

    if (expected_response_body != NULL &&
        expect_bytes_equal(result.response.body, expected_response_body) != 0)
    {
        fprintf(stderr, "unexpected response body: expected '%s' got '%.*s'\n",
                expected_response_body, (int)result.response.body.length,
                result.response.body.ptr == NULL ? "" : (const char*)result.response.body.ptr);
        sl_engine_destroy(engine);
        return 5;
    }

    if (expected_diag_code == SL_DIAG_NONE) {
        if (diag.code != SL_DIAG_NONE) {
            sl_engine_destroy(engine);
            return 7;
        }
    }
    else if (diag.code != expected_diag_code) {
        sl_engine_destroy(engine);
        return 6;
    }

    sl_engine_destroy(engine);
    return 0;
}

static int dispatch_single_route_result(const char* request_text, SlHttpMethod route_method,
                                        const char* pattern_text, SlHandlerId handler_id,
                                        SlEngineResult* out_result, SlDiag* out_diag)
{
    static unsigned char engine_storage[TEST_ARENA_SIZE];
    static unsigned char plan_storage[TEST_ARENA_SIZE];
    static unsigned char request_storage[TEST_ARENA_SIZE];
    static unsigned char dispatch_storage[TEST_ARENA_SIZE];
    SlArena engine_arena = {0};
    SlArena plan_arena = {0};
    SlArena request_arena = {0};
    SlArena dispatch_arena = {0};
    SlEngine* engine = NULL;
    SlPlan plan = {0};
    SlHttpRequestHead request = {0};
    SlRoutePattern pattern = {0};
    SlHttpRouteBinding route = {0};
    SlHttpDispatchTable table = {0};
    SlStatus status;

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&plan_arena, plan_storage, sizeof(plan_storage)) != 0 ||
        init_arena(&request_arena, request_storage, sizeof(request_storage)) != 0 ||
        init_arena(&dispatch_arena, dispatch_storage, sizeof(dispatch_storage)) != 0 ||
        load_plan(&plan_arena, &plan, out_diag) != 0 ||
        create_v8_engine(&engine_arena, &engine) != 0 || eval_app(engine, out_diag) != 0 ||
        parse_request(&request_arena, request_text, &request) != 0 ||
        parse_pattern(&dispatch_arena, pattern_text, &pattern) != 0)
    {
        sl_engine_destroy(engine);
        return 1;
    }

    route.method = route_method;
    route.pattern = &pattern;
    route.handler_id = handler_id;
    table.routes = &route;
    table.route_count = 1U;

    status = sl_http_dispatch_request_head(&dispatch_arena, engine, &plan, &table, &request,
                                           out_result, out_diag);
    sl_engine_destroy(engine);
    return expect_status(status, SL_STATUS_OK);
}

static int test_get_hello_dispatches_to_handler_id(void)
{
    if (dispatch_single_route_expect("GET /hello HTTP/1.1\r\nHost: example\r\n\r\n",
                                     SL_HTTP_METHOD_GET, "/hello", 1U, SL_STATUS_OK,
                                     SL_ENGINE_RESULT_TEXT, NULL, "sloppy-ok", SL_DIAG_NONE) != 0)
    {
        return 1;
    }

    return 0;
}

static int test_missing_js_function_fails_through_engine_path(void)
{
    if (dispatch_single_route_expect("GET /missing-js HTTP/1.1\r\nHost: example\r\n\r\n",
                                     SL_HTTP_METHOD_GET, "/missing-js", 2U, SL_STATUS_INVALID_STATE,
                                     SL_ENGINE_RESULT_NONE, NULL, NULL,
                                     SL_DIAG_ENGINE_CALL_ERROR) != 0)
    {
        return 10;
    }

    return 0;
}

static int test_throwing_js_handler_fails_through_engine_path(void)
{
    if (dispatch_single_route_expect("GET /throw HTTP/1.1\r\nHost: example\r\n\r\n",
                                     SL_HTTP_METHOD_GET, "/throw", 3U, SL_STATUS_INVALID_STATE,
                                     SL_ENGINE_RESULT_NONE, NULL, NULL,
                                     SL_DIAG_ENGINE_EXCEPTION) != 0)
    {
        return 20;
    }

    return 0;
}

static int test_non_get_methods_dispatch_to_handler_id(void)
{
    static const struct
    {
        SlHttpMethod method;
        const char* request;
        const char* response_body;
    } cases[] = {
        {SL_HTTP_METHOD_POST, "POST /method HTTP/1.1\r\nHost: example\r\n\r\n",
         "{\"method\":\"POST\"}"},
        {SL_HTTP_METHOD_PUT, "PUT /method HTTP/1.1\r\nHost: example\r\n\r\n",
         "{\"method\":\"PUT\"}"},
        {SL_HTTP_METHOD_PATCH, "PATCH /method HTTP/1.1\r\nHost: example\r\n\r\n",
         "{\"method\":\"PATCH\"}"},
        {SL_HTTP_METHOD_DELETE, "DELETE /method HTTP/1.1\r\nHost: example\r\n\r\n",
         "{\"method\":\"DELETE\"}"},
    };
    size_t index = 0U;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index += 1U) {
        if (dispatch_single_route_expect(cases[index].request, cases[index].method, "/method", 4U,
                                         SL_STATUS_OK, SL_ENGINE_RESULT_JSON, NULL,
                                         cases[index].response_body, SL_DIAG_NONE) != 0)
        {
            return 30 + (int)index;
        }
    }

    return 0;
}

static int test_headers_and_json_body_reach_v8_handler(void)
{
    if (dispatch_single_route_expect(
            "POST /body HTTP/1.1\r\nHost: example\r\nContent-Type: application/json; "
            "charset=utf-8\r\nX-Trace: one\r\nx-trace: two\r\nContent-Length: 8\r\n\r\n{\"id\":7}",
            SL_HTTP_METHOD_POST, "/body", 5U, SL_STATUS_OK, SL_ENGINE_RESULT_JSON, NULL,
            "{\"contentType\":\"application/json; charset=utf-8\",\"trace\":\"one, "
            "two\",\"text\":\"{\\\"id\\\":7}\",\"body\":{\"id\":7}}",
            SL_DIAG_NONE) != 0)
    {
        return 40;
    }

    return 0;
}

static int test_context_metadata_and_bytes_body_reach_v8_handler(void)
{
    if (dispatch_single_route_expect(
            "POST /bytes?x=1 HTTP/1.1\r\nHost: example\r\n"
            "Content-Type: application/octet-stream\r\nContent-Length: 3\r\n\r\nABC",
            SL_HTTP_METHOD_POST, "/bytes", 6U, SL_STATUS_OK, SL_ENGINE_RESULT_JSON, NULL,
            "{\"id\":\"0\",\"scheme\":\"http\",\"protocol\":\"HTTP/1.1\",\"queryString\":\"x=1\","
            "\"contentType\":\"application/octet-stream\",\"contentLength\":3,"
            "\"connectionId\":\"0\",\"connection\":\"http:http:false\",\"bodyKind\":\"bytes\","
            "\"consumed\":true,\"bytes\":[65,66,67],\"secondReadRejected\":true,"
            "\"secondReadError\":{\"name\":\"TypeError\",\"message\":\"Request body is already "
            "consumed.\"}}",
            SL_DIAG_NONE) != 0)
    {
        return 50;
    }

    return 0;
}

static int test_problem_details_wrapped_sync_error_returns_safe_problem(void)
{
    if (dispatch_single_route_expect("GET /problem-sync HTTP/1.1\r\nHost: example\r\n\r\n",
                                     SL_HTTP_METHOD_GET, "/problem-sync", 7U, SL_STATUS_OK,
                                     SL_ENGINE_RESULT_ERROR, NULL,
                                     "{\"status\":500,\"title\":\"Internal Server "
                                     "Error\",\"code\":\"SLOPPY_E_HANDLER_ERROR\"}",
                                     SL_DIAG_NONE) != 0)
    {
        return 55;
    }

    return 0;
}

static int test_problem_details_wrapped_async_error_returns_safe_problem(void)
{
    if (dispatch_single_route_expect("GET /problem-async HTTP/1.1\r\nHost: example\r\n\r\n",
                                     SL_HTTP_METHOD_GET, "/problem-async", 8U, SL_STATUS_OK,
                                     SL_ENGINE_RESULT_ERROR, NULL,
                                     "{\"status\":500,\"title\":\"Internal Server "
                                     "Error\",\"code\":\"SLOPPY_E_HANDLER_ERROR\"}",
                                     SL_DIAG_NONE) != 0)
    {
        return 56;
    }

    return 0;
}

static int test_request_cookies_reach_v8_handler(void)
{
    if (dispatch_single_route_expect(
            "GET /cookies HTTP/1.1\r\nHost: example\r\n"
            "Cookie: session=abc%20123; theme=dark\r\n\r\n",
            SL_HTTP_METHOD_GET, "/cookies", 9U, SL_STATUS_OK, SL_ENGINE_RESULT_JSON, NULL,
            "{\"session\":\"abc 123\",\"theme\":\"dark\",\"missing\":null}", SL_DIAG_NONE) != 0)
    {
        return 57;
    }
    return 0;
}

static int test_response_cookies_append_set_cookie_headers(void)
{
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (dispatch_single_route_result("GET /response-cookies HTTP/1.1\r\nHost: example\r\n\r\n",
                                     SL_HTTP_METHOD_GET, "/response-cookies", 10U, &result,
                                     &diag) != 0)
    {
        return 58;
    }
    if (result.kind != SL_ENGINE_RESULT_JSON || result.response.header_count != 2U ||
        !sl_str_equal_ci_ascii(result.response.headers[0].name, sl_str_from_cstr("Set-Cookie")) ||
        !sl_str_equal(result.response.headers[0].value,
                      sl_str_from_cstr("session=abc; Path=/; SameSite=Strict; HttpOnly; Secure")) ||
        !sl_str_equal_ci_ascii(result.response.headers[1].name, sl_str_from_cstr("Set-Cookie")) ||
        !sl_str_equal(result.response.headers[1].value, sl_str_from_cstr("theme=dark")))
    {
        return 59;
    }
    return 0;
}

static int test_form_urlencoded_body_reaches_v8_handler(void)
{
    if (dispatch_single_route_expect(
            "POST /form HTTP/1.1\r\nHost: example\r\n"
            "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: 43\r\n\r\n"
            "name=Ada+Lovelace&repeated=one&repeated=two",
            SL_HTTP_METHOD_POST, "/form", 11U, SL_STATUS_OK, SL_ENGINE_RESULT_JSON, NULL,
            "{\"name\":\"Ada Lovelace\",\"repeated\":\"two\",\"entries\":[[\"name\",\"Ada "
            "Lovelace\"],[\"repeated\",\"one\"],[\"repeated\",\"two\"]]}",
            SL_DIAG_NONE) != 0)
    {
        return 62;
    }
    return 0;
}

static int test_empty_form_urlencoded_body_has_no_fields(void)
{
    if (dispatch_single_route_expect(
            "POST /form HTTP/1.1\r\nHost: example\r\n"
            "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: 0\r\n\r\n",
            SL_HTTP_METHOD_POST, "/form", 11U, SL_STATUS_OK, SL_ENGINE_RESULT_JSON, NULL,
            "{\"name\":null,\"repeated\":null,\"entries\":[]}", SL_DIAG_NONE) != 0)
    {
        return 66;
    }
    return 0;
}

static int test_multipart_body_file_reaches_v8_handler(void)
{
    if (dispatch_single_route_expect(
            "POST /multipart HTTP/1.1\r\nHost: example\r\n"
            "Content-Type: multipart/form-data; boundary=BOUNDARY\r\n"
            "Content-Length: 194\r\n\r\n"
            "--BOUNDARY\r\ncontent-disposition: form-data; name=\"title\"\r\n\r\navatar\r\n"
            "--BOUNDARY\r\ncontent-disposition: form-data; name=\"avatar\"; "
            "filename=\"ada.txt\"\r\ncontent-type: text/plain\r\n\r\nAda\r\n--BOUNDARY--\r\n",
            SL_HTTP_METHOD_POST, "/multipart", 12U, SL_STATUS_OK, SL_ENGINE_RESULT_JSON, NULL,
            "{\"title\":\"avatar\",\"file\":{\"fieldName\":\"avatar\",\"name\":\"ada.txt\","
            "\"contentType\":\"text/plain\",\"size\":3,\"text\":\"Ada\",\"bytes\":[65,100,97]}}",
            SL_DIAG_NONE) != 0)
    {
        return 63;
    }
    return 0;
}

static int test_stream_response_concatenates_chunks(void)
{
    SlEngineResult result = {0};
    SlDiag diag = {0};

    if (dispatch_single_route_result("GET /stream HTTP/1.1\r\nHost: example\r\n\r\n",
                                     SL_HTTP_METHOD_GET, "/stream", 13U, &result, &diag) != 0)
    {
        return 64;
    }
    if (result.kind != SL_ENGINE_RESULT_BYTES || result.response.kind != SL_HTTP_RESPONSE_STREAM ||
        !sl_str_equal(result.response.content_type,
                      sl_str_from_cstr("text/plain; charset=utf-8")) ||
        result.response.stream_chunk_count != 2U || result.response.stream_chunks == NULL ||
        expect_bytes_equal(result.response.stream_chunks[0].bytes, "hello ") != 0 ||
        expect_bytes_equal(result.response.stream_chunks[1].bytes, "world") != 0)
    {
        return 65;
    }
    return 0;
}

static int test_lifecycle_context_metadata_reaches_v8_handler(void)
{
    unsigned char engine_storage[TEST_ARENA_SIZE];
    unsigned char plan_storage[TEST_ARENA_SIZE];
    unsigned char request_storage[TEST_ARENA_SIZE];
    unsigned char dispatch_storage[TEST_ARENA_SIZE];
    SlArena engine_arena = {0};
    SlArena plan_arena = {0};
    SlArena request_arena = {0};
    SlArena dispatch_arena = {0};
    SlEngine* engine = NULL;
    SlPlan plan = {0};
    SlHttpBackend backend = {0};
    SlHttpConnection connection = {0};
    SlHttpRequestLifecycle request = {0};
    SlRoutePattern pattern = {0};
    SlHttpRouteBinding route = {0};
    SlHttpDispatchTable table = {0};
    SlEngineResult result = {0};
    SlDiag diag = {0};
    SlStatus status;

    if (init_arena(&engine_arena, engine_storage, sizeof(engine_storage)) != 0 ||
        init_arena(&plan_arena, plan_storage, sizeof(plan_storage)) != 0 ||
        init_arena(&request_arena, request_storage, sizeof(request_storage)) != 0 ||
        init_arena(&dispatch_arena, dispatch_storage, sizeof(dispatch_storage)) != 0 ||
        load_plan(&plan_arena, &plan, &diag) != 0 ||
        create_v8_engine(&engine_arena, &engine) != 0 || eval_app(engine, &diag) != 0 ||
        expect_status(sl_http_backend_init(&backend, NULL, &diag), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_backend_start(&backend, NULL, &diag), SL_STATUS_OK) != 0 ||
        expect_status(sl_http_backend_accept_connection(&backend, &connection, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_begin(&connection, &request_arena, &request, &diag),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_http_request_parse_head(
                          &request,
                          bytes_from_cstr("POST /bytes?x=1 HTTP/1.1\r\nHost: example\r\n"
                                          "Content-Type: application/octet-stream\r\n"
                                          "Content-Length: 3\r\n\r\nABC"),
                          &diag),
                      SL_STATUS_OK) != 0 ||
        parse_pattern(&dispatch_arena, "/bytes", &pattern) != 0)
    {
        sl_engine_destroy(engine);
        return 60;
    }

    route.method = SL_HTTP_METHOD_POST;
    route.pattern = &pattern;
    route.handler_id = 6U;
    table.routes = &route;
    table.route_count = 1U;

    status = sl_http_dispatch_request_lifecycle(&dispatch_arena, engine, &plan, &table, &request,
                                                &result, &diag);
    if (sl_status_code(status) != SL_STATUS_OK || result.kind != SL_ENGINE_RESULT_JSON ||
        expect_bytes_equal(
            result.response.body,
            "{\"id\":\"1\",\"scheme\":\"http\",\"protocol\":\"HTTP/1.1\","
            "\"queryString\":\"x=1\",\"contentType\":\"application/octet-stream\","
            "\"contentLength\":3,\"connectionId\":\"1\","
            "\"connection\":\"http:http:false\",\"bodyKind\":\"bytes\","
            "\"consumed\":true,\"bytes\":[65,66,67],"
            "\"secondReadRejected\":true,\"secondReadError\":{\"name\":\"TypeError\","
            "\"message\":\"Request body is already consumed.\"}}") != 0)
    {
        sl_engine_destroy(engine);
        return 61;
    }

    sl_http_request_close(&request, &diag);
    sl_http_connection_close(&connection, &diag);
    sl_http_backend_stop(&backend, &diag);
    sl_http_backend_dispose(&backend, &diag);
    sl_engine_destroy(engine);
    return 0;
}

int main(void)
{
    int result = test_get_hello_dispatches_to_handler_id();
    if (result != 0) {
        return result;
    }

    result = test_missing_js_function_fails_through_engine_path();
    if (result != 0) {
        return result;
    }

    result = test_throwing_js_handler_fails_through_engine_path();
    if (result != 0) {
        return result;
    }

    result = test_non_get_methods_dispatch_to_handler_id();
    if (result != 0) {
        return result;
    }

    result = test_headers_and_json_body_reach_v8_handler();
    if (result != 0) {
        return result;
    }

    result = test_context_metadata_and_bytes_body_reach_v8_handler();
    if (result != 0) {
        return result;
    }

    result = test_problem_details_wrapped_sync_error_returns_safe_problem();
    if (result != 0) {
        return result;
    }

    result = test_problem_details_wrapped_async_error_returns_safe_problem();
    if (result != 0) {
        return result;
    }

    result = test_request_cookies_reach_v8_handler();
    if (result != 0) {
        return result;
    }

    result = test_response_cookies_append_set_cookie_headers();
    if (result != 0) {
        return result;
    }

    result = test_form_urlencoded_body_reaches_v8_handler();
    if (result != 0) {
        return result;
    }

    result = test_empty_form_urlencoded_body_has_no_fields();
    if (result != 0) {
        return result;
    }

    result = test_multipart_body_file_reaches_v8_handler();
    if (result != 0) {
        return result;
    }

    result = test_stream_response_concatenates_chunks();
    if (result != 0) {
        return result;
    }

    return test_lifecycle_context_metadata_reaches_v8_handler();
}

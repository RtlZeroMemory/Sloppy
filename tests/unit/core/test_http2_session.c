#include "sloppy/http2_session.h"

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

static int expect_header(const SlHttp2HeaderList* headers, size_t index, const char* name,
                         const char* value)
{
    if (headers == NULL || index >= headers->count) {
        return 1;
    }
    return expect_true(sl_str_equal(headers->fields[index].name, sl_str_from_cstr(name)) &&
                       sl_str_equal(headers->fields[index].value, sl_str_from_cstr(value)));
}

static int pump(SlHttp2Session* from, SlHttp2Session* to)
{
    SlBytes bytes = {0};
    size_t consumed = 0U;

    if (expect_status(sl_http2_session_drain_output(from, &bytes), SL_STATUS_OK) != 0) {
        return 1;
    }
    if (bytes.length == 0U) {
        return 0;
    }
    if (expect_status(sl_http2_session_receive(to, bytes, &consumed), SL_STATUS_OK) != 0 ||
        consumed != bytes.length)
    {
        return 2;
    }
    return 0;
}

static int init_pair(SlArena* client_arena, SlArena* server_arena, SlHttp2Session* client,
                     SlHttp2Session* server)
{
    SlHttp2SessionConfig client_config = {.role = SL_HTTP2_SESSION_ROLE_CLIENT};
    SlHttp2SessionConfig server_config = {.role = SL_HTTP2_SESSION_ROLE_SERVER};

    if (expect_status(sl_http2_session_init(client, client_arena, &client_config), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_http2_session_init(server, server_arena, &server_config), SL_STATUS_OK) !=
            0)
    {
        return 1;
    }
    if (pump(client, server) != 0 || pump(server, client) != 0 || pump(client, server) != 0) {
        return 2;
    }
    sl_http2_session_clear_events(client);
    sl_http2_session_clear_events(server);
    return 0;
}

static int test_client_server_request_response_round_trip(void)
{
    unsigned char client_storage[65536];
    unsigned char server_storage[65536];
    SlArena client_arena = {0};
    SlArena server_arena = {0};
    SlHttp2Session client = {0};
    SlHttp2Session server = {0};
    SlHttp2HeaderField request_fields[] = {
        h2_header(":method", "GET"), h2_header(":scheme", "https"),
        h2_header(":authority", "localhost"), h2_header(":path", "/hello")};
    SlHttp2HeaderField response_fields[] = {h2_header(":status", "200"),
                                            h2_header("content-type", "text/plain")};
    SlHttp2HeaderList request_headers = {
        .fields = request_fields, .count = sizeof(request_fields) / sizeof(request_fields[0])};
    SlHttp2HeaderList response_headers = {
        .fields = response_fields, .count = sizeof(response_fields) / sizeof(response_fields[0])};
    SlHttp2EventList events = {0};
    int32_t stream_id = 0;

    if (expect_status(sl_arena_init(&client_arena, client_storage, sizeof(client_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&server_arena, server_storage, sizeof(server_storage)),
                      SL_STATUS_OK) != 0 ||
        init_pair(&client_arena, &server_arena, &client, &server) != 0)
    {
        return 1;
    }

    if (expect_status(sl_http2_session_submit_request(&client, &request_headers, sl_bytes_empty(),
                                                      &stream_id),
                      SL_STATUS_OK) != 0 ||
        stream_id != 1 || pump(&client, &server) != 0)
    {
        sl_http2_session_dispose(&client);
        sl_http2_session_dispose(&server);
        return 2;
    }

    events = sl_http2_session_events(&server);
    if (events.count < 1U || events.events[0].type != SL_HTTP2_EVENT_REQUEST_HEADERS ||
        events.events[0].stream_id != 1 || !events.events[0].end_stream ||
        expect_header(&events.events[0].headers, 0U, ":method", "GET") != 0 ||
        expect_header(&events.events[0].headers, 3U, ":path", "/hello") != 0)
    {
        sl_http2_session_dispose(&client);
        sl_http2_session_dispose(&server);
        return 3;
    }

    if (expect_status(
            sl_http2_session_submit_response(&server, 1, &response_headers, bytes_from_cstr("ok")),
            SL_STATUS_OK) != 0 ||
        pump(&server, &client) != 0)
    {
        sl_http2_session_dispose(&client);
        sl_http2_session_dispose(&server);
        return 4;
    }

    events = sl_http2_session_events(&client);
    if (events.count < 3U || events.events[0].type != SL_HTTP2_EVENT_RESPONSE_HEADERS ||
        events.events[0].stream_id != 1 || events.events[1].type != SL_HTTP2_EVENT_DATA ||
        !sl_bytes_equal(events.events[1].data, bytes_from_cstr("ok")) ||
        expect_header(&events.events[0].headers, 0U, ":status", "200") != 0)
    {
        sl_http2_session_dispose(&client);
        sl_http2_session_dispose(&server);
        return 5;
    }

    sl_http2_session_dispose(&client);
    sl_http2_session_dispose(&server);
    return 0;
}

static int test_rst_stream_and_goaway_surface_as_events(void)
{
    unsigned char client_storage[65536];
    unsigned char server_storage[65536];
    SlArena client_arena = {0};
    SlArena server_arena = {0};
    SlHttp2Session client = {0};
    SlHttp2Session server = {0};
    SlHttp2HeaderField request_fields[] = {
        h2_header(":method", "GET"), h2_header(":scheme", "https"),
        h2_header(":authority", "localhost"), h2_header(":path", "/cancel")};
    SlHttp2HeaderList request_headers = {
        .fields = request_fields, .count = sizeof(request_fields) / sizeof(request_fields[0])};
    SlHttp2EventList events = {0};
    int32_t stream_id = 0;

    if (expect_status(sl_arena_init(&client_arena, client_storage, sizeof(client_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&server_arena, server_storage, sizeof(server_storage)),
                      SL_STATUS_OK) != 0 ||
        init_pair(&client_arena, &server_arena, &client, &server) != 0)
    {
        return 1;
    }

    if (expect_status(sl_http2_session_submit_request(&client, &request_headers, sl_bytes_empty(),
                                                      &stream_id),
                      SL_STATUS_OK) != 0 ||
        pump(&client, &server) != 0 ||
        expect_status(sl_http2_session_submit_rst_stream(&server, stream_id, 8U), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_http2_session_submit_goaway(&server, stream_id, 0U), SL_STATUS_OK) != 0 ||
        pump(&server, &client) != 0)
    {
        sl_http2_session_dispose(&client);
        sl_http2_session_dispose(&server);
        return 2;
    }

    events = sl_http2_session_events(&client);
    if (events.count < 2U || events.events[0].type != SL_HTTP2_EVENT_RST_STREAM ||
        events.events[0].stream_id != stream_id || events.events[0].error_code != 8U ||
        events.events[1].type != SL_HTTP2_EVENT_STREAM_CLOSE ||
        events.events[1].stream_id != stream_id)
    {
        sl_http2_session_dispose(&client);
        sl_http2_session_dispose(&server);
        return 3;
    }

    sl_http2_session_dispose(&client);
    sl_http2_session_dispose(&server);
    return 0;
}

static int test_session_init_rejects_event_array_size_overflow(void)
{
    unsigned char storage[128];
    SlArena arena = {0};
    SlHttp2Session session = {0};
    SlHttp2SessionConfig config = {.role = SL_HTTP2_SESSION_ROLE_SERVER,
                                   .max_events = (SIZE_MAX / sizeof(SlHttp2Event)) + 1U};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0) {
        return 1;
    }

    return expect_status(sl_http2_session_init(&session, &arena, &config), SL_STATUS_OVERFLOW);
}

int main(void)
{
    int result = 0;

    result = test_client_server_request_response_round_trip();
    if (result != 0) {
        return result;
    }
    result = test_rst_stream_and_goaway_surface_as_events();
    if (result != 0) {
        return result;
    }
    result = test_session_init_rejects_event_array_size_overflow();
    if (result != 0) {
        return result;
    }

    return 0;
}

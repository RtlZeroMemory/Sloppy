#include "sloppy/http2_frame.h"

#include <stdbool.h>
#include <string.h>

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static int test_header_parse_masks_reserved_stream_bit(void)
{
    static const unsigned char bytes[] = {0x00, 0x00, 0x05, 0x01, 0x04, 0x80, 0x00,
                                          0x00, 0x03, 'h',  'e',  'l',  'l',  'o'};
    SlHttp2FrameHeader header = {0};

    if (expect_status(
            sl_http2_frame_header_parse(sl_bytes_from_parts(bytes, sizeof(bytes)), &header),
            SL_STATUS_OK) != 0)
    {
        return 1;
    }

    return expect_true(header.length == 5U && header.type == SL_HTTP2_FRAME_HEADERS &&
                       header.flags == SL_HTTP2_FLAG_END_HEADERS && header.stream_id == 3U);
}

static int test_frame_write_round_trips_header_and_payload(void)
{
    static const unsigned char payload[] = {'o', 'k'};
    unsigned char buffer[32];
    SlBytes written = {0};
    SlHttp2Frame frame = {.header = {.length = sizeof(payload),
                                     .type = SL_HTTP2_FRAME_DATA,
                                     .flags = SL_HTTP2_FLAG_END_STREAM,
                                     .stream_id = 1U},
                          .payload = sl_bytes_from_parts(payload, sizeof(payload))};
    static const unsigned char expected[] = {0x00, 0x00, 0x02, 0x00, 0x01, 0x00,
                                             0x00, 0x00, 0x01, 'o',  'k'};

    if (expect_status(sl_http2_frame_write(&frame, buffer, sizeof(buffer), &written),
                      SL_STATUS_OK) != 0)
    {
        return 1;
    }

    return expect_true(written.length == sizeof(expected) &&
                       memcmp(written.ptr, expected, sizeof(expected)) == 0);
}

static int test_data_frame_rejects_stream_zero_and_bad_padding(void)
{
    static const unsigned char payload[] = {4U, 'a'};
    SlHttp2Frame frame = {
        .header = {.length = 0U, .type = SL_HTTP2_FRAME_DATA, .flags = 0U, .stream_id = 0U},
        .payload = sl_bytes_empty()};

    if (expect_status(sl_http2_frame_validate(&frame, SL_HTTP2_DEFAULT_MAX_FRAME_SIZE),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 1;
    }

    frame.header.length = sizeof(payload);
    frame.header.flags = SL_HTTP2_FLAG_PADDED;
    frame.header.stream_id = 1U;
    frame.payload = sl_bytes_from_parts(payload, sizeof(payload));
    return expect_status(sl_http2_frame_validate(&frame, SL_HTTP2_DEFAULT_MAX_FRAME_SIZE),
                         SL_STATUS_INVALID_ARGUMENT);
}

static int test_headers_frame_validates_flags_priority_and_padding(void)
{
    static const unsigned char payload[] = {0U, 0x80, 0x00, 0x00, 0x05, 0x10, 'x'};
    static const unsigned char self_dependency[] = {0x00, 0x00, 0x00, 0x01, 0x10, 'x'};
    SlHttp2Frame frame = {.header = {.length = sizeof(payload),
                                     .type = SL_HTTP2_FRAME_HEADERS,
                                     .flags = SL_HTTP2_FLAG_PADDED | SL_HTTP2_FLAG_PRIORITY |
                                              SL_HTTP2_FLAG_END_HEADERS,
                                     .stream_id = 1U},
                          .payload = sl_bytes_from_parts(payload, sizeof(payload))};

    if (expect_status(sl_http2_frame_validate(&frame, SL_HTTP2_DEFAULT_MAX_FRAME_SIZE),
                      SL_STATUS_OK) != 0)
    {
        return 1;
    }

    frame.header.length = sizeof(self_dependency);
    frame.header.flags = SL_HTTP2_FLAG_PRIORITY | SL_HTTP2_FLAG_END_HEADERS;
    frame.payload = sl_bytes_from_parts(self_dependency, sizeof(self_dependency));
    if (expect_status(sl_http2_frame_validate(&frame, SL_HTTP2_DEFAULT_MAX_FRAME_SIZE),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 2;
    }

    frame.header.flags = 0x80U;
    return expect_status(sl_http2_frame_validate(&frame, SL_HTTP2_DEFAULT_MAX_FRAME_SIZE),
                         SL_STATUS_INVALID_ARGUMENT);
}

static int test_fixed_length_frames_validate_stream_and_payload_rules(void)
{
    unsigned char priority[] = {0x00, 0x00, 0x00, 0x03, 0x10};
    unsigned char rst[] = {0x00, 0x00, 0x00, 0x08};
    unsigned char ping[] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U};
    unsigned char window_update[] = {0x00, 0x00, 0x00, 0x01};
    SlHttp2Frame frame = {.header = {.length = sizeof(priority),
                                     .type = SL_HTTP2_FRAME_PRIORITY,
                                     .flags = 0U,
                                     .stream_id = 3U},
                          .payload = sl_bytes_from_parts(priority, sizeof(priority))};

    if (expect_status(sl_http2_frame_validate(&frame, SL_HTTP2_DEFAULT_MAX_FRAME_SIZE),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 1;
    }

    priority[3] = 0x05U;
    if (expect_status(sl_http2_frame_validate(&frame, SL_HTTP2_DEFAULT_MAX_FRAME_SIZE),
                      SL_STATUS_OK) != 0)
    {
        return 2;
    }

    frame.header.type = SL_HTTP2_FRAME_RST_STREAM;
    frame.header.length = sizeof(rst);
    frame.payload = sl_bytes_from_parts(rst, sizeof(rst));
    if (expect_status(sl_http2_frame_validate(&frame, SL_HTTP2_DEFAULT_MAX_FRAME_SIZE),
                      SL_STATUS_OK) != 0)
    {
        return 3;
    }

    frame.header.type = SL_HTTP2_FRAME_PING;
    frame.header.stream_id = 0U;
    frame.header.length = sizeof(ping);
    frame.payload = sl_bytes_from_parts(ping, sizeof(ping));
    if (expect_status(sl_http2_frame_validate(&frame, SL_HTTP2_DEFAULT_MAX_FRAME_SIZE),
                      SL_STATUS_OK) != 0)
    {
        return 4;
    }

    frame.header.type = SL_HTTP2_FRAME_WINDOW_UPDATE;
    frame.header.length = sizeof(window_update);
    frame.payload = sl_bytes_from_parts(window_update, sizeof(window_update));
    if (expect_status(sl_http2_frame_validate(&frame, SL_HTTP2_DEFAULT_MAX_FRAME_SIZE),
                      SL_STATUS_OK) != 0)
    {
        return 5;
    }

    window_update[3] = 0U;
    return expect_status(sl_http2_frame_validate(&frame, SL_HTTP2_DEFAULT_MAX_FRAME_SIZE),
                         SL_STATUS_INVALID_ARGUMENT);
}

static int test_settings_parse_validates_values_and_ack(void)
{
    static const unsigned char payload[] = {0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
                                            0x00, 0x05, 0x00, 0x00, 0x40, 0x00};
    static const unsigned char invalid_push[] = {0x00, 0x02, 0x00, 0x00, 0x00, 0x02};
    SlHttp2Frame frame = {.header = {.length = sizeof(payload),
                                     .type = SL_HTTP2_FRAME_SETTINGS,
                                     .flags = 0U,
                                     .stream_id = 0U},
                          .payload = sl_bytes_from_parts(payload, sizeof(payload))};
    SlHttp2SettingsFrame settings = {0};

    if (expect_status(sl_http2_settings_parse(&frame, &settings), SL_STATUS_OK) != 0 ||
        settings.setting_count != 2U || settings.settings[0].id != SL_HTTP2_SETTING_ENABLE_PUSH ||
        settings.settings[0].value != 0U ||
        settings.settings[1].id != SL_HTTP2_SETTING_MAX_FRAME_SIZE ||
        settings.settings[1].value != SL_HTTP2_DEFAULT_MAX_FRAME_SIZE)
    {
        return 1;
    }

    frame.header.flags = SL_HTTP2_FLAG_ACK;
    if (expect_status(sl_http2_settings_parse(&frame, &settings), SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 2;
    }

    frame.header.flags = 0U;
    frame.header.length = sizeof(invalid_push);
    frame.payload = sl_bytes_from_parts(invalid_push, sizeof(invalid_push));
    return expect_status(sl_http2_settings_parse(&frame, &settings), SL_STATUS_INVALID_ARGUMENT);
}

static int test_parse_requires_complete_payload_and_respects_max_frame_size(void)
{
    static const unsigned char bytes[] = {0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    SlHttp2Frame frame = {0};

    if (expect_status(sl_http2_frame_parse(sl_bytes_from_parts(bytes, sizeof(bytes)),
                                           SL_HTTP2_DEFAULT_MAX_FRAME_SIZE, &frame),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 1;
    }

    return expect_status(
        sl_http2_frame_parse(sl_bytes_from_parts(bytes, sizeof(bytes)), 1024U, &frame),
        SL_STATUS_INVALID_ARGUMENT);
}

static int test_unknown_extension_frame_is_ignored_after_generic_validation(void)
{
    static const unsigned char bytes[] = {0x00, 0x00, 0x03, 0xfe, 0xff, 0x00,
                                          0x00, 0x00, 0x00, 'x',  'y',  'z'};
    SlHttp2Frame frame = {0};

    if (expect_status(sl_http2_frame_parse(sl_bytes_from_parts(bytes, sizeof(bytes)),
                                           SL_HTTP2_DEFAULT_MAX_FRAME_SIZE, &frame),
                      SL_STATUS_OK) != 0)
    {
        return 1;
    }

    return expect_true(frame.header.type == 0xfeU && frame.payload.length == 3U);
}

int main(void)
{
    int result = 0;

    result = test_header_parse_masks_reserved_stream_bit();
    if (result != 0) {
        return result;
    }
    result = test_frame_write_round_trips_header_and_payload();
    if (result != 0) {
        return result;
    }
    result = test_data_frame_rejects_stream_zero_and_bad_padding();
    if (result != 0) {
        return result;
    }
    result = test_headers_frame_validates_flags_priority_and_padding();
    if (result != 0) {
        return result;
    }
    result = test_fixed_length_frames_validate_stream_and_payload_rules();
    if (result != 0) {
        return result;
    }
    result = test_settings_parse_validates_values_and_ack();
    if (result != 0) {
        return result;
    }
    result = test_parse_requires_complete_payload_and_respects_max_frame_size();
    if (result != 0) {
        return result;
    }
    result = test_unknown_extension_frame_is_ignored_after_generic_validation();
    if (result != 0) {
        return result;
    }

    return 0;
}

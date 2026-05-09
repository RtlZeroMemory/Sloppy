#include "sloppy/http2_frame.h"

static bool sl_http2_valid_bytes(SlBytes bytes)
{
    return bytes.ptr != NULL || bytes.length == 0U;
}

static bool sl_http2_valid_frame_size(uint32_t max_frame_size)
{
    return max_frame_size >= SL_HTTP2_MIN_MAX_FRAME_SIZE &&
           max_frame_size <= SL_HTTP2_MAX_MAX_FRAME_SIZE;
}

static uint32_t sl_http2_read_u24(const unsigned char* bytes)
{
    return ((uint32_t)bytes[0] << 16U) | ((uint32_t)bytes[1] << 8U) | (uint32_t)bytes[2];
}

static uint32_t sl_http2_read_u32(const unsigned char* bytes)
{
    return ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) | ((uint32_t)bytes[2] << 8U) |
           (uint32_t)bytes[3];
}

static void sl_http2_write_u24(unsigned char* bytes, uint32_t value)
{
    bytes[0] = (unsigned char)((value >> 16U) & 0xffU);
    bytes[1] = (unsigned char)((value >> 8U) & 0xffU);
    bytes[2] = (unsigned char)(value & 0xffU);
}

static void sl_http2_write_u32(unsigned char* bytes, uint32_t value)
{
    bytes[0] = (unsigned char)((value >> 24U) & 0xffU);
    bytes[1] = (unsigned char)((value >> 16U) & 0xffU);
    bytes[2] = (unsigned char)((value >> 8U) & 0xffU);
    bytes[3] = (unsigned char)(value & 0xffU);
}

static void sl_http2_write_bytes(unsigned char* dst, SlBytes src)
{
    size_t index = 0U;

    for (index = 0U; index < src.length; index += 1U) {
        dst[index] = src.ptr[index];
    }
}

static bool sl_http2_payload_has_padding(const SlHttp2Frame* frame, size_t bytes_after_pad_length)
{
    size_t pad_length = 0U;

    if ((frame->header.flags & SL_HTTP2_FLAG_PADDED) == 0U) {
        return true;
    }
    if (frame->payload.length < bytes_after_pad_length + 1U) {
        return false;
    }

    pad_length = (size_t)frame->payload.ptr[0];
    return pad_length <= frame->payload.length - bytes_after_pad_length - 1U;
}

static SlStatus sl_http2_validate_data(const SlHttp2Frame* frame)
{
    if (frame->header.stream_id == 0U || (frame->header.flags & ~(uint8_t)0x9U) != 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_http2_payload_has_padding(frame, 0U)
               ? sl_status_ok()
               : sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
}

static SlStatus sl_http2_validate_headers(const SlHttp2Frame* frame)
{
    uint8_t allowed = (uint8_t)(SL_HTTP2_FLAG_END_STREAM | SL_HTTP2_FLAG_END_HEADERS |
                                SL_HTTP2_FLAG_PADDED | SL_HTTP2_FLAG_PRIORITY);
    size_t minimum = 0U;
    size_t bytes_after_pad_length = 0U;
    size_t priority_offset = 0U;
    uint32_t dependency = 0U;

    if (frame->header.stream_id == 0U || (frame->header.flags & (uint8_t)~allowed) != 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if ((frame->header.flags & SL_HTTP2_FLAG_PADDED) != 0U) {
        minimum += 1U;
        priority_offset = 1U;
    }
    if ((frame->header.flags & SL_HTTP2_FLAG_PRIORITY) != 0U) {
        minimum += 5U;
        bytes_after_pad_length = 5U;
    }
    if (frame->payload.length < minimum) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (!sl_http2_payload_has_padding(frame, bytes_after_pad_length)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if ((frame->header.flags & SL_HTTP2_FLAG_PRIORITY) != 0U) {
        dependency = sl_http2_read_u32(frame->payload.ptr + priority_offset) & 0x7fffffffU;
        if (dependency == frame->header.stream_id) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
    }
    return sl_status_ok();
}

static SlStatus sl_http2_validate_priority(const SlHttp2Frame* frame)
{
    if (frame->header.stream_id == 0U || frame->payload.length != 5U || frame->header.flags != 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if ((sl_http2_read_u32(frame->payload.ptr) & 0x7fffffffU) == frame->header.stream_id) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    return sl_status_ok();
}

static SlStatus sl_http2_validate_rst_stream(const SlHttp2Frame* frame)
{
    if (frame->header.stream_id == 0U || frame->payload.length != 4U || frame->header.flags != 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    return sl_status_ok();
}

static SlStatus sl_http2_validate_settings(const SlHttp2Frame* frame)
{
    if (frame->header.stream_id != 0U || (frame->header.flags & (uint8_t)~SL_HTTP2_FLAG_ACK) != 0U)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if ((frame->header.flags & SL_HTTP2_FLAG_ACK) != 0U) {
        return frame->payload.length == 0U ? sl_status_ok()
                                           : sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if ((frame->payload.length % 6U) != 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    return sl_status_ok();
}

static SlStatus sl_http2_validate_push_promise(const SlHttp2Frame* frame)
{
    uint8_t allowed = (uint8_t)(SL_HTTP2_FLAG_END_HEADERS | SL_HTTP2_FLAG_PADDED);
    size_t minimum = 4U;
    uint32_t promised_stream_id = 0U;
    size_t stream_id_offset = 0U;

    if (frame->header.stream_id == 0U || (frame->header.flags & (uint8_t)~allowed) != 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if ((frame->header.flags & SL_HTTP2_FLAG_PADDED) != 0U) {
        minimum += 1U;
        stream_id_offset = 1U;
    }
    if (frame->payload.length < minimum || !sl_http2_payload_has_padding(frame, 4U)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    promised_stream_id = sl_http2_read_u32(frame->payload.ptr + stream_id_offset) & 0x7fffffffU;
    return promised_stream_id == 0U ? sl_status_from_code(SL_STATUS_INVALID_ARGUMENT)
                                    : sl_status_ok();
}

static SlStatus sl_http2_validate_ping(const SlHttp2Frame* frame)
{
    if (frame->header.stream_id != 0U || frame->payload.length != 8U ||
        (frame->header.flags & (uint8_t)~SL_HTTP2_FLAG_ACK) != 0U)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    return sl_status_ok();
}

static SlStatus sl_http2_validate_goaway(const SlHttp2Frame* frame)
{
    if (frame->header.stream_id != 0U || frame->payload.length < 8U || frame->header.flags != 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    return sl_status_ok();
}

static SlStatus sl_http2_validate_window_update(const SlHttp2Frame* frame)
{
    uint32_t increment = 0U;

    if (frame->payload.length != 4U || frame->header.flags != 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    increment = sl_http2_read_u32(frame->payload.ptr) & 0x7fffffffU;
    return increment == 0U ? sl_status_from_code(SL_STATUS_INVALID_ARGUMENT) : sl_status_ok();
}

static SlStatus sl_http2_validate_continuation(const SlHttp2Frame* frame)
{
    if (frame->header.stream_id == 0U ||
        (frame->header.flags & (uint8_t)~SL_HTTP2_FLAG_END_HEADERS) != 0U)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    return sl_status_ok();
}

SlStatus sl_http2_frame_header_parse(SlBytes bytes, SlHttp2FrameHeader* out_header)
{
    if (!sl_http2_valid_bytes(bytes) || out_header == NULL ||
        bytes.length < SL_HTTP2_FRAME_HEADER_BYTES)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    out_header->length = sl_http2_read_u24(bytes.ptr);
    out_header->type = bytes.ptr[3];
    out_header->flags = bytes.ptr[4];
    out_header->stream_id = sl_http2_read_u32(bytes.ptr + 5U) & 0x7fffffffU;
    return sl_status_ok();
}

SlStatus sl_http2_frame_header_write(const SlHttp2FrameHeader* header, unsigned char* buffer,
                                     size_t capacity, SlBytes* out_bytes)
{
    if (header == NULL || buffer == NULL || out_bytes == NULL ||
        capacity < SL_HTTP2_FRAME_HEADER_BYTES || header->length > SL_HTTP2_MAX_MAX_FRAME_SIZE ||
        (header->stream_id & 0x80000000U) != 0U)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    sl_http2_write_u24(buffer, header->length);
    buffer[3] = header->type;
    buffer[4] = header->flags;
    sl_http2_write_u32(buffer + 5U, header->stream_id);
    *out_bytes = sl_bytes_from_parts(buffer, SL_HTTP2_FRAME_HEADER_BYTES);
    return sl_status_ok();
}

SlStatus sl_http2_frame_parse(SlBytes bytes, uint32_t max_frame_size, SlHttp2Frame* out_frame)
{
    SlHttp2Frame frame = {0};
    SlStatus status = {0};

    if (!sl_http2_valid_bytes(bytes) || out_frame == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (!sl_http2_valid_frame_size(max_frame_size)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_http2_frame_header_parse(bytes, &frame.header);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (frame.header.length > max_frame_size ||
        bytes.length < SL_HTTP2_FRAME_HEADER_BYTES + (size_t)frame.header.length)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    frame.payload =
        sl_bytes_from_parts(bytes.ptr + SL_HTTP2_FRAME_HEADER_BYTES, (size_t)frame.header.length);
    status = sl_http2_frame_validate(&frame, max_frame_size);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    *out_frame = frame;
    return sl_status_ok();
}

SlStatus sl_http2_frame_write(const SlHttp2Frame* frame, unsigned char* buffer, size_t capacity,
                              SlBytes* out_bytes)
{
    SlBytes header = {0};
    SlStatus status = {0};

    if (frame == NULL || buffer == NULL || out_bytes == NULL ||
        !sl_http2_valid_bytes(frame->payload) || frame->header.length != frame->payload.length ||
        capacity < SL_HTTP2_FRAME_HEADER_BYTES + frame->payload.length)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_http2_frame_header_write(&frame->header, buffer, capacity, &header);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (frame->payload.length != 0U) {
        sl_http2_write_bytes(buffer + header.length, frame->payload);
    }
    *out_bytes = sl_bytes_from_parts(buffer, header.length + frame->payload.length);
    return sl_status_ok();
}

SlStatus sl_http2_frame_validate(const SlHttp2Frame* frame, uint32_t max_frame_size)
{
    if (frame == NULL || !sl_http2_valid_bytes(frame->payload) ||
        !sl_http2_valid_frame_size(max_frame_size) ||
        frame->header.length != frame->payload.length || frame->header.length > max_frame_size)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    switch (frame->header.type) {
    case SL_HTTP2_FRAME_DATA:
        return sl_http2_validate_data(frame);
    case SL_HTTP2_FRAME_HEADERS:
        return sl_http2_validate_headers(frame);
    case SL_HTTP2_FRAME_PRIORITY:
        return sl_http2_validate_priority(frame);
    case SL_HTTP2_FRAME_RST_STREAM:
        return sl_http2_validate_rst_stream(frame);
    case SL_HTTP2_FRAME_SETTINGS:
        return sl_http2_validate_settings(frame);
    case SL_HTTP2_FRAME_PUSH_PROMISE:
        return sl_http2_validate_push_promise(frame);
    case SL_HTTP2_FRAME_PING:
        return sl_http2_validate_ping(frame);
    case SL_HTTP2_FRAME_GOAWAY:
        return sl_http2_validate_goaway(frame);
    case SL_HTTP2_FRAME_WINDOW_UPDATE:
        return sl_http2_validate_window_update(frame);
    case SL_HTTP2_FRAME_CONTINUATION:
        return sl_http2_validate_continuation(frame);
    default:
        return sl_status_ok();
    }
}

SlStatus sl_http2_settings_parse(const SlHttp2Frame* frame, SlHttp2SettingsFrame* out_settings)
{
    SlStatus status = {0};
    size_t count = 0U;

    if (frame == NULL || out_settings == NULL || frame->header.type != SL_HTTP2_FRAME_SETTINGS) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_settings = (SlHttp2SettingsFrame){0};
    status = sl_http2_validate_settings(frame);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if ((frame->header.flags & SL_HTTP2_FLAG_ACK) != 0U) {
        return sl_status_ok();
    }

    count = frame->payload.length / 6U;
    if (count > SL_HTTP2_MAX_SETTINGS_PER_FRAME) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }

    for (size_t index = 0U; index < count; index += 1U) {
        const unsigned char* cursor = frame->payload.ptr + (index * 6U);
        uint16_t id = (uint16_t)(((uint16_t)cursor[0] << 8U) | (uint16_t)cursor[1]);
        uint32_t value = sl_http2_read_u32(cursor + 2U);

        switch (id) {
        case SL_HTTP2_SETTING_ENABLE_PUSH:
            if (value > 1U) {
                return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            }
            break;
        case SL_HTTP2_SETTING_INITIAL_WINDOW_SIZE:
            if (value > 0x7fffffffU) {
                return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            }
            break;
        case SL_HTTP2_SETTING_MAX_FRAME_SIZE:
            if (!sl_http2_valid_frame_size(value)) {
                return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            }
            break;
        default:
            break;
        }

        out_settings->settings[out_settings->setting_count].id = id;
        out_settings->settings[out_settings->setting_count].value = value;
        out_settings->setting_count += 1U;
    }

    return sl_status_ok();
}

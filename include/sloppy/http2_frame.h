#ifndef SLOPPY_HTTP2_FRAME_H
#define SLOPPY_HTTP2_FRAME_H

#include "sloppy/bytes.h"
#include "sloppy/status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SL_HTTP2_FRAME_HEADER_BYTES 9U
#define SL_HTTP2_DEFAULT_MAX_FRAME_SIZE 16384U
#define SL_HTTP2_MIN_MAX_FRAME_SIZE 16384U
#define SL_HTTP2_MAX_MAX_FRAME_SIZE 16777215U
#define SL_HTTP2_MAX_SETTINGS_PER_FRAME 32U

typedef enum SlHttp2FrameType
{
    SL_HTTP2_FRAME_DATA = 0x0,
    SL_HTTP2_FRAME_HEADERS = 0x1,
    SL_HTTP2_FRAME_PRIORITY = 0x2,
    SL_HTTP2_FRAME_RST_STREAM = 0x3,
    SL_HTTP2_FRAME_SETTINGS = 0x4,
    SL_HTTP2_FRAME_PUSH_PROMISE = 0x5,
    SL_HTTP2_FRAME_PING = 0x6,
    SL_HTTP2_FRAME_GOAWAY = 0x7,
    SL_HTTP2_FRAME_WINDOW_UPDATE = 0x8,
    SL_HTTP2_FRAME_CONTINUATION = 0x9
} SlHttp2FrameType;

typedef enum SlHttp2FrameFlag
{
    SL_HTTP2_FLAG_END_STREAM = 0x1,
    SL_HTTP2_FLAG_ACK = 0x1,
    SL_HTTP2_FLAG_END_HEADERS = 0x4,
    SL_HTTP2_FLAG_PADDED = 0x8,
    SL_HTTP2_FLAG_PRIORITY = 0x20
} SlHttp2FrameFlag;

typedef enum SlHttp2SettingId
{
    SL_HTTP2_SETTING_HEADER_TABLE_SIZE = 0x1,
    SL_HTTP2_SETTING_ENABLE_PUSH = 0x2,
    SL_HTTP2_SETTING_MAX_CONCURRENT_STREAMS = 0x3,
    SL_HTTP2_SETTING_INITIAL_WINDOW_SIZE = 0x4,
    SL_HTTP2_SETTING_MAX_FRAME_SIZE = 0x5,
    SL_HTTP2_SETTING_MAX_HEADER_LIST_SIZE = 0x6
} SlHttp2SettingId;

typedef enum SlHttp2ErrorCode
{
    SL_HTTP2_ERROR_NO_ERROR = 0x0,
    SL_HTTP2_ERROR_PROTOCOL_ERROR = 0x1,
    SL_HTTP2_ERROR_INTERNAL_ERROR = 0x2,
    SL_HTTP2_ERROR_FLOW_CONTROL_ERROR = 0x3,
    SL_HTTP2_ERROR_SETTINGS_TIMEOUT = 0x4,
    SL_HTTP2_ERROR_STREAM_CLOSED = 0x5,
    SL_HTTP2_ERROR_FRAME_SIZE_ERROR = 0x6,
    SL_HTTP2_ERROR_REFUSED_STREAM = 0x7,
    SL_HTTP2_ERROR_CANCEL = 0x8,
    SL_HTTP2_ERROR_COMPRESSION_ERROR = 0x9,
    SL_HTTP2_ERROR_CONNECT_ERROR = 0xa,
    SL_HTTP2_ERROR_ENHANCE_YOUR_CALM = 0xb,
    SL_HTTP2_ERROR_INADEQUATE_SECURITY = 0xc,
    SL_HTTP2_ERROR_HTTP_1_1_REQUIRED = 0xd
} SlHttp2ErrorCode;

typedef struct SlHttp2FrameHeader
{
    uint32_t length;
    uint8_t type;
    uint8_t flags;
    uint32_t stream_id;
} SlHttp2FrameHeader;

typedef struct SlHttp2Frame
{
    SlHttp2FrameHeader header;
    SlBytes payload;
} SlHttp2Frame;

typedef struct SlHttp2Setting
{
    uint16_t id;
    uint32_t value;
} SlHttp2Setting;

typedef struct SlHttp2SettingsFrame
{
    SlHttp2Setting settings[SL_HTTP2_MAX_SETTINGS_PER_FRAME];
    size_t setting_count;
} SlHttp2SettingsFrame;

SlStatus sl_http2_frame_header_parse(SlBytes bytes, SlHttp2FrameHeader* out_header);
SlStatus sl_http2_frame_header_write(const SlHttp2FrameHeader* header, unsigned char* buffer,
                                     size_t capacity, SlBytes* out_bytes);
SlStatus sl_http2_frame_parse(SlBytes bytes, uint32_t max_frame_size, SlHttp2Frame* out_frame);
SlStatus sl_http2_frame_write(const SlHttp2Frame* frame, unsigned char* buffer, size_t capacity,
                              SlBytes* out_bytes);
SlStatus sl_http2_frame_validate(const SlHttp2Frame* frame, uint32_t max_frame_size);
SlStatus sl_http2_settings_parse(const SlHttp2Frame* frame, SlHttp2SettingsFrame* out_settings);

#ifdef __cplusplus
}
#endif

#endif

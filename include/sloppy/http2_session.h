#ifndef SLOPPY_HTTP2_SESSION_H
#define SLOPPY_HTTP2_SESSION_H

#include "sloppy/arena.h"
#include "sloppy/builder.h"
#include "sloppy/bytes.h"
#include "sloppy/http2_hpack.h"
#include "sloppy/status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SL_HTTP2_SESSION_DEFAULT_MAX_EVENTS 256U
#define SL_HTTP2_SESSION_DEFAULT_MAX_HEADERS_PER_EVENT 128U
#define SL_HTTP2_SESSION_DEFAULT_MAX_HEADER_LIST_BYTES 65536U
#define SL_HTTP2_SESSION_DEFAULT_MAX_OUTBOUND_BYTES 1048576U
#define SL_HTTP2_SESSION_DEFAULT_MAX_EVENT_DATA_BYTES 1048576U
#define SL_HTTP2_SESSION_DEFAULT_MAX_CONCURRENT_STREAMS 100U
#define SL_HTTP2_SESSION_DEFAULT_INITIAL_WINDOW_SIZE 65535U

typedef enum SlHttp2SessionRole
{
    SL_HTTP2_SESSION_ROLE_CLIENT = 1,
    SL_HTTP2_SESSION_ROLE_SERVER = 2
} SlHttp2SessionRole;

typedef enum SlHttp2EventType
{
    SL_HTTP2_EVENT_NONE = 0,
    SL_HTTP2_EVENT_REQUEST_HEADERS = 1,
    SL_HTTP2_EVENT_RESPONSE_HEADERS = 2,
    SL_HTTP2_EVENT_HEADERS = 3,
    SL_HTTP2_EVENT_DATA = 4,
    SL_HTTP2_EVENT_STREAM_END = 5,
    SL_HTTP2_EVENT_STREAM_CLOSE = 6,
    SL_HTTP2_EVENT_RST_STREAM = 7,
    SL_HTTP2_EVENT_GOAWAY = 8,
    SL_HTTP2_EVENT_SETTINGS = 9
} SlHttp2EventType;

typedef struct SlHttp2Event
{
    SlHttp2EventType type;
    int32_t stream_id;
    int32_t last_stream_id;
    uint32_t error_code;
    bool end_stream;
    SlHttp2HeaderList headers;
    SlBytes data;
} SlHttp2Event;

typedef struct SlHttp2EventList
{
    const SlHttp2Event* events;
    size_t count;
} SlHttp2EventList;

typedef struct SlHttp2SessionConfig
{
    SlHttp2SessionRole role;
    size_t max_events;
    size_t max_headers_per_event;
    size_t max_header_list_bytes;
    size_t max_outbound_bytes;
    size_t max_event_data_bytes;
    uint32_t max_concurrent_streams;
    uint32_t initial_window_size;
} SlHttp2SessionConfig;

typedef struct SlHttp2Session
{
    void* session;
    SlArena* arena;
    SlHttp2SessionConfig config;
    SlByteBuilder outbound;
    SlHttp2Event* events;
    size_t event_count;
    SlHttp2HeaderField* current_headers;
    size_t current_header_count;
    size_t current_header_bytes;
    int32_t current_stream_id;
    uint8_t current_header_category;
    SlStatus callback_status;
} SlHttp2Session;

SlStatus sl_http2_session_init(SlHttp2Session* session, SlArena* arena,
                               const SlHttp2SessionConfig* config);
void sl_http2_session_dispose(SlHttp2Session* session);
SlStatus sl_http2_session_receive(SlHttp2Session* session, SlBytes bytes, size_t* out_consumed);
SlStatus sl_http2_session_drain_output(SlHttp2Session* session, SlBytes* out_bytes);
SlHttp2EventList sl_http2_session_events(const SlHttp2Session* session);
void sl_http2_session_clear_events(SlHttp2Session* session);
SlStatus sl_http2_session_upgrade_h2c(SlHttp2Session* session, SlBytes settings_payload,
                                      bool head_request);
SlStatus sl_http2_session_submit_request(SlHttp2Session* session, const SlHttp2HeaderList* headers,
                                         SlBytes body, int32_t* out_stream_id);
SlStatus sl_http2_session_submit_response(SlHttp2Session* session, int32_t stream_id,
                                          const SlHttp2HeaderList* headers, SlBytes body);
SlStatus sl_http2_session_submit_rst_stream(SlHttp2Session* session, int32_t stream_id,
                                            uint32_t error_code);
SlStatus sl_http2_session_submit_goaway(SlHttp2Session* session, int32_t last_stream_id,
                                        uint32_t error_code);
bool sl_http2_session_want_read(const SlHttp2Session* session);
bool sl_http2_session_want_write(const SlHttp2Session* session);

#ifdef __cplusplus
}
#endif

#endif

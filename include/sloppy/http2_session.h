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
#define SL_HTTP2_SESSION_CLOSED_STREAM_TRACK 1024U

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
    SL_HTTP2_EVENT_SETTINGS = 9,
    SL_HTTP2_EVENT_INVALID_FRAME = 10
} SlHttp2EventType;

typedef struct SlHttp2Event
{
    SlHttp2EventType type;
    int32_t stream_id;
    int32_t last_stream_id;
    uint32_t error_code;
    bool end_stream;
    /*
     * Borrowed session-owned views. `headers.fields`, every header name/value view, and
     * `data` bytes remain valid until sl_http2_session_clear_events(),
     * sl_http2_session_dispose(), or the owning arena is reset/disposed. Copy any value that
     * must outlive the current event batch.
     */
    SlHttp2HeaderList headers;
    SlBytes data;
} SlHttp2Event;

typedef struct SlHttp2EventList
{
    /*
     * Borrowed event array. Valid until sl_http2_session_clear_events(),
     * sl_http2_session_dispose(), or the owning arena is reset/disposed. Event payload views
     * have the same lifetime and are reclaimed by clear_events().
     */
    const SlHttp2Event* events;
    size_t count;
} SlHttp2EventList;

typedef struct SlHttp2ClosedStream
{
    int32_t stream_id;
    int64_t outbound_window;
    bool active;
    bool remote_closed;
    bool reset_by_peer;
    bool outbound_window_known;
} SlHttp2ClosedStream;

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
    void* outbound_bodies;
    size_t outbound_body_capacity;
    SlArenaMark event_mark;
    bool event_mark_valid;
    SlHttp2SessionConfig config;
    SlHttp2ClosedStream closed_streams[SL_HTTP2_SESSION_CLOSED_STREAM_TRACK];
    size_t next_closed_stream;
    SlByteBuilder outbound;
    SlHttp2Event* events;
    size_t event_count;
    SlHttp2HeaderField* current_headers;
    size_t current_header_count;
    size_t current_header_bytes;
    int64_t outbound_connection_window;
    int64_t outbound_initial_stream_window;
    int32_t current_stream_id;
    int32_t highest_peer_stream_id;
    uint8_t current_header_category;
    bool close_without_goaway;
    bool received_goaway;
    SlStatus callback_status;
} SlHttp2Session;

SlStatus sl_http2_session_init(SlHttp2Session* session, SlArena* arena,
                               const SlHttp2SessionConfig* config);
void sl_http2_session_dispose(SlHttp2Session* session);
SlStatus sl_http2_session_receive(SlHttp2Session* session, SlBytes bytes, size_t* out_consumed);
SlStatus sl_http2_session_drain_output(SlHttp2Session* session, SlBytes* out_bytes);
/*
 * Returns a borrowed view of pending received events. The list and each event's header/data
 * views remain valid until clear_events(), dispose(), or arena reset/dispose.
 */
SlHttp2EventList sl_http2_session_events(const SlHttp2Session* session);
/* Reclaims pending event header/data storage and invalidates prior event views. */
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
bool sl_http2_session_close_without_goaway(const SlHttp2Session* session);

#ifdef __cplusplus
}
#endif

#endif

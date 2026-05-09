#ifndef SLOPPY_HTTP2_DISPATCH_H
#define SLOPPY_HTTP2_DISPATCH_H

#include "sloppy/arena.h"
#include "sloppy/builder.h"
#include "sloppy/diagnostics.h"
#include "sloppy/http2_frame.h"
#include "sloppy/http2_mapping.h"
#include "sloppy/http2_session.h"
#include "sloppy/http_backend.h"
#include "sloppy/http_response.h"
#include "sloppy/status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SL_HTTP2_DISPATCH_DEFAULT_MAX_STREAMS 100U
#define SL_HTTP2_DISPATCH_DEFAULT_MAX_BODY_BYTES SL_HTTP_DEFAULT_MAX_BODY_LENGTH
#define SL_HTTP2_DISPATCH_DEFAULT_MAX_RESPONSE_BODY_BYTES 1048576U

typedef SlStatus (*SlHttp2DispatchFn)(SlHttpConnection* connection, SlArena* arena,
                                      const SlHttpRequestLifecycle* request,
                                      SlHttpResponse* out_response, SlDiag* out_diag, void* user);

typedef struct SlHttp2DispatchConfig
{
    size_t max_streams;
    size_t max_body_bytes;
    size_t max_response_body_bytes;
    SlHttp2SessionConfig session;
    SlHttp2DispatchFn dispatch;
    void* dispatch_user;
} SlHttp2DispatchConfig;

typedef struct SlHttp2DispatchStream
{
    int32_t stream_id;
    bool active;
    bool headers_seen;
    SlArenaMark mark;
    bool mark_valid;
    SlHttp2HeaderList headers;
    SlByteBuilder body;
} SlHttp2DispatchStream;

typedef struct SlHttp2ServerDispatcher
{
    SlArena* arena;
    SlHttpConnection* connection;
    SlHttp2DispatchConfig config;
    SlHttp2Session session;
    SlHttp2DispatchStream* streams;
    size_t active_streams;
    SlDiag last_diag;
    bool initialized;
} SlHttp2ServerDispatcher;

SlStatus sl_http2_server_dispatcher_init(SlHttp2ServerDispatcher* dispatcher, SlArena* arena,
                                         SlHttpConnection* connection,
                                         const SlHttp2DispatchConfig* config);
void sl_http2_server_dispatcher_dispose(SlHttp2ServerDispatcher* dispatcher);
SlStatus sl_http2_server_dispatcher_upgrade_h2c(SlHttp2ServerDispatcher* dispatcher,
                                                SlBytes settings_payload,
                                                SlHttpRequestLifecycle* request);
SlStatus sl_http2_server_dispatcher_receive(SlHttp2ServerDispatcher* dispatcher, SlBytes bytes,
                                            size_t* out_consumed);
SlStatus sl_http2_server_dispatcher_process_pending(SlHttp2ServerDispatcher* dispatcher);
SlStatus sl_http2_server_dispatcher_drain_output(SlHttp2ServerDispatcher* dispatcher,
                                                 SlBytes* out_bytes);
size_t sl_http2_server_dispatcher_active_streams(const SlHttp2ServerDispatcher* dispatcher);

#ifdef __cplusplus
}
#endif

#endif

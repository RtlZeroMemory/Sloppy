#ifndef SLOPPY_WEBSOCKET_H
#define SLOPPY_WEBSOCKET_H

#include "sloppy/arena.h"
#include "sloppy/bytes.h"
#include "sloppy/builder.h"
#include "sloppy/diagnostics.h"
#include "sloppy/http.h"
#include "sloppy/plan.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SlWebSocketHandshakeResult
{
    uint16_t http_status;
    SlStr selected_protocol;
    SlBytes response_bytes;
} SlWebSocketHandshakeResult;

typedef enum SlWebSocketOpcode
{
    SL_WEBSOCKET_OPCODE_CONTINUATION = 0x0,
    SL_WEBSOCKET_OPCODE_TEXT = 0x1,
    SL_WEBSOCKET_OPCODE_BINARY = 0x2,
    SL_WEBSOCKET_OPCODE_CLOSE = 0x8,
    SL_WEBSOCKET_OPCODE_PING = 0x9,
    SL_WEBSOCKET_OPCODE_PONG = 0xA
} SlWebSocketOpcode;

typedef struct SlWebSocketFrameParseOptions
{
    bool require_mask;
    size_t max_payload_bytes;
} SlWebSocketFrameParseOptions;

typedef struct SlWebSocketFrame
{
    bool fin;
    bool masked;
    SlWebSocketOpcode opcode;
    SlBytes payload;
} SlWebSocketFrame;

typedef struct SlWebSocketFrameParseResult
{
    SlWebSocketFrame frame;
    size_t consumed;
    uint16_t close_code;
} SlWebSocketFrameParseResult;

typedef struct SlWebSocketFrameWriteOptions
{
    bool fin;
    bool mask;
    SlWebSocketOpcode opcode;
    SlBytes payload;
    unsigned char mask_key[4];
} SlWebSocketFrameWriteOptions;

SlStatus sl_websocket_build_server_handshake(SlArena* arena, const SlHttpRequestHead* request,
                                             const SlPlanRoute* route,
                                             SlWebSocketHandshakeResult* out_result,
                                             SlDiag* out_diag);
SlStatus sl_websocket_parse_frame(SlArena* arena, SlBytes bytes,
                                  const SlWebSocketFrameParseOptions* options,
                                  SlWebSocketFrameParseResult* out_result, SlDiag* out_diag);
SlStatus sl_websocket_write_frame(SlByteBuilder* builder,
                                  const SlWebSocketFrameWriteOptions* options, SlDiag* out_diag);

#ifdef __cplusplus
}
#endif

#endif

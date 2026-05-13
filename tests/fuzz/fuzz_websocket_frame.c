#include "sloppy/websocket.h"

#include "fuzz_support.h"

#include <stdint.h>

static SlWebSocketOpcode fuzz_opcode(uint8_t value)
{
    static const SlWebSocketOpcode opcodes[] = {
        SL_WEBSOCKET_OPCODE_CONTINUATION, SL_WEBSOCKET_OPCODE_TEXT, SL_WEBSOCKET_OPCODE_BINARY,
        SL_WEBSOCKET_OPCODE_CLOSE,        SL_WEBSOCKET_OPCODE_PING, SL_WEBSOCKET_OPCODE_PONG,
    };
    return opcodes[value % (sizeof(opcodes) / sizeof(opcodes[0]))];
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    unsigned char arena_storage[8192];
    unsigned char output_storage[8192];
    SlArena arena = {0};
    SlByteBuilder builder = {0};
    SlWebSocketFrameParseOptions parse_options = {0};
    SlWebSocketFrameParseResult parsed = {0};
    SlWebSocketFrameWriteOptions write_options = {0};
    SlDiag diag = {0};

    if (data == NULL || size == 0U) {
        return 0;
    }

    if (!sl_status_is_ok(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)))) {
        return 0;
    }
    parse_options.require_mask = (data[0] & 1U) != 0U;
    parse_options.max_payload_bytes = 1U + (size_t)(data[0] % 128U);
    sl_websocket_parse_frame(&arena, sl_bytes_from_parts(data, size), &parse_options, &parsed,
                             &diag);

    if (!sl_status_is_ok(sl_byte_builder_init_fixed(&builder, output_storage,
                                                    sizeof(output_storage))))
    {
        return 0;
    }
    write_options.fin = (data[0] & 0x80U) != 0U;
    write_options.mask = (data[0] & 0x40U) != 0U;
    write_options.opcode = fuzz_opcode(data[0]);
    write_options.payload =
        size > 1U ? sl_bytes_from_parts(data + 1U, size - 1U) : sl_bytes_empty();
    write_options.mask_key[0] = size > 1U ? data[1] : 0U;
    write_options.mask_key[1] = size > 2U ? data[2] : 0U;
    write_options.mask_key[2] = size > 3U ? data[3] : 0U;
    write_options.mask_key[3] = size > 4U ? data[4] : 0U;
    sl_websocket_write_frame(&builder, &write_options, &diag);
    return 0;
}

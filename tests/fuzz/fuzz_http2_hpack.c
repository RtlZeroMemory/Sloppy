#include "sloppy/http2_hpack.h"

#include "fuzz_support.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    unsigned char arena_storage[65536];
    SlArena arena = {0};
    SlHttp2HpackDecoder decoder = {0};
    SlHttp2HeaderList headers = {0};

    if (data == NULL || size == 0U) {
        return 0;
    }

    if (!sl_status_is_ok(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)))) {
        return 0;
    }
    if (!sl_status_is_ok(sl_http2_hpack_decoder_init(&decoder, 128U, sizeof(arena_storage)))) {
        return 0;
    }

    sl_http2_hpack_decode(&decoder, &arena, sl_bytes_from_parts(data, size), true, &headers);
    sl_http2_hpack_decoder_dispose(&decoder);
    return 0;
}

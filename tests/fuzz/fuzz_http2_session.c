#include "sloppy/http2_session.h"

#include "fuzz_support.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    unsigned char arena_storage[65536];
    SlArena arena = {0};
    SlHttp2Session session = {0};
    SlHttp2SessionConfig config = {.role = SL_HTTP2_SESSION_ROLE_SERVER,
                                   .max_events = 64U,
                                   .max_headers_per_event = 64U,
                                   .max_event_data_bytes = 32768U,
                                   .max_outbound_bytes = 32768U};
    size_t consumed = 0U;
    SlBytes out = {0};

    if (data == NULL || size == 0U) {
        return 0;
    }
    if (!sl_status_is_ok(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)))) {
        return 0;
    }
    if (!sl_status_is_ok(sl_http2_session_init(&session, &arena, &config))) {
        return 0;
    }

    sl_http2_session_receive(&session, sl_bytes_from_parts(data, size), &consumed);
    sl_http2_session_drain_output(&session, &out);
    sl_http2_session_dispose(&session);
    return 0;
}

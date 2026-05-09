#include "sloppy/http2_frame.h"

#include "fuzz_support.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    SlHttp2Frame frame = {0};
    SlHttp2SettingsFrame settings = {0};
    uint32_t max_frame_size = SL_HTTP2_DEFAULT_MAX_FRAME_SIZE;

    if (data == NULL || size == 0U) {
        return 0;
    }

    if (size > 1U && data[0] != 0U) {
        max_frame_size = SL_HTTP2_MIN_MAX_FRAME_SIZE + (uint32_t)data[0];
    }

    if (sl_status_is_ok(
            sl_http2_frame_parse(sl_bytes_from_parts(data, size), max_frame_size, &frame)) &&
        frame.header.type == SL_HTTP2_FRAME_SETTINGS)
    {
        (void)sl_http2_settings_parse(&frame, &settings);
    }

    return 0;
}

#include "sloppy/http.h"

#include "fuzz_support.h"

#include <stddef.h>
#include <stdint.h>

#define FUZZ_HTTP_ARENA_SIZE 131072U

static size_t bounded_limit(uint8_t byte, size_t fallback, size_t max)
{
    size_t value = (size_t)byte;

    if (value == 0U) {
        return fallback;
    }

    return value > max ? max : value;
}

static void parse_once(const uint8_t* data, size_t size, const SlHttpParseOptions* options)
{
    unsigned char arena_storage[FUZZ_HTTP_ARENA_SIZE];
    SlArena arena = {0};
    SlHttpRequestHead request = {0};
    SlDiag diag = {0};

    if (!sl_status_is_ok(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)))) {
        return;
    }

    (void)sl_http_parse_request_head(&arena, sl_bytes_from_parts(data, size), options, &request,
                                     &diag);
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    SlHttpParseOptions options = {0};

    if (data == NULL || size == 0U) {
        return 0;
    }

    parse_once(data, size, NULL);

    options.max_headers = bounded_limit(data[0], SL_HTTP_DEFAULT_MAX_HEADERS, 8U);
    options.max_target_length =
        size > 1U ? bounded_limit(data[1], SL_HTTP_DEFAULT_MAX_TARGET_LENGTH, 256U) : 32U;
    options.max_header_name_length =
        size > 2U ? bounded_limit(data[2], SL_HTTP_DEFAULT_MAX_HEADER_NAME_LENGTH, 128U) : 32U;
    options.max_header_value_length =
        size > 3U ? bounded_limit(data[3], SL_HTTP_DEFAULT_MAX_HEADER_VALUE_LENGTH, 512U) : 64U;
    options.max_total_header_bytes =
        size > 4U ? bounded_limit(data[4], SL_HTTP_DEFAULT_MAX_TOTAL_HEADER_BYTES, 2048U) : 512U;
    options.max_body_length =
        size > 5U ? bounded_limit(data[5], SL_HTTP_DEFAULT_MAX_BODY_LENGTH, 4096U) : 256U;

    parse_once(data, size, &options);
    return 0;
}

#include "sloppy/route.h"

#include "fuzz_support.h"

#include <stddef.h>
#include <stdint.h>

#define FUZZ_ROUTE_ARENA_SIZE 65536U

static size_t split_input(const uint8_t* data, size_t size)
{
    size_t index = 0U;

    for (index = 0U; index < size; index += 1U) {
        if (data[index] == (uint8_t)'\n') {
            return index;
        }
    }

    return size;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    unsigned char parse_storage[FUZZ_ROUTE_ARENA_SIZE];
    unsigned char match_storage[FUZZ_ROUTE_ARENA_SIZE];
    SlArena parse_arena = {0};
    SlArena match_arena = {0};
    SlRoutePattern pattern = {0};
    SlRouteMatch match = {0};
    SlDiag diag = {0};
    SlStatus status;
    size_t split = 0U;

    if (data == NULL || size == 0U) {
        return 0;
    }

    split = split_input(data, size);
    if (!sl_status_is_ok(sl_arena_init(&parse_arena, parse_storage, sizeof(parse_storage)))) {
        return 0;
    }

    status = sl_route_pattern_parse(&parse_arena, sl_str_from_parts((const char*)data, split),
                                    &pattern, &diag);
    if (!sl_status_is_ok(status)) {
        return 0;
    }

    if (!sl_status_is_ok(sl_arena_init(&match_arena, match_storage, sizeof(match_storage)))) {
        return 0;
    }

    if (split < size) {
        const uint8_t* path = data + split + 1U;
        size_t path_length = size - split - 1U;
        sl_route_pattern_match(&match_arena, &pattern,
                               sl_str_from_parts((const char*)path, path_length), &match);
    }
    else {
        sl_route_pattern_match(&match_arena, &pattern, pattern.source, &match);
    }

    return 0;
}

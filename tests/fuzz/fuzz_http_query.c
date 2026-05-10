#include "sloppy/http_context.h"

#include "fuzz_support.h"

#include <stddef.h>
#include <stdint.h>

#define FUZZ_QUERY_ARENA_SIZE 32768U
#define FUZZ_QUERY_MAX_TARGET_SIZE 2048U

static void parse_query_target(const uint8_t* data, size_t size)
{
    unsigned char arena_storage[FUZZ_QUERY_ARENA_SIZE];
    SlArena arena = {0};
    SlHttpQuery query = {0};

    if (!sl_status_is_ok(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)))) {
        return;
    }

    sl_http_query_parse(&arena, sl_str_from_parts((const char*)data, size), &query);
}

static void parse_as_origin_form_query(const uint8_t* data, size_t size)
{
    unsigned char target[FUZZ_QUERY_MAX_TARGET_SIZE + 2U];
    size_t copy_size = size;
    size_t index = 0U;

    if (copy_size > FUZZ_QUERY_MAX_TARGET_SIZE) {
        copy_size = FUZZ_QUERY_MAX_TARGET_SIZE;
    }

    target[0] = (unsigned char)'/';
    target[1] = (unsigned char)'?';
    for (index = 0U; index < copy_size; index += 1U) {
        target[index + 2U] = data[index];
    }

    parse_query_target(target, copy_size + 2U);
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (data == NULL) {
        return 0;
    }

    parse_query_target(data, size);
    parse_as_origin_form_query(data, size);
    return 0;
}

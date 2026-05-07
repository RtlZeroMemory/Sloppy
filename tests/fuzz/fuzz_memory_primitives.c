#include "sloppy/arena.h"
#include "sloppy/builder.h"
#include "sloppy/bytes.h"
#include "sloppy/checked_math.h"
#include "sloppy/string.h"

#include "fuzz_support.h"

#include <stddef.h>
#include <stdint.h>

#define FUZZ_MEMORY_ARENA_SIZE 256U
#define FUZZ_MEMORY_FIXED_BUILDER_SIZE 96U

static int status_is_ok(SlStatus status)
{
    return sl_status_is_ok(status) ? 1 : 0;
}

static size_t bounded_size(uint8_t byte, size_t max)
{
    size_t value = (size_t)byte;
    return value > max ? max : value;
}

static int exercise_arena_and_checked_math(const uint8_t* data, size_t size)
{
    unsigned char arena_storage[FUZZ_MEMORY_ARENA_SIZE];
    SlArena arena;
    SlArenaStats before;
    SlArenaStats after;
    SlArenaMark mark;
    void* ptr = NULL;
    size_t alloc_size = size == 0U ? 1U : bounded_size(data[0], 64U) + 1U;
    size_t alignment = size > 1U ? ((size_t)1U << (data[1] & 3U)) : 1U;
    size_t checked = 0U;

    if (!status_is_ok(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)))) {
        return 1;
    }

    before = sl_arena_stats(&arena);
    if (before.capacity != sizeof(arena_storage) || before.used != 0U ||
        before.remaining != sizeof(arena_storage))
    {
        return 1;
    }

    mark = sl_arena_mark(&arena);
    (void)sl_checked_add3_size(alloc_size, size > 2U ? data[2] : 0U, size > 3U ? data[3] : 0U,
                               &checked);
    (void)sl_checked_array_size(alloc_size, alignment, &checked);
    (void)sl_arena_alloc(&arena, alloc_size, alignment, &ptr);
    after = sl_arena_stats(&arena);
    if (after.used > after.capacity || after.high_water < after.used) {
        return 1;
    }

    if (!status_is_ok(sl_arena_reset_to(&arena, mark))) {
        return 1;
    }
    if (sl_arena_stats(&arena).used != mark.offset) {
        return 1;
    }

    sl_arena_reset(&arena);
    if (sl_arena_stats(&arena).used != 0U) {
        return 1;
    }

    return 0;
}

static int exercise_string_and_bytes(const uint8_t* data, size_t size)
{
    SlBytes bytes = sl_bytes_from_parts(data, size);
    SlStr str = sl_str_from_parts((const char*)data, size);
    SlBytesFindResult result = {0};
    uint64_t hash = 0U;

    if (!status_is_ok(sl_bytes_find(bytes, 0U, &result))) {
        return 1;
    }
    if (result.found && (result.index >= size || data[result.index] != 0U)) {
        return 1;
    }
    if (!status_is_ok(
            sl_bytes_find_any(bytes, sl_bytes_from_parts(data, size > 4U ? 4U : size), &result)))
    {
        return 1;
    }
    if (result.found && result.index >= size) {
        return 1;
    }
    if (!status_is_ok(sl_bytes_hash(bytes, &hash)) || !status_is_ok(sl_str_hash(str, &hash))) {
        return 1;
    }
    if (!sl_str_equal_ci_ascii(str, str)) {
        return 1;
    }
    if (sl_str_contains_nul(str) &&
        sl_status_code(sl_str_validate_no_nul(str)) != SL_STATUS_INVALID_ARGUMENT)
    {
        return 1;
    }

    return 0;
}

static int exercise_builders(const uint8_t* data, size_t size)
{
    unsigned char storage[FUZZ_MEMORY_FIXED_BUILDER_SIZE];
    unsigned char arena_storage[FUZZ_MEMORY_ARENA_SIZE];
    SlByteBuilder byte_builder;
    SlByteBuilder small_builder;
    SlStringBuilder string_builder;
    SlArena arena;
    SlByteBuilderStats stats;
    SlBytes bytes = sl_bytes_from_parts(data, size);
    size_t prefix = size > SL_BYTE_BUILDER_SMALL_CAPACITY ? SL_BYTE_BUILDER_SMALL_CAPACITY : size;

    if (!status_is_ok(sl_byte_builder_init_fixed(&byte_builder, storage, sizeof(storage)))) {
        return 1;
    }

    (void)sl_byte_builder_append_bytes(&byte_builder, bytes);
    if (prefix != 0U) {
        (void)sl_byte_builder_append_bytes(
            &byte_builder, sl_bytes_from_parts(storage, sl_byte_builder_length(&byte_builder)));
    }
    stats = sl_byte_builder_stats(&byte_builder);
    if (stats.length > stats.capacity || stats.appended_bytes < stats.length) {
        return 1;
    }

    if (!status_is_ok(sl_byte_builder_init_small(&small_builder))) {
        return 1;
    }
    if (!status_is_ok(
            sl_byte_builder_append_bytes(&small_builder, sl_bytes_from_parts(data, prefix))))
    {
        return 1;
    }
    stats = sl_byte_builder_stats(&small_builder);
    if (stats.storage != SL_BUILDER_STORAGE_SMALL || stats.length > stats.capacity ||
        stats.capacity != SL_BYTE_BUILDER_SMALL_CAPACITY || stats.length != prefix)
    {
        return 1;
    }

    if (!status_is_ok(sl_arena_init(&arena, arena_storage, sizeof(arena_storage))) ||
        !status_is_ok(
            sl_string_builder_init_arena(&string_builder, &arena, 0U, sizeof(arena_storage))))
    {
        return 1;
    }

    (void)sl_string_builder_append_str(&string_builder,
                                       sl_str_from_parts((const char*)data, prefix));
    (void)sl_string_builder_append_size(&string_builder, size);
    stats = sl_string_builder_stats(&string_builder);
    if (stats.length > stats.capacity || stats.capacity > stats.max_capacity) {
        return 1;
    }

    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (data == NULL) {
        return 0;
    }

    if (exercise_arena_and_checked_math(data, size) != 0 ||
        exercise_string_and_bytes(data, size) != 0 || exercise_builders(data, size) != 0)
    {
        return 1;
    }

    return 0;
}

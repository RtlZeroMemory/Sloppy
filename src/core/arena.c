/*
 * src/core/arena.c
 *
 * Implements Sloppy's first caller-backed arena allocator.
 *
 * Safety invariants:
 * - the arena never allocates or frees its backing storage;
 * - alignment padding and final offsets use checked size arithmetic;
 * - returned pointers satisfy caller-requested power-of-two alignment;
 * - resets never shrink high-water statistics;
 * - full resets invalidate marks in all builds;
 * - assert-enabled builds poison allocation and reset ranges.
 *
 * Tests: tests/unit/core/test_arena.c.
 */
#include "sloppy/arena.h"

#include "sloppy/checked_math.h"
#include "sloppy/string.h"

#include <stdbool.h>
#include <stdint.h>

#define SL_ARENA_POISON_ALLOC 0xCDU
#define SL_ARENA_POISON_RESET 0xDDU

static bool sl_arena_is_power_of_two(size_t value)
{
    return value != 0U && (value & (value - 1U)) == 0U;
}

#if SL_ENABLE_ASSERTS
static void sl_arena_poison(unsigned char* ptr, size_t size, unsigned char value)
{
    size_t index = 0U;

    for (index = 0U; index < size; index += 1U) {
        ptr[index] = value;
    }
}
#endif

static SlStatus sl_arena_alignment_padding(const SlArena* arena, size_t alignment,
                                           size_t* out_padding)
{
    uintptr_t base_address = 0U;
    uintptr_t current_address = 0U;
    size_t mask = 0U;
    size_t misalignment = 0U;

    if (alignment > (size_t)UINTPTR_MAX) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    base_address = (uintptr_t)arena->base;
    if ((uintptr_t)arena->offset > UINTPTR_MAX - base_address) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    current_address = base_address + (uintptr_t)arena->offset;
    mask = alignment - 1U;
    misalignment = (size_t)(current_address & (uintptr_t)mask);

    *out_padding = misalignment == 0U ? 0U : alignment - misalignment;
    return sl_status_ok();
}

SlStatus sl_arena_init(SlArena* arena, void* buffer, size_t capacity)
{
    if (arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (buffer == NULL && capacity != 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    arena->base = (unsigned char*)buffer;
    arena->capacity = capacity;
    arena->offset = 0U;
    arena->high_water = 0U;
    arena->generation = 1U;

    return sl_status_ok();
}

void sl_arena_reset(SlArena* arena)
{
    if (arena == NULL) {
        return;
    }

#if SL_ENABLE_ASSERTS
    if (arena->base != NULL && arena->offset != 0U) {
        size_t poison_size = arena->offset;
        if (poison_size > arena->capacity) {
            poison_size = arena->capacity;
        }
        if (poison_size != 0U) {
            sl_arena_poison(arena->base, poison_size, SL_ARENA_POISON_RESET);
        }
    }
#endif

    arena->generation += 1U;
    if (arena->generation == 0U) {
        arena->generation = 1U;
    }

    arena->offset = 0U;
}

void sl_arena_dispose(SlArena* arena)
{
    if (arena == NULL) {
        return;
    }

#if SL_ENABLE_ASSERTS
    if (arena->base != NULL && arena->offset != 0U) {
        sl_arena_poison(arena->base, arena->offset, SL_ARENA_POISON_RESET);
    }
#endif

    arena->base = NULL;
    arena->capacity = 0U;
    arena->offset = 0U;
    arena->high_water = 0U;
    arena->generation += 1U;
    if (arena->generation == 0U) {
        arena->generation = 1U;
    }
}

SlArenaMark sl_arena_mark(const SlArena* arena)
{
    SlArenaMark mark = {0U};

    if (arena == NULL) {
        return mark;
    }

    mark.offset = arena->offset;
    mark.generation = arena->generation;

    return mark;
}

SlStatus sl_arena_reset_to(SlArena* arena, SlArenaMark mark)
{
    if (arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (mark.generation != arena->generation) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (mark.offset > arena->offset || mark.offset > arena->capacity) {
        return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
    }

#if SL_ENABLE_ASSERTS
    if (arena->base != NULL && mark.offset < arena->offset) {
        sl_arena_poison(arena->base + mark.offset, arena->offset - mark.offset,
                        SL_ARENA_POISON_RESET);
    }
#endif

    arena->offset = mark.offset;
    return sl_status_ok();
}

SlStatus sl_arena_alloc(SlArena* arena, size_t size, size_t alignment, void** out)
{
    SlStatus status;
    size_t padding = 0U;
    size_t offset_with_padding = 0U;
    size_t total_advance = 0U;
    size_t final_offset = 0U;
    unsigned char* result = NULL;

    if (arena == NULL || out == NULL || size == 0U || !sl_arena_is_power_of_two(alignment)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (arena->offset > arena->capacity || (arena->base == NULL && arena->capacity != 0U)) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    if (arena->base == NULL) {
        return sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
    }

    status = sl_arena_alignment_padding(arena, alignment, &padding);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    /*
     * Check padding + size and offset + padding independently so neither the requested
     * allocation size nor the arena cursor can wrap into a smaller final offset.
     */
    status = sl_checked_add_size(padding, size, &total_advance);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_checked_add_size(arena->offset, padding, &offset_with_padding);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_checked_add_size(arena->offset, total_advance, &final_offset);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (final_offset > arena->capacity) {
        return sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
    }

    result = arena->base + offset_with_padding;
    arena->offset = final_offset;
    if (arena->high_water < arena->offset) {
        arena->high_water = arena->offset;
    }

#if SL_ENABLE_ASSERTS
    sl_arena_poison(result, size, SL_ARENA_POISON_ALLOC);
#endif

    *out = result;
    return sl_status_ok();
}

size_t sl_arena_capacity(const SlArena* arena)
{
    return arena == NULL ? 0U : arena->capacity;
}

size_t sl_arena_used(const SlArena* arena)
{
    return arena == NULL ? 0U : arena->offset;
}

size_t sl_arena_remaining(const SlArena* arena)
{
    if (arena == NULL || arena->offset > arena->capacity) {
        return 0U;
    }

    return arena->capacity - arena->offset;
}

size_t sl_arena_high_water(const SlArena* arena)
{
    return arena == NULL ? 0U : arena->high_water;
}

SlArenaStats sl_arena_stats(const SlArena* arena)
{
    SlArenaStats stats = {0U};

    if (arena == NULL) {
        return stats;
    }

    stats.capacity = arena->capacity;
    stats.used = arena->offset;
    stats.remaining = sl_arena_remaining(arena);
    stats.high_water = arena->high_water;
    stats.generation = arena->generation;
    return stats;
}

bool sl_arena_contains_str(const SlArena* arena, SlStr str)
{
    uintptr_t arena_start = 0U;
    uintptr_t arena_end = 0U;
    uintptr_t str_start = 0U;
    uintptr_t str_end = 0U;

    if (arena == NULL) {
        return false;
    }

    if (str.length == 0U) {
        return true;
    }

    if (arena->base == NULL || str.ptr == NULL || arena->capacity == 0U ||
        arena->capacity > (size_t)UINTPTR_MAX)
    {
        return false;
    }

    arena_start = (uintptr_t)arena->base;
    if ((size_t)(UINTPTR_MAX - arena_start) < arena->capacity) {
        return false;
    }
    arena_end = arena_start + arena->capacity;

    str_start = (uintptr_t)str.ptr;
    if ((size_t)(UINTPTR_MAX - str_start) < str.length) {
        return false;
    }
    str_end = str_start + str.length;

    return str_start >= arena_start && str_end <= arena_end;
}

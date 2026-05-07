#include "sloppy/arena.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static int expect_aligned(const void* ptr, size_t alignment)
{
    uintptr_t address = (uintptr_t)ptr;
    return expect_true((address & (uintptr_t)(alignment - 1U)) == 0U);
}

static void fill_bytes(unsigned char* ptr, size_t size, unsigned char value)
{
    size_t index = 0U;

    for (index = 0U; index < size; index += 1U) {
        ptr[index] = value;
    }
}

static int test_initialization(void)
{
    unsigned char buffer[128];
    SlArena arena;
    SlArena zero_capacity;
    void* ptr = (void*)buffer;

    fill_bytes(buffer, sizeof(buffer), 0U);

    if (expect_status(sl_arena_init(&arena, buffer, sizeof(buffer)), SL_STATUS_OK) != 0) {
        return 1;
    }

    if (sl_arena_capacity(&arena) != sizeof(buffer) || sl_arena_used(&arena) != 0U ||
        sl_arena_remaining(&arena) != sizeof(buffer) || sl_arena_high_water(&arena) != 0U)
    {
        return 2;
    }

    if (expect_status(sl_arena_init(NULL, buffer, sizeof(buffer)), SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 3;
    }

    if (expect_status(sl_arena_init(&arena, NULL, sizeof(buffer)), SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 4;
    }

    if (expect_status(sl_arena_init(&zero_capacity, NULL, 0U), SL_STATUS_OK) != 0 ||
        sl_arena_capacity(&zero_capacity) != 0U)
    {
        return 5;
    }

    if (expect_status(sl_arena_alloc(&zero_capacity, 1U, 1U, &ptr), SL_STATUS_OUT_OF_MEMORY) != 0 ||
        ptr != (void*)buffer)
    {
        return 6;
    }

    return 0;
}

static int test_allocation_and_alignment(void)
{
    unsigned char buffer[128];
    SlArena arena;
    void* ptr = NULL;
    void* first = NULL;
    void* second = NULL;
    void* sentinel = (void*)buffer;

    fill_bytes(buffer, sizeof(buffer), 0U);

    if (expect_status(sl_arena_init(&arena, buffer, sizeof(buffer)), SL_STATUS_OK) != 0) {
        return 10;
    }

    if (expect_status(sl_arena_alloc(&arena, 8U, 1U, &ptr), SL_STATUS_OK) != 0 || ptr == NULL ||
        sl_arena_used(&arena) != 8U)
    {
        return 11;
    }

    first = ptr;
    if (expect_status(sl_arena_alloc(&arena, 4U, 4U, &second), SL_STATUS_OK) != 0 ||
        (unsigned char*)second < (unsigned char*)first + 8U || sl_arena_high_water(&arena) < 12U)
    {
        return 12;
    }

    sl_arena_reset(&arena);
    if (sl_arena_used(&arena) != 0U || sl_arena_high_water(&arena) < 12U) {
        return 13;
    }

    if (expect_status(sl_arena_alloc(&arena, 1U, 1U, &ptr), SL_STATUS_OK) != 0 ||
        expect_aligned(ptr, 1U) != 0)
    {
        return 14;
    }

    if (expect_status(sl_arena_alloc(&arena, 1U, 2U, &ptr), SL_STATUS_OK) != 0 ||
        expect_aligned(ptr, 2U) != 0)
    {
        return 15;
    }

    if (expect_status(sl_arena_alloc(&arena, 1U, 4U, &ptr), SL_STATUS_OK) != 0 ||
        expect_aligned(ptr, 4U) != 0)
    {
        return 16;
    }

    if (expect_status(sl_arena_alloc(&arena, 1U, 8U, &ptr), SL_STATUS_OK) != 0 ||
        expect_aligned(ptr, 8U) != 0)
    {
        return 17;
    }

    if (expect_status(sl_arena_alloc(&arena, 1U, 16U, &ptr), SL_STATUS_OK) != 0 ||
        expect_aligned(ptr, 16U) != 0)
    {
        return 18;
    }

    if (expect_status(sl_arena_alloc(&arena, 1U, _Alignof(max_align_t), &ptr), SL_STATUS_OK) != 0 ||
        expect_aligned(ptr, _Alignof(max_align_t)) != 0)
    {
        return 19;
    }

    ptr = sentinel;
    if (expect_status(sl_arena_alloc(NULL, 1U, 1U, &ptr), SL_STATUS_INVALID_ARGUMENT) != 0 ||
        ptr != sentinel)
    {
        return 20;
    }

    if (expect_status(sl_arena_alloc(&arena, 1U, 1U, NULL), SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 21;
    }

    ptr = sentinel;
    if (expect_status(sl_arena_alloc(&arena, 0U, 1U, &ptr), SL_STATUS_INVALID_ARGUMENT) != 0 ||
        ptr != sentinel)
    {
        return 22;
    }

    if (expect_status(sl_arena_alloc(&arena, 1U, 0U, &ptr), SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 23;
    }

    if (expect_status(sl_arena_alloc(&arena, 1U, 3U, &ptr), SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 24;
    }

    return 0;
}

static int test_capacity_and_overflow(void)
{
    unsigned char small[8];
    SlArena arena;
    SlArena corrupt_arena;
    void* ptr = (void*)small;

    fill_bytes(small, sizeof(small), 0U);

    if (expect_status(sl_arena_init(&arena, small, sizeof(small)), SL_STATUS_OK) != 0) {
        return 30;
    }

    if (expect_status(sl_arena_alloc(&arena, sizeof(small) + 1U, 1U, &ptr),
                      SL_STATUS_OUT_OF_MEMORY) != 0 ||
        ptr != (void*)small || sl_arena_used(&arena) != 0U)
    {
        return 31;
    }

    corrupt_arena = arena;
    corrupt_arena.offset = SIZE_MAX;
    if (expect_status(sl_arena_alloc(&corrupt_arena, 1U, 1U, &ptr), SL_STATUS_INTERNAL) != 0) {
        return 32;
    }

    corrupt_arena = arena;
    corrupt_arena.capacity = SIZE_MAX;
    corrupt_arena.offset = SIZE_MAX - 1U;
    if (expect_status(sl_arena_alloc(&corrupt_arena, SIZE_MAX, 1U, &ptr), SL_STATUS_OVERFLOW) != 0)
    {
        return 33;
    }

    return 0;
}

static int test_mark_invalid_inputs(void)
{
    unsigned char small[8];
    SlArena arena;
    SlArenaMark mark;
    SlArenaMark future;
    SlArenaMark null_mark;
    void* ptr = NULL;

    fill_bytes(small, sizeof(small), 0U);

    if (expect_status(sl_arena_init(&arena, small, sizeof(small)), SL_STATUS_OK) != 0) {
        return 40;
    }

    null_mark = sl_arena_mark(NULL);
    if (expect_status(sl_arena_reset_to(&arena, null_mark), SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 41;
    }

    if (expect_status(sl_arena_alloc(&arena, 4U, 4U, &ptr), SL_STATUS_OK) != 0) {
        return 42;
    }

    mark = sl_arena_mark(&arena);
    future = sl_arena_mark(&arena);
    future.offset = sizeof(small) + 1U;
    if (expect_status(sl_arena_reset_to(&arena, future), SL_STATUS_OUT_OF_RANGE) != 0) {
        return 43;
    }

    sl_arena_reset(&arena);
    if (expect_status(sl_arena_reset_to(&arena, mark), SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 44;
    }

    if (expect_status(sl_arena_alloc(&arena, 2U, 1U, &ptr), SL_STATUS_OK) != 0) {
        return 45;
    }

    if (expect_status(sl_arena_reset_to(&arena, mark), SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 46;
    }

    return 0;
}

static int test_mark_reset_and_stats(void)
{
    unsigned char small[8];
    SlArena arena;
    SlArenaMark mark;
    SlArenaMark outer;
    SlArenaMark inner;
    void* ptr = NULL;

    fill_bytes(small, sizeof(small), 0U);

    if (expect_status(sl_arena_init(&arena, small, sizeof(small)), SL_STATUS_OK) != 0) {
        return 50;
    }

    if (expect_status(sl_arena_alloc(&arena, 4U, 4U, &ptr), SL_STATUS_OK) != 0) {
        return 51;
    }

    mark = sl_arena_mark(&arena);
    if (expect_status(sl_arena_alloc(&arena, 4U, 1U, &ptr), SL_STATUS_OK) != 0 ||
        sl_arena_used(&arena) != 8U)
    {
        return 52;
    }

    if (expect_status(sl_arena_reset_to(&arena, mark), SL_STATUS_OK) != 0 ||
        sl_arena_used(&arena) != mark.offset)
    {
        return 53;
    }

    outer = sl_arena_mark(&arena);
    if (expect_status(sl_arena_alloc(&arena, 1U, 1U, &ptr), SL_STATUS_OK) != 0) {
        return 54;
    }

    inner = sl_arena_mark(&arena);
    if (expect_status(sl_arena_alloc(&arena, 1U, 1U, &ptr), SL_STATUS_OK) != 0) {
        return 55;
    }

    if (expect_status(sl_arena_reset_to(&arena, inner), SL_STATUS_OK) != 0 ||
        sl_arena_used(&arena) != inner.offset)
    {
        return 56;
    }

    if (expect_status(sl_arena_reset_to(&arena, outer), SL_STATUS_OK) != 0 ||
        sl_arena_used(&arena) != outer.offset)
    {
        return 57;
    }

    sl_arena_reset(&arena);
    if (sl_arena_used(&arena) != 0U || sl_arena_high_water(&arena) < 8U) {
        return 58;
    }

    if (expect_status(sl_arena_alloc(&arena, 1U, 1U, &ptr), SL_STATUS_OK) != 0 ||
        ptr != (void*)small)
    {
        return 59;
    }

    return 0;
}

static int test_stats_snapshot_contract(void)
{
    unsigned char buffer[32];
    SlArena arena;
    SlArenaStats stats;
    SlArenaMark mark;
    void* ptr = NULL;
    unsigned int initial_generation = 0U;

    fill_bytes(buffer, sizeof(buffer), 0U);

    stats = sl_arena_stats(NULL);
    if (stats.capacity != 0U || stats.used != 0U || stats.remaining != 0U ||
        stats.high_water != 0U || stats.generation != 0U)
    {
        return 80;
    }

    if (expect_status(sl_arena_init(&arena, buffer, sizeof(buffer)), SL_STATUS_OK) != 0) {
        return 81;
    }

    stats = sl_arena_stats(&arena);
    initial_generation = stats.generation;
    if (stats.capacity != sizeof(buffer) || stats.used != 0U || stats.remaining != sizeof(buffer) ||
        stats.high_water != 0U || stats.generation == 0U)
    {
        return 82;
    }

    if (expect_status(sl_arena_alloc(&arena, 7U, 1U, &ptr), SL_STATUS_OK) != 0) {
        return 83;
    }

    mark = sl_arena_mark(&arena);
    if (expect_status(sl_arena_alloc(&arena, 8U, 8U, &ptr), SL_STATUS_OK) != 0) {
        return 84;
    }

    stats = sl_arena_stats(&arena);
    if (stats.used != sl_arena_used(&arena) || stats.remaining != sl_arena_remaining(&arena) ||
        stats.high_water != sl_arena_high_water(&arena) || stats.generation != initial_generation)
    {
        return 85;
    }

    if (expect_status(sl_arena_reset_to(&arena, mark), SL_STATUS_OK) != 0) {
        return 86;
    }

    stats = sl_arena_stats(&arena);
    if (stats.used != mark.offset || stats.high_water <= stats.used ||
        stats.generation != initial_generation)
    {
        return 87;
    }

    sl_arena_reset(&arena);
    stats = sl_arena_stats(&arena);
    if (stats.used != 0U || stats.high_water <= 0U || stats.generation == initial_generation ||
        stats.remaining != sizeof(buffer))
    {
        return 88;
    }

    sl_arena_dispose(&arena);
    stats = sl_arena_stats(&arena);
    if (stats.capacity != 0U || stats.used != 0U || stats.remaining != 0U || stats.high_water != 0U)
    {
        return 89;
    }

    return 0;
}

static int test_debug_poisoning(void)
{
#if SL_ENABLE_ASSERTS
    unsigned char buffer[16];
    SlArena arena;
    void* ptr = NULL;

    fill_bytes(buffer, sizeof(buffer), 0U);

    if (expect_status(sl_arena_init(&arena, buffer, sizeof(buffer)), SL_STATUS_OK) != 0) {
        return 60;
    }

    if (expect_status(sl_arena_alloc(&arena, 4U, 1U, &ptr), SL_STATUS_OK) != 0) {
        return 61;
    }

    if (((unsigned char*)ptr)[0] != 0xCDU || ((unsigned char*)ptr)[3] != 0xCDU) {
        return 62;
    }

    sl_arena_reset(&arena);
    if (buffer[0] != 0xDDU || buffer[3] != 0xDDU) {
        return 63;
    }
#endif

    return 0;
}

static int test_dispose_invalidates_arena_object(void)
{
    unsigned char buffer[16];
    SlArena arena;
    SlArenaMark mark;
    void* ptr = (void*)buffer;

    fill_bytes(buffer, sizeof(buffer), 0U);

    if (expect_status(sl_arena_init(&arena, buffer, sizeof(buffer)), SL_STATUS_OK) != 0) {
        return 70;
    }

    if (expect_status(sl_arena_alloc(&arena, 4U, 1U, &ptr), SL_STATUS_OK) != 0) {
        return 71;
    }

    mark = sl_arena_mark(&arena);
    sl_arena_dispose(&arena);
    if (sl_arena_capacity(&arena) != 0U || sl_arena_used(&arena) != 0U ||
        sl_arena_remaining(&arena) != 0U || sl_arena_high_water(&arena) != 0U)
    {
        return 72;
    }

    ptr = (void*)buffer;
    if (expect_status(sl_arena_alloc(&arena, 1U, 1U, &ptr), SL_STATUS_OUT_OF_MEMORY) != 0 ||
        ptr != (void*)buffer)
    {
        return 73;
    }

    if (expect_status(sl_arena_reset_to(&arena, mark), SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 74;
    }

    sl_arena_dispose(NULL);
    return 0;
}

int main(void)
{
    int result = 0;

    result = test_initialization();
    if (result != 0) {
        return result;
    }

    result = test_allocation_and_alignment();
    if (result != 0) {
        return result;
    }

    result = test_capacity_and_overflow();
    if (result != 0) {
        return result;
    }

    result = test_mark_invalid_inputs();
    if (result != 0) {
        return result;
    }

    result = test_mark_reset_and_stats();
    if (result != 0) {
        return result;
    }

    result = test_stats_snapshot_contract();
    if (result != 0) {
        return result;
    }

    result = test_debug_poisoning();
    if (result != 0) {
        return result;
    }

    return test_dispose_invalidates_arena_object();
}

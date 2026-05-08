#include "sloppy/container.h"

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

static int test_arena_array_alloc_and_copy(void)
{
    unsigned char storage[256];
    SlArena arena;
    SlSlice slice = {0};
    SlSlice copied = {0};
    int source[3] = {7, 8, 9};

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0) {
        return 1;
    }
    if (expect_status(sl_arena_array_alloc(&arena, 4U, sizeof(int), _Alignof(int), &slice),
                      SL_STATUS_OK) != 0 ||
        slice.ptr == NULL || slice.count != 4U || slice.elem_size != sizeof(int))
    {
        return 2;
    }
    if (((int*)slice.ptr)[0] != 0 || ((int*)slice.ptr)[3] != 0) {
        return 3;
    }
    if (expect_status(sl_arena_array_copy(&arena, source, 3U, sizeof(int), _Alignof(int), &copied),
                      SL_STATUS_OK) != 0 ||
        ((int*)copied.ptr)[0] != 7 || ((int*)copied.ptr)[2] != 9)
    {
        return 4;
    }
    if (expect_status(sl_arena_array_copy(&arena, NULL, 1U, sizeof(int), _Alignof(int), &copied),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 5;
    }
    return 0;
}

static int test_fixed_vec_push_pop_and_clear(void)
{
    int storage[2] = {99, 99};
    SlFixedVec vec;
    int first = 11;
    int second = 22;
    int third = 33;
    int popped = 0;
    void* slot = NULL;

    if (expect_status(sl_fixed_vec_init(&vec, storage, sizeof(int), 2U), SL_STATUS_OK) != 0 ||
        sl_fixed_vec_count(&vec) != 0U || storage[0] != 0)
    {
        return 10;
    }
    if (expect_status(sl_fixed_vec_push(&vec, &first, &slot), SL_STATUS_OK) != 0 || slot == NULL ||
        *(int*)slot != 11)
    {
        return 11;
    }
    if (expect_status(sl_fixed_vec_push(&vec, &second, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_fixed_vec_push(&vec, &third, NULL), SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        sl_fixed_vec_count(&vec) != 2U)
    {
        return 12;
    }
    if (!sl_fixed_vec_pop(&vec, &popped) || popped != 22 || storage[1] != 0 ||
        sl_fixed_vec_count(&vec) != 1U)
    {
        return 13;
    }
    sl_fixed_vec_clear(&vec);
    if (sl_fixed_vec_count(&vec) != 0U || storage[0] != 0 || storage[1] != 0) {
        return 14;
    }
    return 0;
}

static int test_ring_queue_fifo_wrap_and_pop_back(void)
{
    int storage[3] = {0};
    SlRingQueue queue;
    int value = 0;
    int out = 0;

    if (expect_status(sl_ring_queue_init(&queue, storage, sizeof(int), 3U), SL_STATUS_OK) != 0) {
        return 20;
    }
    value = 1;
    if (expect_status(sl_ring_queue_push(&queue, &value), SL_STATUS_OK) != 0) {
        return 21;
    }
    value = 2;
    if (expect_status(sl_ring_queue_push(&queue, &value), SL_STATUS_OK) != 0 ||
        !sl_ring_queue_pop_front(&queue, &out) || out != 1)
    {
        return 22;
    }
    value = 3;
    if (expect_status(sl_ring_queue_push(&queue, &value), SL_STATUS_OK) != 0) {
        return 23;
    }
    value = 4;
    if (expect_status(sl_ring_queue_push(&queue, &value), SL_STATUS_OK) != 0 ||
        sl_ring_queue_count(&queue) != 3U)
    {
        return 24;
    }
    value = 5;
    if (expect_status(sl_ring_queue_push(&queue, &value), SL_STATUS_CAPACITY_EXCEEDED) != 0) {
        return 25;
    }
    if (!sl_ring_queue_pop_back(&queue, &out) || out != 4 ||
        !sl_ring_queue_pop_front(&queue, &out) || out != 2 ||
        !sl_ring_queue_pop_front(&queue, &out) || out != 3 || sl_ring_queue_pop_front(&queue, &out))
    {
        return 26;
    }
    return 0;
}

typedef struct HashRecord
{
    size_t target;
} HashRecord;

static bool hash_equals(size_t entry_index, void* user)
{
    HashRecord* record = (HashRecord*)user;
    return record != NULL && entry_index == record->target;
}

static int test_hash_index_find_insert(void)
{
    unsigned char storage[256];
    SlArena arena;
    SlArenaHashIndex index = {0};
    HashRecord record = {0};
    size_t found = 0U;

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_hash_index_init(&index, &arena, 3U, 2U), SL_STATUS_OK) != 0)
    {
        return 30;
    }
    if (expect_status(sl_arena_hash_index_insert(&index, 7U, 1U), SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_hash_index_insert(&index, 9U, 2U), SL_STATUS_OK) != 0)
    {
        return 31;
    }
    if (expect_status(sl_arena_hash_index_insert(&index, 11U, 1U), SL_STATUS_INVALID_STATE) != 0) {
        return 35;
    }
    record.target = 2U;
    if (expect_status(sl_arena_hash_index_find(&index, 9U, hash_equals, &record, &found),
                      SL_STATUS_OK) != 0 ||
        found != 2U)
    {
        return 32;
    }
    record.target = 3U;
    if (expect_status(sl_arena_hash_index_find(&index, 9U, hash_equals, &record, &found),
                      SL_STATUS_OUT_OF_RANGE) != 0)
    {
        return 33;
    }
    sl_arena_hash_index_reset(&index);
    record.target = 1U;
    if (expect_status(sl_arena_hash_index_find(&index, 7U, hash_equals, &record, &found),
                      SL_STATUS_OUT_OF_RANGE) != 0)
    {
        return 34;
    }
    return 0;
}

int main(void)
{
    int result = test_arena_array_alloc_and_copy();
    if (result != 0) {
        return result;
    }
    result = test_fixed_vec_push_pop_and_clear();
    if (result != 0) {
        return result;
    }
    result = test_ring_queue_fifo_wrap_and_pop_back();
    if (result != 0) {
        return result;
    }
    return test_hash_index_find_insert();
}

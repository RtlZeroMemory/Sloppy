/*
 * src/core/container.c
 *
 * Implements Sloppy's standard C container primitives for arena/caller-backed storage.
 *
 * Safety invariants:
 * - all byte sizes are checked before allocation or address calculation;
 * - failed push/alloc/find calls leave caller-visible outputs unchanged unless documented;
 * - containers do not call malloc/free or OS APIs;
 * - generic copies are byte-exact and safe for trivially copyable C structs used by the C
 *   kernel.
 *
 * Tests: tests/unit/core/test_container.c.
 */
#include "sloppy/container.h"

#include "sloppy/checked_math.h"

static void sl_container_zero(void* ptr, size_t length)
{
    size_t index = 0U;
    unsigned char* bytes = (unsigned char*)ptr;

    for (index = 0U; bytes != NULL && index < length; index += 1U) {
        bytes[index] = 0U;
    }
}

static void sl_container_copy(void* restrict dest, const void* restrict src, size_t length)
{
    size_t index = 0U;
    unsigned char* dest_bytes = (unsigned char*)dest;
    const unsigned char* src_bytes = (const unsigned char*)src;

    for (index = 0U; index < length; index += 1U) {
        dest_bytes[index] = src_bytes[index];
    }
}

static SlStatus sl_container_storage_size(size_t count, size_t elem_size, size_t* out_size)
{
    if (out_size == NULL || elem_size == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    return sl_checked_array_size(count, elem_size, out_size);
}

static void* sl_container_item_at(unsigned char* items, size_t elem_size, size_t index)
{
    return items + (index * elem_size);
}

SlSlice sl_slice_empty(size_t elem_size)
{
    SlSlice slice = {NULL, 0U, elem_size};
    return slice;
}

SlConstSlice sl_const_slice_empty(size_t elem_size)
{
    SlConstSlice slice = {NULL, 0U, elem_size};
    return slice;
}

SlStatus sl_arena_array_alloc(SlArena* arena, size_t count, size_t elem_size, size_t alignment,
                              SlSlice* out)
{
    void* storage = NULL;
    size_t storage_size = 0U;
    SlStatus status;
    SlSlice result;

    if (arena == NULL || out == NULL || elem_size == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (count == 0U) {
        *out = sl_slice_empty(elem_size);
        return sl_status_ok();
    }

    status = sl_container_storage_size(count, elem_size, &storage_size);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_arena_alloc(arena, storage_size, alignment, &storage);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    sl_container_zero(storage, storage_size);
    result.ptr = storage;
    result.count = count;
    result.elem_size = elem_size;
    *out = result;
    return sl_status_ok();
}

SlStatus sl_arena_array_copy(SlArena* arena, const void* src, size_t count, size_t elem_size,
                             size_t alignment, SlSlice* out)
{
    SlSlice copied = {0};
    SlStatus status;

    if (out == NULL || elem_size == 0U || (src == NULL && count != 0U)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_arena_array_alloc(arena, count, elem_size, alignment, &copied);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (count != 0U) {
        sl_container_copy(copied.ptr, src, count * elem_size);
    }
    *out = copied;
    return sl_status_ok();
}

SlStatus sl_fixed_vec_init(SlFixedVec* vec, void* storage, size_t elem_size, size_t capacity)
{
    size_t storage_size = 0U;
    SlStatus status;

    if (vec == NULL || elem_size == 0U || (storage == NULL && capacity != 0U)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_container_storage_size(capacity, elem_size, &storage_size);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    vec->items = (unsigned char*)storage;
    vec->elem_size = elem_size;
    vec->capacity = capacity;
    vec->count = 0U;
    sl_container_zero(storage, storage_size);
    return sl_status_ok();
}

SlStatus sl_fixed_vec_init_from_arena(SlFixedVec* vec, SlArena* arena, size_t elem_size,
                                      size_t alignment, size_t capacity)
{
    SlSlice storage = {0};
    SlStatus status;

    if (vec == NULL || arena == NULL || elem_size == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_arena_array_alloc(arena, capacity, elem_size, alignment, &storage);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_fixed_vec_init(vec, storage.ptr, elem_size, capacity);
}

void sl_fixed_vec_clear(SlFixedVec* vec)
{
    size_t storage_size = 0U;

    if (vec == NULL) {
        return;
    }

    if (sl_status_is_ok(sl_container_storage_size(vec->capacity, vec->elem_size, &storage_size))) {
        sl_container_zero(vec->items, storage_size);
    }
    vec->count = 0U;
}

size_t sl_fixed_vec_count(const SlFixedVec* vec)
{
    return vec == NULL ? 0U : vec->count;
}

size_t sl_fixed_vec_capacity(const SlFixedVec* vec)
{
    return vec == NULL ? 0U : vec->capacity;
}

void* sl_fixed_vec_at(SlFixedVec* vec, size_t index)
{
    if (vec == NULL || vec->items == NULL || index >= vec->count || vec->elem_size == 0U) {
        return NULL;
    }
    return sl_container_item_at(vec->items, vec->elem_size, index);
}

const void* sl_fixed_vec_at_const(const SlFixedVec* vec, size_t index)
{
    if (vec == NULL || vec->items == NULL || index >= vec->count || vec->elem_size == 0U) {
        return NULL;
    }
    return sl_container_item_at(vec->items, vec->elem_size, index);
}

SlStatus sl_fixed_vec_push(SlFixedVec* vec, const void* item, void** out_item)
{
    void* slot = NULL;

    if (vec == NULL || item == NULL || vec->elem_size == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (vec->count >= vec->capacity) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }
    if (vec->items == NULL) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    slot = sl_container_item_at(vec->items, vec->elem_size, vec->count);
    sl_container_copy(slot, item, vec->elem_size);
    vec->count += 1U;
    if (out_item != NULL) {
        *out_item = slot;
    }
    return sl_status_ok();
}

SlStatus sl_fixed_vec_push_zero(SlFixedVec* vec, void** out_item)
{
    void* slot = NULL;

    if (vec == NULL || vec->elem_size == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (vec->count >= vec->capacity) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }
    if (vec->items == NULL) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    slot = sl_container_item_at(vec->items, vec->elem_size, vec->count);
    sl_container_zero(slot, vec->elem_size);
    vec->count += 1U;
    if (out_item != NULL) {
        *out_item = slot;
    }
    return sl_status_ok();
}

bool sl_fixed_vec_pop(SlFixedVec* vec, void* out_item)
{
    void* slot = NULL;

    if (vec == NULL || vec->count == 0U || vec->items == NULL || vec->elem_size == 0U) {
        return false;
    }

    vec->count -= 1U;
    slot = sl_container_item_at(vec->items, vec->elem_size, vec->count);
    if (out_item != NULL) {
        sl_container_copy(out_item, slot, vec->elem_size);
    }
    sl_container_zero(slot, vec->elem_size);
    return true;
}

SlSlice sl_fixed_vec_as_slice(SlFixedVec* vec)
{
    SlSlice slice = {NULL, 0U, 0U};
    if (vec == NULL) {
        return slice;
    }
    slice.ptr = vec->items;
    slice.count = vec->count;
    slice.elem_size = vec->elem_size;
    return slice;
}

SlConstSlice sl_fixed_vec_as_const_slice(const SlFixedVec* vec)
{
    SlConstSlice slice = {NULL, 0U, 0U};
    if (vec == NULL) {
        return slice;
    }
    slice.ptr = vec->items;
    slice.count = vec->count;
    slice.elem_size = vec->elem_size;
    return slice;
}

SlStatus sl_ring_queue_init(SlRingQueue* queue, void* storage, size_t elem_size, size_t capacity)
{
    SlStatus status;
    size_t storage_size = 0U;

    if (queue == NULL || elem_size == 0U || (storage == NULL && capacity != 0U)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_container_storage_size(capacity, elem_size, &storage_size);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    queue->items = (unsigned char*)storage;
    queue->elem_size = elem_size;
    queue->capacity = capacity;
    queue->head = 0U;
    queue->tail = 0U;
    queue->count = 0U;
    sl_container_zero(storage, storage_size);
    return sl_status_ok();
}

SlStatus sl_ring_queue_init_from_arena(SlRingQueue* queue, SlArena* arena, size_t elem_size,
                                       size_t alignment, size_t capacity)
{
    SlSlice storage = {0};
    SlStatus status;

    if (queue == NULL || arena == NULL || elem_size == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_arena_array_alloc(arena, capacity, elem_size, alignment, &storage);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_ring_queue_init(queue, storage.ptr, elem_size, capacity);
}

void sl_ring_queue_clear(SlRingQueue* queue)
{
    size_t storage_size = 0U;

    if (queue == NULL) {
        return;
    }
    if (sl_status_is_ok(
            sl_container_storage_size(queue->capacity, queue->elem_size, &storage_size)))
    {
        sl_container_zero(queue->items, storage_size);
    }
    queue->head = 0U;
    queue->tail = 0U;
    queue->count = 0U;
}

size_t sl_ring_queue_count(const SlRingQueue* queue)
{
    return queue == NULL ? 0U : queue->count;
}

size_t sl_ring_queue_capacity(const SlRingQueue* queue)
{
    return queue == NULL ? 0U : queue->capacity;
}

bool sl_ring_queue_is_empty(const SlRingQueue* queue)
{
    return queue == NULL || queue->count == 0U;
}

bool sl_ring_queue_is_full(const SlRingQueue* queue)
{
    return queue != NULL && queue->count >= queue->capacity;
}

SlStatus sl_ring_queue_push(SlRingQueue* queue, const void* item)
{
    void* slot = NULL;

    if (queue == NULL || item == NULL || queue->elem_size == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (queue->count >= queue->capacity) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }
    if (queue->items == NULL) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    slot = sl_container_item_at(queue->items, queue->elem_size, queue->tail);
    sl_container_copy(slot, item, queue->elem_size);
    queue->tail = (queue->tail + 1U) % queue->capacity;
    queue->count += 1U;
    return sl_status_ok();
}

bool sl_ring_queue_pop_front(SlRingQueue* queue, void* out_item)
{
    void* slot = NULL;

    if (queue == NULL || out_item == NULL || queue->count == 0U || queue->items == NULL ||
        queue->capacity == 0U || queue->elem_size == 0U)
    {
        return false;
    }

    slot = sl_container_item_at(queue->items, queue->elem_size, queue->head);
    sl_container_copy(out_item, slot, queue->elem_size);
    sl_container_zero(slot, queue->elem_size);
    queue->head = (queue->head + 1U) % queue->capacity;
    queue->count -= 1U;
    return true;
}

bool sl_ring_queue_pop_back(SlRingQueue* queue, void* out_item)
{
    void* slot = NULL;
    size_t tail = 0U;

    if (queue == NULL || out_item == NULL || queue->count == 0U || queue->items == NULL ||
        queue->capacity == 0U || queue->elem_size == 0U)
    {
        return false;
    }

    tail = (queue->tail + queue->capacity - 1U) % queue->capacity;
    slot = sl_container_item_at(queue->items, queue->elem_size, tail);
    sl_container_copy(out_item, slot, queue->elem_size);
    sl_container_zero(slot, queue->elem_size);
    queue->tail = tail;
    queue->count -= 1U;
    return true;
}

SlStatus sl_arena_hash_index_init(SlArenaHashIndex* index, SlArena* arena, size_t capacity,
                                  size_t bucket_count)
{
    SlSlice buckets = {0};
    SlSlice next_indices = {0};
    SlStatus status;

    if (index == NULL || arena == NULL || capacity == 0U || bucket_count == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_arena_array_alloc(arena, bucket_count, sizeof(size_t), _Alignof(size_t), &buckets);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_array_alloc(arena, capacity, sizeof(size_t), _Alignof(size_t), &next_indices);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    index->buckets = (size_t*)buckets.ptr;
    index->next_indices = (size_t*)next_indices.ptr;
    index->capacity = capacity;
    index->bucket_count = bucket_count;
    index->count = 0U;
    return sl_status_ok();
}

void sl_arena_hash_index_reset(SlArenaHashIndex* index)
{
    size_t bucket = 0U;
    size_t entry = 0U;

    if (index == NULL) {
        return;
    }
    for (bucket = 0U; index->buckets != NULL && bucket < index->bucket_count; bucket += 1U) {
        index->buckets[bucket] = 0U;
    }
    for (entry = 0U; index->next_indices != NULL && entry < index->capacity; entry += 1U) {
        index->next_indices[entry] = 0U;
    }
    index->count = 0U;
}

SlStatus sl_arena_hash_index_find(const SlArenaHashIndex* index, uint64_t hash,
                                  SlArenaHashIndexEqualsFn equals, void* user,
                                  size_t* out_entry_index)
{
    size_t entry_index = 0U;
    size_t bucket_index = 0U;

    if (index == NULL || equals == NULL || out_entry_index == NULL || index->buckets == NULL ||
        index->next_indices == NULL || index->bucket_count == 0U)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    bucket_index = (size_t)(hash % (uint64_t)index->bucket_count);
    entry_index = index->buckets[bucket_index];
    while (entry_index != 0U) {
        if (entry_index > index->capacity) {
            return sl_status_from_code(SL_STATUS_INVALID_STATE);
        }
        if (equals(entry_index, user)) {
            *out_entry_index = entry_index;
            return sl_status_ok();
        }
        entry_index = index->next_indices[entry_index - 1U];
    }

    return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
}

SlStatus sl_arena_hash_index_insert(SlArenaHashIndex* index, uint64_t hash, size_t entry_index)
{
    size_t bucket_index = 0U;

    if (index == NULL || index->buckets == NULL || index->next_indices == NULL ||
        index->bucket_count == 0U || entry_index == 0U || entry_index > index->capacity)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (index->count >= index->capacity) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }

    bucket_index = (size_t)(hash % (uint64_t)index->bucket_count);
    index->next_indices[entry_index - 1U] = index->buckets[bucket_index];
    index->buckets[bucket_index] = entry_index;
    index->count += 1U;
    return sl_status_ok();
}

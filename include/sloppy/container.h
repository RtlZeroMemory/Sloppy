#ifndef SLOPPY_CONTAINER_H
#define SLOPPY_CONTAINER_H

#include "sloppy/arena.h"
#include "sloppy/status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Generic C containers for Sloppy's arena/caller-backed memory model.
 *
 * These primitives do not allocate from the OS, do not own their backing arena, and do not
 * expose typed C++-style templates. Callers keep public typed APIs readable by wrapping
 * these byte-oriented primitives at module boundaries.
 */
typedef struct SlSlice
{
    void* ptr;
    size_t count;
    size_t elem_size;
} SlSlice;

typedef struct SlConstSlice
{
    const void* ptr;
    size_t count;
    size_t elem_size;
} SlConstSlice;

typedef struct SlFixedVec
{
    unsigned char* items;
    size_t elem_size;
    size_t capacity;
    size_t count;
} SlFixedVec;

typedef struct SlRingQueue
{
    unsigned char* items;
    size_t elem_size;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
} SlRingQueue;

typedef struct SlArenaHashIndex
{
    size_t* buckets;
    size_t* next_indices;
    size_t capacity;
    size_t bucket_count;
    size_t count;
} SlArenaHashIndex;

typedef bool (*SlArenaHashIndexEqualsFn)(size_t entry_index, void* user);

SlSlice sl_slice_empty(size_t elem_size);
SlConstSlice sl_const_slice_empty(size_t elem_size);

/*
 * Allocates `count * elem_size` bytes from `arena`.
 *
 * Zero-count arrays succeed and return an empty slice with NULL storage. Non-zero arrays
 * are zero-initialized so callers do not need a second cleanup loop before publishing.
 */
SlStatus sl_arena_array_alloc(SlArena* arena, size_t count, size_t elem_size, size_t alignment,
                              SlSlice* out);
SlStatus sl_arena_array_copy(SlArena* arena, const void* src, size_t count, size_t elem_size,
                             size_t alignment, SlSlice* out);

SlStatus sl_fixed_vec_init(SlFixedVec* vec, void* storage, size_t elem_size, size_t capacity);
SlStatus sl_fixed_vec_init_from_arena(SlFixedVec* vec, SlArena* arena, size_t elem_size,
                                      size_t alignment, size_t capacity);
void sl_fixed_vec_clear(SlFixedVec* vec);
size_t sl_fixed_vec_count(const SlFixedVec* vec);
size_t sl_fixed_vec_capacity(const SlFixedVec* vec);
void* sl_fixed_vec_at(SlFixedVec* vec, size_t index);
const void* sl_fixed_vec_at_const(const SlFixedVec* vec, size_t index);
SlStatus sl_fixed_vec_push(SlFixedVec* vec, const void* item, void** out_item);
SlStatus sl_fixed_vec_push_zero(SlFixedVec* vec, void** out_item);
bool sl_fixed_vec_pop(SlFixedVec* vec, void* out_item);
SlSlice sl_fixed_vec_as_slice(SlFixedVec* vec);
SlConstSlice sl_fixed_vec_as_const_slice(const SlFixedVec* vec);

SlStatus sl_ring_queue_init(SlRingQueue* queue, void* storage, size_t elem_size, size_t capacity);
SlStatus sl_ring_queue_init_from_arena(SlRingQueue* queue, SlArena* arena, size_t elem_size,
                                       size_t alignment, size_t capacity);
void sl_ring_queue_clear(SlRingQueue* queue);
size_t sl_ring_queue_count(const SlRingQueue* queue);
size_t sl_ring_queue_capacity(const SlRingQueue* queue);
bool sl_ring_queue_is_empty(const SlRingQueue* queue);
bool sl_ring_queue_is_full(const SlRingQueue* queue);
SlStatus sl_ring_queue_push(SlRingQueue* queue, const void* item);
bool sl_ring_queue_peek_front(const SlRingQueue* queue, void* out_item);
bool sl_ring_queue_discard_front(SlRingQueue* queue);
bool sl_ring_queue_pop_front(SlRingQueue* queue, void* out_item);
bool sl_ring_queue_pop_back(SlRingQueue* queue, void* out_item);

/*
 * Bounded index hash for arena-owned tables.
 *
 * Entries are addressed by one-based indices supplied by the caller. The hash index owns
 * only bucket/next-index storage; the caller owns the parallel entry array and supplies
 * equality for a candidate one-based entry index.
 */
SlStatus sl_arena_hash_index_init(SlArenaHashIndex* index, SlArena* arena, size_t capacity,
                                  size_t bucket_count);
void sl_arena_hash_index_reset(SlArenaHashIndex* index);
SlStatus sl_arena_hash_index_find(const SlArenaHashIndex* index, uint64_t hash,
                                  SlArenaHashIndexEqualsFn equals, void* user,
                                  size_t* out_entry_index);
SlStatus sl_arena_hash_index_insert(SlArenaHashIndex* index, uint64_t hash, size_t entry_index);

#ifdef __cplusplus
}
#endif

#endif

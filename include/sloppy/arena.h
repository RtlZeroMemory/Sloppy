#ifndef SLOPPY_ARENA_H
#define SLOPPY_ARENA_H

#include "sloppy/status.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SlArena is a caller-backed scoped allocator.
 *
 * The arena does not own `base`; the caller must keep the backing buffer alive for the
 * arena lifetime. Pointers returned by sl_arena_alloc remain valid until sl_arena_reset(),
 * sl_arena_reset_to() invalidates their offset range, or the backing buffer lifetime ends.
 *
 * Arena memory is for scoped data, not independently closable resources. Data that crosses
 * async boundaries must only point into an arena that outlives the async operation. SlArena
 * is not thread-safe unless callers provide external synchronization.
 *
 * This is not a general allocator interface. It intentionally has no OS allocation,
 * malloc-backed factory, vtable, registry, or ownership transfer behavior.
 */
typedef struct SlArena
{
    unsigned char* base;
    size_t capacity;
    size_t offset;
    size_t high_water;
    unsigned int generation;
} SlArena;

typedef struct SlArenaMark
{
    size_t offset;
    unsigned int generation;
} SlArenaMark;

typedef struct SlArenaStats
{
    size_t capacity;
    size_t used;
    size_t remaining;
    size_t high_water;
    unsigned int generation;
} SlArenaStats;

/*
 * Initializes `arena` over caller-owned `buffer`.
 *
 * `arena` is required. `buffer` is required when `capacity` is nonzero. Zero-capacity
 * arenas are allowed and may use a NULL buffer, but no allocation can succeed from them.
 */
SlStatus sl_arena_init(SlArena* arena, void* buffer, size_t capacity);

/*
 * Resets the arena to empty. Existing arena allocations become invalid.
 *
 * High-water statistics are preserved. Old marks are invalidated in all builds.
 */
void sl_arena_reset(SlArena* arena);

/*
 * Disposes the arena object without freeing caller-owned backing storage.
 *
 * Existing arena allocations become invalid by contract. The caller still owns `base` and
 * may reuse or release it independently. Passing NULL is allowed.
 */
void sl_arena_dispose(SlArena* arena);

/*
 * Captures the current arena offset for a later sl_arena_reset_to().
 *
 * Passing NULL returns a mark that reset_to will reject.
 */
SlArenaMark sl_arena_mark(const SlArena* arena);

/*
 * Resets to a previously captured mark.
 *
 * Marks must belong to the arena's current generation and must not point beyond the
 * arena's current used bytes. Marks captured before a full reset are stale and rejected.
 */
SlStatus sl_arena_reset_to(SlArena* arena, SlArenaMark mark);

/*
 * Allocates `size` bytes aligned to `alignment`.
 *
 * `arena` and `out` are required. `alignment` must be a nonzero power of two. `size` must
 * be nonzero; zero-size allocations are rejected so callers cannot accidentally depend on
 * ambiguous pointer identity or lifetime for an allocation that owns no bytes.
 *
 * On success, `*out` receives an arena-owned pointer. On failure, `*out` is left unchanged.
 */
SlStatus sl_arena_alloc(SlArena* arena, size_t size, size_t alignment, void** out);

size_t sl_arena_capacity(const SlArena* arena);
size_t sl_arena_used(const SlArena* arena);
size_t sl_arena_remaining(const SlArena* arena);
size_t sl_arena_high_water(const SlArena* arena);
SlArenaStats sl_arena_stats(const SlArena* arena);

#ifdef __cplusplus
}
#endif

#endif

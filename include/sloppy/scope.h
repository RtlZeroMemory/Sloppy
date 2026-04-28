#ifndef SLOPPY_SCOPE_H
#define SLOPPY_SCOPE_H

#include "sloppy/arena.h"
#include "sloppy/status.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SlScope groups native cleanup callbacks for deterministic close-time cleanup.
 *
 * The scope owns cleanup registrations only. It never owns callback payloads or user data:
 * those pointers are borrowed and interpreted solely by the cleanup callback. Payload and
 * user pointers may be NULL.
 *
 * Cleanup storage is caller-owned unless sl_scope_init_from_arena() is used, in which case
 * the storage is arena-owned and valid until the arena resets or its backing buffer ends.
 * SlScope is not thread-safe or reentrant; callers provide external synchronization and must
 * not mutate the same scope from cleanup callbacks.
 */
typedef void (*SlScopeCleanupFn)(void* payload, void* user);

typedef struct SlScopeCleanup
{
    SlScopeCleanupFn fn;
    void* payload;
    void* user;
} SlScopeCleanup;

typedef struct SlScope
{
    SlScopeCleanup* cleanups;
    size_t capacity;
    size_t count;
    bool closed;
} SlScope;

/*
 * Initializes `scope` over caller-owned cleanup storage.
 *
 * `scope` is required. `storage` is required when `capacity` is nonzero. A zero-capacity
 * scope is valid and useful for exercising exhaustion behavior, but registration cannot
 * succeed until the scope is reinitialized with storage.
 */
SlStatus sl_scope_init(SlScope* scope, SlScopeCleanup* storage, size_t capacity);

/*
 * Initializes `scope` with cleanup storage allocated from `arena`.
 *
 * The scope does not own the arena. The arena allocation remains valid only until the arena
 * is reset, reset to a mark before the storage allocation, or its backing buffer ends.
 */
SlStatus sl_scope_init_from_arena(SlScope* scope, SlArena* arena, size_t cleanup_capacity);

/*
 * Registers a cleanup callback.
 *
 * `scope` and `fn` are required. `payload` and `user` may be NULL. Registration preserves
 * insertion order internally; sl_scope_close() invokes callbacks in reverse order. Failed
 * registration does not change the existing cleanup count or overwrite existing callbacks.
 */
SlStatus sl_scope_add_cleanup(SlScope* scope, SlScopeCleanupFn fn, void* payload, void* user);

/*
 * Closes the scope and runs registered cleanup callbacks once in LIFO order.
 *
 * Closing an already closed scope is idempotent and returns OK without invoking callbacks.
 * Passing NULL returns SL_STATUS_INVALID_ARGUMENT.
 */
SlStatus sl_scope_close(SlScope* scope);

/*
 * Clears registrations without invoking callbacks and marks the scope open for reuse.
 *
 * Reset is for callers that know the registered cleanup actions are not needed, or for
 * reusing a scope after it has been closed. Passing NULL is a no-op.
 */
void sl_scope_reset(SlScope* scope);

size_t sl_scope_cleanup_count(const SlScope* scope);
size_t sl_scope_cleanup_capacity(const SlScope* scope);
bool sl_scope_is_closed(const SlScope* scope);

#ifdef __cplusplus
}
#endif

#endif

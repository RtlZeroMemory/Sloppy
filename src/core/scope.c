/*
 * src/core/scope.c
 *
 * Implements Sloppy's first native cleanup scope primitive.
 *
 * Safety invariants:
 * - cleanup storage is caller-owned or explicitly arena-owned;
 * - no raw allocation or platform API is used;
 * - registration fails without mutating existing callbacks when the scope is closed or full;
 * - close is deterministic LIFO and each registered callback is invoked at most once.
 *
 * Tests: tests/unit/core/test_scope.c.
 */
#include "sloppy/scope.h"

SlStatus sl_scope_init(SlScope* scope, SlScopeCleanup* storage, size_t capacity)
{
    SlStatus status;

    if (scope == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (storage == NULL && capacity != 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_fixed_vec_init(&scope->cleanups, storage, sizeof(SlScopeCleanup), capacity);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    scope->closed = false;

    return sl_status_ok();
}

SlStatus sl_scope_init_from_arena(SlScope* scope, SlArena* arena, size_t cleanup_capacity)
{
    SlStatus status;

    if (scope == NULL || arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_fixed_vec_init_from_arena(&scope->cleanups, arena, sizeof(SlScopeCleanup),
                                          _Alignof(SlScopeCleanup), cleanup_capacity);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    scope->closed = false;
    return sl_status_ok();
}

SlStatus sl_scope_add_cleanup(SlScope* scope, SlScopeCleanupFn fn, void* payload, void* user)
{
    SlScopeCleanup cleanup;

    if (scope == NULL || fn == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (scope->closed) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    cleanup.fn = fn;
    cleanup.payload = payload;
    cleanup.user = user;

    return sl_fixed_vec_push(&scope->cleanups, &cleanup, NULL);
}

SlStatus sl_scope_close(SlScope* scope)
{
    SlScopeCleanup cleanup;

    if (scope == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (scope->closed) {
        return sl_status_ok();
    }

    scope->closed = true;

    while (sl_fixed_vec_pop(&scope->cleanups, &cleanup)) {
        cleanup.fn(cleanup.payload, cleanup.user);
    }

    return sl_status_ok();
}

void sl_scope_reset(SlScope* scope)
{
    if (scope == NULL) {
        return;
    }

    sl_fixed_vec_clear(&scope->cleanups);

    scope->closed = false;
}

size_t sl_scope_cleanup_count(const SlScope* scope)
{
    return sl_fixed_vec_count(scope == NULL ? NULL : &scope->cleanups);
}

size_t sl_scope_cleanup_capacity(const SlScope* scope)
{
    return sl_fixed_vec_capacity(scope == NULL ? NULL : &scope->cleanups);
}

bool sl_scope_is_closed(const SlScope* scope)
{
    return scope != NULL && scope->closed;
}

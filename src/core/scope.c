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

#include "sloppy/checked_math.h"

SlStatus sl_scope_init(SlScope* scope, SlScopeCleanup* storage, size_t capacity)
{
    if (scope == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (storage == NULL && capacity != 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    scope->cleanups = storage;
    scope->capacity = capacity;
    scope->count = 0U;
    scope->closed = false;

    return sl_status_ok();
}

SlStatus sl_scope_init_from_arena(SlScope* scope, SlArena* arena, size_t cleanup_capacity)
{
    SlStatus status;
    size_t storage_size = 0U;
    void* storage = NULL;

    if (scope == NULL || arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (cleanup_capacity == 0U) {
        return sl_scope_init(scope, NULL, 0U);
    }

    status = sl_checked_mul_size(cleanup_capacity, sizeof(SlScopeCleanup), &storage_size);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_arena_alloc(arena, storage_size, _Alignof(SlScopeCleanup), &storage);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_scope_init(scope, (SlScopeCleanup*)storage, cleanup_capacity);
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

    if (scope->count >= scope->capacity) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }

    if (scope->cleanups == NULL) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    cleanup.fn = fn;
    cleanup.payload = payload;
    cleanup.user = user;

    scope->cleanups[scope->count] = cleanup;
    scope->count += 1U;

    return sl_status_ok();
}

SlStatus sl_scope_close(SlScope* scope)
{
    if (scope == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (scope->closed) {
        return sl_status_ok();
    }

    scope->closed = true;

    while (scope->count != 0U) {
        SlScopeCleanup cleanup;

        scope->count -= 1U;
        cleanup = scope->cleanups[scope->count];
        scope->cleanups[scope->count].fn = NULL;
        scope->cleanups[scope->count].payload = NULL;
        scope->cleanups[scope->count].user = NULL;

        cleanup.fn(cleanup.payload, cleanup.user);
    }

    return sl_status_ok();
}

void sl_scope_reset(SlScope* scope)
{
    if (scope == NULL) {
        return;
    }

    while (scope->count != 0U) {
        scope->count -= 1U;
        scope->cleanups[scope->count].fn = NULL;
        scope->cleanups[scope->count].payload = NULL;
        scope->cleanups[scope->count].user = NULL;
    }

    scope->closed = false;
}

size_t sl_scope_cleanup_count(const SlScope* scope)
{
    return scope == NULL ? 0U : scope->count;
}

size_t sl_scope_cleanup_capacity(const SlScope* scope)
{
    return scope == NULL ? 0U : scope->capacity;
}

bool sl_scope_is_closed(const SlScope* scope)
{
    return scope != NULL && scope->closed;
}

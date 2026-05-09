#ifndef SLOPPY_ASYNC_BACKEND_H
#define SLOPPY_ASYNC_BACKEND_H

#include "sloppy/arena.h"
#include "sloppy/diagnostics.h"
#include "sloppy/status.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SlAsyncLoop SlAsyncLoop;
typedef struct SlAsyncCompletion SlAsyncCompletion;
typedef struct SlAsyncIoWatch SlAsyncIoWatch;

typedef enum SlAsyncBackendKind
{
    SL_ASYNC_BACKEND_NONE = 0,
    SL_ASYNC_BACKEND_LIBUV = 1,
    SL_ASYNC_BACKEND_TEST = 2
} SlAsyncBackendKind;

typedef enum SlAsyncCompletionKind
{
    SL_ASYNC_COMPLETION_NONE = 0,
    SL_ASYNC_COMPLETION_TEST = 1,
    SL_ASYNC_COMPLETION_NATIVE = 2,
    SL_ASYNC_COMPLETION_V8_CONTINUATION = 3
} SlAsyncCompletionKind;

typedef enum SlAsyncOperationKind
{
    SL_ASYNC_OPERATION_INTERNAL_COMPLETION = 0,
    SL_ASYNC_OPERATION_NONBLOCKING_IO = 1,
    SL_ASYNC_OPERATION_BLOCKING_OFFLOAD = 2,
    SL_ASYNC_OPERATION_TIMER = 3,
    SL_ASYNC_OPERATION_PROVIDER = 4
} SlAsyncOperationKind;

typedef enum SlAsyncIoEvent
{
    SL_ASYNC_IO_EVENT_READABLE = 1U << 0U,
    SL_ASYNC_IO_EVENT_WRITABLE = 1U << 1U,
    SL_ASYNC_IO_EVENT_DISCONNECT = 1U << 2U
} SlAsyncIoEvent;

typedef SlStatus (*SlAsyncCompletionDispatchFn)(SlAsyncLoop* loop,
                                                const SlAsyncCompletion* completion, void* user);
typedef void (*SlAsyncIoWatchFn)(SlAsyncLoop* loop, SlAsyncIoWatch* watch, unsigned events,
                                 SlStatus status, void* user);
typedef void (*SlAsyncCompletionCleanupFn)(const SlAsyncCompletion* completion, void* user);
typedef bool (*SlAsyncCompletionTerminalCheckFn)(const SlAsyncCompletion* completion, void* user);
typedef void (*SlAsyncCompletionLateFn)(const SlAsyncCompletion* completion, void* user);
typedef SlStatus (*SlAsyncScopeRetainFn)(void* scope, void* user);
typedef void (*SlAsyncScopeReleaseFn)(void* scope, void* user);

typedef struct SlAsyncScopeRef
{
    void* scope;
    SlAsyncScopeRetainFn retain;
    SlAsyncScopeReleaseFn release;
    void* user;
} SlAsyncScopeRef;

struct SlAsyncCompletion
{
    SlAsyncCompletionKind kind;
    SlAsyncOperationKind operation_kind;
    SlStatus status;
    const SlDiag* diag;
    void* payload;
    void* operation;
    SlAsyncCompletionTerminalCheckFn terminal_check;
    void* terminal_check_user;
    SlAsyncScopeRef scope;
    SlAsyncCompletionDispatchFn dispatch;
    void* dispatch_user;
    SlAsyncCompletionLateFn late;
    void* late_user;
    SlAsyncCompletionCleanupFn cleanup;
    void* cleanup_user;
};

/*
 * Creates a Sloppy-owned async backend loop over caller-owned completion storage.
 *
 * The returned loop is arena-owned and must be disposed before the arena is reset. The
 * completion queue is fixed-capacity and bounded for every backend. Posting transfers
 * completion ownership only on success; failed posts leave cleanup to the caller. Queued
 * completion payloads must point to owned/stable native memory, not borrowed request views
 * that can disappear before owner-thread dispatch.
 */
SlStatus sl_async_loop_create(SlAsyncBackendKind kind, SlArena* arena, SlAsyncCompletion* storage,
                              size_t capacity, SlAsyncLoop** out_loop);

/*
 * Disposes a loop and discards pending completions.
 *
 * Pending owned completions run their cleanup callback and scope release exactly once.
 * If a completion carries `terminal_check` and that check reports terminal during
 * owner-thread drain, dispatch is skipped, the optional late-completion hook runs, and
 * cleanup/scope release still run exactly once. This lets request/app/resource owners make
 * late completions cleanup-only without letting the async queue touch closed state.
 */
void sl_async_loop_dispose(SlAsyncLoop* loop);

/*
 * Posts one native completion.
 *
 * This API is safe to call from non-owner threads when the backend supports it. The libuv
 * backend uses a thread-safe wakeup internally; the deterministic test backend is intended
 * for single-threaded unit tests. Overflow is deterministic and returns
 * SL_STATUS_CAPACITY_EXCEEDED without taking ownership.
 */
SlStatus sl_async_loop_post(SlAsyncLoop* loop, const SlAsyncCompletion* completion);

/*
 * Dispatches completions on the owner thread.
 *
 * run_once dispatches at most one completion. drain dispatches until the queue is empty or
 * `max_count` completions have run; max_count == 0 means drain until idle. Dispatching from
 * a detectable non-owner thread returns SL_STATUS_INVALID_STATE before invoking callbacks.
 */
SlStatus sl_async_loop_run_once(SlAsyncLoop* loop, size_t* out_ran);
SlStatus sl_async_loop_drain(SlAsyncLoop* loop, size_t max_count, size_t* out_ran);

SlAsyncBackendKind sl_async_loop_backend_kind(const SlAsyncLoop* loop);
size_t sl_async_loop_pending_count(const SlAsyncLoop* loop);
size_t sl_async_loop_capacity(const SlAsyncLoop* loop);
bool sl_async_loop_is_owner_thread(const SlAsyncLoop* loop);
bool sl_async_loop_is_disposed(const SlAsyncLoop* loop);

/*
 * Registers a Sloppy-owned readiness watch for a native dependency socket.
 *
 * This is intentionally narrower than exposing libuv: callers pass the integer socket
 * descriptor returned by their dependency boundary, receive Slop event bits, and keep all
 * libuv handles inside the platform backend. The callback runs on the loop owner thread.
 * The deterministic test backend returns unsupported; production socket readiness requires
 * the libuv backend. `watch` is arena-owned and must be stopped before the owning resource
 * is discarded.
 */
SlStatus sl_async_io_watch_start(SlAsyncLoop* loop, SlArena* arena, int socket, unsigned events,
                                 SlAsyncIoWatchFn callback, void* user, SlAsyncIoWatch** out_watch);
SlStatus sl_async_io_watch_update(SlAsyncIoWatch* watch, unsigned events);
void sl_async_io_watch_stop(SlAsyncIoWatch* watch);

#ifdef __cplusplus
}
#endif

#endif

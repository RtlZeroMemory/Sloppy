#ifndef SLOPPY_ASYNC_BACKEND_INTERNAL_H
#define SLOPPY_ASYNC_BACKEND_INTERNAL_H

#include "sloppy/async_backend.h"

struct SlAsyncLoop
{
    SlAsyncBackendKind kind;
    SlArena* arena;
    SlAsyncCompletion* queue;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    bool disposed;
    bool draining;
    void* backend;
};

SlStatus sl_async_loop_common_init(SlAsyncLoop* loop, SlAsyncBackendKind kind, SlArena* arena,
                                   SlAsyncCompletion* storage, size_t capacity);
SlStatus sl_async_loop_enqueue_owned(SlAsyncLoop* loop, const SlAsyncCompletion* completion);
bool sl_async_loop_unenqueue_last_owned(SlAsyncLoop* loop, SlAsyncCompletion* out_completion);
bool sl_async_loop_pop(SlAsyncLoop* loop, SlAsyncCompletion* out_completion);
SlStatus sl_async_loop_dispatch_completion(SlAsyncLoop* loop, const SlAsyncCompletion* completion);
void sl_async_loop_finish_completion(const SlAsyncCompletion* completion);
void sl_async_loop_release_completion_scope(const SlAsyncCompletion* completion);
void sl_async_loop_discard_pending_unlocked(SlAsyncLoop* loop);

SlStatus sl_async_loop_libuv_init(SlAsyncLoop* loop, SlArena* arena);
void sl_async_loop_libuv_dispose(SlAsyncLoop* loop);
SlStatus sl_async_loop_libuv_post(SlAsyncLoop* loop, const SlAsyncCompletion* completion);
SlStatus sl_async_loop_libuv_run_once(SlAsyncLoop* loop, size_t* out_ran);
SlStatus sl_async_loop_libuv_drain(SlAsyncLoop* loop, size_t max_count, size_t* out_ran);
bool sl_async_loop_libuv_is_owner_thread(const SlAsyncLoop* loop);

#endif

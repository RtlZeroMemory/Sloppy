/*
 * src/engine/v8/async_scheduler.cc
 *
 * Internal owner-thread scheduler for V8 native async completions. Native code may post a
 * completion through SlAsyncLoop from any supported backend thread, but this file is the
 * only place where that completion is allowed to touch V8 Promise state.
 */
#include "async_scheduler.h"
#include "string_interop.h"

#include <mutex>
#include <new>
#include <thread>

namespace {

SlV8Engine* sl_v8_scheduler_backend(SlEngine* engine)
{
    return engine == nullptr ? nullptr : static_cast<SlV8Engine*>(engine->backend);
}

bool sl_v8_scheduler_on_owner_thread(const SlV8Engine* backend)
{
    return backend != nullptr && backend->owner_thread == std::this_thread::get_id();
}

SlStatus sl_v8_native_continuation_dispatch(SlAsyncLoop* loop, const SlAsyncCompletion* completion,
                                            void* user)
{
    SlV8NativeContinuation* continuation = static_cast<SlV8NativeContinuation*>(user);
    SlV8Engine* backend =
        continuation == nullptr ? nullptr : sl_v8_scheduler_backend(continuation->engine);

    (void)loop;
    if (completion == nullptr || continuation == nullptr || backend == nullptr ||
        backend->isolate == nullptr)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (!sl_v8_scheduler_on_owner_thread(backend)) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    v8::Isolate* isolate = backend->isolate;
    std::string text;
    bool reject = false;
    {
        std::lock_guard<std::mutex> lock(continuation->mutex);
        if (!continuation->queued) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        text = continuation->text;
        reject = continuation->reject;
    }

    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = backend->context.Get(isolate);
    v8::Context::Scope context_scope(context);
    v8::Local<v8::Promise::Resolver> resolver = continuation->resolver.Get(isolate);
    v8::Local<v8::String> local_text;
    SlStatus string_status = sl_v8_string_from_native_view(
        backend, sl_str_from_parts(text.data(), text.size()), &local_text);
    if (!sl_status_is_ok(string_status)) {
        return string_status;
    }

    if (!sl_status_is_ok(completion->status) || reject) {
        v8::Local<v8::Value> error = v8::Exception::Error(local_text);
        if (!resolver->Reject(context, error).FromMaybe(false)) {
            return sl_status_from_code(SL_STATUS_INVALID_STATE);
        }
    }
    else if (!resolver->Resolve(context, local_text).FromMaybe(false)) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    isolate->PerformMicrotaskCheckpoint();
    {
        std::lock_guard<std::mutex> lock(continuation->mutex);
        continuation->settled = true;
    }
    return sl_status_ok();
}

void sl_v8_native_continuation_cleanup(const SlAsyncCompletion* completion, void* user)
{
    SlV8NativeContinuation* continuation = static_cast<SlV8NativeContinuation*>(user);

    (void)completion;
    if (continuation == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(continuation->mutex);
    continuation->cleanup_ran = true;
    continuation->queued = false;
    continuation->resolver_ready = false;
    continuation->resolver.Reset();
}

} // namespace

SlStatus sl_v8_native_continuation_prepare(SlEngine* engine, SlV8NativeContinuation* continuation,
                                           v8::Local<v8::Promise>* out_promise)
{
    SlV8Engine* backend = sl_v8_scheduler_backend(engine);

    if (engine == nullptr || continuation == nullptr || out_promise == nullptr ||
        backend == nullptr || backend->isolate == nullptr)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_promise = v8::Local<v8::Promise>();
    if (!sl_v8_scheduler_on_owner_thread(backend)) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    v8::Isolate* isolate = backend->isolate;
    v8::Local<v8::Context> context = backend->context.Get(isolate);
    v8::Local<v8::Promise::Resolver> resolver;
    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    continuation->engine = engine;
    {
        std::lock_guard<std::mutex> lock(continuation->mutex);
        continuation->resolver.Reset(isolate, resolver);
        continuation->text.clear();
        continuation->reject = false;
        continuation->queued = false;
        continuation->settled = false;
        continuation->cleanup_ran = false;
        continuation->resolver_ready = true;
    }
    *out_promise = resolver->GetPromise();
    return sl_status_ok();
}

SlStatus sl_v8_native_continuation_post(SlAsyncLoop* loop, SlV8NativeContinuation* continuation,
                                        SlStatus status, const char* text, bool reject,
                                        SlAsyncScopeRef scope)
{
    SlAsyncCompletion completion;
    SlStatus post_status;

    if (loop == nullptr || continuation == nullptr || text == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    std::lock_guard<std::mutex> lock(continuation->mutex);
    if (!continuation->resolver_ready) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (continuation->queued || continuation->settled || continuation->cleanup_ran) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    continuation->text = text;
    continuation->reject = reject;
    continuation->queued = true;
    continuation->settled = false;
    continuation->cleanup_ran = false;

    completion = SlAsyncCompletion{};
    completion.kind = SL_ASYNC_COMPLETION_V8_CONTINUATION;
    completion.status = status;
    completion.payload = continuation;
    completion.scope = scope;
    completion.dispatch = sl_v8_native_continuation_dispatch;
    completion.dispatch_user = continuation;
    completion.cleanup = sl_v8_native_continuation_cleanup;
    completion.cleanup_user = continuation;

    post_status = sl_async_loop_post(loop, &completion);
    if (!sl_status_is_ok(post_status)) {
        continuation->text.clear();
        continuation->reject = false;
        continuation->queued = false;
        continuation->settled = false;
        continuation->cleanup_ran = false;
        return post_status;
    }

    return sl_status_ok();
}

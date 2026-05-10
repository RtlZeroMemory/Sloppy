/*
 * src/engine/v8/intrinsics_time.cc
 *
 * Installs the V8-internal time bridge under __sloppy.time. The Time scheduler never
 * enters V8; it posts owned completions to SlAsyncLoop, and Promise settlement happens
 * only on the owning isolate thread.
 */
#include "engine_v8_internal.h"
#include "string_interop.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <new>
#include <thread>

struct SlV8TimeRequest
{
    SlV8Engine* backend = nullptr;
    v8::Global<v8::Promise::Resolver> resolver;
    std::atomic_bool cancelled = false;
    std::atomic_bool completion_posted = false;
    bool settled = false;
    uint64_t delay_ms = 0U;
    uint64_t due_ms = 0U;
};

namespace {

struct TimeV8CompletionPayload
{
    std::shared_ptr<SlV8TimeRequest> request;
};

SlStatus time_v8_to_local_string(v8::Isolate* isolate, SlStr str, v8::Local<v8::String>* out)
{
    SlV8Engine* backend =
        isolate == nullptr ? nullptr : static_cast<SlV8Engine*>(isolate->GetData(0));
    return sl_v8_string_from_native_view(backend, str, out);
}

void time_v8_throw_type_error(v8::Isolate* isolate, const char* message)
{
    v8::Local<v8::String> local_message;
    if (!sl_status_is_ok(
            time_v8_to_local_string(isolate, sl_str_from_cstr(message), &local_message)))
    {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "Sloppy time type error")));
        return;
    }
    isolate->ThrowException(v8::Exception::TypeError(local_message));
}

uint64_t time_v8_monotonic_ms()
{
    using Clock = std::chrono::steady_clock;
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch())
            .count());
}

bool time_v8_delay_arg(v8::Local<v8::Value> value, uint64_t* out_ms)
{
    if (out_ms == nullptr || value.IsEmpty() || !value->IsNumber()) {
        return false;
    }

    double number = value.As<v8::Number>()->Value();
    if (!std::isfinite(number) || number < 0.0 || number > static_cast<double>(UINT32_MAX)) {
        return false;
    }

    *out_ms = static_cast<uint64_t>(std::ceil(number));
    return true;
}

void time_v8_remove_request(const std::shared_ptr<SlV8TimeRequest>& request)
{
    SlV8Engine* backend = request == nullptr ? nullptr : request->backend;
    if (backend == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(backend->time_mutex);
    auto& requests = backend->time_requests;
    requests.erase(std::remove(requests.begin(), requests.end(), request), requests.end());
}

SlStatus time_v8_completion_dispatch(SlAsyncLoop* loop, const SlAsyncCompletion* completion,
                                     void* user)
{
    TimeV8CompletionPayload* payload =
        completion == nullptr ? nullptr
                              : static_cast<TimeV8CompletionPayload*>(completion->payload);
    std::shared_ptr<SlV8TimeRequest> request = payload == nullptr ? nullptr : payload->request;
    SlV8Engine* backend = request == nullptr ? nullptr : request->backend;

    (void)loop;
    (void)user;
    if (request == nullptr || backend == nullptr || backend->isolate == nullptr ||
        request->resolver.IsEmpty())
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (backend->owner_thread != std::this_thread::get_id()) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    v8::Isolate* isolate = backend->isolate;
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = backend->context.Get(isolate);
    v8::Context::Scope context_scope(context);
    v8::Local<v8::Promise::Resolver> resolver = request->resolver.Get(isolate);
    bool ok = false;

    if (request->cancelled.load() || !sl_status_is_ok(completion->status)) {
        v8::Local<v8::String> message =
            v8::String::NewFromUtf8Literal(isolate, "Sloppy timer was disposed before completion");
        ok = resolver->Reject(context, v8::Exception::Error(message)).FromMaybe(false);
    }
    else {
        ok = resolver->Resolve(context, v8::Undefined(isolate)).FromMaybe(false);
    }

    request->settled = ok;
    isolate->PerformMicrotaskCheckpoint();
    return ok ? sl_status_ok() : sl_status_from_code(SL_STATUS_INVALID_STATE);
}

void time_v8_completion_cleanup(const SlAsyncCompletion* completion, void* user)
{
    TimeV8CompletionPayload* payload =
        completion == nullptr ? nullptr
                              : static_cast<TimeV8CompletionPayload*>(completion->payload);
    std::shared_ptr<SlV8TimeRequest> request = payload == nullptr ? nullptr : payload->request;

    (void)user;
    if (request != nullptr) {
        request->resolver.Reset();
        request->completion_posted.store(false);
        time_v8_remove_request(request);
    }
    delete payload;
}

bool time_v8_post_completion(const std::shared_ptr<SlV8TimeRequest>& request)
{
    SlV8Engine* backend = request == nullptr ? nullptr : request->backend;
    if (backend == nullptr) {
        return false;
    }

    while (!request->cancelled.load()) {
        {
            std::lock_guard<std::mutex> lock(backend->time_mutex);
            if (backend->time_shutting_down || backend->async_loop == nullptr) {
                request->cancelled.store(true);
                return false;
            }
        }

        TimeV8CompletionPayload* payload = new (std::nothrow) TimeV8CompletionPayload();
        if (payload == nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        payload->request = request;

        SlAsyncCompletion completion = {};
        completion.kind = SL_ASYNC_COMPLETION_V8_CONTINUATION;
        completion.operation_kind = SL_ASYNC_OPERATION_TIMER;
        completion.status = sl_status_ok();
        completion.payload = payload;
        completion.dispatch = time_v8_completion_dispatch;
        completion.cleanup = time_v8_completion_cleanup;

        request->completion_posted.store(true);
        SlStatus status = sl_async_loop_post(backend->async_loop, &completion);
        if (sl_status_is_ok(status)) {
            return true;
        }
        request->completion_posted.store(false);
        delete payload;

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

void time_v8_scheduler(SlV8Engine* backend)
{
    if (backend == nullptr) {
        return;
    }

    std::unique_lock<std::mutex> lock(backend->time_mutex);
    while (!backend->time_shutting_down) {
        uint64_t next_due_ms = 0U;
        bool has_due = false;

        for (const std::shared_ptr<SlV8TimeRequest>& request : backend->time_requests) {
            if (request == nullptr || request->cancelled.load() ||
                request->completion_posted.load())
            {
                continue;
            }
            if (!has_due || request->due_ms < next_due_ms) {
                next_due_ms = request->due_ms;
                has_due = true;
            }
        }

        if (!has_due) {
            backend->time_cv.wait(lock);
            continue;
        }

        uint64_t now_ms = time_v8_monotonic_ms();
        if (next_due_ms > now_ms) {
            auto wake_at =
                std::chrono::steady_clock::time_point(std::chrono::milliseconds(next_due_ms));
            backend->time_cv.wait_until(lock, wake_at);
            continue;
        }

        std::vector<std::shared_ptr<SlV8TimeRequest>> due_requests;
        for (const std::shared_ptr<SlV8TimeRequest>& request : backend->time_requests) {
            if (request != nullptr && !request->cancelled.load() &&
                !request->completion_posted.load() && request->due_ms <= now_ms)
            {
                due_requests.push_back(request);
            }
        }

        lock.unlock();
        for (const std::shared_ptr<SlV8TimeRequest>& request : due_requests) {
            (void)time_v8_post_completion(request);
        }
        lock.lock();
    }
}

bool time_v8_set_function(v8::Isolate* isolate, v8::Local<v8::Context> context,
                          v8::Local<v8::Object> object, const char* name,
                          v8::FunctionCallback callback)
{
    v8::Local<v8::String> key;
    v8::Local<v8::FunctionTemplate> function_template;
    v8::Local<v8::Function> function;

    if (!sl_status_is_ok(time_v8_to_local_string(isolate, sl_str_from_cstr(name), &key))) {
        return false;
    }
    function_template = v8::FunctionTemplate::New(isolate, callback);
    if (!function_template->GetFunction(context).ToLocal(&function)) {
        return false;
    }
    return object->Set(context, key, function).FromMaybe(false);
}

void time_v8_delay_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Promise::Resolver> resolver;
    uint64_t delay_ms = 0U;

    if (backend == nullptr || backend->async_loop == nullptr) {
        time_v8_throw_type_error(isolate,
                                 "__sloppy.time is unavailable because stdlib.time is inactive");
        return;
    }
    if (args.Length() < 1 || !time_v8_delay_arg(args[0], &delay_ms)) {
        time_v8_throw_type_error(
            isolate, "__sloppy.time.delay requires a finite non-negative millisecond value");
        return;
    }
    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
        time_v8_throw_type_error(isolate, "__sloppy.time.delay could not create a Promise");
        return;
    }

    std::shared_ptr<SlV8TimeRequest> request(new (std::nothrow) SlV8TimeRequest());
    if (request == nullptr) {
        time_v8_throw_type_error(isolate, "__sloppy.time.delay could not allocate a timer");
        return;
    }

    request->backend = backend;
    request->delay_ms = delay_ms;
    request->due_ms = time_v8_monotonic_ms() + delay_ms;
    request->resolver.Reset(isolate, resolver);
    {
        std::lock_guard<std::mutex> lock(backend->time_mutex);
        if (backend->time_shutting_down) {
            time_v8_throw_type_error(isolate, "__sloppy.time.delay is unavailable during shutdown");
            return;
        }
        if (!backend->time_scheduler_started) {
            try {
                backend->time_scheduler = std::thread(time_v8_scheduler, backend);
            } catch (...) {
                request->resolver.Reset();
                time_v8_throw_type_error(isolate,
                                         "__sloppy.time.delay could not start timer scheduler");
                return;
            }
            backend->time_scheduler_started = true;
        }
        backend->time_requests.push_back(request);
    }
    backend->time_cv.notify_one();

    args.GetReturnValue().Set(resolver->GetPromise());
}

void time_v8_monotonic_ms_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    args.GetReturnValue().Set(
        v8::Number::New(args.GetIsolate(), static_cast<double>(time_v8_monotonic_ms())));
}

} // namespace

void sl_v8_append_time_external_references(std::vector<intptr_t>* refs)
{
    if (refs == nullptr) {
        return;
    }
    refs->push_back(reinterpret_cast<intptr_t>(time_v8_delay_callback));
    refs->push_back(reinterpret_cast<intptr_t>(time_v8_monotonic_ms_callback));
}

bool sl_v8_install_time_intrinsics(SlV8Engine* backend, v8::Local<v8::Context> context,
                                   v8::Local<v8::Object> sloppy)
{
    v8::Isolate* isolate = backend == nullptr ? nullptr : backend->isolate;
    v8::Local<v8::String> time_key;

    if (backend == nullptr || isolate == nullptr) {
        return false;
    }

    if (!backend->has_runtime_features ||
        !sl_v8_runtime_feature_active(backend, SL_RUNTIME_FEATURE_STDLIB_TIME))
    {
        return true;
    }

    v8::Local<v8::Object> time = v8::Object::New(isolate);
    if (!sl_status_is_ok(time_v8_to_local_string(isolate, sl_str_from_cstr("time"), &time_key)) ||
        !time_v8_set_function(isolate, context, time, "delay", time_v8_delay_callback) ||
        !time_v8_set_function(isolate, context, time, "monotonicMs", time_v8_monotonic_ms_callback))
    {
        return false;
    }
    return sloppy->Set(context, time_key, time).FromMaybe(false);
}

void sl_v8_time_dispose(SlV8Engine* backend)
{
    std::vector<std::shared_ptr<SlV8TimeRequest>> requests;

    if (backend == nullptr) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(backend->time_mutex);
        backend->time_shutting_down = true;
        requests = backend->time_requests;
    }
    backend->time_cv.notify_all();

    if (backend->time_scheduler_started && backend->time_scheduler.joinable()) {
        backend->time_scheduler.join();
    }

    for (const std::shared_ptr<SlV8TimeRequest>& request : requests) {
        if (request == nullptr) {
            continue;
        }
        request->cancelled.store(true);
        if (!request->completion_posted.load()) {
            request->resolver.Reset();
        }
    }

    {
        std::lock_guard<std::mutex> lock(backend->time_mutex);
        backend->time_requests.clear();
        backend->time_scheduler_started = false;
    }
}

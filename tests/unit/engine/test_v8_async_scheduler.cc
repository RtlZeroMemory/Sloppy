#include "../../../src/engine/v8/async_scheduler.h"

#include "sloppy/async_backend.h"
#include "sloppy/engine.h"

#include <cstddef>
#include <string>
#include <thread>

typedef struct ScopeCounter
{
    size_t retain_count;
    size_t release_count;
} ScopeCounter;

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static SlStatus retain_scope(void* scope, void* user)
{
    ScopeCounter* counter = static_cast<ScopeCounter*>(scope);

    (void)user;
    if (counter == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    counter->retain_count += 1U;
    return sl_status_ok();
}

static void release_scope(void* scope, void* user)
{
    ScopeCounter* counter = static_cast<ScopeCounter*>(scope);

    (void)user;
    if (counter != nullptr) {
        counter->release_count += 1U;
    }
}

static SlAsyncScopeRef scope_ref(ScopeCounter* counter)
{
    SlAsyncScopeRef scope = {};

    scope.scope = counter;
    scope.retain = retain_scope;
    scope.release = release_scope;
    return scope;
}

static SlEngineOptions v8_options(void)
{
    SlEngineOptions options = {};

    options.kind = SL_ENGINE_KIND_V8;
    options.runtime_name = sl_str_from_cstr("sloppy-v8-async-scheduler-test");
    options.runtime_version = sl_str_from_cstr("0.12.0-test");
    options.target_platform = sl_str_from_cstr(SL_PLAN_TARGET_PLATFORM_WINDOWS_X64);
    options.target_engine = sl_str_from_cstr(SL_PLAN_TARGET_ENGINE_V8);
    return options;
}

static std::string v8_value_to_string(v8::Isolate* isolate, v8::Local<v8::Value> value)
{
    v8::String::Utf8Value utf8(isolate, value);
    if (*utf8 == nullptr) {
        return std::string();
    }

    return std::string(*utf8, static_cast<size_t>(utf8.length()));
}

static int test_cross_thread_native_completion_fulfills_on_owner_thread(void)
{
    unsigned char engine_storage[8192];
    unsigned char loop_arena_storage[4096];
    SlArena engine_arena = {};
    SlArena loop_arena = {};
    SlAsyncCompletion completion_storage[4];
    SlAsyncLoop* loop = nullptr;
    SlEngine* engine = nullptr;
    SlEngineOptions options = v8_options();
    SlV8NativeContinuation continuation = {};
    ScopeCounter scope = {0U, 0U};
    SlStatus worker_status = sl_status_ok();
    size_t ran = 0U;

    if (expect_status(sl_arena_init(&engine_arena, engine_storage, sizeof(engine_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&loop_arena, loop_arena_storage, sizeof(loop_arena_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_async_loop_create(SL_ASYNC_BACKEND_LIBUV, &loop_arena, completion_storage,
                                           4U, &loop),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0)
    {
        return 1;
    }

    int result = [&]() {
        SlV8Engine* backend = static_cast<SlV8Engine*>(engine->backend);
        v8::Isolate* isolate = backend->isolate;
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        v8::Local<v8::Context> context = backend->context.Get(isolate);
        v8::Context::Scope context_scope(context);
        v8::Local<v8::Promise> promise;

        if (expect_status(sl_v8_native_continuation_prepare(engine, &continuation, &promise),
                          SL_STATUS_OK) != 0 ||
            promise->State() != v8::Promise::kPending)
        {
            return 2;
        }

        v8::Global<v8::Promise> promise_ref(isolate, promise);
        std::thread worker([&]() {
            worker_status = sl_v8_native_continuation_post(loop, &continuation, sl_status_ok(),
                                                           "async ok", false, scope_ref(&scope));
        });
        worker.join();

        if (expect_status(worker_status, SL_STATUS_OK) != 0 || continuation.settled ||
            scope.retain_count != 1U || scope.release_count != 0U)
        {
            promise_ref.Reset();
            return 3;
        }

        if (expect_status(sl_async_loop_drain(loop, 0U, &ran), SL_STATUS_OK) != 0 || ran != 1U ||
            !continuation.settled || !continuation.cleanup_ran || scope.release_count != 1U)
        {
            promise_ref.Reset();
            return 4;
        }

        v8::Local<v8::Promise> settled = promise_ref.Get(isolate);
        if (settled->State() != v8::Promise::kFulfilled ||
            v8_value_to_string(isolate, settled->Result()) != "async ok")
        {
            promise_ref.Reset();
            return 5;
        }

        promise_ref.Reset();
        return 0;
    }();

    sl_async_loop_dispose(loop);
    sl_engine_destroy(engine);
    return result;
}

static int test_native_completion_rejects_on_owner_thread(void)
{
    unsigned char engine_storage[8192];
    unsigned char loop_arena_storage[4096];
    SlArena engine_arena = {};
    SlArena loop_arena = {};
    SlAsyncCompletion completion_storage[2];
    SlAsyncLoop* loop = nullptr;
    SlEngine* engine = nullptr;
    SlEngineOptions options = v8_options();
    SlV8NativeContinuation continuation = {};
    ScopeCounter scope = {0U, 0U};
    size_t ran = 0U;

    if (expect_status(sl_arena_init(&engine_arena, engine_storage, sizeof(engine_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&loop_arena, loop_arena_storage, sizeof(loop_arena_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(
            sl_async_loop_create(SL_ASYNC_BACKEND_TEST, &loop_arena, completion_storage, 2U, &loop),
            SL_STATUS_OK) != 0 ||
        expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0)
    {
        return 10;
    }

    int result = [&]() {
        SlV8Engine* backend = static_cast<SlV8Engine*>(engine->backend);
        v8::Isolate* isolate = backend->isolate;
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        v8::Local<v8::Context> context = backend->context.Get(isolate);
        v8::Context::Scope context_scope(context);
        v8::Local<v8::Promise> promise;

        if (expect_status(sl_v8_native_continuation_prepare(engine, &continuation, &promise),
                          SL_STATUS_OK) != 0 ||
            expect_status(sl_v8_native_continuation_post(
                              loop, &continuation, sl_status_from_code(SL_STATUS_INVALID_STATE),
                              "async bad", true, scope_ref(&scope)),
                          SL_STATUS_OK) != 0)
        {
            return 11;
        }

        v8::Global<v8::Promise> promise_ref(isolate, promise);
        if (expect_status(sl_async_loop_drain(loop, 0U, &ran), SL_STATUS_OK) != 0 || ran != 1U ||
            !continuation.settled || scope.retain_count != 1U || scope.release_count != 1U)
        {
            promise_ref.Reset();
            return 12;
        }

        v8::Local<v8::Promise> settled = promise_ref.Get(isolate);
        if (settled->State() != v8::Promise::kRejected ||
            v8_value_to_string(isolate, settled->Result()).find("async bad") == std::string::npos)
        {
            promise_ref.Reset();
            return 13;
        }

        promise_ref.Reset();
        return 0;
    }();

    sl_async_loop_dispose(loop);
    sl_engine_destroy(engine);
    return result;
}

static int test_wrong_thread_drain_does_not_resume_promise(void)
{
    unsigned char engine_storage[8192];
    unsigned char loop_arena_storage[4096];
    SlArena engine_arena = {};
    SlArena loop_arena = {};
    SlAsyncCompletion completion_storage[1];
    SlAsyncLoop* loop = nullptr;
    SlEngine* engine = nullptr;
    SlEngineOptions options = v8_options();
    SlV8NativeContinuation continuation = {};
    ScopeCounter scope = {0U, 0U};
    SlStatus worker_status = sl_status_ok();
    size_t worker_ran = 0U;

    if (expect_status(sl_arena_init(&engine_arena, engine_storage, sizeof(engine_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&loop_arena, loop_arena_storage, sizeof(loop_arena_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(
            sl_async_loop_create(SL_ASYNC_BACKEND_TEST, &loop_arena, completion_storage, 1U, &loop),
            SL_STATUS_OK) != 0 ||
        expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0)
    {
        return 20;
    }

    int result = [&]() {
        SlV8Engine* backend = static_cast<SlV8Engine*>(engine->backend);
        v8::Isolate* isolate = backend->isolate;
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        v8::Local<v8::Context> context = backend->context.Get(isolate);
        v8::Context::Scope context_scope(context);
        v8::Local<v8::Promise> promise;

        if (expect_status(sl_v8_native_continuation_prepare(engine, &continuation, &promise),
                          SL_STATUS_OK) != 0 ||
            expect_status(sl_v8_native_continuation_post(loop, &continuation, sl_status_ok(),
                                                         "should not settle", false,
                                                         scope_ref(&scope)),
                          SL_STATUS_OK) != 0)
        {
            return 21;
        }

        v8::Global<v8::Promise> promise_ref(isolate, promise);
        std::thread worker([&]() { worker_status = sl_async_loop_drain(loop, 0U, &worker_ran); });
        worker.join();

        v8::Local<v8::Promise> still_pending = promise_ref.Get(isolate);
        if (expect_status(worker_status, SL_STATUS_INVALID_STATE) != 0 || worker_ran != 1U ||
            continuation.settled || !continuation.cleanup_ran ||
            still_pending->State() != v8::Promise::kPending || scope.release_count != 1U)
        {
            promise_ref.Reset();
            return 22;
        }

        promise_ref.Reset();
        return 0;
    }();

    sl_async_loop_dispose(loop);
    sl_engine_destroy(engine);
    return result;
}

int main(void)
{
    int result = test_cross_thread_native_completion_fulfills_on_owner_thread();

    if (result != 0) {
        return result;
    }

    result = test_native_completion_rejects_on_owner_thread();
    if (result != 0) {
        return result;
    }

    return test_wrong_thread_drain_does_not_resume_promise();
}

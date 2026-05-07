#ifndef SLOPPY_ENGINE_V8_ASYNC_SCHEDULER_H
#define SLOPPY_ENGINE_V8_ASYNC_SCHEDULER_H

#include "engine_v8_internal.h"

#include "sloppy/async_backend.h"

#include <v8.h>

#include <mutex>
#include <string>

typedef struct SlV8NativeContinuation
{
    std::mutex mutex;
    SlEngine* engine;
    v8::Global<v8::Promise::Resolver> resolver;
    std::string text;
    bool reject;
    bool queued;
    bool settled;
    bool cleanup_ran;
    bool resolver_ready;
} SlV8NativeContinuation;

SlStatus sl_v8_native_continuation_prepare(SlEngine* engine, SlV8NativeContinuation* continuation,
                                           v8::Local<v8::Promise>* out_promise);
SlStatus sl_v8_native_continuation_post(SlAsyncLoop* loop, SlV8NativeContinuation* continuation,
                                        SlStatus status, const char* text, bool reject,
                                        SlAsyncScopeRef scope);

#endif

/*
 * src/engine/v8/engine_v8_internal.h
 *
 * Private C++ contract shared only by files inside src/engine/v8/.
 * Provider-specific V8 bridges belong in separate intrinsic modules, and framework-specific
 * bridges such as HTTP context/result conversion belong in separate sibling modules, not in
 * engine_v8.cc. This header intentionally keeps V8 types inside the V8 module boundary while
 * allowing those modules to use engine-owned state.
 */
#ifndef SLOPPY_ENGINE_V8_INTERNAL_H
#define SLOPPY_ENGINE_V8_INTERNAL_H

#include "../engine_internal.h"

#include "sloppy/features.h"
#include "sloppy/async_backend.h"
#include "sloppy/provider_executor.h"
#include "sloppy/resource.h"

#include <v8.h>

#include <array>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

struct SlV8TimeRequest;

struct SlV8Engine
{
    v8::ArrayBuffer::Allocator* allocator = nullptr;
    v8::Isolate* isolate = nullptr;
    SlArena* arena = nullptr;
    v8::Global<v8::Context> context;
    std::unordered_map<uint32_t, v8::Global<v8::Function>> handlers;
    std::unordered_map<uint32_t, v8::Global<v8::Function>>* pending_handlers = nullptr;
    std::thread::id owner_thread;
    /* Non-owning app metadata; both referenced objects must outlive this engine. */
    const SlPlan* plan = nullptr;
    const SlCapabilityRegistry* capabilities = nullptr;
    const SlFsPolicy* filesystem_policy = nullptr;
    SlBytes source_map = {};
    SlStr source_map_source_name = {};
    bool has_runtime_features = false;
    SlRuntimeFeatureSet runtime_features = {};
    std::array<SlResourceEntry, 64U> resource_entries = {};
    SlResourceTable resources = {};
    std::array<SlAsyncCompletion, 64U> async_completions = {};
    SlAsyncLoop* async_loop = nullptr;
    std::mutex time_mutex;
    std::condition_variable time_cv;
    std::vector<std::shared_ptr<SlV8TimeRequest>> time_requests;
    std::thread time_scheduler;
    bool time_scheduler_started = false;
    bool time_shutting_down = false;
    SlProviderInstanceExecutor fs_executor = {};
    std::array<SlProviderExecutorSlot, 32U> fs_slots = {};
    bool fs_executor_initialized = false;
};

bool sl_v8_runtime_feature_active(const SlV8Engine* backend, SlRuntimeFeatureId id);

bool sl_v8_install_provider_intrinsics(SlV8Engine* backend, v8::Local<v8::Context> context,
                                       v8::Local<v8::Object> data);

bool sl_v8_install_fs_intrinsics(SlV8Engine* backend, v8::Local<v8::Context> context,
                                 v8::Local<v8::Object> sloppy);

bool sl_v8_install_time_intrinsics(SlV8Engine* backend, v8::Local<v8::Context> context,
                                   v8::Local<v8::Object> sloppy);
void sl_v8_time_dispose(SlV8Engine* backend);

bool sl_v8_install_sqlite_intrinsics(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                     v8::Local<v8::Object> data);

bool sl_v8_make_http_context_object(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                    const SlHttpRequestContext* request_context,
                                    v8::Local<v8::Object>* out);

SlStatus sl_v8_convert_http_handler_result(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                           SlEngine* engine, SlArena* arena,
                                           v8::Local<v8::Value> js_result,
                                           SlEngineResult* out_result, SlDiag* out_diag);

#endif

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

#include "sloppy/capability.h"
#include "sloppy/resource.h"

#include <v8.h>

#include <array>
#include <cstdint>
#include <thread>
#include <unordered_map>

struct SlV8Engine
{
    v8::ArrayBuffer::Allocator* allocator = nullptr;
    v8::Isolate* isolate = nullptr;
    v8::Global<v8::Context> context;
    std::unordered_map<uint32_t, v8::Global<v8::Function>> handlers;
    std::unordered_map<uint32_t, v8::Global<v8::Function>>* pending_handlers = nullptr;
    std::thread::id owner_thread;
    SlCapabilityRegistry capabilities = {};
    std::array<SlResourceEntry, 64U> resource_entries = {};
    SlResourceTable resources = {};
};

bool sl_v8_install_provider_intrinsics(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                       v8::Local<v8::Object> data);

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

/*
 * src/engine/v8/engine_v8_internal.h
 *
 * Private C++ contract shared only by files inside src/engine/v8/.
 * Provider-specific V8 bridges belong in separate intrinsic modules, not in
 * engine_v8.cc. This header intentionally keeps V8 types inside the V8 module
 * boundary while allowing those modules to use the engine-owned resource table.
 */
#ifndef SLOPPY_ENGINE_V8_INTERNAL_H
#define SLOPPY_ENGINE_V8_INTERNAL_H

#include "../engine_internal.h"

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
    std::array<SlResourceEntry, 64U> resource_entries = {};
    SlResourceTable resources = {};
};

bool sl_v8_install_provider_intrinsics(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                       v8::Local<v8::Object> data);

bool sl_v8_install_sqlite_intrinsics(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                     v8::Local<v8::Object> data);

#endif

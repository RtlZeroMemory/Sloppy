/*
 * src/engine/v8/intrinsics.cc
 *
 * Aggregates provider-specific V8 intrinsic modules. engine_v8.cc calls this
 * file once while constructing the private __sloppy namespace; new provider
 * bridges should add sibling intrinsics_<provider>.cc files and register them
 * here instead of growing engine_v8.cc.
 */
#include "engine_v8_internal.h"

bool sl_v8_install_provider_intrinsics(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                       v8::Local<v8::Object> data)
{
    return sl_v8_install_sqlite_intrinsics(isolate, context, data);
}

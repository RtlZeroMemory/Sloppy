/*
 * src/engine/v8/intrinsics_workers.cc
 *
 * Installs the feature-gated workers namespace under __sloppy.workers. The first
 * JS surface uses bootstrap-owned lifecycle/queue code; native worker execution must
 * enter through explicit bridge functions added here, never by exposing raw handles to JS.
 */
#include "engine_v8_internal.h"
#include "string_interop.h"

namespace {

bool workers_v8_set_string(SlV8Engine* backend, v8::Local<v8::Context> context,
                           v8::Local<v8::Object> object, const char* key, const char* value)
{
    v8::Local<v8::String> local_key;
    v8::Local<v8::String> local_value;
    if (!sl_status_is_ok(
            sl_v8_string_from_native_view(backend, sl_str_from_cstr(key), &local_key)) ||
        !sl_status_is_ok(
            sl_v8_string_from_native_view(backend, sl_str_from_cstr(value), &local_value)))
    {
        return false;
    }
    return object->Set(context, local_key, local_value).FromMaybe(false);
}

bool workers_v8_set_bool(SlV8Engine* backend, v8::Local<v8::Context> context,
                         v8::Local<v8::Object> object, const char* key, bool value)
{
    v8::Local<v8::String> local_key;
    if (!sl_status_is_ok(sl_v8_string_from_native_view(backend, sl_str_from_cstr(key), &local_key)))
    {
        return false;
    }
    return object->Set(context, local_key, v8::Boolean::New(backend->isolate, value))
        .FromMaybe(false);
}

} // namespace

bool sl_v8_install_workers_intrinsics(SlV8Engine* backend, v8::Local<v8::Context> context,
                                      v8::Local<v8::Object> sloppy)
{
    if (backend == nullptr || backend->isolate == nullptr) {
        return false;
    }
    if (backend->has_runtime_features &&
        !sl_v8_runtime_feature_active(backend, SL_RUNTIME_FEATURE_STDLIB_WORKERS))
    {
        return true;
    }

    v8::Local<v8::String> workers_key;
    v8::Local<v8::Object> workers = v8::Object::New(backend->isolate);
    if (!sl_status_is_ok(
            sl_v8_string_from_native_view(backend, sl_str_from_cstr("workers"), &workers_key)))
    {
        return false;
    }
    if (!workers_v8_set_string(backend, context, workers, "feature", "stdlib.workers") ||
        !workers_v8_set_bool(backend, context, workers, "boundedByDefault", true) ||
        !workers_v8_set_bool(backend, context, workers, "rawNativeHandlesExposed", false) ||
        !workers_v8_set_bool(backend, context, workers, "ownerThreadSettlement", true))
    {
        return false;
    }
    return sloppy->Set(context, workers_key, workers).FromMaybe(false);
}

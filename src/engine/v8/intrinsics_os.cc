/*
 * src/engine/v8/intrinsics_os.cc
 *
 * Installs the V8-internal OS bridge under __sloppy.os. This slice exposes only System
 * and Environment operations; process and signal APIs remain deferred.
 */
#include "engine_v8_internal.h"
#include "string_interop.h"

#include "sloppy/os.h"

#include <string>

namespace {

SlStatus os_v8_to_local_string(v8::Isolate* isolate, SlStr str, v8::Local<v8::String>* out)
{
    SlV8Engine* backend =
        isolate == nullptr ? nullptr : static_cast<SlV8Engine*>(isolate->GetData(0));
    return sl_v8_string_from_native_view(backend, str, out);
}

bool os_v8_set_string(v8::Isolate* isolate, v8::Local<v8::Context> context,
                      v8::Local<v8::Object> object, const char* name, SlOwnedStr value)
{
    v8::Local<v8::String> key;
    v8::Local<v8::String> text;
    return sl_status_is_ok(os_v8_to_local_string(isolate, sl_str_from_cstr(name), &key)) &&
           sl_status_is_ok(os_v8_to_local_string(isolate, sl_owned_str_as_view(value), &text)) &&
           object->Set(context, key, text).FromMaybe(false);
}

bool os_v8_set_uint32(v8::Isolate* isolate, v8::Local<v8::Context> context,
                      v8::Local<v8::Object> object, const char* name, uint32_t value)
{
    v8::Local<v8::String> key;
    return sl_status_is_ok(os_v8_to_local_string(isolate, sl_str_from_cstr(name), &key)) &&
           object->Set(context, key, v8::Uint32::New(isolate, value)).FromMaybe(false);
}

void os_v8_throw(v8::Isolate* isolate, const char* message)
{
    v8::Local<v8::String> text;
    if (!sl_status_is_ok(os_v8_to_local_string(isolate, sl_str_from_cstr(message), &text))) {
        text = v8::String::NewFromUtf8Literal(isolate, "Sloppy OS operation failed");
    }
    isolate->ThrowException(v8::Exception::Error(text));
}

bool os_v8_value_to_string(v8::Isolate* isolate, v8::Local<v8::Value> value, SlArena* arena,
                           SlOwnedStr* out)
{
    std::string text;

    if (value.IsEmpty() || !value->IsString()) {
        return false;
    }
    if (!sl_v8_std_string_from_value(isolate, value, &text)) {
        return false;
    }
    return sl_status_is_ok(
        sl_str_copy_to_arena(arena, sl_str_from_parts(text.data(), text.size()), out));
}

void os_v8_system_info_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    unsigned char storage[4096];
    SlArena arena = {};
    SlOsSystemInfo info = {};
    SlOsPolicy policy = sl_os_development_policy();
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));
    v8::Local<v8::Object> result;

    if (!sl_status_is_ok(status) ||
        !sl_status_is_ok(sl_os_system_info(&arena, &policy, &info, nullptr)))
    {
        os_v8_throw(isolate, "SLOPPY_E_OS_FEATURE_UNAVAILABLE: System metadata unavailable");
        return;
    }
    result = v8::Object::New(isolate);
    if (!os_v8_set_string(isolate, context, result, "platform", info.platform) ||
        !os_v8_set_string(isolate, context, result, "arch", info.arch) ||
        !os_v8_set_uint32(isolate, context, result, "cpuCount", info.cpu_count) ||
        !os_v8_set_string(isolate, context, result, "tempDirectory", info.temp_directory) ||
        !os_v8_set_string(isolate, context, result, "hostname", info.hostname) ||
        !os_v8_set_string(isolate, context, result, "endOfLine", info.end_of_line))
    {
        os_v8_throw(isolate, "SLOPPY_E_OS_FEATURE_UNAVAILABLE: System metadata unavailable");
        return;
    }
    args.GetReturnValue().Set(result);
}

void os_v8_env_get_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    unsigned char storage[4096];
    SlArena arena = {};
    SlOwnedStr key = {};
    SlOwnedStr value = {};
    bool found = false;
    SlOsPolicy policy = sl_os_development_policy();

    if (!sl_status_is_ok(sl_arena_init(&arena, storage, sizeof(storage))) || args.Length() != 1 ||
        !os_v8_value_to_string(isolate, args[0], &arena, &key))
    {
        os_v8_throw(isolate, "SLOPPY_E_INVALID_ARGUMENT: Environment.get requires a key string");
        return;
    }
    if (!sl_status_is_ok(sl_os_environment_get(&arena, &policy, sl_owned_str_as_view(key), &value,
                                               &found, nullptr)))
    {
        os_v8_throw(isolate, "SLOPPY_E_OS_ENV_ACCESS_DENIED: environment variable access denied");
        return;
    }
    if (!found) {
        args.GetReturnValue().Set(v8::Undefined(isolate));
        return;
    }
    v8::Local<v8::String> text;
    if (!sl_status_is_ok(os_v8_to_local_string(isolate, sl_owned_str_as_view(value), &text))) {
        os_v8_throw(isolate, "SLOPPY_E_OS_FEATURE_UNAVAILABLE: environment value unavailable");
        return;
    }
    args.GetReturnValue().Set(text);
}

void os_v8_env_has_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    unsigned char storage[1024];
    SlArena arena = {};
    SlOwnedStr key = {};
    bool found = false;
    SlOsPolicy policy = sl_os_development_policy();

    if (!sl_status_is_ok(sl_arena_init(&arena, storage, sizeof(storage))) || args.Length() != 1 ||
        !os_v8_value_to_string(isolate, args[0], &arena, &key))
    {
        os_v8_throw(isolate, "SLOPPY_E_INVALID_ARGUMENT: Environment.has requires a key string");
        return;
    }
    if (!sl_status_is_ok(
            sl_os_environment_has(&policy, sl_owned_str_as_view(key), &found, nullptr)))
    {
        os_v8_throw(isolate, "SLOPPY_E_OS_ENV_ACCESS_DENIED: environment variable access denied");
        return;
    }
    args.GetReturnValue().Set(v8::Boolean::New(isolate, found));
}

void os_v8_env_list_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    unsigned char storage[65536];
    SlArena arena = {};
    SlOwnedStr prefix = {};
    SlOsEnvironmentList list = {};
    SlOsPolicy policy = sl_os_development_policy();

    if (!sl_status_is_ok(sl_arena_init(&arena, storage, sizeof(storage)))) {
        os_v8_throw(isolate, "SLOPPY_E_OS_FEATURE_UNAVAILABLE: environment list unavailable");
        return;
    }
    if (args.Length() > 0 && !args[0]->IsUndefined() &&
        !os_v8_value_to_string(isolate, args[0], &arena, &prefix))
    {
        os_v8_throw(isolate, "SLOPPY_E_INVALID_ARGUMENT: Environment.list prefix must be a string");
        return;
    }
    if (!sl_status_is_ok(
            sl_os_environment_list(&arena, &policy, sl_owned_str_as_view(prefix), &list, nullptr)))
    {
        os_v8_throw(isolate, "SLOPPY_E_OS_ENV_ACCESS_DENIED: environment list denied");
        return;
    }
    v8::Local<v8::Array> result = v8::Array::New(isolate, static_cast<int>(list.count));
    for (size_t index = 0U; index < list.count; index += 1U) {
        v8::Local<v8::String> item;
        if (!sl_status_is_ok(os_v8_to_local_string(
                isolate, sl_owned_str_as_view(list.entries[index].key), &item)) ||
            !result->Set(context, static_cast<uint32_t>(index), item).FromMaybe(false))
        {
            os_v8_throw(isolate, "SLOPPY_E_OS_FEATURE_UNAVAILABLE: environment list unavailable");
            return;
        }
    }
    args.GetReturnValue().Set(result);
}

bool os_v8_set_function(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        v8::Local<v8::Object> object, const char* name,
                        v8::FunctionCallback callback)
{
    v8::Local<v8::String> key;
    v8::Local<v8::Function> function;
    return sl_status_is_ok(os_v8_to_local_string(isolate, sl_str_from_cstr(name), &key)) &&
           v8::FunctionTemplate::New(isolate, callback)->GetFunction(context).ToLocal(&function) &&
           object->Set(context, key, function).FromMaybe(false);
}

} // namespace

bool sl_v8_install_os_intrinsics(SlV8Engine* backend, v8::Local<v8::Context> context,
                                 v8::Local<v8::Object> sloppy)
{
    v8::Isolate* isolate = backend == nullptr ? nullptr : backend->isolate;
    v8::Local<v8::String> os_key;
    v8::Local<v8::Object> os;

    if (backend == nullptr || isolate == nullptr) {
        return false;
    }
    if (!backend->has_runtime_features ||
        !sl_v8_runtime_feature_active(backend, SL_RUNTIME_FEATURE_STDLIB_OS))
    {
        return true;
    }
    if (!sl_status_is_ok(os_v8_to_local_string(isolate, sl_str_from_cstr("os"), &os_key))) {
        return false;
    }
    os = v8::Object::New(isolate);
    if (!os_v8_set_function(isolate, context, os, "systemInfo", os_v8_system_info_callback) ||
        !os_v8_set_function(isolate, context, os, "environmentGet", os_v8_env_get_callback) ||
        !os_v8_set_function(isolate, context, os, "environmentHas", os_v8_env_has_callback) ||
        !os_v8_set_function(isolate, context, os, "environmentList", os_v8_env_list_callback))
    {
        return false;
    }
    return sloppy->Set(context, os_key, os).FromMaybe(false);
}

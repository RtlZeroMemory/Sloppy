/*
 * src/engine/v8/intrinsics_db_bridge.cc
 *
 * Shared V8-only helpers used by SQLite, PostgreSQL, and SQL Server provider
 * bridges. Provider-specific state machines remain in their own modules.
 */
#include "intrinsics_db_bridge.h"
#include "string_interop.h"

namespace {

v8::Local<v8::String> sl_v8_db_fallback_string(v8::Isolate* isolate, const char* fallback)
{
    v8::Local<v8::String> value;
    if (fallback != nullptr &&
        v8::String::NewFromUtf8(isolate, fallback, v8::NewStringType::kNormal).ToLocal(&value))
    {
        return value;
    }
    return v8::String::Empty(isolate);
}

} // namespace

SlStatus sl_v8_db_to_local_string(v8::Isolate* isolate, SlStr str, v8::Local<v8::String>* out)
{
    SlV8Engine* backend =
        isolate == nullptr ? nullptr : static_cast<SlV8Engine*>(isolate->GetData(0));
    return sl_v8_string_from_native_view(backend, str, out);
}

bool sl_v8_db_value_to_std_string(v8::Isolate* isolate, v8::Local<v8::Value> value,
                                  std::string* out)
{
    return out != nullptr && value->IsString() && sl_v8_std_string_from_value(isolate, value, out);
}

bool sl_v8_db_local_string_from_std_string(v8::Isolate* isolate, const std::string& value,
                                           v8::Local<v8::String>* out)
{
    return sl_status_is_ok(
        sl_v8_db_to_local_string(isolate, sl_str_from_parts(value.data(), value.size()), out));
}

void sl_v8_db_throw_type_error(v8::Isolate* isolate, const char* message, const char* fallback)
{
    v8::Local<v8::String> local_message;
    if (isolate == nullptr) {
        return;
    }
    if (!sl_status_is_ok(
            sl_v8_db_to_local_string(isolate, sl_str_from_cstr(message), &local_message)))
    {
        isolate->ThrowException(
            v8::Exception::TypeError(sl_v8_db_fallback_string(isolate, fallback)));
        return;
    }
    isolate->ThrowException(v8::Exception::TypeError(local_message));
}

void sl_v8_db_throw_error(v8::Isolate* isolate, const std::string& message, const char* fallback)
{
    v8::Local<v8::String> local_message;
    if (isolate == nullptr) {
        return;
    }
    if (!sl_v8_db_local_string_from_std_string(isolate, message, &local_message)) {
        isolate->ThrowException(v8::Exception::Error(sl_v8_db_fallback_string(isolate, fallback)));
        return;
    }
    isolate->ThrowException(v8::Exception::Error(local_message));
}

bool sl_v8_db_get_object_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                  v8::Local<v8::Object> object, const char* key,
                                  v8::Local<v8::Value>* out)
{
    v8::Local<v8::String> local_key;
    return out != nullptr &&
           sl_status_is_ok(sl_v8_db_to_local_string(isolate, sl_str_from_cstr(key), &local_key)) &&
           object->Get(context, local_key).ToLocal(out);
}

bool sl_v8_db_get_object_string_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                         v8::Local<v8::Object> object, const char* key,
                                         std::string* out)
{
    v8::Local<v8::Value> value;
    return out != nullptr && sl_v8_db_get_object_property(isolate, context, object, key, &value) &&
           value->IsString() && sl_v8_std_string_from_value(isolate, value, out);
}

bool sl_v8_db_get_optional_object_string(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                         v8::Local<v8::Object> object, const char* key,
                                         std::string* out, bool* present)
{
    v8::Local<v8::Value> value;
    if (out == nullptr || present == nullptr ||
        !sl_v8_db_get_object_property(isolate, context, object, key, &value))
    {
        return false;
    }
    if (value->IsUndefined() || value->IsNull()) {
        *present = false;
        out->clear();
        return true;
    }
    *present = true;
    return sl_v8_db_value_to_std_string(isolate, value, out);
}

bool sl_v8_db_set_object_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                  v8::Local<v8::Object> object, const char* key,
                                  v8::Local<v8::Value> value)
{
    v8::Local<v8::String> local_key;
    return sl_status_is_ok(sl_v8_db_to_local_string(isolate, sl_str_from_cstr(key), &local_key)) &&
           object->Set(context, local_key, value).FromMaybe(false);
}

bool sl_v8_db_is_value_wrapper(v8::Isolate* isolate, v8::Local<v8::Context> context,
                               v8::Local<v8::Value> value)
{
    v8::Local<v8::Value> marker;
    return value->IsObject() &&
           sl_v8_db_get_object_property(isolate, context, value.As<v8::Object>(), "__sloppyDbValue",
                                        &marker) &&
           marker->IsTrue();
}

bool sl_v8_db_make_typed_string_value(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                      const char* kind, SlStr value, v8::Local<v8::Value>* out)
{
    v8::Local<v8::String> text;
    v8::Local<v8::String> kind_value;
    v8::Local<v8::Value> wrapper_value;
    v8::Local<v8::Object> wrapper;

    if (out == nullptr || kind == nullptr ||
        !sl_status_is_ok(sl_v8_db_to_local_string(isolate, value, &text)) ||
        !sl_status_is_ok(sl_v8_db_to_local_string(isolate, sl_str_from_cstr(kind), &kind_value)))
    {
        return false;
    }
    wrapper_value = v8::StringObject::New(isolate, text);
    if (!wrapper_value->IsObject()) {
        return false;
    }
    wrapper = wrapper_value.As<v8::Object>();
    if (!sl_v8_db_set_object_property(isolate, context, wrapper, "__sloppyDbValue",
                                      v8::Boolean::New(isolate, true)) ||
        !sl_v8_db_set_object_property(isolate, context, wrapper, "kind", kind_value) ||
        !sl_v8_db_set_object_property(isolate, context, wrapper, "value", text) ||
        !wrapper->SetIntegrityLevel(context, v8::IntegrityLevel::kFrozen).FromMaybe(false))
    {
        return false;
    }
    *out = wrapper_value;
    return true;
}

bool sl_v8_db_get_resource_id(v8::Isolate* isolate, v8::Local<v8::Context> context,
                              v8::Local<v8::Value> value, SlResourceId* out)
{
    v8::Local<v8::Object> object;
    v8::Local<v8::Value> slot_value;
    v8::Local<v8::Value> generation_value;

    if (out == nullptr || !value->IsObject()) {
        return false;
    }
    object = value.As<v8::Object>();
    if (!sl_v8_db_get_object_property(isolate, context, object, "slot", &slot_value) ||
        !sl_v8_db_get_object_property(isolate, context, object, "generation", &generation_value) ||
        !slot_value->IsUint32() || !generation_value->IsUint32())
    {
        return false;
    }
    out->slot = slot_value.As<v8::Uint32>()->Value();
    out->generation = generation_value.As<v8::Uint32>()->Value();
    return sl_resource_id_is_valid(*out);
}

bool sl_v8_db_make_resource_handle(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                   SlResourceId id, const char* kind, v8::Local<v8::Object>* out)
{
    v8::Local<v8::Object> handle = v8::Object::New(isolate);
    v8::Local<v8::String> kind_value;

    if (out == nullptr || kind == nullptr ||
        !sl_status_is_ok(sl_v8_db_to_local_string(isolate, sl_str_from_cstr(kind), &kind_value)) ||
        !sl_v8_db_set_object_property(isolate, context, handle, "slot",
                                      v8::Integer::NewFromUnsigned(isolate, id.slot)) ||
        !sl_v8_db_set_object_property(isolate, context, handle, "generation",
                                      v8::Integer::NewFromUnsigned(isolate, id.generation)) ||
        !sl_v8_db_set_object_property(isolate, context, handle, "kind", kind_value))
    {
        return false;
    }
    *out = handle;
    return true;
}

bool sl_v8_db_make_promise(v8::Isolate* isolate, v8::Local<v8::Context> context,
                           v8::Global<v8::Promise::Resolver>* resolver, v8::Local<v8::Promise>* out)
{
    v8::Local<v8::Promise::Resolver> local_resolver;
    if (isolate == nullptr || resolver == nullptr || out == nullptr ||
        !v8::Promise::Resolver::New(context).ToLocal(&local_resolver))
    {
        return false;
    }
    resolver->Reset(isolate, local_resolver);
    *out = local_resolver->GetPromise();
    return true;
}

bool sl_v8_db_resolve_promise(v8::Local<v8::Context> context,
                              v8::Local<v8::Promise::Resolver> resolver, v8::Local<v8::Value> value)
{
    return resolver->Resolve(context, value).FromMaybe(false);
}

bool sl_v8_db_reject_promise(v8::Isolate* isolate, v8::Local<v8::Context> context,
                             v8::Local<v8::Promise::Resolver> resolver, const std::string& message,
                             const char* fallback)
{
    v8::Local<v8::String> local_message;
    if (isolate == nullptr) {
        return false;
    }
    if (!sl_v8_db_local_string_from_std_string(isolate, message, &local_message)) {
        local_message = sl_v8_db_fallback_string(isolate, fallback);
    }
    return resolver->Reject(context, v8::Exception::Error(local_message)).FromMaybe(false);
}

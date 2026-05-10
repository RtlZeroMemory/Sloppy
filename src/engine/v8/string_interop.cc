/*
 * src/engine/v8/string_interop.cc
 *
 * Implements the private V8/native text and byte conversion policy for bridge modules.
 *
 * Safety invariants:
 * - native views are converted to V8 strings only on the owning V8 engine thread;
 * - V8 strings copied to native storage become arena-owned before leaving the helper;
 * - all conversions use explicit lengths and make no NUL-termination assumption;
 * - no V8 object is allowed to retain a pointer into transient native memory.
 *
 * Tests: engine.v8.smoke and engine.v8.owner_thread when SLOPPY_ENABLE_V8 is enabled.
 */
#include "string_interop.h"

#include <limits>
#include <thread>

namespace {

bool sl_v8_native_view_valid(SlStr str)
{
    return str.length == 0U || str.ptr != nullptr;
}

bool sl_v8_value_to_string(v8::Isolate* isolate, v8::Local<v8::Value> value,
                           v8::Local<v8::String>* out)
{
    if (isolate == nullptr || out == nullptr || value.IsEmpty()) {
        return false;
    }
    if (value->IsString()) {
        *out = value.As<v8::String>();
        return true;
    }
    return value->ToString(isolate->GetCurrentContext()).ToLocal(out);
}

SlStatus sl_v8_string_write_to_arena(v8::Isolate* isolate, SlArena* arena,
                                     v8::Local<v8::String> value, SlStr* out)
{
    void* copied = nullptr;

    if (isolate == nullptr || arena == nullptr || out == nullptr || value.IsEmpty()) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    const size_t length = value->Utf8LengthV2(isolate);
    if (length == 0U) {
        *out = sl_str_empty();
        return sl_status_ok();
    }

    SlStatus status = sl_arena_alloc(arena, length, alignof(char), &copied);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    const size_t written = value->WriteUtf8V2(isolate, static_cast<char*>(copied), length,
                                              v8::String::WriteFlags::kReplaceInvalidUtf8);
    *out = sl_str_from_parts(static_cast<const char*>(copied), written);
    return sl_status_ok();
}

} // namespace

SlStatus sl_v8_string_from_native_view(SlV8Engine* backend, SlStr str, v8::Local<v8::String>* out)
{
    v8::MaybeLocal<v8::String> maybe;

    if (backend == nullptr || backend->isolate == nullptr || out == nullptr ||
        !sl_v8_native_view_valid(str) ||
        str.length > static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (backend->owner_thread != std::this_thread::get_id()) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    maybe = v8::String::NewFromUtf8(backend->isolate, str.ptr == nullptr ? "" : str.ptr,
                                    v8::NewStringType::kNormal, static_cast<int>(str.length));
    if (!maybe.ToLocal(out)) {
        return sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
    }

    return sl_status_ok();
}

bool sl_v8_std_string_from_value(v8::Isolate* isolate, v8::Local<v8::Value> value, std::string* out)
{
    if (isolate == nullptr || out == nullptr || value.IsEmpty()) {
        return false;
    }

    v8::String::Utf8Value utf8(isolate, value);
    if (*utf8 == nullptr) {
        return false;
    }

    *out = std::string(*utf8, static_cast<size_t>(utf8.length()));
    return true;
}

SlStatus sl_v8_string_from_value_copy_to_arena(v8::Isolate* isolate, SlArena* arena,
                                               v8::Local<v8::Value> value, SlStr* out)
{
    v8::Local<v8::String> string_value;

    if (isolate == nullptr || arena == nullptr || out == nullptr || value.IsEmpty()) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (!sl_v8_value_to_string(isolate, value, &string_value)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    return sl_v8_string_write_to_arena(isolate, arena, string_value, out);
}

SlStatus sl_v8_string_value_copy_bytes_to_arena(v8::Isolate* isolate, SlArena* arena,
                                                v8::Local<v8::Value> value, SlBytes* out)
{
    SlStr copied = {};
    SlStatus status;

    if (isolate == nullptr || arena == nullptr || out == nullptr || value.IsEmpty()) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_v8_string_from_value_copy_to_arena(isolate, arena, value, &copied);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    *out = sl_bytes_from_parts(reinterpret_cast<const unsigned char*>(copied.ptr), copied.length);
    return sl_status_ok();
}

SlStatus sl_v8_std_string_copy_to_arena(SlArena* arena, const std::string& src, SlStr* out)
{
    if (arena == nullptr || out == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    return sl_str_copy_view_to_arena(arena, sl_str_from_parts(src.data(), src.size()), out);
}

SlStatus sl_v8_std_string_copy_bytes_to_arena(SlArena* arena, const std::string& src, SlBytes* out)
{
    SlOwnedBytes copied = {};
    SlStatus status;

    if (arena == nullptr || out == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_bytes_copy_to_arena(
        arena, sl_bytes_from_parts(reinterpret_cast<const unsigned char*>(src.data()), src.size()),
        &copied);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    *out = sl_owned_bytes_as_view(copied);
    return sl_status_ok();
}

bool sl_v8_throw_type_error_from_native_view(SlV8Engine* backend, SlStr message)
{
    v8::Local<v8::String> local_message;

    if (!sl_status_is_ok(sl_v8_string_from_native_view(backend, message, &local_message))) {
        return false;
    }

    backend->isolate->ThrowException(v8::Exception::TypeError(local_message));
    return true;
}

bool sl_v8_throw_error_from_native_view(SlV8Engine* backend, SlStr message)
{
    v8::Local<v8::String> local_message;

    if (!sl_status_is_ok(sl_v8_string_from_native_view(backend, message, &local_message))) {
        return false;
    }

    backend->isolate->ThrowException(v8::Exception::Error(local_message));
    return true;
}

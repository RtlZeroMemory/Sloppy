/*
 * src/engine/v8/intrinsics_ffi.cc
 *
 * Installs the V8-internal FFI bridge under __sloppy.ffi. The compiler and Plan parser own
 * symbol metadata; this file only binds Plan-prepared function IDs and marshals documented
 * primitive values for synchronous libffi calls on the owner isolate thread.
 */
#include "engine_v8_internal.h"
#include "string_interop.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace {

enum class FfiResourceKind
{
    Ref,
    Buffer,
    CString,
    Utf16,
    Struct,
    StructLayout,
    NativePointer
};

enum class FfiMarshalError
{
    InvalidType,
    IntegerOutOfRange,
    StringNul
};

struct FfiStructField
{
    std::string name;
    SlPlanFfiType type = SL_PLAN_FFI_TYPE_UNKNOWN;
    size_t offset = 0U;
};

struct SlV8FfiResource
{
    FfiResourceKind kind = FfiResourceKind::Buffer;
    SlPlanFfiType type = SL_PLAN_FFI_TYPE_PTR;
    std::vector<unsigned char> bytes;
    std::vector<FfiStructField> fields;
    void* native_pointer = nullptr;
    size_t byte_length = 0U;
    bool disposed = false;
};

struct FfiArgStorage
{
    union {
        uint8_t u8;
        int8_t i8;
        uint16_t u16;
        int16_t i16;
        uint32_t u32;
        int32_t i32;
        uint64_t u64;
        int64_t i64;
        float f32;
        double f64;
        intptr_t isize;
        uintptr_t usize;
        void* ptr;
    } value = {};
    std::string utf8;
    std::vector<uint16_t> utf16;
};

SlV8Engine* ffi_backend(v8::Isolate* isolate)
{
    return isolate == nullptr ? nullptr : static_cast<SlV8Engine*>(isolate->GetData(0));
}

void ffi_throw_type_error(v8::Isolate* isolate, const char* message)
{
    SlV8Engine* backend = ffi_backend(isolate);
    if (backend == nullptr ||
        !sl_v8_throw_type_error_from_native_view(backend, sl_str_from_cstr(message)))
    {
        isolate->ThrowException(
            v8::Exception::TypeError(v8::String::NewFromUtf8Literal(isolate, "FFI type error")));
    }
}

void ffi_throw_error(v8::Isolate* isolate, const char* message)
{
    SlV8Engine* backend = ffi_backend(isolate);
    if (backend == nullptr ||
        !sl_v8_throw_error_from_native_view(backend, sl_str_from_cstr(message)))
    {
        isolate->ThrowException(
            v8::Exception::Error(v8::String::NewFromUtf8Literal(isolate, "FFI error")));
    }
}

bool ffi_set_function(v8::Isolate* isolate, v8::Local<v8::Context> context,
                      v8::Local<v8::Object> object, const char* name, v8::FunctionCallback callback,
                      v8::Local<v8::Value> data = {})
{
    v8::Local<v8::String> key;
    v8::Local<v8::Function> function;
    SlV8Engine* backend = ffi_backend(isolate);
    if (backend == nullptr ||
        !sl_status_is_ok(sl_v8_string_from_native_view(backend, sl_str_from_cstr(name), &key)))
    {
        return false;
    }
    v8::Local<v8::FunctionTemplate> tmpl = data.IsEmpty()
                                               ? v8::FunctionTemplate::New(isolate, callback)
                                               : v8::FunctionTemplate::New(isolate, callback, data);
    if (!tmpl->GetFunction(context).ToLocal(&function)) {
        return false;
    }
    return object->Set(context, key, function).FromMaybe(false);
}

bool ffi_get_private(v8::Isolate* isolate, const char* name, v8::Local<v8::Private>* out)
{
    SlV8Engine* backend = ffi_backend(isolate);
    v8::Local<v8::String> key;
    if (out == nullptr || backend == nullptr ||
        !sl_status_is_ok(sl_v8_string_from_native_view(backend, sl_str_from_cstr(name), &key)))
    {
        return false;
    }
    *out = v8::Private::ForApi(isolate, key);
    return true;
}

SlV8FfiResource* ffi_resource_from_value(v8::Isolate* isolate, v8::Local<v8::Value> value)
{
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Private> key;
    v8::Local<v8::Value> private_value;
    if (value.IsEmpty() || !value->IsObject() ||
        !ffi_get_private(isolate, "sloppy.ffi.resource", &key) ||
        !value.As<v8::Object>()->GetPrivate(context, key).ToLocal(&private_value) ||
        !private_value->IsExternal())
    {
        return nullptr;
    }
    return static_cast<SlV8FfiResource*>(
        private_value.As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
}

SlV8FfiResource* ffi_new_resource(SlV8Engine* backend, FfiResourceKind kind)
{
    auto* resource = new (std::nothrow) SlV8FfiResource();
    if (resource == nullptr) {
        return nullptr;
    }
    resource->kind = kind;
    backend->ffi_resources.push_back(resource);
    return resource;
}

bool ffi_set_resource(v8::Isolate* isolate, v8::Local<v8::Context> context,
                      v8::Local<v8::Object> object, SlV8FfiResource* resource)
{
    v8::Local<v8::Private> key;
    return ffi_get_private(isolate, "sloppy.ffi.resource", &key) &&
           object
               ->SetPrivate(context, key,
                            v8::External::New(isolate, static_cast<void*>(resource),
                                              v8::kExternalPointerTypeTagDefault))
               .FromMaybe(false);
}

bool ffi_make_resource_object(v8::Isolate* isolate, v8::Local<v8::Context> context,
                              SlV8FfiResource* resource, v8::Local<v8::Object>* out);

bool ffi_string_value(v8::Isolate* isolate, v8::Local<v8::Value> value, std::string* out)
{
    return out != nullptr && value->IsString() && sl_v8_std_string_from_value(isolate, value, out);
}

bool ffi_has_nul(const std::string& value)
{
    return std::find(value.begin(), value.end(), '\0') != value.end();
}

bool ffi_number_integral_value(v8::Isolate* isolate, v8::Local<v8::Value> value, double* out)
{
    if (out == nullptr || !value->IsNumber()) {
        return false;
    }
    v8::Maybe<double> maybe = value->NumberValue(isolate->GetCurrentContext());
    if (maybe.IsNothing()) {
        return false;
    }
    double number = maybe.FromJust();
    if (!std::isfinite(number) || std::floor(number) != number) {
        return false;
    }
    *out = number;
    return true;
}

bool ffi_signed_integer_out_of_range(v8::Isolate* isolate, v8::Local<v8::Value> value, int64_t min,
                                     int64_t max)
{
    if (value->IsBigInt()) {
        bool lossless = false;
        int64_t result = value.As<v8::BigInt>()->Int64Value(&lossless);
        return !lossless || result < min || result > max;
    }
    double number = 0.0;
    return ffi_number_integral_value(isolate, value, &number) &&
           (number < static_cast<double>(min) || number > static_cast<double>(max));
}

bool ffi_unsigned_integer_out_of_range(v8::Isolate* isolate, v8::Local<v8::Value> value,
                                       uint64_t max)
{
    if (value->IsBigInt()) {
        bool lossless = false;
        uint64_t result = value.As<v8::BigInt>()->Uint64Value(&lossless);
        return !lossless || result > max;
    }
    double number = 0.0;
    return ffi_number_integral_value(isolate, value, &number) &&
           (number < 0.0 || number > static_cast<double>(max));
}

bool ffi_type_from_value(v8::Isolate* isolate, v8::Local<v8::Value> value, SlPlanFfiType* out)
{
    std::string type_name;
    if (!ffi_string_value(isolate, value, &type_name)) {
        return false;
    }
    return sl_status_is_ok(
        sl_plan_ffi_type_from_str(sl_str_from_parts(type_name.data(), type_name.size()), out));
}

bool ffi_plan_type_from_name(const std::string& type_name, SlPlanFfiType* out)
{
    return out != nullptr && sl_status_is_ok(sl_plan_ffi_type_from_str(
                                 sl_str_from_parts(type_name.data(), type_name.size()), out));
}

bool ffi_object_string(v8::Isolate* isolate, v8::Local<v8::Context> context,
                       v8::Local<v8::Object> object, const char* name, std::string* out)
{
    v8::Local<v8::String> key;
    v8::Local<v8::Value> value;
    SlV8Engine* backend = ffi_backend(isolate);
    return backend != nullptr &&
           sl_status_is_ok(sl_v8_string_from_native_view(backend, sl_str_from_cstr(name), &key)) &&
           object->Get(context, key).ToLocal(&value) && ffi_string_value(isolate, value, out);
}

bool ffi_object_value(v8::Isolate* isolate, v8::Local<v8::Context> context,
                      v8::Local<v8::Object> object, const char* name, v8::Local<v8::Value>* out)
{
    v8::Local<v8::String> key;
    SlV8Engine* backend = ffi_backend(isolate);
    return out != nullptr && backend != nullptr &&
           sl_status_is_ok(sl_v8_string_from_native_view(backend, sl_str_from_cstr(name), &key)) &&
           object->Get(context, key).ToLocal(out);
}

bool ffi_object_string_array(v8::Isolate* isolate, v8::Local<v8::Context> context,
                             v8::Local<v8::Object> object, const char* name,
                             std::vector<std::string>* out)
{
    v8::Local<v8::String> key;
    v8::Local<v8::Value> value;
    SlV8Engine* backend = ffi_backend(isolate);
    if (out == nullptr || backend == nullptr ||
        !sl_status_is_ok(sl_v8_string_from_native_view(backend, sl_str_from_cstr(name), &key)) ||
        !object->Get(context, key).ToLocal(&value) || !value->IsArray())
    {
        return false;
    }
    v8::Local<v8::Array> array = value.As<v8::Array>();
    for (uint32_t index = 0U; index < array->Length(); index += 1U) {
        v8::Local<v8::Value> element;
        std::string text;
        if (!array->Get(context, index).ToLocal(&element) ||
            !ffi_string_value(isolate, element, &text))
        {
            return false;
        }
        out->push_back(text);
    }
    return true;
}

bool ffi_descriptor_matches(v8::Isolate* isolate, v8::Local<v8::Context> context,
                            v8::Local<v8::Value> value, const SlFfiFunction* function)
{
    std::string kind;
    std::string return_type;
    std::vector<std::string> parameters;
    if (function == nullptr || !value->IsObject()) {
        return false;
    }
    v8::Local<v8::Object> object = value.As<v8::Object>();
    if (!ffi_object_string(isolate, context, object, "kind", &kind) || kind != "ffi.fn" ||
        !ffi_object_string(isolate, context, object, "returnType", &return_type) ||
        !ffi_object_string_array(isolate, context, object, "parameters", &parameters))
    {
        return false;
    }
    SlPlanFfiType parsed_return = SL_PLAN_FFI_TYPE_UNKNOWN;
    if (!ffi_plan_type_from_name(return_type, &parsed_return) ||
        parsed_return != function->return_type || parameters.size() != function->parameter_count)
    {
        return false;
    }
    for (size_t index = 0U; index < parameters.size(); index += 1U) {
        SlPlanFfiType parsed_parameter = SL_PLAN_FFI_TYPE_UNKNOWN;
        if (!ffi_plan_type_from_name(parameters[index], &parsed_parameter) ||
            parsed_parameter != function->parameters[index])
        {
            return false;
        }
    }
    return true;
}

bool ffi_i64_from_value(v8::Isolate* isolate, v8::Local<v8::Value> value, int64_t min, int64_t max,
                        int64_t* out)
{
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    int64_t result = 0;
    if (value->IsBigInt()) {
        bool lossless = false;
        result = value.As<v8::BigInt>()->Int64Value(&lossless);
        if (!lossless) {
            return false;
        }
    }
    else {
        if (!value->IsNumber()) {
            return false;
        }
        v8::Maybe<double> maybe = value->NumberValue(context);
        if (maybe.IsNothing()) {
            return false;
        }
        double number = maybe.FromJust();
        if (!std::isfinite(number) || std::floor(number) != number ||
            number < static_cast<double>(min) || number > static_cast<double>(max))
        {
            return false;
        }
        result = static_cast<int64_t>(number);
    }
    if (result < min || result > max) {
        return false;
    }
    *out = result;
    return true;
}

bool ffi_u64_from_value(v8::Isolate* isolate, v8::Local<v8::Value> value, uint64_t max,
                        uint64_t* out)
{
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    uint64_t result = 0U;
    if (value->IsBigInt()) {
        bool lossless = false;
        result = value.As<v8::BigInt>()->Uint64Value(&lossless);
        if (!lossless) {
            return false;
        }
    }
    else {
        if (!value->IsNumber()) {
            return false;
        }
        v8::Maybe<double> maybe = value->NumberValue(context);
        if (maybe.IsNothing()) {
            return false;
        }
        double number = maybe.FromJust();
        if (!std::isfinite(number) || std::floor(number) != number || number < 0.0 ||
            number > static_cast<double>(max))
        {
            return false;
        }
        result = static_cast<uint64_t>(number);
    }
    if (result > max) {
        return false;
    }
    *out = result;
    return true;
}

bool ffi_i64_from_bigint(v8::Local<v8::Value> value, int64_t* out)
{
    if (out == nullptr || !value->IsBigInt()) {
        return false;
    }
    bool lossless = false;
    int64_t result = value.As<v8::BigInt>()->Int64Value(&lossless);
    if (!lossless) {
        return false;
    }
    *out = result;
    return true;
}

bool ffi_u64_from_bigint(v8::Local<v8::Value> value, uint64_t* out)
{
    if (out == nullptr || !value->IsBigInt()) {
        return false;
    }
    bool lossless = false;
    uint64_t result = value.As<v8::BigInt>()->Uint64Value(&lossless);
    if (!lossless) {
        return false;
    }
    *out = result;
    return true;
}

template <typename T> void ffi_store(unsigned char* out, T value)
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
    std::copy_n(bytes, sizeof(T), out);
}

template <typename T> T ffi_load(const unsigned char* data)
{
    T value{};
    auto* bytes = reinterpret_cast<unsigned char*>(&value);
    std::copy_n(data, sizeof(T), bytes);
    return value;
}

bool ffi_pointer_from_value(v8::Isolate* isolate, v8::Local<v8::Value> value, void** out)
{
    if (value->IsNull()) {
        *out = nullptr;
        return true;
    }
    SlV8FfiResource* resource = ffi_resource_from_value(isolate, value);
    if (resource == nullptr || resource->disposed ||
        resource->kind == FfiResourceKind::StructLayout)
    {
        return false;
    }
    if (resource->kind == FfiResourceKind::NativePointer) {
        *out = resource->native_pointer;
        return true;
    }
    if (resource->bytes.empty()) {
        return false;
    }
    *out = resource->bytes.data();
    return true;
}

bool ffi_write_value_to_bytes(v8::Isolate* isolate, SlPlanFfiType type, v8::Local<v8::Value> value,
                              unsigned char* out, size_t capacity)
{
    int64_t i64 = 0;
    uint64_t u64 = 0U;
    void* pointer = nullptr;
    v8::Local<v8::Context> context = isolate->GetCurrentContext();

    if (out == nullptr || capacity < sl_ffi_type_size(type)) {
        return false;
    }
    switch (type) {
    case SL_PLAN_FFI_TYPE_BOOL:
        if (!value->IsBoolean()) {
            return false;
        }
        out[0] = value->BooleanValue(isolate) ? 1U : 0U;
        return true;
    case SL_PLAN_FFI_TYPE_I8:
        if (!ffi_i64_from_value(isolate, value, INT8_MIN, INT8_MAX, &i64)) {
            return false;
        }
        ffi_store<int8_t>(out, static_cast<int8_t>(i64));
        return true;
    case SL_PLAN_FFI_TYPE_U8:
        if (!ffi_u64_from_value(isolate, value, UINT8_MAX, &u64)) {
            return false;
        }
        ffi_store<uint8_t>(out, static_cast<uint8_t>(u64));
        return true;
    case SL_PLAN_FFI_TYPE_I16:
        if (!ffi_i64_from_value(isolate, value, INT16_MIN, INT16_MAX, &i64)) {
            return false;
        }
        ffi_store<int16_t>(out, static_cast<int16_t>(i64));
        return true;
    case SL_PLAN_FFI_TYPE_U16:
        if (!ffi_u64_from_value(isolate, value, UINT16_MAX, &u64)) {
            return false;
        }
        ffi_store<uint16_t>(out, static_cast<uint16_t>(u64));
        return true;
    case SL_PLAN_FFI_TYPE_I32:
        if (!ffi_i64_from_value(isolate, value, INT32_MIN, INT32_MAX, &i64)) {
            return false;
        }
        ffi_store<int32_t>(out, static_cast<int32_t>(i64));
        return true;
    case SL_PLAN_FFI_TYPE_U32:
        if (!ffi_u64_from_value(isolate, value, UINT32_MAX, &u64)) {
            return false;
        }
        ffi_store<uint32_t>(out, static_cast<uint32_t>(u64));
        return true;
    case SL_PLAN_FFI_TYPE_I64:
        if (!ffi_i64_from_bigint(value, &i64)) {
            return false;
        }
        ffi_store<int64_t>(out, i64);
        return true;
    case SL_PLAN_FFI_TYPE_ISIZE:
        if (sizeof(void*) == sizeof(int64_t) &&
            !ffi_i64_from_value(isolate, value, std::numeric_limits<int64_t>::min(),
                                std::numeric_limits<int64_t>::max(), &i64))
        {
            return false;
        }
        if (sizeof(void*) != sizeof(int64_t) &&
            !ffi_i64_from_value(isolate, value, INT32_MIN, INT32_MAX, &i64))
        {
            return false;
        }
        if (sizeof(void*) == sizeof(int64_t)) {
            ffi_store<int64_t>(out, i64);
        }
        else {
            ffi_store<int32_t>(out, static_cast<int32_t>(i64));
        }
        return true;
    case SL_PLAN_FFI_TYPE_U64:
        if (!ffi_u64_from_bigint(value, &u64)) {
            return false;
        }
        ffi_store<uint64_t>(out, u64);
        return true;
    case SL_PLAN_FFI_TYPE_USIZE:
        if (sizeof(void*) == sizeof(uint64_t) &&
            !ffi_u64_from_value(isolate, value, std::numeric_limits<uint64_t>::max(), &u64))
        {
            return false;
        }
        if (sizeof(void*) != sizeof(uint64_t) &&
            !ffi_u64_from_value(isolate, value, UINT32_MAX, &u64))
        {
            return false;
        }
        if (sizeof(void*) == sizeof(uint64_t)) {
            ffi_store<uint64_t>(out, u64);
        }
        else {
            ffi_store<uint32_t>(out, static_cast<uint32_t>(u64));
        }
        return true;
    case SL_PLAN_FFI_TYPE_F32: {
        if (!value->IsNumber()) {
            return false;
        }
        v8::Maybe<double> number = value->NumberValue(context);
        if (number.IsNothing() || !std::isfinite(number.FromJust())) {
            return false;
        }
        ffi_store<float>(out, static_cast<float>(number.FromJust()));
        return true;
    }
    case SL_PLAN_FFI_TYPE_F64: {
        if (!value->IsNumber()) {
            return false;
        }
        v8::Maybe<double> number = value->NumberValue(context);
        if (number.IsNothing() || !std::isfinite(number.FromJust())) {
            return false;
        }
        ffi_store<double>(out, number.FromJust());
        return true;
    }
    case SL_PLAN_FFI_TYPE_PTR:
        if (!ffi_pointer_from_value(isolate, value, &pointer)) {
            return false;
        }
        ffi_store<void*>(out, pointer);
        return true;
    default:
        return false;
    }
}

v8::Local<v8::Value> ffi_value_from_bytes(v8::Isolate* isolate, SlPlanFfiType type,
                                          const unsigned char* data)
{
    if (data == nullptr) {
        return v8::Undefined(isolate);
    }
    switch (type) {
    case SL_PLAN_FFI_TYPE_BOOL:
        return v8::Boolean::New(isolate, data[0] != 0U);
    case SL_PLAN_FFI_TYPE_I8:
        return v8::Integer::New(isolate, ffi_load<int8_t>(data));
    case SL_PLAN_FFI_TYPE_U8:
        return v8::Integer::NewFromUnsigned(isolate, ffi_load<uint8_t>(data));
    case SL_PLAN_FFI_TYPE_I16:
        return v8::Integer::New(isolate, ffi_load<int16_t>(data));
    case SL_PLAN_FFI_TYPE_U16:
        return v8::Integer::NewFromUnsigned(isolate, ffi_load<uint16_t>(data));
    case SL_PLAN_FFI_TYPE_I32:
        return v8::Integer::New(isolate, ffi_load<int32_t>(data));
    case SL_PLAN_FFI_TYPE_U32:
        return v8::Integer::NewFromUnsigned(isolate, ffi_load<uint32_t>(data));
    case SL_PLAN_FFI_TYPE_I64:
        return v8::BigInt::New(isolate, ffi_load<int64_t>(data));
    case SL_PLAN_FFI_TYPE_ISIZE:
        return v8::BigInt::New(isolate, sizeof(void*) == sizeof(int64_t) ? ffi_load<int64_t>(data)
                                                                         : ffi_load<int32_t>(data));
    case SL_PLAN_FFI_TYPE_U64:
        return v8::BigInt::NewFromUnsigned(isolate, ffi_load<uint64_t>(data));
    case SL_PLAN_FFI_TYPE_USIZE:
        return v8::BigInt::NewFromUnsigned(isolate, sizeof(void*) == sizeof(uint64_t)
                                                        ? ffi_load<uint64_t>(data)
                                                        : ffi_load<uint32_t>(data));
    case SL_PLAN_FFI_TYPE_F32:
        return v8::Number::New(isolate, ffi_load<float>(data));
    case SL_PLAN_FFI_TYPE_F64:
        return v8::Number::New(isolate, ffi_load<double>(data));
    case SL_PLAN_FFI_TYPE_PTR: {
        void* pointer = ffi_load<void*>(data);
        return pointer == nullptr ? v8::Null(isolate) : v8::Undefined(isolate);
    }
    default:
        return v8::Undefined(isolate);
    }
}

bool ffi_marshal_arg(v8::Isolate* isolate, const SlFfiFunction* function, size_t index,
                     v8::Local<v8::Value> value, FfiArgStorage* storage, void** out)
{
    SlPlanFfiType type = function->parameters[index];
    int64_t i64 = 0;
    uint64_t u64 = 0U;
    v8::Local<v8::Context> context = isolate->GetCurrentContext();

    switch (type) {
    case SL_PLAN_FFI_TYPE_BOOL:
        if (!value->IsBoolean()) {
            return false;
        }
        storage->value.u8 = value->BooleanValue(isolate) ? 1U : 0U;
        *out = &storage->value.u8;
        return true;
    case SL_PLAN_FFI_TYPE_I8:
        if (!ffi_i64_from_value(isolate, value, INT8_MIN, INT8_MAX, &i64)) {
            return false;
        }
        storage->value.i8 = static_cast<int8_t>(i64);
        *out = &storage->value.i8;
        return true;
    case SL_PLAN_FFI_TYPE_U8:
        if (!ffi_u64_from_value(isolate, value, UINT8_MAX, &u64)) {
            return false;
        }
        storage->value.u8 = static_cast<uint8_t>(u64);
        *out = &storage->value.u8;
        return true;
    case SL_PLAN_FFI_TYPE_I16:
        if (!ffi_i64_from_value(isolate, value, INT16_MIN, INT16_MAX, &i64)) {
            return false;
        }
        storage->value.i16 = static_cast<int16_t>(i64);
        *out = &storage->value.i16;
        return true;
    case SL_PLAN_FFI_TYPE_U16:
        if (!ffi_u64_from_value(isolate, value, UINT16_MAX, &u64)) {
            return false;
        }
        storage->value.u16 = static_cast<uint16_t>(u64);
        *out = &storage->value.u16;
        return true;
    case SL_PLAN_FFI_TYPE_I32:
        if (!ffi_i64_from_value(isolate, value, INT32_MIN, INT32_MAX, &i64)) {
            return false;
        }
        storage->value.i32 = static_cast<int32_t>(i64);
        *out = &storage->value.i32;
        return true;
    case SL_PLAN_FFI_TYPE_U32:
        if (!ffi_u64_from_value(isolate, value, UINT32_MAX, &u64)) {
            return false;
        }
        storage->value.u32 = static_cast<uint32_t>(u64);
        *out = &storage->value.u32;
        return true;
    case SL_PLAN_FFI_TYPE_I64:
        if (!ffi_i64_from_bigint(value, &storage->value.i64)) {
            return false;
        }
        *out = &storage->value.i64;
        return true;
    case SL_PLAN_FFI_TYPE_ISIZE:
        if (sizeof(void*) == sizeof(int64_t) &&
            !ffi_i64_from_value(isolate, value, std::numeric_limits<int64_t>::min(),
                                std::numeric_limits<int64_t>::max(), &i64))
        {
            return false;
        }
        if (sizeof(void*) != sizeof(int64_t) &&
            !ffi_i64_from_value(isolate, value, INT32_MIN, INT32_MAX, &i64))
        {
            return false;
        }
        storage->value.isize = static_cast<intptr_t>(i64);
        *out = &storage->value.isize;
        return true;
    case SL_PLAN_FFI_TYPE_U64:
        if (!ffi_u64_from_bigint(value, &storage->value.u64)) {
            return false;
        }
        *out = &storage->value.u64;
        return true;
    case SL_PLAN_FFI_TYPE_USIZE:
        if (sizeof(void*) == sizeof(uint64_t) &&
            !ffi_u64_from_value(isolate, value, std::numeric_limits<uint64_t>::max(), &u64))
        {
            return false;
        }
        if (sizeof(void*) != sizeof(uint64_t) &&
            !ffi_u64_from_value(isolate, value, UINT32_MAX, &u64))
        {
            return false;
        }
        storage->value.usize = static_cast<uintptr_t>(u64);
        *out = &storage->value.usize;
        return true;
    case SL_PLAN_FFI_TYPE_F32: {
        if (!value->IsNumber()) {
            return false;
        }
        v8::Maybe<double> number = value->NumberValue(context);
        if (number.IsNothing() || !std::isfinite(number.FromJust())) {
            return false;
        }
        storage->value.f32 = static_cast<float>(number.FromJust());
        *out = &storage->value.f32;
        return true;
    }
    case SL_PLAN_FFI_TYPE_F64: {
        if (!value->IsNumber()) {
            return false;
        }
        v8::Maybe<double> number = value->NumberValue(context);
        if (number.IsNothing() || !std::isfinite(number.FromJust())) {
            return false;
        }
        storage->value.f64 = number.FromJust();
        *out = &storage->value.f64;
        return true;
    }
    case SL_PLAN_FFI_TYPE_PTR:
        if (!ffi_pointer_from_value(isolate, value, &storage->value.ptr)) {
            return false;
        }
        *out = &storage->value.ptr;
        return true;
    case SL_PLAN_FFI_TYPE_CSTRING:
        if (!ffi_string_value(isolate, value, &storage->utf8) || ffi_has_nul(storage->utf8)) {
            return false;
        }
        storage->utf8.push_back('\0');
        storage->value.ptr = storage->utf8.data();
        *out = &storage->value.ptr;
        return true;
    case SL_PLAN_FFI_TYPE_UTF16: {
        if (!value->IsString()) {
            return false;
        }
        v8::String::Value utf16(isolate, value);
        if (*utf16 == nullptr) {
            return false;
        }
        for (uint32_t i = 0; i < utf16.length(); i += 1U) {
            if ((*utf16)[static_cast<int>(i)] == 0U) {
                return false;
            }
            storage->utf16.push_back(static_cast<uint16_t>((*utf16)[static_cast<int>(i)]));
        }
        storage->utf16.push_back(0U);
        storage->value.ptr = storage->utf16.data();
        *out = &storage->value.ptr;
        return true;
    }
    case SL_PLAN_FFI_TYPE_BYTES:
    case SL_PLAN_FFI_TYPE_MUT_BYTES:
        if (value->IsUint8Array()) {
            v8::Local<v8::Uint8Array> view = value.As<v8::Uint8Array>();
            std::shared_ptr<v8::BackingStore> backing = view->Buffer()->GetBackingStore();
            if (backing == nullptr || view->ByteOffset() > backing->ByteLength() ||
                view->ByteLength() > backing->ByteLength() - view->ByteOffset())
            {
                return false;
            }
            storage->value.ptr = static_cast<unsigned char*>(backing->Data()) + view->ByteOffset();
            *out = &storage->value.ptr;
            return true;
        }
        if (!ffi_pointer_from_value(isolate, value, &storage->value.ptr)) {
            return false;
        }
        *out = &storage->value.ptr;
        return true;
    default:
        return false;
    }
}

FfiMarshalError ffi_marshal_error_for_value(v8::Isolate* isolate, SlPlanFfiType type,
                                            v8::Local<v8::Value> value)
{
    std::string text;

    switch (type) {
    case SL_PLAN_FFI_TYPE_I8:
        return ffi_signed_integer_out_of_range(isolate, value, INT8_MIN, INT8_MAX)
                   ? FfiMarshalError::IntegerOutOfRange
                   : FfiMarshalError::InvalidType;
    case SL_PLAN_FFI_TYPE_U8:
        return ffi_unsigned_integer_out_of_range(isolate, value, UINT8_MAX)
                   ? FfiMarshalError::IntegerOutOfRange
                   : FfiMarshalError::InvalidType;
    case SL_PLAN_FFI_TYPE_I16:
        return ffi_signed_integer_out_of_range(isolate, value, INT16_MIN, INT16_MAX)
                   ? FfiMarshalError::IntegerOutOfRange
                   : FfiMarshalError::InvalidType;
    case SL_PLAN_FFI_TYPE_U16:
        return ffi_unsigned_integer_out_of_range(isolate, value, UINT16_MAX)
                   ? FfiMarshalError::IntegerOutOfRange
                   : FfiMarshalError::InvalidType;
    case SL_PLAN_FFI_TYPE_I32:
        return ffi_signed_integer_out_of_range(isolate, value, INT32_MIN, INT32_MAX)
                   ? FfiMarshalError::IntegerOutOfRange
                   : FfiMarshalError::InvalidType;
    case SL_PLAN_FFI_TYPE_U32:
        return ffi_unsigned_integer_out_of_range(isolate, value, UINT32_MAX)
                   ? FfiMarshalError::IntegerOutOfRange
                   : FfiMarshalError::InvalidType;
    case SL_PLAN_FFI_TYPE_I64:
        return value->IsBigInt() && ffi_signed_integer_out_of_range(
                                        isolate, value, std::numeric_limits<int64_t>::min(),
                                        std::numeric_limits<int64_t>::max())
                   ? FfiMarshalError::IntegerOutOfRange
                   : FfiMarshalError::InvalidType;
    case SL_PLAN_FFI_TYPE_U64:
        return value->IsBigInt() && ffi_unsigned_integer_out_of_range(
                                        isolate, value, std::numeric_limits<uint64_t>::max())
                   ? FfiMarshalError::IntegerOutOfRange
                   : FfiMarshalError::InvalidType;
    case SL_PLAN_FFI_TYPE_ISIZE:
        return sizeof(void*) == sizeof(int64_t)
                   ? (ffi_signed_integer_out_of_range(isolate, value,
                                                      std::numeric_limits<int64_t>::min(),
                                                      std::numeric_limits<int64_t>::max())
                          ? FfiMarshalError::IntegerOutOfRange
                          : FfiMarshalError::InvalidType)
                   : (ffi_signed_integer_out_of_range(isolate, value, INT32_MIN, INT32_MAX)
                          ? FfiMarshalError::IntegerOutOfRange
                          : FfiMarshalError::InvalidType);
    case SL_PLAN_FFI_TYPE_USIZE:
        return sizeof(void*) == sizeof(uint64_t)
                   ? (ffi_unsigned_integer_out_of_range(isolate, value,
                                                        std::numeric_limits<uint64_t>::max())
                          ? FfiMarshalError::IntegerOutOfRange
                          : FfiMarshalError::InvalidType)
                   : (ffi_unsigned_integer_out_of_range(isolate, value, UINT32_MAX)
                          ? FfiMarshalError::IntegerOutOfRange
                          : FfiMarshalError::InvalidType);
    case SL_PLAN_FFI_TYPE_CSTRING:
        return ffi_string_value(isolate, value, &text) && ffi_has_nul(text)
                   ? FfiMarshalError::StringNul
                   : FfiMarshalError::InvalidType;
    case SL_PLAN_FFI_TYPE_UTF16:
        if (value->IsString()) {
            v8::String::Value utf16(isolate, value);
            if (*utf16 != nullptr) {
                for (uint32_t i = 0; i < utf16.length(); i += 1U) {
                    if ((*utf16)[static_cast<int>(i)] == 0U) {
                        return FfiMarshalError::StringNul;
                    }
                }
            }
        }
        return FfiMarshalError::InvalidType;
    default:
        return FfiMarshalError::InvalidType;
    }
}

const char* ffi_marshal_error_message(FfiMarshalError error)
{
    switch (error) {
    case FfiMarshalError::IntegerOutOfRange:
        return "SLOPPY_E_FFI_INTEGER_OUT_OF_RANGE: FFI integer argument is out of range.";
    case FfiMarshalError::StringNul:
        return "SLOPPY_E_FFI_STRING_NUL: FFI strings cannot contain NUL.";
    case FfiMarshalError::InvalidType:
    default:
        return "SLOPPY_E_FFI_INVALID_ARGUMENT_TYPE: invalid FFI argument.";
    }
}

v8::Local<v8::Value> ffi_return_value(v8::Isolate* isolate, const SlFfiFunction* function,
                                      FfiArgStorage* result)
{
    switch (function->return_type) {
    case SL_PLAN_FFI_TYPE_VOID:
        return v8::Undefined(isolate);
    case SL_PLAN_FFI_TYPE_BOOL:
        return v8::Boolean::New(isolate, result->value.u8 != 0U);
    case SL_PLAN_FFI_TYPE_I8:
        return v8::Integer::New(isolate, result->value.i8);
    case SL_PLAN_FFI_TYPE_U8:
        return v8::Integer::NewFromUnsigned(isolate, result->value.u8);
    case SL_PLAN_FFI_TYPE_I16:
        return v8::Integer::New(isolate, result->value.i16);
    case SL_PLAN_FFI_TYPE_U16:
        return v8::Integer::NewFromUnsigned(isolate, result->value.u16);
    case SL_PLAN_FFI_TYPE_I32:
        return v8::Integer::New(isolate, result->value.i32);
    case SL_PLAN_FFI_TYPE_U32:
        return v8::Integer::NewFromUnsigned(isolate, result->value.u32);
    case SL_PLAN_FFI_TYPE_I64:
        return v8::BigInt::New(isolate, result->value.i64);
    case SL_PLAN_FFI_TYPE_ISIZE:
        return v8::BigInt::New(isolate, static_cast<int64_t>(result->value.isize));
    case SL_PLAN_FFI_TYPE_U64:
        return v8::BigInt::NewFromUnsigned(isolate, result->value.u64);
    case SL_PLAN_FFI_TYPE_USIZE:
        return v8::BigInt::NewFromUnsigned(isolate, static_cast<uint64_t>(result->value.usize));
    case SL_PLAN_FFI_TYPE_F32:
        return v8::Number::New(isolate, result->value.f32);
    case SL_PLAN_FFI_TYPE_F64:
        return v8::Number::New(isolate, result->value.f64);
    case SL_PLAN_FFI_TYPE_PTR:
        if (result->value.ptr == nullptr) {
            return v8::Null(isolate);
        }
        {
            SlV8Engine* backend = ffi_backend(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Object> object;
            SlV8FfiResource* resource =
                backend == nullptr ? nullptr
                                   : ffi_new_resource(backend, FfiResourceKind::NativePointer);
            if (resource == nullptr) {
                ffi_throw_error(
                    isolate,
                    "SLOPPY_E_FFI_CALL_FAILED: failed to allocate NativePointer resource.");
                return v8::Undefined(isolate);
            }
            resource->native_pointer = result->value.ptr;
            if (!ffi_make_resource_object(isolate, context, resource, &object)) {
                ffi_throw_error(
                    isolate, "SLOPPY_E_FFI_CALL_FAILED: failed to create NativePointer resource.");
                return v8::Undefined(isolate);
            }
            return object;
        }
    default:
        return v8::Undefined(isolate);
    }
}

void ffi_call_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    const SlFfiFunction* function = static_cast<const SlFfiFunction*>(
        args.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    if (function == nullptr) {
        ffi_throw_error(isolate, "SLOPPY_E_FFI_RUNTIME_UNAVAILABLE: FFI function is not bound.");
        return;
    }
    if (args.Length() != static_cast<int>(function->parameter_count)) {
        ffi_throw_type_error(isolate,
                             "SLOPPY_E_FFI_INVALID_ARGUMENT_COUNT: wrong FFI argument count.");
        return;
    }
    std::vector<FfiArgStorage> storage(function->parameter_count);
    std::vector<void*> ffi_args(function->parameter_count);
    for (size_t index = 0U; index < function->parameter_count; index += 1U) {
        if (!ffi_marshal_arg(isolate, function, index, args[static_cast<int>(index)],
                             &storage[index], &ffi_args[index]))
        {
            ffi_throw_type_error(
                isolate, ffi_marshal_error_message(ffi_marshal_error_for_value(
                             isolate, function->parameters[index], args[static_cast<int>(index)])));
            return;
        }
    }
    FfiArgStorage result;
    ffi_call(const_cast<ffi_cif*>(&function->cif), FFI_FN(function->symbol_ptr), &result.value,
             ffi_args.empty() ? nullptr : ffi_args.data());
    args.GetReturnValue().Set(ffi_return_value(isolate, function, &result));
}

void ffi_library_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = ffi_backend(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::string library_name;

    if (backend == nullptr || !backend->ffi_registry_initialized) {
        ffi_throw_error(isolate,
                        "SLOPPY_E_FFI_RUNTIME_UNAVAILABLE: FFI registry is not initialized.");
        return;
    }
    if (args.Length() < 2 || !ffi_string_value(isolate, args[0], &library_name) ||
        !args[1]->IsObject())
    {
        ffi_throw_type_error(isolate, "unsafeFfi.library requires a name and descriptor object.");
        return;
    }

    v8::Local<v8::Object> descriptors = args[1].As<v8::Object>();
    v8::Local<v8::Array> names;
    if (!descriptors->GetOwnPropertyNames(context).ToLocal(&names)) {
        ffi_throw_error(isolate, "SLOPPY_E_FFI_CALL_FAILED: failed to enumerate FFI descriptors.");
        return;
    }
    v8::Local<v8::Object> output = v8::Object::New(isolate);
    for (uint32_t index = 0U; index < names->Length(); index += 1U) {
        v8::Local<v8::Value> key_value;
        v8::Local<v8::Value> descriptor_value;
        std::string function_name;
        const SlFfiFunction* function = nullptr;
        if (!names->Get(context, index).ToLocal(&key_value) ||
            !ffi_string_value(isolate, key_value, &function_name) ||
            !descriptors->Get(context, key_value).ToLocal(&descriptor_value))
        {
            ffi_throw_error(isolate, "SLOPPY_E_FFI_CALL_FAILED: invalid FFI descriptor.");
            return;
        }
        SlStatus status = sl_ffi_registry_find(
            &backend->ffi_registry, sl_str_from_parts(library_name.data(), library_name.size()),
            sl_str_from_parts(function_name.data(), function_name.size()), &function);
        if (!sl_status_is_ok(status)) {
            ffi_throw_error(
                isolate, "SLOPPYC_E_FFI_SYMBOL_NOT_FOUND: FFI function is not in Plan metadata.");
            return;
        }
        if (!ffi_descriptor_matches(isolate, context, descriptor_value, function)) {
            ffi_throw_type_error(
                isolate,
                "SLOPPY_E_FFI_INVALID_ARGUMENT_TYPE: FFI descriptor does not match Plan metadata.");
            return;
        }
        v8::Local<v8::Function> callable;
        if (!v8::FunctionTemplate::New(isolate, ffi_call_callback,
                                       v8::External::New(isolate,
                                                         const_cast<SlFfiFunction*>(function),
                                                         v8::kExternalPointerTypeTagDefault))
                 ->GetFunction(context)
                 .ToLocal(&callable) ||
            !output->Set(context, key_value, callable).FromMaybe(false))
        {
            ffi_throw_error(isolate, "SLOPPY_E_FFI_CALL_FAILED: failed to bind FFI function.");
            return;
        }
    }
    (void)output->SetIntegrityLevel(context, v8::IntegrityLevel::kFrozen);
    args.GetReturnValue().Set(output);
}

void ffi_dispose_resource_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    SlV8FfiResource* resource = ffi_resource_from_value(args.GetIsolate(), args.This());
    if (resource != nullptr) {
        resource->disposed = true;
        resource->bytes.clear();
        resource->byte_length = 0U;
    }
    args.GetReturnValue().Set(v8::Undefined(args.GetIsolate()));
}

void ffi_native_pointer_is_null_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8FfiResource* resource = ffi_resource_from_value(isolate, args.This());
    if (resource == nullptr || resource->kind != FfiResourceKind::NativePointer) {
        ffi_throw_type_error(isolate, "NativePointer resource is invalid.");
        return;
    }
    args.GetReturnValue().Set(v8::Boolean::New(isolate, resource->native_pointer == nullptr));
}

bool ffi_copy_uint8_array(v8::Local<v8::Value> value, const unsigned char** data, size_t* length)
{
    if (data == nullptr || length == nullptr || !value->IsUint8Array()) {
        return false;
    }
    v8::Local<v8::Uint8Array> view = value.As<v8::Uint8Array>();
    std::shared_ptr<v8::BackingStore> backing = view->Buffer()->GetBackingStore();
    if (backing == nullptr || view->ByteOffset() > backing->ByteLength() ||
        view->ByteLength() > backing->ByteLength() - view->ByteOffset())
    {
        return false;
    }
    *data = static_cast<const unsigned char*>(backing->Data()) + view->ByteOffset();
    *length = view->ByteLength();
    return true;
}

void ffi_ref_get_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8FfiResource* resource = ffi_resource_from_value(isolate, args.This());
    if (resource == nullptr || resource->disposed || resource->kind != FfiResourceKind::Ref ||
        resource->bytes.empty())
    {
        ffi_throw_type_error(isolate, "unsafeFfi.ref resource is disposed or invalid.");
        return;
    }
    args.GetReturnValue().Set(
        ffi_value_from_bytes(isolate, resource->type, resource->bytes.data()));
}

void ffi_ref_set_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8FfiResource* resource = ffi_resource_from_value(isolate, args.This());
    if (resource == nullptr || resource->disposed || resource->kind != FfiResourceKind::Ref ||
        resource->bytes.empty() || args.Length() < 1 ||
        !ffi_write_value_to_bytes(isolate, resource->type, args[0], resource->bytes.data(),
                                  resource->byte_length))
    {
        ffi_throw_type_error(isolate, "SLOPPY_E_FFI_INVALID_ARGUMENT_TYPE: invalid ref value.");
        return;
    }
    args.GetReturnValue().Set(args.This());
}

void ffi_buffer_read_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8FfiResource* resource = ffi_resource_from_value(isolate, args.This());
    if (resource == nullptr || resource->disposed ||
        (resource->kind != FfiResourceKind::Buffer && resource->kind != FfiResourceKind::CString &&
         resource->kind != FfiResourceKind::Utf16 && resource->kind != FfiResourceKind::Struct))
    {
        ffi_throw_type_error(isolate, "FFI buffer resource is disposed or invalid.");
        return;
    }
    v8::Local<v8::ArrayBuffer> buffer = v8::ArrayBuffer::New(isolate, resource->byte_length);
    std::shared_ptr<v8::BackingStore> backing = buffer->GetBackingStore();
    if (resource->byte_length > 0U && backing != nullptr && backing->Data() != nullptr) {
        auto* target = static_cast<unsigned char*>(backing->Data());
        std::copy_n(resource->bytes.data(), resource->byte_length, target);
    }
    args.GetReturnValue().Set(v8::Uint8Array::New(buffer, 0U, resource->byte_length));
}

void ffi_buffer_write_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    const unsigned char* data = nullptr;
    size_t length = 0U;
    uint64_t offset = 0U;
    SlV8FfiResource* resource = ffi_resource_from_value(isolate, args.This());
    if (args.Length() < 1) {
        ffi_throw_type_error(isolate, "FFI buffer write requires a Uint8Array.");
        return;
    }
    if (args.Length() > 1 &&
        !ffi_u64_from_value(isolate, args[1], std::numeric_limits<uint64_t>::max(), &offset))
    {
        ffi_throw_type_error(isolate, "FFI buffer offset must be a non-negative integer.");
        return;
    }
    if (resource == nullptr || resource->disposed ||
        (resource->kind != FfiResourceKind::Buffer && resource->kind != FfiResourceKind::CString &&
         resource->kind != FfiResourceKind::Utf16 && resource->kind != FfiResourceKind::Struct) ||
        !ffi_copy_uint8_array(args[0], &data, &length) || offset > resource->byte_length ||
        length > resource->byte_length - (size_t)offset)
    {
        ffi_throw_type_error(isolate, "SLOPPY_E_FFI_INVALID_ARGUMENT_TYPE: invalid buffer write.");
        return;
    }
    if (length > 0U) {
        std::copy_n(data, length, resource->bytes.data() + (size_t)offset);
    }
    args.GetReturnValue().Set(args.This());
}

void ffi_cstring_read_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8FfiResource* resource = ffi_resource_from_value(isolate, args.This());
    if (resource == nullptr || resource->disposed || resource->kind != FfiResourceKind::CString) {
        ffi_throw_type_error(isolate, "unsafeFfi.cstringBuffer resource is disposed or invalid.");
        return;
    }
    size_t length = 0U;
    while (length < resource->byte_length && resource->bytes[length] != 0U) {
        length += 1U;
    }
    v8::Local<v8::String> value;
    if (!v8::String::NewFromUtf8(isolate, reinterpret_cast<const char*>(resource->bytes.data()),
                                 v8::NewStringType::kNormal, static_cast<int>(length))
             .ToLocal(&value))
    {
        ffi_throw_error(isolate, "SLOPPY_E_FFI_CALL_FAILED: failed to decode C string buffer.");
        return;
    }
    args.GetReturnValue().Set(value);
}

void ffi_cstring_write_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8FfiResource* resource = ffi_resource_from_value(isolate, args.This());
    std::string value;
    if (resource == nullptr || resource->disposed || resource->kind != FfiResourceKind::CString ||
        args.Length() < 1 || !ffi_string_value(isolate, args[0], &value) || ffi_has_nul(value) ||
        value.size() + 1U > resource->byte_length)
    {
        ffi_throw_type_error(isolate, "SLOPPY_E_FFI_STRING_NUL: invalid C string buffer value.");
        return;
    }
    std::fill(resource->bytes.begin(), resource->bytes.end(), 0U);
    std::copy_n(reinterpret_cast<const unsigned char*>(value.data()), value.size(),
                resource->bytes.data());
    args.GetReturnValue().Set(args.This());
}

void ffi_utf16_read_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8FfiResource* resource = ffi_resource_from_value(isolate, args.This());
    if (resource == nullptr || resource->disposed || resource->kind != FfiResourceKind::Utf16 ||
        resource->byte_length % sizeof(uint16_t) != 0U)
    {
        ffi_throw_type_error(isolate, "unsafeFfi.utf16Buffer resource is disposed or invalid.");
        return;
    }
    const uint16_t* data = reinterpret_cast<const uint16_t*>(resource->bytes.data());
    size_t units = resource->byte_length / sizeof(uint16_t);
    size_t length = 0U;
    while (length < units && data[length] != 0U) {
        length += 1U;
    }
    v8::Local<v8::String> value;
    if (!v8::String::NewFromTwoByte(isolate, data, v8::NewStringType::kNormal,
                                    static_cast<int>(length))
             .ToLocal(&value))
    {
        ffi_throw_error(isolate, "SLOPPY_E_FFI_CALL_FAILED: failed to decode UTF-16 buffer.");
        return;
    }
    args.GetReturnValue().Set(value);
}

void ffi_utf16_write_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8FfiResource* resource = ffi_resource_from_value(isolate, args.This());
    if (resource == nullptr || resource->disposed || resource->kind != FfiResourceKind::Utf16 ||
        args.Length() < 1 || !args[0]->IsString())
    {
        ffi_throw_type_error(isolate, "unsafeFfi.utf16Buffer.writeString requires text.");
        return;
    }
    v8::String::Value value(isolate, args[0]);
    if (*value == nullptr ||
        (static_cast<size_t>(value.length()) + 1U) * sizeof(uint16_t) > resource->byte_length)
    {
        ffi_throw_type_error(isolate, "unsafeFfi.utf16Buffer value does not fit.");
        return;
    }
    for (uint32_t i = 0; i < value.length(); i += 1U) {
        if ((*value)[static_cast<int>(i)] == 0U) {
            ffi_throw_type_error(isolate,
                                 "SLOPPY_E_FFI_STRING_NUL: UTF-16 strings cannot contain NUL.");
            return;
        }
    }
    std::fill(resource->bytes.begin(), resource->bytes.end(), 0U);
    std::copy_n(reinterpret_cast<const unsigned char*>(*value),
                (size_t)value.length() * sizeof(uint16_t), resource->bytes.data());
    args.GetReturnValue().Set(args.This());
}

const FfiStructField* ffi_struct_field(const SlV8FfiResource* resource, const std::string& name)
{
    if (resource == nullptr) {
        return nullptr;
    }
    for (const FfiStructField& field : resource->fields) {
        if (field.name == name) {
            return &field;
        }
    }
    return nullptr;
}

void ffi_struct_get_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8FfiResource* resource = ffi_resource_from_value(isolate, args.This());
    std::string name;
    if (resource == nullptr || resource->disposed || resource->kind != FfiResourceKind::Struct ||
        args.Length() < 1 || !ffi_string_value(isolate, args[0], &name))
    {
        ffi_throw_type_error(isolate, "unsafeFfi.struct instance get requires a field name.");
        return;
    }
    const FfiStructField* field = ffi_struct_field(resource, name);
    if (field == nullptr || field->offset + sl_ffi_type_size(field->type) > resource->byte_length) {
        ffi_throw_type_error(isolate, "unsafeFfi.struct field is not present.");
        return;
    }
    args.GetReturnValue().Set(
        ffi_value_from_bytes(isolate, field->type, resource->bytes.data() + field->offset));
}

void ffi_struct_set_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8FfiResource* resource = ffi_resource_from_value(isolate, args.This());
    std::string name;
    if (resource == nullptr || resource->disposed || resource->kind != FfiResourceKind::Struct ||
        args.Length() < 2 || !ffi_string_value(isolate, args[0], &name))
    {
        ffi_throw_type_error(isolate,
                             "unsafeFfi.struct instance set requires a field name and value.");
        return;
    }
    const FfiStructField* field = ffi_struct_field(resource, name);
    if (field == nullptr || field->offset + sl_ffi_type_size(field->type) > resource->byte_length ||
        !ffi_write_value_to_bytes(isolate, field->type, args[1],
                                  resource->bytes.data() + field->offset,
                                  resource->byte_length - field->offset))
    {
        ffi_throw_type_error(isolate, "SLOPPY_E_FFI_INVALID_ARGUMENT_TYPE: invalid struct value.");
        return;
    }
    args.GetReturnValue().Set(args.This());
}

void ffi_struct_alloc_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = ffi_backend(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    SlV8FfiResource* layout = ffi_resource_from_value(isolate, args.This());
    if (backend == nullptr || layout == nullptr || layout->disposed ||
        layout->kind != FfiResourceKind::StructLayout)
    {
        ffi_throw_type_error(isolate, "unsafeFfi.struct layout is disposed or invalid.");
        return;
    }
    SlV8FfiResource* resource = ffi_new_resource(backend, FfiResourceKind::Struct);
    v8::Local<v8::Object> object;
    if (resource == nullptr) {
        ffi_throw_error(isolate, "SLOPPY_E_FFI_CALL_FAILED: failed to allocate struct instance.");
        return;
    }
    resource->fields = layout->fields;
    resource->byte_length = layout->byte_length;
    resource->bytes.assign(resource->byte_length == 0U ? 1U : resource->byte_length, 0U);
    if (!ffi_make_resource_object(isolate, context, resource, &object)) {
        ffi_throw_error(isolate, "SLOPPY_E_FFI_CALL_FAILED: failed to create struct instance.");
        return;
    }
    if (args.Length() > 0 && args[0]->IsObject()) {
        v8::Local<v8::Object> initial = args[0].As<v8::Object>();
        for (const FfiStructField& field : resource->fields) {
            v8::Local<v8::String> key;
            v8::Local<v8::Value> value;
            if (!v8::String::NewFromUtf8(isolate, field.name.c_str(), v8::NewStringType::kNormal,
                                         static_cast<int>(field.name.size()))
                     .ToLocal(&key) ||
                !initial->Get(context, key).ToLocal(&value) || value->IsUndefined())
            {
                continue;
            }
            if (!ffi_write_value_to_bytes(isolate, field.type, value,
                                          resource->bytes.data() + field.offset,
                                          resource->byte_length - field.offset))
            {
                ffi_throw_type_error(
                    isolate, "SLOPPY_E_FFI_INVALID_ARGUMENT_TYPE: invalid struct initializer.");
                return;
            }
        }
    }
    args.GetReturnValue().Set(object);
}

bool ffi_make_resource_object(v8::Isolate* isolate, v8::Local<v8::Context> context,
                              SlV8FfiResource* resource, v8::Local<v8::Object>* out)
{
    v8::Local<v8::Object> object = v8::Object::New(isolate);
    if (out == nullptr || resource == nullptr ||
        !ffi_set_resource(isolate, context, object, resource) ||
        !object
             ->Set(context, v8::String::NewFromUtf8Literal(isolate, "byteLength"),
                   v8::Integer::NewFromUnsigned(isolate,
                                                static_cast<uint32_t>(resource->byte_length)))
             .FromMaybe(false))
    {
        return false;
    }
    if (resource->kind != FfiResourceKind::NativePointer &&
        !ffi_set_function(isolate, context, object, "dispose", ffi_dispose_resource_callback))
    {
        return false;
    }
    if (resource->kind == FfiResourceKind::NativePointer &&
        !ffi_set_function(isolate, context, object, "isNull", ffi_native_pointer_is_null_callback))
    {
        return false;
    }
    if (resource->kind == FfiResourceKind::Ref &&
        (!ffi_set_function(isolate, context, object, "get", ffi_ref_get_callback) ||
         !ffi_set_function(isolate, context, object, "set", ffi_ref_set_callback)))
    {
        return false;
    }
    if ((resource->kind == FfiResourceKind::Buffer || resource->kind == FfiResourceKind::CString ||
         resource->kind == FfiResourceKind::Utf16 || resource->kind == FfiResourceKind::Struct) &&
        (!ffi_set_function(isolate, context, object, "read", ffi_buffer_read_callback) ||
         !ffi_set_function(isolate, context, object, "write", ffi_buffer_write_callback)))
    {
        return false;
    }
    if (resource->kind == FfiResourceKind::CString &&
        (!ffi_set_function(isolate, context, object, "readString", ffi_cstring_read_callback) ||
         !ffi_set_function(isolate, context, object, "writeString", ffi_cstring_write_callback)))
    {
        return false;
    }
    if (resource->kind == FfiResourceKind::Utf16 &&
        (!ffi_set_function(isolate, context, object, "readString", ffi_utf16_read_callback) ||
         !ffi_set_function(isolate, context, object, "writeString", ffi_utf16_write_callback)))
    {
        return false;
    }
    if (resource->kind == FfiResourceKind::Struct &&
        (!ffi_set_function(isolate, context, object, "get", ffi_struct_get_callback) ||
         !ffi_set_function(isolate, context, object, "set", ffi_struct_set_callback)))
    {
        return false;
    }
    if (resource->kind == FfiResourceKind::StructLayout &&
        !ffi_set_function(isolate, context, object, "alloc", ffi_struct_alloc_callback))
    {
        return false;
    }
    *out = object;
    return true;
}

void ffi_buffer_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = ffi_backend(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (backend == nullptr || args.Length() < 1 || !args[0]->IsUint32()) {
        ffi_throw_type_error(isolate, "unsafeFfi.buffer requires a byte length.");
        return;
    }
    uint32_t length = args[0].As<v8::Uint32>()->Value();
    SlV8FfiResource* resource = ffi_new_resource(backend, FfiResourceKind::Buffer);
    v8::Local<v8::Object> object;
    if (resource == nullptr) {
        ffi_throw_error(isolate, "SLOPPY_E_FFI_CALL_FAILED: failed to allocate FFI buffer.");
        return;
    }
    resource->byte_length = length;
    resource->bytes.resize(length == 0U ? 1U : length);
    if (!ffi_make_resource_object(isolate, context, resource, &object)) {
        ffi_throw_error(isolate, "SLOPPY_E_FFI_CALL_FAILED: failed to create FFI buffer.");
        return;
    }
    args.GetReturnValue().Set(object);
}

void ffi_ref_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = ffi_backend(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    SlPlanFfiType type = SL_PLAN_FFI_TYPE_UNKNOWN;
    if (backend == nullptr || args.Length() < 1 || !ffi_type_from_value(isolate, args[0], &type)) {
        ffi_throw_type_error(isolate, "unsafeFfi.ref requires a type name.");
        return;
    }
    size_t size = sl_ffi_type_size(type);
    if (size == 0U) {
        ffi_throw_type_error(isolate, "unsafeFfi.ref requires a sized primitive type.");
        return;
    }
    SlV8FfiResource* resource = ffi_new_resource(backend, FfiResourceKind::Ref);
    v8::Local<v8::Object> object;
    if (resource == nullptr) {
        ffi_throw_error(isolate, "SLOPPY_E_FFI_CALL_FAILED: failed to allocate FFI ref.");
        return;
    }
    resource->type = type;
    resource->byte_length = size;
    resource->bytes.resize(size);
    if (!ffi_make_resource_object(isolate, context, resource, &object)) {
        ffi_throw_error(isolate, "SLOPPY_E_FFI_CALL_FAILED: failed to create FFI ref.");
        return;
    }
    if (args.Length() > 1 &&
        !ffi_write_value_to_bytes(isolate, resource->type, args[1], resource->bytes.data(),
                                  resource->byte_length))
    {
        ffi_throw_type_error(isolate, "SLOPPY_E_FFI_INVALID_ARGUMENT_TYPE: invalid ref value.");
        return;
    }
    args.GetReturnValue().Set(object);
}

void ffi_cstring_buffer_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = ffi_backend(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::string value;
    size_t length = 0U;
    if (backend == nullptr || args.Length() < 1) {
        ffi_throw_type_error(isolate, "unsafeFfi.cstringBuffer requires text or byte length.");
        return;
    }
    if (args[0]->IsString()) {
        if (!ffi_string_value(isolate, args[0], &value) || ffi_has_nul(value)) {
            ffi_throw_type_error(isolate, "SLOPPY_E_FFI_STRING_NUL: C strings cannot contain NUL.");
            return;
        }
        length = value.size() + 1U;
    }
    else if (args[0]->IsUint32()) {
        length = args[0].As<v8::Uint32>()->Value();
    }
    else {
        ffi_throw_type_error(isolate, "unsafeFfi.cstringBuffer requires text or byte length.");
        return;
    }
    SlV8FfiResource* resource = ffi_new_resource(backend, FfiResourceKind::CString);
    v8::Local<v8::Object> object;
    if (resource == nullptr) {
        ffi_throw_error(isolate, "SLOPPY_E_FFI_CALL_FAILED: failed to allocate C string buffer.");
        return;
    }
    resource->byte_length = length == 0U ? 1U : length;
    resource->bytes.assign(resource->byte_length, 0U);
    if (!value.empty()) {
        std::copy_n(reinterpret_cast<const unsigned char*>(value.data()), value.size(),
                    resource->bytes.data());
    }
    if (!ffi_make_resource_object(isolate, context, resource, &object)) {
        ffi_throw_error(isolate, "SLOPPY_E_FFI_CALL_FAILED: failed to create C string buffer.");
        return;
    }
    args.GetReturnValue().Set(object);
}

void ffi_utf16_buffer_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = ffi_backend(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    size_t units = 0U;
    std::vector<uint16_t> text;
    if (backend == nullptr || args.Length() < 1) {
        ffi_throw_type_error(isolate, "unsafeFfi.utf16Buffer requires text or code unit length.");
        return;
    }
    if (args[0]->IsString()) {
        v8::String::Value value(isolate, args[0]);
        if (*value == nullptr) {
            ffi_throw_type_error(isolate, "unsafeFfi.utf16Buffer requires text.");
            return;
        }
        units = static_cast<size_t>(value.length()) + 1U;
        text.reserve(units);
        for (uint32_t i = 0; i < value.length(); i += 1U) {
            if ((*value)[static_cast<int>(i)] == 0U) {
                ffi_throw_type_error(isolate,
                                     "SLOPPY_E_FFI_STRING_NUL: UTF-16 strings cannot contain NUL.");
                return;
            }
            text.push_back(static_cast<uint16_t>((*value)[static_cast<int>(i)]));
        }
    }
    else if (args[0]->IsUint32()) {
        units = args[0].As<v8::Uint32>()->Value();
    }
    else {
        ffi_throw_type_error(isolate, "unsafeFfi.utf16Buffer requires text or code unit length.");
        return;
    }
    SlV8FfiResource* resource = ffi_new_resource(backend, FfiResourceKind::Utf16);
    v8::Local<v8::Object> object;
    if (resource == nullptr) {
        ffi_throw_error(isolate, "SLOPPY_E_FFI_CALL_FAILED: failed to allocate UTF-16 buffer.");
        return;
    }
    resource->byte_length = (units == 0U ? 1U : units) * sizeof(uint16_t);
    resource->bytes.assign(resource->byte_length, 0U);
    if (!text.empty()) {
        std::copy_n(reinterpret_cast<const unsigned char*>(text.data()),
                    text.size() * sizeof(uint16_t), resource->bytes.data());
    }
    if (!ffi_make_resource_object(isolate, context, resource, &object)) {
        ffi_throw_error(isolate, "SLOPPY_E_FFI_CALL_FAILED: failed to create UTF-16 buffer.");
        return;
    }
    args.GetReturnValue().Set(object);
}

bool ffi_field_type(v8::Isolate* isolate, v8::Local<v8::Value> value, SlPlanFfiType* out)
{
    if (!ffi_type_from_value(isolate, value, out) || sl_ffi_type_size(*out) == 0U) {
        return false;
    }
    return *out != SL_PLAN_FFI_TYPE_VOID && *out != SL_PLAN_FFI_TYPE_CSTRING &&
           *out != SL_PLAN_FFI_TYPE_UTF16 && *out != SL_PLAN_FFI_TYPE_BYTES &&
           *out != SL_PLAN_FFI_TYPE_MUT_BYTES;
}

void ffi_struct_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = ffi_backend(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (backend == nullptr || args.Length() < 2 || !args[1]->IsObject()) {
        ffi_throw_type_error(isolate, "unsafeFfi.struct requires a field descriptor object.");
        return;
    }
    v8::Local<v8::Object> fields = args[1].As<v8::Object>();
    v8::Local<v8::Array> names;
    if (!fields->GetOwnPropertyNames(context).ToLocal(&names)) {
        ffi_throw_error(isolate, "SLOPPY_E_FFI_CALL_FAILED: failed to inspect struct fields.");
        return;
    }
    size_t pack = 0U;
    if (args.Length() > 2 && args[2]->IsObject()) {
        v8::Local<v8::Object> options = args[2].As<v8::Object>();
        v8::Local<v8::Value> value;
        std::string layout;
        if (ffi_object_value(isolate, context, options, "layout", &value) &&
            !value->IsUndefined() &&
            (!ffi_string_value(isolate, value, &layout) || layout != "sequential"))
        {
            ffi_throw_type_error(isolate, "unsafeFfi.struct only supports sequential layout.");
            return;
        }
        if (ffi_object_value(isolate, context, options, "pack", &value) && !value->IsUndefined()) {
            uint64_t parsed = 0U;
            if (!ffi_u64_from_value(isolate, value, 16U, &parsed) ||
                !(parsed == 1U || parsed == 2U || parsed == 4U || parsed == 8U || parsed == 16U))
            {
                ffi_throw_type_error(isolate, "unsafeFfi.struct pack must be 1, 2, 4, 8, or 16.");
                return;
            }
            pack = static_cast<size_t>(parsed);
        }
    }
    SlV8FfiResource* layout = ffi_new_resource(backend, FfiResourceKind::StructLayout);
    if (layout == nullptr) {
        ffi_throw_error(isolate, "SLOPPY_E_FFI_CALL_FAILED: failed to allocate struct layout.");
        return;
    }
    size_t offset = 0U;
    for (uint32_t index = 0U; index < names->Length(); index += 1U) {
        v8::Local<v8::Value> key;
        v8::Local<v8::Value> type_value;
        std::string name;
        SlPlanFfiType type = SL_PLAN_FFI_TYPE_UNKNOWN;
        if (!names->Get(context, index).ToLocal(&key) || !ffi_string_value(isolate, key, &name) ||
            !fields->Get(context, key).ToLocal(&type_value) ||
            !ffi_field_type(isolate, type_value, &type))
        {
            ffi_throw_type_error(isolate, "unsafeFfi.struct fields must use sized FFI types.");
            return;
        }
        size_t alignment = sl_ffi_type_alignment(type);
        size_t size = sl_ffi_type_size(type);
        if (pack > 0U && alignment > pack) {
            alignment = pack;
        }
        offset = (offset + alignment - 1U) & ~(alignment - 1U);
        layout->fields.push_back({name, type, offset});
        offset += size;
    }
    layout->byte_length = offset;
    layout->bytes.resize(offset == 0U ? 1U : offset);
    v8::Local<v8::Object> object;
    if (!ffi_make_resource_object(isolate, context, layout, &object)) {
        ffi_throw_error(isolate, "SLOPPY_E_FFI_CALL_FAILED: failed to create struct layout.");
        return;
    }
    args.GetReturnValue().Set(object);
}

} // namespace

bool sl_v8_install_ffi_intrinsics(SlV8Engine* backend, v8::Local<v8::Context> context,
                                  v8::Local<v8::Object> sloppy)
{
    if (backend == nullptr || backend->isolate == nullptr) {
        return false;
    }
    if (!sl_v8_runtime_feature_active(backend, SL_RUNTIME_FEATURE_STDLIB_FFI)) {
        return true;
    }
    v8::Isolate* isolate = backend->isolate;
    v8::Local<v8::Object> ffi = v8::Object::New(isolate);
    v8::Local<v8::String> key;
    if (!ffi_set_function(isolate, context, ffi, "library", ffi_library_callback) ||
        !ffi_set_function(isolate, context, ffi, "ref", ffi_ref_callback) ||
        !ffi_set_function(isolate, context, ffi, "buffer", ffi_buffer_callback) ||
        !ffi_set_function(isolate, context, ffi, "cstringBuffer", ffi_cstring_buffer_callback) ||
        !ffi_set_function(isolate, context, ffi, "utf16Buffer", ffi_utf16_buffer_callback) ||
        !ffi_set_function(isolate, context, ffi, "struct", ffi_struct_callback) ||
        !sl_status_is_ok(sl_v8_string_from_native_view(backend, sl_str_from_cstr("ffi"), &key)))
    {
        return false;
    }
    return sloppy->Set(context, key, ffi).FromMaybe(false);
}

void sl_v8_ffi_dispose(SlV8Engine* backend)
{
    if (backend == nullptr) {
        return;
    }
    for (void* ptr : backend->ffi_resources) {
        delete static_cast<SlV8FfiResource*>(ptr);
    }
    backend->ffi_resources.clear();
    if (backend->ffi_registry_initialized) {
        sl_ffi_registry_dispose(&backend->ffi_registry);
        backend->ffi_registry_initialized = false;
    }
}

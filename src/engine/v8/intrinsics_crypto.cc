/*
 * src/engine/v8/intrinsics_crypto.cc
 *
 * Installs the V8-internal crypto bridge under __sloppy.crypto. This bridge exposes
 * bounded synchronous primitives only; public stdlib functions keep async return shapes
 * where the contract requires them. Password hashing is intentionally absent until the
 * dedicated offload PR.
 */
#include "engine_v8_internal.h"
#include "string_interop.h"

#include "sloppy/crypto.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr size_t kCryptoMaxInlineBytes = 1024U * 1024U;
constexpr size_t kCryptoMaxRandomBytes = 4096U;
constexpr size_t kCryptoMaxTextRandom = 1024U;

SlStatus crypto_v8_to_local_string(v8::Isolate* isolate, SlStr str, v8::Local<v8::String>* out)
{
    SlV8Engine* backend =
        isolate == nullptr ? nullptr : static_cast<SlV8Engine*>(isolate->GetData(0));
    return sl_v8_string_from_native_view(backend, str, out);
}

void crypto_v8_throw_type_error(v8::Isolate* isolate, const char* message)
{
    v8::Local<v8::String> local_message;
    if (!sl_status_is_ok(
            crypto_v8_to_local_string(isolate, sl_str_from_cstr(message), &local_message)))
    {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "Sloppy crypto type error")));
        return;
    }
    isolate->ThrowException(v8::Exception::TypeError(local_message));
}

void crypto_v8_throw_error(v8::Isolate* isolate, const char* message)
{
    v8::Local<v8::String> local_message;
    if (!sl_status_is_ok(
            crypto_v8_to_local_string(isolate, sl_str_from_cstr(message), &local_message)))
    {
        isolate->ThrowException(v8::Exception::Error(
            v8::String::NewFromUtf8Literal(isolate, "Sloppy crypto operation failed")));
        return;
    }
    isolate->ThrowException(v8::Exception::Error(local_message));
}

bool crypto_v8_set_function(v8::Isolate* isolate, v8::Local<v8::Context> context,
                            v8::Local<v8::Object> object, const char* name,
                            v8::FunctionCallback callback)
{
    v8::Local<v8::String> key;
    v8::Local<v8::Function> function;

    if (!sl_status_is_ok(crypto_v8_to_local_string(isolate, sl_str_from_cstr(name), &key)) ||
        !v8::FunctionTemplate::New(isolate, callback)->GetFunction(context).ToLocal(&function))
    {
        return false;
    }
    return object->Set(context, key, function).FromMaybe(false);
}

bool crypto_v8_size_arg(v8::Local<v8::Value> value, size_t max, size_t* out)
{
    if (out == nullptr || value.IsEmpty() || !value->IsUint32()) {
        return false;
    }
    uint32_t number = value.As<v8::Uint32>()->Value();
    if (number > max) {
        return false;
    }
    *out = static_cast<size_t>(number);
    return true;
}

bool crypto_v8_algorithm_arg(v8::Isolate* isolate, v8::Local<v8::Value> value,
                             SlCryptoHashAlgorithm* out)
{
    std::string algorithm;

    if (out == nullptr || !sl_v8_std_string_from_value(isolate, value, &algorithm)) {
        return false;
    }
    return sl_status_is_ok(sl_crypto_hash_algorithm_from_str(
        sl_str_from_parts(algorithm.data(), algorithm.size()), out));
}

bool crypto_v8_bytes_arg(v8::Local<v8::Value> value, size_t max, std::vector<unsigned char>* out)
{
    if (out == nullptr || value.IsEmpty() || !value->IsUint8Array()) {
        return false;
    }

    v8::Local<v8::Uint8Array> view = value.As<v8::Uint8Array>();
    size_t length = view->ByteLength();
    size_t offset = view->ByteOffset();
    if (length > max) {
        return false;
    }

    std::shared_ptr<v8::BackingStore> backing = view->Buffer()->GetBackingStore();
    if (backing == nullptr || offset > backing->ByteLength() ||
        length > backing->ByteLength() - offset)
    {
        return false;
    }

    const unsigned char* start =
        static_cast<const unsigned char*>(backing->Data()) + static_cast<ptrdiff_t>(offset);
    out->assign(start, start + length);
    return true;
}

bool crypto_v8_uint8_array(v8::Isolate* isolate, const unsigned char* data, size_t length,
                           v8::Local<v8::Uint8Array>* out)
{
    if (isolate == nullptr || out == nullptr ||
        length > static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        return false;
    }

    std::unique_ptr<v8::BackingStore> backing = v8::ArrayBuffer::NewBackingStore(isolate, length);
    if (backing == nullptr) {
        return false;
    }
    if (length != 0U && data != nullptr) {
        std::copy(data, data + length, static_cast<unsigned char*>(backing->Data()));
    }

    v8::Local<v8::ArrayBuffer> buffer = v8::ArrayBuffer::New(isolate, std::move(backing));
    *out = v8::Uint8Array::New(buffer, 0U, length);
    return true;
}

void crypto_v8_random_bytes_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    size_t length = 0U;
    std::vector<unsigned char> bytes;
    v8::Local<v8::Uint8Array> result;

    if (args.Length() != 1 || !crypto_v8_size_arg(args[0], kCryptoMaxRandomBytes, &length)) {
        crypto_v8_throw_type_error(
            isolate, "SLOPPY_E_CRYPTO_INVALID_KEY_SECRET: Random.bytes length must be 0..4096");
        return;
    }

    bytes.resize(length);
    SlOwnedBytes output = {bytes.data(), bytes.size()};
    if (!sl_status_is_ok(sl_crypto_random_bytes(output)) ||
        !crypto_v8_uint8_array(isolate, bytes.data(), bytes.size(), &result))
    {
        crypto_v8_throw_error(
            isolate, "SLOPPY_E_CRYPTO_RANDOM_SOURCE_UNAVAILABLE: OS random source unavailable");
        return;
    }
    args.GetReturnValue().Set(result);
}

void crypto_v8_random_uuid_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    char uuid[SL_CRYPTO_UUID_V4_TEXT_LENGTH] = {0};
    v8::Local<v8::String> result;

    if (!sl_status_is_ok(sl_crypto_random_uuid_v4(uuid, sizeof(uuid))) ||
        !sl_status_is_ok(
            crypto_v8_to_local_string(isolate, sl_str_from_parts(uuid, sizeof(uuid)), &result)))
    {
        crypto_v8_throw_error(
            isolate, "SLOPPY_E_CRYPTO_RANDOM_SOURCE_UNAVAILABLE: OS random source unavailable");
        return;
    }
    args.GetReturnValue().Set(result);
}

template <typename Fn>
void crypto_v8_random_text_callback(const v8::FunctionCallbackInfo<v8::Value>& args,
                                    const char* message, Fn generate)
{
    v8::Isolate* isolate = args.GetIsolate();
    size_t length = 0U;
    std::vector<char> text;
    v8::Local<v8::String> result;

    if (args.Length() != 1 || !crypto_v8_size_arg(args[0], kCryptoMaxTextRandom, &length)) {
        crypto_v8_throw_type_error(isolate, message);
        return;
    }

    text.resize(length == 0U ? 1U : length);
    if (!sl_status_is_ok(generate(length, text.data(), text.size())) ||
        !sl_status_is_ok(crypto_v8_to_local_string(
            isolate, sl_str_from_parts(text.data(), text.size()), &result)))
    {
        crypto_v8_throw_error(
            isolate, "SLOPPY_E_CRYPTO_RANDOM_SOURCE_UNAVAILABLE: OS random source unavailable");
        return;
    }
    args.GetReturnValue().Set(result);
}

void crypto_v8_random_hex_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    crypto_v8_random_text_callback(
        args, "SLOPPY_E_CRYPTO_INVALID_KEY_SECRET: Random.hex length must be 0..1024",
        [](size_t length, char* out, size_t out_length) {
            return sl_crypto_random_hex(length, out, out_length);
        });
}

void crypto_v8_random_token_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    crypto_v8_random_text_callback(
        args, "SLOPPY_E_CRYPTO_INVALID_KEY_SECRET: Random.token length must be 0..1024",
        [](size_t length, char* out, size_t out_length) {
            return sl_crypto_random_token(length, out, out_length);
        });
}

void crypto_v8_random_numeric_code_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    crypto_v8_random_text_callback(
        args, "SLOPPY_E_CRYPTO_INVALID_KEY_SECRET: Random.numericCode length must be 0..1024",
        [](size_t length, char* out, size_t out_length) {
            return sl_crypto_random_numeric_code(length, out, out_length);
        });
}

void crypto_v8_hash_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlCryptoHashAlgorithm algorithm = SL_CRYPTO_HASH_SHA256;
    std::vector<unsigned char> data;
    unsigned char digest[SL_CRYPTO_SHA512_SIZE] = {0};
    size_t digest_size = 0U;
    v8::Local<v8::Uint8Array> result;

    if (args.Length() != 2 || !crypto_v8_algorithm_arg(isolate, args[0], &algorithm) ||
        !crypto_v8_bytes_arg(args[1], kCryptoMaxInlineBytes, &data))
    {
        crypto_v8_throw_type_error(
            isolate,
            "SLOPPY_E_CRYPTO_UNSUPPORTED_ALGORITHM: Hash requires sha256, sha384, or sha512 "
            "and bounded Uint8Array data");
        return;
    }

    digest_size = sl_crypto_hash_digest_size(algorithm);
    SlOwnedBytes output = {digest, digest_size};
    if (!sl_status_is_ok(
            sl_crypto_hash(algorithm, sl_bytes_from_parts(data.data(), data.size()), output)) ||
        !crypto_v8_uint8_array(isolate, digest, digest_size, &result))
    {
        crypto_v8_throw_error(isolate,
                              "SLOPPY_E_CRYPTO_BACKEND_UNAVAILABLE: hash backend unavailable");
        return;
    }
    args.GetReturnValue().Set(result);
}

void crypto_v8_hmac_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlCryptoHashAlgorithm algorithm = SL_CRYPTO_HASH_SHA256;
    std::vector<unsigned char> key;
    std::vector<unsigned char> data;
    unsigned char digest[SL_CRYPTO_SHA512_SIZE] = {0};
    size_t digest_size = 0U;
    v8::Local<v8::Uint8Array> result;

    if (args.Length() != 3 || !crypto_v8_algorithm_arg(isolate, args[0], &algorithm) ||
        !crypto_v8_bytes_arg(args[1], kCryptoMaxInlineBytes, &key) ||
        !crypto_v8_bytes_arg(args[2], kCryptoMaxInlineBytes, &data) || key.empty())
    {
        crypto_v8_throw_type_error(
            isolate, "SLOPPY_E_CRYPTO_INVALID_KEY_SECRET: HMAC requires an algorithm, non-empty "
                     "Uint8Array secret, and bounded Uint8Array data");
        return;
    }

    digest_size = sl_crypto_hash_digest_size(algorithm);
    SlOwnedBytes output = {digest, digest_size};
    if (!sl_status_is_ok(sl_crypto_hmac(algorithm, sl_bytes_from_parts(key.data(), key.size()),
                                        sl_bytes_from_parts(data.data(), data.size()), output)) ||
        !crypto_v8_uint8_array(isolate, digest, digest_size, &result))
    {
        crypto_v8_throw_error(isolate,
                              "SLOPPY_E_CRYPTO_BACKEND_UNAVAILABLE: HMAC backend unavailable");
        return;
    }
    args.GetReturnValue().Set(result);
}

void crypto_v8_constant_time_equals_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    std::vector<unsigned char> left;
    std::vector<unsigned char> right;
    bool equal = false;

    if (args.Length() != 2 || !crypto_v8_bytes_arg(args[0], kCryptoMaxInlineBytes, &left) ||
        !crypto_v8_bytes_arg(args[1], kCryptoMaxInlineBytes, &right))
    {
        crypto_v8_throw_type_error(
            isolate, "SLOPPY_E_CRYPTO_CONSTANT_TIME_INVALID_INPUT: ConstantTime.equals requires "
                     "bounded Uint8Array inputs");
        return;
    }

    if (!sl_status_is_ok(sl_crypto_constant_time_equals(
            sl_bytes_from_parts(left.data(), left.size()),
            sl_bytes_from_parts(right.data(), right.size()), &equal)))
    {
        crypto_v8_throw_error(
            isolate,
            "SLOPPY_E_CRYPTO_CONSTANT_TIME_INVALID_INPUT: ConstantTime.equals rejected inputs");
        return;
    }
    args.GetReturnValue().Set(v8::Boolean::New(isolate, equal));
}

} // namespace

bool sl_v8_install_crypto_intrinsics(SlV8Engine* backend, v8::Local<v8::Context> context,
                                     v8::Local<v8::Object> sloppy)
{
    v8::Isolate* isolate = backend == nullptr ? nullptr : backend->isolate;
    v8::Local<v8::String> crypto_key;

    if (backend == nullptr || isolate == nullptr) {
        return false;
    }

    if (!backend->has_runtime_features ||
        !sl_v8_runtime_feature_active(backend, SL_RUNTIME_FEATURE_STDLIB_CRYPTO))
    {
        return true;
    }

    v8::Local<v8::Object> crypto = v8::Object::New(isolate);
    if (!sl_status_is_ok(
            crypto_v8_to_local_string(isolate, sl_str_from_cstr("crypto"), &crypto_key)) ||
        !crypto_v8_set_function(isolate, context, crypto, "randomBytes",
                                crypto_v8_random_bytes_callback) ||
        !crypto_v8_set_function(isolate, context, crypto, "randomUuid",
                                crypto_v8_random_uuid_callback) ||
        !crypto_v8_set_function(isolate, context, crypto, "randomHex",
                                crypto_v8_random_hex_callback) ||
        !crypto_v8_set_function(isolate, context, crypto, "randomToken",
                                crypto_v8_random_token_callback) ||
        !crypto_v8_set_function(isolate, context, crypto, "randomNumericCode",
                                crypto_v8_random_numeric_code_callback) ||
        !crypto_v8_set_function(isolate, context, crypto, "hash", crypto_v8_hash_callback) ||
        !crypto_v8_set_function(isolate, context, crypto, "hmac", crypto_v8_hmac_callback) ||
        !crypto_v8_set_function(isolate, context, crypto, "constantTimeEquals",
                                crypto_v8_constant_time_equals_callback))
    {
        return false;
    }
    return sloppy->Set(context, crypto_key, crypto).FromMaybe(false);
}

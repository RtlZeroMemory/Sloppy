/*
 * src/engine/v8/intrinsics_crypto.cc
 *
 * Installs the V8-internal crypto bridge under __sloppy.crypto. This bridge exposes
 * bounded synchronous primitives and password offload entrypoints. Public stdlib
 * functions keep async return shapes where the contract requires them.
 */
#include "engine_v8_internal.h"
#include "string_interop.h"

#include "sloppy/crypto.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <vector>

enum class SlV8CryptoPasswordOperation
{
    Hash,
    Verify,
    NeedsRehash
};

struct SlV8CryptoPasswordRequest
{
    SlV8Engine* backend = nullptr;
    v8::Global<v8::Promise::Resolver> resolver;
    std::atomic_bool cancelled = false;
    std::atomic_bool completion_posted = false;
    std::thread worker;
    SlV8CryptoPasswordOperation operation = SlV8CryptoPasswordOperation::Hash;
    std::vector<unsigned char> password;
    std::string encoded_hash;
    SlCryptoPasswordOptions options = {};
    SlStatus status = sl_status_ok();
    std::string text_result;
    bool bool_result = false;
};

namespace {

constexpr size_t kCryptoMaxInlineBytes = 1024U * 1024U;
constexpr size_t kCryptoMaxRandomBytes = 4096U;
constexpr size_t kCryptoMaxTextRandom = 1024U;
constexpr size_t kCryptoMaxPasswordBytes = 4096U;

struct CryptoV8PasswordCompletionPayload
{
    std::shared_ptr<SlV8CryptoPasswordRequest> request;
};

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

bool crypto_v8_u64_arg(v8::Local<v8::Value> value, uint64_t min, uint64_t max, uint64_t* out)
{
    if (out == nullptr || value.IsEmpty() || !value->IsUint32()) {
        return false;
    }
    uint64_t number = static_cast<uint64_t>(value.As<v8::Uint32>()->Value());
    if (number < min || number > max) {
        return false;
    }
    *out = number;
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

bool crypto_v8_password_hash_arg(v8::Isolate* isolate, v8::Local<v8::Value> value, std::string* out)
{
    if (out == nullptr || value.IsEmpty() || !value->IsString()) {
        return false;
    }
    if (value.As<v8::String>()->Length() >= static_cast<int>(SL_CRYPTO_PASSWORD_HASH_ENCODED_MAX)) {
        return false;
    }
    if (!sl_v8_std_string_from_value(isolate, value, out)) {
        return false;
    }
    return out->size() < SL_CRYPTO_PASSWORD_HASH_ENCODED_MAX;
}

void crypto_v8_zero_vector(std::vector<unsigned char>& bytes)
{
    volatile unsigned char* data = bytes.empty() ? nullptr : bytes.data();
    size_t index = 0U;

    for (index = 0U; data != nullptr && index < bytes.size(); index += 1U) {
        data[index] = 0U;
    }
    bytes.clear();
    bytes.shrink_to_fit();
}

const char* crypto_v8_password_error_message(const SlV8CryptoPasswordRequest& request)
{
    if (sl_status_code(request.status) == SL_STATUS_UNSUPPORTED &&
        request.operation != SlV8CryptoPasswordOperation::Hash)
    {
        return "SLOPPY_E_CRYPTO_PASSWORD_HASH_UNSUPPORTED: password hash format is unsupported";
    }
    if (sl_status_code(request.status) == SL_STATUS_INVALID_ARGUMENT) {
        return "SLOPPY_E_CRYPTO_INVALID_KEY_SECRET: invalid password hashing inputs";
    }
    return "SLOPPY_E_CRYPTO_BACKEND_UNAVAILABLE: password hashing backend is unavailable";
}

void crypto_v8_remove_password_request(const std::shared_ptr<SlV8CryptoPasswordRequest>& request)
{
    SlV8Engine* backend = request == nullptr ? nullptr : request->backend;
    if (backend == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(backend->crypto_mutex);
    auto& requests = backend->crypto_password_requests;
    requests.erase(std::remove(requests.begin(), requests.end(), request), requests.end());
}

SlStatus crypto_v8_password_completion_dispatch(SlAsyncLoop* loop,
                                                const SlAsyncCompletion* completion, void* user)
{
    CryptoV8PasswordCompletionPayload* payload =
        completion == nullptr
            ? nullptr
            : static_cast<CryptoV8PasswordCompletionPayload*>(completion->payload);
    std::shared_ptr<SlV8CryptoPasswordRequest> request =
        payload == nullptr ? nullptr : payload->request;
    SlV8Engine* backend = request == nullptr ? nullptr : request->backend;

    (void)loop;
    (void)user;
    if (request == nullptr || backend == nullptr || backend->isolate == nullptr ||
        request->resolver.IsEmpty())
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (backend->owner_thread != std::this_thread::get_id()) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    v8::Isolate* isolate = backend->isolate;
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = backend->context.Get(isolate);
    v8::Context::Scope context_scope(context);
    v8::Local<v8::Promise::Resolver> resolver = request->resolver.Get(isolate);
    bool ok = false;

    if (request->cancelled.load() || !sl_status_is_ok(request->status)) {
        v8::Local<v8::String> message;
        if (!sl_status_is_ok(crypto_v8_to_local_string(
                isolate, sl_str_from_cstr(crypto_v8_password_error_message(*request)), &message)))
        {
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
        ok = resolver->Reject(context, v8::Exception::Error(message)).FromMaybe(false);
    }
    else if (request->operation == SlV8CryptoPasswordOperation::Hash) {
        v8::Local<v8::String> encoded;
        if (!sl_status_is_ok(crypto_v8_to_local_string(
                isolate,
                sl_str_from_parts(request->text_result.data(), request->text_result.size()),
                &encoded)))
        {
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
        ok = resolver->Resolve(context, encoded).FromMaybe(false);
    }
    else {
        ok = resolver->Resolve(context, v8::Boolean::New(isolate, request->bool_result))
                 .FromMaybe(false);
    }

    isolate->PerformMicrotaskCheckpoint();
    return ok ? sl_status_ok() : sl_status_from_code(SL_STATUS_INVALID_STATE);
}

void crypto_v8_password_completion_cleanup(const SlAsyncCompletion* completion, void* user)
{
    CryptoV8PasswordCompletionPayload* payload =
        completion == nullptr
            ? nullptr
            : static_cast<CryptoV8PasswordCompletionPayload*>(completion->payload);
    std::shared_ptr<SlV8CryptoPasswordRequest> request =
        payload == nullptr ? nullptr : payload->request;

    (void)user;
    if (request != nullptr) {
        request->cancelled.store(true);
        if (request->worker.joinable() && request->worker.get_id() != std::this_thread::get_id()) {
            request->worker.join();
        }
        request->resolver.Reset();
        request->completion_posted.store(false);
        crypto_v8_zero_vector(request->password);
        crypto_v8_remove_password_request(request);
    }
    delete payload;
}

bool crypto_v8_post_password_completion(const std::shared_ptr<SlV8CryptoPasswordRequest>& request)
{
    SlV8Engine* backend = request == nullptr ? nullptr : request->backend;
    if (backend == nullptr) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(backend->crypto_mutex);
        if (request->cancelled.load() || backend->crypto_shutting_down ||
            backend->async_loop == nullptr)
        {
            request->cancelled.store(true);
            return false;
        }
    }

    CryptoV8PasswordCompletionPayload* payload =
        new (std::nothrow) CryptoV8PasswordCompletionPayload();
    if (payload == nullptr) {
        request->cancelled.store(true);
        return false;
    }
    payload->request = request;

    SlAsyncCompletion completion = {};
    completion.kind = SL_ASYNC_COMPLETION_V8_CONTINUATION;
    completion.operation_kind = SL_ASYNC_OPERATION_BLOCKING_OFFLOAD;
    completion.status = request->status;
    completion.payload = payload;
    completion.dispatch = crypto_v8_password_completion_dispatch;
    completion.cleanup = crypto_v8_password_completion_cleanup;

    request->completion_posted.store(true);
    SlStatus post_status = sl_async_loop_post(backend->async_loop, &completion);
    if (sl_status_is_ok(post_status)) {
        return true;
    }
    request->completion_posted.store(false);
    request->cancelled.store(true);
    delete payload;
    return false;
}

bool crypto_v8_reject_password_promise(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                       v8::Local<v8::Promise::Resolver> resolver,
                                       const char* message, bool type_error)
{
    v8::Local<v8::String> local_message;
    v8::Local<v8::Value> exception;

    if (!sl_status_is_ok(
            crypto_v8_to_local_string(isolate, sl_str_from_cstr(message), &local_message)))
    {
        return false;
    }
    exception =
        type_error ? v8::Exception::TypeError(local_message) : v8::Exception::Error(local_message);
    bool ok = resolver->Reject(context, exception).FromMaybe(false);
    isolate->PerformMicrotaskCheckpoint();
    return ok;
}

void crypto_v8_password_worker(std::shared_ptr<SlV8CryptoPasswordRequest> request)
{
    char encoded[SL_CRYPTO_PASSWORD_HASH_ENCODED_MAX] = {0};
    size_t written = 0U;

    if (request == nullptr) {
        return;
    }

    if (request->operation == SlV8CryptoPasswordOperation::Hash) {
        request->status = sl_crypto_password_hash(
            sl_bytes_from_parts(request->password.data(), request->password.size()),
            &request->options, encoded, sizeof(encoded), &written);
        if (sl_status_is_ok(request->status)) {
            request->text_result.assign(encoded, written);
        }
    }
    else if (request->operation == SlV8CryptoPasswordOperation::Verify) {
        request->status = sl_crypto_password_verify(
            sl_bytes_from_parts(request->password.data(), request->password.size()),
            sl_str_from_parts(request->encoded_hash.data(), request->encoded_hash.size()),
            &request->bool_result);
    }
    else {
        request->status = sl_crypto_password_needs_rehash(
            sl_str_from_parts(request->encoded_hash.data(), request->encoded_hash.size()),
            &request->options, &request->bool_result);
    }

    crypto_v8_zero_vector(request->password);
    (void)crypto_v8_post_password_completion(request);
}

bool crypto_v8_password_options_args(const v8::FunctionCallbackInfo<v8::Value>& args, int ops_index,
                                     int mem_index, SlCryptoPasswordOptions* out_options)
{
    SlCryptoPasswordOptions options = sl_crypto_password_default_options();

    if (out_options == nullptr || args.Length() <= mem_index ||
        !crypto_v8_u64_arg(args[ops_index], SL_CRYPTO_PASSWORD_OPSLIMIT_MIN,
                           SL_CRYPTO_PASSWORD_OPSLIMIT_MAX, &options.ops_limit) ||
        !crypto_v8_u64_arg(args[mem_index], SL_CRYPTO_PASSWORD_MEMLIMIT_MIN,
                           SL_CRYPTO_PASSWORD_MEMLIMIT_MAX, &options.mem_limit))
    {
        return false;
    }
    *out_options = options;
    return true;
}

void crypto_v8_start_password_request(const v8::FunctionCallbackInfo<v8::Value>& args,
                                      SlV8CryptoPasswordOperation operation)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Promise::Resolver> resolver;
    std::vector<unsigned char> password;
    std::string encoded_hash;
    SlCryptoPasswordOptions options = sl_crypto_password_default_options();

    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
        crypto_v8_throw_error(isolate,
                              "SLOPPY_E_CRYPTO_BACKEND_UNAVAILABLE: could not create Promise");
        return;
    }
    args.GetReturnValue().Set(resolver->GetPromise());

    if (backend == nullptr || backend->async_loop == nullptr) {
        (void)crypto_v8_reject_password_promise(
            isolate, context, resolver,
            "SLOPPY_E_CRYPTO_BACKEND_UNAVAILABLE: password async lane is unavailable", false);
        return;
    }

    if (operation == SlV8CryptoPasswordOperation::Hash) {
        if (args.Length() != 3 ||
            !crypto_v8_bytes_arg(args[0], kCryptoMaxPasswordBytes, &password) ||
            !crypto_v8_password_options_args(args, 1, 2, &options))
        {
            crypto_v8_zero_vector(password);
            (void)crypto_v8_reject_password_promise(
                isolate, context, resolver,
                "SLOPPY_E_CRYPTO_INVALID_KEY_SECRET: Password.hash arguments invalid", true);
            return;
        }
    }
    else if (operation == SlV8CryptoPasswordOperation::Verify) {
        if (args.Length() != 2 ||
            !crypto_v8_bytes_arg(args[0], kCryptoMaxPasswordBytes, &password) ||
            !crypto_v8_password_hash_arg(isolate, args[1], &encoded_hash))
        {
            crypto_v8_zero_vector(password);
            (void)crypto_v8_reject_password_promise(
                isolate, context, resolver,
                "SLOPPY_E_CRYPTO_INVALID_KEY_SECRET: Password.verify arguments invalid", true);
            return;
        }
    }
    else if (args.Length() != 3 || !crypto_v8_password_hash_arg(isolate, args[0], &encoded_hash) ||
             !crypto_v8_password_options_args(args, 1, 2, &options))
    {
        (void)crypto_v8_reject_password_promise(
            isolate, context, resolver,
            "SLOPPY_E_CRYPTO_INVALID_KEY_SECRET: Password.needsRehash arguments invalid", true);
        return;
    }

    std::shared_ptr<SlV8CryptoPasswordRequest> request(new (std::nothrow)
                                                           SlV8CryptoPasswordRequest());
    if (request == nullptr) {
        crypto_v8_zero_vector(password);
        (void)crypto_v8_reject_password_promise(
            isolate, context, resolver,
            "SLOPPY_E_CRYPTO_BACKEND_UNAVAILABLE: could not allocate request", false);
        return;
    }

    request->backend = backend;
    request->operation = operation;
    request->password = std::move(password);
    request->encoded_hash = std::move(encoded_hash);
    request->options = options;
    request->resolver.Reset(isolate, resolver);

    {
        std::lock_guard<std::mutex> lock(backend->crypto_mutex);
        if (backend->crypto_shutting_down) {
            request->resolver.Reset();
            crypto_v8_zero_vector(request->password);
            (void)crypto_v8_reject_password_promise(
                isolate, context, resolver,
                "SLOPPY_E_CRYPTO_BACKEND_UNAVAILABLE: crypto is shutting down", false);
            return;
        }
        backend->crypto_password_requests.push_back(request);
    }

    try {
        request->worker = std::thread(crypto_v8_password_worker, request);
    } catch (...) {
        request->resolver.Reset();
        crypto_v8_remove_password_request(request);
        crypto_v8_zero_vector(request->password);
        (void)crypto_v8_reject_password_promise(
            isolate, context, resolver,
            "SLOPPY_E_CRYPTO_BACKEND_UNAVAILABLE: password worker unavailable", false);
        return;
    }
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
                                    const char* message, size_t output_multiplier, Fn generate)
{
    v8::Isolate* isolate = args.GetIsolate();
    size_t length = 0U;
    size_t output_length = 0U;
    std::vector<char> text;
    v8::Local<v8::String> result;

    if (args.Length() != 1 || !crypto_v8_size_arg(args[0], kCryptoMaxTextRandom, &length)) {
        crypto_v8_throw_type_error(isolate, message);
        return;
    }

    output_length = length * output_multiplier;
    text.resize(output_length == 0U ? 1U : output_length);
    if (!sl_status_is_ok(generate(length, text.data(), text.size())) ||
        !sl_status_is_ok(crypto_v8_to_local_string(
            isolate, sl_str_from_parts(text.data(), output_length), &result)))
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
        args, "SLOPPY_E_CRYPTO_INVALID_KEY_SECRET: Random.hex length must be 0..1024", 2U,
        [](size_t length, char* out, size_t out_length) {
            return sl_crypto_random_hex(length, out, out_length);
        });
}

void crypto_v8_random_token_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    crypto_v8_random_text_callback(
        args, "SLOPPY_E_CRYPTO_INVALID_KEY_SECRET: Random.token length must be 0..1024", 1U,
        [](size_t length, char* out, size_t out_length) {
            return sl_crypto_random_token(length, out, out_length);
        });
}

void crypto_v8_random_numeric_code_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    crypto_v8_random_text_callback(
        args, "SLOPPY_E_CRYPTO_INVALID_KEY_SECRET: Random.numericCode length must be 0..1024", 1U,
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

void crypto_v8_noncrypto_xxhash64_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    std::vector<unsigned char> data;
    char hex[SL_CRYPTO_XXHASH64_HEX_LENGTH] = {0};
    v8::Local<v8::String> result;

    if (args.Length() != 1 || !crypto_v8_bytes_arg(args[0], kCryptoMaxInlineBytes, &data)) {
        crypto_v8_throw_type_error(
            isolate, "SLOPPY_E_CRYPTO_INVALID_KEY_SECRET: NonCryptoHash.xxHash64 requires "
                     "bounded Uint8Array data");
        return;
    }

    if (!sl_status_is_ok(sl_crypto_noncrypto_xxhash64_hex(
            sl_bytes_from_parts(data.data(), data.size()), hex, sizeof(hex))) ||
        !sl_status_is_ok(crypto_v8_to_local_string(
            isolate, sl_str_from_parts(hex, SL_CRYPTO_XXHASH64_HEX_LENGTH), &result)))
    {
        crypto_v8_throw_error(isolate,
                              "SLOPPY_E_CRYPTO_BACKEND_UNAVAILABLE: xxHash64 backend unavailable");
        return;
    }
    args.GetReturnValue().Set(result);
}

void crypto_v8_password_hash_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    crypto_v8_start_password_request(args, SlV8CryptoPasswordOperation::Hash);
}

void crypto_v8_password_verify_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    crypto_v8_start_password_request(args, SlV8CryptoPasswordOperation::Verify);
}

void crypto_v8_password_needs_rehash_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    crypto_v8_start_password_request(args, SlV8CryptoPasswordOperation::NeedsRehash);
}

} // namespace

void sl_v8_append_crypto_external_references(std::vector<intptr_t>* refs)
{
    if (refs == nullptr) {
        return;
    }
    refs->push_back(reinterpret_cast<intptr_t>(crypto_v8_random_bytes_callback));
    refs->push_back(reinterpret_cast<intptr_t>(crypto_v8_random_uuid_callback));
    refs->push_back(reinterpret_cast<intptr_t>(crypto_v8_random_hex_callback));
    refs->push_back(reinterpret_cast<intptr_t>(crypto_v8_random_token_callback));
    refs->push_back(reinterpret_cast<intptr_t>(crypto_v8_random_numeric_code_callback));
    refs->push_back(reinterpret_cast<intptr_t>(crypto_v8_hash_callback));
    refs->push_back(reinterpret_cast<intptr_t>(crypto_v8_hmac_callback));
    refs->push_back(reinterpret_cast<intptr_t>(crypto_v8_constant_time_equals_callback));
    refs->push_back(reinterpret_cast<intptr_t>(crypto_v8_noncrypto_xxhash64_callback));
    refs->push_back(reinterpret_cast<intptr_t>(crypto_v8_password_hash_callback));
    refs->push_back(reinterpret_cast<intptr_t>(crypto_v8_password_verify_callback));
    refs->push_back(reinterpret_cast<intptr_t>(crypto_v8_password_needs_rehash_callback));
}

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
                                crypto_v8_constant_time_equals_callback) ||
        !crypto_v8_set_function(isolate, context, crypto, "nonCryptoXxHash64",
                                crypto_v8_noncrypto_xxhash64_callback) ||
        !crypto_v8_set_function(isolate, context, crypto, "passwordHash",
                                crypto_v8_password_hash_callback) ||
        !crypto_v8_set_function(isolate, context, crypto, "passwordVerify",
                                crypto_v8_password_verify_callback) ||
        !crypto_v8_set_function(isolate, context, crypto, "passwordNeedsRehash",
                                crypto_v8_password_needs_rehash_callback))
    {
        return false;
    }
    return sloppy->Set(context, crypto_key, crypto).FromMaybe(false);
}

void sl_v8_crypto_dispose(SlV8Engine* backend)
{
    std::vector<std::shared_ptr<SlV8CryptoPasswordRequest>> requests;

    if (backend == nullptr) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(backend->crypto_mutex);
        backend->crypto_shutting_down = true;
        requests = backend->crypto_password_requests;
    }

    for (const std::shared_ptr<SlV8CryptoPasswordRequest>& request : requests) {
        if (request == nullptr) {
            continue;
        }
        request->cancelled.store(true);
        if (request->worker.joinable() && request->worker.get_id() != std::this_thread::get_id()) {
            request->worker.join();
        }
        if (!request->completion_posted.load()) {
            request->resolver.Reset();
        }
        crypto_v8_zero_vector(request->password);
    }

    {
        std::lock_guard<std::mutex> lock(backend->crypto_mutex);
        backend->crypto_password_requests.clear();
    }
}

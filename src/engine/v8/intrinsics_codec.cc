/*
 * src/engine/v8/intrinsics_codec.cc
 *
 * Installs the V8-internal codec bridge under __sloppy.codec. The public
 * stdlib owns JS validation and promise shape; this bridge owns vetted native
 * compression bytes in and bytes out.
 */
#include "engine_v8_internal.h"
#include "string_interop.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <vector>

#include <zlib.h>

namespace {

constexpr size_t kCodecMaxInlineBytes = 1024U * 1024U;
constexpr size_t kCodecMaxDecompressedBytes = 64U * 1024U * 1024U;
constexpr size_t kCodecZlibChunkBytes = 16U * 1024U;

SlStatus codec_v8_to_local_string(SlV8Engine* backend, SlStr str, v8::Local<v8::String>* out)
{
    return sl_v8_string_from_native_view(backend, str, out);
}

SlStatus codec_v8_to_local_string(v8::Isolate* isolate, SlStr str, v8::Local<v8::String>* out)
{
    SlV8Engine* backend =
        isolate == nullptr ? nullptr : static_cast<SlV8Engine*>(isolate->GetData(0));
    return codec_v8_to_local_string(backend, str, out);
}

void codec_v8_throw_type_error(v8::Isolate* isolate, const char* message)
{
    v8::Local<v8::String> local_message;
    if (!sl_status_is_ok(
            codec_v8_to_local_string(isolate, sl_str_from_cstr(message), &local_message)))
    {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "Sloppy codec type error")));
        return;
    }
    isolate->ThrowException(v8::Exception::TypeError(local_message));
}

void codec_v8_throw_error(v8::Isolate* isolate, const char* message)
{
    v8::Local<v8::String> local_message;
    if (!sl_status_is_ok(
            codec_v8_to_local_string(isolate, sl_str_from_cstr(message), &local_message)))
    {
        isolate->ThrowException(v8::Exception::Error(
            v8::String::NewFromUtf8Literal(isolate, "Sloppy codec operation failed")));
        return;
    }
    isolate->ThrowException(v8::Exception::Error(local_message));
}

bool codec_v8_set_function(v8::Isolate* isolate, v8::Local<v8::Context> context,
                           v8::Local<v8::Object> object, const char* name,
                           v8::FunctionCallback callback)
{
    v8::Local<v8::String> key;
    v8::Local<v8::Function> function;

    if (!sl_status_is_ok(codec_v8_to_local_string(isolate, sl_str_from_cstr(name), &key)) ||
        !v8::FunctionTemplate::New(isolate, callback)->GetFunction(context).ToLocal(&function))
    {
        return false;
    }
    return object->Set(context, key, function).FromMaybe(false);
}

bool codec_v8_bytes_arg(v8::Local<v8::Value> value, std::vector<unsigned char>* out)
{
    if (out == nullptr || value.IsEmpty() || !value->IsUint8Array()) {
        return false;
    }

    v8::Local<v8::Uint8Array> view = value.As<v8::Uint8Array>();
    size_t length = view->ByteLength();
    size_t offset = view->ByteOffset();
    if (length > kCodecMaxInlineBytes ||
        length > static_cast<size_t>(std::numeric_limits<uInt>::max()))
    {
        return false;
    }
    if (length == 0U) {
        out->clear();
        return true;
    }

    std::shared_ptr<v8::BackingStore> backing = view->Buffer()->GetBackingStore();
    if (backing == nullptr || backing->Data() == nullptr || offset > backing->ByteLength() ||
        length > backing->ByteLength() - offset)
    {
        return false;
    }

    const unsigned char* start =
        static_cast<const unsigned char*>(backing->Data()) + static_cast<ptrdiff_t>(offset);
    out->assign(start, start + length);
    return true;
}

bool codec_v8_int_arg(v8::Local<v8::Value> value, int min, int max, int* out)
{
    if (out == nullptr || value.IsEmpty() || !value->IsInt32()) {
        return false;
    }
    int number = value.As<v8::Int32>()->Value();
    if (number < min || number > max) {
        return false;
    }
    *out = number;
    return true;
}

bool codec_v8_size_arg(v8::Local<v8::Value> value, size_t max, size_t* out)
{
    if (out == nullptr || value.IsEmpty() || !value->IsUint32()) {
        return false;
    }
    uint32_t number = value.As<v8::Uint32>()->Value();
    if (static_cast<size_t>(number) > max) {
        return false;
    }
    *out = static_cast<size_t>(number);
    return true;
}

bool codec_v8_uint8_array(v8::Isolate* isolate, const unsigned char* data, size_t length,
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

bool codec_v8_append_zlib_output(std::vector<unsigned char>* output, const unsigned char* data,
                                 size_t length, size_t max_output)
{
    if (output == nullptr || length > max_output - output->size()) {
        return false;
    }
    output->insert(output->end(), data, data + length);
    return true;
}

bool codec_v8_gzip_bytes(const std::vector<unsigned char>& input, int level,
                         std::vector<unsigned char>* output)
{
    z_stream stream = {};
    unsigned char chunk[kCodecZlibChunkBytes] = {};

    if (output == nullptr ||
        deflateInit2(&stream, level, Z_DEFLATED, MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
    {
        return false;
    }

    stream.next_in = input.empty() ? nullptr : const_cast<Bytef*>(input.data());
    stream.avail_in = static_cast<uInt>(input.size());

    int result = Z_OK;
    do {
        stream.next_out = chunk;
        stream.avail_out = static_cast<uInt>(sizeof(chunk));
        result = deflate(&stream, Z_FINISH);
        if (result != Z_OK && result != Z_STREAM_END) {
            deflateEnd(&stream);
            return false;
        }
        size_t produced = sizeof(chunk) - stream.avail_out;
        if (!codec_v8_append_zlib_output(output, chunk, produced, kCodecMaxDecompressedBytes)) {
            deflateEnd(&stream);
            return false;
        }
    } while (result != Z_STREAM_END);

    return deflateEnd(&stream) == Z_OK;
}

enum class CodecInflateResult
{
    Ok,
    LimitExceeded,
    Corrupt
};

CodecInflateResult codec_v8_gunzip_bytes(const std::vector<unsigned char>& input, size_t max_output,
                                         std::vector<unsigned char>* output)
{
    z_stream stream = {};
    unsigned char chunk[kCodecZlibChunkBytes] = {};

    if (output == nullptr || inflateInit2(&stream, MAX_WBITS + 16) != Z_OK) {
        return CodecInflateResult::Corrupt;
    }

    stream.next_in = input.empty() ? nullptr : const_cast<Bytef*>(input.data());
    stream.avail_in = static_cast<uInt>(input.size());

    int result = Z_OK;
    do {
        stream.next_out = chunk;
        stream.avail_out = static_cast<uInt>(sizeof(chunk));
        result = inflate(&stream, Z_NO_FLUSH);
        if (result != Z_OK && result != Z_STREAM_END) {
            inflateEnd(&stream);
            return CodecInflateResult::Corrupt;
        }
        size_t produced = sizeof(chunk) - stream.avail_out;
        if (!codec_v8_append_zlib_output(output, chunk, produced, max_output)) {
            inflateEnd(&stream);
            return CodecInflateResult::LimitExceeded;
        }
        if (result == Z_OK && produced == 0U && stream.avail_in == 0U) {
            inflateEnd(&stream);
            return CodecInflateResult::Corrupt;
        }
    } while (result != Z_STREAM_END);

    return inflateEnd(&stream) == Z_OK ? CodecInflateResult::Ok : CodecInflateResult::Corrupt;
}

void codec_v8_gzip_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    std::vector<unsigned char> input;
    std::vector<unsigned char> output;
    int level = 0;
    v8::Local<v8::Uint8Array> result;

    if (args.Length() != 2 || !codec_v8_bytes_arg(args[0], &input) ||
        !codec_v8_int_arg(args[1], 0, 9, &level))
    {
        codec_v8_throw_type_error(isolate,
                                  "SLOPPY_E_INVALID_ARGUMENT: Compression.gzip arguments invalid");
        return;
    }

    if (!codec_v8_gzip_bytes(input, level, &output) ||
        !codec_v8_uint8_array(isolate, output.data(), output.size(), &result))
    {
        codec_v8_throw_error(isolate,
                             "SLOPPY_E_CODEC_COMPRESSION_BACKEND_UNAVAILABLE: gzip backend failed");
        return;
    }

    args.GetReturnValue().Set(result);
}

void codec_v8_gunzip_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    std::vector<unsigned char> input;
    std::vector<unsigned char> output;
    size_t max_output = 0U;
    v8::Local<v8::Uint8Array> result;

    if (args.Length() != 2 || !codec_v8_bytes_arg(args[0], &input) ||
        !codec_v8_size_arg(args[1], kCodecMaxDecompressedBytes, &max_output))
    {
        codec_v8_throw_type_error(
            isolate, "SLOPPY_E_INVALID_ARGUMENT: Compression.gunzip arguments invalid");
        return;
    }

    CodecInflateResult inflate_result = codec_v8_gunzip_bytes(input, max_output, &output);
    if (inflate_result == CodecInflateResult::LimitExceeded) {
        codec_v8_throw_error(isolate, "SLOPPY_E_CODEC_DECOMPRESSION_LIMIT_EXCEEDED: gunzip output "
                                      "exceeded maxOutputBytes");
        return;
    }
    if (inflate_result != CodecInflateResult::Ok ||
        !codec_v8_uint8_array(isolate, output.data(), output.size(), &result))
    {
        codec_v8_throw_error(isolate,
                             "SLOPPY_E_CODEC_COMPRESSED_STREAM_CORRUPT: gunzip input is corrupt");
        return;
    }

    args.GetReturnValue().Set(result);
}

} // namespace

bool sl_v8_install_codec_intrinsics(SlV8Engine* backend, v8::Local<v8::Context> context,
                                    v8::Local<v8::Object> sloppy)
{
    v8::Isolate* isolate = backend == nullptr ? nullptr : backend->isolate;
    v8::Local<v8::String> codec_key;
    v8::Local<v8::Object> codec;

    if (backend == nullptr || isolate == nullptr) {
        return false;
    }

    if (!backend->has_runtime_features ||
        !sl_v8_runtime_feature_active(backend, SL_RUNTIME_FEATURE_STDLIB_CODEC))
    {
        return true;
    }

    if (!sl_status_is_ok(codec_v8_to_local_string(backend, sl_str_from_cstr("codec"), &codec_key)))
    {
        return false;
    }

    codec = v8::Object::New(isolate);
    if (!codec_v8_set_function(isolate, context, codec, "gzip", codec_v8_gzip_callback) ||
        !codec_v8_set_function(isolate, context, codec, "gunzip", codec_v8_gunzip_callback))
    {
        return false;
    }

    return sloppy->Set(context, codec_key, codec).FromMaybe(false);
}

/*
 * src/engine/v8/intrinsics_fs.cc
 *
 * Installs the V8-internal filesystem bridge under __sloppy.fs. Blocking filesystem work is
 * submitted to the Slop provider/offload executor; workers never enter V8, and completion
 * dispatch settles Promises on the owning isolate thread.
 */
#include "engine_v8_internal.h"
#include "string_interop.h"

#include "sloppy/fs.h"

#include <climits>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

enum class FsV8Operation
{
    ReadText,
    ReadBytes,
    WriteText,
    WriteBytes,
    AppendText,
    AppendBytes,
    Exists,
    Stat,
    Copy,
    Move,
    DeleteFile,
};

struct FsV8Request
{
    SlV8Engine* backend = nullptr;
    FsV8Operation operation = FsV8Operation::ReadText;
    v8::Global<v8::Promise::Resolver> resolver;
    std::string path;
    std::string to_path;
    std::string text;
    std::vector<unsigned char> bytes;
    std::string result_text;
    std::vector<unsigned char> result_bytes;
    SlFsStat stat = {};
    bool bool_result = false;
    bool overwrite = false;
    SlStatus status = sl_status_ok();
    std::string error;
};

SlStatus fs_v8_to_local_string(v8::Isolate* isolate, SlStr str, v8::Local<v8::String>* out)
{
    SlV8Engine* backend =
        isolate == nullptr ? nullptr : static_cast<SlV8Engine*>(isolate->GetData(0));
    return sl_v8_string_from_native_view(backend, str, out);
}

bool fs_v8_value_to_std_string(v8::Isolate* isolate, v8::Local<v8::Value> value, std::string* out)
{
    return out != nullptr && value->IsString() && sl_v8_std_string_from_value(isolate, value, out);
}

void fs_v8_throw_type_error(v8::Isolate* isolate, const char* message)
{
    v8::Local<v8::String> local_message;
    if (!sl_status_is_ok(fs_v8_to_local_string(isolate, sl_str_from_cstr(message), &local_message)))
    {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "Sloppy filesystem type error")));
        return;
    }
    isolate->ThrowException(v8::Exception::TypeError(local_message));
}

std::string fs_v8_diag_message(const SlDiag& diag, const char* fallback)
{
    if (diag.message.ptr != nullptr && diag.message.length != 0U) {
        return std::string(diag.message.ptr, diag.message.length);
    }
    return fallback == nullptr ? "filesystem operation failed" : fallback;
}

SlCapabilityOperation fs_v8_capability_operation(FsV8Operation operation)
{
    switch (operation) {
    case FsV8Operation::ReadText:
    case FsV8Operation::ReadBytes:
        return SL_CAPABILITY_OPERATION_READ;
    case FsV8Operation::WriteText:
    case FsV8Operation::WriteBytes:
        return SL_CAPABILITY_OPERATION_WRITE;
    case FsV8Operation::AppendText:
    case FsV8Operation::AppendBytes:
        return SL_CAPABILITY_OPERATION_APPEND;
    case FsV8Operation::DeleteFile:
        return SL_CAPABILITY_OPERATION_DELETE;
    case FsV8Operation::Exists:
    case FsV8Operation::Stat:
        return SL_CAPABILITY_OPERATION_METADATA;
    case FsV8Operation::Copy:
    case FsV8Operation::Move:
        return SL_CAPABILITY_OPERATION_READWRITE;
    default:
        return SL_CAPABILITY_OPERATION_READWRITE;
    }
}

bool fs_v8_operation_writes(FsV8Operation operation)
{
    return operation == FsV8Operation::WriteText || operation == FsV8Operation::WriteBytes ||
           operation == FsV8Operation::AppendText || operation == FsV8Operation::AppendBytes;
}

bool fs_v8_operation_reads(FsV8Operation operation)
{
    return operation == FsV8Operation::ReadText || operation == FsV8Operation::ReadBytes;
}

void fs_v8_copy_bytes(void* dst, const unsigned char* src, size_t length)
{
    unsigned char* out = static_cast<unsigned char*>(dst);
    size_t index = 0U;

    for (index = 0U; index < length; index += 1U) {
        out[index] = src[index];
    }
}

bool fs_v8_is_valid_utf8(const unsigned char* bytes, size_t length)
{
    size_t index = 0U;

    while (index < length) {
        unsigned char ch = bytes[index];
        size_t remaining = length - index;
        size_t extra = 0U;
        unsigned char min_second = 0x80U;
        unsigned char max_second = 0xBFU;

        if (ch <= 0x7FU) {
            index += 1U;
            continue;
        }
        if (ch >= 0xC2U && ch <= 0xDFU) {
            extra = 1U;
        }
        else if (ch >= 0xE0U && ch <= 0xEFU) {
            extra = 2U;
            if (ch == 0xE0U) {
                min_second = 0xA0U;
            }
            if (ch == 0xEDU) {
                max_second = 0x9FU;
            }
        }
        else if (ch >= 0xF0U && ch <= 0xF4U) {
            extra = 3U;
            if (ch == 0xF0U) {
                min_second = 0x90U;
            }
            if (ch == 0xF4U) {
                max_second = 0x8FU;
            }
        }
        else {
            return false;
        }
        if (remaining <= extra || bytes[index + 1U] < min_second || bytes[index + 1U] > max_second)
        {
            return false;
        }
        for (size_t tail = 2U; tail <= extra; tail += 1U) {
            if (bytes[index + tail] < 0x80U || bytes[index + tail] > 0xBFU) {
                return false;
            }
        }
        index += extra + 1U;
    }
    return true;
}

SlStatus fs_v8_resolve(SlArena* arena, const FsV8Request* request, SlStr input,
                       SlFsResolvedPath* out, SlDiag* out_diag)
{
    const SlFsRoot roots[] = {
        {sl_str_from_cstr("data"), sl_str_from_cstr("./data")},
        {sl_str_from_cstr("tmp"), sl_str_from_cstr("./tmp")},
        {sl_str_from_cstr("uploads"), sl_str_from_cstr("./uploads")},
    };
    const SlV8Engine* backend = request == nullptr ? nullptr : request->backend;
    SlFsPolicy fallback = sl_fs_development_policy(sl_str_from_cstr("."));
    const SlFsPolicy* policy = backend == nullptr ? nullptr : backend->filesystem_policy;

    if (policy == nullptr) {
        fallback.roots = roots;
        fallback.root_count = sizeof(roots) / sizeof(roots[0]);
        policy = &fallback;
    }
    return sl_fs_resolve_path(arena, policy, input, out, out_diag);
}

SlStatus fs_v8_run_operation(SlArena* arena, FsV8Request* request, SlDiag* diag)
{
    SlFsResolvedPath path = {};
    SlFsResolvedPath to_path = {};
    SlStatus status = fs_v8_resolve(
        arena, request, sl_str_from_parts(request->path.data(), request->path.size()), &path, diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    SlStr resolved = sl_owned_str_as_view(path.path);
    switch (request->operation) {
    case FsV8Operation::ReadText:
    case FsV8Operation::ReadBytes: {
        SlOwnedBytes bytes = {};
        status = sl_fs_read_file(arena, resolved, &bytes, diag);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        if (request->operation == FsV8Operation::ReadText) {
            request->result_text.assign(reinterpret_cast<const char*>(bytes.ptr), bytes.length);
        }
        else {
            request->result_bytes.assign(bytes.ptr, bytes.ptr + bytes.length);
        }
        return sl_status_ok();
    }
    case FsV8Operation::WriteText:
    case FsV8Operation::AppendText:
        return sl_fs_write_file(
            resolved,
            sl_bytes_from_parts(reinterpret_cast<const unsigned char*>(request->text.data()),
                                request->text.size()),
            request->operation == FsV8Operation::AppendText, diag);
    case FsV8Operation::WriteBytes:
    case FsV8Operation::AppendBytes:
        return sl_fs_write_file(
            resolved,
            sl_bytes_from_parts(request->bytes.empty() ? nullptr : request->bytes.data(),
                                request->bytes.size()),
            request->operation == FsV8Operation::AppendBytes, diag);
    case FsV8Operation::Exists:
        return sl_fs_exists(resolved, &request->bool_result, diag);
    case FsV8Operation::Stat:
        status = sl_fs_stat(resolved, &request->stat, diag);
        if (sl_status_code(status) == SL_STATUS_OUT_OF_RANGE) {
            request->stat = SlFsStat{};
            return sl_status_ok();
        }
        return status;
    case FsV8Operation::DeleteFile:
        return sl_fs_delete_file(resolved, diag);
    case FsV8Operation::Copy:
    case FsV8Operation::Move:
        status = fs_v8_resolve(arena, request,
                               sl_str_from_parts(request->to_path.data(), request->to_path.size()),
                               &to_path, diag);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        if (request->operation == FsV8Operation::Copy) {
            return sl_fs_copy_file(resolved, sl_owned_str_as_view(to_path.path), request->overwrite,
                                   diag);
        }
        return sl_fs_move_file(resolved, sl_owned_str_as_view(to_path.path), request->overwrite,
                               diag);
    default:
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
}

SlStatus fs_v8_provider_run(SlProviderOperation* operation, void* user, SlDiagCode* out_diag_code,
                            SlStr* out_message)
{
    FsV8Request* request = static_cast<FsV8Request*>(user);
    size_t capacity = 64U * 1024U;
    SlDiag diag = {};
    SlStatus status;

    (void)operation;
    if (request == nullptr || out_diag_code == nullptr || out_message == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    for (;;) {
        std::vector<unsigned char> storage(capacity);
        SlArena arena = {};

        diag = {};
        status = sl_arena_init(&arena, storage.data(), storage.size());
        if (!sl_status_is_ok(status)) {
            break;
        }
        status = fs_v8_run_operation(&arena, request, &diag);
        if (sl_status_code(status) != SL_STATUS_OUT_OF_MEMORY ||
            !fs_v8_operation_reads(request->operation) || capacity > (SIZE_MAX / 2U))
        {
            break;
        }
        capacity *= 2U;
    }

    request->status = status;
    if (!sl_status_is_ok(status)) {
        request->error = fs_v8_diag_message(diag, "filesystem operation failed");
        *out_diag_code = diag.code == SL_DIAG_NONE ? SL_DIAG_INTERNAL_ERROR : diag.code;
        *out_message = sl_str_from_parts(request->error.data(), request->error.size());
    }
    else {
        *out_diag_code = SL_DIAG_NONE;
        *out_message = sl_str_from_cstr("filesystem operation completed");
    }
    return status;
}

bool fs_v8_result_value(v8::Isolate* isolate, v8::Local<v8::Context> context, FsV8Request* request,
                        v8::Local<v8::Value>* out, std::string* out_error)
{
    if (out == nullptr || out_error == nullptr) {
        return false;
    }

    switch (request->operation) {
    case FsV8Operation::ReadText: {
        v8::Local<v8::String> text;
        if (request->result_text.size() > static_cast<size_t>(INT_MAX)) {
            *out_error = "File text is too large to decode";
            return false;
        }
        if (!fs_v8_is_valid_utf8(
                reinterpret_cast<const unsigned char*>(request->result_text.data()),
                request->result_text.size()))
        {
            *out_error = "Invalid UTF-8 in file";
            return false;
        }
        if (!v8::String::NewFromUtf8(isolate, request->result_text.data(),
                                     v8::NewStringType::kNormal,
                                     static_cast<int>(request->result_text.size()))
                 .ToLocal(&text))
        {
            *out_error = "Invalid UTF-8 in file";
            return false;
        }
        *out = text;
        return true;
    }
    case FsV8Operation::ReadBytes: {
        auto backing = v8::ArrayBuffer::NewBackingStore(isolate, request->result_bytes.size());
        if (!request->result_bytes.empty()) {
            fs_v8_copy_bytes(backing->Data(), request->result_bytes.data(),
                             request->result_bytes.size());
        }
        v8::Local<v8::ArrayBuffer> buffer = v8::ArrayBuffer::New(isolate, std::move(backing));
        *out = v8::Uint8Array::New(buffer, 0, request->result_bytes.size());
        return true;
    }
    case FsV8Operation::Exists:
        *out = v8::Boolean::New(isolate, request->bool_result);
        return true;
    case FsV8Operation::Stat: {
        v8::Local<v8::Object> object = v8::Object::New(isolate);
        v8::Local<v8::String> exists_key;
        v8::Local<v8::String> kind_key;
        v8::Local<v8::String> size_key;
        const char* kind = request->stat.kind == SL_FS_NODE_FILE        ? "file"
                           : request->stat.kind == SL_FS_NODE_DIRECTORY ? "directory"
                           : request->stat.kind == SL_FS_NODE_OTHER     ? "other"
                                                                        : "missing";
        (void)fs_v8_to_local_string(isolate, sl_str_from_cstr("exists"), &exists_key);
        (void)fs_v8_to_local_string(isolate, sl_str_from_cstr("kind"), &kind_key);
        (void)fs_v8_to_local_string(isolate, sl_str_from_cstr("size"), &size_key);
        (void)object->Set(context, exists_key, v8::Boolean::New(isolate, request->stat.exists));
        (void)object->Set(context, kind_key,
                          v8::String::NewFromUtf8(isolate, kind).ToLocalChecked());
        (void)object->Set(context, size_key,
                          v8::Number::New(isolate, static_cast<double>(request->stat.size)));
        *out = object;
        return true;
    }
    default:
        *out = v8::Undefined(isolate);
        return true;
    }
}

SlStatus fs_v8_completion_dispatch(SlAsyncLoop* loop, const SlAsyncCompletion* completion,
                                   void* user)
{
    SlProviderOperation* operation =
        completion == nullptr ? nullptr : static_cast<SlProviderOperation*>(completion->payload);
    FsV8Request* request =
        operation == nullptr ? nullptr : static_cast<FsV8Request*>(operation->run_user);
    SlV8Engine* backend = request == nullptr ? nullptr : request->backend;

    (void)loop;
    (void)user;
    if (request == nullptr || backend == nullptr || backend->isolate == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    v8::Isolate* isolate = backend->isolate;
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = backend->context.Get(isolate);
    v8::Context::Scope context_scope(context);
    v8::Local<v8::Promise::Resolver> resolver = request->resolver.Get(isolate);

    bool ok = false;
    if (!sl_status_is_ok(request->status)) {
        v8::Local<v8::String> message =
            v8::String::NewFromUtf8(isolate, request->error.c_str()).ToLocalChecked();
        ok = resolver->Reject(context, v8::Exception::Error(message)).FromMaybe(false);
    }
    else {
        v8::Local<v8::Value> value;
        std::string error;
        if (fs_v8_result_value(isolate, context, request, &value, &error)) {
            ok = resolver->Resolve(context, value).FromMaybe(false);
        }
        else {
            v8::Local<v8::String> message =
                v8::String::NewFromUtf8(isolate, error.c_str()).ToLocalChecked();
            ok = resolver->Reject(context, v8::Exception::Error(message)).FromMaybe(false);
        }
    }
    request->resolver.Reset();
    delete request;
    isolate->PerformMicrotaskCheckpoint();
    return ok ? sl_status_ok() : sl_status_from_code(SL_STATUS_INVALID_STATE);
}

bool fs_v8_get_optional_bool(v8::Isolate* isolate, v8::Local<v8::Context> context,
                             v8::Local<v8::Value> value, const char* key, bool* out)
{
    v8::Local<v8::String> local_key;
    v8::Local<v8::Value> property;

    if (out == nullptr) {
        return false;
    }
    *out = false;
    if (value->IsUndefined() || value->IsNull()) {
        return true;
    }
    if (!value->IsObject() ||
        !sl_status_is_ok(fs_v8_to_local_string(isolate, sl_str_from_cstr(key), &local_key)) ||
        !value.As<v8::Object>()->Get(context, local_key).ToLocal(&property))
    {
        return false;
    }
    if (property->IsUndefined() || property->IsNull()) {
        return true;
    }
    if (!property->IsBoolean()) {
        return false;
    }
    *out = property->BooleanValue(isolate);
    return true;
}

bool fs_v8_value_to_bytes(v8::Local<v8::Value> value, std::vector<unsigned char>* out)
{
    if (out == nullptr || !value->IsUint8Array()) {
        return false;
    }
    v8::Local<v8::Uint8Array> array = value.As<v8::Uint8Array>();
    v8::Local<v8::ArrayBuffer> buffer = array->Buffer();
    std::shared_ptr<v8::BackingStore> backing = buffer->GetBackingStore();
    size_t offset = array->ByteOffset();
    size_t length = array->ByteLength();
    const unsigned char* start = static_cast<const unsigned char*>(backing->Data()) + offset;
    out->assign(start, start + length);
    return true;
}

void fs_v8_submit_callback(const v8::FunctionCallbackInfo<v8::Value>& args, FsV8Operation operation)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::unique_ptr<FsV8Request> request(new (std::nothrow) FsV8Request());
    v8::Local<v8::Promise::Resolver> resolver;

    if (backend == nullptr || backend->async_loop == nullptr || !backend->fs_executor_initialized) {
        fs_v8_throw_type_error(isolate, "__sloppy.fs is unavailable because stdlib.fs is inactive");
        return;
    }
    if (!request || !v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
        fs_v8_throw_type_error(isolate, "__sloppy.fs could not create a Promise");
        return;
    }
    if (args.Length() < 1 || !fs_v8_value_to_std_string(isolate, args[0], &request->path) ||
        request->path.empty())
    {
        fs_v8_throw_type_error(isolate, "__sloppy.fs requires a non-empty path string");
        return;
    }
    request->backend = backend;
    request->operation = operation;

    if (fs_v8_operation_writes(operation)) {
        if (args.Length() < 2) {
            fs_v8_throw_type_error(isolate, "__sloppy.fs write/append requires data");
            return;
        }
        if (operation == FsV8Operation::WriteText || operation == FsV8Operation::AppendText) {
            if (!fs_v8_value_to_std_string(isolate, args[1], &request->text)) {
                fs_v8_throw_type_error(isolate, "__sloppy.fs text data must be a string");
                return;
            }
        }
        else if (!fs_v8_value_to_bytes(args[1], &request->bytes)) {
            fs_v8_throw_type_error(isolate, "__sloppy.fs byte data must be a Uint8Array");
            return;
        }
    }
    if (operation == FsV8Operation::Copy || operation == FsV8Operation::Move) {
        if (args.Length() < 2 || !fs_v8_value_to_std_string(isolate, args[1], &request->to_path) ||
            request->to_path.empty())
        {
            fs_v8_throw_type_error(isolate, "__sloppy.fs copy/move requires a target path string");
            return;
        }
        if (!fs_v8_get_optional_bool(isolate, context,
                                     args.Length() > 2 ? args[2] : v8::Undefined(isolate),
                                     "overwrite", &request->overwrite))
        {
            fs_v8_throw_type_error(isolate, "__sloppy.fs overwrite option must be boolean");
            return;
        }
    }

    request->resolver.Reset(isolate, resolver);
    args.GetReturnValue().Set(resolver->GetPromise());

    SlProviderOperationDescriptor descriptor = sl_provider_operation_descriptor_init(
        sl_str_from_cstr("stdlib.fs"), sl_str_from_cstr("filesystem"),
        SL_PROVIDER_OPERATION_KIND_INTERNAL, sl_str_from_cstr("fs"),
        SL_PROVIDER_EXECUTION_BLOCKING_POOL, fs_v8_completion_dispatch, nullptr);
    (void)sl_provider_operation_descriptor_attach_capability(
        &descriptor, sl_str_from_cstr("stdlib.fs"), fs_v8_capability_operation(operation));
    (void)sl_provider_operation_descriptor_attach_run(&descriptor, fs_v8_provider_run,
                                                      request.get());

    SlProviderOperation* provider_operation = nullptr;
    SlStatus status = sl_provider_executor_submit(&backend->fs_executor, backend->arena,
                                                  &descriptor, &provider_operation);
    if (!sl_status_is_ok(status)) {
        v8::Local<v8::String> message =
            v8::String::NewFromUtf8Literal(isolate, "filesystem operation could not be submitted");
        (void)resolver->Reject(context, v8::Exception::Error(message));
        request->resolver.Reset();
        return;
    }
    (void)provider_operation;
    request.release();
}

void fs_v8_read_text_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::ReadText);
}

void fs_v8_read_bytes_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::ReadBytes);
}

void fs_v8_write_text_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::WriteText);
}

void fs_v8_write_bytes_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::WriteBytes);
}

void fs_v8_append_text_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::AppendText);
}

void fs_v8_append_bytes_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::AppendBytes);
}

void fs_v8_exists_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::Exists);
}

void fs_v8_stat_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::Stat);
}

void fs_v8_copy_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::Copy);
}

void fs_v8_move_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::Move);
}

void fs_v8_delete_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    fs_v8_submit_callback(args, FsV8Operation::DeleteFile);
}

bool fs_v8_set_function(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        v8::Local<v8::Object> object, const char* name,
                        v8::FunctionCallback callback)
{
    v8::Local<v8::String> key;
    v8::Local<v8::FunctionTemplate> function_template;
    v8::Local<v8::Function> function;

    if (!sl_status_is_ok(fs_v8_to_local_string(isolate, sl_str_from_cstr(name), &key))) {
        return false;
    }
    function_template = v8::FunctionTemplate::New(isolate, callback);
    if (!function_template->GetFunction(context).ToLocal(&function)) {
        return false;
    }
    return object->Set(context, key, function).FromMaybe(false);
}

} // namespace

bool sl_v8_install_fs_intrinsics(SlV8Engine* backend, v8::Local<v8::Context> context,
                                 v8::Local<v8::Object> sloppy)
{
    v8::Isolate* isolate = backend == nullptr ? nullptr : backend->isolate;
    v8::Local<v8::String> fs_key;

    if (backend == nullptr || isolate == nullptr) {
        return false;
    }
    v8::Local<v8::Object> fs = v8::Object::New(isolate);
    if (!backend->has_runtime_features ||
        !sl_v8_runtime_feature_active(backend, SL_RUNTIME_FEATURE_STDLIB_FS))
    {
        return true;
    }
    if (!sl_status_is_ok(fs_v8_to_local_string(isolate, sl_str_from_cstr("fs"), &fs_key))) {
        return false;
    }
    if (!fs_v8_set_function(isolate, context, fs, "readText", fs_v8_read_text_callback) ||
        !fs_v8_set_function(isolate, context, fs, "readBytes", fs_v8_read_bytes_callback) ||
        !fs_v8_set_function(isolate, context, fs, "writeText", fs_v8_write_text_callback) ||
        !fs_v8_set_function(isolate, context, fs, "writeBytes", fs_v8_write_bytes_callback) ||
        !fs_v8_set_function(isolate, context, fs, "appendText", fs_v8_append_text_callback) ||
        !fs_v8_set_function(isolate, context, fs, "appendBytes", fs_v8_append_bytes_callback) ||
        !fs_v8_set_function(isolate, context, fs, "exists", fs_v8_exists_callback) ||
        !fs_v8_set_function(isolate, context, fs, "stat", fs_v8_stat_callback) ||
        !fs_v8_set_function(isolate, context, fs, "copy", fs_v8_copy_callback) ||
        !fs_v8_set_function(isolate, context, fs, "move", fs_v8_move_callback) ||
        !fs_v8_set_function(isolate, context, fs, "delete", fs_v8_delete_callback) ||
        !sloppy->Set(context, fs_key, fs).FromMaybe(false))
    {
        return false;
    }
    return true;
}

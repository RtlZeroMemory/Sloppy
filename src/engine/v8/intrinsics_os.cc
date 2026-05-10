/*
 * src/engine/v8/intrinsics_os.cc
 *
 * Installs the V8-internal OS bridge under __sloppy.os. Blocking process work is copied
 * into Sloppy-owned requests, executed off the isolate owner thread, and settled through the
 * engine async loop.
 */
#include "engine_v8_internal.h"
#include "string_interop.h"

#include "sloppy/os.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <utility>
#include <vector>

constexpr size_t kOsV8DefaultArenaBytes = 1024U * 1024U;
constexpr size_t kOsV8MaxCaptureBytes = 16U * 1024U * 1024U;
constexpr size_t kOsV8MaxPipeReadBytes = 1024U * 1024U;
constexpr size_t kOsV8RequestArenaSlackBytes = 4096U;
constexpr size_t kOsV8MaxArgCount = 256U;
constexpr size_t kOsV8MaxArgBytes = 32768U;

struct OsV8Process
{
    std::vector<unsigned char> storage;
    SlArena arena = {};
    SlStatus arena_status = sl_status_from_code(SL_STATUS_INTERNAL);
    SlOsProcessHandle* handle = nullptr;
    std::mutex mutex;
    bool disposed = false;
};

enum class OsV8Operation
{
    Run,
    Start,
    Wait,
    ReadStdout,
    ReadStderr,
    WriteStdin,
    CloseStdin,
    Terminate,
    Kill,
    Cancel,
    Dispose
};

struct SlV8OsRequest
{
    SlV8Engine* backend = nullptr;
    v8::Global<v8::Promise::Resolver> resolver;
    std::thread worker;
    std::atomic_bool cancelled = false;
    std::atomic_bool completion_posted = false;
    OsV8Operation operation = OsV8Operation::Run;
    SlStatus status = sl_status_ok();
    SlDiag diag = {};
    std::vector<unsigned char> storage;
    SlArena arena = {};
    std::string command;
    std::vector<std::string> args;
    std::string cwd;
    std::vector<std::pair<std::string, std::string>> env;
    SlOsProcessCaptureMode capture = SL_OS_PROCESS_CAPTURE_TEXT;
    size_t max_stdout_bytes = 65536U;
    size_t max_stderr_bytes = 65536U;
    uint64_t timeout_ms = 0U;
    SlOsProcessPipeMode stdin_mode = SL_OS_PROCESS_PIPE_IGNORE;
    SlOsProcessPipeMode stdout_mode = SL_OS_PROCESS_PIPE_IGNORE;
    SlOsProcessPipeMode stderr_mode = SL_OS_PROCESS_PIPE_IGNORE;
    std::shared_ptr<OsV8Process> process;
    SlResourceId resource_id = {};
    size_t max_bytes = 0U;
    std::vector<unsigned char> input;
    SlOsProcessRunResult run_result = {};
    SlOsProcessExit exit = {};
    SlOsProcessPipeRead pipe_read = {};
    size_t written = 0U;
};

struct OsV8CompletionPayload
{
    std::shared_ptr<SlV8OsRequest> request;
};

namespace {

bool os_v8_set_function(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        v8::Local<v8::Object> object, const char* name,
                        v8::FunctionCallback callback);

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
           object->Set(context, key, v8::Integer::NewFromUnsigned(isolate, value)).FromMaybe(false);
}

bool os_v8_resource_private(v8::Isolate* isolate, const char* name, v8::Local<v8::Private>* out)
{
    v8::Local<v8::String> key;

    if (out == nullptr ||
        !sl_status_is_ok(os_v8_to_local_string(isolate, sl_str_from_cstr(name), &key)))
    {
        return false;
    }

    *out = v8::Private::ForApi(isolate, key);
    return true;
}

void os_v8_throw(v8::Isolate* isolate, const char* message)
{
    v8::Local<v8::String> text;
    if (!sl_status_is_ok(os_v8_to_local_string(isolate, sl_str_from_cstr(message), &text))) {
        text = v8::String::NewFromUtf8Literal(isolate, "Sloppy OS operation failed");
    }
    isolate->ThrowException(v8::Exception::Error(text));
}

void os_v8_throw_type_error(v8::Isolate* isolate, const char* message)
{
    v8::Local<v8::String> text;
    if (!sl_status_is_ok(os_v8_to_local_string(isolate, sl_str_from_cstr(message), &text))) {
        text = v8::String::NewFromUtf8Literal(isolate, "Sloppy OS type error");
    }
    isolate->ThrowException(v8::Exception::TypeError(text));
}

SlDiag os_v8_diag(SlDiagCode code, const char* message)
{
    SlDiag diag = {};
    diag.severity = SL_DIAG_SEVERITY_ERROR;
    diag.code = code;
    diag.message = sl_str_from_cstr(message);
    diag.primary_span = sl_source_span_unknown();
    return diag;
}

std::string os_v8_diag_message(const SlV8OsRequest& request)
{
    SlStr code = sl_diag_code_name(request.diag.code);
    if (code.ptr != nullptr && code.length != 0U) {
        std::string message(code.ptr, code.length);
        if (request.diag.message.ptr != nullptr && request.diag.message.length != 0U) {
            message += ": ";
            message.append(request.diag.message.ptr, request.diag.message.length);
        }
        return message;
    }
    return "SLOPPY_E_OS_FEATURE_UNAVAILABLE: OS operation failed";
}

bool os_v8_reject_promise(v8::Isolate* isolate, v8::Local<v8::Context> context,
                          v8::Local<v8::Promise::Resolver> resolver, const char* message,
                          bool type_error)
{
    v8::Local<v8::String> local_message;
    v8::Local<v8::Value> exception;

    if (!sl_status_is_ok(os_v8_to_local_string(isolate, sl_str_from_cstr(message), &local_message)))
    {
        return false;
    }
    exception =
        type_error ? v8::Exception::TypeError(local_message) : v8::Exception::Error(local_message);
    bool ok = resolver->Reject(context, exception).FromMaybe(false);
    isolate->PerformMicrotaskCheckpoint();
    return ok;
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

bool os_v8_value_to_std_string(v8::Isolate* isolate, v8::Local<v8::Value> value, std::string* out)
{
    if (out == nullptr || value.IsEmpty() || !value->IsString()) {
        return false;
    }
    return sl_v8_std_string_from_value(isolate, value, out);
}

bool os_v8_string_valid(const std::string& value)
{
    return value.find('\0') == std::string::npos && value.size() <= kOsV8MaxArgBytes;
}

bool os_v8_string_arg(v8::Isolate* isolate, v8::Local<v8::Value> value, std::string* out)
{
    return os_v8_value_to_std_string(isolate, value, out) && os_v8_string_valid(*out) &&
           !out->empty();
}

bool os_v8_array_to_strings(v8::Isolate* isolate, v8::Local<v8::Context> context,
                            v8::Local<v8::Value> value, std::vector<std::string>* out)
{
    if (out == nullptr || value.IsEmpty() || !value->IsArray()) {
        return false;
    }
    v8::Local<v8::Array> array = value.As<v8::Array>();
    uint32_t length = array->Length();
    if (length > kOsV8MaxArgCount) {
        return false;
    }
    out->clear();
    out->reserve(length);
    for (uint32_t index = 0U; index < length; index += 1U) {
        v8::Local<v8::Value> item;
        std::string text;
        if (!array->Get(context, index).ToLocal(&item) ||
            !os_v8_value_to_std_string(isolate, item, &text) || !os_v8_string_valid(text))
        {
            return false;
        }
        out->push_back(std::move(text));
    }
    return true;
}

bool os_v8_get_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        v8::Local<v8::Object> object, const char* name, v8::Local<v8::Value>* out)
{
    v8::Local<v8::String> key;
    if (out == nullptr ||
        !sl_status_is_ok(os_v8_to_local_string(isolate, sl_str_from_cstr(name), &key)))
    {
        return false;
    }
    return object->Get(context, key).ToLocal(out);
}

bool os_v8_number_to_size(v8::Local<v8::Context> context, v8::Local<v8::Value> value, size_t min,
                          size_t max, size_t* out)
{
    double number = 0.0;
    if (out == nullptr || value.IsEmpty() || !value->IsNumber() ||
        !value->NumberValue(context).To(&number) || !std::isfinite(number) ||
        number < static_cast<double>(min) || number > static_cast<double>(max))
    {
        return false;
    }
    *out = static_cast<size_t>(std::ceil(number));
    return true;
}

bool os_v8_number_to_u64(v8::Local<v8::Context> context, v8::Local<v8::Value> value, uint64_t max,
                         uint64_t* out)
{
    double number = 0.0;
    if (out == nullptr || value.IsEmpty() || !value->IsNumber() ||
        !value->NumberValue(context).To(&number) || !std::isfinite(number) || number < 0.0 ||
        number > static_cast<double>(max))
    {
        return false;
    }
    *out = static_cast<uint64_t>(std::ceil(number));
    return true;
}

bool os_v8_bytes_arg(v8::Isolate* isolate, v8::Local<v8::Value> value,
                     std::vector<unsigned char>* out)
{
    std::string text;
    if (out == nullptr || value.IsEmpty()) {
        return false;
    }
    if (value->IsString()) {
        if (!os_v8_value_to_std_string(isolate, value, &text)) {
            return false;
        }
        out->assign(text.begin(), text.end());
        return true;
    }
    if (!value->IsUint8Array()) {
        return false;
    }
    v8::Local<v8::Uint8Array> view = value.As<v8::Uint8Array>();
    size_t length = view->ByteLength();
    size_t offset = view->ByteOffset();
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

bool os_v8_parse_env(v8::Isolate* isolate, v8::Local<v8::Context> context,
                     v8::Local<v8::Value> value,
                     std::vector<std::pair<std::string, std::string>>* out)
{
    if (out == nullptr || value.IsEmpty() || value->IsUndefined()) {
        return true;
    }
    if (!value->IsObject() || value->IsArray()) {
        return false;
    }
    v8::Local<v8::Object> object = value.As<v8::Object>();
    v8::Local<v8::Array> names;
    if (!object->GetOwnPropertyNames(context).ToLocal(&names)) {
        return false;
    }
    out->clear();
    out->reserve(names->Length());
    for (uint32_t index = 0U; index < names->Length(); index += 1U) {
        v8::Local<v8::Value> key_value;
        v8::Local<v8::Value> value_value;
        std::string key;
        std::string entry_value;
        if (!names->Get(context, index).ToLocal(&key_value) ||
            !os_v8_value_to_std_string(isolate, key_value, &key) || !os_v8_string_valid(key) ||
            key.find('=') != std::string::npos ||
            !object->Get(context, key_value).ToLocal(&value_value) ||
            !os_v8_value_to_std_string(isolate, value_value, &entry_value) ||
            !os_v8_string_valid(entry_value))
        {
            return false;
        }
        out->push_back({std::move(key), std::move(entry_value)});
    }
    return true;
}

bool os_v8_parse_capture(v8::Isolate* isolate, v8::Local<v8::Value> value,
                         SlOsProcessCaptureMode* out)
{
    std::string capture;
    if (out == nullptr || value.IsEmpty() || value->IsUndefined()) {
        return true;
    }
    if (!os_v8_value_to_std_string(isolate, value, &capture)) {
        return false;
    }
    if (capture == "none") {
        *out = SL_OS_PROCESS_CAPTURE_NONE;
        return true;
    }
    if (capture == "text") {
        *out = SL_OS_PROCESS_CAPTURE_TEXT;
        return true;
    }
    if (capture == "bytes") {
        *out = SL_OS_PROCESS_CAPTURE_BYTES;
        return true;
    }
    return false;
}

bool os_v8_parse_pipe_mode(v8::Isolate* isolate, v8::Local<v8::Value> value,
                           SlOsProcessPipeMode* out)
{
    std::string mode;
    if (out == nullptr || value.IsEmpty() || value->IsUndefined()) {
        return true;
    }
    if (!os_v8_value_to_std_string(isolate, value, &mode)) {
        return false;
    }
    if (mode == "ignore") {
        *out = SL_OS_PROCESS_PIPE_IGNORE;
        return true;
    }
    if (mode == "pipe") {
        *out = SL_OS_PROCESS_PIPE_PIPE;
        return true;
    }
    return false;
}

bool os_v8_request_arena_init(const std::shared_ptr<SlV8OsRequest>& request, size_t capacity)
{
    try {
        request->storage.resize(capacity);
    } catch (...) {
        return false;
    }
    return sl_status_is_ok(
        sl_arena_init(&request->arena, request->storage.data(), request->storage.size()));
}

size_t os_v8_process_run_arena_bytes(const SlV8OsRequest& request)
{
    size_t stdout_bytes = request.max_stdout_bytes == 0U ? 65536U : request.max_stdout_bytes;
    size_t stderr_bytes = request.max_stderr_bytes == 0U ? 65536U : request.max_stderr_bytes;
    size_t capacity = kOsV8RequestArenaSlackBytes;

    capacity += stdout_bytes + 1U;
    capacity += stderr_bytes + 1U;
    return std::max(capacity, kOsV8DefaultArenaBytes);
}

std::shared_ptr<OsV8Process> os_v8_process_create(void)
{
    std::shared_ptr<OsV8Process> process(new (std::nothrow) OsV8Process());
    if (process == nullptr) {
        return nullptr;
    }
    try {
        process->storage.resize(kOsV8DefaultArenaBytes);
    } catch (...) {
        return nullptr;
    }
    process->arena_status =
        sl_arena_init(&process->arena, process->storage.data(), process->storage.size());
    return process;
}

bool os_v8_handle_arg(v8::Isolate* isolate, v8::Local<v8::Context> context,
                      v8::Local<v8::Value> value, SlResourceId* out)
{
    v8::Local<v8::Value> slot_value;
    v8::Local<v8::Value> generation_value;
    v8::Local<v8::Private> slot_key;
    v8::Local<v8::Private> generation_key;

    if (out == nullptr || value.IsEmpty() || !value->IsObject() ||
        !os_v8_resource_private(isolate, "sloppy.os.resource.slot", &slot_key) ||
        !os_v8_resource_private(isolate, "sloppy.os.resource.generation", &generation_key))
    {
        return false;
    }
    v8::Local<v8::Object> object = value.As<v8::Object>();
    if (!object->GetPrivate(context, slot_key).ToLocal(&slot_value) ||
        !object->GetPrivate(context, generation_key).ToLocal(&generation_value) ||
        !slot_value->IsUint32() || !generation_value->IsUint32())
    {
        return false;
    }
    out->slot = slot_value.As<v8::Uint32>()->Value();
    out->generation = generation_value.As<v8::Uint32>()->Value();
    return sl_resource_id_is_valid(*out);
}

bool os_v8_resource_to_handle(v8::Isolate* isolate, v8::Local<v8::Context> context, SlResourceId id,
                              v8::Local<v8::Object>* out)
{
    v8::Local<v8::Private> slot_key;
    v8::Local<v8::Private> generation_key;
    if (out == nullptr || !os_v8_resource_private(isolate, "sloppy.os.resource.slot", &slot_key) ||
        !os_v8_resource_private(isolate, "sloppy.os.resource.generation", &generation_key))
    {
        return false;
    }
    v8::Local<v8::Object> object = v8::Object::New(isolate);
    if (!object->SetPrivate(context, slot_key, v8::Integer::NewFromUnsigned(isolate, id.slot))
             .FromMaybe(false) ||
        !object
             ->SetPrivate(context, generation_key,
                          v8::Integer::NewFromUnsigned(isolate, id.generation))
             .FromMaybe(false) ||
        !object->SetIntegrityLevel(context, v8::IntegrityLevel::kFrozen).FromMaybe(false))
    {
        return false;
    }
    *out = object;
    return true;
}

void os_v8_process_resource_cleanup(void* ptr, void* user)
{
    auto* holder = static_cast<std::shared_ptr<OsV8Process>*>(ptr);
    (void)user;
    if (holder != nullptr && *holder != nullptr) {
        std::lock_guard<std::mutex> lock((*holder)->mutex);
        if (!(*holder)->disposed && (*holder)->handle != nullptr) {
            sl_os_process_dispose((*holder)->handle);
            (*holder)->handle = nullptr;
            (*holder)->disposed = true;
        }
    }
    delete holder;
}

bool os_v8_lookup_process(SlV8Engine* backend, SlResourceId id, std::shared_ptr<OsV8Process>* out)
{
    void* ptr = nullptr;
    if (backend == nullptr || out == nullptr ||
        !sl_status_is_ok(sl_resource_table_get(&backend->resources, id, SL_RESOURCE_KIND_OS_PROCESS,
                                               &ptr, nullptr)))
    {
        return false;
    }
    auto* holder = static_cast<std::shared_ptr<OsV8Process>*>(ptr);
    if (holder == nullptr || *holder == nullptr) {
        return false;
    }
    *out = *holder;
    return true;
}

void os_v8_remove_request(const std::shared_ptr<SlV8OsRequest>& request)
{
    SlV8Engine* backend = request == nullptr ? nullptr : request->backend;
    if (backend == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(backend->os_mutex);
    auto& requests = backend->os_requests;
    requests.erase(std::remove(requests.begin(), requests.end(), request), requests.end());
}

SlStatus os_v8_completion_dispatch(SlAsyncLoop* loop, const SlAsyncCompletion* completion,
                                   void* user)
{
    OsV8CompletionPayload* payload =
        completion == nullptr ? nullptr : static_cast<OsV8CompletionPayload*>(completion->payload);
    std::shared_ptr<SlV8OsRequest> request = payload == nullptr ? nullptr : payload->request;
    SlV8Engine* backend = request == nullptr ? nullptr : request->backend;

    (void)loop;
    (void)user;
    if (request == nullptr || backend == nullptr || backend->isolate == nullptr ||
        request->resolver.IsEmpty() || backend->owner_thread != std::this_thread::get_id())
    {
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
        std::string message_text = os_v8_diag_message(*request);
        v8::Local<v8::String> message;
        if (!sl_status_is_ok(os_v8_to_local_string(
                isolate, sl_str_from_parts(message_text.data(), message_text.size()), &message)))
        {
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
        ok = resolver->Reject(context, v8::Exception::Error(message)).FromMaybe(false);
    }
    else if (request->operation == OsV8Operation::Run) {
        v8::Local<v8::Object> result = v8::Object::New(isolate);
        v8::Local<v8::String> key;
        (void)os_v8_to_local_string(isolate, sl_str_from_cstr("exitCode"), &key);
        (void)result->Set(context, key, v8::Integer::New(isolate, request->run_result.exit_code));
        (void)os_v8_to_local_string(isolate, sl_str_from_cstr("timedOut"), &key);
        (void)result->Set(context, key, v8::Boolean::New(isolate, request->run_result.timed_out));
        (void)os_v8_to_local_string(isolate, sl_str_from_cstr("stdoutTruncated"), &key);
        (void)result->Set(context, key,
                          v8::Boolean::New(isolate, request->run_result.stdout_truncated));
        (void)os_v8_to_local_string(isolate, sl_str_from_cstr("stderrTruncated"), &key);
        (void)result->Set(context, key,
                          v8::Boolean::New(isolate, request->run_result.stderr_truncated));

        for (int stream_index = 0; stream_index < 2; stream_index += 1) {
            const bool stdout_stream = stream_index == 0;
            SlOwnedStr native =
                stdout_stream ? request->run_result.stdout_text : request->run_result.stderr_text;
            (void)os_v8_to_local_string(
                isolate, sl_str_from_cstr(stdout_stream ? "stdout" : "stderr"), &key);
            if (request->capture == SL_OS_PROCESS_CAPTURE_BYTES) {
                std::unique_ptr<v8::BackingStore> backing =
                    v8::ArrayBuffer::NewBackingStore(isolate, native.length);
                if (backing == nullptr) {
                    return sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
                }
                if (native.length != 0U) {
                    std::copy(native.ptr, native.ptr + native.length,
                              static_cast<char*>(backing->Data()));
                }
                v8::Local<v8::ArrayBuffer> buffer =
                    v8::ArrayBuffer::New(isolate, std::move(backing));
                (void)result->Set(context, key, v8::Uint8Array::New(buffer, 0U, native.length));
            }
            else {
                v8::Local<v8::String> text;
                if (!sl_status_is_ok(
                        os_v8_to_local_string(isolate, sl_owned_str_as_view(native), &text)))
                {
                    return sl_status_from_code(SL_STATUS_INTERNAL);
                }
                (void)result->Set(context, key, text);
            }
        }
        ok = resolver->Resolve(context, result).FromMaybe(false);
    }
    else if (request->operation == OsV8Operation::Start) {
        auto* holder = new (std::nothrow) std::shared_ptr<OsV8Process>(request->process);
        SlResourceId id = {};
        v8::Local<v8::Object> handle;
        SlStatus insert_status =
            holder == nullptr
                ? sl_status_from_code(SL_STATUS_OUT_OF_MEMORY)
                : sl_resource_table_insert(&backend->resources, SL_RESOURCE_KIND_OS_PROCESS, holder,
                                           os_v8_process_resource_cleanup, nullptr, &id, nullptr);
        if (holder == nullptr || !sl_status_is_ok(insert_status) ||
            !os_v8_resource_to_handle(isolate, context, id, &handle))
        {
            if (sl_resource_id_is_valid(id)) {
                (void)sl_resource_table_close_kind(&backend->resources, id,
                                                   SL_RESOURCE_KIND_OS_PROCESS, nullptr);
            }
            else if (holder != nullptr) {
                os_v8_process_resource_cleanup(holder, nullptr);
            }
            v8::Local<v8::String> message = v8::String::NewFromUtf8Literal(
                isolate, "SLOPPY_E_OS_PROCESS_START_FAILED: resource "
                         "insert failed");
            ok = resolver->Reject(context, v8::Exception::Error(message)).FromMaybe(false);
        }
        else {
            ok = resolver->Resolve(context, handle).FromMaybe(false);
        }
    }
    else if (request->operation == OsV8Operation::ReadStdout ||
             request->operation == OsV8Operation::ReadStderr)
    {
        v8::Local<v8::String> text;
        if (!sl_status_is_ok(os_v8_to_local_string(
                isolate, sl_owned_str_as_view(request->pipe_read.bytes), &text)))
        {
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
        ok = resolver->Resolve(context, text).FromMaybe(false);
    }
    else if (request->operation == OsV8Operation::WriteStdin) {
        ok = resolver->Resolve(context, v8::Number::New(isolate, (double)request->written))
                 .FromMaybe(false);
    }
    else if (request->operation == OsV8Operation::Wait) {
        v8::Local<v8::Object> result = v8::Object::New(isolate);
        v8::Local<v8::String> key;
        (void)os_v8_to_local_string(isolate, sl_str_from_cstr("exitCode"), &key);
        (void)result->Set(context, key, v8::Integer::New(isolate, request->exit.exit_code));
        (void)os_v8_to_local_string(isolate, sl_str_from_cstr("timedOut"), &key);
        (void)result->Set(context, key, v8::Boolean::New(isolate, request->exit.timed_out));
        (void)os_v8_to_local_string(isolate, sl_str_from_cstr("cancelled"), &key);
        (void)result->Set(context, key, v8::Boolean::New(isolate, request->exit.cancelled));
        (void)os_v8_to_local_string(isolate, sl_str_from_cstr("killed"), &key);
        (void)result->Set(context, key, v8::Boolean::New(isolate, request->exit.killed));
        ok = resolver->Resolve(context, result).FromMaybe(false);
    }
    else if (request->operation == OsV8Operation::Kill ||
             request->operation == OsV8Operation::Terminate ||
             request->operation == OsV8Operation::Cancel)
    {
        v8::Local<v8::Object> result = v8::Object::New(isolate);
        v8::Local<v8::String> key;
        const bool cancelled = request->operation == OsV8Operation::Cancel;
        (void)os_v8_to_local_string(isolate, sl_str_from_cstr(cancelled ? "cancelled" : "killed"),
                                    &key);
        (void)result->Set(context, key, v8::Boolean::New(isolate, true));
        ok = resolver->Resolve(context, result).FromMaybe(false);
    }
    else {
        if (request->operation == OsV8Operation::Dispose &&
            sl_resource_id_is_valid(request->resource_id))
        {
            (void)sl_resource_table_close_kind(&backend->resources, request->resource_id,
                                               SL_RESOURCE_KIND_OS_PROCESS, nullptr);
        }
        ok = resolver->Resolve(context, v8::Undefined(isolate)).FromMaybe(false);
    }

    isolate->PerformMicrotaskCheckpoint();
    return ok ? sl_status_ok() : sl_status_from_code(SL_STATUS_INVALID_STATE);
}

void os_v8_completion_cleanup(const SlAsyncCompletion* completion, void* user)
{
    OsV8CompletionPayload* payload =
        completion == nullptr ? nullptr : static_cast<OsV8CompletionPayload*>(completion->payload);
    std::shared_ptr<SlV8OsRequest> request = payload == nullptr ? nullptr : payload->request;

    (void)user;
    if (request != nullptr) {
        request->cancelled.store(true);
        if (request->worker.joinable() && request->worker.get_id() != std::this_thread::get_id()) {
            request->worker.join();
        }
        request->resolver.Reset();
        request->completion_posted.store(false);
        os_v8_remove_request(request);
    }
    delete payload;
}

bool os_v8_post_completion(const std::shared_ptr<SlV8OsRequest>& request)
{
    SlV8Engine* backend = request == nullptr ? nullptr : request->backend;
    if (backend == nullptr) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(backend->os_mutex);
        if (request->cancelled.load() || backend->os_shutting_down ||
            backend->async_loop == nullptr)
        {
            request->cancelled.store(true);
            return false;
        }
    }

    OsV8CompletionPayload* payload = new (std::nothrow) OsV8CompletionPayload();
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
    completion.dispatch = os_v8_completion_dispatch;
    completion.cleanup = os_v8_completion_cleanup;

    for (;;) {
        {
            std::lock_guard<std::mutex> lock(backend->os_mutex);
            if (request->cancelled.load() || backend->os_shutting_down ||
                backend->async_loop == nullptr)
            {
                request->cancelled.store(true);
                delete payload;
                return false;
            }
        }

        request->completion_posted.store(true);
        SlStatus post_status = sl_async_loop_post(backend->async_loop, &completion);
        if (sl_status_is_ok(post_status)) {
            return true;
        }
        request->completion_posted.store(false);
        if (sl_status_code(post_status) != SL_STATUS_CAPACITY_EXCEEDED) {
            request->cancelled.store(true);
            delete payload;
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

void os_v8_execute_request(const std::shared_ptr<SlV8OsRequest>& request)
{
    SlOsPolicy policy = sl_os_development_policy();
    std::vector<SlStr> native_args;
    std::vector<SlOsEnvironmentOverride> native_env;

    if (request == nullptr || request->cancelled.load()) {
        return;
    }

    if (request->operation == OsV8Operation::Run || request->operation == OsV8Operation::Start) {
        native_args.reserve(request->args.size());
        for (const std::string& arg : request->args) {
            native_args.push_back(sl_str_from_parts(arg.data(), arg.size()));
        }
        native_env.reserve(request->env.size());
        for (const auto& entry : request->env) {
            native_env.push_back(
                {.key = sl_str_from_parts(entry.first.data(), entry.first.size()),
                 .value = sl_str_from_parts(entry.second.data(), entry.second.size())});
        }
    }

    if (request->operation == OsV8Operation::Run) {
        SlOsProcessRunOptions options = {
            .cwd = request->cwd.empty()
                       ? sl_str_empty()
                       : sl_str_from_parts(request->cwd.data(), request->cwd.size()),
            .environment_overrides = native_env.empty() ? nullptr : native_env.data(),
            .environment_override_count = native_env.size(),
            .capture = request->capture,
            .max_stdout_bytes = request->max_stdout_bytes,
            .max_stderr_bytes = request->max_stderr_bytes,
            .timeout_ms = request->timeout_ms};
        request->status =
            sl_os_process_run(&request->arena, &policy,
                              sl_str_from_parts(request->command.data(), request->command.size()),
                              native_args.empty() ? nullptr : native_args.data(),
                              native_args.size(), &options, &request->run_result, &request->diag);
        return;
    }

    if (request->operation == OsV8Operation::Start) {
        SlOsProcessStartOptions options = {
            .cwd = request->cwd.empty()
                       ? sl_str_empty()
                       : sl_str_from_parts(request->cwd.data(), request->cwd.size()),
            .environment_overrides = native_env.empty() ? nullptr : native_env.data(),
            .environment_override_count = native_env.size(),
            .stdin_mode = request->stdin_mode,
            .stdout_mode = request->stdout_mode,
            .stderr_mode = request->stderr_mode};
        if (request->process == nullptr || !sl_status_is_ok(request->process->arena_status)) {
            request->diag =
                os_v8_diag(SL_DIAG_OS_PROCESS_START_FAILED, "process handle arena unavailable");
            request->status = sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
            return;
        }
        std::lock_guard<std::mutex> lock(request->process->mutex);
        request->status = sl_os_process_start(
            &request->process->arena, &policy,
            sl_str_from_parts(request->command.data(), request->command.size()),
            native_args.empty() ? nullptr : native_args.data(), native_args.size(), &options,
            &request->process->handle, &request->diag);
        return;
    }

    if (request->process == nullptr) {
        request->diag = os_v8_diag(SL_DIAG_OS_PIPE_CLOSED, "process handle is closed");
        request->status = sl_status_from_code(SL_STATUS_INVALID_STATE);
        return;
    }

    std::lock_guard<std::mutex> lock(request->process->mutex);
    if (request->process->disposed || request->process->handle == nullptr) {
        request->diag = os_v8_diag(SL_DIAG_OS_PIPE_CLOSED, "process handle is closed");
        request->status = sl_status_from_code(SL_STATUS_INVALID_STATE);
        return;
    }

    switch (request->operation) {
    case OsV8Operation::Wait: {
        SlOsProcessWaitOptions options = {.timeout_ms = request->timeout_ms};
        request->status =
            sl_os_process_wait(request->process->handle, &options, &request->exit, &request->diag);
        break;
    }
    case OsV8Operation::ReadStdout:
        request->status =
            sl_os_process_stdout_read(&request->arena, request->process->handle, request->max_bytes,
                                      &request->pipe_read, &request->diag);
        break;
    case OsV8Operation::ReadStderr:
        request->status =
            sl_os_process_stderr_read(&request->arena, request->process->handle, request->max_bytes,
                                      &request->pipe_read, &request->diag);
        break;
    case OsV8Operation::WriteStdin:
        request->status = sl_os_process_stdin_write(
            request->process->handle,
            sl_str_from_parts(reinterpret_cast<const char*>(request->input.data()),
                              request->input.size()),
            &request->written, &request->diag);
        break;
    case OsV8Operation::CloseStdin:
        request->status = sl_os_process_stdin_close(request->process->handle, &request->diag);
        break;
    case OsV8Operation::Terminate:
        request->status = sl_os_process_terminate(request->process->handle, &request->diag);
        break;
    case OsV8Operation::Kill:
        request->status = sl_os_process_kill(request->process->handle, &request->diag);
        break;
    case OsV8Operation::Cancel:
        request->status = sl_os_process_cancel(request->process->handle, &request->diag);
        break;
    case OsV8Operation::Dispose:
        sl_os_process_dispose(request->process->handle);
        request->process->handle = nullptr;
        request->process->disposed = true;
        request->status = sl_status_ok();
        break;
    default:
        break;
    }
}

void os_v8_worker(std::shared_ptr<SlV8OsRequest> request)
{
    try {
        os_v8_execute_request(request);
    } catch (...) {
        if (request != nullptr) {
            request->diag = os_v8_diag(SL_DIAG_OS_PROCESS_START_FAILED, "OS worker failed");
            request->status = sl_status_from_code(SL_STATUS_INTERNAL);
        }
    }
    (void)os_v8_post_completion(request);
}

bool os_v8_submit_request(v8::Isolate* isolate, v8::Local<v8::Context> context,
                          v8::Local<v8::Promise::Resolver> resolver,
                          const std::shared_ptr<SlV8OsRequest>& request)
{
    SlV8Engine* backend = request == nullptr ? nullptr : request->backend;
    if (backend == nullptr || backend->async_loop == nullptr) {
        return os_v8_reject_promise(isolate, context, resolver,
                                    "SLOPPY_E_OS_FEATURE_UNAVAILABLE: OS async lane is "
                                    "unavailable",
                                    false);
    }

    {
        std::lock_guard<std::mutex> lock(backend->os_mutex);
        if (backend->os_shutting_down) {
            return os_v8_reject_promise(isolate, context, resolver,
                                        "SLOPPY_E_OS_FEATURE_UNAVAILABLE: OS bridge is "
                                        "shutting down",
                                        false);
        }
        backend->os_requests.push_back(request);
    }

    try {
        request->worker = std::thread(os_v8_worker, request);
    } catch (...) {
        os_v8_remove_request(request);
        return os_v8_reject_promise(
            isolate, context, resolver,
            "SLOPPY_E_OS_PROCESS_START_FAILED: OS worker thread could not start", false);
    }
    return true;
}

bool os_v8_parse_run_options(v8::Isolate* isolate, v8::Local<v8::Context> context,
                             v8::Local<v8::Value> value, SlV8OsRequest* request)
{
    if (request == nullptr || value.IsEmpty() || !value->IsObject() || value->IsArray()) {
        return false;
    }
    v8::Local<v8::Object> options = value.As<v8::Object>();
    v8::Local<v8::Value> item;
    if (os_v8_get_property(isolate, context, options, "cwd", &item) && !item->IsUndefined() &&
        !os_v8_value_to_std_string(isolate, item, &request->cwd))
    {
        return false;
    }
    if (!request->cwd.empty() && !os_v8_string_valid(request->cwd)) {
        return false;
    }
    if (os_v8_get_property(isolate, context, options, "env", &item) &&
        !os_v8_parse_env(isolate, context, item, &request->env))
    {
        return false;
    }
    if (os_v8_get_property(isolate, context, options, "capture", &item) &&
        !os_v8_parse_capture(isolate, item, &request->capture))
    {
        return false;
    }
    if (os_v8_get_property(isolate, context, options, "maxStdoutBytes", &item) &&
        !item->IsUndefined() &&
        !os_v8_number_to_size(context, item, 0U, kOsV8MaxCaptureBytes, &request->max_stdout_bytes))
    {
        return false;
    }
    if (os_v8_get_property(isolate, context, options, "maxStderrBytes", &item) &&
        !item->IsUndefined() &&
        !os_v8_number_to_size(context, item, 0U, kOsV8MaxCaptureBytes, &request->max_stderr_bytes))
    {
        return false;
    }
    if (os_v8_get_property(isolate, context, options, "timeoutMs", &item) && !item->IsUndefined() &&
        !os_v8_number_to_u64(context, item, UINT32_MAX, &request->timeout_ms))
    {
        return false;
    }
    return true;
}

bool os_v8_parse_start_options(v8::Isolate* isolate, v8::Local<v8::Context> context,
                               v8::Local<v8::Value> value, SlV8OsRequest* request)
{
    if (request == nullptr || value.IsEmpty() || !value->IsObject() || value->IsArray()) {
        return false;
    }
    v8::Local<v8::Object> options = value.As<v8::Object>();
    v8::Local<v8::Value> item;
    if (os_v8_get_property(isolate, context, options, "cwd", &item) && !item->IsUndefined() &&
        !os_v8_value_to_std_string(isolate, item, &request->cwd))
    {
        return false;
    }
    if (!request->cwd.empty() && !os_v8_string_valid(request->cwd)) {
        return false;
    }
    if (os_v8_get_property(isolate, context, options, "env", &item) &&
        !os_v8_parse_env(isolate, context, item, &request->env))
    {
        return false;
    }
    if (os_v8_get_property(isolate, context, options, "stdin", &item) &&
        !os_v8_parse_pipe_mode(isolate, item, &request->stdin_mode))
    {
        return false;
    }
    if (os_v8_get_property(isolate, context, options, "stdout", &item) &&
        !os_v8_parse_pipe_mode(isolate, item, &request->stdout_mode))
    {
        return false;
    }
    if (os_v8_get_property(isolate, context, options, "stderr", &item) &&
        !os_v8_parse_pipe_mode(isolate, item, &request->stderr_mode))
    {
        return false;
    }
    return true;
}

void os_v8_process_run_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Promise::Resolver> resolver;

    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
        os_v8_throw(isolate, "SLOPPY_E_OS_FEATURE_UNAVAILABLE: could not create Promise");
        return;
    }
    args.GetReturnValue().Set(resolver->GetPromise());

    std::shared_ptr<SlV8OsRequest> request(new (std::nothrow) SlV8OsRequest());
    if (request == nullptr || args.Length() != 3 ||
        !os_v8_string_arg(isolate, args[0], &request->command) ||
        !os_v8_array_to_strings(isolate, context, args[1], &request->args) ||
        !os_v8_parse_run_options(isolate, context, args[2], request.get()) ||
        !os_v8_request_arena_init(request, os_v8_process_run_arena_bytes(*request)))
    {
        (void)os_v8_reject_promise(isolate, context, resolver,
                                   "SLOPPY_E_INVALID_ARGUMENT: Process.run arguments invalid",
                                   true);
        return;
    }
    request->backend = backend;
    request->operation = OsV8Operation::Run;
    request->resolver.Reset(isolate, resolver);
    (void)os_v8_submit_request(isolate, context, resolver, request);
}

void os_v8_process_start_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Promise::Resolver> resolver;

    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
        os_v8_throw(isolate, "SLOPPY_E_OS_FEATURE_UNAVAILABLE: could not create Promise");
        return;
    }
    args.GetReturnValue().Set(resolver->GetPromise());

    std::shared_ptr<SlV8OsRequest> request(new (std::nothrow) SlV8OsRequest());
    if (request == nullptr || args.Length() != 3 ||
        !os_v8_string_arg(isolate, args[0], &request->command) ||
        !os_v8_array_to_strings(isolate, context, args[1], &request->args) ||
        !os_v8_parse_start_options(isolate, context, args[2], request.get()))
    {
        (void)os_v8_reject_promise(isolate, context, resolver,
                                   "SLOPPY_E_INVALID_ARGUMENT: Process.start arguments invalid",
                                   true);
        return;
    }
    request->process = os_v8_process_create();
    if (request->process == nullptr) {
        (void)os_v8_reject_promise(isolate, context, resolver,
                                   "SLOPPY_E_OS_PROCESS_START_FAILED: process handle allocation "
                                   "failed",
                                   false);
        return;
    }
    request->backend = backend;
    request->operation = OsV8Operation::Start;
    request->resolver.Reset(isolate, resolver);
    (void)os_v8_submit_request(isolate, context, resolver, request);
}

void os_v8_process_operation_callback(const v8::FunctionCallbackInfo<v8::Value>& args,
                                      OsV8Operation operation)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Promise::Resolver> resolver;
    SlResourceId id = {};
    std::shared_ptr<OsV8Process> process;

    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
        os_v8_throw(isolate, "SLOPPY_E_OS_FEATURE_UNAVAILABLE: could not create Promise");
        return;
    }
    args.GetReturnValue().Set(resolver->GetPromise());

    if (args.Length() < 1 || !os_v8_handle_arg(isolate, context, args[0], &id)) {
        (void)os_v8_reject_promise(isolate, context, resolver,
                                   "SLOPPY_E_INVALID_ARGUMENT: process handle invalid", true);
        return;
    }
    if (!os_v8_lookup_process(backend, id, &process)) {
        if (operation == OsV8Operation::Dispose) {
            (void)resolver->Resolve(context, v8::Undefined(isolate)).FromMaybe(false);
            isolate->PerformMicrotaskCheckpoint();
        }
        else {
            (void)os_v8_reject_promise(isolate, context, resolver,
                                       "SLOPPY_E_OS_PIPE_CLOSED: process handle is closed", false);
        }
        return;
    }

    std::shared_ptr<SlV8OsRequest> request(new (std::nothrow) SlV8OsRequest());
    if (request == nullptr) {
        (void)os_v8_reject_promise(isolate, context, resolver,
                                   "SLOPPY_E_OS_FEATURE_UNAVAILABLE: request allocation failed",
                                   false);
        return;
    }
    request->backend = backend;
    request->operation = operation;
    request->process = process;
    request->resource_id = id;
    request->resolver.Reset(isolate, resolver);

    if (operation == OsV8Operation::Wait) {
        v8::Local<v8::Value> timeout_value;
        request->timeout_ms = 0U;
        if (args.Length() != 2 || !args[1]->IsObject() ||
            !os_v8_get_property(isolate, context, args[1].As<v8::Object>(), "timeoutMs",
                                &timeout_value) ||
            !os_v8_number_to_u64(context, timeout_value, UINT32_MAX, &request->timeout_ms))
        {
            (void)os_v8_reject_promise(isolate, context, resolver,
                                       "SLOPPY_E_INVALID_ARGUMENT: ProcessHandle.wait arguments "
                                       "invalid",
                                       true);
            return;
        }
    }
    else if (operation == OsV8Operation::ReadStdout || operation == OsV8Operation::ReadStderr) {
        if (args.Length() != 2 ||
            !os_v8_number_to_size(context, args[1], 1U, kOsV8MaxPipeReadBytes,
                                  &request->max_bytes) ||
            !os_v8_request_arena_init(request, request->max_bytes + 1024U))
        {
            (void)os_v8_reject_promise(isolate, context, resolver,
                                       "SLOPPY_E_INVALID_ARGUMENT: process pipe read arguments "
                                       "invalid",
                                       true);
            return;
        }
    }
    else if (operation == OsV8Operation::WriteStdin) {
        if (args.Length() != 2 || !os_v8_bytes_arg(isolate, args[1], &request->input)) {
            (void)os_v8_reject_promise(isolate, context, resolver,
                                       "SLOPPY_E_INVALID_ARGUMENT: process stdin write arguments "
                                       "invalid",
                                       true);
            return;
        }
    }
    else if (!os_v8_request_arena_init(request, 4096U)) {
        (void)os_v8_reject_promise(isolate, context, resolver,
                                   "SLOPPY_E_OS_FEATURE_UNAVAILABLE: request arena unavailable",
                                   false);
        return;
    }

    (void)os_v8_submit_request(isolate, context, resolver, request);
}

void os_v8_process_wait_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    os_v8_process_operation_callback(args, OsV8Operation::Wait);
}

void os_v8_process_read_stdout_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    os_v8_process_operation_callback(args, OsV8Operation::ReadStdout);
}

void os_v8_process_read_stderr_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    os_v8_process_operation_callback(args, OsV8Operation::ReadStderr);
}

void os_v8_process_write_stdin_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    os_v8_process_operation_callback(args, OsV8Operation::WriteStdin);
}

void os_v8_process_close_stdin_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    os_v8_process_operation_callback(args, OsV8Operation::CloseStdin);
}

void os_v8_process_terminate_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    os_v8_process_operation_callback(args, OsV8Operation::Terminate);
}

void os_v8_process_kill_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    os_v8_process_operation_callback(args, OsV8Operation::Kill);
}

void os_v8_process_cancel_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    os_v8_process_operation_callback(args, OsV8Operation::Cancel);
}

void os_v8_process_dispose_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    os_v8_process_operation_callback(args, OsV8Operation::Dispose);
}

void os_v8_noop_dispose_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    args.GetReturnValue().Set(v8::Undefined(args.GetIsolate()));
}

void os_v8_signals_on_shutdown_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Object> registration = v8::Object::New(isolate);

    if (args.Length() != 1 || !args[0]->IsFunction()) {
        os_v8_throw_type_error(isolate, "Signals.onShutdown requires a function.");
        return;
    }
    if (!os_v8_set_function(isolate, context, registration, "dispose", os_v8_noop_dispose_callback))
    {
        os_v8_throw(isolate, "SLOPPY_E_OS_FEATURE_UNAVAILABLE: signal registration failed");
        return;
    }
    args.GetReturnValue().Set(registration);
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
        !os_v8_set_function(isolate, context, os, "environmentList", os_v8_env_list_callback) ||
        !os_v8_set_function(isolate, context, os, "processRun", os_v8_process_run_callback) ||
        !os_v8_set_function(isolate, context, os, "processStart", os_v8_process_start_callback) ||
        !os_v8_set_function(isolate, context, os, "processWait", os_v8_process_wait_callback) ||
        !os_v8_set_function(isolate, context, os, "processReadStdout",
                            os_v8_process_read_stdout_callback) ||
        !os_v8_set_function(isolate, context, os, "processReadStderr",
                            os_v8_process_read_stderr_callback) ||
        !os_v8_set_function(isolate, context, os, "processWriteStdin",
                            os_v8_process_write_stdin_callback) ||
        !os_v8_set_function(isolate, context, os, "processCloseStdin",
                            os_v8_process_close_stdin_callback) ||
        !os_v8_set_function(isolate, context, os, "processTerminate",
                            os_v8_process_terminate_callback) ||
        !os_v8_set_function(isolate, context, os, "processKill", os_v8_process_kill_callback) ||
        !os_v8_set_function(isolate, context, os, "processCancel", os_v8_process_cancel_callback) ||
        !os_v8_set_function(isolate, context, os, "processDispose",
                            os_v8_process_dispose_callback) ||
        !os_v8_set_function(isolate, context, os, "signalsOnShutdown",
                            os_v8_signals_on_shutdown_callback))
    {
        return false;
    }
    return sloppy->Set(context, os_key, os).FromMaybe(false);
}

void sl_v8_os_dispose(SlV8Engine* backend)
{
    std::vector<std::shared_ptr<SlV8OsRequest>> requests;

    if (backend == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(backend->os_mutex);
        backend->os_shutting_down = true;
        requests = backend->os_requests;
        backend->os_requests.clear();
    }
    for (const std::shared_ptr<SlV8OsRequest>& request : requests) {
        if (request == nullptr) {
            continue;
        }
        request->cancelled.store(true);
        if (request->worker.joinable() && request->worker.get_id() != std::this_thread::get_id()) {
            request->worker.join();
        }
        request->resolver.Reset();
        request->completion_posted.store(false);
    }
}

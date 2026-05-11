/*
 * src/engine/v8/intrinsics_workers.cc
 *
 * Installs the feature-gated workers namespace under __sloppy.workers. Native worker
 * execution never enters the app's owning isolate from a worker thread. Inputs, source,
 * and outputs are copied through JSON text, work runs in a separate V8 isolate owned by
 * the worker thread, and Promise settlement is posted back to the owning isolate through
 * SlAsyncLoop.
 */
#include "engine_v8_internal.h"
#include "string_interop.h"

#include "sloppy/fs.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <regex>
#include <string>
#include <thread>
#include <vector>

constexpr size_t kWorkersV8MaxPayloadBytes = 1024U * 1024U;
constexpr size_t kWorkersV8MaxResultBytes = 1024U * 1024U;
constexpr size_t kWorkersV8MaxModuleBytes = 1024U * 1024U;
constexpr size_t kWorkersV8PerWorkerQueueCapacity = 64U;
constexpr size_t kWorkersV8MicrotaskLimit = 1024U;
constexpr size_t kWorkersV8MinMemoryLimitMb = 16U;
constexpr size_t kWorkersV8BytesPerMb = 1024U * 1024U;

enum class WorkersV8RequestKind
{
    PoolRun,
    JsInvoke,
};

struct SlV8WorkerRequest;

struct SlV8JsWorker
{
    SlV8Engine* backend = nullptr;
    uint32_t id = 0U;
    std::string module_path;
    std::string module_source;
    size_t memory_limit_mb = 128U;
    bool stopped = false;
    size_t active_count = 0U;
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<std::shared_ptr<SlV8WorkerRequest>> queue;
    std::thread thread;
    bool stopping = false;
    bool startup_complete = false;
    bool startup_ok = false;
    std::string startup_error_code;
    std::string startup_error_message;
};

struct SlV8WorkerRequest
{
    SlV8Engine* backend = nullptr;
    WorkersV8RequestKind kind = WorkersV8RequestKind::PoolRun;
    v8::Global<v8::Promise::Resolver> resolver;
    std::weak_ptr<SlV8JsWorker> js_worker;
    std::string pool_name;
    std::string function_source;
    std::string module_path;
    std::string module_source;
    std::string export_name;
    std::string input_json;
    std::string result_json;
    std::string error_code;
    std::string error_message;
    size_t memory_limit_mb = 128U;
    bool result_is_undefined = false;
    bool reject = false;
    std::atomic_bool completion_posted = false;
    std::atomic_bool cleanup_ran = false;
    std::thread thread;
};

struct WorkersV8CompletionPayload
{
    std::shared_ptr<SlV8WorkerRequest> request;
};

struct WorkersV8ExecutionResult
{
    bool ok = false;
    bool undefined_result = false;
    std::string json;
    std::string code;
    std::string message;
};

bool workers_v8_to_local_string(SlV8Engine* backend, const char* value, v8::Local<v8::String>* out)
{
    return sl_status_is_ok(sl_v8_string_from_native_view(backend, sl_str_from_cstr(value), out));
}

bool workers_v8_set_string(SlV8Engine* backend, v8::Local<v8::Context> context,
                           v8::Local<v8::Object> object, const char* key, const char* value)
{
    v8::Local<v8::String> local_key;
    v8::Local<v8::String> local_value;
    if (!workers_v8_to_local_string(backend, key, &local_key) ||
        !workers_v8_to_local_string(backend, value, &local_value))
    {
        return false;
    }
    return object->Set(context, local_key, local_value).FromMaybe(false);
}

bool workers_v8_set_bool(SlV8Engine* backend, v8::Local<v8::Context> context,
                         v8::Local<v8::Object> object, const char* key, bool value)
{
    v8::Local<v8::String> local_key;
    if (!workers_v8_to_local_string(backend, key, &local_key)) {
        return false;
    }
    return object->Set(context, local_key, v8::Boolean::New(backend->isolate, value))
        .FromMaybe(false);
}

bool workers_v8_set_function(SlV8Engine* backend, v8::Local<v8::Context> context,
                             v8::Local<v8::Object> object, const char* key,
                             v8::FunctionCallback callback)
{
    v8::Local<v8::String> local_key;
    v8::Local<v8::FunctionTemplate> function_template;
    v8::Local<v8::Function> function;

    if (!workers_v8_to_local_string(backend, key, &local_key)) {
        return false;
    }
    function_template = v8::FunctionTemplate::New(backend->isolate, callback);
    if (!function_template->GetFunction(context).ToLocal(&function)) {
        return false;
    }
    return object->Set(context, local_key, function).FromMaybe(false);
}

v8::Local<v8::String> workers_v8_literal(v8::Isolate* isolate, const char* value)
{
    return v8::String::NewFromUtf8(isolate, value, v8::NewStringType::kNormal).ToLocalChecked();
}

bool workers_v8_std_string_from_value(v8::Isolate* isolate, v8::Local<v8::Value> value,
                                      std::string* out)
{
    return sl_v8_std_string_from_value(isolate, value, out);
}

bool workers_v8_compile_run(v8::Isolate* isolate, v8::Local<v8::Context> context,
                            const std::string& source_name, const std::string& source,
                            v8::Local<v8::Value>* out);

const char* workers_v8_payload_codec_source()
{
    return R"JS(
(function () {
  if (typeof globalThis.__sloppyWorkerEncodePayload === "function") {
    return;
  }
  const marker = "__sloppyWorkerSerialized";
  const encode = (value, seen = new Set()) => {
    if (value === null || typeof value !== "object") {
      return value;
    }
    if (value instanceof ArrayBuffer) {
      return { [marker]: "arrayBuffer-v1", bytes: Array.from(new Uint8Array(value)) };
    }
    if (ArrayBuffer.isView(value)) {
      return {
        [marker]: "uint8Array-v1",
        bytes: Array.from(new Uint8Array(value.buffer, value.byteOffset, value.byteLength)),
      };
    }
    if (seen.has(value)) {
      throw new TypeError("worker payload contains a cycle");
    }
    seen.add(value);
    if (Array.isArray(value)) {
      const array = value.map((item) => encode(item, seen));
      seen.delete(value);
      return array;
    }
    const prototype = Object.getPrototypeOf(value);
    if (prototype === Object.prototype || prototype === null) {
      if (Object.prototype.hasOwnProperty.call(value, marker)) {
        throw new TypeError("worker payload uses a reserved serialization marker");
      }
      const object = {};
      for (const key of Object.keys(value)) {
        if (value[key] !== undefined) {
          object[key] = encode(value[key], seen);
        }
      }
      seen.delete(value);
      return object;
    }
    throw new TypeError("worker payload type is unsupported");
  };
  const decode = (value) => {
    if (value === null || typeof value !== "object") {
      return value;
    }
    if (value[marker] === "arrayBuffer-v1" || value[marker] === "uint8Array-v1") {
      if (!Array.isArray(value.bytes)) {
        throw new TypeError("worker byte payload is invalid");
      }
      const bytes = new Uint8Array(value.bytes.length);
      for (let index = 0; index < value.bytes.length; index += 1) {
        const byte = value.bytes[index];
        if (!Number.isInteger(byte) || byte < 0 || byte > 255) {
          throw new TypeError("worker byte payload is invalid");
        }
        bytes[index] = byte;
      }
      return value[marker] === "arrayBuffer-v1" ? bytes.buffer : bytes;
    }
    if (Array.isArray(value)) {
      return value.map((item) => decode(item));
    }
    for (const key of Object.keys(value)) {
      value[key] = decode(value[key]);
    }
    return value;
  };
  globalThis.__sloppyWorkerEncodePayload = encode;
  globalThis.__sloppyWorkerDecodePayload = decode;
})();
)JS";
}

bool workers_v8_ensure_payload_codec(v8::Isolate* isolate, v8::Local<v8::Context> context)
{
    v8::Local<v8::Value> encode_value;
    v8::Local<v8::Value> decode_value;
    v8::Local<v8::Value> ignored;
    if (isolate == nullptr || context.IsEmpty()) {
        return false;
    }
    if (context->Global()
            ->Get(context, workers_v8_literal(isolate, "__sloppyWorkerEncodePayload"))
            .ToLocal(&encode_value) &&
        context->Global()
            ->Get(context, workers_v8_literal(isolate, "__sloppyWorkerDecodePayload"))
            .ToLocal(&decode_value) &&
        encode_value->IsFunction() && decode_value->IsFunction())
    {
        return true;
    }
    return workers_v8_compile_run(isolate, context, "sloppy-worker-payload-codec.js",
                                  workers_v8_payload_codec_source(), &ignored);
}

bool workers_v8_apply_payload_codec(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                    const char* function_name, v8::Local<v8::Value> value,
                                    v8::Local<v8::Value>* out)
{
    v8::Local<v8::Value> function_value;
    v8::Local<v8::Value> argv[] = {value};

    if (out == nullptr || !workers_v8_ensure_payload_codec(isolate, context) ||
        !context->Global()
             ->Get(context, workers_v8_literal(isolate, function_name))
             .ToLocal(&function_value) ||
        !function_value->IsFunction())
    {
        return false;
    }
    return function_value.As<v8::Function>()
        ->Call(context, context->Global(), 1, argv)
        .ToLocal(out);
}

bool workers_v8_json_stringify(v8::Isolate* isolate, v8::Local<v8::Context> context,
                               v8::Local<v8::Value> value, std::string* out, bool* out_undefined)
{
    v8::Local<v8::Value> json_value;
    v8::Local<v8::Value> encoded_value;

    if (out == nullptr || out_undefined == nullptr) {
        return false;
    }
    *out = std::string();
    *out_undefined = false;

    if (!workers_v8_apply_payload_codec(isolate, context, "__sloppyWorkerEncodePayload", value,
                                        &encoded_value) ||
        !v8::JSON::Stringify(context, encoded_value).ToLocal(&json_value))
    {
        return false;
    }
    if (json_value->IsUndefined()) {
        *out_undefined = true;
        return true;
    }
    return workers_v8_std_string_from_value(isolate, json_value, out);
}

bool workers_v8_json_parse(v8::Isolate* isolate, v8::Local<v8::Context> context,
                           const std::string& json, v8::Local<v8::Value>* out)
{
    v8::MaybeLocal<v8::String> maybe_json = v8::String::NewFromUtf8(
        isolate, json.c_str(), v8::NewStringType::kNormal, static_cast<int>(json.size()));
    v8::Local<v8::String> local_json;
    v8::Local<v8::Value> parsed;

    if (json.size() > static_cast<size_t>(std::numeric_limits<int>::max()) ||
        !maybe_json.ToLocal(&local_json))
    {
        return false;
    }
    if (!v8::JSON::Parse(context, local_json).ToLocal(&parsed)) {
        return false;
    }
    return workers_v8_apply_payload_codec(isolate, context, "__sloppyWorkerDecodePayload", parsed,
                                          out);
}

void workers_v8_set_error_code(v8::Isolate* isolate, v8::Local<v8::Context> context,
                               v8::Local<v8::Value> error, const char* code)
{
    if (error.IsEmpty() || !error->IsObject()) {
        return;
    }
    if (!error.As<v8::Object>()
             ->Set(context, workers_v8_literal(isolate, "code"), workers_v8_literal(isolate, code))
             .FromMaybe(false))
    {
        return;
    }
}

void workers_v8_throw_code_error(SlV8Engine* backend, const char* code, const char* message)
{
    v8::Isolate* isolate = backend == nullptr ? nullptr : backend->isolate;
    if (backend == nullptr || isolate == nullptr) {
        return;
    }
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::String> local_message;
    if (!workers_v8_to_local_string(backend, message, &local_message)) {
        local_message = v8::String::NewFromUtf8Literal(isolate, "Sloppy workers error");
    }
    v8::Local<v8::Value> error = v8::Exception::Error(local_message);
    workers_v8_set_error_code(isolate, context, error, code);
    isolate->ThrowException(error);
}

bool workers_v8_reject_code_error(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                  v8::Local<v8::Promise::Resolver> resolver,
                                  const std::string& code, const std::string& message)
{
    const char* fallback_code = code.empty() ? "SLOPPY_E_WORKER_CRASHED" : code.c_str();
    const char* fallback_message = message.empty() ? "worker operation failed" : message.c_str();
    v8::Local<v8::String> local_message =
        v8::String::NewFromUtf8(isolate, fallback_message, v8::NewStringType::kNormal)
            .ToLocalChecked();
    v8::Local<v8::Value> error = v8::Exception::Error(local_message);
    workers_v8_set_error_code(isolate, context, error, fallback_code);
    return resolver->Reject(context, error).FromMaybe(false);
}

std::string workers_v8_transform_module_source(const std::string& source)
{
    std::string transformed = source;
    transformed = std::regex_replace(
        transformed, std::regex(R"(\bexport\s+async\s+function\s+([A-Za-z_$][A-Za-z0-9_$]*)\s*\()"),
        "globalThis.__sloppyWorkerExports.$1 = async function $1(");
    transformed = std::regex_replace(
        transformed, std::regex(R"(\bexport\s+function\s+([A-Za-z_$][A-Za-z0-9_$]*)\s*\()"),
        "globalThis.__sloppyWorkerExports.$1 = function $1(");
    transformed = std::regex_replace(
        transformed, std::regex(R"(\bexport\s+const\s+([A-Za-z_$][A-Za-z0-9_$]*)\s*=)"),
        "globalThis.__sloppyWorkerExports.$1 =");
    return "globalThis.__sloppyWorkerExports = Object.create(null);\n" + transformed;
}

bool workers_v8_validate_module_export_syntax(const std::string& source, std::string* out_message)
{
    struct UnsupportedExport
    {
        const char* pattern;
        const char* syntax;
    };
    static const UnsupportedExport unsupported[] = {
        {R"(\bexport\s+default\b)", "export default"},
        {R"(\bexport\s*\{)", "export list/re-export"},
        {R"(\bexport\s*\*)", "export star/re-export"},
        {R"(\bexport\s+class\b)", "export class"},
        {R"(\bexport\s+let\b)", "export let"},
        {R"(\bexport\s+var\b)", "export var"},
    };

    for (const UnsupportedExport& item : unsupported) {
        if (std::regex_search(source, std::regex(item.pattern))) {
            if (out_message != nullptr) {
                *out_message = std::string("worker module export syntax is unsupported: ") +
                               item.syntax +
                               "; supported exports are named export function, export async "
                               "function, and export const declarations";
            }
            return false;
        }
    }
    return true;
}

void workers_v8_apply_memory_limit(v8::Isolate::CreateParams* create_params, size_t memory_limit_mb)
{
    if (create_params == nullptr || memory_limit_mb == 0U ||
        memory_limit_mb > std::numeric_limits<size_t>::max() / kWorkersV8BytesPerMb)
    {
        return;
    }
    create_params->constraints.ConfigureDefaultsFromHeapSize(0U, memory_limit_mb *
                                                                     kWorkersV8BytesPerMb);
}

bool workers_v8_compile_run(v8::Isolate* isolate, v8::Local<v8::Context> context,
                            const std::string& source_name, const std::string& source,
                            v8::Local<v8::Value>* out)
{
    if (source.size() > static_cast<size_t>(std::numeric_limits<int>::max()) ||
        source_name.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        return false;
    }

    v8::Local<v8::String> local_source =
        v8::String::NewFromUtf8(isolate, source.c_str(), v8::NewStringType::kNormal,
                                static_cast<int>(source.size()))
            .ToLocalChecked();
    v8::Local<v8::String> local_name =
        v8::String::NewFromUtf8(isolate, source_name.c_str(), v8::NewStringType::kNormal,
                                static_cast<int>(source_name.size()))
            .ToLocalChecked();
    v8::ScriptOrigin origin(local_name);
    v8::ScriptCompiler::Source script_source(local_source, origin);
    v8::Local<v8::Script> script;
    if (!v8::ScriptCompiler::Compile(context, &script_source).ToLocal(&script)) {
        return false;
    }
    return script->Run(context).ToLocal(out);
}

WorkersV8ExecutionResult workers_v8_execute_in_isolate(const SlV8WorkerRequest& request)
{
    WorkersV8ExecutionResult result = {};
    v8::ArrayBuffer::Allocator* allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();

    if (allocator == nullptr) {
        result.code = "SLOPPY_E_WORKER_POOL_UNAVAILABLE";
        result.message = "worker isolate allocator unavailable";
        return result;
    }

    v8::Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = allocator;
    workers_v8_apply_memory_limit(&create_params, request.memory_limit_mb);
    v8::Isolate* isolate = v8::Isolate::New(create_params);
    if (isolate == nullptr) {
        delete allocator;
        result.code = "SLOPPY_E_WORKER_ISOLATE_STARTUP_FAILED";
        result.message = "worker isolate startup failed";
        return result;
    }

    {
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        isolate->SetMicrotasksPolicy(v8::MicrotasksPolicy::kExplicit);
        v8::Local<v8::Context> context = v8::Context::New(isolate);
        v8::Context::Scope context_scope(context);
        v8::TryCatch try_catch(isolate);
        v8::Local<v8::Value> ignored;
        v8::Local<v8::Value> input_value;
        v8::Local<v8::Object> ctx = v8::Object::New(isolate);
        v8::Local<v8::Object> signal = v8::Object::New(isolate);
        v8::Local<v8::Function> fn;
        v8::Local<v8::Value> call_result;
        bool undefined_json = false;

        if ((request.module_source.size() + request.function_source.size() +
             request.input_json.size()) > kWorkersV8MaxPayloadBytes + kWorkersV8MaxModuleBytes)
        {
            result.code = "SLOPPY_E_WORKER_RESOURCE_LIMIT_EXCEEDED";
            result.message = "worker resource limit exceeded";
            goto workers_v8_execute_done;
        }

        if (!workers_v8_json_parse(isolate, context, request.input_json, &input_value)) {
            result.code = "SLOPPY_E_WORKER_MESSAGE_SERIALIZATION_FAILED";
            result.message = "worker message serialization failed";
            goto workers_v8_execute_done;
        }

        if (!signal
                 ->Set(context, workers_v8_literal(isolate, "cancelled"),
                       v8::Boolean::New(isolate, false))
                 .FromMaybe(false) ||
            !signal
                 ->Set(context, workers_v8_literal(isolate, "aborted"),
                       v8::Boolean::New(isolate, false))
                 .FromMaybe(false) ||
            !ctx->Set(context, workers_v8_literal(isolate, "input"), input_value)
                 .FromMaybe(false) ||
            !ctx->Set(context, workers_v8_literal(isolate, "signal"), signal).FromMaybe(false))
        {
            result.code = "SLOPPY_E_WORKER_CRASHED";
            result.message = "worker context creation failed";
            goto workers_v8_execute_done;
        }

        if (request.kind == WorkersV8RequestKind::PoolRun) {
            std::string pool_source =
                "globalThis.__sloppyWorkerPoolFn = (" + request.function_source + ");";
            if (!workers_v8_compile_run(isolate, context, "sloppy-worker-pool.js", pool_source,
                                        &ignored))
            {
                result.code = "SLOPPY_E_WORKER_CRASHED";
                result.message = "worker pool function failed to compile";
                goto workers_v8_execute_done;
            }
            v8::Local<v8::Value> value;
            if (!context->Global()
                     ->Get(context, workers_v8_literal(isolate, "__sloppyWorkerPoolFn"))
                     .ToLocal(&value) ||
                !value->IsFunction())
            {
                result.code = "SLOPPY_E_WORKER_CRASHED";
                result.message = "worker pool function is not callable";
                goto workers_v8_execute_done;
            }
            fn = value.As<v8::Function>();
            v8::Local<v8::Value> argv[] = {ctx};
            if (!fn->Call(context, context->Global(), 1, argv).ToLocal(&call_result)) {
                result.code = "SLOPPY_E_WORKER_CRASHED";
                result.message = "worker pool function failed";
                goto workers_v8_execute_done;
            }
        }
        else {
            std::string transformed;
            std::string syntax_error;
            if (!workers_v8_validate_module_export_syntax(request.module_source, &syntax_error)) {
                result.code = "SLOPPY_E_WORKER_ISOLATE_STARTUP_FAILED";
                result.message = std::move(syntax_error);
                goto workers_v8_execute_done;
            }
            try {
                transformed = workers_v8_transform_module_source(request.module_source);
            } catch (...) {
                result.code = "SLOPPY_E_WORKER_ISOLATE_STARTUP_FAILED";
                result.message = "worker module transform failed";
                goto workers_v8_execute_done;
            }
            if (!workers_v8_compile_run(isolate, context, request.module_path, transformed,
                                        &ignored))
            {
                result.code = "SLOPPY_E_WORKER_ISOLATE_STARTUP_FAILED";
                result.message = "worker isolate startup failed";
                goto workers_v8_execute_done;
            }
            v8::Local<v8::Value> exports_value;
            v8::Local<v8::Value> fn_value;
            if (!context->Global()
                     ->Get(context, workers_v8_literal(isolate, "__sloppyWorkerExports"))
                     .ToLocal(&exports_value) ||
                !exports_value->IsObject() ||
                !exports_value.As<v8::Object>()
                     ->Get(context,
                           v8::String::NewFromUtf8(isolate, request.export_name.c_str(),
                                                   v8::NewStringType::kNormal,
                                                   static_cast<int>(request.export_name.size()))
                               .ToLocalChecked())
                     .ToLocal(&fn_value) ||
                !fn_value->IsFunction())
            {
                result.code = "SLOPPY_E_WORKER_CRASHED";
                result.message = "worker export is not callable";
                goto workers_v8_execute_done;
            }
            fn = fn_value.As<v8::Function>();
            v8::Local<v8::Value> argv[] = {input_value, ctx};
            if (!fn->Call(context, context->Global(), 2, argv).ToLocal(&call_result)) {
                result.code = "SLOPPY_E_WORKER_CRASHED";
                result.message = "worker crashed";
                goto workers_v8_execute_done;
            }
        }

        if (call_result->IsPromise()) {
            v8::Local<v8::Promise> promise = call_result.As<v8::Promise>();
            size_t spins = 0U;
            while (promise->State() == v8::Promise::kPending && spins < kWorkersV8MicrotaskLimit) {
                isolate->PerformMicrotaskCheckpoint();
                spins += 1U;
            }
            if (promise->State() == v8::Promise::kPending) {
                result.code = "SLOPPY_E_WORK_JOB_TIMEOUT";
                result.message = "worker Promise did not settle during bounded drain";
                goto workers_v8_execute_done;
            }
            if (promise->State() == v8::Promise::kRejected) {
                result.code = "SLOPPY_E_WORKER_CRASHED";
                result.message = "worker Promise rejected";
                goto workers_v8_execute_done;
            }
            call_result = promise->Result();
        }
        else {
            isolate->PerformMicrotaskCheckpoint();
        }

        if (!workers_v8_json_stringify(isolate, context, call_result, &result.json,
                                       &undefined_json))
        {
            result.code = "SLOPPY_E_WORKER_MESSAGE_SERIALIZATION_FAILED";
            result.message = "worker result serialization failed";
            goto workers_v8_execute_done;
        }
        if (result.json.size() > kWorkersV8MaxResultBytes) {
            result.code = "SLOPPY_E_WORKER_RESOURCE_LIMIT_EXCEEDED";
            result.message = "worker resource limit exceeded";
            goto workers_v8_execute_done;
        }
        result.undefined_result = undefined_json;
        result.ok = true;
    }

workers_v8_execute_done:
    isolate->Dispose();
    delete allocator;
    return result;
}

WorkersV8ExecutionResult workers_v8_execute_js_on_runtime(v8::Isolate* isolate,
                                                          v8::Local<v8::Context> context,
                                                          const SlV8WorkerRequest& request)
{
    WorkersV8ExecutionResult result = {};
    v8::TryCatch try_catch(isolate);
    v8::Local<v8::Value> input_value;
    v8::Local<v8::Object> ctx = v8::Object::New(isolate);
    v8::Local<v8::Object> signal = v8::Object::New(isolate);
    v8::Local<v8::Value> exports_value;
    v8::Local<v8::Value> fn_value;
    v8::Local<v8::Function> fn;
    v8::Local<v8::Value> call_result;
    bool undefined_json = false;

    if (request.input_json.size() > kWorkersV8MaxPayloadBytes) {
        result.code = "SLOPPY_E_WORKER_RESOURCE_LIMIT_EXCEEDED";
        result.message = "worker resource limit exceeded";
        return result;
    }
    if (!workers_v8_json_parse(isolate, context, request.input_json, &input_value)) {
        result.code = "SLOPPY_E_WORKER_MESSAGE_SERIALIZATION_FAILED";
        result.message = "worker message serialization failed";
        return result;
    }
    if (!signal
             ->Set(context, workers_v8_literal(isolate, "cancelled"),
                   v8::Boolean::New(isolate, false))
             .FromMaybe(false) ||
        !signal
             ->Set(context, workers_v8_literal(isolate, "aborted"),
                   v8::Boolean::New(isolate, false))
             .FromMaybe(false) ||
        !ctx->Set(context, workers_v8_literal(isolate, "input"), input_value).FromMaybe(false) ||
        !ctx->Set(context, workers_v8_literal(isolate, "signal"), signal).FromMaybe(false))
    {
        result.code = "SLOPPY_E_WORKER_CRASHED";
        result.message = "worker context creation failed";
        return result;
    }
    if (!context->Global()
             ->Get(context, workers_v8_literal(isolate, "__sloppyWorkerExports"))
             .ToLocal(&exports_value) ||
        !exports_value->IsObject() ||
        !exports_value.As<v8::Object>()
             ->Get(context, v8::String::NewFromUtf8(isolate, request.export_name.c_str(),
                                                    v8::NewStringType::kNormal,
                                                    static_cast<int>(request.export_name.size()))
                                .ToLocalChecked())
             .ToLocal(&fn_value) ||
        !fn_value->IsFunction())
    {
        result.code = "SLOPPY_E_WORKER_CRASHED";
        result.message = "worker export is not callable";
        return result;
    }

    fn = fn_value.As<v8::Function>();
    v8::Local<v8::Value> argv[] = {input_value, ctx};
    if (!fn->Call(context, context->Global(), 2, argv).ToLocal(&call_result)) {
        result.code = "SLOPPY_E_WORKER_CRASHED";
        result.message = "worker crashed";
        return result;
    }

    if (call_result->IsPromise()) {
        v8::Local<v8::Promise> promise = call_result.As<v8::Promise>();
        size_t spins = 0U;
        while (promise->State() == v8::Promise::kPending && spins < kWorkersV8MicrotaskLimit) {
            isolate->PerformMicrotaskCheckpoint();
            spins += 1U;
        }
        if (promise->State() == v8::Promise::kPending) {
            result.code = "SLOPPY_E_WORK_JOB_TIMEOUT";
            result.message = "worker Promise did not settle during bounded drain";
            return result;
        }
        if (promise->State() == v8::Promise::kRejected) {
            result.code = "SLOPPY_E_WORKER_CRASHED";
            result.message = "worker Promise rejected";
            return result;
        }
        call_result = promise->Result();
    }
    else {
        isolate->PerformMicrotaskCheckpoint();
    }

    if (!workers_v8_json_stringify(isolate, context, call_result, &result.json, &undefined_json)) {
        result.code = "SLOPPY_E_WORKER_MESSAGE_SERIALIZATION_FAILED";
        result.message = "worker result serialization failed";
        return result;
    }
    if (result.json.size() > kWorkersV8MaxResultBytes) {
        result.code = "SLOPPY_E_WORKER_RESOURCE_LIMIT_EXCEEDED";
        result.message = "worker resource limit exceeded";
        return result;
    }
    result.undefined_result = undefined_json;
    result.ok = true;
    return result;
}

void workers_v8_remove_request(const std::shared_ptr<SlV8WorkerRequest>& request)
{
    SlV8Engine* backend = request == nullptr ? nullptr : request->backend;
    if (backend == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(backend->workers_mutex);
    auto& requests = backend->worker_requests;
    requests.erase(std::remove(requests.begin(), requests.end(), request), requests.end());
}

void workers_v8_release_worker_activity(const std::shared_ptr<SlV8JsWorker>& worker)
{
    if (worker == nullptr) {
        return;
    }
    SlV8Engine* backend = worker->backend;
    if (backend == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(backend->workers_mutex);
    if (worker->active_count != 0U) {
        worker->active_count -= 1U;
    }
    if (worker->stopped && worker->active_count == 0U) {
        auto& workers = backend->js_workers;
        workers.erase(std::remove(workers.begin(), workers.end(), worker), workers.end());
    }
}

void workers_v8_finish_worker_request(const std::shared_ptr<SlV8WorkerRequest>& request)
{
    workers_v8_release_worker_activity(request == nullptr ? nullptr : request->js_worker.lock());
}

void workers_v8_completion_cleanup(const SlAsyncCompletion* completion, void* user)
{
    WorkersV8CompletionPayload* payload =
        completion == nullptr ? nullptr
                              : static_cast<WorkersV8CompletionPayload*>(completion->payload);
    std::shared_ptr<SlV8WorkerRequest> request = payload == nullptr ? nullptr : payload->request;

    (void)user;
    if (request != nullptr && !request->cleanup_ran.exchange(true)) {
        request->resolver.Reset();
        if (request->thread.joinable() && request->thread.get_id() != std::this_thread::get_id()) {
            request->thread.join();
        }
        workers_v8_finish_worker_request(request);
        workers_v8_remove_request(request);
    }
    delete payload;
}

SlStatus workers_v8_completion_dispatch(SlAsyncLoop* loop, const SlAsyncCompletion* completion,
                                        void* user)
{
    WorkersV8CompletionPayload* payload =
        completion == nullptr ? nullptr
                              : static_cast<WorkersV8CompletionPayload*>(completion->payload);
    std::shared_ptr<SlV8WorkerRequest> request = payload == nullptr ? nullptr : payload->request;
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

    if (!sl_status_is_ok(completion->status) || request->reject) {
        bool ok = workers_v8_reject_code_error(isolate, context, resolver, request->error_code,
                                               request->error_message);
        isolate->PerformMicrotaskCheckpoint();
        return ok ? sl_status_ok() : sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    if (request->result_is_undefined) {
        if (!resolver->Resolve(context, v8::Undefined(isolate)).FromMaybe(false)) {
            return sl_status_from_code(SL_STATUS_INVALID_STATE);
        }
        isolate->PerformMicrotaskCheckpoint();
        return sl_status_ok();
    }

    v8::Local<v8::Value> result_value;
    if (!workers_v8_json_parse(isolate, context, request->result_json, &result_value)) {
        bool ok = workers_v8_reject_code_error(isolate, context, resolver,
                                               "SLOPPY_E_WORKER_MESSAGE_SERIALIZATION_FAILED",
                                               "worker result serialization failed");
        isolate->PerformMicrotaskCheckpoint();
        return ok ? sl_status_ok() : sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    if (!resolver->Resolve(context, result_value).FromMaybe(false)) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    isolate->PerformMicrotaskCheckpoint();
    return sl_status_ok();
}

void workers_v8_post_completion(const std::shared_ptr<SlV8WorkerRequest>& request)
{
    SlV8Engine* backend = request == nullptr ? nullptr : request->backend;
    if (backend == nullptr) {
        return;
    }

    for (size_t attempt = 0U; attempt < 1000U; attempt += 1U) {
        {
            std::lock_guard<std::mutex> lock(backend->workers_mutex);
            if (backend->workers_shutting_down || backend->async_loop == nullptr) {
                return;
            }
        }

        WorkersV8CompletionPayload* payload = new (std::nothrow) WorkersV8CompletionPayload();
        if (payload == nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        payload->request = request;

        SlAsyncCompletion completion = {};
        completion.kind = SL_ASYNC_COMPLETION_V8_CONTINUATION;
        completion.operation_kind = SL_ASYNC_OPERATION_BLOCKING_OFFLOAD;
        completion.status =
            request->reject ? sl_status_from_code(SL_STATUS_INVALID_STATE) : sl_status_ok();
        completion.payload = payload;
        completion.dispatch = workers_v8_completion_dispatch;
        completion.cleanup = workers_v8_completion_cleanup;

        request->completion_posted.store(true);
        SlStatus status = sl_async_loop_post(backend->async_loop, &completion);
        if (sl_status_is_ok(status)) {
            return;
        }
        request->completion_posted.store(false);
        delete payload;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::terminate();
}

void workers_v8_thread_main(std::shared_ptr<SlV8WorkerRequest> request)
{
    WorkersV8ExecutionResult result = workers_v8_execute_in_isolate(*request);
    request->reject = !result.ok;
    request->result_is_undefined = result.undefined_result;
    request->result_json = std::move(result.json);
    request->error_code = std::move(result.code);
    request->error_message = std::move(result.message);
    workers_v8_post_completion(request);
}

void workers_v8_signal_worker_startup(const std::shared_ptr<SlV8JsWorker>& worker, bool ok,
                                      const std::string& code, const std::string& message)
{
    if (worker == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(worker->mutex);
        worker->startup_ok = ok;
        worker->startup_complete = true;
        worker->startup_error_code = code;
        worker->startup_error_message = message;
    }
    worker->cv.notify_all();
}

void workers_v8_worker_runtime_main(std::shared_ptr<SlV8JsWorker> worker)
{
    v8::ArrayBuffer::Allocator* allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
    if (allocator == nullptr) {
        workers_v8_signal_worker_startup(worker, false, "SLOPPY_E_WORKER_POOL_UNAVAILABLE",
                                         "worker isolate allocator unavailable");
        return;
    }

    v8::Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = allocator;
    workers_v8_apply_memory_limit(&create_params, worker->memory_limit_mb);
    v8::Isolate* isolate = v8::Isolate::New(create_params);
    if (isolate == nullptr) {
        delete allocator;
        workers_v8_signal_worker_startup(worker, false, "SLOPPY_E_WORKER_ISOLATE_STARTUP_FAILED",
                                         "worker isolate startup failed");
        return;
    }

    {
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        isolate->SetMicrotasksPolicy(v8::MicrotasksPolicy::kExplicit);
        v8::Local<v8::Context> context = v8::Context::New(isolate);
        v8::Context::Scope context_scope(context);
        v8::TryCatch try_catch(isolate);
        v8::Local<v8::Value> ignored;
        std::string transformed;
        std::string syntax_error;

        if (!workers_v8_validate_module_export_syntax(worker->module_source, &syntax_error)) {
            workers_v8_signal_worker_startup(
                worker, false, "SLOPPY_E_WORKER_ISOLATE_STARTUP_FAILED", syntax_error);
        }
        else {
            try {
                transformed = workers_v8_transform_module_source(worker->module_source);
            } catch (...) {
                workers_v8_signal_worker_startup(worker, false,
                                                 "SLOPPY_E_WORKER_ISOLATE_STARTUP_FAILED",
                                                 "worker module transform failed");
            }
        }
        if (!transformed.empty() &&
            !workers_v8_compile_run(isolate, context, worker->module_path, transformed, &ignored))
        {
            workers_v8_signal_worker_startup(worker, false,
                                             "SLOPPY_E_WORKER_ISOLATE_STARTUP_FAILED",
                                             "worker isolate startup failed");
        }
        else if (!transformed.empty()) {
            workers_v8_signal_worker_startup(worker, true, std::string(), std::string());
        }

        for (;;) {
            std::shared_ptr<SlV8WorkerRequest> request;
            {
                std::unique_lock<std::mutex> lock(worker->mutex);
                worker->cv.wait(lock,
                                [&worker]() { return worker->stopping || !worker->queue.empty(); });
                if (worker->queue.empty()) {
                    if (worker->stopping) {
                        break;
                    }
                    continue;
                }
                request = worker->queue.front();
                worker->queue.erase(worker->queue.begin());
            }

            v8::HandleScope request_scope(isolate);
            v8::Context::Scope request_context_scope(context);
            WorkersV8ExecutionResult result =
                workers_v8_execute_js_on_runtime(isolate, context, *request);
            request->reject = !result.ok;
            request->result_is_undefined = result.undefined_result;
            request->result_json = std::move(result.json);
            request->error_code = std::move(result.code);
            request->error_message = std::move(result.message);
            workers_v8_post_completion(request);
        }
    }

    isolate->Dispose();
    delete allocator;
}

bool workers_v8_track_request(SlV8Engine* backend,
                              const std::shared_ptr<SlV8WorkerRequest>& request)
{
    if (backend == nullptr || request == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(backend->workers_mutex);
    if (backend->workers_shutting_down || backend->async_loop == nullptr) {
        return false;
    }
    backend->worker_requests.push_back(request);
    return true;
}

void workers_v8_untrack_request(SlV8Engine* backend,
                                const std::shared_ptr<SlV8WorkerRequest>& request)
{
    if (backend == nullptr || request == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(backend->workers_mutex);
    auto& requests = backend->worker_requests;
    requests.erase(std::remove(requests.begin(), requests.end(), request), requests.end());
}

bool workers_v8_prepare_request_promise(SlV8Engine* backend,
                                        const std::shared_ptr<SlV8WorkerRequest>& request,
                                        v8::Local<v8::Promise>* out_promise)
{
    v8::Isolate* isolate = backend == nullptr ? nullptr : backend->isolate;
    v8::Local<v8::Promise::Resolver> resolver;

    if (backend == nullptr || isolate == nullptr || request == nullptr || out_promise == nullptr ||
        backend->async_loop == nullptr)
    {
        return false;
    }
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
        return false;
    }
    request->backend = backend;
    request->resolver.Reset(isolate, resolver);
    *out_promise = resolver->GetPromise();
    return true;
}

bool workers_v8_start_request(SlV8Engine* backend,
                              const std::shared_ptr<SlV8WorkerRequest>& request)
{
    if (!workers_v8_track_request(backend, request)) {
        return false;
    }
    try {
        request->thread = std::thread(workers_v8_thread_main, request);
    } catch (...) {
        request->resolver.Reset();
        workers_v8_untrack_request(backend, request);
        return false;
    }
    return true;
}

void workers_v8_stop_worker_runtime(const std::shared_ptr<SlV8JsWorker>& worker)
{
    if (worker == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(worker->mutex);
        worker->stopping = true;
    }
    worker->cv.notify_all();
    if (worker->thread.joinable() && worker->thread.get_id() != std::this_thread::get_id()) {
        worker->thread.join();
    }
}

bool workers_v8_start_worker_runtime(const std::shared_ptr<SlV8JsWorker>& worker)
{
    if (worker == nullptr) {
        return false;
    }
    try {
        worker->thread = std::thread(workers_v8_worker_runtime_main, worker);
    } catch (...) {
        return false;
    }

    std::unique_lock<std::mutex> lock(worker->mutex);
    worker->cv.wait(lock, [&worker]() { return worker->startup_complete; });
    return worker->startup_ok;
}

bool workers_v8_enqueue_worker_request(const std::shared_ptr<SlV8JsWorker>& worker,
                                       const std::shared_ptr<SlV8WorkerRequest>& request)
{
    SlV8Engine* backend = worker == nullptr ? nullptr : worker->backend;
    if (backend == nullptr || worker == nullptr || request == nullptr ||
        !workers_v8_track_request(backend, request))
    {
        return false;
    }
    bool stopping = false;
    {
        std::lock_guard<std::mutex> lock(worker->mutex);
        if (worker->stopping) {
            stopping = true;
        }
        else {
            worker->queue.push_back(request);
        }
    }
    if (stopping) {
        workers_v8_untrack_request(backend, request);
        return false;
    }
    worker->cv.notify_one();
    return true;
}

std::shared_ptr<SlV8JsWorker> workers_v8_find_worker(SlV8Engine* backend, uint32_t id)
{
    if (backend == nullptr || id == 0U) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(backend->workers_mutex);
    for (const std::shared_ptr<SlV8JsWorker>& worker : backend->js_workers) {
        if (worker != nullptr && worker->id == id) {
            return worker;
        }
    }
    return nullptr;
}

bool workers_v8_handle_worker_id(v8::Local<v8::Context> context, v8::Local<v8::Object> object,
                                 uint32_t* out_id)
{
    if (out_id == nullptr || object.IsEmpty() || object->InternalFieldCount() == 0) {
        return false;
    }
    v8::Local<v8::Value> value = v8::Local<v8::Value>::Cast(object->GetInternalField(0));
    if (value.IsEmpty() || !value->IsUint32()) {
        return false;
    }
    *out_id = value.As<v8::Uint32>()->Value();
    (void)context;
    return *out_id != 0U;
}

size_t workers_v8_memory_limit_from_options(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                            v8::Local<v8::Value> value)
{
    if (value.IsEmpty() || !value->IsObject()) {
        return 128U;
    }
    v8::Local<v8::Value> limit_value;
    if (!value.As<v8::Object>()
             ->Get(context, workers_v8_literal(isolate, "memoryLimitMb"))
             .ToLocal(&limit_value) ||
        !limit_value->IsNumber())
    {
        return 128U;
    }
    double limit = limit_value.As<v8::Number>()->Value();
    if (!std::isfinite(limit) || limit < 0.0 ||
        limit > static_cast<double>(std::numeric_limits<size_t>::max()))
    {
        return 0U;
    }
    return static_cast<size_t>(limit);
}

bool workers_v8_read_worker_source(SlV8Engine* backend, const std::string& module_path,
                                   std::string* out_source)
{
    SlFsStat stat = {};
    SlDiag diag = {};

    if (out_source == nullptr || module_path.empty() ||
        !sl_status_is_ok(
            sl_fs_stat(sl_str_from_parts(module_path.data(), module_path.size()), &stat, &diag)) ||
        !stat.exists || stat.kind != SL_FS_NODE_FILE || stat.size > kWorkersV8MaxModuleBytes)
    {
        return false;
    }

    std::vector<unsigned char> storage(kWorkersV8MaxModuleBytes + 4096U);
    SlArena arena = {};
    SlOwnedBytes bytes = {};
    if (!sl_status_is_ok(sl_arena_init(&arena, storage.data(), storage.size())) ||
        !sl_status_is_ok(sl_fs_read_file(
            &arena, sl_str_from_parts(module_path.data(), module_path.size()), &bytes, &diag)))
    {
        return false;
    }
    *out_source =
        std::string(reinterpret_cast<const char*>(bytes.ptr), static_cast<size_t>(bytes.length));
    (void)backend;
    return true;
}

void workers_v8_run_pool_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::string function_source;
    std::string input_json;
    bool input_undefined = false;
    v8::Local<v8::Promise> promise;

    if (backend == nullptr || backend->async_loop == nullptr) {
        workers_v8_throw_code_error(backend, "SLOPPY_E_WORKER_POOL_UNAVAILABLE",
                                    "worker pool unavailable");
        return;
    }
    if (args.Length() < 3 || !args[1]->IsFunction()) {
        workers_v8_throw_code_error(backend, "SLOPPY_E_WORKER_UNSUPPORTED_PAYLOAD",
                                    "WorkerPool.run requires a callable worker function");
        return;
    }
    if (!workers_v8_std_string_from_value(isolate, args[1], &function_source) ||
        function_source.empty())
    {
        workers_v8_throw_code_error(backend, "SLOPPY_E_WORKER_MESSAGE_SERIALIZATION_FAILED",
                                    "worker function serialization failed");
        return;
    }
    if (!workers_v8_json_stringify(isolate, context, args[2], &input_json, &input_undefined) ||
        input_undefined || input_json.size() > kWorkersV8MaxPayloadBytes)
    {
        workers_v8_throw_code_error(backend, "SLOPPY_E_WORKER_MESSAGE_SERIALIZATION_FAILED",
                                    "worker message serialization failed");
        return;
    }

    std::shared_ptr<SlV8WorkerRequest> request(new (std::nothrow) SlV8WorkerRequest());
    if (request == nullptr || !workers_v8_prepare_request_promise(backend, request, &promise)) {
        workers_v8_throw_code_error(backend, "SLOPPY_E_WORKER_POOL_UNAVAILABLE",
                                    "worker pool unavailable");
        return;
    }

    request->kind = WorkersV8RequestKind::PoolRun;
    request->function_source = std::move(function_source);
    request->input_json = std::move(input_json);
    request->memory_limit_mb = 128U;
    if (!workers_v8_start_request(backend, request)) {
        workers_v8_throw_code_error(backend, "SLOPPY_E_WORKER_POOL_UNAVAILABLE",
                                    "worker pool unavailable");
        return;
    }
    args.GetReturnValue().Set(promise);
}

void workers_v8_worker_invoke_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    uint32_t worker_id = 0U;
    std::string export_name;
    std::string input_json;
    bool input_undefined = false;
    v8::Local<v8::Promise> promise;

    if (backend == nullptr || backend->async_loop == nullptr ||
        !workers_v8_handle_worker_id(context, args.This(), &worker_id))
    {
        workers_v8_throw_code_error(backend, "SLOPPY_E_WORKER_STALE_HANDLE",
                                    "worker handle is stale or disposed");
        return;
    }
    if (args.Length() < 1 || !workers_v8_std_string_from_value(isolate, args[0], &export_name) ||
        export_name.empty())
    {
        workers_v8_throw_code_error(backend, "SLOPPY_E_WORKER_UNSUPPORTED_PAYLOAD",
                                    "Worker.invoke requires an export name");
        return;
    }
    v8::Local<v8::Value> payload = args.Length() > 1 ? args[1] : v8::Null(isolate).As<v8::Value>();
    if (!workers_v8_json_stringify(isolate, context, payload, &input_json, &input_undefined) ||
        input_undefined || input_json.size() > kWorkersV8MaxPayloadBytes)
    {
        workers_v8_throw_code_error(backend, "SLOPPY_E_WORKER_MESSAGE_SERIALIZATION_FAILED",
                                    "worker message serialization failed");
        return;
    }

    std::shared_ptr<SlV8JsWorker> worker = workers_v8_find_worker(backend, worker_id);
    {
        std::lock_guard<std::mutex> lock(backend->workers_mutex);
        if (worker == nullptr || worker->stopped) {
            workers_v8_throw_code_error(backend, "SLOPPY_E_WORKER_STALE_HANDLE",
                                        "worker handle is stale or disposed");
            return;
        }
        if (worker->active_count >= kWorkersV8PerWorkerQueueCapacity) {
            workers_v8_throw_code_error(backend, "SLOPPY_E_WORKER_POOL_SATURATED",
                                        "worker queue is saturated");
            return;
        }
        worker->active_count += 1U;
    }

    std::shared_ptr<SlV8WorkerRequest> request(new (std::nothrow) SlV8WorkerRequest());
    if (request == nullptr) {
        workers_v8_release_worker_activity(worker);
        workers_v8_throw_code_error(backend, "SLOPPY_E_WORKER_POOL_UNAVAILABLE",
                                    "worker pool unavailable");
        return;
    }
    request->js_worker = worker;
    if (!workers_v8_prepare_request_promise(backend, request, &promise)) {
        workers_v8_release_worker_activity(worker);
        workers_v8_throw_code_error(backend, "SLOPPY_E_WORKER_POOL_UNAVAILABLE",
                                    "worker pool unavailable");
        return;
    }
    request->kind = WorkersV8RequestKind::JsInvoke;
    request->module_path = worker->module_path;
    request->module_source = worker->module_source;
    request->export_name = std::move(export_name);
    request->input_json = std::move(input_json);
    request->memory_limit_mb = worker->memory_limit_mb;
    if (!workers_v8_enqueue_worker_request(worker, request)) {
        workers_v8_finish_worker_request(request);
        workers_v8_throw_code_error(backend, "SLOPPY_E_WORKER_POOL_UNAVAILABLE",
                                    "worker pool unavailable");
        return;
    }
    args.GetReturnValue().Set(promise);
}

void workers_v8_worker_post_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    uint32_t worker_id = 0U;
    std::string input_json;
    bool input_undefined = false;
    v8::Local<v8::Promise> promise;

    if (backend == nullptr || backend->async_loop == nullptr ||
        !workers_v8_handle_worker_id(context, args.This(), &worker_id))
    {
        workers_v8_throw_code_error(backend, "SLOPPY_E_WORKER_STALE_HANDLE",
                                    "worker handle is stale or disposed");
        return;
    }
    v8::Local<v8::Value> payload = args.Length() > 0 ? args[0] : v8::Null(isolate).As<v8::Value>();
    if (!workers_v8_json_stringify(isolate, context, payload, &input_json, &input_undefined) ||
        input_undefined || input_json.size() > kWorkersV8MaxPayloadBytes)
    {
        workers_v8_throw_code_error(backend, "SLOPPY_E_WORKER_MESSAGE_SERIALIZATION_FAILED",
                                    "worker message serialization failed");
        return;
    }

    std::shared_ptr<SlV8JsWorker> worker = workers_v8_find_worker(backend, worker_id);
    {
        std::lock_guard<std::mutex> lock(backend->workers_mutex);
        if (worker == nullptr || worker->stopped) {
            workers_v8_throw_code_error(backend, "SLOPPY_E_WORKER_STALE_HANDLE",
                                        "worker handle is stale or disposed");
            return;
        }
        if (worker->active_count >= kWorkersV8PerWorkerQueueCapacity) {
            workers_v8_throw_code_error(backend, "SLOPPY_E_WORKER_POOL_SATURATED",
                                        "worker queue is saturated");
            return;
        }
        worker->active_count += 1U;
    }

    std::shared_ptr<SlV8WorkerRequest> request(new (std::nothrow) SlV8WorkerRequest());
    if (request == nullptr) {
        workers_v8_release_worker_activity(worker);
        workers_v8_throw_code_error(backend, "SLOPPY_E_WORKER_POOL_UNAVAILABLE",
                                    "worker pool unavailable");
        return;
    }
    request->js_worker = worker;
    if (!workers_v8_prepare_request_promise(backend, request, &promise)) {
        workers_v8_release_worker_activity(worker);
        workers_v8_throw_code_error(backend, "SLOPPY_E_WORKER_POOL_UNAVAILABLE",
                                    "worker pool unavailable");
        return;
    }
    request->kind = WorkersV8RequestKind::JsInvoke;
    request->module_path = worker->module_path;
    request->module_source = worker->module_source;
    request->export_name = "onMessage";
    request->input_json = std::move(input_json);
    request->memory_limit_mb = worker->memory_limit_mb;
    if (!workers_v8_enqueue_worker_request(worker, request)) {
        workers_v8_finish_worker_request(request);
        workers_v8_throw_code_error(backend, "SLOPPY_E_WORKER_POOL_UNAVAILABLE",
                                    "worker pool unavailable");
        return;
    }
    args.GetReturnValue().Set(promise);
}

void workers_v8_worker_stop_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    uint32_t worker_id = 0U;

    if (backend == nullptr || !workers_v8_handle_worker_id(context, args.This(), &worker_id)) {
        workers_v8_throw_code_error(backend, "SLOPPY_E_WORKER_STALE_HANDLE",
                                    "worker handle is stale or disposed");
        return;
    }

    std::shared_ptr<SlV8JsWorker> stopped_worker;
    {
        std::lock_guard<std::mutex> lock(backend->workers_mutex);
        for (auto it = backend->js_workers.begin(); it != backend->js_workers.end(); ++it) {
            std::shared_ptr<SlV8JsWorker> worker = *it;
            if (worker == nullptr || worker->id != worker_id) {
                continue;
            }
            worker->stopped = true;
            stopped_worker = worker;
            if (worker->active_count == 0U) {
                backend->js_workers.erase(it);
            }
            break;
        }
    }
    if (stopped_worker != nullptr) {
        workers_v8_stop_worker_runtime(stopped_worker);
        args.GetReturnValue().Set(v8::Undefined(isolate));
        return;
    }
    workers_v8_throw_code_error(backend, "SLOPPY_E_WORKER_STALE_HANDLE",
                                "worker handle is stale or disposed");
}

void workers_v8_worker_on_message_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);

    workers_v8_throw_code_error(backend, "SLOPPY_E_WORKER_BRIDGE_UNAVAILABLE",
                                "worker receive bridge is unavailable");
}

bool workers_v8_make_worker_handle(SlV8Engine* backend, v8::Local<v8::Context> context,
                                   const std::shared_ptr<SlV8JsWorker>& worker,
                                   v8::Local<v8::Object>* out)
{
    v8::Isolate* isolate = backend == nullptr ? nullptr : backend->isolate;
    v8::Local<v8::ObjectTemplate> templ;
    v8::Local<v8::Object> object;

    if (backend == nullptr || isolate == nullptr || worker == nullptr || out == nullptr) {
        return false;
    }
    templ = v8::ObjectTemplate::New(isolate);
    templ->SetInternalFieldCount(1);
    if (!templ->NewInstance(context).ToLocal(&object)) {
        return false;
    }
    object->SetInternalField(0, v8::Integer::NewFromUnsigned(isolate, worker->id));
    if (!workers_v8_set_function(backend, context, object, "invoke",
                                 workers_v8_worker_invoke_callback) ||
        !workers_v8_set_function(backend, context, object, "post",
                                 workers_v8_worker_post_callback) ||
        !workers_v8_set_function(backend, context, object, "onMessage",
                                 workers_v8_worker_on_message_callback) ||
        !workers_v8_set_function(backend, context, object, "stop",
                                 workers_v8_worker_stop_callback) ||
        !workers_v8_set_string(backend, context, object, "__sloppyWorkerResource", "jsWorker"))
    {
        return false;
    }
    *out = object;
    return true;
}

void workers_v8_start_worker_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::string module_path;
    std::string module_source;
    size_t memory_limit_mb = 128U;
    v8::Local<v8::Object> handle;

    if (backend == nullptr || backend->async_loop == nullptr) {
        workers_v8_throw_code_error(backend, "SLOPPY_E_WORKER_POOL_UNAVAILABLE",
                                    "worker pool unavailable");
        return;
    }
    if (args.Length() < 1 || !workers_v8_std_string_from_value(isolate, args[0], &module_path) ||
        module_path.empty())
    {
        workers_v8_throw_code_error(backend, "SLOPPY_E_WORKER_ISOLATE_STARTUP_FAILED",
                                    "worker isolate startup failed");
        return;
    }
    memory_limit_mb =
        args.Length() > 1 ? workers_v8_memory_limit_from_options(isolate, context, args[1]) : 128U;
    if (memory_limit_mb < kWorkersV8MinMemoryLimitMb) {
        workers_v8_throw_code_error(backend, "SLOPPY_E_WORKER_RESOURCE_LIMIT_EXCEEDED",
                                    "worker memory/resource limit exceeded");
        return;
    }
    if (!workers_v8_read_worker_source(backend, module_path, &module_source) ||
        module_source.size() > kWorkersV8MaxModuleBytes)
    {
        workers_v8_throw_code_error(backend, "SLOPPY_E_WORKER_ISOLATE_STARTUP_FAILED",
                                    "worker isolate startup failed");
        return;
    }

    std::shared_ptr<SlV8JsWorker> worker(new (std::nothrow) SlV8JsWorker());
    if (worker == nullptr) {
        workers_v8_throw_code_error(backend, "SLOPPY_E_WORKER_ISOLATE_STARTUP_FAILED",
                                    "worker isolate startup failed");
        return;
    }
    worker->backend = backend;
    worker->module_path = std::move(module_path);
    worker->module_source = std::move(module_source);
    worker->memory_limit_mb = memory_limit_mb;
    {
        std::lock_guard<std::mutex> lock(backend->workers_mutex);
        if (backend->workers_shutting_down) {
            workers_v8_throw_code_error(backend, "SLOPPY_E_WORKER_SHUTDOWN_CANCELLED",
                                        "app shutdown cancelled worker operation");
            return;
        }
    }
    if (!workers_v8_start_worker_runtime(worker)) {
        workers_v8_stop_worker_runtime(worker);
        workers_v8_throw_code_error(
            backend,
            worker->startup_error_code.empty() ? "SLOPPY_E_WORKER_ISOLATE_STARTUP_FAILED"
                                               : worker->startup_error_code.c_str(),
            worker->startup_error_message.empty() ? "worker isolate startup failed"
                                                  : worker->startup_error_message.c_str());
        return;
    }
    {
        std::lock_guard<std::mutex> lock(backend->workers_mutex);
        if (backend->workers_shutting_down) {
            workers_v8_stop_worker_runtime(worker);
            workers_v8_throw_code_error(backend, "SLOPPY_E_WORKER_SHUTDOWN_CANCELLED",
                                        "app shutdown cancelled worker operation");
            return;
        }
        worker->id = backend->next_worker_id++;
        if (worker->id == 0U) {
            worker->id = backend->next_worker_id++;
        }
        backend->js_workers.push_back(worker);
    }

    if (!workers_v8_make_worker_handle(backend, context, worker, &handle)) {
        {
            std::lock_guard<std::mutex> lock(backend->workers_mutex);
            worker->stopped = true;
            auto& workers = backend->js_workers;
            workers.erase(std::remove(workers.begin(), workers.end(), worker), workers.end());
        }
        workers_v8_stop_worker_runtime(worker);
        workers_v8_throw_code_error(backend, "SLOPPY_E_WORKER_ISOLATE_STARTUP_FAILED",
                                    "worker isolate startup failed");
        return;
    }
    args.GetReturnValue().Set(handle);
}

void sl_v8_append_workers_external_references(std::vector<intptr_t>* refs)
{
    if (refs == nullptr) {
        return;
    }
    refs->push_back(reinterpret_cast<intptr_t>(workers_v8_worker_invoke_callback));
    refs->push_back(reinterpret_cast<intptr_t>(workers_v8_worker_post_callback));
    refs->push_back(reinterpret_cast<intptr_t>(workers_v8_worker_on_message_callback));
    refs->push_back(reinterpret_cast<intptr_t>(workers_v8_worker_stop_callback));
    refs->push_back(reinterpret_cast<intptr_t>(workers_v8_run_pool_callback));
    refs->push_back(reinterpret_cast<intptr_t>(workers_v8_start_worker_callback));
}

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
        !workers_v8_set_bool(backend, context, workers, "ownerThreadSettlement", true) ||
        !workers_v8_set_function(backend, context, workers, "runPool",
                                 workers_v8_run_pool_callback) ||
        !workers_v8_set_function(backend, context, workers, "startWorker",
                                 workers_v8_start_worker_callback))
    {
        return false;
    }
    return sloppy->Set(context, workers_key, workers).FromMaybe(false);
}

void sl_v8_workers_dispose(SlV8Engine* backend)
{
    std::vector<std::shared_ptr<SlV8WorkerRequest>> requests;
    std::vector<std::shared_ptr<SlV8JsWorker>> workers;

    if (backend == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(backend->workers_mutex);
        backend->workers_shutting_down = true;
        requests = backend->worker_requests;
        workers = backend->js_workers;
        for (const std::shared_ptr<SlV8JsWorker>& worker : backend->js_workers) {
            if (worker != nullptr) {
                worker->stopped = true;
            }
        }
    }

    for (const std::shared_ptr<SlV8JsWorker>& worker : workers) {
        workers_v8_stop_worker_runtime(worker);
    }

    for (const std::shared_ptr<SlV8WorkerRequest>& request : requests) {
        if (request == nullptr || request->cleanup_ran.exchange(true)) {
            continue;
        }
        if (request->thread.joinable() && request->thread.get_id() != std::this_thread::get_id()) {
            request->thread.join();
        }
        request->resolver.Reset();
    }

    {
        std::lock_guard<std::mutex> lock(backend->workers_mutex);
        backend->worker_requests.clear();
        backend->js_workers.clear();
    }
}

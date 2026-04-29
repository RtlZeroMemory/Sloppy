/*
 * src/engine/v8/engine_v8.cc
 *
 * Implements the first V8-backed engine smoke path behind Sloppy's engine-neutral C ABI.
 * This file is the only implementation file that includes V8 headers. It owns isolate and
 * context lifetime, evaluates classic JavaScript source strings, calls a named global
 * zero-argument function, and copies supported primitive results back into caller-provided
 * arena storage.
 *
 * Safety invariants:
 * - no V8 handle, value, or type escapes this file;
 * - JS never receives raw native pointers;
 * - result strings are copied out of V8 before returning to C;
 * - the bridge is single-threaded by contract and does not implement workers or owner
 * - one owner thread creates and enters each isolate/context; wrong-thread entry fails
 *   before touching V8 state;
 * - V8's required process-wide platform state is initialized once, kept for process
 *   lifetime, and private to this module until an explicit runtime shutdown task exists.
 *
 * Tests: tests/unit/engine/test_v8_smoke.c when SLOPPY_ENABLE_V8 is enabled.
 */
#include "../engine_internal.h"

#include <libplatform/libplatform.h>
#include <v8.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <unordered_map>

namespace {

struct SlV8Engine
{
    v8::ArrayBuffer::Allocator* allocator = nullptr;
    v8::Isolate* isolate = nullptr;
    v8::Global<v8::Context> context;
    std::unordered_map<uint32_t, v8::Global<v8::Function>> handlers;
    std::unordered_map<uint32_t, v8::Global<v8::Function>>* pending_handlers = nullptr;
    std::thread::id owner_thread;
};

std::mutex g_v8_platform_mutex;
v8::Platform* g_v8_platform = nullptr;
bool g_v8_platform_initialized = false;

SlStr sl_v8_literal(const char* ptr, size_t length)
{
    return sl_str_from_parts(ptr, length);
}

bool sl_v8_str_valid(SlStr str)
{
    return str.length == 0U || str.ptr != nullptr;
}

SlStr sl_v8_str_from_string(const std::string& str)
{
    return sl_str_from_parts(str.data(), str.size());
}

SlStatus sl_v8_copy_string(SlArena* arena, const std::string& src, SlStr* out)
{
    void* memory = nullptr;
    char* dst = nullptr;

    if (arena == nullptr || out == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (src.empty()) {
        *out = sl_str_empty();
        return sl_status_ok();
    }

    SlStatus status = sl_arena_alloc(arena, src.size(), 1U, &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    dst = static_cast<char*>(memory);
    for (size_t index = 0U; index < src.size(); index += 1U) {
        dst[index] = src[index];
    }

    *out = sl_str_from_parts(dst, src.size());
    return sl_status_ok();
}

SlStatus sl_v8_copy_bytes(SlArena* arena, const std::string& src, SlBytes* out)
{
    void* memory = nullptr;
    unsigned char* dst = nullptr;

    if (arena == nullptr || out == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (src.empty()) {
        *out = sl_bytes_empty();
        return sl_status_ok();
    }

    SlStatus status = sl_arena_alloc(arena, src.size(), 1U, &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    dst = static_cast<unsigned char*>(memory);
    for (size_t index = 0U; index < src.size(); index += 1U) {
        dst[index] = static_cast<unsigned char>(src[index]);
    }

    *out = sl_bytes_from_parts(dst, src.size());
    return sl_status_ok();
}

SlStatus sl_v8_write_diag(SlArena* arena, SlDiag* out_diag, SlDiagCode code,
                          SlStatusCode failure_code, SlStr message, SlStr source_name, SlStr hint)
{
    SlDiagBuilder builder;
    SlStatus status;

    if (out_diag == nullptr) {
        return sl_status_from_code(failure_code);
    }

    if (arena == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_diag_builder_init(&builder, arena, SL_DIAG_SEVERITY_ERROR, code, message);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (source_name.length != 0U) {
        status = sl_diag_builder_set_primary_span(&builder,
                                                  sl_source_span_make(source_name, 1U, 1U, 0U));
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    if (hint.length != 0U) {
        status = sl_diag_builder_add_hint(&builder, hint);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    status = sl_diag_builder_finish(&builder, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_status_from_code(failure_code);
}

SlStatus sl_v8_write_diag_with_span(SlArena* arena, SlDiag* out_diag, SlDiagCode code,
                                    SlStatusCode failure_code, SlStr message, SlSourceSpan span,
                                    SlStr hint, const std::string& stack_summary)
{
    SlDiagBuilder builder;
    SlStatus status;

    if (out_diag == nullptr) {
        return sl_status_from_code(failure_code);
    }

    if (arena == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_diag_builder_init(&builder, arena, SL_DIAG_SEVERITY_ERROR, code, message);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (span.path.length != 0U || span.has_location) {
        status = sl_diag_builder_set_primary_span(&builder, span);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    if (!stack_summary.empty()) {
        status = sl_diag_builder_add_related(&builder, span, sl_v8_str_from_string(stack_summary));
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    if (hint.length != 0U) {
        status = sl_diag_builder_add_hint(&builder, hint);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    status = sl_diag_builder_finish(&builder, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_status_from_code(failure_code);
}

SlStatus sl_v8_write_diag_string(SlArena* arena, SlDiag* out_diag, SlDiagCode code,
                                 SlStatusCode failure_code, const std::string& message,
                                 SlStr source_name, SlStr hint)
{
    SlStr copied_message = {};
    SlStatus status;

    if (out_diag == nullptr) {
        return sl_status_from_code(failure_code);
    }

    status = sl_v8_copy_string(arena, message, &copied_message);

    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_v8_write_diag(arena, out_diag, code, failure_code, copied_message, source_name, hint);
}

SlStatus sl_v8_write_diag_string_with_span(SlArena* arena, SlDiag* out_diag, SlDiagCode code,
                                           SlStatusCode failure_code, const std::string& message,
                                           SlSourceSpan span, SlStr hint,
                                           const std::string& stack_summary)
{
    SlStr copied_message = {};
    SlStatus status;

    if (out_diag == nullptr) {
        return sl_status_from_code(failure_code);
    }

    status = sl_v8_copy_string(arena, message, &copied_message);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_v8_write_diag_with_span(arena, out_diag, code, failure_code, copied_message, span,
                                      hint, stack_summary);
}

std::string sl_v8_value_to_string(v8::Isolate* isolate, v8::Local<v8::Value> value)
{
    v8::String::Utf8Value utf8(isolate, value);

    if (*utf8 == nullptr) {
        return std::string("JavaScript exception");
    }

    return std::string(*utf8, static_cast<size_t>(utf8.length()));
}

std::string sl_v8_maybe_value_to_string(v8::Isolate* isolate, v8::Local<v8::Value> value)
{
    if (value.IsEmpty() || value->IsUndefined() || value->IsNull()) {
        return std::string();
    }

    return sl_v8_value_to_string(isolate, value);
}

std::string sl_v8_exception_message(v8::Isolate* isolate, v8::TryCatch& try_catch,
                                    const char* fallback)
{
    if (!try_catch.HasCaught()) {
        return std::string(fallback);
    }

    return sl_v8_value_to_string(isolate, try_catch.Exception());
}

std::string sl_v8_stack_summary(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                v8::TryCatch& try_catch)
{
    v8::Local<v8::Value> stack;
    std::string summary;
    const size_t max_stack_summary = 512U;

    if (!try_catch.StackTrace(context).ToLocal(&stack)) {
        return std::string();
    }

    summary = "JavaScript stack: " + sl_v8_value_to_string(isolate, stack);
    if (summary.size() > max_stack_summary) {
        summary.resize(max_stack_summary);
        summary += "...";
    }

    return summary;
}

std::string sl_v8_message_source_name(v8::Isolate* isolate, v8::TryCatch& try_catch,
                                      SlStr fallback_source_name)
{
    v8::Local<v8::Message> message = try_catch.Message();
    std::string source_name;

    if (!message.IsEmpty()) {
        source_name = sl_v8_maybe_value_to_string(isolate, message->GetScriptResourceName());
    }

    if (!source_name.empty()) {
        return source_name;
    }

    if (fallback_source_name.length != 0U) {
        return std::string(fallback_source_name.ptr, fallback_source_name.length);
    }

    return std::string();
}

SlSourceSpan sl_v8_exception_span(v8::Local<v8::Context> context, v8::TryCatch& try_catch,
                                  const std::string& source_name)
{
    v8::Local<v8::Message> message = try_catch.Message();
    int line = 0;
    int start_column = -1;
    size_t sloppy_column = 0U;

    if (message.IsEmpty()) {
        return sl_source_span_make(sl_v8_str_from_string(source_name), 0U, 0U, 0U);
    }

    line = message->GetLineNumber(context).FromMaybe(0);
    start_column = message->GetStartColumn(context).FromMaybe(-1);
    if (start_column >= 0) {
        // V8 reports start columns as zero-based; Sloppy diagnostics store 1-based columns.
        sloppy_column = static_cast<size_t>(start_column) + 1U;
    }

    return sl_source_span_make(sl_v8_str_from_string(source_name),
                               line > 0 ? static_cast<size_t>(line) : 0U, sloppy_column, 0U);
}

SlStatus sl_v8_write_exception_diag(SlEngine* engine, SlDiag* out_diag, SlDiagCode code,
                                    SlStatusCode failure_code, v8::Isolate* isolate,
                                    v8::Local<v8::Context> context, v8::TryCatch& try_catch,
                                    SlStr fallback_source_name, const char* fallback_message,
                                    SlStr hint)
{
    std::string message = sl_v8_exception_message(isolate, try_catch, fallback_message);
    std::string source_name = sl_v8_message_source_name(isolate, try_catch, fallback_source_name);
    SlSourceSpan span = sl_v8_exception_span(context, try_catch, source_name);
    std::string stack_summary = sl_v8_stack_summary(isolate, context, try_catch);

    return sl_v8_write_diag_string_with_span(engine->arena, out_diag, code, failure_code, message,
                                             span, hint, stack_summary);
}

SlStatus sl_v8_to_local_string(v8::Isolate* isolate, SlStr str, v8::Local<v8::String>* out)
{
    if (!sl_v8_str_valid(str) ||
        str.length > static_cast<size_t>(std::numeric_limits<int>::max()) || out == nullptr)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    v8::MaybeLocal<v8::String> maybe =
        v8::String::NewFromUtf8(isolate, str.ptr == nullptr ? "" : str.ptr,
                                v8::NewStringType::kNormal, static_cast<int>(str.length));
    if (!maybe.ToLocal(out)) {
        return sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
    }

    return sl_status_ok();
}

bool sl_v8_set_string_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
                               v8::Local<v8::Object> object, const char* name, SlStr value)
{
    v8::Local<v8::String> key;
    v8::Local<v8::String> local_value;

    if (!sl_status_is_ok(sl_v8_to_local_string(isolate, sl_str_from_cstr(name), &key)) ||
        !sl_status_is_ok(sl_v8_to_local_string(isolate, value, &local_value)))
    {
        return false;
    }

    return object->Set(context, key, local_value).FromMaybe(false);
}

bool sl_v8_set_object_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
                               v8::Local<v8::Object> object, const char* name,
                               v8::Local<v8::Object> value)
{
    v8::Local<v8::String> key;

    if (!sl_status_is_ok(sl_v8_to_local_string(isolate, sl_str_from_cstr(name), &key))) {
        return false;
    }

    return object->Set(context, key, value).FromMaybe(false);
}

SlStr sl_v8_request_method_name(SlHttpMethod method)
{
    switch (method) {
    case SL_HTTP_METHOD_GET:
        return sl_str_from_cstr("GET");
    case SL_HTTP_METHOD_POST:
        return sl_str_from_cstr("POST");
    case SL_HTTP_METHOD_PUT:
        return sl_str_from_cstr("PUT");
    case SL_HTTP_METHOD_PATCH:
        return sl_str_from_cstr("PATCH");
    case SL_HTTP_METHOD_DELETE:
        return sl_str_from_cstr("DELETE");
    case SL_HTTP_METHOD_OPTIONS:
        return sl_str_from_cstr("OPTIONS");
    case SL_HTTP_METHOD_HEAD:
        return sl_str_from_cstr("HEAD");
    default:
        return sl_str_empty();
    }
}

bool sl_v8_make_context_object(v8::Isolate* isolate, v8::Local<v8::Context> context,
                               const SlHttpRequestContext* request_context,
                               v8::Local<v8::Object>* out)
{
    v8::Local<v8::Object> ctx = v8::Object::New(isolate);
    v8::Local<v8::Object> route = v8::Object::New(isolate);
    v8::Local<v8::Object> query = v8::Object::New(isolate);
    v8::Local<v8::Object> request = v8::Object::New(isolate);
    size_t index = 0U;
    SlStr method = sl_str_empty();

    if (request_context == nullptr || request_context->request == nullptr || out == nullptr) {
        return false;
    }
    method = sl_v8_request_method_name(request_context->request->method);
    if (sl_str_is_empty(method)) {
        return false;
    }

    for (index = 0U; index < request_context->route_param_count; index += 1U) {
        const SlRouteParam* param = &request_context->route_params[index];
        v8::Local<v8::String> key;
        v8::Local<v8::String> value;
        if (!sl_status_is_ok(sl_v8_to_local_string(isolate, param->name, &key)) ||
            !sl_status_is_ok(sl_v8_to_local_string(isolate, param->value, &value)) ||
            !route->Set(context, key, value).FromMaybe(false))
        {
            return false;
        }
    }

    for (index = 0U; index < request_context->query_param_count; index += 1U) {
        const SlHttpQueryParam* param = &request_context->query_params[index];
        v8::Local<v8::String> key;
        v8::Local<v8::String> value;
        if (!sl_status_is_ok(sl_v8_to_local_string(isolate, param->name, &key)) ||
            !sl_status_is_ok(sl_v8_to_local_string(isolate, param->value, &value)) ||
            !query->Set(context, key, value).FromMaybe(false))
        {
            return false;
        }
    }

    if (!sl_v8_set_string_property(isolate, context, request, "method", method) ||
        !sl_v8_set_string_property(isolate, context, request, "path",
                                   request_context->request->path) ||
        !sl_v8_set_string_property(isolate, context, request, "rawTarget",
                                   request_context->request->raw_target) ||
        !sl_v8_set_object_property(isolate, context, ctx, "route", route) ||
        !sl_v8_set_object_property(isolate, context, ctx, "query", query) ||
        !sl_v8_set_object_property(isolate, context, ctx, "request", request))
    {
        return false;
    }

    *out = ctx;
    return true;
}

bool sl_v8_get_object_string(v8::Isolate* isolate, v8::Local<v8::Context> context,
                             v8::Local<v8::Object> object, const char* name, std::string* out)
{
    v8::Local<v8::String> key;
    v8::Local<v8::Value> value;

    if (out == nullptr ||
        !sl_status_is_ok(sl_v8_to_local_string(isolate, sl_str_from_cstr(name), &key)) ||
        !object->Get(context, key).ToLocal(&value) || !value->IsString())
    {
        return false;
    }

    v8::String::Utf8Value utf8(isolate, value);
    if (*utf8 == nullptr) {
        return false;
    }

    *out = std::string(*utf8, static_cast<size_t>(utf8.length()));
    return true;
}

bool sl_v8_get_object_status(v8::Isolate* isolate, v8::Local<v8::Context> context,
                             v8::Local<v8::Object> object, uint16_t* out)
{
    v8::Local<v8::String> key;
    v8::Local<v8::Value> value;
    int32_t status = 0;

    if (out == nullptr ||
        !sl_status_is_ok(sl_v8_to_local_string(isolate, sl_str_from_cstr("status"), &key)) ||
        !object->Get(context, key).ToLocal(&value) || !value->IsInt32())
    {
        return false;
    }

    status = value.As<v8::Int32>()->Value();
    if (status < 100 || status > 999) {
        return false;
    }

    *out = static_cast<uint16_t>(status);
    return true;
}

bool sl_v8_has_marker(v8::Isolate* isolate, v8::Local<v8::Context> context,
                      v8::Local<v8::Object> object)
{
    v8::Local<v8::String> key;
    v8::Local<v8::Value> value;

    if (!sl_status_is_ok(
            sl_v8_to_local_string(isolate, sl_str_from_cstr("__sloppyResult"), &key)) ||
        !object->Get(context, key).ToLocal(&value))
    {
        return false;
    }

    return value->BooleanValue(isolate);
}

bool sl_v8_http_status_supported(uint16_t status)
{
    switch (status) {
    case 200U:
    case 201U:
    case 202U:
    case 204U:
    case 400U:
    case 404U:
    case 405U:
    case 500U:
        return true;
    default:
        return false;
    }
}

bool sl_v8_header_value_safe(const std::string& value)
{
    return value.find('\r') == std::string::npos && value.find('\n') == std::string::npos;
}

bool sl_v8_stringify_json(v8::Isolate* isolate, v8::Local<v8::Context> context,
                          v8::Local<v8::Value> value, std::string* out)
{
    v8::Local<v8::String> json;

    if (out == nullptr || !v8::JSON::Stringify(context, value).ToLocal(&json)) {
        return false;
    }

    v8::String::Utf8Value utf8(isolate, json);
    if (*utf8 == nullptr) {
        return false;
    }

    *out = std::string(*utf8, static_cast<size_t>(utf8.length()));
    return true;
}

SlStatus sl_v8_write_missing_registered_handler_diag(SlEngine* engine, SlDiag* out_diag,
                                                     SlHandlerId handler_id)
{
    std::string message =
        "app plan references unregistered handler ID " + std::to_string(handler_id);
    return sl_v8_write_diag_string(
        engine->arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR, SL_STATUS_INVALID_STATE, message,
        sl_str_empty(),
        sl_v8_literal("Generated app modules must call __sloppy_register_handler(id, handler).",
                      sizeof("Generated app modules must call "
                             "__sloppy_register_handler(id, handler).") -
                          1U));
}

SlStatus sl_v8_write_wrong_thread_diag(SlEngine* engine, SlDiag* out_diag)
{
    return sl_v8_write_diag(
        engine->arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR, SL_STATUS_INVALID_STATE,
        sl_v8_literal("V8 engine entered from a non-owner thread",
                      sizeof("V8 engine entered from a non-owner thread") - 1U),
        sl_str_empty(),
        sl_v8_literal("Create, evaluate, call, validate, and dispose a V8 engine on its owner "
                      "thread. Worker threads must post work back to the owner thread.",
                      sizeof("Create, evaluate, call, validate, and dispose a V8 engine on its "
                             "owner thread. Worker threads must post work back to the owner "
                             "thread.") -
                          1U));
}

bool sl_v8_on_owner_thread(const SlV8Engine* backend)
{
    return backend != nullptr && backend->owner_thread == std::this_thread::get_id();
}

SlStatus sl_v8_check_owner_thread(SlEngine* engine, const SlV8Engine* backend, SlDiag* out_diag)
{
    if (!sl_v8_on_owner_thread(backend)) {
        return sl_v8_write_wrong_thread_diag(engine, out_diag);
    }

    return sl_status_ok();
}

void sl_v8_register_handler_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);

    if (backend == nullptr || args.Length() != 2) {
        isolate->ThrowException(v8::Exception::TypeError(v8::String::NewFromUtf8Literal(
            isolate, "__sloppy_register_handler requires handler ID and handler function")));
        return;
    }

    v8::Local<v8::Value> id_value = args[0];
    v8::Local<v8::Value> handler_value = args[1];

    if (!id_value->IsUint32()) {
        isolate->ThrowException(v8::Exception::TypeError(v8::String::NewFromUtf8Literal(
            isolate, "__sloppy_register_handler handler ID must be a positive integer")));
        return;
    }

    uint32_t handler_id = id_value.As<v8::Uint32>()->Value();
    if (handler_id == 0U) {
        isolate->ThrowException(v8::Exception::TypeError(v8::String::NewFromUtf8Literal(
            isolate, "__sloppy_register_handler handler ID must be a positive integer")));
        return;
    }

    if (!handler_value->IsFunction()) {
        isolate->ThrowException(v8::Exception::TypeError(v8::String::NewFromUtf8Literal(
            isolate, "__sloppy_register_handler handler must be callable")));
        return;
    }

    if (backend->pending_handlers == nullptr) {
        isolate->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8Literal(
            isolate, "__sloppy_register_handler is only valid during app evaluation")));
        return;
    }

    if (backend->handlers.find(handler_id) != backend->handlers.end() ||
        backend->pending_handlers->find(handler_id) != backend->pending_handlers->end())
    {
        isolate->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8Literal(
            isolate, "__sloppy_register_handler duplicate handler ID")));
        return;
    }

    backend->pending_handlers->emplace(
        handler_id, v8::Global<v8::Function>(isolate, handler_value.As<v8::Function>()));
    args.GetReturnValue().Set(v8::Undefined(isolate));
}

bool sl_v8_install_intrinsics(v8::Isolate* isolate, v8::Local<v8::Context> context)
{
    v8::Local<v8::String> name = v8::String::NewFromUtf8Literal(
        isolate, "__sloppy_register_handler", v8::NewStringType::kInternalized);
    v8::Local<v8::FunctionTemplate> function_template =
        v8::FunctionTemplate::New(isolate, sl_v8_register_handler_callback);
    v8::Local<v8::Function> function;

    if (!function_template->GetFunction(context).ToLocal(&function)) {
        return false;
    }

    return context->Global()->Set(context, name, function).FromMaybe(false);
}

SlStatus sl_v8_platform_acquire(void)
{
    std::lock_guard<std::mutex> lock(g_v8_platform_mutex);

    if (!g_v8_platform_initialized) {
        v8::V8::InitializeICUDefaultLocation("");
        v8::V8::InitializeExternalStartupData("");
        std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();
        if (!platform) {
            return sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
        }

        g_v8_platform = platform.release();
        v8::V8::InitializePlatform(g_v8_platform);
        v8::V8::Initialize();
        g_v8_platform_initialized = true;
    }

    return sl_status_ok();
}

SlV8Engine* sl_v8_backend(SlEngine* engine)
{
    return engine == nullptr ? nullptr : static_cast<SlV8Engine*>(engine->backend);
}

const SlV8Engine* sl_v8_backend_const(const SlEngine* engine)
{
    return engine == nullptr ? nullptr : static_cast<const SlV8Engine*>(engine->backend);
}

} // namespace

extern "C" SlStatus sl_engine_v8_create(const SlEngineOptions* options, SlArena* arena,
                                        SlEngine** out_engine)
{
    void* engine_memory = nullptr;
    SlEngine* engine = nullptr;
    SlV8Engine* backend = nullptr;
    SlStatus status;
    SlArenaMark mark = {};

    (void)options;

    if (arena == nullptr || out_engine == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_engine = nullptr;

    mark = sl_arena_mark(arena);

    status = sl_v8_platform_acquire();
    if (!sl_status_is_ok(status)) {
        return status;
    }

    backend = new (std::nothrow) SlV8Engine();
    if (backend == nullptr) {
        return sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
    }
    backend->owner_thread = std::this_thread::get_id();

    backend->allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
    if (backend->allocator == nullptr) {
        delete backend;
        return sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
    }

    v8::Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = backend->allocator;
    backend->isolate = v8::Isolate::New(create_params);
    if (backend->isolate == nullptr) {
        delete backend->allocator;
        delete backend;
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    {
        v8::Isolate::Scope isolate_scope(backend->isolate);
        v8::HandleScope handle_scope(backend->isolate);
        backend->isolate->SetData(0, backend);
        v8::Local<v8::Context> context = v8::Context::New(backend->isolate);
        if (!sl_v8_install_intrinsics(backend->isolate, context)) {
            backend->isolate->SetData(0, nullptr);
            backend->isolate->Dispose();
            delete backend->allocator;
            delete backend;
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
        backend->context.Reset(backend->isolate, context);
    }

    status = sl_arena_alloc(arena, sizeof(SlEngine), alignof(SlEngine), &engine_memory);
    if (!sl_status_is_ok(status)) {
        backend->handlers.clear();
        backend->context.Reset();
        backend->isolate->SetData(0, nullptr);
        backend->isolate->Dispose();
        delete backend->allocator;
        delete backend;
        SlStatus reset_status = sl_arena_reset_to(arena, mark);
        if (!sl_status_is_ok(reset_status)) {
            return reset_status;
        }

        return status;
    }

    engine = static_cast<SlEngine*>(engine_memory);
    engine->kind = SL_ENGINE_KIND_V8;
    engine->arena = arena;
    engine->active = true;
    engine->backend = backend;
    *out_engine = engine;
    return sl_status_ok();
}

extern "C" void sl_engine_v8_destroy(SlEngine* engine)
{
    SlV8Engine* backend = nullptr;

    if (engine == nullptr) {
        return;
    }

    backend = sl_v8_backend(engine);
    if (backend != nullptr && !sl_v8_on_owner_thread(backend)) {
        engine->active = false;
        return;
    }

    engine->active = false;
    engine->backend = nullptr;

    if (backend != nullptr) {
        if (backend->isolate != nullptr) {
            backend->handlers.clear();
            backend->context.Reset();
            backend->isolate->SetData(0, nullptr);
            backend->isolate->Dispose();
        }

        delete backend->allocator;
        delete backend;
    }
}

extern "C" SlStatus sl_engine_v8_info(const SlEngine* engine, SlEngineInfo* out_info)
{
    if (engine == nullptr || out_info == nullptr || sl_v8_backend_const(engine) == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (!sl_v8_on_owner_thread(sl_v8_backend_const(engine))) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    out_info->kind = SL_ENGINE_KIND_V8;
    out_info->name = sl_v8_literal("v8", sizeof("v8") - 1U);
    out_info->version = sl_v8_literal("enabled", sizeof("enabled") - 1U);
    return sl_status_ok();
}

extern "C" SlStatus sl_engine_v8_eval_source(SlEngine* engine, SlStr source_name, SlStr source,
                                             SlDiag* out_diag)
{
    SlV8Engine* backend = sl_v8_backend(engine);
    v8::Local<v8::String> source_string;
    SlStatus status;

    if (engine == nullptr || backend == nullptr || backend->isolate == nullptr ||
        source.length == 0U)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_v8_check_owner_thread(engine, backend, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    v8::Isolate* isolate = backend->isolate;
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = backend->context.Get(isolate);
    v8::Context::Scope context_scope(context);
    v8::TryCatch try_catch(isolate);
    v8::Local<v8::String> source_name_string;

    status = sl_v8_to_local_string(isolate, source, &source_string);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_v8_to_local_string(isolate, source_name, &source_name_string);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    v8::ScriptOrigin origin(source_name_string);
    v8::ScriptCompiler::Source script_source(source_string, origin);
    v8::MaybeLocal<v8::Script> maybe_script = v8::ScriptCompiler::Compile(context, &script_source);
    v8::Local<v8::Script> script;
    std::unordered_map<uint32_t, v8::Global<v8::Function>> pending_handlers;
    if (!maybe_script.ToLocal(&script)) {
        return sl_v8_write_exception_diag(
            engine, out_diag, SL_DIAG_ENGINE_COMPILE_ERROR, SL_STATUS_INVALID_STATE, isolate,
            context, try_catch, source_name, "JavaScript compile failed",
            sl_v8_literal(
                "Generated JavaScript locations are reported without source-map remapping.",
                sizeof("Generated JavaScript locations are reported without source-map "
                       "remapping.") -
                    1U));
    }

    backend->pending_handlers = &pending_handlers;
    if (script->Run(context).IsEmpty()) {
        backend->pending_handlers = nullptr;
        return sl_v8_write_exception_diag(
            engine, out_diag, SL_DIAG_ENGINE_EXCEPTION, SL_STATUS_INVALID_STATE, isolate, context,
            try_catch, source_name, "JavaScript evaluation failed",
            sl_v8_literal(
                "Generated JavaScript locations are reported without source-map remapping.",
                sizeof("Generated JavaScript locations are reported without source-map "
                       "remapping.") -
                    1U));
    }
    backend->pending_handlers = nullptr;

    for (auto& entry : pending_handlers) {
        backend->handlers.emplace(entry.first, std::move(entry.second));
    }

    return sl_status_ok();
}

static SlStatus sl_v8_convert_handler_result(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                             SlEngine* engine, SlArena* arena,
                                             v8::Local<v8::Value> js_result,
                                             SlEngineResult* out_result, SlDiag* out_diag);

extern "C" SlStatus sl_engine_v8_call_function0(SlEngine* engine, SlArena* arena,
                                                SlStr function_name, SlEngineResult* out_result,
                                                SlDiag* out_diag)
{
    SlV8Engine* backend = sl_v8_backend(engine);
    v8::Local<v8::String> name_string;
    SlStatus status;

    if (out_result == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_result = SlEngineResult{};

    if (engine == nullptr || backend == nullptr || backend->isolate == nullptr ||
        arena == nullptr || function_name.length == 0U)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_v8_check_owner_thread(engine, backend, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    v8::Isolate* isolate = backend->isolate;
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = backend->context.Get(isolate);
    v8::Context::Scope context_scope(context);
    v8::TryCatch try_catch(isolate);

    status = sl_v8_to_local_string(isolate, function_name, &name_string);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    v8::MaybeLocal<v8::Value> maybe_value = context->Global()->Get(context, name_string);
    v8::Local<v8::Value> value;
    if (!maybe_value.ToLocal(&value)) {
        return sl_v8_write_exception_diag(
            engine, out_diag, SL_DIAG_ENGINE_CALL_ERROR, SL_STATUS_INVALID_STATE, isolate, context,
            try_catch, sl_str_empty(), "JavaScript function lookup failed",
            sl_v8_literal("Use registered handler dispatch for compiler-generated app handlers.",
                          sizeof("Use registered handler dispatch for compiler-generated app "
                                 "handlers.") -
                              1U));
    }

    if (value->IsUndefined()) {
        std::string message = "JavaScript function was not found: " +
                              std::string(function_name.ptr, function_name.length);
        return sl_v8_write_diag_string(engine->arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR,
                                       SL_STATUS_INVALID_STATE, message, sl_str_empty(),
                                       sl_v8_literal("Use registered handler dispatch for "
                                                     "compiler-generated app handlers.",
                                                     sizeof("Use registered handler dispatch for "
                                                            "compiler-generated app handlers.") -
                                                         1U));
    }

    if (!value->IsFunction()) {
        std::string message = "JavaScript global is not callable: " +
                              std::string(function_name.ptr, function_name.length);
        return sl_v8_write_diag_string(
            engine->arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR, SL_STATUS_INVALID_STATE, message,
            sl_str_empty(),
            sl_v8_literal("Only global functions can be called by the compatibility smoke API.",
                          sizeof("Only global functions can be called by the compatibility smoke "
                                 "API.") -
                              1U));
    }

    v8::Local<v8::Function> function = value.As<v8::Function>();
    v8::MaybeLocal<v8::Value> maybe_result = function->Call(context, context->Global(), 0, nullptr);
    v8::Local<v8::Value> js_result;
    if (!maybe_result.ToLocal(&js_result)) {
        return sl_v8_write_exception_diag(
            engine, out_diag, SL_DIAG_ENGINE_EXCEPTION, SL_STATUS_INVALID_STATE, isolate, context,
            try_catch, sl_str_empty(), "JavaScript function threw",
            sl_v8_literal(
                "Generated JavaScript locations are reported without source-map remapping.",
                sizeof("Generated JavaScript locations are reported without source-map "
                       "remapping.") -
                    1U));
    }

    return sl_v8_convert_handler_result(isolate, context, engine, arena, js_result, out_result,
                                        out_diag);
}

static SlStatus sl_v8_convert_handler_result(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                             SlEngine* engine, SlArena* arena,
                                             v8::Local<v8::Value> js_result,
                                             SlEngineResult* out_result, SlDiag* out_diag)
{
    if (js_result->IsString()) {
        v8::String::Utf8Value utf8(isolate, js_result);
        SlStatus status;

        if (*utf8 == nullptr) {
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }

        status = sl_v8_copy_string(arena, std::string(*utf8, static_cast<size_t>(utf8.length())),
                                   &out_result->text);
        if (!sl_status_is_ok(status)) {
            return status;
        }

        out_result->kind = SL_ENGINE_RESULT_TEXT;
        out_result->response = sl_http_response_text(200U, out_result->text);
        return sl_status_ok();
    }

    if (!js_result->IsObject()) {
        return sl_v8_write_diag(
            engine->arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR, SL_STATUS_UNSUPPORTED,
            sl_v8_literal("JavaScript handler returned an unsupported result type",
                          sizeof("JavaScript handler returned an unsupported result type") - 1U),
            sl_str_empty(),
            sl_v8_literal("Return a string or a supported Results.* descriptor.",
                          sizeof("Return a string or a supported Results.* descriptor.") - 1U));
    }

    if (js_result->IsPromise()) {
        return sl_v8_write_diag(
            engine->arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR, SL_STATUS_UNSUPPORTED,
            sl_v8_literal("JavaScript handler returned a Promise",
                          sizeof("JavaScript handler returned a Promise") - 1U),
            sl_str_empty(),
            sl_v8_literal("Async handlers and Promise results are not supported in the alpha V8 "
                          "bridge; return a concrete string or Results.* descriptor.",
                          sizeof("Async handlers and Promise results are not supported in the "
                                 "alpha V8 bridge; return a concrete string or Results.* "
                                 "descriptor.") -
                              1U));
    }

    v8::Local<v8::Object> object = js_result.As<v8::Object>();
    if (!sl_v8_has_marker(isolate, context, object)) {
        return sl_v8_write_diag(
            engine->arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR, SL_STATUS_UNSUPPORTED,
            sl_v8_literal("JavaScript handler returned an unsupported result type",
                          sizeof("JavaScript handler returned an unsupported result type") - 1U),
            sl_str_empty(),
            sl_v8_literal("Result descriptors must include __sloppyResult: true.",
                          sizeof("Result descriptors must include __sloppyResult: true.") - 1U));
    }

    std::string kind;
    std::string content_type;
    uint16_t status_code = 0U;
    if (!sl_v8_get_object_string(isolate, context, object, "kind", &kind) ||
        !sl_v8_get_object_status(isolate, context, object, &status_code))
    {
        return sl_v8_write_diag(
            engine->arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR, SL_STATUS_INVALID_STATE,
            sl_v8_literal("JavaScript result descriptor is missing kind or status",
                          sizeof("JavaScript result descriptor is missing kind or status") - 1U),
            sl_str_empty(),
            sl_v8_literal("Return a supported Results.* descriptor with kind and status.",
                          sizeof("Return a supported Results.* descriptor with kind and status.") -
                              1U));
    }

    if (!sl_v8_http_status_supported(status_code)) {
        return sl_v8_write_diag(
            engine->arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR, SL_STATUS_INVALID_STATE,
            sl_v8_literal("JavaScript result descriptor has an unsupported status",
                          sizeof("JavaScript result descriptor has an unsupported status") - 1U),
            sl_str_empty(),
            sl_v8_literal("Supported response statuses are 200, 201, 202, 204, 400, 404, 405, "
                          "and 500.",
                          sizeof("Supported response statuses are 200, 201, 202, 204, 400, 404, "
                                 "405, and 500.") -
                              1U));
    }

    if (kind == "empty") {
        out_result->kind = SL_ENGINE_RESULT_NONE;
        out_result->response = sl_http_response_empty(status_code);
        return sl_status_ok();
    }

    if (!sl_v8_get_object_string(isolate, context, object, "contentType", &content_type)) {
        return sl_v8_write_diag(
            engine->arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR, SL_STATUS_INVALID_STATE,
            sl_v8_literal("JavaScript result descriptor is missing contentType",
                          sizeof("JavaScript result descriptor is missing contentType") - 1U),
            sl_str_empty(), sl_str_empty());
    }
    if (!sl_v8_header_value_safe(content_type)) {
        return sl_v8_write_diag(
            engine->arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR, SL_STATUS_INVALID_STATE,
            sl_v8_literal("JavaScript result descriptor has an invalid contentType",
                          sizeof("JavaScript result descriptor has an invalid contentType") - 1U),
            sl_str_empty(),
            sl_v8_literal("Content-Type must not contain CR or LF characters.",
                          sizeof("Content-Type must not contain CR or LF characters.") - 1U));
    }

    v8::Local<v8::String> body_key;
    v8::Local<v8::Value> body;
    if (!sl_status_is_ok(sl_v8_to_local_string(isolate, sl_str_from_cstr("body"), &body_key)) ||
        !object->Get(context, body_key).ToLocal(&body))
    {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    if (kind == "text") {
        if (!body->IsString()) {
            return sl_v8_write_diag(
                engine->arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR, SL_STATUS_INVALID_STATE,
                sl_v8_literal("Results.text body must be a string",
                              sizeof("Results.text body must be a string") - 1U),
                sl_str_empty(), sl_str_empty());
        }

        v8::String::Utf8Value utf8(isolate, body);
        SlStatus status;
        if (*utf8 == nullptr) {
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }

        status = sl_v8_copy_string(arena, std::string(*utf8, static_cast<size_t>(utf8.length())),
                                   &out_result->text);
        if (!sl_status_is_ok(status)) {
            return status;
        }

        out_result->kind = SL_ENGINE_RESULT_TEXT;
        out_result->response = sl_http_response_text(status_code, out_result->text);
        return sl_v8_copy_string(arena, content_type, &out_result->response.content_type);
    }

    if (kind == "json" || kind == "problem") {
        std::string json;
        SlBytes bytes = {nullptr, 0U};
        SlStatus status;

        if (body->IsUndefined()) {
            body = v8::Null(isolate);
        }

        if (!sl_v8_stringify_json(isolate, context, body, &json)) {
            return sl_v8_write_diag(
                engine->arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR, SL_STATUS_INVALID_STATE,
                sl_v8_literal("Results.json body could not be serialized",
                              sizeof("Results.json body could not be serialized") - 1U),
                sl_str_empty(), sl_str_empty());
        }

        status = sl_v8_copy_bytes(arena, json, &bytes);
        if (!sl_status_is_ok(status)) {
            return status;
        }

        out_result->kind = kind == "json" ? SL_ENGINE_RESULT_JSON : SL_ENGINE_RESULT_ERROR;
        out_result->response = kind == "json" ? sl_http_response_json(status_code, bytes)
                                              : sl_http_response_problem(status_code, bytes);
        return sl_v8_copy_string(arena, content_type, &out_result->response.content_type);
    }

    return sl_v8_write_diag(
        engine->arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR, SL_STATUS_UNSUPPORTED,
        sl_v8_literal("JavaScript result descriptor kind is unsupported",
                      sizeof("JavaScript result descriptor kind is unsupported") - 1U),
        sl_str_empty(),
        sl_v8_literal("Supported EPIC-23 result kinds are text, json, empty, and problem.",
                      sizeof("Supported EPIC-23 result kinds are text, json, empty, and "
                             "problem.") -
                          1U));
}

extern "C" SlStatus
sl_engine_v8_call_function_with_context(SlEngine* engine, SlArena* arena, SlStr function_name,
                                        const SlHttpRequestContext* request_context,
                                        SlEngineResult* out_result, SlDiag* out_diag)
{
    SlV8Engine* backend = sl_v8_backend(engine);
    v8::Local<v8::String> name_string;
    v8::Local<v8::Object> context_arg;
    SlStatus status;

    if (out_result == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_result = SlEngineResult{};

    if (engine == nullptr || backend == nullptr || backend->isolate == nullptr ||
        arena == nullptr || request_context == nullptr || function_name.length == 0U)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_v8_check_owner_thread(engine, backend, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    v8::Isolate* isolate = backend->isolate;
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = backend->context.Get(isolate);
    v8::Context::Scope context_scope(context);
    v8::TryCatch try_catch(isolate);

    status = sl_v8_to_local_string(isolate, function_name, &name_string);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    v8::MaybeLocal<v8::Value> maybe_value = context->Global()->Get(context, name_string);
    v8::Local<v8::Value> value;
    if (!maybe_value.ToLocal(&value)) {
        return sl_v8_write_exception_diag(
            engine, out_diag, SL_DIAG_ENGINE_CALL_ERROR, SL_STATUS_INVALID_STATE, isolate, context,
            try_catch, sl_str_empty(), "JavaScript function lookup failed",
            sl_v8_literal("EPIC-23 dispatch maps plan handler IDs to generated globals.",
                          sizeof("EPIC-23 dispatch maps plan handler IDs to generated globals.") -
                              1U));
    }

    if (value->IsUndefined()) {
        std::string message = "JavaScript function was not found: " +
                              std::string(function_name.ptr, function_name.length);
        return sl_v8_write_diag_string(engine->arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR,
                                       SL_STATUS_INVALID_STATE, message, sl_str_empty(),
                                       sl_v8_literal("Regenerate app artifacts or fix the plan "
                                                     "handler export name.",
                                                     sizeof("Regenerate app artifacts or fix the "
                                                            "plan handler export name.") -
                                                         1U));
    }

    if (!value->IsFunction()) {
        std::string message = "JavaScript global is not callable: " +
                              std::string(function_name.ptr, function_name.length);
        return sl_v8_write_diag_string(engine->arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR,
                                       SL_STATUS_INVALID_STATE, message, sl_str_empty(),
                                       sl_str_empty());
    }

    if (!sl_v8_make_context_object(isolate, context, request_context, &context_arg)) {
        return sl_v8_write_diag(
            engine->arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR, SL_STATUS_INVALID_STATE,
            sl_v8_literal("failed to materialize JavaScript request context",
                          sizeof("failed to materialize JavaScript request context") - 1U),
            sl_str_empty(), sl_str_empty());
    }

    v8::Local<v8::Function> function = value.As<v8::Function>();
    v8::Local<v8::Value> args[1] = {context_arg};
    v8::MaybeLocal<v8::Value> maybe_result = function->Call(context, context->Global(), 1, args);
    v8::Local<v8::Value> js_result;
    if (!maybe_result.ToLocal(&js_result)) {
        return sl_v8_write_exception_diag(
            engine, out_diag, SL_DIAG_ENGINE_EXCEPTION, SL_STATUS_INVALID_STATE, isolate, context,
            try_catch, sl_str_empty(), "JavaScript function threw",
            sl_v8_literal(
                "Generated JavaScript locations are reported without source-map remapping.",
                sizeof("Generated JavaScript locations are reported without source-map "
                       "remapping.") -
                    1U));
    }

    return sl_v8_convert_handler_result(isolate, context, engine, arena, js_result, out_result,
                                        out_diag);
}

extern "C" SlStatus sl_engine_v8_validate_registered_handlers(SlEngine* engine, const SlPlan* plan,
                                                              SlDiag* out_diag)
{
    SlV8Engine* backend = sl_v8_backend(engine);
    size_t index = 0U;
    SlStatus status;

    if (engine == nullptr || backend == nullptr || plan == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_v8_check_owner_thread(engine, backend, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (sl_plan_has_duplicate_handler_ids(plan)) {
        return sl_v8_write_diag(
            engine->arena, out_diag, SL_DIAG_DUPLICATE_HANDLER_ID, SL_STATUS_INVALID_ARGUMENT,
            sl_v8_literal("Plan contains duplicate handler IDs.",
                          sizeof("Plan contains duplicate handler IDs.") - 1U),
            sl_str_empty(),
            sl_v8_literal("Handler IDs must be unique before runtime registration validation.",
                          sizeof("Handler IDs must be unique before runtime registration "
                                 "validation.") -
                              1U));
    }

    for (index = 0U; index < plan->handler_count; index += 1U) {
        SlHandlerId handler_id = plan->handlers[index].id;
        if (!sl_handler_id_valid(handler_id) ||
            backend->handlers.find(handler_id) == backend->handlers.end())
        {
            return sl_v8_write_missing_registered_handler_diag(engine, out_diag, handler_id);
        }
    }

    return sl_status_ok();
}

extern "C" SlStatus sl_engine_v8_call_registered_handler_with_context(
    SlEngine* engine, SlArena* arena, SlHandlerId handler_id,
    const SlHttpRequestContext* request_context, SlEngineResult* out_result, SlDiag* out_diag)
{
    SlV8Engine* backend = sl_v8_backend(engine);
    v8::Local<v8::Object> context_arg;

    if (out_result == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_result = SlEngineResult{};

    if (engine == nullptr || backend == nullptr || backend->isolate == nullptr ||
        arena == nullptr || request_context == nullptr || !sl_handler_id_valid(handler_id))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    SlStatus status = sl_v8_check_owner_thread(engine, backend, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    auto handler = backend->handlers.find(handler_id);
    if (handler == backend->handlers.end()) {
        return sl_v8_write_missing_registered_handler_diag(engine, out_diag, handler_id);
    }

    v8::Isolate* isolate = backend->isolate;
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = backend->context.Get(isolate);
    v8::Context::Scope context_scope(context);
    v8::TryCatch try_catch(isolate);

    if (!sl_v8_make_context_object(isolate, context, request_context, &context_arg)) {
        return sl_v8_write_diag(
            engine->arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR, SL_STATUS_INVALID_STATE,
            sl_v8_literal("failed to materialize JavaScript request context",
                          sizeof("failed to materialize JavaScript request context") - 1U),
            sl_str_empty(), sl_str_empty());
    }

    v8::Local<v8::Function> function = handler->second.Get(isolate);
    v8::Local<v8::Value> args[1] = {context_arg};
    v8::MaybeLocal<v8::Value> maybe_result = function->Call(context, context->Global(), 1, args);
    v8::Local<v8::Value> js_result;
    if (!maybe_result.ToLocal(&js_result)) {
        return sl_v8_write_exception_diag(
            engine, out_diag, SL_DIAG_ENGINE_EXCEPTION, SL_STATUS_INVALID_STATE, isolate, context,
            try_catch, sl_str_empty(), "JavaScript handler threw",
            sl_v8_literal(
                "Generated JavaScript locations are reported without source-map remapping.",
                sizeof("Generated JavaScript locations are reported without source-map "
                       "remapping.") -
                    1U));
    }

    return sl_v8_convert_handler_result(isolate, context, engine, arena, js_result, out_result,
                                        out_diag);
}

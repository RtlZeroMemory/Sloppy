/*
 * src/engine/v8/engine_v8.cc
 *
 * Implements the V8-backed engine path behind Sloppy's engine-neutral C ABI. It owns
 * isolate/context lifetime, installs engine-level intrinsics, evaluates classic JavaScript
 * source strings, calls handlers, and orchestrates bounded Promise microtask drains. Framework
 * HTTP context/result conversion and provider-specific JS-to-native bridges live in sibling
 * modules under src/engine/v8/.
 *
 * Safety invariants:
 * - no V8 handle, value, or type escapes this file;
 * - JS never receives raw native pointers;
 * - result strings are copied out of V8 before returning to C;
 * - Promise settlement and microtask drains happen only on the isolate owner thread;
 * - one owner thread creates and enters each isolate/context; wrong-thread entry fails
 *   before touching V8 state;
 * - V8's required process-wide platform state is initialized once, kept for process
 *   lifetime, and private to this module until an explicit runtime shutdown task exists.
 *
 * Tests: tests/unit/engine/test_v8_smoke.c when SLOPPY_ENABLE_V8 is enabled.
 */
#include "engine_v8_internal.h"
#include "string_interop.h"

#include <libplatform/libplatform.h>
#include <v8.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <string>

namespace {

std::mutex g_v8_platform_mutex;
v8::Platform* g_v8_platform = nullptr;
bool g_v8_platform_initialized = false;
constexpr size_t kSlV8MicrotaskDrainLimit = 1024U;

struct SlV8MicrotaskDrainGuard
{
    size_t ran = 0U;
    bool exceeded = false;
};

thread_local SlV8MicrotaskDrainGuard* g_sl_v8_microtask_guard = nullptr;

SlStr sl_v8_literal(const char* ptr, size_t length)
{
    return sl_str_from_parts(ptr, length);
}

SlStr sl_v8_str_from_string(const std::string& str)
{
    return sl_str_from_parts(str.data(), str.size());
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

    status = sl_v8_std_string_copy_to_arena(arena, message, &copied_message);

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

    status = sl_v8_std_string_copy_to_arena(arena, message, &copied_message);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_v8_write_diag_with_span(arena, out_diag, code, failure_code, copied_message, span,
                                      hint, stack_summary);
}

std::string sl_v8_value_to_string(v8::Isolate* isolate, v8::Local<v8::Value> value)
{
    std::string text;

    if (!sl_v8_std_string_from_value(isolate, value, &text)) {
        return std::string("JavaScript exception");
    }

    return text;
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

void sl_v8_microtask_drain_promise_hook(v8::PromiseHookType type, v8::Local<v8::Promise> promise,
                                        v8::Local<v8::Value> parent)
{
    SlV8MicrotaskDrainGuard* guard = g_sl_v8_microtask_guard;

    (void)promise;
    (void)parent;
    if (type != v8::PromiseHookType::kBefore || guard == nullptr || guard->exceeded) {
        return;
    }

    guard->ran += 1U;
    if (guard->ran <= kSlV8MicrotaskDrainLimit) {
        return;
    }

    guard->exceeded = true;
    if (v8::Isolate* isolate = v8::Isolate::TryGetCurrent()) {
        isolate->TerminateExecution();
    }
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

SlStatus sl_v8_write_cancelled_diag(SlEngine* engine, const SlCancellationToken* cancellation,
                                    SlDiag* out_diag)
{
    SlCancellationReason reason = sl_cancellation_token_reason(cancellation);
    SlStatusCode status_code = sl_cancellation_status_code(reason);
    SlDiagCode diag_code = reason == SL_CANCELLATION_REASON_BACKPRESSURE
                               ? SL_DIAG_ENGINE_BACKPRESSURE
                               : SL_DIAG_ENGINE_CANCELLED;
    SlStr reason_name = sl_cancellation_reason_name(reason);
    std::string message = "JavaScript handler request was cancelled";

    if (reason == SL_CANCELLATION_REASON_DEADLINE_EXCEEDED) {
        message = "JavaScript handler request deadline was exceeded";
    }
    else if (reason == SL_CANCELLATION_REASON_BACKPRESSURE) {
        message = "JavaScript handler request was rejected by backpressure";
    }
    else if (reason == SL_CANCELLATION_REASON_SHUTDOWN) {
        message = "JavaScript handler request was cancelled during shutdown";
    }

    if (cancellation != nullptr && !sl_str_is_empty(cancellation->detail)) {
        message += ": ";
        message.append(cancellation->detail.ptr, cancellation->detail.length);
    }

    return sl_v8_write_diag_string(
        engine->arena, out_diag, diag_code, status_code, message, sl_str_empty(),
        reason == SL_CANCELLATION_REASON_NONE ? sl_str_empty() : reason_name);
}

SlStatus sl_v8_check_cancelled(SlEngine* engine, const SlCancellationToken* cancellation,
                               SlDiag* out_diag)
{
    if (!sl_cancellation_token_is_cancelled(cancellation)) {
        return sl_status_ok();
    }

    return sl_v8_write_cancelled_diag(engine, cancellation, out_diag);
}

SlStatus sl_v8_write_microtask_limit_diag(SlEngine* engine, SlDiag* out_diag)
{
    std::string message = "JavaScript microtask drain exceeded bounded checkpoint limit (";
    message += std::to_string(kSlV8MicrotaskDrainLimit);
    message += ")";

    return sl_v8_write_diag_string(
        engine->arena, out_diag, SL_DIAG_ENGINE_PROMISE_PENDING, SL_STATUS_DEADLINE_EXCEEDED,
        message, sl_str_empty(),
        sl_v8_literal(
            "Break recursive Promise microtask chains with a real async boundary; "
            "timers and native async completion queues are future ENGINE-12 work.",
            sizeof("Break recursive Promise microtask chains with a real async boundary; "
                   "timers and native async completion queues are future ENGINE-12 work.") -
                1U));
}

SlStatus sl_v8_drain_microtasks(SlEngine* engine, v8::Isolate* isolate,
                                v8::Local<v8::Context> context, SlDiag* out_diag)
{
    v8::TryCatch try_catch(isolate);
    SlV8MicrotaskDrainGuard guard;
    SlV8MicrotaskDrainGuard* previous_guard = g_sl_v8_microtask_guard;

    g_sl_v8_microtask_guard = &guard;
    isolate->SetPromiseHook(sl_v8_microtask_drain_promise_hook);

    isolate->PerformMicrotaskCheckpoint();
    isolate->SetPromiseHook(nullptr);
    g_sl_v8_microtask_guard = previous_guard;

    if (guard.exceeded) {
        isolate->CancelTerminateExecution();
        return sl_v8_write_microtask_limit_diag(engine, out_diag);
    }

    if (try_catch.HasCaught()) {
        return sl_v8_write_exception_diag(
            engine, out_diag, SL_DIAG_ENGINE_EXCEPTION, SL_STATUS_INVALID_STATE, isolate, context,
            try_catch, sl_str_empty(), "JavaScript microtask failed",
            sl_v8_literal("V8 microtasks are drained only on the owning engine thread.",
                          sizeof("V8 microtasks are drained only on the owning engine thread.") -
                              1U));
    }

    return sl_status_ok();
}

void sl_v8_register_handler_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);

    if (backend == nullptr || args.Length() != 2) {
        (void)sl_v8_throw_type_error_from_native_view(
            backend,
            sl_v8_literal(
                "__sloppy_register_handler requires handler ID and handler function",
                sizeof("__sloppy_register_handler requires handler ID and handler function") - 1U));
        return;
    }

    v8::Local<v8::Value> id_value = args[0];
    v8::Local<v8::Value> handler_value = args[1];

    if (!id_value->IsUint32()) {
        (void)sl_v8_throw_type_error_from_native_view(
            backend,
            sl_v8_literal(
                "__sloppy_register_handler handler ID must be a positive integer",
                sizeof("__sloppy_register_handler handler ID must be a positive integer") - 1U));
        return;
    }

    uint32_t handler_id = id_value.As<v8::Uint32>()->Value();
    if (handler_id == 0U) {
        (void)sl_v8_throw_type_error_from_native_view(
            backend,
            sl_v8_literal(
                "__sloppy_register_handler handler ID must be a positive integer",
                sizeof("__sloppy_register_handler handler ID must be a positive integer") - 1U));
        return;
    }

    if (!handler_value->IsFunction()) {
        (void)sl_v8_throw_type_error_from_native_view(
            backend,
            sl_v8_literal("__sloppy_register_handler handler must be callable",
                          sizeof("__sloppy_register_handler handler must be callable") - 1U));
        return;
    }

    if (backend->pending_handlers == nullptr) {
        (void)sl_v8_throw_error_from_native_view(
            backend,
            sl_v8_literal("__sloppy_register_handler is only valid during app evaluation",
                          sizeof("__sloppy_register_handler is only valid during app evaluation") -
                              1U));
        return;
    }

    if (backend->handlers.find(handler_id) != backend->handlers.end() ||
        backend->pending_handlers->find(handler_id) != backend->pending_handlers->end())
    {
        (void)sl_v8_throw_error_from_native_view(
            backend, sl_v8_literal("__sloppy_register_handler duplicate handler ID",
                                   sizeof("__sloppy_register_handler duplicate handler ID") - 1U));
        return;
    }

    backend->pending_handlers->emplace(
        handler_id, v8::Global<v8::Function>(isolate, handler_value.As<v8::Function>()));
    args.GetReturnValue().Set(v8::Undefined(isolate));
}

bool sl_v8_install_intrinsics(SlV8Engine* backend, v8::Local<v8::Context> context)
{
    v8::Isolate* isolate = backend == nullptr ? nullptr : backend->isolate;
    v8::Local<v8::String> name;
    v8::Local<v8::FunctionTemplate> function_template =
        v8::FunctionTemplate::New(isolate, sl_v8_register_handler_callback);
    v8::Local<v8::Function> function;
    v8::Local<v8::String> sloppy_key;
    v8::Local<v8::String> data_key;
    v8::Local<v8::Object> sloppy = v8::Object::New(isolate);
    v8::Local<v8::Object> data = v8::Object::New(isolate);

    if (!sl_status_is_ok(sl_v8_string_from_native_view(
            backend, sl_str_from_cstr("__sloppy_register_handler"), &name)) ||
        !function_template->GetFunction(context).ToLocal(&function))
    {
        return false;
    }

    if (!context->Global()->Set(context, name, function).FromMaybe(false)) {
        return false;
    }

    if (!sl_status_is_ok(
            sl_v8_string_from_native_view(backend, sl_str_from_cstr("__sloppy"), &sloppy_key)) ||
        !sl_status_is_ok(
            sl_v8_string_from_native_view(backend, sl_str_from_cstr("data"), &data_key)))
    {
        return false;
    }

    if (!sl_v8_install_provider_intrinsics(isolate, context, data) ||
        !sloppy->Set(context, data_key, data).FromMaybe(false) ||
        !context->Global()->Set(context, sloppy_key, sloppy).FromMaybe(false))
    {
        return false;
    }

    return true;
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
    backend->plan = options == nullptr ? nullptr : options->plan;
    backend->capabilities = options == nullptr ? nullptr : options->capabilities;

    status = sl_resource_table_init(&backend->resources, backend->resource_entries.data(),
                                    backend->resource_entries.size());
    if (!sl_status_is_ok(status)) {
        delete backend;
        return status;
    }

    backend->allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
    if (backend->allocator == nullptr) {
        sl_resource_table_dispose(&backend->resources);
        delete backend;
        return sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
    }

    v8::Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = backend->allocator;
    backend->isolate = v8::Isolate::New(create_params);
    if (backend->isolate == nullptr) {
        sl_resource_table_dispose(&backend->resources);
        delete backend->allocator;
        delete backend;
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    {
        v8::Isolate::Scope isolate_scope(backend->isolate);
        v8::HandleScope handle_scope(backend->isolate);
        backend->isolate->SetData(0, backend);
        backend->isolate->SetMicrotasksPolicy(v8::MicrotasksPolicy::kExplicit);
        v8::Local<v8::Context> context = v8::Context::New(backend->isolate);
        v8::Context::Scope context_scope(context);
        if (!sl_v8_install_intrinsics(backend, context)) {
            backend->isolate->SetData(0, nullptr);
            backend->isolate->Dispose();
            sl_resource_table_dispose(&backend->resources);
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
        sl_resource_table_dispose(&backend->resources);
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
        return;
    }

    engine->active = false;
    engine->backend = nullptr;

    if (backend != nullptr) {
        sl_resource_table_dispose(&backend->resources);

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

    status = sl_v8_string_from_native_view(backend, source, &source_string);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_v8_string_from_native_view(backend, source_name, &source_name_string);
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

    status = sl_v8_drain_microtasks(engine, isolate, context, out_diag);
    backend->pending_handlers = nullptr;
    if (!sl_status_is_ok(status)) {
        return status;
    }

    for (auto& entry : pending_handlers) {
        backend->handlers.emplace(entry.first, std::move(entry.second));
    }

    return sl_status_ok();
}

static SlStatus sl_v8_convert_handler_result(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                             SlEngine* engine, SlArena* arena,
                                             v8::Local<v8::Value> js_result,
                                             const SlCancellationToken* cancellation,
                                             SlEngineResult* out_result, SlDiag* out_diag);

static SlStatus sl_v8_write_promise_rejection_diag(SlEngine* engine, v8::Isolate* isolate,
                                                   v8::Local<v8::Value> reason, SlDiag* out_diag)
{
    std::string message = "JavaScript handler Promise rejected";
    std::string reason_text = sl_v8_maybe_value_to_string(isolate, reason);

    if (!reason_text.empty()) {
        message += ": ";
        message += reason_text;
    }

    return sl_v8_write_diag_string(
        engine->arena, out_diag, SL_DIAG_ENGINE_PROMISE_REJECTION, SL_STATUS_INVALID_STATE, message,
        sl_str_empty(),
        sl_v8_literal("Rejected async handlers produce a safe error response.",
                      sizeof("Rejected async handlers produce a safe error response.") - 1U));
}

static SlStatus sl_v8_write_pending_promise_diag(SlEngine* engine, SlDiag* out_diag)
{
    return sl_v8_write_diag(
        engine->arena, out_diag, SL_DIAG_ENGINE_PROMISE_PENDING, SL_STATUS_DEADLINE_EXCEEDED,
        sl_v8_literal("JavaScript handler Promise did not settle during bounded microtask drain",
                      sizeof("JavaScript handler Promise did not settle during bounded microtask "
                             "drain") -
                          1U),
        sl_str_empty(),
        sl_v8_literal("This V8 runtime drains Promise microtasks but does not implement timers, "
                      "fetch, Node APIs, or native async completion queues for handlers.",
                      sizeof("This V8 runtime drains Promise microtasks but does not implement "
                             "timers, fetch, Node APIs, or native async completion queues for "
                             "handlers.") -
                          1U));
}

static SlStatus sl_v8_convert_promise_result(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                             SlEngine* engine, SlArena* arena,
                                             v8::Local<v8::Promise> promise,
                                             const SlCancellationToken* cancellation,
                                             SlEngineResult* out_result, SlDiag* out_diag)
{
    SlStatus status = sl_v8_check_cancelled(engine, cancellation, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (promise->State() == v8::Promise::kPending) {
        status = sl_v8_drain_microtasks(engine, isolate, context, out_diag);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    status = sl_v8_check_cancelled(engine, cancellation, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (promise->State() == v8::Promise::kPending) {
        return sl_v8_write_pending_promise_diag(engine, out_diag);
    }

    if (promise->State() == v8::Promise::kRejected) {
        return sl_v8_write_promise_rejection_diag(engine, isolate, promise->Result(), out_diag);
    }

    return sl_v8_convert_handler_result(isolate, context, engine, arena, promise->Result(),
                                        cancellation, out_result, out_diag);
}

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

    status = sl_v8_string_from_native_view(backend, function_name, &name_string);
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

    status = sl_v8_drain_microtasks(engine, isolate, context, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_v8_convert_handler_result(isolate, context, engine, arena, js_result, nullptr,
                                        out_result, out_diag);
}

static SlStatus sl_v8_convert_handler_result(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                             SlEngine* engine, SlArena* arena,
                                             v8::Local<v8::Value> js_result,
                                             const SlCancellationToken* cancellation,
                                             SlEngineResult* out_result, SlDiag* out_diag)
{
    SlStatus cancel_status = sl_v8_check_cancelled(engine, cancellation, out_diag);
    if (!sl_status_is_ok(cancel_status)) {
        return cancel_status;
    }

    if (js_result->IsPromise()) {
        return sl_v8_convert_promise_result(isolate, context, engine, arena,
                                            js_result.As<v8::Promise>(), cancellation, out_result,
                                            out_diag);
    }

    return sl_v8_convert_http_handler_result(isolate, context, engine, arena, js_result, out_result,
                                             out_diag);
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
    status = sl_v8_check_cancelled(engine, request_context->cancellation, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    v8::Isolate* isolate = backend->isolate;
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = backend->context.Get(isolate);
    v8::Context::Scope context_scope(context);
    v8::TryCatch try_catch(isolate);

    status = sl_v8_string_from_native_view(backend, function_name, &name_string);
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

    if (!sl_v8_make_http_context_object(isolate, context, request_context, &context_arg)) {
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

    status = sl_v8_drain_microtasks(engine, isolate, context, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_v8_convert_handler_result(isolate, context, engine, arena, js_result,
                                        request_context->cancellation, out_result, out_diag);
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
    status = sl_v8_check_cancelled(engine, request_context->cancellation, out_diag);
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

    if (!sl_v8_make_http_context_object(isolate, context, request_context, &context_arg)) {
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

    status = sl_v8_drain_microtasks(engine, isolate, context, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_v8_convert_handler_result(isolate, context, engine, arena, js_result,
                                        request_context->cancellation, out_result, out_diag);
}

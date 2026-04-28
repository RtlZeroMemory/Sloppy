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
 *   enforcement yet;
 * - V8's required process-wide platform state is initialized once, kept for process
 *   lifetime, and private to this module until an explicit runtime shutdown task exists.
 *
 * Tests: tests/unit/engine/test_v8_smoke.c when SLOPPY_ENABLE_V8 is enabled.
 */
#include "../engine_internal.h"

#include <libplatform/libplatform.h>
#include <v8.h>

#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>

namespace {

struct SlV8Engine
{
    v8::ArrayBuffer::Allocator* allocator = nullptr;
    v8::Isolate* isolate = nullptr;
    v8::Global<v8::Context> context;
};

std::mutex g_v8_platform_mutex;
std::unique_ptr<v8::Platform> g_v8_platform;
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
    SlStr copied_message = {0};
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
    SlStr copied_message = {0};
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

SlStatus sl_v8_platform_acquire(void)
{
    std::lock_guard<std::mutex> lock(g_v8_platform_mutex);

    if (!g_v8_platform_initialized) {
        v8::V8::InitializeICUDefaultLocation(nullptr);
        v8::V8::InitializeExternalStartupData(nullptr);
        g_v8_platform = v8::platform::NewDefaultPlatform();
        if (!g_v8_platform) {
            return sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
        }

        v8::V8::InitializePlatform(g_v8_platform.get());
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
    SlArenaMark mark = {0};

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
        v8::Local<v8::Context> context = v8::Context::New(backend->isolate);
        backend->context.Reset(backend->isolate, context);
    }

    status = sl_arena_alloc(arena, sizeof(SlEngine), alignof(SlEngine), &engine_memory);
    if (!sl_status_is_ok(status)) {
        backend->context.Reset();
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
    SlV8Engine* backend = sl_v8_backend(engine);

    if (engine == nullptr) {
        return;
    }

    engine->active = false;
    engine->backend = nullptr;

    if (backend != nullptr) {
        if (backend->isolate != nullptr) {
            backend->context.Reset();
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

    v8::ScriptOrigin origin(isolate, source_name_string);
    v8::ScriptCompiler::Source script_source(source_string, origin);
    v8::MaybeLocal<v8::Script> maybe_script = v8::ScriptCompiler::Compile(context, &script_source);
    v8::Local<v8::Script> script;
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

    if (script->Run(context).IsEmpty()) {
        return sl_v8_write_exception_diag(
            engine, out_diag, SL_DIAG_ENGINE_EXCEPTION, SL_STATUS_INVALID_STATE, isolate, context,
            try_catch, source_name, "JavaScript evaluation failed",
            sl_v8_literal(
                "Generated JavaScript locations are reported without source-map remapping.",
                sizeof("Generated JavaScript locations are reported without source-map "
                       "remapping.") -
                    1U));
    }

    return sl_status_ok();
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
            sl_v8_literal("TASK 08 will map plan handler IDs to registered functions.",
                          sizeof("TASK 08 will map plan handler IDs to registered functions.") -
                              1U));
    }

    if (value->IsUndefined()) {
        std::string message = "JavaScript function was not found: " +
                              std::string(function_name.ptr, function_name.length);
        return sl_v8_write_diag_string(engine->arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR,
                                       SL_STATUS_INVALID_STATE, message, sl_str_empty(),
                                       sl_v8_literal("TASK 08 will map plan handler IDs to "
                                                     "registered functions.",
                                                     sizeof("TASK 08 will map plan handler IDs to "
                                                            "registered functions.") -
                                                         1U));
    }

    if (!value->IsFunction()) {
        std::string message = "JavaScript global is not callable: " +
                              std::string(function_name.ptr, function_name.length);
        return sl_v8_write_diag_string(
            engine->arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR, SL_STATUS_INVALID_STATE, message,
            sl_str_empty(),
            sl_v8_literal("Only global functions can be called by the "
                          "TASK 07 smoke API.",
                          sizeof("Only global functions can be called by "
                                 "the TASK 07 smoke API.") -
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

    if (!js_result->IsString()) {
        return sl_v8_write_diag(
            engine->arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR, SL_STATUS_UNSUPPORTED,
            sl_v8_literal("JavaScript function returned an unsupported result type",
                          sizeof("JavaScript function returned an unsupported result type") - 1U),
            sl_str_empty(),
            sl_v8_literal("TASK 07.C only supports copied string results.",
                          sizeof("TASK 07.C only supports copied string results.") - 1U));
    }

    v8::String::Utf8Value utf8(isolate, js_result);
    if (*utf8 == nullptr) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    status = sl_v8_copy_string(arena, std::string(*utf8, static_cast<size_t>(utf8.length())),
                               &out_result->text);
    if (!sl_status_is_ok(status)) {
        *out_result = SlEngineResult{};
        return status;
    }

    out_result->kind = SL_ENGINE_RESULT_TEXT;
    return sl_status_ok();
}

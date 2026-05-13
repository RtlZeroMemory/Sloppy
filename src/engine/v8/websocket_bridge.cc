#include "engine_v8_internal.h"

#include "string_interop.h"

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <deque>
#include <limits>
#include <new>
#include <thread>

namespace {

struct SlV8WebSocketMessage
{
    SlWebSocketOpcode opcode = SL_WEBSOCKET_OPCODE_TEXT;
    std::string text;
    std::vector<unsigned char> bytes;
};

} // namespace

struct SlEngineWebSocketSession
{
    SlV8Engine* backend = nullptr;
    SlEngineWebSocketBridge bridge = {};
    bool closed = false;
    bool accepted = false;
    std::deque<SlV8WebSocketMessage> messages;
    v8::Global<v8::Promise::Resolver> pending_next;
    v8::Global<v8::Promise> handler_promise;
    v8::Global<v8::Object> socket;
};

namespace {

SlEngineWebSocketSession* ws_session_from_args(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    if (args.Data().IsEmpty() || !args.Data()->IsExternal()) {
        return nullptr;
    }
    return static_cast<SlEngineWebSocketSession*>(
        args.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
}

bool ws_make_string(v8::Isolate* isolate, const char* text, v8::Local<v8::String>* out)
{
    return v8::String::NewFromUtf8(isolate, text, v8::NewStringType::kNormal).ToLocal(out);
}

bool ws_make_string_from_bytes(v8::Isolate* isolate, const char* text, size_t length,
                               v8::Local<v8::String>* out)
{
    if (text == nullptr && length != 0U) {
        return false;
    }
    if (length > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    return v8::String::NewFromUtf8(isolate, text, v8::NewStringType::kNormal,
                                   static_cast<int>(length))
        .ToLocal(out);
}

bool ws_set(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> object,
            const char* name, v8::Local<v8::Value> value)
{
    v8::Local<v8::String> key;
    return ws_make_string(isolate, name, &key) && object->Set(context, key, value).FromMaybe(false);
}

bool ws_set_function(v8::Isolate* isolate, v8::Local<v8::Context> context,
                     v8::Local<v8::Object> object, const char* name, v8::FunctionCallback callback,
                     SlEngineWebSocketSession* session)
{
    v8::Local<v8::String> key;
    v8::Local<v8::Function> function;
    v8::Local<v8::External> data =
        v8::External::New(isolate, session, v8::kExternalPointerTypeTagDefault);
    if (!ws_make_string(isolate, name, &key) || !v8::FunctionTemplate::New(isolate, callback, data)
                                                     ->GetFunction(context)
                                                     .ToLocal(&function))
    {
        return false;
    }
    return object->Set(context, key, function).FromMaybe(false);
}

bool ws_resolved_promise(v8::Isolate* isolate, v8::Local<v8::Context> context,
                         v8::Local<v8::Value> value, v8::Local<v8::Promise>* out)
{
    (void)isolate;
    v8::Local<v8::Promise::Resolver> resolver;
    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver) ||
        !resolver->Resolve(context, value).FromMaybe(false))
    {
        return false;
    }
    *out = resolver->GetPromise();
    return true;
}

void ws_return_resolved(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        const v8::FunctionCallbackInfo<v8::Value>& args, v8::Local<v8::Value> value)
{
    v8::Local<v8::Promise> promise;
    if (!ws_resolved_promise(isolate, context, value, &promise)) {
        isolate->ThrowException(v8::Exception::Error(
            v8::String::NewFromUtf8Literal(isolate, "SLOPPY_E_WEBSOCKET_PROMISE_FAILED")));
        return;
    }
    args.GetReturnValue().Set(promise);
}

bool ws_rejected_promise(v8::Isolate* isolate, v8::Local<v8::Context> context, const char* message,
                         v8::Local<v8::Promise>* out)
{
    v8::Local<v8::Promise::Resolver> resolver;
    v8::Local<v8::String> text;
    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver) ||
        !ws_make_string(isolate, message, &text) ||
        !resolver->Reject(context, v8::Exception::Error(text)).FromMaybe(false))
    {
        return false;
    }
    *out = resolver->GetPromise();
    return true;
}

void ws_return_rejected(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        const v8::FunctionCallbackInfo<v8::Value>& args, const char* message)
{
    v8::Local<v8::Promise> promise;
    if (!ws_rejected_promise(isolate, context, message, &promise)) {
        isolate->ThrowException(v8::Exception::Error(
            v8::String::NewFromUtf8Literal(isolate, "SLOPPY_E_WEBSOCKET_PROMISE_FAILED")));
        return;
    }
    args.GetReturnValue().Set(promise);
}

bool ws_message_value(v8::Isolate* isolate, v8::Local<v8::Context> context,
                      const SlV8WebSocketMessage& message, v8::Local<v8::Value>* out)
{
    v8::Local<v8::Object> value = v8::Object::New(isolate);
    v8::Local<v8::String> text;

    if (message.opcode == SL_WEBSOCKET_OPCODE_TEXT) {
        v8::Local<v8::String> kind = v8::String::NewFromUtf8Literal(isolate, "text");
        if (!ws_set(isolate, context, value, "kind", kind) ||
            !ws_set(isolate, context, value, "type", kind) ||
            !ws_make_string_from_bytes(isolate, message.text.data(), message.text.size(), &text) ||
            !ws_set(isolate, context, value, "text", text))
        {
            return false;
        }
    }
    else {
        v8::Local<v8::ArrayBuffer> buffer = v8::ArrayBuffer::New(isolate, message.bytes.size());
        if (message.bytes.size() != 0U) {
            std::copy(message.bytes.begin(), message.bytes.end(),
                      static_cast<unsigned char*>(buffer->Data()));
        }
        v8::Local<v8::Uint8Array> bytes = v8::Uint8Array::New(buffer, 0, message.bytes.size());
        v8::Local<v8::String> kind = v8::String::NewFromUtf8Literal(isolate, "binary");
        if (!ws_set(isolate, context, value, "kind", kind) ||
            !ws_set(isolate, context, value, "type", kind) ||
            !ws_set(isolate, context, value, "bytes", bytes))
        {
            return false;
        }
    }
    *out = value;
    return true;
}

bool ws_next_result(v8::Isolate* isolate, v8::Local<v8::Context> context,
                    v8::Local<v8::Value> value, bool done, v8::Local<v8::Object>* out)
{
    v8::Local<v8::Object> result = v8::Object::New(isolate);
    if (!ws_set(isolate, context, result, "done", v8::Boolean::New(isolate, done))) {
        return false;
    }
    if (!done && !ws_set(isolate, context, result, "value", value)) {
        return false;
    }
    *out = result;
    return true;
}

bool ws_resolve_next(SlEngineWebSocketSession* session, v8::Isolate* isolate,
                     v8::Local<v8::Context> context)
{
    if (session == nullptr || session->pending_next.IsEmpty()) {
        return true;
    }

    v8::Local<v8::Promise::Resolver> resolver = session->pending_next.Get(isolate);
    v8::Local<v8::Object> result;
    if (session->closed && session->messages.empty()) {
        if (!ws_next_result(isolate, context, v8::Undefined(isolate), true, &result)) {
            return false;
        }
    }
    else if (!session->messages.empty()) {
        v8::Local<v8::Value> value;
        SlV8WebSocketMessage message = std::move(session->messages.front());
        session->messages.pop_front();
        if (!ws_message_value(isolate, context, message, &value) ||
            !ws_next_result(isolate, context, value, false, &result))
        {
            return false;
        }
    }
    else {
        return true;
    }
    session->pending_next.Reset();
    return resolver->Resolve(context, result).FromMaybe(false);
}

SlV8Engine* ws_backend(SlEngine* engine)
{
    return engine == nullptr ? nullptr : static_cast<SlV8Engine*>(engine->backend);
}

SlStr ws_literal(const char* ptr, size_t length)
{
    return sl_str_from_parts(ptr, length);
}

SlStatus ws_write_diag(SlArena* arena, SlDiag* out_diag, SlDiagCode code, SlStatusCode failure_code,
                       SlStr message, SlStr detail, SlStr hint)
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
    if (!sl_str_is_empty(detail)) {
        status = sl_diag_builder_add_related(&builder, sl_source_span_unknown(), detail);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    if (!sl_str_is_empty(hint)) {
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

SlStatus ws_write_missing_registered_handler_diag(SlEngine* engine, SlDiag* out_diag,
                                                  SlHandlerId handler_id)
{
    std::string message = "app plan references unregistered handler ID ";
    message += std::to_string(handler_id);
    return ws_write_diag(
        engine->arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR, SL_STATUS_INVALID_STATE,
        sl_str_from_parts(message.data(), message.size()), sl_str_empty(),
        ws_literal("Generated app modules must call __sloppy_register_handler(id, handler).",
                   sizeof("Generated app modules must call "
                          "__sloppy_register_handler(id, handler).") -
                       1U));
}

SlStatus ws_check_owner_thread(SlEngine* engine, const SlV8Engine* backend, SlDiag* out_diag)
{
    if (backend != nullptr && backend->owner_thread == std::this_thread::get_id()) {
        return sl_status_ok();
    }
    return ws_write_diag(
        engine->arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR, SL_STATUS_INVALID_STATE,
        ws_literal("V8 engine entered from a non-owner thread",
                   sizeof("V8 engine entered from a non-owner thread") - 1U),
        sl_str_empty(),
        ws_literal("Create, evaluate, call, validate, and dispose a V8 engine on its owner thread.",
                   sizeof("Create, evaluate, call, validate, and dispose a V8 engine on its owner "
                          "thread.") -
                       1U));
}

SlStatus ws_check_cancelled(SlEngine* engine, const SlCancellationToken* cancellation,
                            SlDiag* out_diag)
{
    if (!sl_cancellation_token_is_cancelled(cancellation)) {
        return sl_status_ok();
    }

    SlCancellationReason reason = sl_cancellation_token_reason(cancellation);
    SlStatusCode status_code = sl_cancellation_status_code(reason);
    SlDiagCode diag_code = reason == SL_CANCELLATION_REASON_BACKPRESSURE
                               ? SL_DIAG_ENGINE_BACKPRESSURE
                               : SL_DIAG_ENGINE_CANCELLED;
    SlStr reason_name = sl_cancellation_reason_name(reason);
    return ws_write_diag(
        engine->arena, out_diag, diag_code, status_code,
        ws_literal("JavaScript WebSocket handler request was cancelled",
                   sizeof("JavaScript WebSocket handler request was cancelled") - 1U),
        sl_str_empty(), reason_name);
}

SlStatus ws_write_exception_diag(SlEngine* engine, SlDiag* out_diag, SlDiagCode code,
                                 SlStatusCode failure_code, v8::Isolate* isolate,
                                 v8::TryCatch& try_catch, const char* fallback_message, SlStr hint)
{
    std::string message =
        fallback_message == nullptr ? "JavaScript WebSocket error" : fallback_message;
    if (!try_catch.Exception().IsEmpty()) {
        v8::String::Utf8Value exception_text(isolate, try_catch.Exception());
        if (*exception_text != nullptr && exception_text.length() > 0) {
            message.assign(*exception_text, static_cast<size_t>(exception_text.length()));
        }
    }
    message = sl_v8_redact_diagnostic_text(message);

    return ws_write_diag(engine->arena, out_diag, code, failure_code,
                         sl_str_from_parts(message.data(), message.size()), sl_str_empty(), hint);
}

SlStatus ws_drain_microtasks(SlEngine* engine, v8::Isolate* isolate, v8::Local<v8::Context> context,
                             SlDiag* out_diag)
{
    (void)context;
    v8::TryCatch try_catch(isolate);
    isolate->PerformMicrotaskCheckpoint();
    if (!try_catch.HasCaught()) {
        return sl_status_ok();
    }
    return ws_write_exception_diag(
        engine, out_diag, SL_DIAG_ENGINE_EXCEPTION, SL_STATUS_INVALID_STATE, isolate, try_catch,
        "JavaScript WebSocket microtask failed",
        ws_literal("V8 WebSocket microtasks are drained only on the owning engine thread.",
                   sizeof("V8 WebSocket microtasks are drained only on the owning engine thread.") -
                       1U));
}

bool ws_copy_bytes_from_value(v8::Local<v8::Value> value, std::vector<unsigned char>* out)
{
    const unsigned char* bytes = nullptr;
    size_t length = 0U;

    if (out == nullptr) {
        return false;
    }
    if (value->IsArrayBufferView()) {
        v8::Local<v8::ArrayBufferView> view = value.As<v8::ArrayBufferView>();
        std::shared_ptr<v8::BackingStore> backing = view->Buffer()->GetBackingStore();
        size_t offset = view->ByteOffset();
        length = view->ByteLength();
        if (backing == nullptr || offset > backing->ByteLength() ||
            length > backing->ByteLength() - offset)
        {
            return false;
        }
        if (length != 0U) {
            bytes = static_cast<const unsigned char*>(backing->Data()) + offset;
        }
    }
    else if (value->IsArrayBuffer()) {
        v8::Local<v8::ArrayBuffer> buffer = value.As<v8::ArrayBuffer>();
        std::shared_ptr<v8::BackingStore> backing = buffer->GetBackingStore();
        length = buffer->ByteLength();
        if (backing == nullptr || length > backing->ByteLength()) {
            return false;
        }
        if (length != 0U) {
            bytes = static_cast<const unsigned char*>(backing->Data());
        }
    }
    else {
        return false;
    }
    if (length == 0U) {
        out->clear();
        return true;
    }
    out->assign(bytes, bytes + length);
    return true;
}

void ws_mark_closed(v8::Isolate* isolate, v8::Local<v8::Context> context,
                    SlEngineWebSocketSession* session)
{
    if (session == nullptr || session->socket.IsEmpty()) {
        return;
    }
    v8::Local<v8::Object> socket = session->socket.Get(isolate);
    if (!ws_set(isolate, context, socket, "closed", v8::Boolean::New(isolate, true))) {
        return;
    }
}

void ws_accept_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    SlEngineWebSocketSession* session = ws_session_from_args(args);
    if (session == nullptr || session->closed) {
        ws_return_rejected(isolate, context, args, "SLOPPY_E_WEBSOCKET_CLOSED");
        return;
    }
    session->accepted = true;
    ws_return_resolved(isolate, context, args, v8::Boolean::New(isolate, true));
}

void ws_send_text_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    SlEngineWebSocketSession* session = ws_session_from_args(args);
    std::string text;
    SlDiag diag = {};
    SlWebSocketFrameWriteOptions options = {};
    if (session == nullptr || session->closed || session->bridge.send == nullptr) {
        ws_return_rejected(isolate, context, args, "SLOPPY_E_WEBSOCKET_CLOSED");
        return;
    }
    if (args.Length() < 1 || !sl_v8_std_string_from_value(isolate, args[0], &text)) {
        ws_return_rejected(isolate, context, args, "SLOPPY_E_WEBSOCKET_SEND_TEXT_INVALID");
        return;
    }
    options.fin = true;
    options.opcode = SL_WEBSOCKET_OPCODE_TEXT;
    options.payload =
        sl_bytes_from_parts(reinterpret_cast<const unsigned char*>(text.data()), text.size());
    if (!sl_status_is_ok(session->bridge.send(session->bridge.user, &options, &diag))) {
        ws_return_rejected(isolate, context, args, "SLOPPY_E_WEBSOCKET_SEND_FAILED");
        return;
    }
    ws_return_resolved(isolate, context, args, v8::Boolean::New(isolate, true));
}

void ws_send_json_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::String> json;
    SlEngineWebSocketSession* session = ws_session_from_args(args);
    SlDiag diag = {};
    SlWebSocketFrameWriteOptions options = {};
    if (session == nullptr || session->closed || session->bridge.send == nullptr) {
        ws_return_rejected(isolate, context, args, "SLOPPY_E_WEBSOCKET_CLOSED");
        return;
    }
    if (args.Length() < 1 || !v8::JSON::Stringify(context, args[0]).ToLocal(&json)) {
        ws_return_rejected(isolate, context, args, "SLOPPY_E_WEBSOCKET_SEND_JSON_INVALID");
        return;
    }
    v8::String::Utf8Value utf8(isolate, json);
    options.fin = true;
    options.opcode = SL_WEBSOCKET_OPCODE_TEXT;
    options.payload = sl_bytes_from_parts(reinterpret_cast<const unsigned char*>(*utf8),
                                          static_cast<size_t>(utf8.length()));
    if (!sl_status_is_ok(session->bridge.send(session->bridge.user, &options, &diag))) {
        ws_return_rejected(isolate, context, args, "SLOPPY_E_WEBSOCKET_SEND_FAILED");
        return;
    }
    ws_return_resolved(isolate, context, args, v8::Boolean::New(isolate, true));
}

void ws_send_bytes_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    SlEngineWebSocketSession* session = ws_session_from_args(args);
    std::vector<unsigned char> bytes;
    SlDiag diag = {};
    SlWebSocketFrameWriteOptions options = {};
    if (session == nullptr || session->closed || session->bridge.send == nullptr) {
        ws_return_rejected(isolate, context, args, "SLOPPY_E_WEBSOCKET_CLOSED");
        return;
    }
    if (args.Length() < 1 || !ws_copy_bytes_from_value(args[0], &bytes)) {
        ws_return_rejected(isolate, context, args, "SLOPPY_E_WEBSOCKET_SEND_BYTES_INVALID");
        return;
    }
    options.fin = true;
    options.opcode = SL_WEBSOCKET_OPCODE_BINARY;
    options.payload = sl_bytes_from_parts(bytes.data(), bytes.size());
    if (!sl_status_is_ok(session->bridge.send(session->bridge.user, &options, &diag))) {
        ws_return_rejected(isolate, context, args, "SLOPPY_E_WEBSOCKET_SEND_FAILED");
        return;
    }
    ws_return_resolved(isolate, context, args, v8::Boolean::New(isolate, true));
}

void ws_close_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    SlEngineWebSocketSession* session = ws_session_from_args(args);
    uint16_t code = 1000U;
    std::string reason;
    SlDiag diag = {};
    SlStatus status;
    if (session == nullptr || session->closed || session->bridge.close == nullptr) {
        ws_return_resolved(isolate, context, args, v8::Boolean::New(isolate, false));
        return;
    }
    if (args.Length() > 0 && args[0]->IsUint32()) {
        uint32_t raw = args[0].As<v8::Uint32>()->Value();
        if (raw <= UINT16_MAX) {
            code = static_cast<uint16_t>(raw);
        }
    }
    if (args.Length() > 1) {
        sl_v8_std_string_from_value(isolate, args[1], &reason);
    }
    status = session->bridge.close(session->bridge.user, code,
                                   sl_str_from_parts(reason.data(), reason.size()), &diag);
    if (!sl_status_is_ok(status)) {
        ws_return_rejected(isolate, context, args, "SLOPPY_E_WEBSOCKET_CLOSE_FAILED");
        return;
    }
    session->closed = true;
    ws_mark_closed(isolate, context, session);
    ws_resolve_next(session, isolate, context);
    ws_return_resolved(isolate, context, args, v8::Boolean::New(isolate, true));
}

void ws_iterator_async_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    args.GetReturnValue().Set(args.This());
}

void ws_iterator_next_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    SlEngineWebSocketSession* session = ws_session_from_args(args);
    v8::Local<v8::Promise::Resolver> resolver;
    v8::Local<v8::Object> result;
    if (session == nullptr) {
        ws_return_rejected(isolate, context, args, "SLOPPY_E_WEBSOCKET_CLOSED");
        return;
    }
    if (!session->messages.empty()) {
        v8::Local<v8::Value> value;
        SlV8WebSocketMessage message = std::move(session->messages.front());
        session->messages.pop_front();
        if (!ws_message_value(isolate, context, message, &value) ||
            !ws_next_result(isolate, context, value, false, &result))
        {
            ws_return_rejected(isolate, context, args, "SLOPPY_E_WEBSOCKET_MESSAGE_FAILED");
            return;
        }
        ws_return_resolved(isolate, context, args, result);
        return;
    }
    if (session->closed) {
        if (!ws_next_result(isolate, context, v8::Undefined(isolate), true, &result)) {
            ws_return_rejected(isolate, context, args, "SLOPPY_E_WEBSOCKET_MESSAGE_FAILED");
            return;
        }
        ws_return_resolved(isolate, context, args, result);
        return;
    }
    if (!session->pending_next.IsEmpty()) {
        ws_return_rejected(isolate, context, args, "SLOPPY_E_WEBSOCKET_CONCURRENT_NEXT");
        return;
    }
    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
        ws_return_rejected(isolate, context, args, "SLOPPY_E_WEBSOCKET_PROMISE_FAILED");
        return;
    }
    session->pending_next.Reset(isolate, resolver);
    args.GetReturnValue().Set(resolver->GetPromise());
}

void ws_messages_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    SlEngineWebSocketSession* session = ws_session_from_args(args);
    v8::Local<v8::Object> iterator = v8::Object::New(isolate);
    v8::Local<v8::Function> async_iterator;
    v8::Local<v8::Function> next;
    v8::Local<v8::External> data =
        v8::External::New(isolate, session, v8::kExternalPointerTypeTagDefault);
    if (session == nullptr ||
        !v8::FunctionTemplate::New(isolate, ws_iterator_async_callback, data)
             ->GetFunction(context)
             .ToLocal(&async_iterator) ||
        !v8::FunctionTemplate::New(isolate, ws_iterator_next_callback, data)
             ->GetFunction(context)
             .ToLocal(&next) ||
        !iterator->Set(context, v8::Symbol::GetAsyncIterator(isolate), async_iterator)
             .FromMaybe(false) ||
        !ws_set(isolate, context, iterator, "next", next))
    {
        isolate->ThrowException(v8::Exception::Error(
            v8::String::NewFromUtf8Literal(isolate, "SLOPPY_E_WEBSOCKET_ITERATOR_FAILED")));
        return;
    }
    args.GetReturnValue().Set(iterator);
}

void ws_set_context_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (args.Length() > 0) {
        ws_set(isolate, context, args.This(), "ctx", args[0]);
    }
    args.GetReturnValue().Set(v8::Undefined(isolate));
}

bool ws_make_socket(v8::Isolate* isolate, v8::Local<v8::Context> context,
                    SlEngineWebSocketSession* session, v8::Local<v8::Object>* out)
{
    v8::Local<v8::Object> socket = v8::Object::New(isolate);
    if (!ws_set_function(isolate, context, socket, "accept", ws_accept_callback, session) ||
        !ws_set_function(isolate, context, socket, "sendText", ws_send_text_callback, session) ||
        !ws_set_function(isolate, context, socket, "sendJson", ws_send_json_callback, session) ||
        !ws_set_function(isolate, context, socket, "sendBytes", ws_send_bytes_callback, session) ||
        !ws_set_function(isolate, context, socket, "close", ws_close_callback, session) ||
        !ws_set_function(isolate, context, socket, "messages", ws_messages_callback, session) ||
        !ws_set_function(isolate, context, socket, "__setContext", ws_set_context_callback,
                         session) ||
        !ws_set(isolate, context, socket, "accepted", v8::Boolean::New(isolate, true)) ||
        !ws_set(isolate, context, socket, "closed", v8::Boolean::New(isolate, false)))
    {
        return false;
    }
    v8::Local<v8::String> protocol;
    if (!ws_make_string_from_bytes(isolate, session->bridge.protocol.ptr,
                                   session->bridge.protocol.length, &protocol) ||
        !ws_set(isolate, context, socket, "protocol", protocol))
    {
        return false;
    }
    *out = socket;
    return true;
}

SlStatus ws_check_handler_promise(SlEngine* engine, SlEngineWebSocketSession* session,
                                  v8::Isolate* isolate, SlDiag* out_diag)
{
    if (session == nullptr || session->handler_promise.IsEmpty()) {
        return sl_status_ok();
    }
    v8::Local<v8::Promise> promise = session->handler_promise.Get(isolate);
    if (promise->State() != v8::Promise::kRejected) {
        return sl_status_ok();
    }
    return ws_write_diag(engine->arena, out_diag, SL_DIAG_ENGINE_EXCEPTION, SL_STATUS_INVALID_STATE,
                         ws_literal("WebSocket handler promise rejected",
                                    sizeof("WebSocket handler promise rejected") - 1U),
                         sl_str_empty(),
                         ws_literal("Handle exceptions inside WebSocket handlers.",
                                    sizeof("Handle exceptions inside WebSocket handlers.") - 1U));
}

} // namespace

extern "C" SlStatus sl_engine_v8_call_registered_websocket_handler_with_context(
    SlEngine* engine, SlArena* arena, SlHandlerId handler_id,
    const SlHttpRequestContext* request_context, const SlEngineWebSocketBridge* bridge,
    SlEngineWebSocketSession** out_session, SlDiag* out_diag)
{
    SlV8Engine* backend = ws_backend(engine);
    v8::Local<v8::Object> context_arg;
    v8::Local<v8::Object> socket;

    if (out_session == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_session = nullptr;
    if (engine == nullptr || backend == nullptr || backend->isolate == nullptr ||
        arena == nullptr || request_context == nullptr || bridge == nullptr ||
        !sl_handler_id_valid(handler_id))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    SlStatus status = ws_check_owner_thread(engine, backend, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = ws_check_cancelled(engine, request_context->cancellation, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    auto handler = backend->handlers.find(handler_id);
    if (handler == backend->handlers.end()) {
        return ws_write_missing_registered_handler_diag(engine, out_diag, handler_id);
    }

    SlEngineWebSocketSession* session = new (std::nothrow) SlEngineWebSocketSession();
    if (session == nullptr) {
        return sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
    }
    session->backend = backend;
    session->bridge = *bridge;

    v8::Isolate* isolate = backend->isolate;
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = backend->context.Get(isolate);
    v8::Context::Scope context_scope(context);
    v8::TryCatch try_catch(isolate);

    if (!sl_v8_make_http_context_object(isolate, context, request_context, &context_arg) ||
        !ws_make_socket(isolate, context, session, &socket) ||
        !ws_set(isolate, context, context_arg, "__sloppyWebSocketHandshake",
                v8::Boolean::New(isolate, true)) ||
        !ws_set(isolate, context, context_arg, "__sloppyWebSocket", socket))
    {
        delete session;
        return ws_write_diag(
            engine->arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR, SL_STATUS_INVALID_STATE,
            ws_literal("failed to materialize JavaScript WebSocket context",
                       sizeof("failed to materialize JavaScript WebSocket context") - 1U),
            sl_str_empty(), sl_str_empty());
    }

    v8::Local<v8::Function> function = handler->second.Get(isolate);
    v8::Local<v8::Value> args[1] = {context_arg};
    v8::MaybeLocal<v8::Value> maybe_result = function->Call(context, context->Global(), 1, args);
    v8::Local<v8::Value> js_result;
    if (!maybe_result.ToLocal(&js_result)) {
        delete session;
        return ws_write_exception_diag(
            engine, out_diag, SL_DIAG_ENGINE_EXCEPTION, SL_STATUS_INVALID_STATE, isolate, try_catch,
            "JavaScript WebSocket handler threw",
            ws_literal("Generated JavaScript locations are reported without source-map remapping.",
                       sizeof("Generated JavaScript locations are reported without source-map "
                              "remapping.") -
                           1U));
    }
    if (js_result->IsPromise()) {
        session->handler_promise.Reset(isolate, js_result.As<v8::Promise>());
    }
    session->socket.Reset(isolate, socket);
    backend->websocket_sessions.push_back(session);
    *out_session = session;
    return sl_status_ok();
}

extern "C" SlStatus sl_engine_v8_websocket_receive(SlEngine* engine,
                                                   SlEngineWebSocketSession* session,
                                                   const SlWebSocketFrame* frame, SlDiag* out_diag)
{
    if (engine == nullptr || session == nullptr || frame == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    SlV8Engine* backend = ws_backend(engine);
    if (backend == nullptr || backend->isolate == nullptr || session->backend != backend) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    SlStatus status = ws_check_owner_thread(engine, backend, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    v8::Isolate* isolate = backend->isolate;
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = backend->context.Get(isolate);
    v8::Context::Scope context_scope(context);

    status = ws_drain_microtasks(engine, isolate, context, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (frame->opcode == SL_WEBSOCKET_OPCODE_TEXT) {
        SlV8WebSocketMessage message;
        message.opcode = SL_WEBSOCKET_OPCODE_TEXT;
        message.text.assign(reinterpret_cast<const char*>(frame->payload.ptr),
                            frame->payload.length);
        session->messages.push_back(std::move(message));
    }
    else if (frame->opcode == SL_WEBSOCKET_OPCODE_BINARY) {
        SlV8WebSocketMessage message;
        message.opcode = SL_WEBSOCKET_OPCODE_BINARY;
        message.bytes.assign(frame->payload.ptr, frame->payload.ptr + frame->payload.length);
        session->messages.push_back(std::move(message));
    }

    if (!ws_resolve_next(session, isolate, context)) {
        return ws_write_diag(
            engine->arena, out_diag, SL_DIAG_ENGINE_CALL_ERROR, SL_STATUS_INVALID_STATE,
            ws_literal("failed to deliver WebSocket message to JavaScript",
                       sizeof("failed to deliver WebSocket message to JavaScript") - 1U),
            sl_str_empty(), sl_str_empty());
    }
    status = ws_drain_microtasks(engine, isolate, context, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return ws_check_handler_promise(engine, session, isolate, out_diag);
}

extern "C" SlStatus sl_engine_v8_websocket_close(SlEngine* engine,
                                                 SlEngineWebSocketSession* session, uint16_t code,
                                                 SlStr reason, SlDiag* out_diag)
{
    (void)code;
    (void)reason;
    if (engine == nullptr || session == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    SlV8Engine* backend = ws_backend(engine);
    if (backend == nullptr || backend->isolate == nullptr || session->backend != backend) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    SlStatus status = ws_check_owner_thread(engine, backend, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    v8::Isolate* isolate = backend->isolate;
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = backend->context.Get(isolate);
    v8::Context::Scope context_scope(context);
    session->closed = true;
    ws_mark_closed(isolate, context, session);
    if (!ws_resolve_next(session, isolate, context)) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    status = ws_drain_microtasks(engine, isolate, context, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return ws_check_handler_promise(engine, session, isolate, out_diag);
}

void sl_v8_websocket_dispose(SlV8Engine* backend)
{
    if (backend == nullptr) {
        return;
    }
    for (SlEngineWebSocketSession* session : backend->websocket_sessions) {
        if (session != nullptr) {
            session->pending_next.Reset();
            session->handler_promise.Reset();
            session->socket.Reset();
            delete session;
        }
    }
    backend->websocket_sessions.clear();
}

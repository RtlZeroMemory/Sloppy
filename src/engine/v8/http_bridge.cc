/*
 * src/engine/v8/http_bridge.cc
 *
 * Owns framework HTTP request-context materialization and Results.* descriptor conversion for
 * the V8 backend. engine_v8.cc stays focused on isolate/context lifecycle, handler lookup,
 * owner-thread checks, and Promise orchestration; HTTP-specific JS shapes live here.
 */
#include "engine_v8_internal.h"
#include "string_interop.h"

#include <string>
#include <utility>
#include <vector>

namespace {

struct HttpV8HeaderEntry
{
    std::string name;
    std::string value;
};

SlStr http_v8_literal(const char* ptr, size_t length)
{
    return sl_str_from_parts(ptr, length);
}

SlStr http_v8_str_from_string(const std::string& str)
{
    return sl_str_from_parts(str.data(), str.size());
}

SlStatus http_v8_to_local_string(v8::Isolate* isolate, SlStr str, v8::Local<v8::String>* out)
{
    SlV8Engine* backend =
        isolate == nullptr ? nullptr : static_cast<SlV8Engine*>(isolate->GetData(0));
    return sl_v8_string_from_native_view(backend, str, out);
}

std::string http_v8_value_to_string(v8::Isolate* isolate, v8::Local<v8::Value> value)
{
    std::string text;

    if (!sl_v8_std_string_from_value(isolate, value, &text)) {
        return std::string();
    }

    return text;
}

SlStatus http_v8_copy_string(SlArena* arena, const std::string& src, SlStr* out)
{
    return sl_v8_std_string_copy_to_arena(arena, src, out);
}

SlStatus http_v8_copy_bytes(SlArena* arena, const std::string& src, SlBytes* out)
{
    return sl_v8_std_string_copy_bytes_to_arena(arena, src, out);
}

std::string http_v8_ascii_lower_string(SlStr str)
{
    std::string lowered;

    if (str.ptr == nullptr && str.length != 0U) {
        return std::string();
    }

    lowered.reserve(str.length);
    for (size_t index = 0U; index < str.length; index += 1U) {
        char ch = str.ptr[index];
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
        lowered.push_back(ch);
    }

    return lowered;
}

std::string http_v8_ascii_lower_value(v8::Isolate* isolate, v8::Local<v8::Value> value)
{
    std::string lowered = http_v8_value_to_string(isolate, value);

    for (size_t index = 0U; index < lowered.size(); index += 1U) {
        char ch = lowered[index];
        if (ch >= 'A' && ch <= 'Z') {
            lowered[index] = static_cast<char>(ch - 'A' + 'a');
        }
    }

    return lowered;
}

SlStatus http_v8_write_diag(SlEngine* engine, SlDiag* out_diag, SlDiagCode code,
                            SlStatusCode failure_code, SlStr message, SlStr hint)
{
    SlDiagBuilder builder;
    SlStatus status;

    if (out_diag == nullptr) {
        return sl_status_from_code(failure_code);
    }
    if (engine == nullptr || engine->arena == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_diag_builder_init(&builder, engine->arena, SL_DIAG_SEVERITY_ERROR, code, message);
    if (!sl_status_is_ok(status)) {
        return status;
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

bool http_v8_set_string_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                 v8::Local<v8::Object> object, const char* name, SlStr value)
{
    v8::Local<v8::String> key;
    v8::Local<v8::String> local_value;

    if (!sl_status_is_ok(http_v8_to_local_string(isolate, sl_str_from_cstr(name), &key)) ||
        !sl_status_is_ok(http_v8_to_local_string(isolate, value, &local_value)))
    {
        return false;
    }

    return object->Set(context, key, local_value).FromMaybe(false);
}

bool http_v8_set_object_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                 v8::Local<v8::Object> object, const char* name,
                                 v8::Local<v8::Object> value)
{
    v8::Local<v8::String> key;

    if (!sl_status_is_ok(http_v8_to_local_string(isolate, sl_str_from_cstr(name), &key))) {
        return false;
    }

    return object->Set(context, key, value).FromMaybe(false);
}

bool http_v8_set_value_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                v8::Local<v8::Object> object, const char* name,
                                v8::Local<v8::Value> value)
{
    v8::Local<v8::String> key;

    if (!sl_status_is_ok(http_v8_to_local_string(isolate, sl_str_from_cstr(name), &key))) {
        return false;
    }

    return object->Set(context, key, value).FromMaybe(false);
}

bool http_v8_set_bool_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
                               v8::Local<v8::Object> object, const char* name, bool value)
{
    return http_v8_set_value_property(isolate, context, object, name,
                                      v8::Boolean::New(isolate, value));
}

bool http_v8_set_null_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
                               v8::Local<v8::Object> object, const char* name)
{
    return http_v8_set_value_property(isolate, context, object, name, v8::Null(isolate));
}

bool http_v8_set_function_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                   v8::Local<v8::Object> object, const char* name,
                                   v8::FunctionCallback callback)
{
    v8::Local<v8::FunctionTemplate> function_template =
        v8::FunctionTemplate::New(isolate, callback);
    v8::Local<v8::Function> function;

    if (!function_template->GetFunction(context).ToLocal(&function)) {
        return false;
    }

    return http_v8_set_value_property(isolate, context, object, name, function);
}

bool http_v8_get_private_value(v8::Isolate* isolate, v8::Local<v8::Context> context,
                               v8::Local<v8::Object> object, const char* name,
                               v8::Local<v8::Value>* out)
{
    v8::Local<v8::String> key;

    if (out == nullptr ||
        !sl_status_is_ok(http_v8_to_local_string(isolate, sl_str_from_cstr(name), &key)) ||
        !object->Get(context, key).ToLocal(out))
    {
        return false;
    }

    return true;
}

bool http_v8_collect_headers(const SlHttpRequestHead* request, std::vector<HttpV8HeaderEntry>* out)
{
    if (request == nullptr || out == nullptr ||
        (request->header_count != 0U && request->headers == nullptr))
    {
        return false;
    }

    out->clear();
    out->reserve(request->header_count);
    for (size_t index = 0U; index < request->header_count; index += 1U) {
        std::string lowered = http_v8_ascii_lower_string(request->headers[index].name);
        std::string value;
        if (request->headers[index].value.ptr == nullptr &&
            request->headers[index].value.length != 0U)
        {
            return false;
        }
        value.assign(
            request->headers[index].value.ptr == nullptr ? "" : request->headers[index].value.ptr,
            request->headers[index].value.length);

        bool appended = false;
        for (HttpV8HeaderEntry& entry : *out) {
            if (entry.name == lowered) {
                entry.value += ", ";
                entry.value += value;
                appended = true;
                break;
            }
        }
        if (!appended) {
            out->push_back({std::move(lowered), std::move(value)});
        }
    }

    return true;
}

void http_v8_headers_get_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Value> entries_value;

    if (args.Length() < 1 || !args[0]->IsString() ||
        !http_v8_get_private_value(isolate, context, args.This(), "__sloppyHeaderEntries",
                                   &entries_value) ||
        !entries_value->IsArray())
    {
        args.GetReturnValue().Set(v8::Null(isolate));
        return;
    }

    std::string wanted = http_v8_ascii_lower_value(isolate, args[0]);
    v8::Local<v8::Array> entries = entries_value.As<v8::Array>();
    for (uint32_t index = 0U; index < entries->Length(); index += 1U) {
        v8::Local<v8::Value> pair_value;
        if (!entries->Get(context, index).ToLocal(&pair_value) || !pair_value->IsArray()) {
            continue;
        }

        v8::Local<v8::Array> pair = pair_value.As<v8::Array>();
        v8::Local<v8::Value> name_value;
        v8::Local<v8::Value> value;
        if (!pair->Get(context, 0U).ToLocal(&name_value) || !name_value->IsString() ||
            !pair->Get(context, 1U).ToLocal(&value))
        {
            continue;
        }

        if (http_v8_value_to_string(isolate, name_value) == wanted) {
            args.GetReturnValue().Set(value);
            return;
        }
    }

    args.GetReturnValue().Set(v8::Null(isolate));
}

void http_v8_headers_entries_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Value> entries_value;

    if (!http_v8_get_private_value(isolate, context, args.This(), "__sloppyHeaderEntries",
                                   &entries_value) ||
        !entries_value->IsArray())
    {
        args.GetReturnValue().Set(v8::Array::New(isolate, 0));
        return;
    }

    v8::Local<v8::Array> entries = entries_value.As<v8::Array>();
    v8::Local<v8::Array> copy = v8::Array::New(isolate, static_cast<int>(entries->Length()));
    for (uint32_t index = 0U; index < entries->Length(); index += 1U) {
        v8::Local<v8::Value> pair_value;
        if (!entries->Get(context, index).ToLocal(&pair_value) || !pair_value->IsArray() ||
            !copy->Set(context, index, pair_value).FromMaybe(false))
        {
            args.GetReturnValue().Set(v8::Array::New(isolate, 0));
            return;
        }
    }

    args.GetReturnValue().Set(copy);
}

void http_v8_request_text_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Value> body;

    if (!http_v8_get_private_value(isolate, context, args.This(), "__sloppyBody", &body)) {
        args.GetReturnValue().Set(v8::String::Empty(isolate));
        return;
    }

    args.GetReturnValue().Set(body);
}

void http_v8_request_json_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Value> kind;
    v8::Local<v8::Value> body;

    if (!http_v8_get_private_value(isolate, context, args.This(), "__sloppyBodyKind", &kind) ||
        !kind->IsString() || http_v8_value_to_string(isolate, kind) != "json" ||
        !http_v8_get_private_value(isolate, context, args.This(), "__sloppyBody", &body) ||
        !body->IsString())
    {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "Request body is not available as JSON.")));
        return;
    }

    v8::Local<v8::Value> parsed;
    if (!v8::JSON::Parse(context, body.As<v8::String>()).ToLocal(&parsed)) {
        return;
    }

    args.GetReturnValue().Set(parsed);
}

void http_v8_signal_throw_if_aborted_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Object> self = args.This();
    v8::Local<v8::Value> aborted_value;
    v8::Local<v8::Value> reason_value;
    std::string message = "Sloppy request was cancelled";

    if (!http_v8_get_private_value(isolate, context, self, "aborted", &aborted_value) ||
        !aborted_value->BooleanValue(isolate))
    {
        args.GetReturnValue().Set(v8::Undefined(isolate));
        return;
    }

    if (http_v8_get_private_value(isolate, context, self, "reason", &reason_value) &&
        reason_value->IsString())
    {
        std::string reason = http_v8_value_to_string(isolate, reason_value);
        if (!reason.empty()) {
            message += ": ";
            message += reason;
        }
    }

    v8::Local<v8::String> error_message;
    if (!v8::String::NewFromUtf8(isolate, message.c_str(), v8::NewStringType::kNormal,
                                 static_cast<int>(message.size()))
             .ToLocal(&error_message))
    {
        error_message = v8::String::NewFromUtf8Literal(isolate, "Sloppy request was cancelled");
    }
    isolate->ThrowException(v8::Exception::Error(error_message));
}

bool http_v8_make_signal_object(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                const SlCancellationToken* cancellation, v8::Local<v8::Object>* out)
{
    v8::Local<v8::Object> signal = v8::Object::New(isolate);
    bool aborted = sl_cancellation_token_is_cancelled(cancellation);
    SlStr reason = sl_str_empty();

    if (out == nullptr) {
        return false;
    }

    if (aborted) {
        reason = !sl_str_is_empty(cancellation->detail)
                     ? cancellation->detail
                     : sl_cancellation_reason_name(cancellation->reason);
    }

    if (!http_v8_set_bool_property(isolate, context, signal, "aborted", aborted)) {
        return false;
    }
    if (aborted) {
        if (!http_v8_set_string_property(isolate, context, signal, "reason", reason)) {
            return false;
        }
    }
    else if (!http_v8_set_null_property(isolate, context, signal, "reason")) {
        return false;
    }
    if (!http_v8_set_function_property(isolate, context, signal, "throwIfAborted",
                                       http_v8_signal_throw_if_aborted_callback))
    {
        return false;
    }

    *out = signal;
    return true;
}

bool http_v8_make_header_bag(v8::Isolate* isolate, v8::Local<v8::Context> context,
                             const SlHttpRequestHead* request, v8::Local<v8::Object>* out)
{
    v8::Local<v8::Object> headers = v8::Object::New(isolate);
    v8::Local<v8::Array> entries;
    std::vector<HttpV8HeaderEntry> header_entries;

    if (out == nullptr || !http_v8_collect_headers(request, &header_entries) ||
        header_entries.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        return false;
    }

    entries = v8::Array::New(isolate, static_cast<int>(header_entries.size()));
    for (size_t index = 0U; index < header_entries.size(); index += 1U) {
        v8::Local<v8::Array> pair = v8::Array::New(isolate, 2);
        v8::Local<v8::String> name;
        v8::Local<v8::String> value;
        if (!sl_status_is_ok(http_v8_to_local_string(
                isolate, http_v8_str_from_string(header_entries[index].name), &name)) ||
            !sl_status_is_ok(http_v8_to_local_string(
                isolate, http_v8_str_from_string(header_entries[index].value), &value)) ||
            !pair->Set(context, 0U, name).FromMaybe(false) ||
            !pair->Set(context, 1U, value).FromMaybe(false) ||
            !entries->Set(context, static_cast<uint32_t>(index), pair).FromMaybe(false))
        {
            return false;
        }
    }

    if (!http_v8_set_value_property(isolate, context, headers, "__sloppyHeaderEntries", entries) ||
        !http_v8_set_function_property(isolate, context, headers, "get",
                                       http_v8_headers_get_callback) ||
        !http_v8_set_function_property(isolate, context, headers, "entries",
                                       http_v8_headers_entries_callback))
    {
        return false;
    }

    *out = headers;
    return true;
}

SlStr http_v8_request_method_name(SlHttpMethod method)
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

SlStr http_v8_request_body_kind_name(SlHttpRequestBodyKind body_kind)
{
    switch (body_kind) {
    case SL_HTTP_REQUEST_BODY_JSON:
        return sl_str_from_cstr("json");
    case SL_HTTP_REQUEST_BODY_TEXT:
        return sl_str_from_cstr("text");
    case SL_HTTP_REQUEST_BODY_NONE:
    default:
        return sl_str_from_cstr("none");
    }
}

bool http_v8_get_object_string(v8::Isolate* isolate, v8::Local<v8::Context> context,
                               v8::Local<v8::Object> object, const char* name, std::string* out)
{
    v8::Local<v8::String> key;
    v8::Local<v8::Value> value;

    if (out == nullptr ||
        !sl_status_is_ok(http_v8_to_local_string(isolate, sl_str_from_cstr(name), &key)) ||
        !object->Get(context, key).ToLocal(&value) || !value->IsString())
    {
        return false;
    }

    *out = http_v8_value_to_string(isolate, value);
    return true;
}

bool http_v8_get_object_status(v8::Isolate* isolate, v8::Local<v8::Context> context,
                               v8::Local<v8::Object> object, uint16_t* out)
{
    v8::Local<v8::String> key;
    v8::Local<v8::Value> value;
    int32_t status = 0;

    if (out == nullptr ||
        !sl_status_is_ok(http_v8_to_local_string(isolate, sl_str_from_cstr("status"), &key)) ||
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

bool http_v8_has_result_marker(v8::Isolate* isolate, v8::Local<v8::Context> context,
                               v8::Local<v8::Object> object)
{
    v8::Local<v8::String> key;
    v8::Local<v8::Value> value;

    if (!sl_status_is_ok(
            http_v8_to_local_string(isolate, sl_str_from_cstr("__sloppyResult"), &key)) ||
        !object->Get(context, key).ToLocal(&value))
    {
        return false;
    }

    return value->BooleanValue(isolate);
}

bool http_v8_status_supported(uint16_t status)
{
    switch (status) {
    case 200U:
    case 201U:
    case 202U:
    case 204U:
    case 400U:
    case 404U:
    case 405U:
    case 413U:
    case 415U:
    case 500U:
    case 501U:
        return true;
    default:
        return false;
    }
}

bool http_v8_header_value_safe(const std::string& value)
{
    return value.find('\r') == std::string::npos && value.find('\n') == std::string::npos;
}

bool http_v8_header_name_char_safe(char ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
           ch == '!' || ch == '#' || ch == '$' || ch == '%' || ch == '&' || ch == '\'' ||
           ch == '*' || ch == '+' || ch == '-' || ch == '.' || ch == '^' || ch == '_' ||
           ch == '`' || ch == '|' || ch == '~';
}

bool http_v8_header_name_safe(const std::string& name)
{
    if (name.empty()) {
        return false;
    }

    for (size_t index = 0U; index < name.size(); index += 1U) {
        if (!http_v8_header_name_char_safe(name[index])) {
            return false;
        }
    }

    return true;
}

bool http_v8_response_header_managed(const std::string& name)
{
    std::string lowered = name;

    for (size_t index = 0U; index < lowered.size(); index += 1U) {
        char ch = lowered[index];
        if (ch >= 'A' && ch <= 'Z') {
            lowered[index] = static_cast<char>(ch - 'A' + 'a');
        }
    }

    return lowered == "connection" || lowered == "content-type" || lowered == "content-length";
}

SlStatus http_v8_write_invalid_headers_diag(SlEngine* engine, SlDiag* out_diag)
{
    return http_v8_write_diag(
        engine, out_diag, SL_DIAG_INVALID_HTTP_RESULT, SL_STATUS_INVALID_STATE,
        http_v8_literal("JavaScript result descriptor has invalid headers",
                        sizeof("JavaScript result descriptor has invalid headers") - 1U),
        http_v8_literal("options.headers must be a plain object with safe string names and values.",
                        sizeof("options.headers must be a plain object with safe string names and "
                               "values.") -
                            1U));
}

SlStatus http_v8_copy_response_header(SlArena* arena, const std::string& name,
                                      const std::string& value, SlHttpHeader* out)
{
    SlStatus status;

    if (arena == nullptr || out == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out = {};
    status = http_v8_copy_string(arena, name, &out->name);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return http_v8_copy_string(arena, value, &out->value);
}

SlStatus http_v8_get_optional_string_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                              v8::Local<v8::Object> object, const char* name,
                                              bool* out_present, std::string* out)
{
    v8::Local<v8::String> key;
    v8::Local<v8::Value> value;

    if (out_present == nullptr || out == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_present = false;
    *out = std::string();
    if (!sl_status_is_ok(http_v8_to_local_string(isolate, sl_str_from_cstr(name), &key)) ||
        !object->Get(context, key).ToLocal(&value))
    {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    if (value->IsUndefined() || value->IsNull()) {
        return sl_status_ok();
    }
    if (!value->IsString()) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_present = true;
    *out = http_v8_value_to_string(isolate, value);
    return sl_status_ok();
}

SlStatus http_v8_copy_result_headers(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                     SlEngine* engine, SlArena* arena,
                                     v8::Local<v8::Object> descriptor, SlHttpHeader** out_headers,
                                     size_t* out_header_count, SlDiag* out_diag)
{
    v8::Local<v8::String> headers_key;
    v8::Local<v8::Value> headers_value;
    v8::Local<v8::Object> headers_object;
    v8::Local<v8::Array> names;
    bool has_headers_object = false;
    bool has_location = false;
    std::string location;
    size_t total_header_count = 0U;
    size_t index = 0U;
    void* memory = nullptr;
    SlHttpHeader* headers = nullptr;
    SlStatus status;

    if (engine == nullptr || arena == nullptr || out_headers == nullptr ||
        out_header_count == nullptr)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_headers = nullptr;
    *out_header_count = 0U;

    status = http_v8_get_optional_string_property(isolate, context, descriptor, "location",
                                                  &has_location, &location);
    if (sl_status_code(status) == SL_STATUS_INVALID_ARGUMENT) {
        return http_v8_write_invalid_headers_diag(engine, out_diag);
    }
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (has_location && !http_v8_header_value_safe(location)) {
        return http_v8_write_invalid_headers_diag(engine, out_diag);
    }

    if (!sl_status_is_ok(
            http_v8_to_local_string(isolate, sl_str_from_cstr("headers"), &headers_key)) ||
        !descriptor->Get(context, headers_key).ToLocal(&headers_value))
    {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    if (!headers_value->IsUndefined() && !headers_value->IsNull()) {
        if (!headers_value->IsObject() || headers_value->IsArray()) {
            return http_v8_write_invalid_headers_diag(engine, out_diag);
        }
        headers_object = headers_value.As<v8::Object>();
        if (!headers_object->GetOwnPropertyNames(context).ToLocal(&names)) {
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
        has_headers_object = true;
        total_header_count = names->Length();
    }

    if (has_location) {
        total_header_count += 1U;
    }
    if (total_header_count == 0U) {
        return sl_status_ok();
    }
    if (total_header_count > std::numeric_limits<size_t>::max() / sizeof(SlHttpHeader)) {
        return sl_status_from_code(SL_STATUS_OVERFLOW);
    }

    status = sl_arena_alloc(arena, total_header_count * sizeof(SlHttpHeader), alignof(SlHttpHeader),
                            &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    headers = static_cast<SlHttpHeader*>(memory);

    if (has_headers_object) {
        for (uint32_t property_index = 0U; property_index < names->Length(); property_index += 1U) {
            v8::Local<v8::Value> name_value;
            v8::Local<v8::Value> value;
            std::string name;
            std::string header_value;

            if (!names->Get(context, property_index).ToLocal(&name_value) ||
                !name_value->IsString() ||
                !headers_object->Get(context, name_value).ToLocal(&value) || !value->IsString())
            {
                return http_v8_write_invalid_headers_diag(engine, out_diag);
            }

            name = http_v8_value_to_string(isolate, name_value);
            header_value = http_v8_value_to_string(isolate, value);
            if (!http_v8_header_name_safe(name) || http_v8_response_header_managed(name) ||
                !http_v8_header_value_safe(header_value))
            {
                return http_v8_write_invalid_headers_diag(engine, out_diag);
            }

            status = http_v8_copy_response_header(arena, name, header_value, &headers[index]);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            index += 1U;
        }
    }

    if (has_location) {
        status = http_v8_copy_response_header(arena, "Location", location, &headers[index]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        index += 1U;
    }

    *out_headers = headers;
    *out_header_count = index;
    return sl_status_ok();
}

bool http_v8_stringify_json(v8::Isolate* isolate, v8::Local<v8::Context> context,
                            v8::Local<v8::Value> value, std::string* out)
{
    v8::Local<v8::String> json;

    if (out == nullptr || !v8::JSON::Stringify(context, value).ToLocal(&json)) {
        return false;
    }

    *out = http_v8_value_to_string(isolate, json);
    return true;
}

} // namespace

bool sl_v8_make_http_context_object(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                    const SlHttpRequestContext* request_context,
                                    v8::Local<v8::Object>* out)
{
    v8::Local<v8::Object> ctx = v8::Object::New(isolate);
    v8::Local<v8::Object> route = v8::Object::New(isolate);
    v8::Local<v8::Object> query = v8::Object::New(isolate);
    v8::Local<v8::Object> request = v8::Object::New(isolate);
    v8::Local<v8::Object> headers;
    v8::Local<v8::Object> signal;
    size_t index = 0U;
    SlStr method = sl_str_empty();
    SlStr body = sl_str_empty();

    if (request_context == nullptr || request_context->request == nullptr || out == nullptr) {
        return false;
    }
    method = http_v8_request_method_name(request_context->request->method);
    if (sl_str_is_empty(method)) {
        return false;
    }

    for (index = 0U; index < request_context->route_param_count; index += 1U) {
        const SlRouteParam* param = &request_context->route_params[index];
        v8::Local<v8::String> key;
        v8::Local<v8::String> value;
        if (!sl_status_is_ok(http_v8_to_local_string(isolate, param->name, &key)) ||
            !sl_status_is_ok(http_v8_to_local_string(isolate, param->value, &value)) ||
            !route->Set(context, key, value).FromMaybe(false))
        {
            return false;
        }
    }

    for (index = 0U; index < request_context->query_param_count; index += 1U) {
        const SlHttpQueryParam* param = &request_context->query_params[index];
        v8::Local<v8::String> key;
        v8::Local<v8::String> value;
        if (!sl_status_is_ok(http_v8_to_local_string(isolate, param->name, &key)) ||
            !sl_status_is_ok(http_v8_to_local_string(isolate, param->value, &value)) ||
            !query->Set(context, key, value).FromMaybe(false))
        {
            return false;
        }
    }

    if (!http_v8_make_signal_object(isolate, context, request_context->cancellation, &signal) ||
        !http_v8_make_header_bag(isolate, context, request_context->request, &headers))
    {
        return false;
    }

    body = sl_str_from_parts(reinterpret_cast<const char*>(request_context->request->body.ptr),
                             request_context->request->body.length);

    if (!http_v8_set_string_property(isolate, context, request, "method", method) ||
        !http_v8_set_string_property(isolate, context, request, "path",
                                     request_context->request->path) ||
        !http_v8_set_string_property(isolate, context, request, "rawTarget",
                                     request_context->request->raw_target) ||
        !http_v8_set_object_property(isolate, context, request, "headers", headers) ||
        !http_v8_set_string_property(isolate, context, request, "__sloppyBody", body) ||
        !http_v8_set_string_property(isolate, context, request, "__sloppyBodyKind",
                                     http_v8_request_body_kind_name(request_context->body_kind)) ||
        !http_v8_set_function_property(isolate, context, request, "text",
                                       http_v8_request_text_callback) ||
        !http_v8_set_function_property(isolate, context, request, "json",
                                       http_v8_request_json_callback) ||
        !http_v8_set_object_property(isolate, context, ctx, "route", route) ||
        !http_v8_set_object_property(isolate, context, ctx, "query", query) ||
        !http_v8_set_object_property(isolate, context, ctx, "request", request) ||
        !http_v8_set_object_property(isolate, context, ctx, "signal", signal))
    {
        return false;
    }

    if (sl_cancellation_token_reason(request_context->cancellation) ==
        SL_CANCELLATION_REASON_DEADLINE_EXCEEDED)
    {
        v8::Local<v8::Object> deadline = v8::Object::New(isolate);
        if (!http_v8_set_bool_property(isolate, context, deadline, "expired", true) ||
            !http_v8_set_object_property(isolate, context, ctx, "deadline", deadline))
        {
            return false;
        }
    }
    else if (!http_v8_set_null_property(isolate, context, ctx, "deadline")) {
        return false;
    }

    *out = ctx;
    return true;
}

SlStatus sl_v8_convert_http_handler_result(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                           SlEngine* engine, SlArena* arena,
                                           v8::Local<v8::Value> js_result,
                                           SlEngineResult* out_result, SlDiag* out_diag)
{
    if (js_result->IsString()) {
        SlStatus status = http_v8_copy_string(arena, http_v8_value_to_string(isolate, js_result),
                                              &out_result->text);
        if (!sl_status_is_ok(status)) {
            return status;
        }

        out_result->kind = SL_ENGINE_RESULT_TEXT;
        out_result->response = sl_http_response_text(200U, out_result->text);
        return sl_status_ok();
    }

    if (!js_result->IsObject()) {
        return http_v8_write_diag(
            engine, out_diag, SL_DIAG_INVALID_HTTP_RESULT, SL_STATUS_UNSUPPORTED,
            http_v8_literal("JavaScript handler returned an unsupported result type",
                            sizeof("JavaScript handler returned an unsupported result type") - 1U),
            http_v8_literal("Return a string or a supported Results.* descriptor.",
                            sizeof("Return a string or a supported Results.* descriptor.") - 1U));
    }

    v8::Local<v8::Object> object = js_result.As<v8::Object>();
    if (!http_v8_has_result_marker(isolate, context, object)) {
        return http_v8_write_diag(
            engine, out_diag, SL_DIAG_INVALID_HTTP_RESULT, SL_STATUS_UNSUPPORTED,
            http_v8_literal("JavaScript handler returned an unsupported result type",
                            sizeof("JavaScript handler returned an unsupported result type") - 1U),
            http_v8_literal("Result descriptors must include __sloppyResult: true.",
                            sizeof("Result descriptors must include __sloppyResult: true.") - 1U));
    }

    std::string kind;
    std::string content_type;
    uint16_t status_code = 0U;
    if (!http_v8_get_object_string(isolate, context, object, "kind", &kind) ||
        !http_v8_get_object_status(isolate, context, object, &status_code))
    {
        return http_v8_write_diag(
            engine, out_diag, SL_DIAG_INVALID_HTTP_RESULT, SL_STATUS_INVALID_STATE,
            http_v8_literal("JavaScript result descriptor is missing kind or status",
                            sizeof("JavaScript result descriptor is missing kind or status") - 1U),
            http_v8_literal("Return a supported Results.* descriptor with kind and status.",
                            sizeof("Return a supported Results.* descriptor with kind and "
                                   "status.") -
                                1U));
    }

    if (!http_v8_status_supported(status_code)) {
        return http_v8_write_diag(
            engine, out_diag, SL_DIAG_INVALID_HTTP_RESULT, SL_STATUS_INVALID_STATE,
            http_v8_literal("JavaScript result descriptor has an unsupported status",
                            sizeof("JavaScript result descriptor has an unsupported status") - 1U),
            http_v8_literal("Supported response statuses are 200, 201, 202, 204, 400, 404, "
                            "405, 413, 415, 500, and 501.",
                            sizeof("Supported response statuses are 200, 201, 202, 204, 400, "
                                   "404, 405, 413, 415, 500, and 501.") -
                                1U));
    }

    SlHttpHeader* response_headers = nullptr;
    size_t response_header_count = 0U;
    SlStatus header_status =
        http_v8_copy_result_headers(isolate, context, engine, arena, object, &response_headers,
                                    &response_header_count, out_diag);
    if (!sl_status_is_ok(header_status)) {
        return header_status;
    }

    if (kind == "empty") {
        out_result->kind = SL_ENGINE_RESULT_NONE;
        out_result->response = sl_http_response_empty(status_code);
        out_result->response.headers = response_headers;
        out_result->response.header_count = response_header_count;
        return sl_status_ok();
    }

    if (!http_v8_get_object_string(isolate, context, object, "contentType", &content_type)) {
        return http_v8_write_diag(
            engine, out_diag, SL_DIAG_INVALID_HTTP_RESULT, SL_STATUS_INVALID_STATE,
            http_v8_literal("JavaScript result descriptor is missing contentType",
                            sizeof("JavaScript result descriptor is missing contentType") - 1U),
            sl_str_empty());
    }
    if (!http_v8_header_value_safe(content_type)) {
        return http_v8_write_diag(
            engine, out_diag, SL_DIAG_INVALID_HTTP_RESULT, SL_STATUS_INVALID_STATE,
            http_v8_literal("JavaScript result descriptor has an invalid contentType",
                            sizeof("JavaScript result descriptor has an invalid contentType") - 1U),
            http_v8_literal("Content-Type must not contain CR or LF characters.",
                            sizeof("Content-Type must not contain CR or LF characters.") - 1U));
    }

    v8::Local<v8::String> body_key;
    v8::Local<v8::Value> body;
    if (!sl_status_is_ok(http_v8_to_local_string(isolate, sl_str_from_cstr("body"), &body_key)) ||
        !object->Get(context, body_key).ToLocal(&body))
    {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    if (kind == "text" || kind == "html") {
        if (!body->IsString()) {
            return http_v8_write_diag(
                engine, out_diag, SL_DIAG_INVALID_HTTP_RESULT, SL_STATUS_INVALID_STATE,
                http_v8_literal("Text-like Results body must be a string",
                                sizeof("Text-like Results body must be a string") - 1U),
                sl_str_empty());
        }

        SlStatus status =
            http_v8_copy_string(arena, http_v8_value_to_string(isolate, body), &out_result->text);
        if (!sl_status_is_ok(status)) {
            return status;
        }

        out_result->kind = SL_ENGINE_RESULT_TEXT;
        out_result->response = sl_http_response_text(status_code, out_result->text);
        out_result->response.headers = response_headers;
        out_result->response.header_count = response_header_count;
        return http_v8_copy_string(arena, content_type, &out_result->response.content_type);
    }

    if (kind == "json" || kind == "problem") {
        std::string json;
        SlBytes bytes = {nullptr, 0U};
        SlStatus status;

        if (body->IsUndefined()) {
            body = v8::Null(isolate);
        }

        if (!http_v8_stringify_json(isolate, context, body, &json)) {
            return http_v8_write_diag(
                engine, out_diag, SL_DIAG_INVALID_HTTP_RESULT, SL_STATUS_INVALID_STATE,
                http_v8_literal("Results.json body could not be serialized",
                                sizeof("Results.json body could not be serialized") - 1U),
                sl_str_empty());
        }

        status = http_v8_copy_bytes(arena, json, &bytes);
        if (!sl_status_is_ok(status)) {
            return status;
        }

        out_result->kind = kind == "json" ? SL_ENGINE_RESULT_JSON : SL_ENGINE_RESULT_ERROR;
        out_result->response = kind == "json" ? sl_http_response_json(status_code, bytes)
                                              : sl_http_response_problem(status_code, bytes);
        out_result->response.headers = response_headers;
        out_result->response.header_count = response_header_count;
        return http_v8_copy_string(arena, content_type, &out_result->response.content_type);
    }

    return http_v8_write_diag(
        engine, out_diag, SL_DIAG_INVALID_HTTP_RESULT, SL_STATUS_UNSUPPORTED,
        http_v8_literal("JavaScript result descriptor kind is unsupported",
                        sizeof("JavaScript result descriptor kind is unsupported") - 1U),
        http_v8_literal("Supported result kinds are text, html, json, empty, and problem.",
                        sizeof("Supported result kinds are text, html, json, empty, and "
                               "problem.") -
                            1U));
}

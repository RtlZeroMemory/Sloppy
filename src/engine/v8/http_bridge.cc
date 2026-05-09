/*
 * src/engine/v8/http_bridge.cc
 *
 * Owns framework HTTP request-context materialization and Results.* descriptor conversion for
 * the V8 backend. engine_v8.cc stays focused on isolate/context lifecycle, handler lookup,
 * owner-thread checks, and Promise orchestration; HTTP-specific JS shapes live here.
 */
#include "engine_v8_internal.h"
#include "string_interop.h"

#include "sloppy/container.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

struct HttpV8HeaderEntry
{
    std::string name;
    std::string value;
};

void http_v8_headers_get_callback(const v8::FunctionCallbackInfo<v8::Value>& args);
void http_v8_headers_entries_callback(const v8::FunctionCallbackInfo<v8::Value>& args);
void http_v8_body_bytes_callback(const v8::FunctionCallbackInfo<v8::Value>& args);
void http_v8_body_text_callback(const v8::FunctionCallbackInfo<v8::Value>& args);
void http_v8_body_json_callback(const v8::FunctionCallbackInfo<v8::Value>& args);
void http_v8_request_bytes_callback(const v8::FunctionCallbackInfo<v8::Value>& args);
void http_v8_request_text_callback(const v8::FunctionCallbackInfo<v8::Value>& args);
void http_v8_request_json_callback(const v8::FunctionCallbackInfo<v8::Value>& args);
void http_v8_signal_throw_if_aborted_callback(const v8::FunctionCallbackInfo<v8::Value>& args);
void http_v8_log_trace_callback(const v8::FunctionCallbackInfo<v8::Value>& args);
void http_v8_log_debug_callback(const v8::FunctionCallbackInfo<v8::Value>& args);
void http_v8_log_info_callback(const v8::FunctionCallbackInfo<v8::Value>& args);
void http_v8_log_warn_callback(const v8::FunctionCallbackInfo<v8::Value>& args);
void http_v8_log_error_callback(const v8::FunctionCallbackInfo<v8::Value>& args);
void http_v8_log_is_enabled_callback(const v8::FunctionCallbackInfo<v8::Value>& args);
void http_v8_log_for_category_callback(const v8::FunctionCallbackInfo<v8::Value>& args);

const char* http_v8_string_key_name(SlV8HttpStringKey key)
{
    switch (key) {
    case SL_V8_HTTP_STRING_ABORTED:
        return "aborted";
    case SL_V8_HTTP_STRING_BODY:
    case SL_V8_HTTP_STRING_BODY_RESULT:
        return "body";
    case SL_V8_HTTP_STRING_BYTES:
        return "bytes";
    case SL_V8_HTTP_STRING_CONNECTION:
        return "connection";
    case SL_V8_HTTP_STRING_CONSUMED:
        return "consumed";
    case SL_V8_HTTP_STRING_CONTENT_LENGTH:
        return "contentLength";
    case SL_V8_HTTP_STRING_CONTENT_TYPE:
        return "contentType";
    case SL_V8_HTTP_STRING_DEADLINE:
        return "deadline";
    case SL_V8_HTTP_STRING_ENTRIES:
        return "entries";
    case SL_V8_HTTP_STRING_EXPIRED:
        return "expired";
    case SL_V8_HTTP_STRING_GET:
        return "get";
    case SL_V8_HTTP_STRING_HEADER:
        return "header";
    case SL_V8_HTTP_STRING_HEADERS:
        return "headers";
    case SL_V8_HTTP_STRING_ID:
        return "id";
    case SL_V8_HTTP_STRING_JSON:
        return "json";
    case SL_V8_HTTP_STRING_KIND:
        return "kind";
    case SL_V8_HTTP_STRING_LOCATION:
        return "location";
    case SL_V8_HTTP_STRING_LOG:
        return "log";
    case SL_V8_HTTP_STRING_METHOD:
        return "method";
    case SL_V8_HTTP_STRING_PATH:
        return "path";
    case SL_V8_HTTP_STRING_PROTOCOL:
        return "protocol";
    case SL_V8_HTTP_STRING_QUERY:
        return "query";
    case SL_V8_HTTP_STRING_QUERY_STRING:
        return "queryString";
    case SL_V8_HTTP_STRING_RAW_TARGET:
        return "rawTarget";
    case SL_V8_HTTP_STRING_REASON:
        return "reason";
    case SL_V8_HTTP_STRING_REQUEST:
        return "request";
    case SL_V8_HTTP_STRING_REQUEST_ID:
        return "requestId";
    case SL_V8_HTTP_STRING_ROUTE:
        return "route";
    case SL_V8_HTTP_STRING_ROUTE_NAME:
        return "routeName";
    case SL_V8_HTTP_STRING_ROUTE_PATTERN:
        return "routePattern";
    case SL_V8_HTTP_STRING_SCHEME:
        return "scheme";
    case SL_V8_HTTP_STRING_SECURE:
        return "secure";
    case SL_V8_HTTP_STRING_SIGNAL:
        return "signal";
    case SL_V8_HTTP_STRING_SLOPPY_RESULT:
        return "__sloppyResult";
    case SL_V8_HTTP_STRING_STATUS:
        return "status";
    case SL_V8_HTTP_STRING_TEXT:
        return "text";
    case SL_V8_HTTP_STRING_THROW_IF_ABORTED:
        return "throwIfAborted";
    case SL_V8_HTTP_STRING_TRACE:
        return "trace";
    case SL_V8_HTTP_STRING_DEBUG:
        return "debug";
    case SL_V8_HTTP_STRING_INFO:
        return "info";
    case SL_V8_HTTP_STRING_WARN:
        return "warn";
    case SL_V8_HTTP_STRING_ERROR:
        return "error";
    case SL_V8_HTTP_STRING_IS_ENABLED:
        return "isEnabled";
    case SL_V8_HTTP_STRING_FOR_CATEGORY:
        return "forCategory";
    case SL_V8_HTTP_STRING_FAST_RESULT:
        return "__sloppyFastResult";
    case SL_V8_HTTP_STRING_FAST_JSON_TEXT:
        return "__sloppyJsonText";
    case SL_V8_HTTP_STRING_COUNT:
    default:
        return nullptr;
    }
}

const char* http_v8_private_key_name(SlV8HttpPrivateKey key)
{
    switch (key) {
    case SL_V8_HTTP_PRIVATE_ABORTED:
        return "aborted";
    case SL_V8_HTTP_PRIVATE_BODY:
        return "__sloppyBody";
    case SL_V8_HTTP_PRIVATE_BODY_BYTES:
        return "__sloppyBodyBytes";
    case SL_V8_HTTP_PRIVATE_BODY_CONSUMED:
        return "__sloppyBodyConsumed";
    case SL_V8_HTTP_PRIVATE_BODY_KIND:
        return "__sloppyBodyKind";
    case SL_V8_HTTP_PRIVATE_HEADER_SNAPSHOT:
        return "__sloppyHeaderSnapshot";
    case SL_V8_HTTP_PRIVATE_LOG_CATEGORY:
        return "__sloppyLogCategory";
    case SL_V8_HTTP_PRIVATE_LOG_REQUEST_ID:
        return "__sloppyLogRequestId";
    case SL_V8_HTTP_PRIVATE_LOG_ROUTE_NAME:
        return "__sloppyLogRouteName";
    case SL_V8_HTTP_PRIVATE_LOG_ROUTE_PATTERN:
        return "__sloppyLogRoutePattern";
    case SL_V8_HTTP_PRIVATE_REASON:
        return "reason";
    case SL_V8_HTTP_PRIVATE_COUNT:
    default:
        return nullptr;
    }
}

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

SlStatus http_v8_cached_string(v8::Isolate* isolate, SlV8HttpStringKey name,
                               v8::Local<v8::String>* out)
{
    SlV8Engine* backend =
        isolate == nullptr ? nullptr : static_cast<SlV8Engine*>(isolate->GetData(0));
    const char* text = http_v8_string_key_name(name);
    size_t index = static_cast<size_t>(name);
    v8::Local<v8::String> value;
    SlStatus status;

    if (backend == nullptr || out == nullptr || text == nullptr ||
        index >= backend->http_strings.size())
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (!backend->http_strings[index].IsEmpty()) {
        *out = backend->http_strings[index].Get(isolate);
        return sl_status_ok();
    }

    status = sl_v8_string_from_native_view(backend, sl_str_from_cstr(text), &value);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    backend->http_strings[index].Reset(isolate, value);
    *out = value;
    return sl_status_ok();
}

bool http_v8_cached_private(v8::Isolate* isolate, SlV8HttpPrivateKey name,
                            v8::Local<v8::Private>* out)
{
    SlV8Engine* backend =
        isolate == nullptr ? nullptr : static_cast<SlV8Engine*>(isolate->GetData(0));
    const char* text = http_v8_private_key_name(name);
    size_t index = static_cast<size_t>(name);
    v8::Local<v8::String> key;
    v8::Local<v8::Private> private_key;

    if (backend == nullptr || out == nullptr || text == nullptr ||
        index >= backend->http_private_keys.size())
    {
        return false;
    }

    if (!backend->http_private_keys[index].IsEmpty()) {
        *out = backend->http_private_keys[index].Get(isolate);
        return true;
    }

    if (!sl_status_is_ok(sl_v8_string_from_native_view(backend, sl_str_from_cstr(text), &key))) {
        return false;
    }

    private_key = v8::Private::ForApi(isolate, key);
    backend->http_private_keys[index].Reset(isolate, private_key);
    *out = private_key;
    return true;
}

bool http_v8_cached_function(v8::Isolate* isolate, v8::Local<v8::Context> context,
                             SlV8HttpFunctionKey key, v8::FunctionCallback callback,
                             v8::Local<v8::Function>* out)
{
    SlV8Engine* backend =
        isolate == nullptr ? nullptr : static_cast<SlV8Engine*>(isolate->GetData(0));
    size_t index = static_cast<size_t>(key);
    v8::Local<v8::FunctionTemplate> function_template;
    v8::Local<v8::Function> function;

    if (backend == nullptr || out == nullptr || callback == nullptr ||
        index >= backend->http_functions.size())
    {
        return false;
    }

    if (!backend->http_functions[index].IsEmpty()) {
        *out = backend->http_functions[index].Get(isolate);
        return true;
    }

    function_template = v8::FunctionTemplate::New(isolate, callback);
    function_template->RemovePrototype();
    if (!function_template->GetFunction(context).ToLocal(&function)) {
        return false;
    }
    if (!function->SetIntegrityLevel(context, v8::IntegrityLevel::kFrozen).FromMaybe(false)) {
        return false;
    }

    backend->http_functions[index].Reset(isolate, function);
    *out = function;
    return true;
}

bool http_v8_cached_log_noop_function(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                      v8::Local<v8::Function>* out)
{
    SlV8Engine* backend =
        isolate == nullptr ? nullptr : static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::Local<v8::String> source;
    v8::Local<v8::Script> script;
    v8::Local<v8::Value> value;
    v8::Local<v8::Function> function;

    if (backend == nullptr || out == nullptr) {
        return false;
    }
    if (!backend->http_log_noop_function.IsEmpty()) {
        *out = backend->http_log_noop_function.Get(isolate);
        return true;
    }

    if (!sl_status_is_ok(sl_v8_string_from_native_view(
            backend, sl_str_from_cstr("(function sloppyLogDisabled(){})"), &source)) ||
        !v8::Script::Compile(context, source).ToLocal(&script) ||
        !script->Run(context).ToLocal(&value) || !value->IsFunction())
    {
        return false;
    }
    function = value.As<v8::Function>();
    if (!function->SetIntegrityLevel(context, v8::IntegrityLevel::kFrozen).FromMaybe(false)) {
        return false;
    }

    backend->http_log_noop_function.Reset(isolate, function);
    *out = function;
    return true;
}

bool http_v8_prototype_set_function(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                    v8::Local<v8::Object> prototype, SlV8HttpStringKey name,
                                    SlV8HttpFunctionKey function_key, v8::FunctionCallback callback)
{
    v8::Local<v8::String> key;
    v8::Local<v8::Function> function;

    if (!sl_status_is_ok(http_v8_cached_string(isolate, name, &key)) ||
        !http_v8_cached_function(isolate, context, function_key, callback, &function))
    {
        return false;
    }

    return prototype->Set(context, key, function).FromMaybe(false);
}

bool http_v8_cached_prototype(v8::Isolate* isolate, v8::Local<v8::Context> context,
                              SlV8HttpPrototypeKey key, v8::Local<v8::Object>* out)
{
    SlV8Engine* backend =
        isolate == nullptr ? nullptr : static_cast<SlV8Engine*>(isolate->GetData(0));
    size_t index = static_cast<size_t>(key);
    v8::Local<v8::Object> prototype;

    if (backend == nullptr || out == nullptr || index >= backend->http_prototypes.size()) {
        return false;
    }

    if (!backend->http_prototypes[index].IsEmpty()) {
        *out = backend->http_prototypes[index].Get(isolate);
        return true;
    }

    prototype = v8::Object::New(isolate);
    switch (key) {
    case SL_V8_HTTP_PROTOTYPE_BODY:
        if (!http_v8_prototype_set_function(isolate, context, prototype, SL_V8_HTTP_STRING_BYTES,
                                            SL_V8_HTTP_FUNCTION_BODY_BYTES,
                                            http_v8_body_bytes_callback) ||
            !http_v8_prototype_set_function(isolate, context, prototype, SL_V8_HTTP_STRING_TEXT,
                                            SL_V8_HTTP_FUNCTION_BODY_TEXT,
                                            http_v8_body_text_callback) ||
            !http_v8_prototype_set_function(isolate, context, prototype, SL_V8_HTTP_STRING_JSON,
                                            SL_V8_HTTP_FUNCTION_BODY_JSON,
                                            http_v8_body_json_callback))
        {
            return false;
        }
        break;
    case SL_V8_HTTP_PROTOTYPE_HEADERS:
        if (!http_v8_prototype_set_function(isolate, context, prototype, SL_V8_HTTP_STRING_GET,
                                            SL_V8_HTTP_FUNCTION_HEADERS_GET,
                                            http_v8_headers_get_callback) ||
            !http_v8_prototype_set_function(isolate, context, prototype, SL_V8_HTTP_STRING_ENTRIES,
                                            SL_V8_HTTP_FUNCTION_HEADERS_ENTRIES,
                                            http_v8_headers_entries_callback))
        {
            return false;
        }
        break;
    case SL_V8_HTTP_PROTOTYPE_REQUEST:
        if (!http_v8_prototype_set_function(isolate, context, prototype, SL_V8_HTTP_STRING_BYTES,
                                            SL_V8_HTTP_FUNCTION_REQUEST_BYTES,
                                            http_v8_request_bytes_callback) ||
            !http_v8_prototype_set_function(isolate, context, prototype, SL_V8_HTTP_STRING_TEXT,
                                            SL_V8_HTTP_FUNCTION_REQUEST_TEXT,
                                            http_v8_request_text_callback) ||
            !http_v8_prototype_set_function(isolate, context, prototype, SL_V8_HTTP_STRING_JSON,
                                            SL_V8_HTTP_FUNCTION_REQUEST_JSON,
                                            http_v8_request_json_callback))
        {
            return false;
        }
        break;
    case SL_V8_HTTP_PROTOTYPE_SIGNAL:
        if (!http_v8_prototype_set_function(isolate, context, prototype,
                                            SL_V8_HTTP_STRING_THROW_IF_ABORTED,
                                            SL_V8_HTTP_FUNCTION_SIGNAL_THROW_IF_ABORTED,
                                            http_v8_signal_throw_if_aborted_callback))
        {
            return false;
        }
        break;
    case SL_V8_HTTP_PROTOTYPE_LOGGER:
        if (!http_v8_prototype_set_function(isolate, context, prototype, SL_V8_HTTP_STRING_TRACE,
                                            SL_V8_HTTP_FUNCTION_LOG_TRACE,
                                            http_v8_log_trace_callback) ||
            !http_v8_prototype_set_function(isolate, context, prototype, SL_V8_HTTP_STRING_DEBUG,
                                            SL_V8_HTTP_FUNCTION_LOG_DEBUG,
                                            http_v8_log_debug_callback) ||
            !http_v8_prototype_set_function(isolate, context, prototype, SL_V8_HTTP_STRING_INFO,
                                            SL_V8_HTTP_FUNCTION_LOG_INFO,
                                            http_v8_log_info_callback) ||
            !http_v8_prototype_set_function(isolate, context, prototype, SL_V8_HTTP_STRING_WARN,
                                            SL_V8_HTTP_FUNCTION_LOG_WARN,
                                            http_v8_log_warn_callback) ||
            !http_v8_prototype_set_function(isolate, context, prototype, SL_V8_HTTP_STRING_ERROR,
                                            SL_V8_HTTP_FUNCTION_LOG_ERROR,
                                            http_v8_log_error_callback) ||
            !http_v8_prototype_set_function(
                isolate, context, prototype, SL_V8_HTTP_STRING_IS_ENABLED,
                SL_V8_HTTP_FUNCTION_LOG_IS_ENABLED, http_v8_log_is_enabled_callback) ||
            !http_v8_prototype_set_function(
                isolate, context, prototype, SL_V8_HTTP_STRING_FOR_CATEGORY,
                SL_V8_HTTP_FUNCTION_LOG_FOR_CATEGORY, http_v8_log_for_category_callback))
        {
            return false;
        }
        break;
    case SL_V8_HTTP_PROTOTYPE_COUNT:
    default:
        return false;
    }

    if (!prototype->SetIntegrityLevel(context, v8::IntegrityLevel::kFrozen).FromMaybe(false)) {
        return false;
    }

    backend->http_prototypes[index].Reset(isolate, prototype);
    *out = prototype;
    return true;
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

SlStatus http_v8_copy_value_string(v8::Isolate* isolate, SlArena* arena, v8::Local<v8::Value> value,
                                   SlStr* out)
{
    return sl_v8_string_from_value_copy_to_arena(isolate, arena, value, out);
}

SlStatus http_v8_copy_value_bytes(v8::Isolate* isolate, SlArena* arena, v8::Local<v8::Value> value,
                                  SlBytes* out)
{
    return sl_v8_string_value_copy_bytes_to_arena(isolate, arena, value, out);
}

SlStatus http_v8_copy_binary_value_bytes(SlArena* arena, v8::Local<v8::Value> value, SlBytes* out)
{
    const unsigned char* bytes = nullptr;
    size_t length = 0U;
    SlOwnedBytes copied = {};

    if (arena == nullptr || out == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (value->IsArrayBufferView()) {
        v8::Local<v8::ArrayBufferView> view = value.As<v8::ArrayBufferView>();
        std::shared_ptr<v8::BackingStore> backing = view->Buffer()->GetBackingStore();
        size_t offset = view->ByteOffset();
        length = view->ByteLength();
        if (backing == nullptr || offset > backing->ByteLength() ||
            length > backing->ByteLength() - offset)
        {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
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
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        if (length != 0U) {
            bytes = static_cast<const unsigned char*>(backing->Data());
        }
    }
    else {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    SlStatus status = sl_bytes_copy_to_arena(arena, sl_bytes_from_parts(bytes, length), &copied);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    *out = sl_owned_bytes_as_view(copied);
    return sl_status_ok();
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

bool http_v8_set_string_property_key(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                     v8::Local<v8::Object> object, SlV8HttpStringKey name,
                                     SlStr value)
{
    v8::Local<v8::String> key;
    v8::Local<v8::String> local_value;

    if (!sl_status_is_ok(http_v8_cached_string(isolate, name, &key)) ||
        !sl_status_is_ok(http_v8_to_local_string(isolate, value, &local_value)))
    {
        return false;
    }

    return object->Set(context, key, local_value).FromMaybe(false);
}

bool http_v8_set_object_property_key(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                     v8::Local<v8::Object> object, SlV8HttpStringKey name,
                                     v8::Local<v8::Object> value)
{
    v8::Local<v8::String> key;

    if (!sl_status_is_ok(http_v8_cached_string(isolate, name, &key))) {
        return false;
    }

    return object->Set(context, key, value).FromMaybe(false);
}

bool http_v8_set_value_property_key(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                    v8::Local<v8::Object> object, SlV8HttpStringKey name,
                                    v8::Local<v8::Value> value)
{
    v8::Local<v8::String> key;

    if (!sl_status_is_ok(http_v8_cached_string(isolate, name, &key))) {
        return false;
    }

    return object->Set(context, key, value).FromMaybe(false);
}

bool http_v8_get_value_property_key(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                    v8::Local<v8::Object> object, SlV8HttpStringKey name,
                                    v8::Local<v8::Value>* out)
{
    v8::Local<v8::String> key;

    if (out == nullptr || !sl_status_is_ok(http_v8_cached_string(isolate, name, &key))) {
        return false;
    }

    return object->Get(context, key).ToLocal(out);
}

bool http_v8_set_bool_property_key(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                   v8::Local<v8::Object> object, SlV8HttpStringKey name, bool value)
{
    return http_v8_set_value_property_key(isolate, context, object, name,
                                          v8::Boolean::New(isolate, value));
}

bool http_v8_set_uint64_property_key(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                     v8::Local<v8::Object> object, SlV8HttpStringKey name,
                                     uint64_t value)
{
    return http_v8_set_value_property_key(isolate, context, object, name,
                                          v8::Number::New(isolate, static_cast<double>(value)));
}

bool http_v8_set_null_property_key(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                   v8::Local<v8::Object> object, SlV8HttpStringKey name)
{
    return http_v8_set_value_property_key(isolate, context, object, name, v8::Null(isolate));
}

bool http_v8_set_private_value(v8::Isolate* isolate, v8::Local<v8::Context> context,
                               v8::Local<v8::Object> object, SlV8HttpPrivateKey name,
                               v8::Local<v8::Value> value)
{
    v8::Local<v8::Private> key;

    if (!http_v8_cached_private(isolate, name, &key)) {
        return false;
    }

    return object->SetPrivate(context, key, value).FromMaybe(false);
}

bool http_v8_get_private_value(v8::Isolate* isolate, v8::Local<v8::Context> context,
                               v8::Local<v8::Object> object, SlV8HttpPrivateKey name,
                               v8::Local<v8::Value>* out)
{
    v8::Local<v8::Private> key;

    if (out == nullptr || !http_v8_cached_private(isolate, name, &key) ||
        !object->GetPrivate(context, key).ToLocal(out))
    {
        return false;
    }

    return true;
}

void http_v8_log_throw_type_error(v8::Isolate* isolate, const char* message)
{
    SlV8Engine* backend =
        isolate == nullptr ? nullptr : static_cast<SlV8Engine*>(isolate->GetData(0));

    if (backend != nullptr &&
        sl_v8_throw_type_error_from_native_view(backend, sl_str_from_cstr(message)))
    {
        return;
    }
    isolate->ThrowException(
        v8::Exception::TypeError(v8::String::NewFromUtf8(isolate, message).ToLocalChecked()));
}

bool http_v8_set_private_string(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                v8::Local<v8::Object> object, SlV8HttpPrivateKey name, SlStr value)
{
    v8::Local<v8::String> local_value;

    if (!sl_status_is_ok(http_v8_to_local_string(isolate, value, &local_value))) {
        return false;
    }
    return http_v8_set_private_value(isolate, context, object, name, local_value);
}

bool http_v8_get_private_string(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                v8::Local<v8::Object> object, SlV8HttpPrivateKey name,
                                std::string* out)
{
    v8::Local<v8::Value> value;

    if (out == nullptr || !http_v8_get_private_value(isolate, context, object, name, &value)) {
        return false;
    }
    if (!value->IsString()) {
        out->clear();
        return true;
    }
    *out = http_v8_value_to_string(isolate, value);
    return true;
}

bool http_v8_log_level_from_value(v8::Isolate* isolate, v8::Local<v8::Value> value,
                                  SlLogLevel* out_level)
{
    std::string level_text;
    SlStatus status;

    if (out_level == nullptr || !value->IsString()) {
        return false;
    }
    level_text = http_v8_value_to_string(isolate, value);
    status = sl_log_level_from_str(http_v8_str_from_string(level_text), out_level);
    return sl_status_is_ok(status) && *out_level != SL_LOG_LEVEL_OFF;
}

bool http_v8_log_append_field(v8::Isolate* isolate, v8::Local<v8::Context> context,
                              SlLogEventBuilder* builder, const std::string& key,
                              v8::Local<v8::Value> value)
{
    SlStatus status;

    if (key.empty()) {
        http_v8_log_throw_type_error(isolate, "Sloppy log field keys must be non-empty strings.");
        return false;
    }

    if (value->IsNull()) {
        status = sl_log_event_builder_add_null(builder, http_v8_str_from_string(key));
    }
    else if (value->IsBoolean()) {
        status = sl_log_event_builder_add_bool(builder, http_v8_str_from_string(key),
                                               value->BooleanValue(isolate));
    }
    else if (value->IsInt32()) {
        status = sl_log_event_builder_add_i64(
            builder, http_v8_str_from_string(key),
            static_cast<int64_t>(value->Int32Value(context).FromMaybe(0)));
    }
    else if (value->IsUint32()) {
        status = sl_log_event_builder_add_i64(
            builder, http_v8_str_from_string(key),
            static_cast<int64_t>(value->Uint32Value(context).FromMaybe(0U)));
    }
    else if (value->IsNumber()) {
        double number = value->NumberValue(context).FromMaybe(0.0);
        if (!std::isfinite(number)) {
            http_v8_log_throw_type_error(isolate, "Sloppy log number fields must be finite.");
            return false;
        }
        status = sl_log_event_builder_add_f64(builder, http_v8_str_from_string(key), number);
    }
    else if (value->IsString()) {
        std::string text = http_v8_value_to_string(isolate, value);
        status = sl_log_event_builder_add_string(builder, http_v8_str_from_string(key),
                                                 http_v8_str_from_string(text));
    }
    else {
        http_v8_log_throw_type_error(
            isolate, "Sloppy log fields support null, boolean, number, and string values.");
        return false;
    }

    if (!sl_status_is_ok(status)) {
        http_v8_log_throw_type_error(isolate, "Sloppy log field exceeded bounded event capacity.");
        return false;
    }
    return true;
}

bool http_v8_log_append_fields(v8::Isolate* isolate, v8::Local<v8::Context> context,
                               SlLogEventBuilder* builder, v8::Local<v8::Value> fields)
{
    v8::Local<v8::Object> object;
    v8::Local<v8::Array> names;

    if (fields->IsUndefined() || fields->IsNull()) {
        return true;
    }
    if (!fields->IsObject() || fields->IsArray() || fields->IsFunction()) {
        http_v8_log_throw_type_error(isolate, "Sloppy log fields must be a shallow plain object.");
        return false;
    }

    object = fields.As<v8::Object>();
    if (!object->GetOwnPropertyNames(context).ToLocal(&names)) {
        http_v8_log_throw_type_error(isolate, "Sloppy log fields could not be enumerated.");
        return false;
    }
    if (names->Length() > SL_LOG_MAX_FIELDS) {
        http_v8_log_throw_type_error(isolate, "Sloppy log fields exceed the maximum field count.");
        return false;
    }

    for (uint32_t index = 0U; index < names->Length(); index += 1U) {
        v8::Local<v8::Value> key_value;
        v8::Local<v8::Value> value;
        std::string key;

        if (!names->Get(context, index).ToLocal(&key_value) || !key_value->IsString() ||
            !object->Get(context, key_value).ToLocal(&value))
        {
            http_v8_log_throw_type_error(isolate, "Sloppy log field keys must be strings.");
            return false;
        }
        key = http_v8_value_to_string(isolate, key_value);
        if (!http_v8_log_append_field(isolate, context, builder, key, value)) {
            return false;
        }
    }

    return true;
}

SlV8HttpStringKey http_v8_log_method_key(SlLogLevel level)
{
    switch (level) {
    case SL_LOG_LEVEL_TRACE:
        return SL_V8_HTTP_STRING_TRACE;
    case SL_LOG_LEVEL_DEBUG:
        return SL_V8_HTTP_STRING_DEBUG;
    case SL_LOG_LEVEL_INFO:
        return SL_V8_HTTP_STRING_INFO;
    case SL_LOG_LEVEL_WARN:
        return SL_V8_HTTP_STRING_WARN;
    case SL_LOG_LEVEL_ERROR:
        return SL_V8_HTTP_STRING_ERROR;
    case SL_LOG_LEVEL_OFF:
    default:
        return SL_V8_HTTP_STRING_COUNT;
    }
}

bool http_v8_logger_shadow_disabled_method(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                           v8::Local<v8::Object> logger, SlLogLevel level)
{
    SlV8Engine* backend =
        isolate == nullptr ? nullptr : static_cast<SlV8Engine*>(isolate->GetData(0));
    SlV8HttpStringKey method_name = http_v8_log_method_key(level);
    v8::Local<v8::String> key;
    v8::Local<v8::Function> noop;

    if (backend == nullptr || method_name == SL_V8_HTTP_STRING_COUNT) {
        return false;
    }
    if (backend->logging != nullptr && sl_log_runtime_is_enabled(backend->logging, level)) {
        return true;
    }
    if (!sl_status_is_ok(http_v8_cached_string(isolate, method_name, &key)) ||
        !http_v8_cached_log_noop_function(isolate, context, &noop))
    {
        return false;
    }
    return logger
        ->DefineOwnProperty(context, key, noop,
                            static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete))
        .FromMaybe(false);
}

bool http_v8_logger_shadow_disabled_methods(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                            v8::Local<v8::Object> logger)
{
    return http_v8_logger_shadow_disabled_method(isolate, context, logger, SL_LOG_LEVEL_TRACE) &&
           http_v8_logger_shadow_disabled_method(isolate, context, logger, SL_LOG_LEVEL_DEBUG) &&
           http_v8_logger_shadow_disabled_method(isolate, context, logger, SL_LOG_LEVEL_INFO) &&
           http_v8_logger_shadow_disabled_method(isolate, context, logger, SL_LOG_LEVEL_WARN) &&
           http_v8_logger_shadow_disabled_method(isolate, context, logger, SL_LOG_LEVEL_ERROR);
}

bool http_v8_make_logger_object(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                SlStr category, SlStr request_id, SlStr route_name,
                                SlStr route_pattern, v8::Local<v8::Object>* out)
{
    v8::Local<v8::Object> logger = v8::Object::New(isolate);
    v8::Local<v8::Object> prototype;

    if (out == nullptr ||
        !http_v8_cached_prototype(isolate, context, SL_V8_HTTP_PROTOTYPE_LOGGER, &prototype) ||
        !logger->SetPrototype(context, prototype).FromMaybe(false) ||
        !http_v8_set_private_string(isolate, context, logger, SL_V8_HTTP_PRIVATE_LOG_CATEGORY,
                                    category) ||
        !http_v8_set_private_string(isolate, context, logger, SL_V8_HTTP_PRIVATE_LOG_REQUEST_ID,
                                    request_id) ||
        !http_v8_set_private_string(isolate, context, logger, SL_V8_HTTP_PRIVATE_LOG_ROUTE_NAME,
                                    route_name) ||
        !http_v8_set_private_string(isolate, context, logger, SL_V8_HTTP_PRIVATE_LOG_ROUTE_PATTERN,
                                    route_pattern) ||
        !http_v8_logger_shadow_disabled_methods(isolate, context, logger) ||
        !logger->SetIntegrityLevel(context, v8::IntegrityLevel::kFrozen).FromMaybe(false))
    {
        return false;
    }

    *out = logger;
    return true;
}

void http_v8_log_write_callback(const v8::FunctionCallbackInfo<v8::Value>& args, SlLogLevel level)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    std::string message;
    std::string category;
    std::string request_id;
    std::string route_name;
    std::string route_pattern;
    SlLogEventBuilder builder = {};
    SlLogEvent event = {};
    SlStatus status;

    if (backend == nullptr || backend->logging == nullptr ||
        !sl_log_runtime_is_enabled(backend->logging, level))
    {
        return;
    }
    if (args.Length() < 1 || args[0]->IsUndefined() || args[0]->IsSymbol()) {
        http_v8_log_throw_type_error(isolate, "Sloppy log message must be a stringable value.");
        return;
    }

    message = http_v8_value_to_string(isolate, args[0]);
    if (!http_v8_get_private_string(isolate, context, args.This(), SL_V8_HTTP_PRIVATE_LOG_CATEGORY,
                                    &category) ||
        !http_v8_get_private_string(isolate, context, args.This(),
                                    SL_V8_HTTP_PRIVATE_LOG_REQUEST_ID, &request_id) ||
        !http_v8_get_private_string(isolate, context, args.This(),
                                    SL_V8_HTTP_PRIVATE_LOG_ROUTE_NAME, &route_name) ||
        !http_v8_get_private_string(isolate, context, args.This(),
                                    SL_V8_HTTP_PRIVATE_LOG_ROUTE_PATTERN, &route_pattern))
    {
        http_v8_log_throw_type_error(isolate,
                                     "Sloppy log method was called with an invalid receiver.");
        return;
    }

    status = sl_log_event_builder_init(&builder, level, http_v8_str_from_string(message));
    if (sl_status_is_ok(status) && !category.empty()) {
        status = sl_log_event_builder_set_category(&builder, http_v8_str_from_string(category));
    }
    if (sl_status_is_ok(status) && !request_id.empty()) {
        status = sl_log_event_builder_set_request_id(&builder, http_v8_str_from_string(request_id));
    }
    if (sl_status_is_ok(status) && (!route_name.empty() || !route_pattern.empty())) {
        status = sl_log_event_builder_set_route(&builder, http_v8_str_from_string(route_name),
                                                http_v8_str_from_string(route_pattern));
    }
    if (!sl_status_is_ok(status)) {
        http_v8_log_throw_type_error(isolate, "Sloppy log event exceeded bounded event capacity.");
        return;
    }
    if (args.Length() >= 2 && !http_v8_log_append_fields(isolate, context, &builder, args[1])) {
        return;
    }
    status = sl_log_event_builder_finish(&builder, &event);
    if (!sl_status_is_ok(status)) {
        http_v8_log_throw_type_error(isolate, "Sloppy log event could not be completed.");
        return;
    }
    (void)sl_log_runtime_submit(backend->logging, &event);
}

void http_v8_log_trace_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    http_v8_log_write_callback(args, SL_LOG_LEVEL_TRACE);
}

void http_v8_log_debug_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    http_v8_log_write_callback(args, SL_LOG_LEVEL_DEBUG);
}

void http_v8_log_info_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    http_v8_log_write_callback(args, SL_LOG_LEVEL_INFO);
}

void http_v8_log_warn_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    http_v8_log_write_callback(args, SL_LOG_LEVEL_WARN);
}

void http_v8_log_error_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    http_v8_log_write_callback(args, SL_LOG_LEVEL_ERROR);
}

void http_v8_log_is_enabled_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    SlLogLevel level = SL_LOG_LEVEL_OFF;

    if (args.Length() < 1 || !http_v8_log_level_from_value(isolate, args[0], &level)) {
        http_v8_log_throw_type_error(isolate, "Sloppy log level is invalid.");
        return;
    }
    args.GetReturnValue().Set(backend != nullptr && backend->logging != nullptr &&
                              sl_log_runtime_is_enabled(backend->logging, level));
}

void http_v8_log_for_category_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::string category;
    std::string request_id;
    std::string route_name;
    std::string route_pattern;
    v8::Local<v8::Object> logger;

    if (args.Length() < 1 || !args[0]->IsString()) {
        http_v8_log_throw_type_error(isolate, "Sloppy log category must be a string.");
        return;
    }
    category = http_v8_value_to_string(isolate, args[0]);
    if (category.empty()) {
        http_v8_log_throw_type_error(isolate, "Sloppy log category must be non-empty.");
        return;
    }
    if (!http_v8_get_private_string(isolate, context, args.This(),
                                    SL_V8_HTTP_PRIVATE_LOG_REQUEST_ID, &request_id) ||
        !http_v8_get_private_string(isolate, context, args.This(),
                                    SL_V8_HTTP_PRIVATE_LOG_ROUTE_NAME, &route_name) ||
        !http_v8_get_private_string(isolate, context, args.This(),
                                    SL_V8_HTTP_PRIVATE_LOG_ROUTE_PATTERN, &route_pattern) ||
        !http_v8_make_logger_object(isolate, context, http_v8_str_from_string(category),
                                    http_v8_str_from_string(request_id),
                                    http_v8_str_from_string(route_name),
                                    http_v8_str_from_string(route_pattern), &logger))
    {
        http_v8_log_throw_type_error(isolate, "Sloppy category logger could not be created.");
        return;
    }

    args.GetReturnValue().Set(logger);
}

bool http_v8_make_uint8_array(v8::Isolate* isolate, SlBytes bytes, v8::Local<v8::Uint8Array>* out)
{
    if (isolate == nullptr || out == nullptr || (bytes.ptr == nullptr && bytes.length != 0U) ||
        bytes.length > static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        return false;
    }

    std::unique_ptr<v8::BackingStore> backing = v8::ArrayBuffer::NewBackingStore(
        isolate, bytes.length, v8::BackingStoreInitializationMode::kUninitialized,
        v8::BackingStoreOnFailureMode::kReturnNull);
    if (backing == nullptr) {
        return false;
    }
    if (bytes.length != 0U) {
        std::copy(bytes.ptr, bytes.ptr + bytes.length,
                  static_cast<unsigned char*>(backing->Data()));
    }

    v8::Local<v8::ArrayBuffer> buffer = v8::ArrayBuffer::New(isolate, std::move(backing));
    *out = v8::Uint8Array::New(buffer, 0U, bytes.length);
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

std::string http_v8_header_facade_property_name(const std::string& header_name)
{
    std::string property;
    bool uppercase_next = false;

    property.reserve(header_name.size());
    for (char ch : header_name) {
        if (ch == '-') {
            uppercase_next = !property.empty();
            continue;
        }
        if (uppercase_next && ch >= 'a' && ch <= 'z') {
            property.push_back(static_cast<char>(ch - 'a' + 'A'));
        }
        else {
            property.push_back(ch);
        }
        uppercase_next = false;
    }

    return property;
}

bool http_v8_make_header_facade(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                const SlHttpRequestHead* request, v8::Local<v8::Object>* out)
{
    std::vector<HttpV8HeaderEntry> header_entries;

    if (isolate == nullptr || out == nullptr || !http_v8_collect_headers(request, &header_entries))
    {
        return false;
    }

    v8::Local<v8::Object> facade = v8::Object::New(isolate);

    for (const HttpV8HeaderEntry& entry : header_entries) {
        std::string property_name = http_v8_header_facade_property_name(entry.name);
        v8::Local<v8::String> key;
        v8::Local<v8::String> value;

        if (!sl_status_is_ok(
                http_v8_to_local_string(isolate, http_v8_str_from_string(property_name), &key)) ||
            !sl_status_is_ok(
                http_v8_to_local_string(isolate, http_v8_str_from_string(entry.value), &value)) ||
            !facade->Set(context, key, value).FromMaybe(false))
        {
            return false;
        }
    }

    if (!facade->SetIntegrityLevel(context, v8::IntegrityLevel::kFrozen).FromMaybe(false)) {
        return false;
    }

    *out = facade;
    return true;
}

bool http_v8_push_u32(std::vector<unsigned char>* bytes, uint32_t value)
{
    if (bytes == nullptr) {
        return false;
    }

    bytes->push_back(static_cast<unsigned char>(value & 0xffU));
    bytes->push_back(static_cast<unsigned char>((value >> 8U) & 0xffU));
    bytes->push_back(static_cast<unsigned char>((value >> 16U) & 0xffU));
    bytes->push_back(static_cast<unsigned char>((value >> 24U) & 0xffU));
    return true;
}

bool http_v8_read_u32(const unsigned char* bytes, size_t length, size_t* offset, uint32_t* out)
{
    size_t cursor;

    if (bytes == nullptr || offset == nullptr || out == nullptr || *offset > length ||
        length - *offset < 4U)
    {
        return false;
    }

    cursor = *offset;
    *out = static_cast<uint32_t>(bytes[cursor]) |
           (static_cast<uint32_t>(bytes[cursor + 1U]) << 8U) |
           (static_cast<uint32_t>(bytes[cursor + 2U]) << 16U) |
           (static_cast<uint32_t>(bytes[cursor + 3U]) << 24U);
    *offset = cursor + 4U;
    return true;
}

bool http_v8_make_header_snapshot(v8::Isolate* isolate,
                                  const std::vector<HttpV8HeaderEntry>& header_entries,
                                  v8::Local<v8::ArrayBuffer>* out)
{
    std::vector<unsigned char> snapshot;
    std::unique_ptr<v8::BackingStore> backing;

    if (isolate == nullptr || out == nullptr ||
        header_entries.size() > static_cast<size_t>(std::numeric_limits<uint32_t>::max()))
    {
        return false;
    }

    snapshot.reserve(4U + (header_entries.size() * 16U));
    if (!http_v8_push_u32(&snapshot, static_cast<uint32_t>(header_entries.size()))) {
        return false;
    }

    for (const HttpV8HeaderEntry& entry : header_entries) {
        if (entry.name.size() > static_cast<size_t>(std::numeric_limits<uint32_t>::max()) ||
            entry.value.size() > static_cast<size_t>(std::numeric_limits<uint32_t>::max()))
        {
            return false;
        }
        if (!http_v8_push_u32(&snapshot, static_cast<uint32_t>(entry.name.size())) ||
            !http_v8_push_u32(&snapshot, static_cast<uint32_t>(entry.value.size())))
        {
            return false;
        }
        snapshot.insert(snapshot.end(), entry.name.begin(), entry.name.end());
        snapshot.insert(snapshot.end(), entry.value.begin(), entry.value.end());
    }

    backing = v8::ArrayBuffer::NewBackingStore(isolate, snapshot.size(),
                                               v8::BackingStoreInitializationMode::kUninitialized,
                                               v8::BackingStoreOnFailureMode::kReturnNull);
    if (backing == nullptr) {
        return false;
    }
    if (!snapshot.empty()) {
        unsigned char* output = static_cast<unsigned char*>(backing->Data());
        std::copy(snapshot.begin(), snapshot.end(), output);
    }

    *out = v8::ArrayBuffer::New(isolate, std::move(backing));
    return true;
}

bool http_v8_snapshot_bytes(v8::Local<v8::Value> value, const unsigned char** out_bytes,
                            size_t* out_length)
{
    std::shared_ptr<v8::BackingStore> backing;

    if (out_bytes == nullptr || out_length == nullptr || !value->IsArrayBuffer()) {
        return false;
    }

    backing = value.As<v8::ArrayBuffer>()->GetBackingStore();
    if (backing == nullptr) {
        return false;
    }

    *out_bytes = static_cast<const unsigned char*>(backing->Data());
    *out_length = backing->ByteLength();
    return *out_bytes != nullptr || *out_length == 0U;
}

bool http_v8_header_snapshot_find(const unsigned char* bytes, size_t length,
                                  const std::string& wanted, SlStr* out_value, bool* out_found)
{
    size_t offset = 0U;
    uint32_t count = 0U;

    if (bytes == nullptr || out_value == nullptr || out_found == nullptr ||
        !http_v8_read_u32(bytes, length, &offset, &count))
    {
        return false;
    }

    *out_found = false;
    for (uint32_t index = 0U; index < count; index += 1U) {
        uint32_t name_length = 0U;
        uint32_t value_length = 0U;
        const unsigned char* name = nullptr;
        const unsigned char* value = nullptr;

        if (!http_v8_read_u32(bytes, length, &offset, &name_length) ||
            !http_v8_read_u32(bytes, length, &offset, &value_length) || offset > length ||
            length - offset < static_cast<size_t>(name_length) ||
            length - offset - static_cast<size_t>(name_length) < static_cast<size_t>(value_length))
        {
            return false;
        }

        name = bytes + offset;
        offset += static_cast<size_t>(name_length);
        value = bytes + offset;
        offset += static_cast<size_t>(value_length);

        SlStr name_view = sl_str_from_parts(reinterpret_cast<const char*>(name),
                                            static_cast<size_t>(name_length));
        SlStr wanted_view = sl_str_from_parts(wanted.data(), wanted.size());
        if (sl_str_equal(name_view, wanted_view)) {
            *out_value = sl_str_from_parts(reinterpret_cast<const char*>(value),
                                           static_cast<size_t>(value_length));
            *out_found = true;
            return true;
        }
    }

    *out_value = sl_str_empty();
    return true;
}

void http_v8_headers_get_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Value> snapshot_value;
    const unsigned char* snapshot = nullptr;
    size_t snapshot_length = 0U;
    SlStr value = sl_str_empty();
    bool found = false;
    v8::Local<v8::String> local_value;

    if (args.Length() < 1 || !args[0]->IsString() ||
        !http_v8_get_private_value(isolate, context, args.This(),
                                   SL_V8_HTTP_PRIVATE_HEADER_SNAPSHOT, &snapshot_value) ||
        !http_v8_snapshot_bytes(snapshot_value, &snapshot, &snapshot_length))
    {
        args.GetReturnValue().Set(v8::Null(isolate));
        return;
    }

    std::string wanted = http_v8_ascii_lower_value(isolate, args[0]);
    if (!http_v8_header_snapshot_find(snapshot, snapshot_length, wanted, &value, &found)) {
        args.GetReturnValue().Set(v8::Null(isolate));
        return;
    }

    if (!found) {
        args.GetReturnValue().Set(v8::Null(isolate));
        return;
    }

    if (!sl_status_is_ok(http_v8_to_local_string(isolate, value, &local_value))) {
        args.GetReturnValue().Set(v8::Null(isolate));
        return;
    }

    args.GetReturnValue().Set(local_value);
}

void http_v8_headers_entries_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Value> snapshot_value;
    const unsigned char* snapshot = nullptr;
    size_t snapshot_length = 0U;
    size_t offset = 0U;
    uint32_t count = 0U;

    if (!http_v8_get_private_value(isolate, context, args.This(),
                                   SL_V8_HTTP_PRIVATE_HEADER_SNAPSHOT, &snapshot_value) ||
        !http_v8_snapshot_bytes(snapshot_value, &snapshot, &snapshot_length) ||
        !http_v8_read_u32(snapshot, snapshot_length, &offset, &count))
    {
        args.GetReturnValue().Set(v8::Array::New(isolate, 0));
        return;
    }

    v8::Local<v8::Array> entries = v8::Array::New(isolate, static_cast<int>(count));
    for (uint32_t index = 0U; index < count; index += 1U) {
        uint32_t name_length = 0U;
        uint32_t value_length = 0U;
        const unsigned char* name = nullptr;
        const unsigned char* value = nullptr;
        v8::Local<v8::String> local_name;
        v8::Local<v8::String> local_value;
        v8::Local<v8::Array> pair = v8::Array::New(isolate, 2);

        if (!http_v8_read_u32(snapshot, snapshot_length, &offset, &name_length) ||
            !http_v8_read_u32(snapshot, snapshot_length, &offset, &value_length) ||
            offset > snapshot_length ||
            snapshot_length - offset < static_cast<size_t>(name_length) ||
            snapshot_length - offset - static_cast<size_t>(name_length) <
                static_cast<size_t>(value_length))
        {
            args.GetReturnValue().Set(v8::Array::New(isolate, 0));
            return;
        }

        name = snapshot + offset;
        offset += static_cast<size_t>(name_length);
        value = snapshot + offset;
        offset += static_cast<size_t>(value_length);

        if (!sl_status_is_ok(
                http_v8_to_local_string(isolate,
                                        sl_str_from_parts(reinterpret_cast<const char*>(name),
                                                          static_cast<size_t>(name_length)),
                                        &local_name)) ||
            !sl_status_is_ok(
                http_v8_to_local_string(isolate,
                                        sl_str_from_parts(reinterpret_cast<const char*>(value),
                                                          static_cast<size_t>(value_length)),
                                        &local_value)) ||
            !pair->Set(context, 0U, local_name).FromMaybe(false) ||
            !pair->Set(context, 1U, local_value).FromMaybe(false) ||
            !entries->Set(context, index, pair).FromMaybe(false))
        {
            args.GetReturnValue().Set(v8::Array::New(isolate, 0));
            return;
        }
    }

    args.GetReturnValue().Set(entries);
}

bool http_v8_body_mark_consumed(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                v8::Local<v8::Object> body)
{
    v8::Local<v8::Value> consumed;

    if (!http_v8_get_private_value(isolate, context, body, SL_V8_HTTP_PRIVATE_BODY_CONSUMED,
                                   &consumed))
    {
        return false;
    }
    if (consumed->BooleanValue(isolate)) {
        SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
        (void)http_v8_set_bool_property_key(isolate, context, body, SL_V8_HTTP_STRING_CONSUMED,
                                            true);
        if (!sl_v8_throw_type_error_from_native_view(
                backend, http_v8_literal("Request body is already consumed.",
                                         sizeof("Request body is already consumed.") - 1U)))
        {
            isolate->ThrowException(v8::Exception::TypeError(
                v8::String::NewFromUtf8Literal(isolate, "Request body is already consumed.")));
        }
        return false;
    }

    return http_v8_set_private_value(isolate, context, body, SL_V8_HTTP_PRIVATE_BODY_CONSUMED,
                                     v8::Boolean::New(isolate, true)) &&
           http_v8_set_bool_property_key(isolate, context, body, SL_V8_HTTP_STRING_CONSUMED, true);
}

bool http_v8_uint8_array_bytes(v8::Local<v8::Value> value, const unsigned char** out_bytes,
                               size_t* out_length)
{
    v8::Local<v8::Uint8Array> view;
    std::shared_ptr<v8::BackingStore> backing;
    size_t offset;
    size_t length;
    size_t backing_length;

    if (out_bytes == nullptr || out_length == nullptr || !value->IsUint8Array()) {
        return false;
    }

    view = value.As<v8::Uint8Array>();
    backing = view->Buffer()->GetBackingStore();
    if (backing == nullptr) {
        return false;
    }

    offset = view->ByteOffset();
    length = view->ByteLength();
    backing_length = backing->ByteLength();
    if (offset > backing_length || length > backing_length - offset) {
        return false;
    }

    const unsigned char* data = static_cast<const unsigned char*>(backing->Data());
    if (data == nullptr) {
        if (length != 0U) {
            return false;
        }
        *out_bytes = nullptr;
        *out_length = 0U;
        return true;
    }

    *out_bytes = data + offset;
    *out_length = length;
    return true;
}

bool http_v8_body_text_value(v8::Isolate* isolate, v8::Local<v8::Context> context,
                             v8::Local<v8::Object> object, v8::Local<v8::Value>* out)
{
    v8::Local<v8::Value> body;
    v8::Local<v8::Value> body_bytes_value;
    const unsigned char* body_bytes = nullptr;
    size_t body_length = 0U;
    v8::Local<v8::String> body_text;

    if (out == nullptr) {
        return false;
    }

    if (http_v8_get_private_value(isolate, context, object, SL_V8_HTTP_PRIVATE_BODY, &body) &&
        body->IsString())
    {
        *out = body;
        return true;
    }

    if (!http_v8_get_private_value(isolate, context, object, SL_V8_HTTP_PRIVATE_BODY_BYTES,
                                   &body_bytes_value) ||
        !http_v8_uint8_array_bytes(body_bytes_value, &body_bytes, &body_length))
    {
        body_text = v8::String::Empty(isolate);
    }
    else if (!sl_status_is_ok(http_v8_to_local_string(
                 isolate, sl_str_from_parts(reinterpret_cast<const char*>(body_bytes), body_length),
                 &body_text)))
    {
        return false;
    }

    if (!http_v8_set_private_value(isolate, context, object, SL_V8_HTTP_PRIVATE_BODY, body_text)) {
        return false;
    }

    *out = body_text;
    return true;
}

void http_v8_request_text_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Value> body;

    if (!http_v8_body_text_value(isolate, context, args.This(), &body)) {
        args.GetReturnValue().Set(v8::String::Empty(isolate));
        return;
    }

    args.GetReturnValue().Set(body);
}

void http_v8_request_bytes_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Value> body_bytes;

    if (!http_v8_get_private_value(isolate, context, args.This(), SL_V8_HTTP_PRIVATE_BODY_BYTES,
                                   &body_bytes) ||
        !body_bytes->IsUint8Array())
    {
        v8::Local<v8::Uint8Array> empty;
        if (!http_v8_make_uint8_array(isolate, sl_bytes_from_parts(nullptr, 0U), &empty)) {
            return;
        }
        args.GetReturnValue().Set(empty);
        return;
    }

    args.GetReturnValue().Set(body_bytes);
}

void http_v8_request_json_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Value> kind;
    v8::Local<v8::Value> body;

    if (!http_v8_get_private_value(isolate, context, args.This(), SL_V8_HTTP_PRIVATE_BODY_KIND,
                                   &kind) ||
        !kind->IsString() || http_v8_value_to_string(isolate, kind) != "json" ||
        !http_v8_body_text_value(isolate, context, args.This(), &body) || !body->IsString())
    {
        SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
        if (!sl_v8_throw_type_error_from_native_view(
                backend, http_v8_literal("Request body is not available as JSON.",
                                         sizeof("Request body is not available as JSON.") - 1U)))
        {
            isolate->ThrowException(v8::Exception::TypeError(
                v8::String::NewFromUtf8Literal(isolate, "Request body is not available as JSON.")));
        }
        return;
    }

    v8::Local<v8::Value> parsed;
    if (!v8::JSON::Parse(context, body.As<v8::String>()).ToLocal(&parsed)) {
        return;
    }

    args.GetReturnValue().Set(parsed);
}

void http_v8_body_text_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Object> body_object = args.This();
    v8::Local<v8::Value> body;

    if (!http_v8_body_mark_consumed(isolate, context, body_object) ||
        !http_v8_body_text_value(isolate, context, body_object, &body))
    {
        return;
    }

    args.GetReturnValue().Set(body);
}

void http_v8_body_bytes_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Object> body_object = args.This();
    v8::Local<v8::Value> body_bytes;

    if (!http_v8_body_mark_consumed(isolate, context, body_object) ||
        !http_v8_get_private_value(isolate, context, body_object, SL_V8_HTTP_PRIVATE_BODY_BYTES,
                                   &body_bytes) ||
        !body_bytes->IsUint8Array())
    {
        v8::Local<v8::Uint8Array> empty;
        if (!http_v8_make_uint8_array(isolate, sl_bytes_from_parts(nullptr, 0U), &empty)) {
            return;
        }
        args.GetReturnValue().Set(empty);
        return;
    }

    args.GetReturnValue().Set(body_bytes);
}

void http_v8_body_json_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Object> body_object = args.This();
    v8::Local<v8::Value> kind;
    v8::Local<v8::Value> body;

    if (!http_v8_body_mark_consumed(isolate, context, body_object)) {
        return;
    }

    if (!http_v8_get_private_value(isolate, context, body_object, SL_V8_HTTP_PRIVATE_BODY_KIND,
                                   &kind) ||
        !kind->IsString() || http_v8_value_to_string(isolate, kind) != "json" ||
        !http_v8_body_text_value(isolate, context, body_object, &body) || !body->IsString())
    {
        SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
        if (!sl_v8_throw_type_error_from_native_view(
                backend, http_v8_literal("Request body is not available as JSON.",
                                         sizeof("Request body is not available as JSON.") - 1U)))
        {
            isolate->ThrowException(v8::Exception::TypeError(
                v8::String::NewFromUtf8Literal(isolate, "Request body is not available as JSON.")));
        }
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

    if (!http_v8_get_private_value(isolate, context, self, SL_V8_HTTP_PRIVATE_ABORTED,
                                   &aborted_value) ||
        !aborted_value->BooleanValue(isolate))
    {
        args.GetReturnValue().Set(v8::Undefined(isolate));
        return;
    }

    if (http_v8_get_private_value(isolate, context, self, SL_V8_HTTP_PRIVATE_REASON,
                                  &reason_value) &&
        reason_value->IsString())
    {
        std::string reason = http_v8_value_to_string(isolate, reason_value);
        if (!reason.empty()) {
            message += ": ";
            message += reason;
        }
    }

    v8::Local<v8::String> error_message;
    if (!sl_status_is_ok(
            http_v8_to_local_string(isolate, http_v8_str_from_string(message), &error_message)))
    {
        SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
        (void)sl_v8_throw_error_from_native_view(
            backend, http_v8_literal("Sloppy request was cancelled",
                                     sizeof("Sloppy request was cancelled") - 1U));
        return;
    }
    isolate->ThrowException(v8::Exception::Error(error_message));
}

bool http_v8_make_signal_object(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                const SlCancellationToken* cancellation, v8::Local<v8::Object>* out)
{
    v8::Local<v8::Object> signal = v8::Object::New(isolate);
    v8::Local<v8::Object> signal_prototype;
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

    if (!http_v8_cached_prototype(isolate, context, SL_V8_HTTP_PROTOTYPE_SIGNAL,
                                  &signal_prototype) ||
        !signal->SetPrototype(context, signal_prototype).FromMaybe(false) ||
        !http_v8_set_bool_property_key(isolate, context, signal, SL_V8_HTTP_STRING_ABORTED,
                                       aborted) ||
        !http_v8_set_private_value(isolate, context, signal, SL_V8_HTTP_PRIVATE_ABORTED,
                                   v8::Boolean::New(isolate, aborted)))
    {
        return false;
    }
    if (aborted) {
        v8::Local<v8::String> reason_value;
        if (!sl_status_is_ok(http_v8_to_local_string(isolate, reason, &reason_value)) ||
            !http_v8_set_string_property_key(isolate, context, signal, SL_V8_HTTP_STRING_REASON,
                                             reason) ||
            !http_v8_set_private_value(isolate, context, signal, SL_V8_HTTP_PRIVATE_REASON,
                                       reason_value))
        {
            return false;
        }
    }
    else if (!http_v8_set_null_property_key(isolate, context, signal, SL_V8_HTTP_STRING_REASON) ||
             !http_v8_set_private_value(isolate, context, signal, SL_V8_HTTP_PRIVATE_REASON,
                                        v8::Null(isolate)))
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
    v8::Local<v8::Object> headers_prototype;
    v8::Local<v8::Value> snapshot = v8::Undefined(isolate);
    std::vector<HttpV8HeaderEntry> header_entries;

    if (out == nullptr || !http_v8_collect_headers(request, &header_entries) ||
        header_entries.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        return false;
    }

    if (!header_entries.empty()) {
        v8::Local<v8::ArrayBuffer> snapshot_buffer;
        if (!http_v8_make_header_snapshot(isolate, header_entries, &snapshot_buffer)) {
            return false;
        }
        snapshot = snapshot_buffer;
    }

    if (!http_v8_cached_prototype(isolate, context, SL_V8_HTTP_PROTOTYPE_HEADERS,
                                  &headers_prototype) ||
        !headers->SetPrototype(context, headers_prototype).FromMaybe(false) ||
        !http_v8_set_private_value(isolate, context, headers, SL_V8_HTTP_PRIVATE_HEADER_SNAPSHOT,
                                   snapshot))
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
    case SL_HTTP_REQUEST_BODY_BYTES:
        return sl_str_from_cstr("bytes");
    case SL_HTTP_REQUEST_BODY_NONE:
    default:
        return sl_str_from_cstr("none");
    }
}

bool http_v8_get_object_status(v8::Isolate* isolate, v8::Local<v8::Context> context,
                               v8::Local<v8::Object> object, uint16_t* out)
{
    v8::Local<v8::Value> value;
    int32_t status = 0;

    if (out == nullptr ||
        !http_v8_get_value_property_key(isolate, context, object, SL_V8_HTTP_STRING_STATUS,
                                        &value) ||
        !value->IsInt32())
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
    v8::Local<v8::Value> value;

    if (!http_v8_get_value_property_key(isolate, context, object, SL_V8_HTTP_STRING_SLOPPY_RESULT,
                                        &value))
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
    for (char raw_ch : value) {
        unsigned char ch = static_cast<unsigned char>(raw_ch);
        if ((ch < 0x20U && ch != '\t') || ch == 0x7FU) {
            return false;
        }
    }
    return true;
}

bool http_v8_header_value_view_safe(SlStr value)
{
    if (value.ptr == nullptr && value.length != 0U) {
        return false;
    }

    for (size_t index = 0U; index < value.length; index += 1U) {
        unsigned char ch = static_cast<unsigned char>(value.ptr[index]);
        if ((ch < 0x20U && ch != '\t') || ch == 0x7FU) {
            return false;
        }
    }

    return true;
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

    return lowered == "connection" || lowered == "content-type" || lowered == "content-length" ||
           lowered == "transfer-encoding" || lowered == "keep-alive";
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
                                              v8::Local<v8::Object> object, SlV8HttpStringKey name,
                                              bool* out_present, std::string* out)
{
    v8::Local<v8::Value> value;

    if (out_present == nullptr || out == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_present = false;
    *out = std::string();
    if (!http_v8_get_value_property_key(isolate, context, object, name, &value)) {
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

SlStatus http_v8_get_object_string_copy(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                        SlArena* arena, v8::Local<v8::Object> object,
                                        SlV8HttpStringKey name, SlStr* out)
{
    v8::Local<v8::Value> value;

    if (out == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (!http_v8_get_value_property_key(isolate, context, object, name, &value)) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    if (!value->IsString()) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    return http_v8_copy_value_string(isolate, arena, value, out);
}

SlStatus http_v8_copy_result_headers(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                     SlEngine* engine, SlArena* arena,
                                     v8::Local<v8::Object> descriptor, SlHttpHeader** out_headers,
                                     size_t* out_header_count, SlDiag* out_diag)
{
    v8::Local<v8::Value> headers_value;
    v8::Local<v8::Object> headers_object;
    v8::Local<v8::Array> names;
    bool has_headers_object = false;
    bool has_location = false;
    std::string location;
    SlSlice header_storage = {nullptr, 0U, sizeof(SlHttpHeader)};
    size_t total_header_count = 0U;
    size_t index = 0U;
    SlHttpHeader* headers = nullptr;
    SlStatus status;

    if (engine == nullptr || arena == nullptr || out_headers == nullptr ||
        out_header_count == nullptr)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_headers = nullptr;
    *out_header_count = 0U;

    status = http_v8_get_optional_string_property(
        isolate, context, descriptor, SL_V8_HTTP_STRING_LOCATION, &has_location, &location);
    if (sl_status_code(status) == SL_STATUS_INVALID_ARGUMENT) {
        return http_v8_write_invalid_headers_diag(engine, out_diag);
    }
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (has_location && !http_v8_header_value_safe(location)) {
        return http_v8_write_invalid_headers_diag(engine, out_diag);
    }

    if (!http_v8_get_value_property_key(isolate, context, descriptor, SL_V8_HTTP_STRING_HEADERS,
                                        &headers_value))
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

    status = sl_arena_array_alloc(arena, total_header_count, sizeof(SlHttpHeader),
                                  alignof(SlHttpHeader), &header_storage);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    headers = static_cast<SlHttpHeader*>(header_storage.ptr);

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

bool http_v8_stringify_json(v8::Local<v8::Context> context, v8::Local<v8::Value> value,
                            v8::Local<v8::String>* out)
{
    if (out == nullptr || !v8::JSON::Stringify(context, value).ToLocal(out)) {
        return false;
    }

    return true;
}

typedef enum HttpV8FastResultKind
{
    HTTP_V8_FAST_RESULT_NONE = 0,
    HTTP_V8_FAST_RESULT_TEXT_OK = 1,
    HTTP_V8_FAST_RESULT_NO_CONTENT = 2,
    HTTP_V8_FAST_RESULT_JSON = 3,
    HTTP_V8_FAST_RESULT_CREATED = 4
} HttpV8FastResultKind;

bool http_v8_get_optional_int32_property_key(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                             v8::Local<v8::Object> object, SlV8HttpStringKey name,
                                             int32_t* out)
{
    v8::Local<v8::Value> value;

    if (out == nullptr || !http_v8_get_value_property_key(isolate, context, object, name, &value)) {
        return false;
    }
    if (value->IsUndefined() || value->IsNull()) {
        *out = 0;
        return true;
    }
    if (!value->IsInt32()) {
        return false;
    }

    *out = value.As<v8::Int32>()->Value();
    return true;
}

bool http_v8_property_is_undefined_or_null(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                           v8::Local<v8::Object> object, SlV8HttpStringKey name)
{
    v8::Local<v8::Value> value;

    return http_v8_get_value_property_key(isolate, context, object, name, &value) &&
           (value->IsUndefined() || value->IsNull());
}

bool http_v8_string_property_equals(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                    v8::Local<v8::Object> object, SlV8HttpStringKey name,
                                    const char* expected)
{
    v8::Local<v8::Value> value;

    if (expected == nullptr ||
        !http_v8_get_value_property_key(isolate, context, object, name, &value) ||
        !value->IsString())
    {
        return false;
    }

    return http_v8_value_to_string(isolate, value) == expected;
}

SlStatus http_v8_copy_fast_json_text(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                     SlArena* arena, v8::Local<v8::Object> object, SlBytes* out)
{
    v8::Local<v8::Value> value;

    if (out == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = {};
    if (!http_v8_get_value_property_key(isolate, context, object, SL_V8_HTTP_STRING_FAST_JSON_TEXT,
                                        &value))
    {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    if (!value->IsString()) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    return http_v8_copy_value_bytes(isolate, arena, value, out);
}

bool http_v8_fast_content_type_matches(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                       v8::Local<v8::Object> object, const char* expected)
{
    return http_v8_string_property_equals(isolate, context, object, SL_V8_HTTP_STRING_CONTENT_TYPE,
                                          expected);
}

bool http_v8_fast_result_has_no_custom_headers(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                               v8::Local<v8::Object> object)
{
    return http_v8_property_is_undefined_or_null(isolate, context, object,
                                                 SL_V8_HTTP_STRING_HEADERS);
}

SlStatus http_v8_try_convert_fast_result(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                         SlEngine* engine, SlArena* arena,
                                         v8::Local<v8::Object> object, SlEngineResult* out_result,
                                         bool* out_handled)
{
    static const char* const kTextContentType = "text/plain; charset=utf-8";
    static const char* const kJsonContentType = "application/json; charset=utf-8";
    int32_t fast_kind = 0;
    uint16_t status_code = 0U;
    SlStatus status;

    (void)engine;
    if (out_handled == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_handled = false;
    if (!http_v8_get_optional_int32_property_key(isolate, context, object,
                                                 SL_V8_HTTP_STRING_FAST_RESULT, &fast_kind) ||
        fast_kind == HTTP_V8_FAST_RESULT_NONE)
    {
        return sl_status_ok();
    }
    if (!http_v8_get_object_status(isolate, context, object, &status_code) ||
        !http_v8_fast_result_has_no_custom_headers(isolate, context, object))
    {
        return sl_status_ok();
    }

    if (fast_kind == HTTP_V8_FAST_RESULT_TEXT_OK) {
        if (status_code != 200U ||
            !http_v8_string_property_equals(isolate, context, object, SL_V8_HTTP_STRING_KIND,
                                            "text") ||
            !http_v8_fast_content_type_matches(isolate, context, object, kTextContentType) ||
            !http_v8_string_property_equals(isolate, context, object, SL_V8_HTTP_STRING_BODY_RESULT,
                                            "ok"))
        {
            return sl_status_ok();
        }

        out_result->kind = SL_ENGINE_RESULT_TEXT;
        out_result->text = sl_str_from_cstr("ok");
        out_result->response = sl_http_response_text(200U, out_result->text);
        *out_handled = true;
        return sl_status_ok();
    }

    if (fast_kind == HTTP_V8_FAST_RESULT_NO_CONTENT) {
        if (status_code != 204U ||
            !http_v8_string_property_equals(isolate, context, object, SL_V8_HTTP_STRING_KIND,
                                            "empty") ||
            !http_v8_property_is_undefined_or_null(isolate, context, object,
                                                   SL_V8_HTTP_STRING_CONTENT_TYPE))
        {
            return sl_status_ok();
        }

        out_result->kind = SL_ENGINE_RESULT_NONE;
        out_result->response = sl_http_response_empty(204U);
        *out_handled = true;
        return sl_status_ok();
    }

    if (fast_kind == HTTP_V8_FAST_RESULT_JSON || fast_kind == HTTP_V8_FAST_RESULT_CREATED) {
        SlBytes bytes = {nullptr, 0U};

        if ((fast_kind == HTTP_V8_FAST_RESULT_JSON && status_code != 200U) ||
            (fast_kind == HTTP_V8_FAST_RESULT_CREATED && status_code != 201U) ||
            !http_v8_string_property_equals(isolate, context, object, SL_V8_HTTP_STRING_KIND,
                                            "json") ||
            !http_v8_fast_content_type_matches(isolate, context, object, kJsonContentType))
        {
            return sl_status_ok();
        }

        status = http_v8_copy_fast_json_text(isolate, context, arena, object, &bytes);
        if (sl_status_code(status) == SL_STATUS_INVALID_ARGUMENT) {
            return sl_status_ok();
        }
        if (!sl_status_is_ok(status)) {
            return status;
        }

        out_result->kind = SL_ENGINE_RESULT_JSON;
        out_result->response = sl_http_response_json(status_code, bytes);
        if (fast_kind == HTTP_V8_FAST_RESULT_CREATED) {
            bool has_location = false;
            std::string location;
            SlSlice header_storage = {nullptr, 0U, sizeof(SlHttpHeader)};
            SlHttpHeader* header = nullptr;

            status = http_v8_get_optional_string_property(
                isolate, context, object, SL_V8_HTTP_STRING_LOCATION, &has_location, &location);
            if (!sl_status_is_ok(status) || !has_location || !http_v8_header_value_safe(location)) {
                return sl_status_ok();
            }

            status = sl_arena_array_alloc(arena, 1U, sizeof(SlHttpHeader), alignof(SlHttpHeader),
                                          &header_storage);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            header = static_cast<SlHttpHeader*>(header_storage.ptr);
            status = http_v8_copy_response_header(arena, "Location", location, header);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            out_result->response.headers = header;
            out_result->response.header_count = 1U;
        }
        *out_handled = true;
        return sl_status_ok();
    }

    return sl_status_ok();
}

} // namespace

bool sl_v8_make_http_context_object(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                    const SlHttpRequestContext* request_context,
                                    v8::Local<v8::Object>* out)
{
    v8::Local<v8::Object> ctx = v8::Object::New(isolate);
    v8::Local<v8::Object> route;
    v8::Local<v8::Object> query;
    v8::Local<v8::Object> request;
    v8::Local<v8::Object> body_object;
    v8::Local<v8::Object> connection;
    v8::Local<v8::Object> logger;
    v8::Local<v8::Object> body_prototype;
    v8::Local<v8::Object> request_prototype;
    v8::Local<v8::Value> body_bytes_value = v8::Undefined(isolate);
    v8::Local<v8::String> body_kind_value;
    v8::Local<v8::Object> headers;
    v8::Local<v8::Object> header_facade;
    v8::Local<v8::Object> signal;
    size_t index = 0U;
    SlStr method = sl_str_empty();
    std::string request_id;
    std::string connection_id;

    if (request_context == nullptr || request_context->request == nullptr || out == nullptr) {
        return false;
    }
    if (request_context->needs_request) {
        method = http_v8_request_method_name(request_context->request->method);
        if (sl_str_is_empty(method)) {
            return false;
        }
    }

    if (request_context->needs_route_params) {
        route = v8::Object::New(isolate);
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
    }

    if (request_context->needs_query_params) {
        query = v8::Object::New(isolate);
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
    }

    if (request_context->needs_signal &&
        !http_v8_make_signal_object(isolate, context, request_context->cancellation, &signal))
    {
        return false;
    }
    if (request_context->needs_headers && request_context->needs_request &&
        !http_v8_make_header_bag(isolate, context, request_context->request, &headers))
    {
        return false;
    }
    if (request_context->needs_header_facade &&
        !http_v8_make_header_facade(isolate, context, request_context->request, &header_facade))
    {
        return false;
    }

    if (request_context->needs_metadata || request_context->needs_log ||
        request_context->needs_request)
    {
        request_id = std::to_string(request_context->request_id);
    }
    if (request_context->needs_connection) {
        connection_id = std::to_string(request_context->connection_id);
    }
    if (request_context->needs_log &&
        !http_v8_make_logger_object(
            isolate, context, sl_str_from_cstr("request"), http_v8_str_from_string(request_id),
            request_context->route_name, request_context->route_pattern, &logger))
    {
        return false;
    }
    if (request_context->needs_body) {
        if (request_context->request->body.length != 0U) {
            v8::Local<v8::Uint8Array> body_bytes;
            if (!http_v8_make_uint8_array(isolate, request_context->request->body, &body_bytes)) {
                return false;
            }
            body_bytes_value = body_bytes;
        }
        if (!sl_status_is_ok(http_v8_to_local_string(
                isolate, http_v8_request_body_kind_name(request_context->body_kind),
                &body_kind_value)))
        {
            return false;
        }
    }

    if (request_context->needs_body) {
        body_object = v8::Object::New(isolate);
        if (!http_v8_cached_prototype(isolate, context, SL_V8_HTTP_PROTOTYPE_BODY,
                                      &body_prototype) ||
            !body_object->SetPrototype(context, body_prototype).FromMaybe(false) ||
            !http_v8_set_bool_property_key(isolate, context, body_object,
                                           SL_V8_HTTP_STRING_CONSUMED, false) ||
            !http_v8_set_private_value(isolate, context, body_object,
                                       SL_V8_HTTP_PRIVATE_BODY_CONSUMED,
                                       v8::Boolean::New(isolate, false)) ||
            !http_v8_set_string_property_key(
                isolate, context, body_object, SL_V8_HTTP_STRING_KIND,
                http_v8_request_body_kind_name(request_context->body_kind)) ||
            !http_v8_set_private_value(isolate, context, body_object, SL_V8_HTTP_PRIVATE_BODY,
                                       v8::Undefined(isolate)) ||
            !http_v8_set_private_value(isolate, context, body_object, SL_V8_HTTP_PRIVATE_BODY_KIND,
                                       body_kind_value) ||
            !http_v8_set_private_value(isolate, context, body_object, SL_V8_HTTP_PRIVATE_BODY_BYTES,
                                       body_bytes_value))
        {
            return false;
        }
    }

    if (request_context->needs_request) {
        request = v8::Object::New(isolate);
        if (!http_v8_cached_prototype(isolate, context, SL_V8_HTTP_PROTOTYPE_REQUEST,
                                      &request_prototype) ||
            !request->SetPrototype(context, request_prototype).FromMaybe(false) ||
            !http_v8_set_string_property_key(isolate, context, request, SL_V8_HTTP_STRING_ID,
                                             http_v8_str_from_string(request_id)) ||
            !http_v8_set_string_property_key(isolate, context, request, SL_V8_HTTP_STRING_METHOD,
                                             method) ||
            !http_v8_set_string_property_key(isolate, context, request, SL_V8_HTTP_STRING_SCHEME,
                                             request_context->scheme) ||
            !http_v8_set_string_property_key(isolate, context, request, SL_V8_HTTP_STRING_PROTOCOL,
                                             request_context->protocol) ||
            !http_v8_set_string_property_key(isolate, context, request, SL_V8_HTTP_STRING_PATH,
                                             request_context->request->path) ||
            !http_v8_set_string_property_key(isolate, context, request,
                                             SL_V8_HTTP_STRING_QUERY_STRING,
                                             request_context->query_string) ||
            !http_v8_set_string_property_key(isolate, context, request,
                                             SL_V8_HTTP_STRING_RAW_TARGET,
                                             request_context->request->raw_target))
        {
            return false;
        }

        if (request_context->needs_headers &&
            !http_v8_set_object_property_key(isolate, context, request, SL_V8_HTTP_STRING_HEADERS,
                                             headers))
        {
            return false;
        }
        if (request_context->needs_body) {
            if (!http_v8_set_object_property_key(isolate, context, request, SL_V8_HTTP_STRING_BODY,
                                                 body_object) ||
                !http_v8_set_private_value(isolate, context, request, SL_V8_HTTP_PRIVATE_BODY_BYTES,
                                           body_bytes_value) ||
                !http_v8_set_private_value(isolate, context, request, SL_V8_HTTP_PRIVATE_BODY,
                                           v8::Undefined(isolate)) ||
                !http_v8_set_private_value(isolate, context, request, SL_V8_HTTP_PRIVATE_BODY_KIND,
                                           body_kind_value))
            {
                return false;
            }
        }
    }

    if (request_context->needs_connection) {
        connection = v8::Object::New(isolate);
        if (!http_v8_set_string_property_key(isolate, context, connection, SL_V8_HTTP_STRING_ID,
                                             http_v8_str_from_string(connection_id)) ||
            !http_v8_set_string_property_key(isolate, context, connection,
                                             SL_V8_HTTP_STRING_PROTOCOL,
                                             sl_str_from_cstr("http")) ||
            !http_v8_set_string_property_key(isolate, context, connection, SL_V8_HTTP_STRING_SCHEME,
                                             request_context->scheme) ||
            !http_v8_set_bool_property_key(
                isolate, context, connection, SL_V8_HTTP_STRING_SECURE,
                sl_str_equal(request_context->scheme, sl_str_from_cstr("https"))))
        {
            return false;
        }
    }

    if (request_context->needs_metadata &&
        (!http_v8_set_string_property_key(isolate, context, ctx, SL_V8_HTTP_STRING_ROUTE_NAME,
                                          request_context->route_name) ||
         !http_v8_set_string_property_key(isolate, context, ctx, SL_V8_HTTP_STRING_ROUTE_PATTERN,
                                          request_context->route_pattern) ||
         !http_v8_set_string_property_key(isolate, context, ctx, SL_V8_HTTP_STRING_REQUEST_ID,
                                          http_v8_str_from_string(request_id))))
    {
        return false;
    }
    if (request_context->needs_route_params &&
        !http_v8_set_object_property_key(isolate, context, ctx, SL_V8_HTTP_STRING_ROUTE, route))
    {
        return false;
    }
    if (request_context->needs_query_params &&
        !http_v8_set_object_property_key(isolate, context, ctx, SL_V8_HTTP_STRING_QUERY, query))
    {
        return false;
    }
    if (request_context->needs_request &&
        !http_v8_set_object_property_key(isolate, context, ctx, SL_V8_HTTP_STRING_REQUEST, request))
    {
        return false;
    }
    if (request_context->needs_connection &&
        !http_v8_set_object_property_key(isolate, context, ctx, SL_V8_HTTP_STRING_CONNECTION,
                                         connection))
    {
        return false;
    }
    if (request_context->needs_signal &&
        !http_v8_set_object_property_key(isolate, context, ctx, SL_V8_HTTP_STRING_SIGNAL, signal))
    {
        return false;
    }
    if (request_context->needs_log &&
        !http_v8_set_object_property_key(isolate, context, ctx, SL_V8_HTTP_STRING_LOG, logger))
    {
        return false;
    }
    if (request_context->needs_header_facade &&
        !http_v8_set_object_property_key(isolate, context, ctx, SL_V8_HTTP_STRING_HEADER,
                                         header_facade))
    {
        return false;
    }

    if (request_context->needs_request) {
        if (!sl_str_is_empty(request_context->content_type)) {
            if (!http_v8_set_string_property_key(isolate, context, request,
                                                 SL_V8_HTTP_STRING_CONTENT_TYPE,
                                                 request_context->content_type))
            {
                return false;
            }
        }
        else if (!http_v8_set_null_property_key(isolate, context, request,
                                                SL_V8_HTTP_STRING_CONTENT_TYPE))
        {
            return false;
        }
        if (request_context->has_content_length) {
            if (!http_v8_set_uint64_property_key(isolate, context, request,
                                                 SL_V8_HTTP_STRING_CONTENT_LENGTH,
                                                 request_context->content_length))
            {
                return false;
            }
        }
        else if (!http_v8_set_null_property_key(isolate, context, request,
                                                SL_V8_HTTP_STRING_CONTENT_LENGTH))
        {
            return false;
        }
    }

    if (request_context->needs_signal &&
        sl_cancellation_token_reason(request_context->cancellation) ==
            SL_CANCELLATION_REASON_DEADLINE_EXCEEDED)
    {
        v8::Local<v8::Object> deadline = v8::Object::New(isolate);
        if (!http_v8_set_bool_property_key(isolate, context, deadline, SL_V8_HTTP_STRING_EXPIRED,
                                           true) ||
            !http_v8_set_object_property_key(isolate, context, ctx, SL_V8_HTTP_STRING_DEADLINE,
                                             deadline))
        {
            return false;
        }
    }
    else if (request_context->needs_signal &&
             !http_v8_set_null_property_key(isolate, context, ctx, SL_V8_HTTP_STRING_DEADLINE))
    {
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
        SlStatus status = http_v8_copy_value_string(isolate, arena, js_result, &out_result->text);
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

    bool fast_result_handled = false;
    SlStatus fast_result_status = http_v8_try_convert_fast_result(
        isolate, context, engine, arena, object, out_result, &fast_result_handled);
    if (!sl_status_is_ok(fast_result_status) || fast_result_handled) {
        return fast_result_status;
    }

    SlStr kind = sl_str_empty();
    SlStr content_type = sl_str_empty();
    uint16_t status_code = 0U;
    SlStatus kind_status = http_v8_get_object_string_copy(isolate, context, arena, object,
                                                          SL_V8_HTTP_STRING_KIND, &kind);
    if (sl_status_code(kind_status) == SL_STATUS_INVALID_ARGUMENT ||
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
    if (!sl_status_is_ok(kind_status)) {
        return kind_status;
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

    if (sl_str_equal(kind, sl_str_from_cstr("empty"))) {
        out_result->kind = SL_ENGINE_RESULT_NONE;
        out_result->response = sl_http_response_empty(status_code);
        out_result->response.headers = response_headers;
        out_result->response.header_count = response_header_count;
        return sl_status_ok();
    }

    SlStatus content_type_status = http_v8_get_object_string_copy(
        isolate, context, arena, object, SL_V8_HTTP_STRING_CONTENT_TYPE, &content_type);
    if (sl_status_code(content_type_status) == SL_STATUS_INVALID_ARGUMENT) {
        return http_v8_write_diag(
            engine, out_diag, SL_DIAG_INVALID_HTTP_RESULT, SL_STATUS_INVALID_STATE,
            http_v8_literal("JavaScript result descriptor is missing contentType",
                            sizeof("JavaScript result descriptor is missing contentType") - 1U),
            sl_str_empty());
    }
    if (!sl_status_is_ok(content_type_status)) {
        return content_type_status;
    }
    if (!http_v8_header_value_view_safe(content_type)) {
        return http_v8_write_diag(
            engine, out_diag, SL_DIAG_INVALID_HTTP_RESULT, SL_STATUS_INVALID_STATE,
            http_v8_literal("JavaScript result descriptor has an invalid contentType",
                            sizeof("JavaScript result descriptor has an invalid contentType") - 1U),
            http_v8_literal("Content-Type must not contain control characters.",
                            sizeof("Content-Type must not contain control characters.") - 1U));
    }

    v8::Local<v8::Value> body;
    if (!http_v8_get_value_property_key(isolate, context, object, SL_V8_HTTP_STRING_BODY_RESULT,
                                        &body))
    {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    if (sl_str_equal(kind, sl_str_from_cstr("text")) ||
        sl_str_equal(kind, sl_str_from_cstr("html")))
    {
        if (!body->IsString()) {
            return http_v8_write_diag(
                engine, out_diag, SL_DIAG_INVALID_HTTP_RESULT, SL_STATUS_INVALID_STATE,
                http_v8_literal("Text-like Results body must be a string",
                                sizeof("Text-like Results body must be a string") - 1U),
                sl_str_empty());
        }

        SlStatus status = http_v8_copy_value_string(isolate, arena, body, &out_result->text);
        if (!sl_status_is_ok(status)) {
            return status;
        }

        out_result->kind = SL_ENGINE_RESULT_TEXT;
        out_result->response = sl_http_response_text(status_code, out_result->text);
        out_result->response.headers = response_headers;
        out_result->response.header_count = response_header_count;
        out_result->response.content_type = content_type;
        return sl_status_ok();
    }

    if (sl_str_equal(kind, sl_str_from_cstr("bytes"))) {
        SlBytes bytes = {nullptr, 0U};
        SlStatus status = http_v8_copy_binary_value_bytes(arena, body, &bytes);
        if (sl_status_code(status) == SL_STATUS_INVALID_ARGUMENT) {
            return http_v8_write_diag(
                engine, out_diag, SL_DIAG_INVALID_HTTP_RESULT, SL_STATUS_INVALID_STATE,
                http_v8_literal("Results.bytes body must be binary data",
                                sizeof("Results.bytes body must be binary data") - 1U),
                http_v8_literal("Return an ArrayBuffer or ArrayBuffer view for Results.bytes.",
                                sizeof("Return an ArrayBuffer or ArrayBuffer view for "
                                       "Results.bytes.") -
                                    1U));
        }
        if (!sl_status_is_ok(status)) {
            return status;
        }

        out_result->kind = SL_ENGINE_RESULT_BYTES;
        out_result->response = sl_http_response_bytes(status_code, content_type, bytes);
        out_result->response.headers = response_headers;
        out_result->response.header_count = response_header_count;
        return sl_status_ok();
    }

    bool is_json = sl_str_equal(kind, sl_str_from_cstr("json"));
    bool is_problem = sl_str_equal(kind, sl_str_from_cstr("problem"));
    if (is_json || is_problem) {
        v8::Local<v8::String> json;
        SlBytes bytes = {nullptr, 0U};
        SlStatus status;

        if (body->IsUndefined()) {
            body = v8::Null(isolate);
        }

        if (!http_v8_stringify_json(context, body, &json)) {
            return http_v8_write_diag(
                engine, out_diag, SL_DIAG_INVALID_HTTP_RESULT, SL_STATUS_INVALID_STATE,
                http_v8_literal("Results.json body could not be serialized",
                                sizeof("Results.json body could not be serialized") - 1U),
                sl_str_empty());
        }

        status = http_v8_copy_value_bytes(isolate, arena, json, &bytes);
        if (!sl_status_is_ok(status)) {
            return status;
        }

        out_result->kind = is_json ? SL_ENGINE_RESULT_JSON : SL_ENGINE_RESULT_ERROR;
        out_result->response = is_json ? sl_http_response_json(status_code, bytes)
                                       : sl_http_response_problem(status_code, bytes);
        out_result->response.headers = response_headers;
        out_result->response.header_count = response_header_count;
        out_result->response.content_type = content_type;
        return sl_status_ok();
    }

    return http_v8_write_diag(
        engine, out_diag, SL_DIAG_INVALID_HTTP_RESULT, SL_STATUS_UNSUPPORTED,
        http_v8_literal("JavaScript result descriptor kind is unsupported",
                        sizeof("JavaScript result descriptor kind is unsupported") - 1U),
        http_v8_literal("Supported result kinds are text, html, bytes, json, empty, and problem.",
                        sizeof("Supported result kinds are text, html, bytes, json, empty, and "
                               "problem.") -
                            1U));
}

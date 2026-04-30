/*
 * src/engine/v8/intrinsics_sqlite.cc
 *
 * Installs the V8-internal SQLite bridge under __sloppy.data.sqlite.
 * Provider bridge modules may include V8 and provider headers because they
 * remain inside src/engine/v8/, but engine_v8.cc must stay focused on isolate,
 * context, and handler orchestration.
 */
#include "engine_v8_internal.h"

#include "sloppy/capability.h"
#include "sloppy/data_sqlite.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace {

struct SqliteV8OpenRequest
{
    std::string database;
    std::string capability;
    std::string provider_token;
    SlSqliteAccess access = SL_SQLITE_ACCESS_READWRITE;
    bool from_provider = false;
};

struct SqliteV8ConnectionResource
{
    SlSqliteConnection connection = {};
    std::string capability;
    std::string provider_token;
};

SlStatus sqlite_v8_to_local_string(v8::Isolate* isolate, SlStr str, v8::Local<v8::String>* out)
{
    v8::MaybeLocal<v8::String> maybe;

    if (isolate == nullptr || out == nullptr || (str.length != 0U && str.ptr == nullptr) ||
        str.length > static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    maybe = v8::String::NewFromUtf8(isolate, str.ptr, v8::NewStringType::kNormal,
                                    static_cast<int>(str.length));
    if (!maybe.ToLocal(out)) {
        return sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
    }

    return sl_status_ok();
}

std::string sqlite_v8_diag_to_string(const char* fallback, const SlDiag& diag)
{
    std::string message = fallback == nullptr ? "Sloppy native operation failed" : fallback;

    if (diag.message.ptr != nullptr && diag.message.length != 0U) {
        message.assign(diag.message.ptr, diag.message.length);
    }

    for (size_t index = 0U; index < diag.hint_count; index += 1U) {
        if (diag.hints[index].ptr != nullptr && diag.hints[index].length != 0U) {
            message += "\n";
            message.append(diag.hints[index].ptr, diag.hints[index].length);
        }
    }

    return message;
}

void sqlite_v8_throw_type_error(v8::Isolate* isolate, const char* message)
{
    v8::Local<v8::String> local_message;
    if (!sl_status_is_ok(
            sqlite_v8_to_local_string(isolate, sl_str_from_cstr(message), &local_message)))
    {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "Sloppy native type error")));
        return;
    }

    isolate->ThrowException(v8::Exception::TypeError(local_message));
}

void sqlite_v8_throw_error(v8::Isolate* isolate, const std::string& message)
{
    v8::Local<v8::String> local_message;
    if (!sl_status_is_ok(sqlite_v8_to_local_string(
            isolate, sl_str_from_parts(message.data(), message.size()), &local_message)))
    {
        isolate->ThrowException(v8::Exception::Error(
            v8::String::NewFromUtf8Literal(isolate, "Sloppy native operation failed")));
        return;
    }

    isolate->ThrowException(v8::Exception::Error(local_message));
}

void sqlite_v8_throw_diag(v8::Isolate* isolate, const char* fallback, const SlDiag& diag)
{
    sqlite_v8_throw_error(isolate, sqlite_v8_diag_to_string(fallback, diag));
}

bool sqlite_v8_value_to_std_string(v8::Isolate* isolate, v8::Local<v8::Value> value,
                                   std::string* out)
{
    if (out == nullptr || !value->IsString()) {
        return false;
    }

    v8::String::Utf8Value utf8(isolate, value);
    if (*utf8 == nullptr) {
        return false;
    }

    *out = std::string(*utf8, static_cast<size_t>(utf8.length()));
    return true;
}

bool sqlite_v8_get_optional_object_string(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                          v8::Local<v8::Object> object, const char* key,
                                          std::string* out, bool* present)
{
    v8::Local<v8::String> local_key;
    v8::Local<v8::Value> value;

    if (out == nullptr || present == nullptr ||
        !sl_status_is_ok(sqlite_v8_to_local_string(isolate, sl_str_from_cstr(key), &local_key)) ||
        !object->Get(context, local_key).ToLocal(&value))
    {
        return false;
    }

    if (value->IsUndefined() || value->IsNull()) {
        *present = false;
        out->clear();
        return true;
    }

    *present = true;
    return sqlite_v8_value_to_std_string(isolate, value, out);
}

bool sqlite_v8_parse_access(v8::Isolate* isolate, v8::Local<v8::Context> context,
                            v8::Local<v8::Object> object, SlSqliteAccess* out)
{
    std::string access_text;
    bool access_present = false;

    if (out == nullptr) {
        return false;
    }

    if (!sqlite_v8_get_optional_object_string(isolate, context, object, "access", &access_text,
                                              &access_present))
    {
        return false;
    }

    if (!access_present || access_text == "write" || access_text == "readwrite") {
        *out = SL_SQLITE_ACCESS_READWRITE;
        return true;
    }

    if (access_text == "read") {
        *out = SL_SQLITE_ACCESS_READ;
        return true;
    }

    return false;
}

bool sqlite_v8_parse_open_options(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                  v8::Local<v8::Value> value, SqliteV8OpenRequest* out)
{
    std::string database;
    std::string path;
    bool provider_present = false;
    bool database_present = false;
    bool path_present = false;
    bool capability_present = false;

    if (out == nullptr) {
        return false;
    }

    *out = SqliteV8OpenRequest{};

    if (sqlite_v8_value_to_std_string(isolate, value, &out->database)) {
        return !out->database.empty();
    }

    if (!value->IsObject()) {
        return false;
    }

    v8::Local<v8::Object> object = value.As<v8::Object>();
    if (!sqlite_v8_get_optional_object_string(isolate, context, object, "provider",
                                              &out->provider_token, &provider_present) ||
        !sqlite_v8_get_optional_object_string(isolate, context, object, "database", &database,
                                              &database_present) ||
        !sqlite_v8_get_optional_object_string(isolate, context, object, "path", &path,
                                              &path_present) ||
        !sqlite_v8_get_optional_object_string(isolate, context, object, "capability",
                                              &out->capability, &capability_present) ||
        !sqlite_v8_parse_access(isolate, context, object, &out->access))
    {
        return false;
    }

    if (!database_present && !path_present && !capability_present) {
        out->from_provider = provider_present;
        return provider_present && !out->provider_token.empty();
    }

    if (provider_present) {
        if (out->provider_token != "sqlite") {
            return false;
        }
        out->provider_token.clear();
    }

    if (database_present && path_present && database != path) {
        return false;
    }

    out->database = database_present ? database : path;
    if (out->database.empty() || !capability_present || out->capability.empty()) {
        return false;
    }

    return true;
}

bool sqlite_v8_plan_provider_matches(const SlPlanDataProvider& provider, SlStr token)
{
    if (sl_str_equal(provider.token, token)) {
        return true;
    }
    if (!sl_str_is_empty(provider.service) && sl_str_equal(provider.service, token)) {
        return true;
    }
    return !sl_str_is_empty(provider.capability) && sl_str_equal(provider.capability, token);
}

const SlPlanDataProvider* sqlite_v8_find_provider(const SlV8Engine* backend, SlStr token)
{
    size_t index = 0U;

    if (backend == nullptr || backend->plan == nullptr || sl_str_is_empty(token) ||
        (backend->plan->data_provider_count > 0U && backend->plan->data_providers == nullptr))
    {
        return nullptr;
    }

    for (index = 0U; index < backend->plan->data_provider_count; index += 1U) {
        const SlPlanDataProvider& provider = backend->plan->data_providers[index];
        if (sqlite_v8_plan_provider_matches(provider, token)) {
            return &provider;
        }
    }

    return nullptr;
}

bool sqlite_v8_capability_is_read_only(const SlCapabilityRegistry* registry, SlStr token)
{
    const SlPlanCapability* capability = nullptr;

    if (registry == nullptr || sl_str_is_empty(token) ||
        !sl_status_is_ok(sl_capability_registry_find(registry, token, &capability)) ||
        capability == nullptr)
    {
        return false;
    }

    return sl_str_equal(capability->access, sl_str_from_cstr("read"));
}

bool sqlite_v8_resolve_provider_request(v8::Isolate* isolate, const SlV8Engine* backend,
                                        SqliteV8OpenRequest* request)
{
    SlStr token;
    const SlPlanDataProvider* provider = nullptr;

    if (request == nullptr || !request->from_provider) {
        return true;
    }

    token = sl_str_from_parts(request->provider_token.data(), request->provider_token.size());
    provider = sqlite_v8_find_provider(backend, token);
    if (provider == nullptr) {
        sqlite_v8_throw_error(isolate, "sqlite provider is not configured in the app plan");
        return false;
    }
    if (!sl_str_equal(provider->provider, sl_str_from_cstr("sqlite"))) {
        sqlite_v8_throw_error(isolate, "sqlite provider token resolves to a non-sqlite provider");
        return false;
    }
    if (sl_str_is_empty(provider->database)) {
        sqlite_v8_throw_error(isolate, "sqlite provider metadata is missing database");
        return false;
    }

    request->database.assign(provider->database.ptr, provider->database.length);
    request->provider_token.assign(provider->token.ptr, provider->token.length);
    if (!sl_str_is_empty(provider->capability)) {
        request->capability.assign(provider->capability.ptr, provider->capability.length);
    }
    else {
        request->capability.assign(provider->token.ptr, provider->token.length);
    }

    if (sqlite_v8_capability_is_read_only(
            backend == nullptr ? nullptr : backend->capabilities,
            sl_str_from_parts(request->capability.data(), request->capability.size())))
    {
        request->access = SL_SQLITE_ACCESS_READ;
    }

    return true;
}

bool sqlite_v8_check_capability(v8::Isolate* isolate, SlV8Engine* backend, SlArena* arena,
                                const SqliteV8ConnectionResource* resource,
                                SlCapabilityOperation operation)
{
    SlDiag diag = {};
    SlStr token;
    SlStr provider;
    SlStatus status;

    if (backend == nullptr || backend->capabilities == nullptr) {
        sqlite_v8_throw_error(isolate,
                              "sqlite capability registry is unavailable; database access is not "
                              "enforced for this engine");
        return false;
    }
    if (arena == nullptr || resource == nullptr || resource->capability.empty()) {
        sqlite_v8_throw_error(isolate, "sqlite resource is missing capability metadata");
        return false;
    }

    token = sl_str_from_parts(resource->capability.data(), resource->capability.size());
    if (!resource->provider_token.empty()) {
        provider =
            sl_str_from_parts(resource->provider_token.data(), resource->provider_token.size());
        status = sl_capability_check_database(backend->capabilities, arena, token, operation,
                                              provider, &diag);
    }
    else {
        status = sl_capability_check_database_provider(
            backend->capabilities, arena, token, operation, sl_str_from_cstr("sqlite"), &diag);
    }
    if (!sl_status_is_ok(status)) {
        sqlite_v8_throw_diag(isolate, "sqlite capability check failed", diag);
        return false;
    }

    return true;
}

bool sqlite_v8_get_resource_id(v8::Isolate* isolate, v8::Local<v8::Context> context,
                               v8::Local<v8::Value> value, SlResourceId* out)
{
    v8::Local<v8::Object> object;
    v8::Local<v8::String> slot_key;
    v8::Local<v8::String> generation_key;
    v8::Local<v8::Value> slot_value;
    v8::Local<v8::Value> generation_value;

    if (out == nullptr || !value->IsObject()) {
        return false;
    }

    object = value.As<v8::Object>();
    if (!sl_status_is_ok(sqlite_v8_to_local_string(isolate, sl_str_from_cstr("slot"), &slot_key)) ||
        !sl_status_is_ok(
            sqlite_v8_to_local_string(isolate, sl_str_from_cstr("generation"), &generation_key)) ||
        !object->Get(context, slot_key).ToLocal(&slot_value) ||
        !object->Get(context, generation_key).ToLocal(&generation_value) ||
        !slot_value->IsUint32() || !generation_value->IsUint32())
    {
        return false;
    }

    out->slot = slot_value.As<v8::Uint32>()->Value();
    out->generation = generation_value.As<v8::Uint32>()->Value();
    return true;
}

bool sqlite_v8_make_resource_handle(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                    SlResourceId id, v8::Local<v8::Object>* out)
{
    v8::Local<v8::Object> handle = v8::Object::New(isolate);
    v8::Local<v8::String> slot_key;
    v8::Local<v8::String> generation_key;
    v8::Local<v8::String> kind_key;
    v8::Local<v8::String> kind_value;

    if (out == nullptr ||
        !sl_status_is_ok(sqlite_v8_to_local_string(isolate, sl_str_from_cstr("slot"), &slot_key)) ||
        !sl_status_is_ok(
            sqlite_v8_to_local_string(isolate, sl_str_from_cstr("generation"), &generation_key)) ||
        !sl_status_is_ok(sqlite_v8_to_local_string(isolate, sl_str_from_cstr("kind"), &kind_key)) ||
        !sl_status_is_ok(sqlite_v8_to_local_string(isolate, sl_str_from_cstr("sqlite.connection"),
                                                   &kind_value)) ||
        !handle->Set(context, slot_key, v8::Integer::NewFromUnsigned(isolate, id.slot))
             .FromMaybe(false) ||
        !handle->Set(context, generation_key, v8::Integer::NewFromUnsigned(isolate, id.generation))
             .FromMaybe(false) ||
        !handle->Set(context, kind_key, kind_value).FromMaybe(false))
    {
        return false;
    }

    *out = handle;
    return true;
}

void sqlite_v8_connection_cleanup(void* ptr, void* user)
{
    (void)user;

    SqliteV8ConnectionResource* resource = static_cast<SqliteV8ConnectionResource*>(ptr);
    if (resource != nullptr) {
        (void)sl_sqlite_close(&resource->connection);
        delete resource;
    }
}

SqliteV8ConnectionResource* sqlite_v8_lookup_connection(v8::Isolate* isolate,
                                                        v8::Local<v8::Context> context,
                                                        SlV8Engine* backend,
                                                        v8::Local<v8::Value> handle_value)
{
    SlResourceId id = {};
    SlDiag diag = {};
    void* ptr = nullptr;

    if (!sqlite_v8_get_resource_id(isolate, context, handle_value, &id)) {
        sqlite_v8_throw_type_error(isolate,
                                   "sqlite resource handle must be an opaque Sloppy handle");
        return nullptr;
    }

    SlStatus status = sl_resource_table_get(&backend->resources, id,
                                            SL_RESOURCE_KIND_SQLITE_CONNECTION, &ptr, &diag);
    if (!sl_status_is_ok(status)) {
        sqlite_v8_throw_diag(isolate, "sqlite resource handle is invalid", diag);
        return nullptr;
    }

    return static_cast<SqliteV8ConnectionResource*>(ptr);
}

bool sqlite_v8_convert_params(v8::Isolate* isolate, v8::Local<v8::Context> context,
                              v8::Local<v8::Value> value, std::vector<std::string>* text_storage,
                              std::vector<SlSqliteParam>* out)
{
    v8::Local<v8::Array> array;
    uint32_t length = 0U;

    if (out == nullptr || text_storage == nullptr) {
        return false;
    }

    out->clear();
    text_storage->clear();

    if (value->IsUndefined() || value->IsNull()) {
        return true;
    }

    if (!value->IsArray()) {
        sqlite_v8_throw_type_error(isolate, "sqlite parameters must be an array when supplied");
        return false;
    }

    array = value.As<v8::Array>();
    length = array->Length();
    out->reserve(length);
    text_storage->reserve(length);

    for (uint32_t index = 0U; index < length; index += 1U) {
        v8::Local<v8::Value> item;
        SlSqliteParam param = {};

        if (!array->Get(context, index).ToLocal(&item)) {
            return false;
        }

        if (item->IsNull() || item->IsUndefined()) {
            param.kind = SL_SQLITE_PARAM_NULL;
        }
        else if (item->IsBoolean()) {
            param.kind = SL_SQLITE_PARAM_BOOL;
            param.value.boolean = item->BooleanValue(isolate);
        }
        else if (item->IsInt32()) {
            param.kind = SL_SQLITE_PARAM_INTEGER;
            param.value.integer = item.As<v8::Int32>()->Value();
        }
        else if (item->IsNumber()) {
            const double number = item.As<v8::Number>()->Value();
            if (std::isfinite(number) && std::trunc(number) == number &&
                number >= static_cast<double>(std::numeric_limits<int64_t>::min()) &&
                number < static_cast<double>(std::numeric_limits<int64_t>::max()))
            {
                param.kind = SL_SQLITE_PARAM_INTEGER;
                param.value.integer = static_cast<int64_t>(number);
            }
            else {
                param.kind = SL_SQLITE_PARAM_FLOAT;
                param.value.number = number;
            }
        }
        else if (item->IsString()) {
            std::string text;
            if (!sqlite_v8_value_to_std_string(isolate, item, &text)) {
                return false;
            }
            text_storage->push_back(std::move(text));
            const std::string& stored = text_storage->back();
            param.kind = SL_SQLITE_PARAM_TEXT;
            param.value.text = sl_str_from_parts(stored.data(), stored.size());
        }
        else {
            sqlite_v8_throw_type_error(
                isolate,
                "sqlite parameters support only null, string, integer, float, and boolean values");
            return false;
        }

        out->push_back(param);
    }

    return true;
}

bool sqlite_v8_set_cell(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        v8::Local<v8::Object> row, SlStr column_name, const SlSqliteValue* value)
{
    v8::Local<v8::String> key;
    v8::Local<v8::Value> js_value;

    if (value == nullptr || !sl_status_is_ok(sqlite_v8_to_local_string(isolate, column_name, &key)))
    {
        return false;
    }

    switch (value->kind) {
    case SL_SQLITE_VALUE_NULL:
        js_value = v8::Null(isolate);
        break;
    case SL_SQLITE_VALUE_TEXT: {
        v8::Local<v8::String> text;
        if (!sl_status_is_ok(sqlite_v8_to_local_string(isolate, value->value.text, &text))) {
            return false;
        }
        js_value = text;
        break;
    }
    case SL_SQLITE_VALUE_INTEGER:
        js_value = v8::Number::New(isolate, static_cast<double>(value->value.integer));
        break;
    case SL_SQLITE_VALUE_FLOAT:
        js_value = v8::Number::New(isolate, value->value.number);
        break;
    default:
        return false;
    }

    return row->Set(context, key, js_value).FromMaybe(false);
}

bool sqlite_v8_result_to_array(v8::Isolate* isolate, v8::Local<v8::Context> context,
                               const SlSqliteResult* result, v8::Local<v8::Array>* out)
{
    v8::Local<v8::Array> rows;

    if (result == nullptr || out == nullptr ||
        result->row_count > static_cast<size_t>(std::numeric_limits<uint32_t>::max()))
    {
        return false;
    }

    rows = v8::Array::New(isolate, static_cast<int>(result->row_count));
    for (size_t row_index = 0U; row_index < result->row_count; row_index += 1U) {
        v8::Local<v8::Object> row = v8::Object::New(isolate);
        for (size_t column_index = 0U; column_index < result->column_count; column_index += 1U) {
            if (!sqlite_v8_set_cell(isolate, context, row, result->column_names[column_index],
                                    &result->rows[row_index].values[column_index]))
            {
                return false;
            }
        }
        if (!rows->Set(context, static_cast<uint32_t>(row_index), row).FromMaybe(false)) {
            return false;
        }
    }

    *out = rows;
    return true;
}

bool sqlite_v8_one_to_value(v8::Isolate* isolate, v8::Local<v8::Context> context,
                            const SlSqliteQueryOneResult* result, v8::Local<v8::Value>* out)
{
    v8::Local<v8::Object> row;

    if (result == nullptr || out == nullptr) {
        return false;
    }

    if (!result->found) {
        *out = v8::Null(isolate);
        return true;
    }

    row = v8::Object::New(isolate);
    for (size_t column_index = 0U; column_index < result->column_count; column_index += 1U) {
        if (!sqlite_v8_set_cell(isolate, context, row, result->column_names[column_index],
                                &result->values[column_index]))
        {
            return false;
        }
    }

    *out = row;
    return true;
}

void sqlite_v8_open_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    constexpr size_t scratch_size = 65536U;
    unsigned char scratch[scratch_size];
    SlArena arena = {};
    SlDiag diag = {};
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    SqliteV8OpenRequest request;
    SlSqliteOpenOptions options = {};
    SqliteV8ConnectionResource* resource = nullptr;
    SlResourceId id = sl_resource_id_invalid();
    v8::Local<v8::Object> handle;

    if (backend == nullptr || args.Length() != 1 ||
        !sqlite_v8_parse_open_options(isolate, context, args[0], &request))
    {
        sqlite_v8_throw_type_error(
            isolate,
            "__sloppy.data.sqlite.open requires database/capability options or a provider token");
        return;
    }

    if (!sl_status_is_ok(sl_arena_init(&arena, scratch, sizeof(scratch)))) {
        sqlite_v8_throw_error(isolate, "sqlite bridge scratch arena initialization failed");
        return;
    }

    if (!sqlite_v8_resolve_provider_request(isolate, backend, &request)) {
        return;
    }
    if (request.capability.empty()) {
        sqlite_v8_throw_error(
            isolate, "sqlite open requires capability metadata before provider work can begin");
        return;
    }

    resource = new (std::nothrow) SqliteV8ConnectionResource{};
    if (resource == nullptr) {
        sqlite_v8_throw_error(isolate, "sqlite bridge could not allocate a connection resource");
        return;
    }

    resource->capability = request.capability;
    resource->provider_token = request.provider_token;

    if (!sqlite_v8_check_capability(isolate, backend, &arena, resource,
                                    request.access == SL_SQLITE_ACCESS_READ
                                        ? SL_CAPABILITY_OPERATION_READ
                                        : SL_CAPABILITY_OPERATION_WRITE))
    {
        delete resource;
        return;
    }

    options.path = sl_str_from_parts(request.database.data(), request.database.size());
    options.access = request.access;
    SlStatus status = sl_sqlite_open(&arena, &options, &resource->connection, &diag);
    if (!sl_status_is_ok(status)) {
        delete resource;
        sqlite_v8_throw_diag(isolate, "sqlite open failed", diag);
        return;
    }

    status = sl_resource_table_insert(&backend->resources, SL_RESOURCE_KIND_SQLITE_CONNECTION,
                                      resource, sqlite_v8_connection_cleanup, nullptr, &id, &diag);
    if (!sl_status_is_ok(status)) {
        sqlite_v8_connection_cleanup(resource, nullptr);
        sqlite_v8_throw_diag(isolate, "sqlite resource registration failed", diag);
        return;
    }

    if (!sqlite_v8_make_resource_handle(isolate, context, id, &handle)) {
        (void)sl_resource_table_close(&backend->resources, id, nullptr);
        sqlite_v8_throw_error(isolate, "sqlite bridge could not create a resource handle");
        return;
    }

    args.GetReturnValue().Set(handle);
}

void sqlite_v8_close_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    SlResourceId id = {};
    SlDiag diag = {};
    void* ptr = nullptr;

    if (backend == nullptr || args.Length() != 1 ||
        !sqlite_v8_get_resource_id(isolate, context, args[0], &id))
    {
        sqlite_v8_throw_type_error(isolate,
                                   "__sloppy.data.sqlite.close requires a resource handle");
        return;
    }

    SlStatus status = sl_resource_table_get(&backend->resources, id,
                                            SL_RESOURCE_KIND_SQLITE_CONNECTION, &ptr, &diag);
    if (!sl_status_is_ok(status)) {
        sqlite_v8_throw_diag(isolate, "sqlite close failed", diag);
        return;
    }

    status = sl_resource_table_close(&backend->resources, id, &diag);
    if (!sl_status_is_ok(status)) {
        sqlite_v8_throw_diag(isolate, "sqlite close failed", diag);
        return;
    }

    args.GetReturnValue().Set(v8::Undefined(isolate));
}

void sqlite_v8_exec_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    constexpr size_t scratch_size = 65536U;
    unsigned char scratch[scratch_size];
    SlArena arena = {};
    SlDiag diag = {};
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::string sql;
    std::vector<std::string> text_storage;
    std::vector<SlSqliteParam> params;
    SlSqliteExecResult result = {};
    SqliteV8ConnectionResource* resource = nullptr;

    if (backend == nullptr || args.Length() < 2 || args.Length() > 3 ||
        !sqlite_v8_value_to_std_string(isolate, args[1], &sql) || sql.empty())
    {
        sqlite_v8_throw_type_error(
            isolate,
            "__sloppy.data.sqlite.exec requires a handle, SQL string, and optional params");
        return;
    }

    resource = sqlite_v8_lookup_connection(isolate, context, backend, args[0]);
    if (resource == nullptr ||
        !sqlite_v8_convert_params(isolate, context,
                                  args.Length() == 3 ? args[2] : v8::Undefined(isolate),
                                  &text_storage, &params))
    {
        return;
    }

    if (!sl_status_is_ok(sl_arena_init(&arena, scratch, sizeof(scratch)))) {
        sqlite_v8_throw_error(isolate, "sqlite bridge scratch arena initialization failed");
        return;
    }

    if (!sqlite_v8_check_capability(isolate, backend, &arena, resource,
                                    SL_CAPABILITY_OPERATION_WRITE))
    {
        return;
    }

    SlStatus status =
        sl_sqlite_exec(&arena, &resource->connection, sl_str_from_parts(sql.data(), sql.size()),
                       params.empty() ? nullptr : params.data(), params.size(), &result, &diag);
    if (!sl_status_is_ok(status)) {
        sqlite_v8_throw_diag(isolate, "sqlite exec failed", diag);
        return;
    }

    v8::Local<v8::Object> output = v8::Object::New(isolate);
    v8::Local<v8::String> key;
    if (!sl_status_is_ok(
            sqlite_v8_to_local_string(isolate, sl_str_from_cstr("affectedRows"), &key)) ||
        !output->Set(context, key, v8::Int32::New(isolate, result.changes)).FromMaybe(false))
    {
        sqlite_v8_throw_error(isolate, "sqlite bridge could not create exec result");
        return;
    }

    args.GetReturnValue().Set(output);
}

void sqlite_v8_query_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    constexpr size_t scratch_size = 131072U;
    unsigned char scratch[scratch_size];
    SlArena arena = {};
    SlDiag diag = {};
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::string sql;
    std::vector<std::string> text_storage;
    std::vector<SlSqliteParam> params;
    SlSqliteResult result = {};
    SqliteV8ConnectionResource* resource = nullptr;
    v8::Local<v8::Array> rows;

    if (backend == nullptr || args.Length() < 2 || args.Length() > 3 ||
        !sqlite_v8_value_to_std_string(isolate, args[1], &sql) || sql.empty())
    {
        sqlite_v8_throw_type_error(
            isolate,
            "__sloppy.data.sqlite.query requires a handle, SQL string, and optional params");
        return;
    }

    resource = sqlite_v8_lookup_connection(isolate, context, backend, args[0]);
    if (resource == nullptr ||
        !sqlite_v8_convert_params(isolate, context,
                                  args.Length() == 3 ? args[2] : v8::Undefined(isolate),
                                  &text_storage, &params))
    {
        return;
    }

    if (!sl_status_is_ok(sl_arena_init(&arena, scratch, sizeof(scratch)))) {
        sqlite_v8_throw_error(isolate, "sqlite bridge scratch arena initialization failed");
        return;
    }

    if (!sqlite_v8_check_capability(isolate, backend, &arena, resource,
                                    SL_CAPABILITY_OPERATION_READ))
    {
        return;
    }

    SlStatus status = sl_sqlite_query(
        &arena, &resource->connection, sl_str_from_parts(sql.data(), sql.size()),
        params.empty() ? nullptr : params.data(), params.size(), nullptr, &result, &diag);
    if (!sl_status_is_ok(status)) {
        sqlite_v8_throw_diag(isolate, "sqlite query failed", diag);
        return;
    }

    if (!sqlite_v8_result_to_array(isolate, context, &result, &rows)) {
        sqlite_v8_throw_error(isolate, "sqlite bridge could not materialize query rows");
        return;
    }

    args.GetReturnValue().Set(rows);
}

void sqlite_v8_query_one_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    constexpr size_t scratch_size = 65536U;
    unsigned char scratch[scratch_size];
    SlArena arena = {};
    SlDiag diag = {};
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::string sql;
    std::vector<std::string> text_storage;
    std::vector<SlSqliteParam> params;
    SlSqliteQueryOneResult result = {};
    SqliteV8ConnectionResource* resource = nullptr;
    v8::Local<v8::Value> row;

    if (backend == nullptr || args.Length() < 2 || args.Length() > 3 ||
        !sqlite_v8_value_to_std_string(isolate, args[1], &sql) || sql.empty())
    {
        sqlite_v8_throw_type_error(
            isolate,
            "__sloppy.data.sqlite.queryOne requires a handle, SQL string, and optional params");
        return;
    }

    resource = sqlite_v8_lookup_connection(isolate, context, backend, args[0]);
    if (resource == nullptr ||
        !sqlite_v8_convert_params(isolate, context,
                                  args.Length() == 3 ? args[2] : v8::Undefined(isolate),
                                  &text_storage, &params))
    {
        return;
    }

    if (!sl_status_is_ok(sl_arena_init(&arena, scratch, sizeof(scratch)))) {
        sqlite_v8_throw_error(isolate, "sqlite bridge scratch arena initialization failed");
        return;
    }

    if (!sqlite_v8_check_capability(isolate, backend, &arena, resource,
                                    SL_CAPABILITY_OPERATION_READ))
    {
        return;
    }

    SlStatus status = sl_sqlite_query_one(
        &arena, &resource->connection, sl_str_from_parts(sql.data(), sql.size()),
        params.empty() ? nullptr : params.data(), params.size(), &result, &diag);
    if (!sl_status_is_ok(status)) {
        sqlite_v8_throw_diag(isolate, "sqlite queryOne failed", diag);
        return;
    }

    if (!sqlite_v8_one_to_value(isolate, context, &result, &row)) {
        sqlite_v8_throw_error(isolate, "sqlite bridge could not materialize queryOne row");
        return;
    }

    args.GetReturnValue().Set(row);
}

bool sqlite_v8_set_function(v8::Isolate* isolate, v8::Local<v8::Context> context,
                            v8::Local<v8::Object> object, const char* name,
                            v8::FunctionCallback callback)
{
    v8::Local<v8::String> key;
    v8::Local<v8::FunctionTemplate> function_template;
    v8::Local<v8::Function> function;

    if (!sl_status_is_ok(sqlite_v8_to_local_string(isolate, sl_str_from_cstr(name), &key))) {
        return false;
    }

    function_template = v8::FunctionTemplate::New(isolate, callback);
    if (!function_template->GetFunction(context).ToLocal(&function)) {
        return false;
    }

    return object->Set(context, key, function).FromMaybe(false);
}

} /* namespace */

bool sl_v8_install_sqlite_intrinsics(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                     v8::Local<v8::Object> data)
{
    v8::Local<v8::String> sqlite_key;
    v8::Local<v8::Object> sqlite = v8::Object::New(isolate);

    if (!sl_status_is_ok(
            sqlite_v8_to_local_string(isolate, sl_str_from_cstr("sqlite"), &sqlite_key)))
    {
        return false;
    }

    if (!sqlite_v8_set_function(isolate, context, sqlite, "open", sqlite_v8_open_callback) ||
        !sqlite_v8_set_function(isolate, context, sqlite, "close", sqlite_v8_close_callback) ||
        !sqlite_v8_set_function(isolate, context, sqlite, "exec", sqlite_v8_exec_callback) ||
        !sqlite_v8_set_function(isolate, context, sqlite, "query", sqlite_v8_query_callback) ||
        !sqlite_v8_set_function(isolate, context, sqlite, "queryOne",
                                sqlite_v8_query_one_callback) ||
        !data->Set(context, sqlite_key, sqlite).FromMaybe(false))
    {
        return false;
    }

    return true;
}

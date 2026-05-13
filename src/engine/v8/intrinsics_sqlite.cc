/*
 * src/engine/v8/intrinsics_sqlite.cc
 *
 * Installs the V8-internal SQLite bridge under __sloppy.data.sqlite.
 * Provider bridge modules may include V8 and provider headers because they
 * remain inside src/engine/v8/, but engine_v8.cc must stay focused on isolate,
 * context, and handler orchestration.
 */
#include "engine_v8_internal.h"
#include "intrinsics_db_bridge.h"
#include "string_interop.h"

#include "sloppy/capability.h"
#include "sloppy/data_sqlite.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <variant>
#include <vector>

namespace {

constexpr double sqlite_v8_max_safe_integer = 9007199254740991.0;
constexpr double sqlite_v8_min_safe_integer = -9007199254740991.0;

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
    SlSqliteTransaction transaction = {};
    std::string capability;
    std::string provider_token;
    SlSqliteAccess access = SL_SQLITE_ACCESS_READWRITE;
    std::atomic_uint32_t pending_operations = 0U;
    std::atomic_bool closing = false;
};

struct SqliteV8CursorResource
{
    SqliteV8ConnectionResource* owner = nullptr;
    SlSqliteCursor cursor = {};
    std::vector<unsigned char> arena_storage;
    bool raw = false;
    bool closed = false;
};

enum class SqliteV8Operation
{
    Exec,
    Query,
    QueryRaw,
    QueryOne,
    TransactionBegin,
    TransactionCommit,
    TransactionRollback,
    TransactionExec,
    TransactionQuery,
    TransactionQueryRaw,
    TransactionQueryOne,
};

using SqliteV8ParamStorage =
    std::variant<std::monostate, std::string, std::vector<unsigned char>, int64_t, double, bool>;

struct SqliteV8ParamValue
{
    SlSqliteParamKind kind = SL_SQLITE_PARAM_NULL;
    SqliteV8ParamStorage value = std::monostate{};
};

struct SqliteV8Request
{
    SlV8Engine* backend = nullptr;
    SqliteV8ConnectionResource* resource = nullptr;
    SqliteV8Operation operation = SqliteV8Operation::Exec;
    v8::Global<v8::Promise::Resolver> resolver;
    std::string sql;
    std::vector<SqliteV8ParamValue> param_values;
    std::vector<SlSqliteParam> params;
    std::vector<unsigned char> storage;
    SlArena arena = {};
    SlSqliteExecResult exec_result = {};
    SlSqliteResult query_result = {};
    SlSqliteQueryOneResult one_result = {};
    uint32_t max_rows = SL_SQLITE_DEFAULT_MAX_ROWS;
    bool has_timeout_ms = false;
    uint32_t timeout_ms = 0U;
    SlDiag admission_diag = {};
    SlStatus status = sl_status_ok();
    std::string error;
    bool owner_dispatched = false;
};

/* Keep aligned with SQLite parameter limits and the documented V8 bridge cap tests. */
constexpr uint32_t sqlite_v8_max_parameter_count = 32766U;

SlStatus sqlite_v8_to_local_string(v8::Isolate* isolate, SlStr str, v8::Local<v8::String>* out)
{
    return sl_v8_db_to_local_string(isolate, str, out);
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
    sl_v8_db_throw_type_error(isolate, message, "Sloppy native type error");
}

void sqlite_v8_throw_error(v8::Isolate* isolate, const std::string& message)
{
    sl_v8_db_throw_error(isolate, message, "Sloppy native operation failed");
}

void sqlite_v8_throw_diag(v8::Isolate* isolate, const char* fallback, const SlDiag& diag)
{
    sqlite_v8_throw_error(isolate, sqlite_v8_diag_to_string(fallback, diag));
}

bool sqlite_v8_value_to_std_string(v8::Isolate* isolate, v8::Local<v8::Value> value,
                                   std::string* out)
{
    return sl_v8_db_value_to_std_string(isolate, value, out);
}

bool sqlite_v8_get_optional_object_string(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                          v8::Local<v8::Object> object, const char* key,
                                          std::string* out, bool* present)
{
    return sl_v8_db_get_optional_object_string(isolate, context, object, key, out, present);
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

    if (!access_present || access_text == "readwrite") {
        *out = SL_SQLITE_ACCESS_READWRITE;
        return true;
    }

    if (access_text == "read") {
        *out = SL_SQLITE_ACCESS_READ;
        return true;
    }

    if (access_text == "write") {
        *out = SL_SQLITE_ACCESS_WRITE;
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

SlCapabilityOperation sqlite_v8_open_capability_operation(SlSqliteAccess access)
{
    switch (access) {
    case SL_SQLITE_ACCESS_READ:
        return SL_CAPABILITY_OPERATION_READ;
    case SL_SQLITE_ACCESS_WRITE:
        return SL_CAPABILITY_OPERATION_WRITE;
    case SL_SQLITE_ACCESS_READWRITE:
    default:
        return SL_CAPABILITY_OPERATION_READWRITE;
    }
}

SlStr sqlite_v8_operation_name(SlCapabilityOperation operation)
{
    switch (operation) {
    case SL_CAPABILITY_OPERATION_READ:
        return sl_str_from_cstr("read");
    case SL_CAPABILITY_OPERATION_WRITE:
        return sl_str_from_cstr("write");
    case SL_CAPABILITY_OPERATION_READWRITE:
        return sl_str_from_cstr("readwrite");
    default:
        return sl_str_from_cstr("unsupported");
    }
}

SlStr sqlite_v8_access_name(SlSqliteAccess access)
{
    switch (access) {
    case SL_SQLITE_ACCESS_READ:
        return sl_str_from_cstr("read");
    case SL_SQLITE_ACCESS_WRITE:
        return sl_str_from_cstr("write");
    case SL_SQLITE_ACCESS_READWRITE:
        return sl_str_from_cstr("readwrite");
    default:
        return sl_str_from_cstr("unknown");
    }
}

bool sqlite_v8_access_allows(SlSqliteAccess access, SlCapabilityOperation operation)
{
    if (operation == SL_CAPABILITY_OPERATION_READ) {
        return access == SL_SQLITE_ACCESS_READ || access == SL_SQLITE_ACCESS_READWRITE;
    }
    if (operation == SL_CAPABILITY_OPERATION_WRITE) {
        return access == SL_SQLITE_ACCESS_WRITE || access == SL_SQLITE_ACCESS_READWRITE;
    }
    if (operation == SL_CAPABILITY_OPERATION_READWRITE) {
        return access == SL_SQLITE_ACCESS_READWRITE;
    }
    return false;
}

bool sqlite_v8_check_handle_access(v8::Isolate* isolate, const SqliteV8ConnectionResource* resource,
                                   SlCapabilityOperation operation)
{
    std::string message;
    SlStr operation_name;
    SlStr access_name;

    if (resource == nullptr) {
        sqlite_v8_throw_error(isolate, "sqlite resource is missing capability metadata");
        return false;
    }

    if (sqlite_v8_access_allows(resource->access, operation)) {
        return true;
    }

    operation_name = sqlite_v8_operation_name(operation);
    access_name = sqlite_v8_access_name(resource->access);
    message = "capability access denied: insufficient handle access\noperation: ";
    message.append(operation_name.ptr, operation_name.length);
    message += "\nactual access: ";
    message.append(access_name.ptr, access_name.length);
    message += "\nprovider: sqlite";
    sqlite_v8_throw_error(isolate, message);
    return false;
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
    if (!sqlite_v8_check_handle_access(isolate, resource, operation)) {
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
    return sl_v8_db_get_resource_id(isolate, context, value, out);
}

bool sqlite_v8_make_resource_handle(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                    SlResourceId id, v8::Local<v8::Object>* out)
{
    return sl_v8_db_make_resource_handle(isolate, context, id, "sqlite.connection", out);
}

bool sqlite_v8_make_cursor_handle(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                  SlResourceId id, v8::Local<v8::Object>* out)
{
    return sl_v8_db_make_resource_handle(isolate, context, id, "sqlite.cursor", out);
}

void sqlite_v8_connection_cleanup(void* ptr, void* user)
{
    (void)user;

    SqliteV8ConnectionResource* resource = static_cast<SqliteV8ConnectionResource*>(ptr);
    if (resource != nullptr) {
        resource->closing.store(true);
        sl_sqlite_close(&resource->connection);
        delete resource;
    }
}

void sqlite_v8_cursor_cleanup(void* ptr, void* user)
{
    (void)user;

    SqliteV8CursorResource* resource = static_cast<SqliteV8CursorResource*>(ptr);
    if (resource == nullptr) {
        return;
    }
    if (!resource->closed) {
        sl_sqlite_cursor_close(&resource->cursor);
        resource->closed = true;
    }
    if (resource->owner != nullptr) {
        resource->owner->pending_operations.fetch_sub(1U);
    }
    delete resource;
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

SqliteV8CursorResource* sqlite_v8_lookup_cursor(v8::Isolate* isolate,
                                                v8::Local<v8::Context> context, SlV8Engine* backend,
                                                v8::Local<v8::Value> handle_value)
{
    SlResourceId id = {};
    SlDiag diag = {};
    void* ptr = nullptr;

    if (!sqlite_v8_get_resource_id(isolate, context, handle_value, &id)) {
        sqlite_v8_throw_type_error(isolate, "sqlite cursor handle must be an opaque Sloppy handle");
        return nullptr;
    }

    SlStatus status = sl_resource_table_get(&backend->resources, id,
                                            SL_RESOURCE_KIND_SQLITE_STATEMENT, &ptr, &diag);
    if (!sl_status_is_ok(status)) {
        sqlite_v8_throw_diag(isolate, "sqlite cursor handle is invalid", diag);
        return nullptr;
    }

    return static_cast<SqliteV8CursorResource*>(ptr);
}

bool sqlite_v8_convert_param_values(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                    v8::Local<v8::Value> value,
                                    std::vector<SqliteV8ParamValue>* out)
{
    if (out == nullptr) {
        sqlite_v8_throw_error(isolate, "sqlite bridge internal error: missing parameter output");
        return false;
    }

    out->clear();
    if (value->IsUndefined() || value->IsNull()) {
        return true;
    }
    if (!value->IsArray()) {
        sqlite_v8_throw_type_error(isolate, "sqlite parameters must be an array when supplied");
        return false;
    }

    v8::Local<v8::Array> array = value.As<v8::Array>();
    uint32_t length = array->Length();
    if (length > sqlite_v8_max_parameter_count) {
        sqlite_v8_throw_type_error(isolate,
                                   "sqlite parameter array exceeds supported parameter count");
        return false;
    }
    out->reserve(length);

    for (uint32_t index = 0U; index < length; index += 1U) {
        v8::Local<v8::Value> item;
        SqliteV8ParamValue param = {};
        if (!array->Get(context, index).ToLocal(&item)) {
            return false;
        }

        if (item->IsUndefined()) {
            sqlite_v8_throw_type_error(isolate,
                                       "sqlite undefined parameters are not SQL NULL; use null");
            return false;
        }
        if (item->IsNull()) {
            param.kind = SL_SQLITE_PARAM_NULL;
        }
        else if (item->IsBoolean()) {
            param.kind = SL_SQLITE_PARAM_BOOL;
            param.value = item->BooleanValue(isolate);
        }
        else if (item->IsInt32()) {
            param.kind = SL_SQLITE_PARAM_INTEGER;
            param.value = static_cast<int64_t>(item.As<v8::Int32>()->Value());
        }
        else if (item->IsNumber()) {
            const double number = item.As<v8::Number>()->Value();
            if (!std::isfinite(number)) {
                sqlite_v8_throw_type_error(isolate, "sqlite number parameters must be finite");
                return false;
            }
            if (std::isfinite(number) && std::trunc(number) == number &&
                (number < sqlite_v8_min_safe_integer || number > sqlite_v8_max_safe_integer))
            {
                sqlite_v8_throw_type_error(
                    isolate,
                    "sqlite integer parameters outside JS safe integer range must use BigInt");
                return false;
            }
            if (std::trunc(number) == number) {
                param.kind = SL_SQLITE_PARAM_INTEGER;
                param.value = static_cast<int64_t>(number);
            }
            else {
                param.kind = SL_SQLITE_PARAM_FLOAT;
                param.value = number;
            }
        }
        else if (item->IsBigInt()) {
            bool lossless = false;
            int64_t value64 = item.As<v8::BigInt>()->Int64Value(&lossless);
            if (!lossless) {
                sqlite_v8_throw_type_error(isolate, "sqlite BigInt parameters must fit int64");
                return false;
            }
            param.kind = SL_SQLITE_PARAM_INTEGER;
            param.value = value64;
        }
        else if (item->IsString()) {
            std::string text;
            param.kind = SL_SQLITE_PARAM_TEXT;
            if (!sl_v8_std_string_from_value(isolate, item, &text)) {
                sqlite_v8_throw_error(isolate, "sqlite bridge could not copy parameter text");
                return false;
            }
            param.value = std::move(text);
        }
        else if (item->IsUint8Array()) {
            std::vector<unsigned char> bytes;
            param.kind = SL_SQLITE_PARAM_BLOB;
            if (!sl_v8_db_copy_uint8_array(item, &bytes)) {
                sqlite_v8_throw_type_error(isolate, "sqlite Uint8Array parameter is out of range");
                return false;
            }
            param.value = std::move(bytes);
        }
        else {
            sqlite_v8_throw_type_error(
                isolate,
                "sqlite parameters support only null, string, number, BigInt, boolean, and bytes");
            return false;
        }

        out->push_back(std::move(param));
    }

    return true;
}

bool sqlite_v8_prepare_param_values(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                    const v8::FunctionCallbackInfo<v8::Value>& args,
                                    std::vector<SqliteV8ParamValue>* out)
{
    return sqlite_v8_convert_param_values(
        isolate, context, args.Length() >= 3 ? args[2] : v8::Undefined(isolate), out);
}

bool sqlite_v8_prepare_query_options(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                     const v8::FunctionCallbackInfo<v8::Value>& args,
                                     SqliteV8Request* request, const char* operation_label)
{
    if (request == nullptr) {
        return false;
    }
    if (!sl_v8_db_parse_max_rows_option(
            isolate, context, args.Length() >= 4 ? args[3] : v8::Undefined(isolate),
            SL_SQLITE_DEFAULT_MAX_ROWS, &request->max_rows, operation_label))
    {
        return false;
    }
    return sl_v8_db_parse_timeout_ms_option(
        isolate, context, args.Length() >= 4 ? args[3] : v8::Undefined(isolate),
        &request->has_timeout_ms, &request->timeout_ms, operation_label);
}

bool sqlite_v8_parse_cursor_batch_size(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                       v8::Local<v8::Value> options, size_t* out,
                                       const char* operation_label)
{
    if (out == nullptr) {
        return false;
    }
    *out = SL_SQLITE_DEFAULT_CURSOR_BATCH_SIZE;
    if (options->IsUndefined() || options->IsNull()) {
        return true;
    }
    if (!options->IsObject() || options->IsArray()) {
        sqlite_v8_throw_type_error(isolate, "sqlite cursor options must be an object");
        return false;
    }
    v8::Local<v8::Value> batch_value;
    if (!sl_v8_db_get_object_property(isolate, context, options.As<v8::Object>(), "batchSize",
                                      &batch_value))
    {
        return false;
    }
    if (batch_value->IsUndefined() || batch_value->IsNull()) {
        return true;
    }
    if (!batch_value->IsNumber()) {
        std::string label = operation_label == nullptr ? "sqlite cursor" : operation_label;
        sqlite_v8_throw_type_error(
            isolate, (label + " batchSize option must be an integer from 1 to 4096").c_str());
        return false;
    }
    double batch_size = batch_value.As<v8::Number>()->Value();
    if (!std::isfinite(batch_size) || std::floor(batch_size) != batch_size || batch_size < 1.0 ||
        batch_size > 4096.0)
    {
        std::string label = operation_label == nullptr ? "sqlite cursor" : operation_label;
        sqlite_v8_throw_type_error(
            isolate, (label + " batchSize option must be an integer from 1 to 4096").c_str());
        return false;
    }
    *out = static_cast<size_t>(batch_size);
    return true;
}

bool sqlite_v8_prepare_cursor_options(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                      const v8::FunctionCallbackInfo<v8::Value>& args,
                                      SlSqliteCursorOptions* options, const char* operation_label)
{
    bool has_timeout = false;
    uint32_t max_rows = 0U;
    uint32_t timeout_ms = 0U;
    size_t batch_size = SL_SQLITE_DEFAULT_CURSOR_BATCH_SIZE;
    v8::Local<v8::Value> value = args.Length() >= 4 ? args[3] : v8::Undefined(isolate);

    if (options == nullptr) {
        return false;
    }
    if (!sl_v8_db_parse_max_rows_option(isolate, context, value, 0U, &max_rows, operation_label)) {
        return false;
    }
    if (!sl_v8_db_parse_timeout_ms_option(isolate, context, value, &has_timeout, &timeout_ms,
                                          operation_label))
    {
        return false;
    }
    if (!sqlite_v8_parse_cursor_batch_size(isolate, context, value, &batch_size, operation_label)) {
        return false;
    }
    options->batch_size = batch_size;
    options->max_rows = max_rows;
    options->timeout_ms = has_timeout ? timeout_ms : 0U;
    return true;
}

void sqlite_v8_refresh_param_views(SqliteV8Request* request)
{
    if (request == nullptr) {
        return;
    }

    request->params.clear();
    request->params.reserve(request->param_values.size());
    for (SqliteV8ParamValue& value : request->param_values) {
        SlSqliteParam param = {};
        param.kind = value.kind;
        switch (value.kind) {
        case SL_SQLITE_PARAM_TEXT: {
            const auto* text = std::get_if<std::string>(&value.value);
            if (text == nullptr) {
                param.kind = SL_SQLITE_PARAM_NULL;
                break;
            }
            param.value.text = sl_str_from_parts(text->data(), text->size());
            break;
        }
        case SL_SQLITE_PARAM_BLOB: {
            const auto* bytes = std::get_if<std::vector<unsigned char>>(&value.value);
            if (bytes == nullptr) {
                param.kind = SL_SQLITE_PARAM_NULL;
                break;
            }
            param.value.blob =
                sl_bytes_from_parts(bytes->empty() ? nullptr : bytes->data(), bytes->size());
            break;
        }
        case SL_SQLITE_PARAM_INTEGER: {
            const auto* integer = std::get_if<int64_t>(&value.value);
            if (integer == nullptr) {
                param.kind = SL_SQLITE_PARAM_NULL;
                break;
            }
            param.value.integer = *integer;
            break;
        }
        case SL_SQLITE_PARAM_FLOAT: {
            const auto* number = std::get_if<double>(&value.value);
            if (number == nullptr) {
                param.kind = SL_SQLITE_PARAM_NULL;
                break;
            }
            param.value.number = *number;
            break;
        }
        case SL_SQLITE_PARAM_BOOL: {
            const auto* boolean = std::get_if<bool>(&value.value);
            if (boolean == nullptr) {
                param.kind = SL_SQLITE_PARAM_NULL;
                break;
            }
            param.value.boolean = *boolean;
            break;
        }
        case SL_SQLITE_PARAM_NULL:
        default:
            break;
        }
        request->params.push_back(param);
    }
}

bool sqlite_v8_make_exec_result(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                const SlSqliteExecResult& result, v8::Local<v8::Object>* out)
{
    v8::Local<v8::Object> output = v8::Object::New(isolate);
    v8::Local<v8::String> key;

    if (out == nullptr ||
        !sl_status_is_ok(
            sqlite_v8_to_local_string(isolate, sl_str_from_cstr("affectedRows"), &key)) ||
        !output->Set(context, key, v8::Int32::New(isolate, result.changes)).FromMaybe(false))
    {
        return false;
    }

    *out = output;
    return true;
}

bool sqlite_v8_value_to_local(v8::Isolate* isolate, v8::Local<v8::Context> context,
                              const SlSqliteValue* value, v8::Local<v8::Value>* out)
{
    (void)context;

    v8::Local<v8::Value> js_value;

    if (value == nullptr || out == nullptr) {
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
        if (value->value.integer < static_cast<int64_t>(sqlite_v8_min_safe_integer) ||
            value->value.integer > static_cast<int64_t>(sqlite_v8_max_safe_integer))
        {
            js_value = v8::BigInt::New(isolate, value->value.integer);
        }
        else {
            js_value = v8::Number::New(isolate, static_cast<double>(value->value.integer));
        }
        break;
    case SL_SQLITE_VALUE_FLOAT:
        js_value = v8::Number::New(isolate, value->value.number);
        break;
    case SL_SQLITE_VALUE_BLOB: {
        if (!sl_v8_db_uint8_array_from_bytes(isolate, value->value.blob, &js_value)) {
            return false;
        }
        break;
    }
    default:
        return false;
    }

    *out = js_value;
    return true;
}

bool sqlite_v8_result_to_array(v8::Isolate* isolate, v8::Local<v8::Context> context,
                               const SlSqliteResult* result, v8::Local<v8::Array>* out)
{
    v8::Local<v8::Array> rows;
    SlV8DbColumnSet columns;
    std::vector<v8::Local<v8::Value>> values;

    if (result == nullptr || out == nullptr ||
        result->row_count > static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        return false;
    }

    if (!sl_v8_db_prepare_column_set(isolate, context, result->column_names, result->column_count,
                                     &columns))
    {
        return false;
    }
    values.resize(result->column_count);
    rows = v8::Array::New(isolate, static_cast<int>(result->row_count));
    for (size_t row_index = 0U; row_index < result->row_count; row_index += 1U) {
        v8::Local<v8::Object> row;
        for (size_t column_index = 0U; column_index < result->column_count; column_index += 1U) {
            if (!sqlite_v8_value_to_local(isolate, context,
                                          &result->rows[row_index].values[column_index],
                                          &values[column_index]))
            {
                return false;
            }
        }
        if (!sl_v8_db_make_row_object(isolate, context, &columns, values.data(), values.size(),
                                      &row))
        {
            return false;
        }
        if (!rows->Set(context, static_cast<uint32_t>(row_index), row).FromMaybe(false)) {
            return false;
        }
    }

    if (!sl_v8_db_attach_result_metadata(isolate, context, rows, &columns, SL_V8_DB_STRING_OBJECT))
    {
        return false;
    }
    *out = rows;
    return true;
}

bool sqlite_v8_result_to_raw(v8::Isolate* isolate, v8::Local<v8::Context> context,
                             const SlSqliteResult* result, v8::Local<v8::Object>* out)
{
    v8::Local<v8::Array> rows;
    SlV8DbColumnSet columns;
    std::vector<v8::Local<v8::Value>> values;

    if (result == nullptr || out == nullptr ||
        result->row_count > static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        return false;
    }

    if (!sl_v8_db_prepare_column_set(isolate, context, result->column_names, result->column_count,
                                     &columns))
    {
        return false;
    }
    values.resize(result->column_count);
    rows = v8::Array::New(isolate, static_cast<int>(result->row_count));
    for (size_t row_index = 0U; row_index < result->row_count; row_index += 1U) {
        v8::Local<v8::Array> row;
        for (size_t column_index = 0U; column_index < result->column_count; column_index += 1U) {
            if (!sqlite_v8_value_to_local(isolate, context,
                                          &result->rows[row_index].values[column_index],
                                          &values[column_index]))
            {
                return false;
            }
        }
        if (!sl_v8_db_make_raw_row(isolate, context, values.data(), values.size(), &row) ||
            !rows->Set(context, static_cast<uint32_t>(row_index), row).FromMaybe(false))
        {
            return false;
        }
    }

    return sl_v8_db_make_raw_result(isolate, context, &columns, rows, out);
}

bool sqlite_v8_cursor_row_to_value(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                   const SlSqliteCursor* cursor, const SlSqliteRow* row, bool raw,
                                   v8::Local<v8::Value>* out)
{
    SlV8DbColumnSet columns;
    std::vector<v8::Local<v8::Value>> values;

    if (cursor == nullptr || row == nullptr || out == nullptr ||
        (cursor->column_count > 0U && row->values == nullptr))
    {
        return false;
    }

    if (!sl_v8_db_prepare_column_set(isolate, context, cursor->column_names, cursor->column_count,
                                     &columns))
    {
        return false;
    }
    values.resize(cursor->column_count);
    for (size_t column_index = 0U; column_index < cursor->column_count; column_index += 1U) {
        if (!sqlite_v8_value_to_local(isolate, context, &row->values[column_index],
                                      &values[column_index]))
        {
            return false;
        }
    }

    if (raw) {
        v8::Local<v8::Array> row_array;
        if (!sl_v8_db_make_raw_row(isolate, context, values.data(), values.size(), &row_array)) {
            return false;
        }
        *out = row_array;
        return true;
    }

    v8::Local<v8::Object> row_object;
    if (!sl_v8_db_make_row_object(isolate, context, &columns, values.data(), values.size(),
                                  &row_object))
    {
        return false;
    }
    *out = row_object;
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

    SlV8DbColumnSet columns;
    std::vector<v8::Local<v8::Value>> values(result->column_count);
    if (!sl_v8_db_prepare_column_set(isolate, context, result->column_names, result->column_count,
                                     &columns))
    {
        return false;
    }
    for (size_t column_index = 0U; column_index < result->column_count; column_index += 1U) {
        if (!sqlite_v8_value_to_local(isolate, context, &result->values[column_index],
                                      &values[column_index]))
        {
            return false;
        }
    }
    if (!sl_v8_db_make_row_object(isolate, context, &columns, values.data(), values.size(), &row) ||
        !sl_v8_db_attach_result_metadata(isolate, context, row, &columns, SL_V8_DB_STRING_OBJECT))
    {
        return false;
    }

    *out = row;
    return true;
}

std::string sqlite_v8_diag_message(const SlDiag& diag, const char* fallback)
{
    return sqlite_v8_diag_to_string(fallback, diag);
}

SlStatus sqlite_v8_run_request_with_capacity(SqliteV8Request* request, size_t capacity,
                                             SlDiag* out_diag)
{
    if (request == nullptr || out_diag == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    request->storage.assign(capacity, 0U);
    request->arena = {};
    SlStatus status =
        sl_arena_init(&request->arena, request->storage.data(), request->storage.size());
    if (!sl_status_is_ok(status)) {
        return status;
    }

    sqlite_v8_refresh_param_views(request);
    SlStr sql = sl_str_from_parts(request->sql.data(), request->sql.size());
    const SlSqliteParam* params = request->params.empty() ? nullptr : request->params.data();
    size_t param_count = request->params.size();

    switch (request->operation) {
    case SqliteV8Operation::Exec:
        return sl_sqlite_exec(&request->arena, &request->resource->connection, sql, params,
                              param_count, &request->exec_result, out_diag);
    case SqliteV8Operation::Query:
    case SqliteV8Operation::QueryRaw: {
        SlSqliteQueryOptionsV2 options = {};
        options.max_rows = request->max_rows;
        options.timeout_ms = request->has_timeout_ms ? request->timeout_ms : 0U;
        return sl_sqlite_query_v2(&request->arena, &request->resource->connection, sql, params,
                                  param_count, &options, &request->query_result, out_diag);
    }
    case SqliteV8Operation::QueryOne:
        return sl_sqlite_query_one(&request->arena, &request->resource->connection, sql, params,
                                   param_count, &request->one_result, out_diag);
    case SqliteV8Operation::TransactionBegin: {
        SlSqliteTransaction transaction = {};
        status = sl_sqlite_transaction_begin(&request->arena, &request->resource->connection,
                                             &transaction, out_diag);
        if (sl_status_is_ok(status)) {
            request->resource->transaction = transaction;
        }
        return status;
    }
    case SqliteV8Operation::TransactionCommit:
        return sl_sqlite_transaction_commit(&request->arena, &request->resource->transaction,
                                            out_diag);
    case SqliteV8Operation::TransactionRollback:
        return sl_sqlite_transaction_rollback(&request->arena, &request->resource->transaction,
                                              out_diag);
    case SqliteV8Operation::TransactionExec:
        return sl_sqlite_transaction_exec(&request->arena, &request->resource->transaction, sql,
                                          params, param_count, &request->exec_result, out_diag);
    case SqliteV8Operation::TransactionQuery:
    case SqliteV8Operation::TransactionQueryRaw: {
        SlSqliteQueryOptionsV2 options = {};
        options.max_rows = request->max_rows;
        options.timeout_ms = request->has_timeout_ms ? request->timeout_ms : 0U;
        return sl_sqlite_transaction_query_v2(&request->arena, &request->resource->transaction, sql,
                                              params, param_count, &options, &request->query_result,
                                              out_diag);
    }
    case SqliteV8Operation::TransactionQueryOne:
        return sl_sqlite_transaction_query_one(&request->arena, &request->resource->transaction,
                                               sql, params, param_count, &request->one_result,
                                               out_diag);
    default:
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
}

bool sqlite_v8_operation_returns_rows(SqliteV8Operation operation)
{
    return operation == SqliteV8Operation::Query || operation == SqliteV8Operation::QueryRaw ||
           operation == SqliteV8Operation::QueryOne ||
           operation == SqliteV8Operation::TransactionQuery ||
           operation == SqliteV8Operation::TransactionQueryRaw ||
           operation == SqliteV8Operation::TransactionQueryOne;
}

SlStatus sqlite_v8_provider_run(SlProviderOperation* operation, void* user,
                                SlDiagCode* out_diag_code, SlStr* out_message)
{
    SqliteV8Request* request = static_cast<SqliteV8Request*>(user);
    size_t capacity = 131072U;
    SlDiag diag = {};
    SlStatus status;

    (void)operation;
    if (request == nullptr || request->resource == nullptr || out_diag_code == nullptr ||
        out_message == nullptr)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    for (;;) {
        diag = {};
        status = sqlite_v8_run_request_with_capacity(request, capacity, &diag);
        if (sl_status_code(status) != SL_STATUS_OUT_OF_MEMORY ||
            !sqlite_v8_operation_returns_rows(request->operation) || capacity > (SIZE_MAX / 2U))
        {
            break;
        }
        capacity *= 2U;
    }

    request->status = status;
    if (!sl_status_is_ok(status)) {
        request->error = sqlite_v8_diag_message(diag, "sqlite operation failed");
        *out_diag_code = diag.code == SL_DIAG_NONE ? SL_DIAG_SQLITE_PROVIDER_ERROR : diag.code;
        *out_message = sl_str_from_parts(request->error.data(), request->error.size());
    }
    else {
        *out_diag_code = SL_DIAG_NONE;
        *out_message = sl_str_from_cstr("sqlite operation completed");
    }
    return status;
}

bool sqlite_v8_request_value(v8::Isolate* isolate, v8::Local<v8::Context> context,
                             SqliteV8Request* request, v8::Local<v8::Value>* out,
                             std::string* out_error)
{
    if (request == nullptr || out == nullptr || out_error == nullptr) {
        return false;
    }

    switch (request->operation) {
    case SqliteV8Operation::Exec:
    case SqliteV8Operation::TransactionExec: {
        v8::Local<v8::Object> result;
        if (!sqlite_v8_make_exec_result(isolate, context, request->exec_result, &result)) {
            *out_error = "sqlite bridge could not create exec result";
            return false;
        }
        *out = result;
        return true;
    }
    case SqliteV8Operation::Query:
    case SqliteV8Operation::TransactionQuery: {
        v8::Local<v8::Array> rows;
        if (!sqlite_v8_result_to_array(isolate, context, &request->query_result, &rows)) {
            *out_error = "sqlite bridge could not materialize query rows";
            return false;
        }
        *out = rows;
        return true;
    }
    case SqliteV8Operation::QueryRaw:
    case SqliteV8Operation::TransactionQueryRaw: {
        v8::Local<v8::Object> result;
        if (!sqlite_v8_result_to_raw(isolate, context, &request->query_result, &result)) {
            *out_error = "sqlite bridge could not materialize raw query rows";
            return false;
        }
        *out = result;
        return true;
    }
    case SqliteV8Operation::QueryOne:
    case SqliteV8Operation::TransactionQueryOne:
        return sqlite_v8_one_to_value(isolate, context, &request->one_result, out);
    case SqliteV8Operation::TransactionBegin:
    case SqliteV8Operation::TransactionCommit:
    case SqliteV8Operation::TransactionRollback:
        *out = v8::Undefined(isolate);
        return true;
    default:
        *out_error = "sqlite bridge received an unsupported operation result";
        return false;
    }
}

SlCapabilityOperation sqlite_v8_operation_capability(SqliteV8Operation operation)
{
    switch (operation) {
    case SqliteV8Operation::Query:
    case SqliteV8Operation::QueryRaw:
    case SqliteV8Operation::QueryOne:
    case SqliteV8Operation::TransactionQuery:
    case SqliteV8Operation::TransactionQueryRaw:
    case SqliteV8Operation::TransactionQueryOne:
        return SL_CAPABILITY_OPERATION_READ;
    case SqliteV8Operation::Exec:
    case SqliteV8Operation::TransactionBegin:
    case SqliteV8Operation::TransactionCommit:
    case SqliteV8Operation::TransactionRollback:
    case SqliteV8Operation::TransactionExec:
    default:
        return SL_CAPABILITY_OPERATION_WRITE;
    }
}

SlStatus sqlite_v8_completion_dispatch(SlAsyncLoop* loop, const SlAsyncCompletion* completion,
                                       void* user)
{
    SlProviderOperation* provider_operation =
        completion == nullptr ? nullptr : static_cast<SlProviderOperation*>(completion->payload);
    SqliteV8Request* request = provider_operation == nullptr
                                   ? nullptr
                                   : static_cast<SqliteV8Request*>(provider_operation->run_user);
    SlV8Engine* backend = request == nullptr ? nullptr : request->backend;

    (void)loop;
    (void)user;
    if (request == nullptr || backend == nullptr || backend->isolate == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (request->resource != nullptr) {
        request->resource->pending_operations.fetch_sub(1U);
    }

    v8::Isolate* isolate = backend->isolate;
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = backend->context.Get(isolate);
    v8::Context::Scope context_scope(context);
    v8::Local<v8::Promise::Resolver> resolver = request->resolver.Get(isolate);

    bool ok = false;
    if (!sl_status_is_ok(request->status)) {
        ok = sl_v8_db_reject_promise(isolate, context, resolver, request->error,
                                     "sqlite operation failed");
    }
    else {
        v8::Local<v8::Value> value;
        std::string error;
        if (sqlite_v8_request_value(isolate, context, request, &value, &error)) {
            ok = sl_v8_db_resolve_promise(context, resolver, value);
        }
        else {
            ok = sl_v8_db_reject_promise(isolate, context, resolver, error,
                                         "sqlite result conversion failed");
        }
    }

    request->resolver.Reset();
    request->owner_dispatched = true;
    isolate->PerformMicrotaskCheckpoint();
    return ok ? sl_status_ok() : sl_status_from_code(SL_STATUS_INVALID_STATE);
}

void sqlite_v8_request_cleanup(SlProviderOperation* operation, void* user)
{
    (void)user;
    SqliteV8Request* request =
        operation == nullptr ? nullptr : static_cast<SqliteV8Request*>(operation->run_user);
    if (request == nullptr) {
        return;
    }
    if (!request->owner_dispatched && request->resource != nullptr) {
        request->resource->pending_operations.fetch_sub(1U);
    }
    request->resolver.Reset();
    delete request;
}

bool sqlite_v8_submit_request(v8::Isolate* isolate, v8::Local<v8::Context> context,
                              SlV8Engine* backend, SqliteV8ConnectionResource* resource,
                              std::unique_ptr<SqliteV8Request> request, const char* submit_error,
                              v8::Local<v8::Promise>* out_promise)
{
    if (backend == nullptr || resource == nullptr || !request || out_promise == nullptr ||
        backend->async_loop == nullptr || !backend->sqlite_executor_initialized)
    {
        sqlite_v8_throw_error(isolate, "sqlite async provider executor is unavailable");
        return false;
    }
    if (resource->closing.load()) {
        sqlite_v8_throw_error(isolate, "sqlite resource handle is closed");
        return false;
    }

    v8::Local<v8::Promise> promise;
    if (!sl_v8_db_make_promise(isolate, context, &request->resolver, &promise)) {
        sqlite_v8_throw_error(isolate, "sqlite bridge could not create a Promise");
        return false;
    }

    request->backend = backend;
    request->resource = resource;
    resource->pending_operations.fetch_add(1U);

    SlProviderOperationDescriptor descriptor = sl_provider_operation_descriptor_init(
        sl_str_from_cstr("stdlib.sqlite"), sl_str_from_cstr("sqlite"),
        SL_PROVIDER_OPERATION_KIND_INTERNAL, sl_str_from_cstr("sqlite"),
        SL_PROVIDER_EXECUTION_SERIALIZED_BLOCKING, sqlite_v8_completion_dispatch, nullptr);
    sl_provider_operation_descriptor_attach_capability(
        &descriptor, sl_str_from_parts(resource->capability.data(), resource->capability.size()),
        sqlite_v8_operation_capability(request->operation));
    sl_provider_operation_descriptor_attach_admission_diag(&descriptor, &request->admission_diag);
    sl_provider_operation_descriptor_attach_run(&descriptor, sqlite_v8_provider_run, request.get());
    sl_provider_operation_descriptor_attach_cleanup(&descriptor, sqlite_v8_request_cleanup,
                                                    nullptr);

    SlProviderOperation* provider_operation = nullptr;
    SlStatus status = sl_provider_executor_submit(&backend->sqlite_executor, backend->arena,
                                                  &descriptor, &provider_operation);
    if (!sl_status_is_ok(status)) {
        resource->pending_operations.fetch_sub(1U);
        request->resolver.Reset();
        sqlite_v8_throw_error(
            isolate, sqlite_v8_diag_to_string(submit_error == nullptr
                                                  ? "sqlite operation could not be submitted"
                                                  : submit_error,
                                              request->admission_diag));
        return false;
    }

    (void)provider_operation;
    *out_promise = promise;
    request.release();
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
    if (backend->async_loop == nullptr || !backend->sqlite_executor_initialized) {
        sqlite_v8_throw_error(isolate, "sqlite async provider executor is unavailable");
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
    resource->access = request.access;

    if (!sqlite_v8_check_capability(isolate, backend, &arena, resource,
                                    sqlite_v8_open_capability_operation(request.access)))
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
        sl_resource_table_close(&backend->resources, id, nullptr);
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
    SqliteV8ConnectionResource* resource = static_cast<SqliteV8ConnectionResource*>(ptr);
    if (resource != nullptr && resource->pending_operations.load() != 0U) {
        sqlite_v8_throw_error(isolate, "sqlite connection has pending operations");
        return;
    }
    if (resource != nullptr) {
        resource->closing.store(true);
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
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::string sql;
    SqliteV8ConnectionResource* resource = nullptr;
    std::unique_ptr<SqliteV8Request> request(new (std::nothrow) SqliteV8Request());
    v8::Local<v8::Promise> promise;

    if (backend == nullptr || args.Length() < 2 || args.Length() > 3 || !request ||
        !sqlite_v8_value_to_std_string(isolate, args[1], &sql) || sql.empty())
    {
        sqlite_v8_throw_type_error(
            isolate,
            "__sloppy.data.sqlite.exec requires a handle, SQL string, and optional params");
        return;
    }

    resource = sqlite_v8_lookup_connection(isolate, context, backend, args[0]);
    if (resource == nullptr) {
        return;
    }

    if (!sqlite_v8_prepare_param_values(isolate, context, args, &request->param_values)) {
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

    request->operation = SqliteV8Operation::Exec;
    request->sql = std::move(sql);
    if (!sqlite_v8_submit_request(isolate, context, backend, resource, std::move(request),
                                  "sqlite exec could not be submitted", &promise))
    {
        return;
    }

    args.GetReturnValue().Set(promise);
}

void sqlite_v8_query_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    constexpr size_t scratch_size = 65536U;
    unsigned char scratch[scratch_size];
    SlArena arena = {};
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::string sql;
    SqliteV8ConnectionResource* resource = nullptr;
    std::unique_ptr<SqliteV8Request> request(new (std::nothrow) SqliteV8Request());
    v8::Local<v8::Promise> promise;

    if (backend == nullptr || args.Length() < 2 || args.Length() > 4 || !request ||
        !sqlite_v8_value_to_std_string(isolate, args[1], &sql) || sql.empty())
    {
        sqlite_v8_throw_type_error(isolate, "__sloppy.data.sqlite.query requires a handle, SQL "
                                            "string, optional params, and optional options");
        return;
    }

    resource = sqlite_v8_lookup_connection(isolate, context, backend, args[0]);
    if (resource == nullptr) {
        return;
    }

    if (!sl_status_is_ok(sl_arena_init(&arena, scratch, sizeof(scratch)))) {
        sqlite_v8_throw_error(isolate, "sqlite bridge scratch arena initialization failed");
        return;
    }

    if (!sqlite_v8_prepare_param_values(isolate, context, args, &request->param_values)) {
        return;
    }
    if (!sqlite_v8_prepare_query_options(isolate, context, args, request.get(), "sqlite query")) {
        return;
    }

    if (!sqlite_v8_check_capability(isolate, backend, &arena, resource,
                                    SL_CAPABILITY_OPERATION_READ))
    {
        return;
    }

    request->operation = SqliteV8Operation::Query;
    request->sql = std::move(sql);
    if (!sqlite_v8_submit_request(isolate, context, backend, resource, std::move(request),
                                  "sqlite query could not be submitted", &promise))
    {
        return;
    }

    args.GetReturnValue().Set(promise);
}

void sqlite_v8_query_raw_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    constexpr size_t scratch_size = 65536U;
    unsigned char scratch[scratch_size];
    SlArena arena = {};
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::string sql;
    SqliteV8ConnectionResource* resource = nullptr;
    std::unique_ptr<SqliteV8Request> request(new (std::nothrow) SqliteV8Request());
    v8::Local<v8::Promise> promise;

    if (backend == nullptr || args.Length() < 2 || args.Length() > 4 || !request ||
        !sqlite_v8_value_to_std_string(isolate, args[1], &sql) || sql.empty())
    {
        sqlite_v8_throw_type_error(isolate, "__sloppy.data.sqlite.queryRaw requires a handle, SQL "
                                            "string, optional params, and optional options");
        return;
    }

    resource = sqlite_v8_lookup_connection(isolate, context, backend, args[0]);
    if (resource == nullptr) {
        return;
    }

    if (!sl_status_is_ok(sl_arena_init(&arena, scratch, sizeof(scratch)))) {
        sqlite_v8_throw_error(isolate, "sqlite bridge scratch arena initialization failed");
        return;
    }

    if (!sqlite_v8_prepare_param_values(isolate, context, args, &request->param_values)) {
        return;
    }
    if (!sqlite_v8_prepare_query_options(isolate, context, args, request.get(), "sqlite raw query"))
    {
        return;
    }

    if (!sqlite_v8_check_capability(isolate, backend, &arena, resource,
                                    SL_CAPABILITY_OPERATION_READ))
    {
        return;
    }

    request->operation = SqliteV8Operation::QueryRaw;
    request->sql = std::move(sql);
    if (!sqlite_v8_submit_request(isolate, context, backend, resource, std::move(request),
                                  "sqlite raw query could not be submitted", &promise))
    {
        return;
    }

    args.GetReturnValue().Set(promise);
}

void sqlite_v8_cursor_open_callback_impl(const v8::FunctionCallbackInfo<v8::Value>& args, bool raw,
                                         bool transaction)
{
    constexpr size_t cursor_arena_size = 131072U;
    SlArena arena = {};
    SlDiag diag = {};
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::string sql;
    SqliteV8ConnectionResource* owner = nullptr;
    std::unique_ptr<SqliteV8CursorResource> cursor(new (std::nothrow) SqliteV8CursorResource());
    SqliteV8Request request;
    SlSqliteCursorOptions options = {};
    SlResourceId id = sl_resource_id_invalid();
    v8::Local<v8::Object> handle;
    SlV8DbColumnSet columns;

    if (backend == nullptr || args.Length() < 2 || args.Length() > 4 || !cursor ||
        !sqlite_v8_value_to_std_string(isolate, args[1], &sql) || sql.empty())
    {
        sqlite_v8_throw_type_error(
            isolate,
            transaction
                ? "__sloppy.data.sqlite.transactionQueryCursor requires a handle, SQL string, "
                  "optional params, and optional options"
                : "__sloppy.data.sqlite.queryCursor requires a handle, SQL string, optional "
                  "params, and optional options");
        return;
    }

    owner = sqlite_v8_lookup_connection(isolate, context, backend, args[0]);
    if (owner == nullptr) {
        return;
    }
    if (owner->closing.load()) {
        sqlite_v8_throw_error(isolate, "sqlite resource handle is closed");
        return;
    }
    if (!sqlite_v8_prepare_param_values(isolate, context, args, &request.param_values)) {
        return;
    }
    if (!sqlite_v8_prepare_cursor_options(isolate, context, args, &options,
                                          raw ? "sqlite raw cursor" : "sqlite cursor"))
    {
        return;
    }

    cursor->arena_storage.assign(cursor_arena_size, 0U);
    if (!sl_status_is_ok(
            sl_arena_init(&arena, cursor->arena_storage.data(), cursor->arena_storage.size())))
    {
        sqlite_v8_throw_error(isolate, "sqlite cursor arena initialization failed");
        return;
    }
    if (!sqlite_v8_check_capability(isolate, backend, &arena, owner, SL_CAPABILITY_OPERATION_READ))
    {
        return;
    }

    request.sql = sql;
    sqlite_v8_refresh_param_views(&request);
    SlStr sql_view = sl_str_from_parts(request.sql.data(), request.sql.size());
    const SlSqliteParam* params = request.params.empty() ? nullptr : request.params.data();
    SlStatus status =
        transaction
            ? sl_sqlite_transaction_cursor_open(&arena, &owner->transaction, sql_view, params,
                                                request.params.size(), &options, &cursor->cursor,
                                                &diag)
            : sl_sqlite_cursor_open(&arena, &owner->connection, sql_view, params,
                                    request.params.size(), &options, &cursor->cursor, &diag);
    if (!sl_status_is_ok(status)) {
        sqlite_v8_throw_diag(isolate, "sqlite cursor open failed", diag);
        return;
    }

    cursor->owner = owner;
    cursor->raw = raw;
    owner->pending_operations.fetch_add(1U);

    status = sl_resource_table_insert(&backend->resources, SL_RESOURCE_KIND_SQLITE_STATEMENT,
                                      cursor.get(), sqlite_v8_cursor_cleanup, nullptr, &id, &diag);
    if (!sl_status_is_ok(status)) {
        sqlite_v8_cursor_cleanup(cursor.release(), nullptr);
        sqlite_v8_throw_diag(isolate, "sqlite cursor registration failed", diag);
        return;
    }

    if (!sqlite_v8_make_cursor_handle(isolate, context, id, &handle)) {
        sl_resource_table_close_kind(&backend->resources, id, SL_RESOURCE_KIND_SQLITE_STATEMENT,
                                     nullptr);
        sqlite_v8_throw_error(isolate, "sqlite bridge could not create a cursor handle");
        return;
    }
    if (!sl_v8_db_prepare_column_set(isolate, context, cursor->cursor.column_names,
                                     cursor->cursor.column_count, &columns) ||
        !handle->Set(context, v8::String::NewFromUtf8Literal(isolate, "columns"), columns.columns)
             .FromMaybe(false) ||
        !handle
             ->Set(context, v8::String::NewFromUtf8Literal(isolate, "provider"),
                   v8::String::NewFromUtf8Literal(isolate, "sqlite"))
             .FromMaybe(false) ||
        !handle
             ->Set(context, v8::String::NewFromUtf8Literal(isolate, "closed"),
                   v8::Boolean::New(isolate, false))
             .FromMaybe(false))
    {
        sl_resource_table_close_kind(&backend->resources, id, SL_RESOURCE_KIND_SQLITE_STATEMENT,
                                     nullptr);
        sqlite_v8_throw_error(isolate, "sqlite bridge could not describe cursor");
        return;
    }

    cursor.release();
    args.GetReturnValue().Set(handle);
}

void sqlite_v8_query_cursor_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    sqlite_v8_cursor_open_callback_impl(args, false, false);
}

void sqlite_v8_query_raw_cursor_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    sqlite_v8_cursor_open_callback_impl(args, true, false);
}

void sqlite_v8_transaction_query_cursor_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    sqlite_v8_cursor_open_callback_impl(args, false, true);
}

void sqlite_v8_transaction_query_raw_cursor_callback(
    const v8::FunctionCallbackInfo<v8::Value>& args)
{
    sqlite_v8_cursor_open_callback_impl(args, true, true);
}

void sqlite_v8_cursor_next_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    constexpr size_t scratch_size = 131072U;
    unsigned char scratch[scratch_size];
    SlArena arena = {};
    SlDiag diag = {};
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    SqliteV8CursorResource* cursor = nullptr;
    SlSqliteCursorNextResult next = {};
    v8::Local<v8::Object> result;

    if (backend == nullptr || args.Length() != 1) {
        sqlite_v8_throw_type_error(isolate, "__sloppy.data.sqlite.cursorNext requires a cursor");
        return;
    }
    cursor = sqlite_v8_lookup_cursor(isolate, context, backend, args[0]);
    if (cursor == nullptr) {
        return;
    }
    if (cursor->closed) {
        sqlite_v8_throw_error(isolate, "sqlite cursor is closed");
        return;
    }
    if (!sl_status_is_ok(sl_arena_init(&arena, scratch, sizeof(scratch)))) {
        sqlite_v8_throw_error(isolate, "sqlite cursor scratch arena initialization failed");
        return;
    }

    SlStatus status = sl_sqlite_cursor_next(&arena, &cursor->cursor, &next, &diag);
    if (!sl_status_is_ok(status)) {
        cursor->closed = true;
        sqlite_v8_throw_diag(isolate, "sqlite cursor next failed", diag);
        return;
    }

    result = v8::Object::New(isolate);
    if (next.done) {
        cursor->closed = true;
        if (!result
                 ->Set(context, v8::String::NewFromUtf8Literal(isolate, "done"),
                       v8::Boolean::New(isolate, true))
                 .FromMaybe(false))
        {
            sqlite_v8_throw_error(isolate, "sqlite cursor result allocation failed");
            return;
        }
        args.GetReturnValue().Set(result);
        return;
    }

    v8::Local<v8::Value> row_value;
    if (!sqlite_v8_cursor_row_to_value(isolate, context, &cursor->cursor, &next.row, cursor->raw,
                                       &row_value) ||
        !result
             ->Set(context, v8::String::NewFromUtf8Literal(isolate, "done"),
                   v8::Boolean::New(isolate, false))
             .FromMaybe(false) ||
        !result->Set(context, v8::String::NewFromUtf8Literal(isolate, "value"), row_value)
             .FromMaybe(false))
    {
        sqlite_v8_throw_error(isolate, "sqlite cursor result allocation failed");
        return;
    }

    args.GetReturnValue().Set(result);
}

void sqlite_v8_cursor_close_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    SlResourceId id = {};
    SlDiag diag = {};

    if (backend == nullptr || args.Length() != 1 ||
        !sqlite_v8_get_resource_id(isolate, context, args[0], &id))
    {
        sqlite_v8_throw_type_error(isolate, "__sloppy.data.sqlite.cursorClose requires a cursor");
        return;
    }

    SlStatus status = sl_resource_table_close_kind(&backend->resources, id,
                                                   SL_RESOURCE_KIND_SQLITE_STATEMENT, &diag);
    if (!sl_status_is_ok(status)) {
        sqlite_v8_throw_diag(isolate, "sqlite cursor close failed", diag);
        return;
    }

    args.GetReturnValue().Set(v8::Undefined(isolate));
}

void sqlite_v8_query_one_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    constexpr size_t scratch_size = 65536U;
    unsigned char scratch[scratch_size];
    SlArena arena = {};
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::string sql;
    SqliteV8ConnectionResource* resource = nullptr;
    std::unique_ptr<SqliteV8Request> request(new (std::nothrow) SqliteV8Request());
    v8::Local<v8::Promise> promise;

    if (backend == nullptr || args.Length() < 2 || args.Length() > 3 || !request ||
        !sqlite_v8_value_to_std_string(isolate, args[1], &sql) || sql.empty())
    {
        sqlite_v8_throw_type_error(
            isolate,
            "__sloppy.data.sqlite.queryOne requires a handle, SQL string, and optional params");
        return;
    }

    resource = sqlite_v8_lookup_connection(isolate, context, backend, args[0]);
    if (resource == nullptr) {
        return;
    }

    if (!sl_status_is_ok(sl_arena_init(&arena, scratch, sizeof(scratch)))) {
        sqlite_v8_throw_error(isolate, "sqlite bridge scratch arena initialization failed");
        return;
    }

    if (!sqlite_v8_prepare_param_values(isolate, context, args, &request->param_values)) {
        return;
    }

    if (!sqlite_v8_check_capability(isolate, backend, &arena, resource,
                                    SL_CAPABILITY_OPERATION_READ))
    {
        return;
    }

    request->operation = SqliteV8Operation::QueryOne;
    request->sql = std::move(sql);
    if (!sqlite_v8_submit_request(isolate, context, backend, resource, std::move(request),
                                  "sqlite queryOne could not be submitted", &promise))
    {
        return;
    }

    args.GetReturnValue().Set(promise);
}

void sqlite_v8_transaction_begin_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    constexpr size_t scratch_size = 65536U;
    unsigned char scratch[scratch_size];
    SlArena arena = {};
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    SqliteV8ConnectionResource* resource = nullptr;
    std::unique_ptr<SqliteV8Request> request(new (std::nothrow) SqliteV8Request());
    v8::Local<v8::Promise> promise;

    if (backend == nullptr || args.Length() != 1 || !request) {
        sqlite_v8_throw_type_error(
            isolate, "__sloppy.data.sqlite.transactionBegin requires a resource handle");
        return;
    }

    resource = sqlite_v8_lookup_connection(isolate, context, backend, args[0]);
    if (resource == nullptr) {
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

    request->operation = SqliteV8Operation::TransactionBegin;
    if (!sqlite_v8_submit_request(isolate, context, backend, resource, std::move(request),
                                  "sqlite transaction begin could not be submitted", &promise))
    {
        return;
    }

    args.GetReturnValue().Set(promise);
}

void sqlite_v8_transaction_commit_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    constexpr size_t scratch_size = 65536U;
    unsigned char scratch[scratch_size];
    SlArena arena = {};
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    SqliteV8ConnectionResource* resource = nullptr;
    std::unique_ptr<SqliteV8Request> request(new (std::nothrow) SqliteV8Request());
    v8::Local<v8::Promise> promise;

    if (backend == nullptr || args.Length() != 1 || !request) {
        sqlite_v8_throw_type_error(
            isolate, "__sloppy.data.sqlite.transactionCommit requires a resource handle");
        return;
    }

    resource = sqlite_v8_lookup_connection(isolate, context, backend, args[0]);
    if (resource == nullptr) {
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

    request->operation = SqliteV8Operation::TransactionCommit;
    if (!sqlite_v8_submit_request(isolate, context, backend, resource, std::move(request),
                                  "sqlite transaction commit could not be submitted", &promise))
    {
        return;
    }

    args.GetReturnValue().Set(promise);
}

void sqlite_v8_transaction_rollback_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    constexpr size_t scratch_size = 65536U;
    unsigned char scratch[scratch_size];
    SlArena arena = {};
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    SqliteV8ConnectionResource* resource = nullptr;
    std::unique_ptr<SqliteV8Request> request(new (std::nothrow) SqliteV8Request());
    v8::Local<v8::Promise> promise;

    if (backend == nullptr || args.Length() != 1 || !request) {
        sqlite_v8_throw_type_error(
            isolate, "__sloppy.data.sqlite.transactionRollback requires a resource handle");
        return;
    }

    resource = sqlite_v8_lookup_connection(isolate, context, backend, args[0]);
    if (resource == nullptr) {
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

    request->operation = SqliteV8Operation::TransactionRollback;
    if (!sqlite_v8_submit_request(isolate, context, backend, resource, std::move(request),
                                  "sqlite transaction rollback could not be submitted", &promise))
    {
        return;
    }

    args.GetReturnValue().Set(promise);
}

void sqlite_v8_transaction_exec_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    constexpr size_t scratch_size = 65536U;
    unsigned char scratch[scratch_size];
    SlArena arena = {};
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::string sql;
    SqliteV8ConnectionResource* resource = nullptr;
    std::unique_ptr<SqliteV8Request> request(new (std::nothrow) SqliteV8Request());
    v8::Local<v8::Promise> promise;

    if (backend == nullptr || args.Length() < 2 || args.Length() > 3 || !request ||
        !sqlite_v8_value_to_std_string(isolate, args[1], &sql) || sql.empty())
    {
        sqlite_v8_throw_type_error(isolate, "__sloppy.data.sqlite.transactionExec requires a "
                                            "handle, SQL string, and optional params");
        return;
    }

    resource = sqlite_v8_lookup_connection(isolate, context, backend, args[0]);
    if (resource == nullptr) {
        return;
    }

    if (!sl_status_is_ok(sl_arena_init(&arena, scratch, sizeof(scratch)))) {
        sqlite_v8_throw_error(isolate, "sqlite bridge scratch arena initialization failed");
        return;
    }

    if (!sqlite_v8_prepare_param_values(isolate, context, args, &request->param_values)) {
        return;
    }

    if (!sqlite_v8_check_capability(isolate, backend, &arena, resource,
                                    SL_CAPABILITY_OPERATION_WRITE))
    {
        return;
    }

    request->operation = SqliteV8Operation::TransactionExec;
    request->sql = std::move(sql);
    if (!sqlite_v8_submit_request(isolate, context, backend, resource, std::move(request),
                                  "sqlite transaction exec could not be submitted", &promise))
    {
        return;
    }

    args.GetReturnValue().Set(promise);
}

void sqlite_v8_transaction_query_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    constexpr size_t scratch_size = 65536U;
    unsigned char scratch[scratch_size];
    SlArena arena = {};
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::string sql;
    SqliteV8ConnectionResource* resource = nullptr;
    std::unique_ptr<SqliteV8Request> request(new (std::nothrow) SqliteV8Request());
    v8::Local<v8::Promise> promise;

    if (backend == nullptr || args.Length() < 2 || args.Length() > 4 || !request ||
        !sqlite_v8_value_to_std_string(isolate, args[1], &sql) || sql.empty())
    {
        sqlite_v8_throw_type_error(isolate,
                                   "__sloppy.data.sqlite.transactionQuery requires a "
                                   "handle, SQL string, optional params, and optional options");
        return;
    }

    resource = sqlite_v8_lookup_connection(isolate, context, backend, args[0]);
    if (resource == nullptr) {
        return;
    }

    if (!sl_status_is_ok(sl_arena_init(&arena, scratch, sizeof(scratch)))) {
        sqlite_v8_throw_error(isolate, "sqlite bridge scratch arena initialization failed");
        return;
    }

    if (!sqlite_v8_prepare_param_values(isolate, context, args, &request->param_values)) {
        return;
    }
    if (!sqlite_v8_prepare_query_options(isolate, context, args, request.get(),
                                         "sqlite transaction query"))
    {
        return;
    }

    if (!sqlite_v8_check_capability(isolate, backend, &arena, resource,
                                    SL_CAPABILITY_OPERATION_READ))
    {
        return;
    }

    request->operation = SqliteV8Operation::TransactionQuery;
    request->sql = std::move(sql);
    if (!sqlite_v8_submit_request(isolate, context, backend, resource, std::move(request),
                                  "sqlite transaction query could not be submitted", &promise))
    {
        return;
    }

    args.GetReturnValue().Set(promise);
}

void sqlite_v8_transaction_query_raw_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    constexpr size_t scratch_size = 65536U;
    unsigned char scratch[scratch_size];
    SlArena arena = {};
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::string sql;
    SqliteV8ConnectionResource* resource = nullptr;
    std::unique_ptr<SqliteV8Request> request(new (std::nothrow) SqliteV8Request());
    v8::Local<v8::Promise> promise;

    if (backend == nullptr || args.Length() < 2 || args.Length() > 4 || !request ||
        !sqlite_v8_value_to_std_string(isolate, args[1], &sql) || sql.empty())
    {
        sqlite_v8_throw_type_error(isolate,
                                   "__sloppy.data.sqlite.transactionQueryRaw requires a "
                                   "handle, SQL string, optional params, and optional options");
        return;
    }

    resource = sqlite_v8_lookup_connection(isolate, context, backend, args[0]);
    if (resource == nullptr) {
        return;
    }

    if (!sl_status_is_ok(sl_arena_init(&arena, scratch, sizeof(scratch)))) {
        sqlite_v8_throw_error(isolate, "sqlite bridge scratch arena initialization failed");
        return;
    }

    if (!sqlite_v8_prepare_param_values(isolate, context, args, &request->param_values)) {
        return;
    }
    if (!sqlite_v8_prepare_query_options(isolate, context, args, request.get(),
                                         "sqlite transaction raw query"))
    {
        return;
    }

    if (!sqlite_v8_check_capability(isolate, backend, &arena, resource,
                                    SL_CAPABILITY_OPERATION_READ))
    {
        return;
    }

    request->operation = SqliteV8Operation::TransactionQueryRaw;
    request->sql = std::move(sql);
    if (!sqlite_v8_submit_request(isolate, context, backend, resource, std::move(request),
                                  "sqlite transaction raw query could not be submitted", &promise))
    {
        return;
    }

    args.GetReturnValue().Set(promise);
}

void sqlite_v8_transaction_query_one_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    constexpr size_t scratch_size = 65536U;
    unsigned char scratch[scratch_size];
    SlArena arena = {};
    v8::Isolate* isolate = args.GetIsolate();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::string sql;
    SqliteV8ConnectionResource* resource = nullptr;
    std::unique_ptr<SqliteV8Request> request(new (std::nothrow) SqliteV8Request());
    v8::Local<v8::Promise> promise;

    if (backend == nullptr || args.Length() < 2 || args.Length() > 3 || !request ||
        !sqlite_v8_value_to_std_string(isolate, args[1], &sql) || sql.empty())
    {
        sqlite_v8_throw_type_error(isolate, "__sloppy.data.sqlite.transactionQueryOne requires a "
                                            "handle, SQL string, and optional params");
        return;
    }

    resource = sqlite_v8_lookup_connection(isolate, context, backend, args[0]);
    if (resource == nullptr) {
        return;
    }

    if (!sl_status_is_ok(sl_arena_init(&arena, scratch, sizeof(scratch)))) {
        sqlite_v8_throw_error(isolate, "sqlite bridge scratch arena initialization failed");
        return;
    }

    if (!sqlite_v8_prepare_param_values(isolate, context, args, &request->param_values)) {
        return;
    }

    if (!sqlite_v8_check_capability(isolate, backend, &arena, resource,
                                    SL_CAPABILITY_OPERATION_READ))
    {
        return;
    }

    request->operation = SqliteV8Operation::TransactionQueryOne;
    request->sql = std::move(sql);
    if (!sqlite_v8_submit_request(isolate, context, backend, resource, std::move(request),
                                  "sqlite transaction queryOne could not be submitted", &promise))
    {
        return;
    }

    args.GetReturnValue().Set(promise);
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

void sl_v8_append_sqlite_external_references(std::vector<intptr_t>* refs)
{
    if (refs == nullptr) {
        return;
    }
    refs->push_back(reinterpret_cast<intptr_t>(sqlite_v8_open_callback));
    refs->push_back(reinterpret_cast<intptr_t>(sqlite_v8_close_callback));
    refs->push_back(reinterpret_cast<intptr_t>(sqlite_v8_exec_callback));
    refs->push_back(reinterpret_cast<intptr_t>(sqlite_v8_query_callback));
    refs->push_back(reinterpret_cast<intptr_t>(sqlite_v8_query_raw_callback));
    refs->push_back(reinterpret_cast<intptr_t>(sqlite_v8_query_cursor_callback));
    refs->push_back(reinterpret_cast<intptr_t>(sqlite_v8_query_raw_cursor_callback));
    refs->push_back(reinterpret_cast<intptr_t>(sqlite_v8_cursor_next_callback));
    refs->push_back(reinterpret_cast<intptr_t>(sqlite_v8_cursor_close_callback));
    refs->push_back(reinterpret_cast<intptr_t>(sqlite_v8_query_one_callback));
    refs->push_back(reinterpret_cast<intptr_t>(sqlite_v8_transaction_begin_callback));
    refs->push_back(reinterpret_cast<intptr_t>(sqlite_v8_transaction_commit_callback));
    refs->push_back(reinterpret_cast<intptr_t>(sqlite_v8_transaction_rollback_callback));
    refs->push_back(reinterpret_cast<intptr_t>(sqlite_v8_transaction_exec_callback));
    refs->push_back(reinterpret_cast<intptr_t>(sqlite_v8_transaction_query_callback));
    refs->push_back(reinterpret_cast<intptr_t>(sqlite_v8_transaction_query_raw_callback));
    refs->push_back(reinterpret_cast<intptr_t>(sqlite_v8_transaction_query_cursor_callback));
    refs->push_back(reinterpret_cast<intptr_t>(sqlite_v8_transaction_query_raw_cursor_callback));
    refs->push_back(reinterpret_cast<intptr_t>(sqlite_v8_transaction_query_one_callback));
}

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
        !sqlite_v8_set_function(isolate, context, sqlite, "queryRaw",
                                sqlite_v8_query_raw_callback) ||
        !sqlite_v8_set_function(isolate, context, sqlite, "queryCursor",
                                sqlite_v8_query_cursor_callback) ||
        !sqlite_v8_set_function(isolate, context, sqlite, "queryRawCursor",
                                sqlite_v8_query_raw_cursor_callback) ||
        !sqlite_v8_set_function(isolate, context, sqlite, "cursorNext",
                                sqlite_v8_cursor_next_callback) ||
        !sqlite_v8_set_function(isolate, context, sqlite, "cursorClose",
                                sqlite_v8_cursor_close_callback) ||
        !sqlite_v8_set_function(isolate, context, sqlite, "queryOne",
                                sqlite_v8_query_one_callback) ||
        !sqlite_v8_set_function(isolate, context, sqlite, "transactionBegin",
                                sqlite_v8_transaction_begin_callback) ||
        !sqlite_v8_set_function(isolate, context, sqlite, "transactionCommit",
                                sqlite_v8_transaction_commit_callback) ||
        !sqlite_v8_set_function(isolate, context, sqlite, "transactionRollback",
                                sqlite_v8_transaction_rollback_callback) ||
        !sqlite_v8_set_function(isolate, context, sqlite, "transactionExec",
                                sqlite_v8_transaction_exec_callback) ||
        !sqlite_v8_set_function(isolate, context, sqlite, "transactionQuery",
                                sqlite_v8_transaction_query_callback) ||
        !sqlite_v8_set_function(isolate, context, sqlite, "transactionQueryRaw",
                                sqlite_v8_transaction_query_raw_callback) ||
        !sqlite_v8_set_function(isolate, context, sqlite, "transactionQueryCursor",
                                sqlite_v8_transaction_query_cursor_callback) ||
        !sqlite_v8_set_function(isolate, context, sqlite, "transactionQueryRawCursor",
                                sqlite_v8_transaction_query_raw_cursor_callback) ||
        !sqlite_v8_set_function(isolate, context, sqlite, "transactionQueryOne",
                                sqlite_v8_transaction_query_one_callback) ||
        !data->Set(context, sqlite_key, sqlite).FromMaybe(false))
    {
        return false;
    }

    return true;
}

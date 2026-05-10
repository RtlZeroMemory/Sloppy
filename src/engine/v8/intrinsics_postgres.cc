/*
 * src/engine/v8/intrinsics_postgres.cc
 *
 * Installs the V8-internal PostgreSQL bridge under __sloppy.data.postgres.
 * PostgreSQL I/O is driven by libpq's nonblocking state machine and Slop's async
 * readiness watch; no blocking worker is used and JS receives only resource IDs.
 */
#include "engine_v8_internal.h"
#include "intrinsics_db_bridge.h"
#include "string_interop.h"

#include "sloppy/capability.h"
#include "sloppy/data_postgres.h"

#include <libpq-fe.h>

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr double pg_v8_max_safe_integer = 9007199254740991.0;
constexpr double pg_v8_min_safe_integer = -9007199254740991.0;

enum
{
    SL_PG_OID_BOOL = 16,
    SL_PG_OID_BYTEA = 17,
    SL_PG_OID_INT8 = 20,
    SL_PG_OID_INT2 = 21,
    SL_PG_OID_INT4 = 23,
    SL_PG_OID_TEXT = 25,
    SL_PG_OID_FLOAT4 = 700,
    SL_PG_OID_FLOAT8 = 701,
    SL_PG_OID_VARCHAR = 1043,
    SL_PG_OID_DATE = 1082,
    SL_PG_OID_TIME = 1083,
    SL_PG_OID_TIMESTAMP = 1114,
    SL_PG_OID_TIMESTAMPTZ = 1184,
    SL_PG_OID_NUMERIC = 1700,
    SL_PG_OID_UUID = 2950,
    SL_PG_OID_JSON = 114,
    SL_PG_OID_JSONB = 3802
};

bool pg_v8_is_digit(char value)
{
    return value >= '0' && value <= '9';
}

bool pg_v8_parse_date_part(const char* value, int length, std::string* out)
{
    if (out == nullptr || value == nullptr || length < 10 || !pg_v8_is_digit(value[0]) ||
        !pg_v8_is_digit(value[1]) || !pg_v8_is_digit(value[2]) || !pg_v8_is_digit(value[3]) ||
        value[4] != '-' || !pg_v8_is_digit(value[5]) || !pg_v8_is_digit(value[6]) ||
        value[7] != '-' || !pg_v8_is_digit(value[8]) || !pg_v8_is_digit(value[9]))
    {
        return false;
    }
    out->assign(value, 10U);
    return true;
}

bool pg_v8_normalize_date_text(const char* value, int length, std::string* out)
{
    return length == 10 && pg_v8_parse_date_part(value, length, out);
}

bool pg_v8_parse_time_part(const char* value, int length, int* consumed, std::string* out)
{
    int index = 8;
    if (out == nullptr || consumed == nullptr || value == nullptr || length < 8 ||
        !pg_v8_is_digit(value[0]) || !pg_v8_is_digit(value[1]) || value[2] != ':' ||
        !pg_v8_is_digit(value[3]) || !pg_v8_is_digit(value[4]) || value[5] != ':' ||
        !pg_v8_is_digit(value[6]) || !pg_v8_is_digit(value[7]))
    {
        return false;
    }
    if (index < length && value[index] == '.') {
        index += 1;
        const int fraction_start = index;
        while (index < length && pg_v8_is_digit(value[index])) {
            index += 1;
        }
        if (index == fraction_start) {
            return false;
        }
    }
    out->assign(value, static_cast<size_t>(index));
    *consumed = index;
    return true;
}

bool pg_v8_normalize_time_text(const char* value, int length, std::string* out)
{
    int consumed = 0;
    return pg_v8_parse_time_part(value, length, &consumed, out) && consumed == length;
}

bool pg_v8_normalize_timestamp_text(const char* value, int length, std::string* out,
                                    int* consumed = nullptr)
{
    std::string date;
    std::string time;
    int time_consumed = 0;
    if (out == nullptr || value == nullptr || length < 19 ||
        !pg_v8_parse_date_part(value, length, &date) || (value[10] != ' ' && value[10] != 'T') ||
        !pg_v8_parse_time_part(value + 11, length - 11, &time_consumed, &time))
    {
        return false;
    }
    const int total_consumed = 11 + time_consumed;
    if (consumed == nullptr && total_consumed != length) {
        return false;
    }
    *out = date + "T" + time;
    if (consumed != nullptr) {
        *consumed = total_consumed;
    }
    return true;
}

bool pg_v8_normalize_timestamptz_text(const char* value, int length, std::string* kind,
                                      std::string* out)
{
    std::string timestamp;
    int consumed = 0;
    if (kind == nullptr || out == nullptr ||
        !pg_v8_normalize_timestamp_text(value, length, &timestamp, &consumed))
    {
        return false;
    }
    if (consumed == length - 1 && value[consumed] == 'Z') {
        *kind = "instant";
        *out = timestamp + "Z";
        return true;
    }
    if (consumed >= length || (value[consumed] != '+' && value[consumed] != '-')) {
        return false;
    }
    const char sign = value[consumed];
    const int offset_start = consumed + 1;
    if (offset_start + 2 > length || !pg_v8_is_digit(value[offset_start]) ||
        !pg_v8_is_digit(value[offset_start + 1]))
    {
        return false;
    }
    char hour0 = value[offset_start];
    char hour1 = value[offset_start + 1];
    char minute0 = '0';
    char minute1 = '0';
    int offset_end = offset_start + 2;
    if (offset_end < length) {
        if (value[offset_end] != ':' || offset_end + 3 != length ||
            !pg_v8_is_digit(value[offset_end + 1]) || !pg_v8_is_digit(value[offset_end + 2]))
        {
            return false;
        }
        minute0 = value[offset_end + 1];
        minute1 = value[offset_end + 2];
        offset_end += 3;
    }
    if (offset_end != length) {
        return false;
    }
    if (hour0 == '0' && hour1 == '0' && minute0 == '0' && minute1 == '0') {
        *kind = "instant";
        *out = timestamp + "Z";
        return true;
    }
    *kind = "offsetDateTime";
    *out = timestamp;
    out->push_back(sign);
    out->push_back(hour0);
    out->push_back(hour1);
    out->push_back(':');
    out->push_back(minute0);
    out->push_back(minute1);
    return true;
}

enum class PgV8Operation
{
    Exec,
    Query,
    QueryOne,
    Begin,
    Commit,
    Rollback,
    TransactionExec,
    TransactionQuery,
    TransactionQueryOne,
};

enum class PgV8ConnectionState
{
    Empty,
    Connecting,
    Busy,
    Idle,
    Closed,
};

enum class PgV8RequestState
{
    Connecting,
    Sending,
    Reading,
    Terminal,
};

struct PgV8ConnectionResource;
struct PgV8Connection;
struct PgV8Request;

void pg_v8_clear_result(PgV8Request* request);

struct PgV8Param
{
    std::string text;
    std::vector<unsigned char> bytes;
    Oid type = 0;
    const char* value = nullptr;
    int length = 0;
    int format = 0;
    bool is_null = false;
};

struct PgV8Request
{
    SlV8Engine* backend = nullptr;
    PgV8ConnectionResource* resource = nullptr;
    PgV8Connection* connection = nullptr;
    PgV8Operation operation = PgV8Operation::Query;
    PgV8RequestState state = PgV8RequestState::Connecting;
    v8::Global<v8::Promise::Resolver> resolver;
    std::string sql;
    std::vector<PgV8Param> params;
    std::vector<Oid> param_types;
    std::vector<const char*> param_values;
    std::vector<int> param_lengths;
    std::vector<int> param_formats;
    PGresult* result = nullptr;
    std::string error;
    bool release_after = true;
    bool transaction_terminal = false;
};

struct PgV8Connection
{
    PGconn* conn = nullptr;
    SlAsyncIoWatch* watch = nullptr;
    PgV8ConnectionState state = PgV8ConnectionState::Empty;
    std::shared_ptr<PgV8Request> request;
    bool transaction_pinned = false;
};

struct PgV8ConnectionResource
{
    std::string connection_string;
    std::string capability;
    std::string provider_token;
    SlPostgresAccess access = SL_POSTGRES_ACCESS_READWRITE;
    size_t max_connections = SL_POSTGRES_DEFAULT_MAX_CONNECTIONS;
    std::vector<PgV8Connection> connections;
    bool closing = false;
    bool transaction_active = false;
    size_t transaction_index = 0U;
};

SlStatus pg_v8_to_local_string(v8::Isolate* isolate, SlStr str, v8::Local<v8::String>* out)
{
    return sl_v8_db_to_local_string(isolate, str, out);
}

void pg_v8_throw_type_error(v8::Isolate* isolate, const char* message)
{
    sl_v8_db_throw_type_error(isolate, message, "Sloppy PostgreSQL type error");
}

void pg_v8_throw_error(v8::Isolate* isolate, const std::string& message)
{
    sl_v8_db_throw_error(isolate, message, "Sloppy PostgreSQL operation failed");
}

bool pg_v8_value_to_std_string(v8::Isolate* isolate, v8::Local<v8::Value> value, std::string* out)
{
    return sl_v8_db_value_to_std_string(isolate, value, out);
}

bool pg_v8_local_string_from_text(v8::Isolate* isolate, const char* value, int length,
                                  v8::Local<v8::String>* out)
{
    if (value == nullptr || length < 0) {
        return false;
    }
    return sl_status_is_ok(
        pg_v8_to_local_string(isolate, sl_str_from_parts(value, static_cast<size_t>(length)), out));
}

bool pg_v8_make_typed_string_value(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                   const char* kind, const char* value, int length,
                                   v8::Local<v8::Value>* out)
{
    if (out == nullptr || kind == nullptr || value == nullptr || length < 0) {
        return false;
    }
    return sl_v8_db_make_typed_string_value(
        isolate, context, kind, sl_str_from_parts(value, static_cast<size_t>(length)), out);
}

bool pg_v8_parse_json_value(v8::Isolate* isolate, v8::Local<v8::Context> context, const char* value,
                            int length, v8::Local<v8::Value>* out)
{
    v8::Local<v8::String> text;
    if (out == nullptr || !pg_v8_local_string_from_text(isolate, value, length, &text)) {
        return false;
    }
    return v8::JSON::Parse(context, text).ToLocal(out);
}

bool pg_v8_get_object_string_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                      v8::Local<v8::Object> object, const char* key,
                                      std::string* out)
{
    return sl_v8_db_get_object_string_property(isolate, context, object, key, out);
}

bool pg_v8_get_object_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
                               v8::Local<v8::Object> object, const char* key,
                               v8::Local<v8::Value>* out)
{
    return sl_v8_db_get_object_property(isolate, context, object, key, out);
}

bool pg_v8_is_db_value(v8::Isolate* isolate, v8::Local<v8::Context> context,
                       v8::Local<v8::Value> value)
{
    return sl_v8_db_is_value_wrapper(isolate, context, value);
}

bool pg_v8_get_optional_object_string(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                      v8::Local<v8::Object> object, const char* key,
                                      std::string* out, bool* present)
{
    return sl_v8_db_get_optional_object_string(isolate, context, object, key, out, present);
}

bool pg_v8_get_resource_id(v8::Isolate* isolate, v8::Local<v8::Context> context,
                           v8::Local<v8::Value> value, SlResourceId* out)
{
    return sl_v8_db_get_resource_id(isolate, context, value, out);
}

bool pg_v8_make_resource_handle(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                SlResourceId id, v8::Local<v8::Object>* out)
{
    return sl_v8_db_make_resource_handle(isolate, context, id, "postgres.connection", out);
}

bool pg_v8_plan_provider_matches(const SlPlanDataProvider& provider, SlStr token)
{
    if (sl_str_equal(provider.token, token)) {
        return true;
    }
    if (!sl_str_is_empty(provider.service) && sl_str_equal(provider.service, token)) {
        return true;
    }
    return !sl_str_is_empty(provider.capability) && sl_str_equal(provider.capability, token);
}

const SlPlanDataProvider* pg_v8_find_provider(const SlV8Engine* backend, SlStr token)
{
    if (backend == nullptr || backend->plan == nullptr || sl_str_is_empty(token) ||
        (backend->plan->data_provider_count > 0U && backend->plan->data_providers == nullptr))
    {
        return nullptr;
    }
    for (size_t index = 0U; index < backend->plan->data_provider_count; index += 1U) {
        const SlPlanDataProvider& provider = backend->plan->data_providers[index];
        if (pg_v8_plan_provider_matches(provider, token)) {
            return &provider;
        }
    }
    return nullptr;
}

SlCapabilityOperation pg_v8_open_capability_operation(SlPostgresAccess access)
{
    return access == SL_POSTGRES_ACCESS_READ ? SL_CAPABILITY_OPERATION_READ
                                             : SL_CAPABILITY_OPERATION_READWRITE;
}

SlCapabilityOperation pg_v8_request_capability(PgV8Operation operation)
{
    switch (operation) {
    case PgV8Operation::Query:
    case PgV8Operation::QueryOne:
    case PgV8Operation::TransactionQuery:
    case PgV8Operation::TransactionQueryOne:
        return SL_CAPABILITY_OPERATION_READ;
    default:
        return SL_CAPABILITY_OPERATION_WRITE;
    }
}

bool pg_v8_access_allows(SlPostgresAccess access, SlCapabilityOperation operation)
{
    if (operation == SL_CAPABILITY_OPERATION_READ) {
        return access == SL_POSTGRES_ACCESS_READ || access == SL_POSTGRES_ACCESS_READWRITE;
    }
    return access == SL_POSTGRES_ACCESS_READWRITE;
}

bool pg_v8_check_capability(v8::Isolate* isolate, SlV8Engine* backend, SlArena* arena,
                            const PgV8ConnectionResource* resource, SlCapabilityOperation operation)
{
    SlDiag diag = {};
    SlStatus status;

    if (backend == nullptr || backend->capabilities == nullptr) {
        pg_v8_throw_error(isolate, "postgres capability registry is unavailable");
        return false;
    }
    if (resource == nullptr || arena == nullptr || resource->capability.empty()) {
        pg_v8_throw_error(isolate, "postgres resource is missing capability metadata");
        return false;
    }
    if (!pg_v8_access_allows(resource->access, operation)) {
        pg_v8_throw_error(isolate, "capability access denied: postgres handle access is read-only");
        return false;
    }

    status = sl_capability_check_database_provider(
        backend->capabilities, arena,
        sl_str_from_parts(resource->capability.data(), resource->capability.size()), operation,
        sl_str_from_cstr("postgres"), &diag);
    if (!sl_status_is_ok(status)) {
        std::string message = "postgres capability check failed";
        if (diag.message.ptr != nullptr && diag.message.length != 0U) {
            message.assign(diag.message.ptr, diag.message.length);
        }
        pg_v8_throw_error(isolate, message);
        return false;
    }
    return true;
}

PgV8ConnectionResource* pg_v8_lookup_connection(v8::Isolate* isolate,
                                                v8::Local<v8::Context> context, SlV8Engine* backend,
                                                v8::Local<v8::Value> handle_value)
{
    SlResourceId id = {};
    SlDiag diag = {};
    void* ptr = nullptr;

    if (!pg_v8_get_resource_id(isolate, context, handle_value, &id)) {
        pg_v8_throw_type_error(isolate, "postgres resource handle must be an opaque Sloppy handle");
        return nullptr;
    }
    SlStatus status = sl_resource_table_get(&backend->resources, id,
                                            SL_RESOURCE_KIND_POSTGRES_CONNECTION, &ptr, &diag);
    if (!sl_status_is_ok(status)) {
        pg_v8_throw_error(isolate, "postgres resource handle is invalid");
        return nullptr;
    }
    return static_cast<PgV8ConnectionResource*>(ptr);
}

void pg_v8_close_connection(PgV8Connection& connection)
{
    if (connection.watch != nullptr) {
        sl_async_io_watch_stop(connection.watch);
        connection.watch = nullptr;
    }
    if (connection.conn != nullptr) {
        PQfinish(connection.conn);
        connection.conn = nullptr;
    }
    connection.request.reset();
    connection.transaction_pinned = false;
    connection.state = PgV8ConnectionState::Closed;
}

void pg_v8_connection_cleanup(void* ptr, void* user)
{
    (void)user;
    PgV8ConnectionResource* resource = static_cast<PgV8ConnectionResource*>(ptr);
    if (resource == nullptr) {
        return;
    }
    resource->closing = true;
    for (PgV8Connection& connection : resource->connections) {
        pg_v8_close_connection(connection);
    }
    delete resource;
}

bool pg_v8_result_status_ok(PgV8Operation operation, ExecStatusType status)
{
    switch (operation) {
    case PgV8Operation::Exec:
    case PgV8Operation::Begin:
    case PgV8Operation::Commit:
    case PgV8Operation::Rollback:
    case PgV8Operation::TransactionExec:
        return status == PGRES_COMMAND_OK;
    case PgV8Operation::Query:
    case PgV8Operation::QueryOne:
    case PgV8Operation::TransactionQuery:
    case PgV8Operation::TransactionQueryOne:
        return status == PGRES_TUPLES_OK;
    default:
        return false;
    }
}

bool pg_v8_set_cell(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> row,
                    PGresult* result, int row_index, int column_index)
{
    const char* name = PQfname(result, column_index);
    const Oid oid = PQftype(result, column_index);
    v8::Local<v8::String> key;
    v8::Local<v8::Value> js_value;

    if (name == nullptr ||
        !sl_status_is_ok(pg_v8_to_local_string(isolate, sl_str_from_cstr(name), &key)))
    {
        return false;
    }
    if (PQgetisnull(result, row_index, column_index) != 0) {
        js_value = v8::Null(isolate);
        return row->Set(context, key, js_value).FromMaybe(false);
    }

    const char* value = PQgetvalue(result, row_index, column_index);
    const int length = PQgetlength(result, row_index, column_index);
    if (oid == SL_PG_OID_BOOL && length == 1) {
        js_value = v8::Boolean::New(isolate, value[0] == 't');
    }
    else if (oid == SL_PG_OID_INT2 || oid == SL_PG_OID_INT4) {
        char* end = nullptr;
        errno = 0;
        long parsed = std::strtol(value, &end, 10);
        if (errno != 0 || end != value + length) {
            return false;
        }
        js_value = v8::Number::New(isolate, static_cast<double>(parsed));
    }
    else if (oid == SL_PG_OID_INT8) {
        char* end = nullptr;
        errno = 0;
        long long parsed = std::strtoll(value, &end, 10);
        if (errno != 0 || end != value + length) {
            return false;
        }
        js_value = v8::BigInt::New(isolate, static_cast<int64_t>(parsed));
    }
    else if (oid == SL_PG_OID_FLOAT4 || oid == SL_PG_OID_FLOAT8) {
        js_value = v8::Number::New(isolate, std::strtod(value, nullptr));
    }
    else if (oid == SL_PG_OID_NUMERIC) {
        if (!pg_v8_make_typed_string_value(isolate, context, "decimal", value, length, &js_value)) {
            return false;
        }
    }
    else if (oid == SL_PG_OID_BYTEA) {
        size_t unescaped_length = 0U;
        unsigned char* unescaped =
            PQunescapeBytea(reinterpret_cast<const unsigned char*>(value), &unescaped_length);
        if (unescaped == nullptr) {
            return false;
        }
        std::unique_ptr<v8::BackingStore> backing =
            v8::ArrayBuffer::NewBackingStore(isolate, unescaped_length);
        if (!backing) {
            PQfreemem(unescaped);
            return false;
        }
        unsigned char* destination = static_cast<unsigned char*>(backing->Data());
        for (size_t index = 0U; index < unescaped_length; index += 1U) {
            destination[index] = unescaped[index];
        }
        PQfreemem(unescaped);
        v8::Local<v8::ArrayBuffer> buffer = v8::ArrayBuffer::New(isolate, std::move(backing));
        js_value = v8::Uint8Array::New(buffer, 0U, unescaped_length);
    }
    else if (oid == SL_PG_OID_UUID) {
        if (!pg_v8_make_typed_string_value(isolate, context, "uuid", value, length, &js_value)) {
            return false;
        }
    }
    else if (oid == SL_PG_OID_JSON || oid == SL_PG_OID_JSONB) {
        if (!pg_v8_parse_json_value(isolate, context, value, length, &js_value)) {
            return false;
        }
    }
    else if (oid == SL_PG_OID_DATE) {
        std::string normalized;
        if (!pg_v8_normalize_date_text(value, length, &normalized) ||
            !pg_v8_make_typed_string_value(isolate, context, "date", normalized.data(),
                                           static_cast<int>(normalized.size()), &js_value))
        {
            return false;
        }
    }
    else if (oid == SL_PG_OID_TIME) {
        std::string normalized;
        if (!pg_v8_normalize_time_text(value, length, &normalized) ||
            !pg_v8_make_typed_string_value(isolate, context, "time", normalized.data(),
                                           static_cast<int>(normalized.size()), &js_value))
        {
            return false;
        }
    }
    else if (oid == SL_PG_OID_TIMESTAMP) {
        std::string normalized;
        if (!pg_v8_normalize_timestamp_text(value, length, &normalized) ||
            !pg_v8_make_typed_string_value(isolate, context, "localDateTime", normalized.data(),
                                           static_cast<int>(normalized.size()), &js_value))
        {
            return false;
        }
    }
    else if (oid == SL_PG_OID_TIMESTAMPTZ) {
        std::string kind;
        std::string normalized;
        if (!pg_v8_normalize_timestamptz_text(value, length, &kind, &normalized) ||
            !pg_v8_make_typed_string_value(isolate, context, kind.c_str(), normalized.data(),
                                           static_cast<int>(normalized.size()), &js_value))
        {
            return false;
        }
    }
    else {
        v8::Local<v8::String> text;
        if (!sl_status_is_ok(pg_v8_to_local_string(
                isolate, sl_str_from_parts(value, static_cast<size_t>(length)), &text)))
        {
            return false;
        }
        js_value = text;
    }
    return row->Set(context, key, js_value).FromMaybe(false);
}

bool pg_v8_result_to_array(v8::Isolate* isolate, v8::Local<v8::Context> context, PGresult* result,
                           v8::Local<v8::Array>* out)
{
    const int rows_count = PQntuples(result);
    const int columns = PQnfields(result);
    v8::Local<v8::Array> rows = v8::Array::New(isolate, rows_count);

    if (out == nullptr || rows_count < 0 || columns < 0) {
        return false;
    }
    for (int row_index = 0; row_index < rows_count; row_index += 1) {
        v8::Local<v8::Object> row = v8::Object::New(isolate);
        for (int column_index = 0; column_index < columns; column_index += 1) {
            if (!pg_v8_set_cell(isolate, context, row, result, row_index, column_index)) {
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

bool pg_v8_result_to_one(v8::Isolate* isolate, v8::Local<v8::Context> context, PGresult* result,
                         v8::Local<v8::Value>* out)
{
    if (out == nullptr) {
        return false;
    }
    if (PQntuples(result) == 0) {
        *out = v8::Null(isolate);
        return true;
    }
    v8::Local<v8::Object> row = v8::Object::New(isolate);
    for (int column_index = 0; column_index < PQnfields(result); column_index += 1) {
        if (!pg_v8_set_cell(isolate, context, row, result, 0, column_index)) {
            return false;
        }
    }
    *out = row;
    return true;
}

bool pg_v8_exec_result(v8::Isolate* isolate, v8::Local<v8::Context> context, PGresult* result,
                       v8::Local<v8::Value>* out)
{
    v8::Local<v8::Object> object = v8::Object::New(isolate);
    v8::Local<v8::String> affected_key;
    v8::Local<v8::String> known_key;
    const char* tuples = PQcmdTuples(result);
    int64_t affected = 0;
    bool known = false;

    if (tuples != nullptr && tuples[0] != '\0') {
        affected = static_cast<int64_t>(std::strtoll(tuples, nullptr, 10));
        known = true;
    }
    if (out == nullptr ||
        !sl_status_is_ok(
            pg_v8_to_local_string(isolate, sl_str_from_cstr("affectedRows"), &affected_key)) ||
        !sl_status_is_ok(
            pg_v8_to_local_string(isolate, sl_str_from_cstr("affectedRowsKnown"), &known_key)) ||
        !object->Set(context, affected_key, v8::Number::New(isolate, static_cast<double>(affected)))
             .FromMaybe(false) ||
        !object->Set(context, known_key, v8::Boolean::New(isolate, known)).FromMaybe(false))
    {
        return false;
    }
    *out = object;
    return true;
}

void pg_v8_finish_request(const std::shared_ptr<PgV8Request>& request, bool ok)
{
    PgV8Connection* connection = request == nullptr ? nullptr : request->connection;
    if (connection == nullptr || request->resource == nullptr) {
        return;
    }
    if (!ok) {
        if (request->operation == PgV8Operation::Begin || request->transaction_terminal) {
            request->resource->transaction_active = false;
        }
        pg_v8_close_connection(*connection);
    }
    else if (request->transaction_terminal) {
        connection->transaction_pinned = false;
        request->resource->transaction_active = false;
        connection->state = PgV8ConnectionState::Idle;
    }
    else {
        if (request->operation == PgV8Operation::Begin) {
            request->resource->transaction_active = true;
            connection->transaction_pinned = true;
        }
        connection->state = PgV8ConnectionState::Idle;
    }
    connection->request.reset();
}

void pg_v8_settle_request(const std::shared_ptr<PgV8Request>& request, bool ok)
{
    SlV8Engine* backend = request == nullptr ? nullptr : request->backend;
    v8::Isolate* isolate = backend == nullptr ? nullptr : backend->isolate;
    if (backend == nullptr || isolate == nullptr) {
        return;
    }

    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = backend->context.Get(isolate);
    v8::Context::Scope context_scope(context);
    v8::Local<v8::Promise::Resolver> resolver = request->resolver.Get(isolate);
    if (!ok) {
        (void)sl_v8_db_reject_promise(isolate, context, resolver, request->error,
                                      "postgres operation failed");
        pg_v8_finish_request(request, false);
        return;
    }

    v8::Local<v8::Value> value;
    bool converted = false;
    switch (request->operation) {
    case PgV8Operation::Exec:
    case PgV8Operation::TransactionExec:
        converted = pg_v8_exec_result(isolate, context, request->result, &value);
        break;
    case PgV8Operation::Query:
    case PgV8Operation::TransactionQuery: {
        v8::Local<v8::Array> rows;
        converted = pg_v8_result_to_array(isolate, context, request->result, &rows);
        value = rows;
        break;
    }
    case PgV8Operation::QueryOne:
    case PgV8Operation::TransactionQueryOne:
        converted = pg_v8_result_to_one(isolate, context, request->result, &value);
        break;
    default:
        value = v8::Undefined(isolate);
        converted = true;
        break;
    }

    if (!converted) {
        value = v8::Exception::Error(
            v8::String::NewFromUtf8Literal(isolate, "postgres result conversion failed"));
        (void)resolver->Reject(context, value);
        pg_v8_clear_result(request.get());
        pg_v8_finish_request(request, false);
        return;
    }
    (void)sl_v8_db_resolve_promise(context, resolver, value);
    pg_v8_finish_request(request, true);
}

void pg_v8_clear_result(PgV8Request* request)
{
    if (request != nullptr && request->result != nullptr) {
        PQclear(request->result);
        request->result = nullptr;
    }
}

void pg_v8_fail_request(const std::shared_ptr<PgV8Request>& request, const char* fallback)
{
    if (request == nullptr) {
        return;
    }
    const char* message = nullptr;
    if (request->connection != nullptr && request->connection->conn != nullptr) {
        message = PQerrorMessage(request->connection->conn);
    }
    request->error = message != nullptr && message[0] != '\0' ? message : fallback;
    pg_v8_clear_result(request.get());
    pg_v8_settle_request(request, false);
}

unsigned pg_v8_events_for_polling(PostgresPollingStatusType status)
{
    if (status == PGRES_POLLING_READING) {
        return SL_ASYNC_IO_EVENT_READABLE;
    }
    if (status == PGRES_POLLING_WRITING) {
        return SL_ASYNC_IO_EVENT_WRITABLE;
    }
    return 0U;
}

void pg_v8_pump_connection(PgV8Connection* connection);

void pg_v8_watch_callback(SlAsyncLoop* loop, SlAsyncIoWatch* watch, unsigned events,
                          SlStatus status, void* user)
{
    (void)loop;
    (void)watch;
    (void)events;
    PgV8Connection* connection = static_cast<PgV8Connection*>(user);
    if (connection == nullptr) {
        return;
    }
    if (!sl_status_is_ok(status)) {
        std::shared_ptr<PgV8Request> request = connection->request;
        pg_v8_fail_request(request, "postgres socket readiness poll failed");
        return;
    }
    pg_v8_pump_connection(connection);
}

bool pg_v8_start_watch(PgV8Request* request, unsigned events)
{
    PgV8Connection* connection = request == nullptr ? nullptr : request->connection;
    if (connection == nullptr || connection->conn == nullptr) {
        return false;
    }
    if (connection->watch != nullptr) {
        return sl_status_is_ok(sl_async_io_watch_update(connection->watch, events));
    }
    SlStatus status = sl_async_io_watch_start(request->backend->async_loop, request->backend->arena,
                                              PQsocket(connection->conn), events,
                                              pg_v8_watch_callback, connection, &connection->watch);
    return sl_status_is_ok(status);
}

bool pg_v8_request_prepare_wire(PgV8Request* request)
{
    if (request == nullptr) {
        return false;
    }
    request->param_types.clear();
    request->param_values.clear();
    request->param_lengths.clear();
    request->param_formats.clear();
    request->param_types.reserve(request->params.size());
    request->param_values.reserve(request->params.size());
    request->param_lengths.reserve(request->params.size());
    request->param_formats.reserve(request->params.size());

    for (PgV8Param& param : request->params) {
        request->param_types.push_back(param.type);
        if (param.is_null) {
            request->param_values.push_back(nullptr);
            request->param_lengths.push_back(0);
            request->param_formats.push_back(0);
            continue;
        }
        if (param.format == 1) {
            param.value = reinterpret_cast<const char*>(param.bytes.data());
            param.length = static_cast<int>(param.bytes.size());
        }
        else {
            param.value = param.text.c_str();
            param.length = static_cast<int>(param.text.size());
        }
        request->param_values.push_back(param.value);
        request->param_lengths.push_back(param.length);
        request->param_formats.push_back(param.format);
    }
    return true;
}

bool pg_v8_send_query(PgV8Request* request)
{
    if (request == nullptr || request->connection == nullptr ||
        request->connection->conn == nullptr)
    {
        return false;
    }
    if (!pg_v8_request_prepare_wire(request)) {
        return false;
    }
    const int param_count = static_cast<int>(request->params.size());
    int sent = PQsendQueryParams(request->connection->conn, request->sql.c_str(), param_count,
                                 param_count == 0 ? nullptr : request->param_types.data(),
                                 param_count == 0 ? nullptr : request->param_values.data(),
                                 param_count == 0 ? nullptr : request->param_lengths.data(),
                                 param_count == 0 ? nullptr : request->param_formats.data(), 0);
    return sent == 1;
}

void pg_v8_complete_read(PgV8Connection* connection)
{
    std::shared_ptr<PgV8Request> request = connection->request;
    PGresult* result = nullptr;
    bool saw_result = false;

    for (;;) {
        result = PQgetResult(connection->conn);
        if (result == nullptr) {
            break;
        }
        saw_result = true;
        ExecStatusType status = PQresultStatus(result);
        if (!pg_v8_result_status_ok(request->operation, status)) {
            request->error = PQresultErrorMessage(result);
            PQclear(result);
            pg_v8_settle_request(request, false);
            return;
        }
        pg_v8_clear_result(request.get());
        request->result = result;
    }
    if (!saw_result && request->result == nullptr) {
        request->error = "postgres operation completed without a result";
        pg_v8_settle_request(request, false);
        return;
    }

    pg_v8_settle_request(request, true);
    pg_v8_clear_result(request.get());
}

void pg_v8_pump_connection(PgV8Connection* connection)
{
    if (connection == nullptr || connection->request == nullptr || connection->conn == nullptr) {
        return;
    }
    std::shared_ptr<PgV8Request> request = connection->request;

    if (request->state == PgV8RequestState::Connecting) {
        PostgresPollingStatusType poll_status = PQconnectPoll(connection->conn);
        if (poll_status == PGRES_POLLING_FAILED) {
            pg_v8_fail_request(request, "postgres connection failed");
            return;
        }
        if (poll_status == PGRES_POLLING_READING || poll_status == PGRES_POLLING_WRITING) {
            if (!pg_v8_start_watch(request.get(), pg_v8_events_for_polling(poll_status))) {
                pg_v8_fail_request(request, "postgres socket readiness watch failed");
            }
            return;
        }
        request->state = PgV8RequestState::Sending;
        connection->state = PgV8ConnectionState::Busy;
        if (!pg_v8_send_query(request.get())) {
            pg_v8_fail_request(request, "postgres query submission failed");
            return;
        }
    }

    if (request->state == PgV8RequestState::Sending) {
        int flush_status = PQflush(connection->conn);
        if (flush_status < 0) {
            pg_v8_fail_request(request, "postgres query flush failed");
            return;
        }
        if (flush_status == 1) {
            if (!pg_v8_start_watch(request.get(), SL_ASYNC_IO_EVENT_WRITABLE)) {
                pg_v8_fail_request(request, "postgres socket readiness watch failed");
            }
            return;
        }
        request->state = PgV8RequestState::Reading;
    }

    if (request->state == PgV8RequestState::Reading) {
        if (PQconsumeInput(connection->conn) != 1) {
            pg_v8_fail_request(request, "postgres input consumption failed");
            return;
        }
        if (PQisBusy(connection->conn) != 0) {
            if (!pg_v8_start_watch(request.get(), SL_ASYNC_IO_EVENT_READABLE)) {
                pg_v8_fail_request(request, "postgres socket readiness watch failed");
            }
            return;
        }
        pg_v8_complete_read(connection);
    }
}

bool pg_v8_attach_request_to_connection(PgV8ConnectionResource* resource,
                                        PgV8Connection* connection,
                                        const std::shared_ptr<PgV8Request>& request)
{
    if (resource == nullptr || connection == nullptr || request == nullptr) {
        return false;
    }
    connection->request = request;
    request->connection = connection;
    connection->state = PgV8ConnectionState::Busy;

    if (connection->conn == nullptr || connection->state == PgV8ConnectionState::Empty ||
        connection->state == PgV8ConnectionState::Closed)
    {
        connection->conn = PQconnectStart(resource->connection_string.c_str());
        if (connection->conn == nullptr || PQsetnonblocking(connection->conn, 1) != 0) {
            request->error = "postgres connection allocation failed";
            return false;
        }
        connection->state = PgV8ConnectionState::Connecting;
        request->state = PgV8RequestState::Connecting;
    }
    else {
        request->state = PgV8RequestState::Sending;
        if (!pg_v8_send_query(request.get())) {
            request->error = "postgres query submission failed";
            return false;
        }
    }
    pg_v8_pump_connection(connection);
    return true;
}

PgV8Connection* pg_v8_acquire_connection(PgV8ConnectionResource* resource,
                                         const std::shared_ptr<PgV8Request>& request)
{
    if (resource == nullptr || request == nullptr) {
        return nullptr;
    }
    if (request->operation == PgV8Operation::TransactionExec ||
        request->operation == PgV8Operation::TransactionQuery ||
        request->operation == PgV8Operation::TransactionQueryOne ||
        request->operation == PgV8Operation::Commit ||
        request->operation == PgV8Operation::Rollback)
    {
        if (!resource->transaction_active ||
            resource->transaction_index >= resource->connections.size())
        {
            request->error = "postgres transaction is not active";
            return nullptr;
        }
        if (resource->connections[resource->transaction_index].request != nullptr) {
            request->error = "postgres transaction connection is busy";
            return nullptr;
        }
        return &resource->connections[resource->transaction_index];
    }

    for (PgV8Connection& connection : resource->connections) {
        if (connection.state == PgV8ConnectionState::Idle && connection.request == nullptr &&
            !connection.transaction_pinned)
        {
            return &connection;
        }
    }
    for (PgV8Connection& connection : resource->connections) {
        if (connection.state == PgV8ConnectionState::Empty ||
            connection.state == PgV8ConnectionState::Closed)
        {
            return &connection;
        }
    }
    request->error = "postgres provider pool is exhausted";
    return nullptr;
}

bool pg_v8_submit_request(const std::shared_ptr<PgV8Request>& request)
{
    PgV8ConnectionResource* resource = request == nullptr ? nullptr : request->resource;
    PgV8Connection* connection = pg_v8_acquire_connection(resource, request);
    if (connection == nullptr) {
        return false;
    }
    if (request->operation == PgV8Operation::Begin) {
        request->sql = "BEGIN";
        request->release_after = false;
        for (size_t index = 0U; index < resource->connections.size(); index += 1U) {
            if (&resource->connections[index] == connection) {
                resource->transaction_index = index;
                break;
            }
        }
        resource->transaction_active = true;
    }
    else if (request->operation == PgV8Operation::Commit) {
        request->sql = "COMMIT";
        request->transaction_terminal = true;
    }
    else if (request->operation == PgV8Operation::Rollback) {
        request->sql = "ROLLBACK";
        request->transaction_terminal = true;
    }
    if (!pg_v8_attach_request_to_connection(resource, connection, request)) {
        return false;
    }
    return true;
}

std::string pg_v8_array_literal(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                v8::Local<v8::Array> array)
{
    std::string output = "{";
    for (uint32_t index = 0U; index < array->Length(); index += 1U) {
        v8::Local<v8::Value> item;
        std::string text;
        if (!array->Get(context, index).ToLocal(&item)) {
            return "";
        }
        if (index != 0U) {
            output += ",";
        }
        if (item->IsUndefined()) {
            return "";
        }
        if (item->IsNull()) {
            output += "NULL";
            continue;
        }
        if (item->IsBoolean()) {
            output += item->BooleanValue(isolate) ? "true" : "false";
            continue;
        }
        if (item->IsNumber()) {
            double number_value = item.As<v8::Number>()->Value();
            if (!std::isfinite(number_value) ||
                (std::trunc(number_value) == number_value &&
                 (number_value < pg_v8_min_safe_integer || number_value > pg_v8_max_safe_integer)))
            {
                return "";
            }
            v8::String::Utf8Value number(isolate, item);
            if (*number == nullptr) {
                return "";
            }
            output += *number;
            continue;
        }
        if (!pg_v8_value_to_std_string(isolate, item, &text)) {
            return "";
        }
        output += "\"";
        for (char ch : text) {
            if (ch == '"' || ch == '\\') {
                output += "\\";
            }
            output += ch;
        }
        output += "\"";
    }
    output += "}";
    return output;
}

bool pg_v8_convert_db_value_param(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                  v8::Local<v8::Value> item, PgV8Param* param)
{
    std::string kind;
    std::string text;
    v8::Local<v8::Value> raw_value;
    v8::Local<v8::String> json_text;

    if (param == nullptr || !item->IsObject()) {
        return false;
    }
    v8::Local<v8::Object> object = item.As<v8::Object>();
    if (!pg_v8_get_object_string_property(isolate, context, object, "kind", &kind) ||
        !pg_v8_get_object_property(isolate, context, object, "value", &raw_value))
    {
        return false;
    }
    if (kind == "json") {
        if (!v8::JSON::Stringify(context, raw_value).ToLocal(&json_text) ||
            !sl_v8_std_string_from_value(isolate, json_text, &param->text))
        {
            pg_v8_throw_type_error(isolate, "postgres json parameters must be JSON-serializable");
            return false;
        }
        param->type = SL_PG_OID_JSONB;
        return true;
    }
    if (kind == "rawJson") {
        if (!raw_value->IsString() ||
            !sl_v8_std_string_from_value(isolate, raw_value, &param->text))
        {
            return false;
        }
        param->type = SL_PG_OID_JSONB;
        return true;
    }
    if (kind == "bytes") {
        if (!sl_v8_db_copy_uint8_array(raw_value, &param->bytes)) {
            pg_v8_throw_type_error(isolate, "postgres bytes value wrapper must hold Uint8Array");
            return false;
        }
        param->type = SL_PG_OID_BYTEA;
        param->format = 1;
        return true;
    }
    if (!raw_value->IsString() || !sl_v8_std_string_from_value(isolate, raw_value, &text)) {
        return false;
    }
    param->text = text;
    if (kind == "decimal") {
        param->type = SL_PG_OID_NUMERIC;
    }
    else if (kind == "uuid") {
        param->type = SL_PG_OID_UUID;
    }
    else if (kind == "date") {
        param->type = SL_PG_OID_DATE;
    }
    else if (kind == "time") {
        param->type = SL_PG_OID_TIME;
    }
    else if (kind == "localDateTime") {
        param->type = SL_PG_OID_TIMESTAMP;
    }
    else if (kind == "instant" || kind == "offsetDateTime") {
        param->type = SL_PG_OID_TIMESTAMPTZ;
    }
    else {
        pg_v8_throw_type_error(isolate, "postgres sql value wrapper kind is not supported");
        return false;
    }
    return true;
}

bool pg_v8_convert_params(v8::Isolate* isolate, v8::Local<v8::Context> context,
                          v8::Local<v8::Value> value, std::vector<PgV8Param>* out)
{
    if (out == nullptr) {
        return false;
    }
    out->clear();
    if (value->IsUndefined() || value->IsNull()) {
        return true;
    }
    if (!value->IsArray()) {
        pg_v8_throw_type_error(isolate, "postgres parameters must be an array when supplied");
        return false;
    }
    v8::Local<v8::Array> array = value.As<v8::Array>();
    if (array->Length() > SL_POSTGRES_MAX_PARAMS) {
        pg_v8_throw_type_error(isolate,
                               "postgres parameter array exceeds supported parameter count");
        return false;
    }
    out->reserve(array->Length());
    for (uint32_t index = 0U; index < array->Length(); index += 1U) {
        v8::Local<v8::Value> item;
        PgV8Param param = {};
        if (!array->Get(context, index).ToLocal(&item)) {
            return false;
        }
        if (item->IsUndefined()) {
            pg_v8_throw_type_error(isolate,
                                   "postgres undefined parameters are not SQL NULL; use null");
            return false;
        }
        if (item->IsNull()) {
            param.is_null = true;
        }
        else if (item->IsBoolean()) {
            param.text = item->BooleanValue(isolate) ? "true" : "false";
            param.type = SL_PG_OID_BOOL;
        }
        else if (item->IsNumber()) {
            double number = item.As<v8::Number>()->Value();
            if (!std::isfinite(number)) {
                pg_v8_throw_type_error(isolate, "postgres number parameters must be finite");
                return false;
            }
            if (std::trunc(number) == number &&
                (number < pg_v8_min_safe_integer || number > pg_v8_max_safe_integer))
            {
                pg_v8_throw_type_error(
                    isolate,
                    "postgres integer parameters outside JS safe integer range must use BigInt");
                return false;
            }
            if (std::trunc(number) == number) {
                param.type = SL_PG_OID_INT8;
            }
            else {
                param.type = SL_PG_OID_FLOAT8;
            }
            v8::String::Utf8Value text(isolate, item);
            if (*text == nullptr) {
                return false;
            }
            param.text = *text;
        }
        else if (item->IsBigInt()) {
            bool lossless = false;
            int64_t value64 = item.As<v8::BigInt>()->Int64Value(&lossless);
            if (!lossless) {
                pg_v8_throw_type_error(isolate, "postgres bigint parameters must fit int64");
                return false;
            }
            param.type = SL_PG_OID_INT8;
            param.text = std::to_string(value64);
        }
        else if (item->IsUint8Array()) {
            if (!sl_v8_db_copy_uint8_array(item, &param.bytes)) {
                return false;
            }
            param.type = SL_PG_OID_BYTEA;
            param.format = 1;
        }
        else if (pg_v8_is_db_value(isolate, context, item)) {
            if (!pg_v8_convert_db_value_param(isolate, context, item, &param)) {
                return false;
            }
        }
        else if (item->IsArray()) {
            param.text = pg_v8_array_literal(isolate, context, item.As<v8::Array>());
            if (param.text.empty()) {
                pg_v8_throw_type_error(isolate, "postgres array parameters support scalar values");
                return false;
            }
        }
        else if (item->IsString()) {
            if (!pg_v8_value_to_std_string(isolate, item, &param.text)) {
                return false;
            }
            param.type = SL_PG_OID_TEXT;
        }
        else {
            pg_v8_throw_type_error(isolate, "postgres parameters support null, boolean, number, "
                                            "bigint, string, bytes, sql value wrappers, and scalar "
                                            "arrays");
            return false;
        }
        out->push_back(std::move(param));
    }
    return true;
}

bool pg_v8_parse_open_options(v8::Isolate* isolate, v8::Local<v8::Context> context,
                              v8::Local<v8::Value> value, PgV8ConnectionResource* out)
{
    std::string access;
    bool connection_present = false;
    bool access_present = false;
    bool capability_present = false;
    bool provider_present = false;
    if (out == nullptr || !value->IsObject()) {
        return false;
    }
    v8::Local<v8::Object> object = value.As<v8::Object>();
    if (!pg_v8_get_optional_object_string(isolate, context, object, "connectionString",
                                          &out->connection_string, &connection_present) ||
        !pg_v8_get_optional_object_string(isolate, context, object, "access", &access,
                                          &access_present) ||
        !pg_v8_get_optional_object_string(isolate, context, object, "capability", &out->capability,
                                          &capability_present) ||
        !pg_v8_get_optional_object_string(isolate, context, object, "provider",
                                          &out->provider_token, &provider_present))
    {
        return false;
    }
    if (!connection_present && provider_present) {
        const SlPlanDataProvider* provider = pg_v8_find_provider(
            static_cast<SlV8Engine*>(isolate->GetData(0)),
            sl_str_from_parts(out->provider_token.data(), out->provider_token.size()));
        if (provider == nullptr || !sl_str_equal(provider->provider, sl_str_from_cstr("postgres")))
        {
            return false;
        }
        if (!sl_str_is_empty(provider->capability)) {
            out->capability.assign(provider->capability.ptr, provider->capability.length);
        }
        else {
            out->capability.assign(provider->token.ptr, provider->token.length);
        }
        out->provider_token.assign(provider->token.ptr, provider->token.length);
    }
    if (out->connection_string.empty()) {
        return false;
    }
    if (!capability_present && out->capability.empty()) {
        out->capability = out->provider_token.empty() ? "data.postgres" : out->provider_token;
    }
    if (!access_present || access == "readwrite") {
        out->access = SL_POSTGRES_ACCESS_READWRITE;
    }
    else if (access == "read") {
        out->access = SL_POSTGRES_ACCESS_READ;
    }
    else {
        return false;
    }

    v8::Local<v8::String> max_key;
    v8::Local<v8::Value> max_value;
    v8::Maybe<bool> has_max = v8::Nothing<bool>();
    if (!sl_status_is_ok(
            pg_v8_to_local_string(isolate, sl_str_from_cstr("maxConnections"), &max_key)))
    {
        return false;
    }
    has_max = object->HasOwnProperty(context, max_key);
    if (has_max.IsNothing()) {
        return false;
    }
    if (has_max.FromJust()) {
        if (!object->Get(context, max_key).ToLocal(&max_value)) {
            return false;
        }
    }
    else {
        max_value = v8::Undefined(isolate);
    }
    if (!max_value->IsUndefined() && !max_value->IsNull()) {
        if (!max_value->IsUint32()) {
            return false;
        }
        out->max_connections = max_value.As<v8::Uint32>()->Value();
    }
    if (out->max_connections == 0U || out->max_connections > SL_POSTGRES_MAX_POOL_CONNECTIONS) {
        return false;
    }
    return true;
}

void pg_v8_open_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    auto* resource = new (std::nothrow) PgV8ConnectionResource();
    SlResourceId id = sl_resource_id_invalid();
    SlDiag diag = {};
    v8::Local<v8::Object> handle;
    unsigned char storage[4096];
    SlArena arena = {};

    if (resource == nullptr) {
        pg_v8_throw_error(isolate, "postgres bridge could not allocate a connection resource");
        return;
    }
    if (args.Length() != 1 || !pg_v8_parse_open_options(isolate, context, args[0], resource)) {
        delete resource;
        pg_v8_throw_type_error(isolate, "__sloppy.data.postgres.open requires open options");
        return;
    }
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));
    if (!sl_status_is_ok(status) ||
        !pg_v8_check_capability(isolate, backend, &arena, resource,
                                pg_v8_open_capability_operation(resource->access)))
    {
        delete resource;
        return;
    }
    resource->connections.resize(resource->max_connections);
    status = sl_resource_table_insert(&backend->resources, SL_RESOURCE_KIND_POSTGRES_CONNECTION,
                                      resource, pg_v8_connection_cleanup, nullptr, &id, &diag);
    if (!sl_status_is_ok(status)) {
        pg_v8_connection_cleanup(resource, nullptr);
        pg_v8_throw_error(isolate, "postgres resource registration failed");
        return;
    }
    if (!pg_v8_make_resource_handle(isolate, context, id, &handle)) {
        (void)sl_resource_table_close_kind(&backend->resources, id,
                                           SL_RESOURCE_KIND_POSTGRES_CONNECTION, &diag);
        pg_v8_throw_error(isolate, "postgres resource registration failed");
        return;
    }
    args.GetReturnValue().Set(handle);
}

void pg_v8_close_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    SlResourceId id = {};
    SlDiag diag = {};
    PgV8ConnectionResource* resource = nullptr;

    if (args.Length() != 1 || !pg_v8_get_resource_id(isolate, context, args[0], &id)) {
        pg_v8_throw_type_error(isolate, "__sloppy.data.postgres.close requires a resource handle");
        return;
    }
    resource = pg_v8_lookup_connection(isolate, context, backend, args[0]);
    if (resource == nullptr) {
        return;
    }
    for (PgV8Connection& connection : resource->connections) {
        if (connection.request != nullptr || connection.transaction_pinned) {
            pg_v8_throw_error(isolate, "postgres connection has pending operations");
            return;
        }
    }
    SlStatus status = sl_resource_table_close_kind(&backend->resources, id,
                                                   SL_RESOURCE_KIND_POSTGRES_CONNECTION, &diag);
    if (!sl_status_is_ok(status)) {
        pg_v8_throw_error(isolate, "postgres close failed");
    }
}

bool pg_v8_make_promise(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        const std::shared_ptr<PgV8Request>& request, v8::Local<v8::Promise>* out)
{
    if (request == nullptr || !sl_v8_db_make_promise(isolate, context, &request->resolver, out)) {
        return false;
    }
    return true;
}

void pg_v8_operation_callback(const v8::FunctionCallbackInfo<v8::Value>& args,
                              PgV8Operation operation, const char* signature)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    PgV8ConnectionResource* resource = nullptr;
    auto request = std::make_shared<PgV8Request>();
    v8::Local<v8::Promise> promise;
    unsigned char storage[4096];
    SlArena arena = {};

    if (args.Length() < 1) {
        pg_v8_throw_type_error(isolate, signature);
        return;
    }
    resource = pg_v8_lookup_connection(isolate, context, backend, args[0]);
    if (resource == nullptr) {
        return;
    }
    if (operation != PgV8Operation::Begin && operation != PgV8Operation::Commit &&
        operation != PgV8Operation::Rollback)
    {
        if (args.Length() < 2 || !pg_v8_value_to_std_string(isolate, args[1], &request->sql) ||
            request->sql.empty())
        {
            pg_v8_throw_type_error(isolate, signature);
            return;
        }
        if (!pg_v8_convert_params(isolate, context,
                                  args.Length() == 3 ? args[2] : v8::Undefined(isolate),
                                  &request->params))
        {
            return;
        }
    }
    request->backend = backend;
    request->resource = resource;
    request->operation = operation;
    if (!pg_v8_make_promise(isolate, context, request, &promise)) {
        pg_v8_throw_error(isolate, "postgres bridge could not create a promise");
        return;
    }
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));
    if (!sl_status_is_ok(status) || !pg_v8_check_capability(isolate, backend, &arena, resource,
                                                            pg_v8_request_capability(operation)))
    {
        return;
    }
    if (operation == PgV8Operation::Begin && resource->transaction_active) {
        pg_v8_throw_error(isolate, "postgres nested transactions are not supported");
        return;
    }
    if (!pg_v8_submit_request(request)) {
        pg_v8_finish_request(request, false);
        request->resolver.Reset(isolate, v8::Local<v8::Promise::Resolver>());
        pg_v8_throw_error(isolate, request->error.empty() ? "postgres operation could not start"
                                                          : request->error);
        return;
    }
    args.GetReturnValue().Set(promise);
}

void pg_v8_exec_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    pg_v8_operation_callback(
        args, PgV8Operation::Exec,
        "__sloppy.data.postgres.exec requires a handle, SQL string, and optional params");
}

void pg_v8_query_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    pg_v8_operation_callback(
        args, PgV8Operation::Query,
        "__sloppy.data.postgres.query requires a handle, SQL string, and optional params");
}

void pg_v8_query_one_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    pg_v8_operation_callback(
        args, PgV8Operation::QueryOne,
        "__sloppy.data.postgres.queryOne requires a handle, SQL string, and optional params");
}

void pg_v8_begin_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    pg_v8_operation_callback(args, PgV8Operation::Begin,
                             "__sloppy.data.postgres.transactionBegin requires a handle");
}

void pg_v8_commit_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    pg_v8_operation_callback(args, PgV8Operation::Commit,
                             "__sloppy.data.postgres.transactionCommit requires a handle");
}

void pg_v8_rollback_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    pg_v8_operation_callback(args, PgV8Operation::Rollback,
                             "__sloppy.data.postgres.transactionRollback requires a handle");
}

void pg_v8_transaction_exec_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    pg_v8_operation_callback(args, PgV8Operation::TransactionExec,
                             "__sloppy.data.postgres.transactionExec requires a handle, SQL "
                             "string, and optional params");
}

void pg_v8_transaction_query_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    pg_v8_operation_callback(args, PgV8Operation::TransactionQuery,
                             "__sloppy.data.postgres.transactionQuery requires a handle, SQL "
                             "string, and optional params");
}

void pg_v8_transaction_query_one_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    pg_v8_operation_callback(args, PgV8Operation::TransactionQueryOne,
                             "__sloppy.data.postgres.transactionQueryOne requires a handle, SQL "
                             "string, and optional params");
}

bool pg_v8_set_function(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        v8::Local<v8::Object> object, const char* name,
                        v8::FunctionCallback callback)
{
    v8::Local<v8::String> key;
    v8::Local<v8::Function> function;
    if (!sl_status_is_ok(pg_v8_to_local_string(isolate, sl_str_from_cstr(name), &key)) ||
        !v8::Function::New(context, callback).ToLocal(&function))
    {
        return false;
    }
    return object->Set(context, key, function).FromMaybe(false);
}

} // namespace

bool sl_v8_install_postgres_intrinsics(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                       v8::Local<v8::Object> data)
{
    v8::Local<v8::Object> postgres = v8::Object::New(isolate);
    v8::Local<v8::String> postgres_key;

    if (isolate == nullptr || !sl_status_is_ok(pg_v8_to_local_string(
                                  isolate, sl_str_from_cstr("postgres"), &postgres_key)))
    {
        return false;
    }
    if (!pg_v8_set_function(isolate, context, postgres, "open", pg_v8_open_callback) ||
        !pg_v8_set_function(isolate, context, postgres, "close", pg_v8_close_callback) ||
        !pg_v8_set_function(isolate, context, postgres, "exec", pg_v8_exec_callback) ||
        !pg_v8_set_function(isolate, context, postgres, "query", pg_v8_query_callback) ||
        !pg_v8_set_function(isolate, context, postgres, "queryOne", pg_v8_query_one_callback) ||
        !pg_v8_set_function(isolate, context, postgres, "transactionBegin", pg_v8_begin_callback) ||
        !pg_v8_set_function(isolate, context, postgres, "transactionCommit",
                            pg_v8_commit_callback) ||
        !pg_v8_set_function(isolate, context, postgres, "transactionRollback",
                            pg_v8_rollback_callback) ||
        !pg_v8_set_function(isolate, context, postgres, "transactionExec",
                            pg_v8_transaction_exec_callback) ||
        !pg_v8_set_function(isolate, context, postgres, "transactionQuery",
                            pg_v8_transaction_query_callback) ||
        !pg_v8_set_function(isolate, context, postgres, "transactionQueryOne",
                            pg_v8_transaction_query_one_callback) ||
        !data->Set(context, postgres_key, postgres).FromMaybe(false))
    {
        return false;
    }
    return true;
}

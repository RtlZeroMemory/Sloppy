/*
 * src/engine/v8/intrinsics_sqlserver.cc
 *
 * Installs the V8-internal SQL Server bridge under __sloppy.data.sqlserver.
 * The bridge enables ODBC asynchronous connection/statement mode and advances the
 * driver state machine through Sloppy-owned V8 continuations. It does not expose ODBC
 * handles, sockets, native pointers, or worker objects to JavaScript.
 */
#include "engine_v8_internal.h"
#include "intrinsics_db_bridge.h"
#include "string_interop.h"

#include "sloppy/capability.h"
#include "sloppy/data_sqlserver.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

#ifdef SLOPPY_ENABLE_SQLSERVER_PROVIDER
#if defined(_WIN32) && !defined(_WINDOWS_)
typedef int64_t INT64;
typedef uint64_t UINT64;
typedef void* HWND;
typedef unsigned long DWORD;
typedef unsigned short WORD;
typedef unsigned char BYTE;
typedef wchar_t* LPWSTR;
typedef wchar_t WCHAR;
typedef char CHAR;
typedef void VOID;
typedef int BOOL;
typedef struct GUID
{
    DWORD Data1;
    WORD Data2;
    WORD Data3;
    BYTE Data4[8];
} GUID;
#endif
#include <sql.h>
#include <sqlext.h>
#ifndef SQL_SS_TIME2
#define SQL_SS_TIME2 (-154)
#endif
#ifndef SQL_SS_TIMESTAMPOFFSET
#define SQL_SS_TIMESTAMPOFFSET (-155)
#endif
#endif

namespace {

constexpr double sqlsrv_v8_max_safe_integer = 9007199254740991.0;
constexpr double sqlsrv_v8_min_safe_integer = -9007199254740991.0;
constexpr std::chrono::seconds sqlsrv_v8_async_progress_timeout{30};

enum class SqlSrvV8Operation
{
    Exec,
    Query,
    QueryRaw,
    QueryOne,
    Begin,
    Commit,
    Rollback,
    TransactionExec,
    TransactionQuery,
    TransactionQueryRaw,
    TransactionQueryOne,
};

SlStatus sqlsrv_v8_to_local_string(v8::Isolate* isolate, SlStr str, v8::Local<v8::String>* out)
{
    return sl_v8_db_to_local_string(isolate, str, out);
}

void sqlsrv_v8_throw_type_error(v8::Isolate* isolate, const char* message)
{
    sl_v8_db_throw_type_error(isolate, message, "Sloppy SQL Server type error");
}

void sqlsrv_v8_throw_error(v8::Isolate* isolate, const std::string& message)
{
    sl_v8_db_throw_error(isolate, message, "Sloppy SQL Server operation failed");
}

bool sqlsrv_v8_value_to_std_string(v8::Isolate* isolate, v8::Local<v8::Value> value,
                                   std::string* out)
{
    return sl_v8_db_value_to_std_string(isolate, value, out);
}

bool sqlsrv_v8_get_resource_id(v8::Isolate* isolate, v8::Local<v8::Context> context,
                               v8::Local<v8::Value> value, SlResourceId* out)
{
    return sl_v8_db_get_resource_id(isolate, context, value, out);
}

bool sqlsrv_v8_make_resource_handle(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                    SlResourceId id, v8::Local<v8::Object>* out)
{
    return sl_v8_db_make_resource_handle(isolate, context, id, "sqlserver.connection", out);
}

bool sqlsrv_v8_get_optional_object_string(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                          v8::Local<v8::Object> object, const char* key,
                                          std::string* out, bool* present)
{
    return sl_v8_db_get_optional_object_string(isolate, context, object, key, out, present);
}

#ifdef SLOPPY_ENABLE_SQLSERVER_PROVIDER

enum class SqlSrvV8ConnectionState
{
    Empty,
    Connecting,
    Busy,
    Idle,
    Closed,
};

enum class SqlSrvV8RequestState
{
    Connecting,
    Executing,
    Fetching,
    Terminal,
};

enum class SqlSrvV8ParamKind
{
    Null,
    Bool,
    Int64,
    Float64,
    Text,
    Bytes,
    Decimal,
    Uuid,
    Date,
    Time,
    LocalDateTime,
    OffsetDateTime,
    JsonText,
};

enum class SqlSrvV8CellKind
{
    Null,
    Bool,
    Int32,
    Int64,
    Float64,
    Text,
    Bytes,
    Decimal,
    Uuid,
    Date,
    Time,
    LocalDateTime,
    OffsetDateTime,
};

struct SqlSrvV8ConnectionResource;
struct SqlSrvV8Connection;
struct SqlSrvV8Request;

struct SqlSrvV8Param
{
    SqlSrvV8ParamKind kind = SqlSrvV8ParamKind::Null;
    std::string text;
    std::vector<unsigned char> bytes;
    int64_t int_value = 0;
    double float_value = 0.0;
    unsigned char bool_value = 0U;
    SQLLEN indicator = SQL_NULL_DATA;
    SQLSMALLINT sql_type = SQL_VARCHAR;
};

struct SqlSrvV8Column
{
    std::string name;
    SQLSMALLINT sql_type = SQL_VARCHAR;
};

struct SqlSrvV8Cell
{
    SqlSrvV8CellKind kind = SqlSrvV8CellKind::Null;
    std::string text;
    std::vector<unsigned char> bytes;
    int64_t int_value = 0;
    double float_value = 0.0;
    bool bool_value = false;
};

struct SqlSrvV8Request
{
    SlV8Engine* backend = nullptr;
    SqlSrvV8ConnectionResource* resource = nullptr;
    SqlSrvV8Connection* connection = nullptr;
    SqlSrvV8Operation operation = SqlSrvV8Operation::Query;
    SqlSrvV8RequestState state = SqlSrvV8RequestState::Connecting;
    v8::Global<v8::Promise::Resolver> resolver;
    std::string sql;
    std::vector<SqlSrvV8Param> params;
    std::vector<SqlSrvV8Column> columns;
    std::vector<std::vector<SqlSrvV8Cell>> rows;
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    int64_t affected_rows = -1;
    bool affected_rows_known = false;
    std::string error;
    uint32_t max_rows = SL_SQLSERVER_DEFAULT_MAX_ROWS;
    bool has_timeout_ms = false;
    uint32_t timeout_ms = 0U;
    std::atomic_bool terminal = false;
    std::atomic_bool timeout_cancelled = false;
    std::atomic_bool timeout_watch_started = false;
    std::mutex stmt_mutex;
    std::mutex timeout_mutex;
    std::condition_variable timeout_cv;
    std::thread timeout_thread;
    bool timeout_stop_requested = false;
    bool transaction_terminal = false;
    bool connect_pending = false;
    bool execute_pending = false;
    bool fetch_pending = false;
    size_t async_poll_count = 0U;
    bool async_progress_started = false;
    std::chrono::steady_clock::time_point async_progress_started_at;
};

struct SqlSrvV8Connection
{
    SQLHENV env = SQL_NULL_HENV;
    SQLHDBC dbc = SQL_NULL_HDBC;
    SqlSrvV8ConnectionState state = SqlSrvV8ConnectionState::Empty;
    std::shared_ptr<SqlSrvV8Request> request;
    bool transaction_pinned = false;
};

struct SqlSrvV8ConnectionResource
{
    std::string connection_string;
    std::string capability;
    std::string provider_token;
    SlSqlServerAccess access = SL_SQLSERVER_ACCESS_READWRITE;
    size_t max_connections = SL_SQLSERVER_DEFAULT_MAX_CONNECTIONS;
    std::vector<SqlSrvV8Connection> connections;
    bool transaction_active = false;
    size_t transaction_index = 0U;
};

struct SqlSrvV8CompletionPayload
{
    std::shared_ptr<SqlSrvV8Request> request;
    SqlSrvV8Connection* connection = nullptr;
};

bool sqlsrv_v8_plan_provider_matches(const SlPlanDataProvider& provider, SlStr token)
{
    if (sl_str_equal(provider.token, token)) {
        return true;
    }
    if (!sl_str_is_empty(provider.service) && sl_str_equal(provider.service, token)) {
        return true;
    }
    return !sl_str_is_empty(provider.capability) && sl_str_equal(provider.capability, token);
}

const SlPlanDataProvider* sqlsrv_v8_find_provider(const SlV8Engine* backend, SlStr token)
{
    if (backend == nullptr || backend->plan == nullptr || sl_str_is_empty(token) ||
        (backend->plan->data_provider_count > 0U && backend->plan->data_providers == nullptr))
    {
        return nullptr;
    }
    for (size_t index = 0U; index < backend->plan->data_provider_count; index += 1U) {
        const SlPlanDataProvider& provider = backend->plan->data_providers[index];
        if (sqlsrv_v8_plan_provider_matches(provider, token)) {
            return &provider;
        }
    }
    return nullptr;
}

SlCapabilityOperation sqlsrv_v8_open_capability_operation(SlSqlServerAccess access)
{
    return access == SL_SQLSERVER_ACCESS_READ ? SL_CAPABILITY_OPERATION_READ
                                              : SL_CAPABILITY_OPERATION_READWRITE;
}

SlCapabilityOperation sqlsrv_v8_request_capability(SqlSrvV8Operation operation)
{
    switch (operation) {
    case SqlSrvV8Operation::Query:
    case SqlSrvV8Operation::QueryRaw:
    case SqlSrvV8Operation::QueryOne:
    case SqlSrvV8Operation::TransactionQuery:
    case SqlSrvV8Operation::TransactionQueryRaw:
    case SqlSrvV8Operation::TransactionQueryOne:
        return SL_CAPABILITY_OPERATION_READ;
    default:
        return SL_CAPABILITY_OPERATION_WRITE;
    }
}

bool sqlsrv_v8_access_allows(SlSqlServerAccess access, SlCapabilityOperation operation)
{
    if (operation == SL_CAPABILITY_OPERATION_READ) {
        return access == SL_SQLSERVER_ACCESS_READ || access == SL_SQLSERVER_ACCESS_READWRITE;
    }
    return access == SL_SQLSERVER_ACCESS_READWRITE;
}

bool sqlsrv_v8_check_capability(v8::Isolate* isolate, SlV8Engine* backend, SlArena* arena,
                                const SqlSrvV8ConnectionResource* resource,
                                SlCapabilityOperation operation)
{
    SlDiag diag = {};
    SlStatus status;

    if (backend == nullptr || backend->capabilities == nullptr) {
        sqlsrv_v8_throw_error(isolate, "sqlserver capability registry is unavailable");
        return false;
    }
    if (resource == nullptr || arena == nullptr || resource->capability.empty()) {
        sqlsrv_v8_throw_error(isolate, "sqlserver resource is missing capability metadata");
        return false;
    }
    if (!sqlsrv_v8_access_allows(resource->access, operation)) {
        sqlsrv_v8_throw_error(isolate,
                              "capability access denied: sqlserver handle access is read-only");
        return false;
    }

    status = sl_capability_check_database_provider(
        backend->capabilities, arena,
        sl_str_from_parts(resource->capability.data(), resource->capability.size()), operation,
        sl_str_from_cstr("sqlserver"), &diag);
    if (!sl_status_is_ok(status)) {
        std::string message = "sqlserver capability check failed";
        if (diag.message.ptr != nullptr && diag.message.length != 0U) {
            message.assign(diag.message.ptr, diag.message.length);
        }
        sqlsrv_v8_throw_error(isolate, message);
        return false;
    }
    return true;
}

SqlSrvV8ConnectionResource* sqlsrv_v8_lookup_connection(v8::Isolate* isolate,
                                                        v8::Local<v8::Context> context,
                                                        SlV8Engine* backend,
                                                        v8::Local<v8::Value> handle_value)
{
    SlResourceId id = {};
    SlDiag diag = {};
    void* ptr = nullptr;

    if (!sqlsrv_v8_get_resource_id(isolate, context, handle_value, &id)) {
        sqlsrv_v8_throw_type_error(isolate,
                                   "sqlserver resource handle must be an opaque Sloppy handle");
        return nullptr;
    }
    SlStatus status = sl_resource_table_get(&backend->resources, id,
                                            SL_RESOURCE_KIND_SQLSERVER_CONNECTION, &ptr, &diag);
    if (!sl_status_is_ok(status)) {
        sqlsrv_v8_throw_error(isolate, "sqlserver resource handle is invalid");
        return nullptr;
    }
    return static_cast<SqlSrvV8ConnectionResource*>(ptr);
}

std::string sqlsrv_v8_diag(SQLSMALLINT handle_type, SQLHANDLE handle, const char* fallback)
{
    SQLCHAR state[6] = {};
    SQLINTEGER native = 0;
    SQLCHAR message[1024] = {};
    SQLSMALLINT length = 0;
    SQLRETURN rc = SQL_INVALID_HANDLE;

    if (handle != SQL_NULL_HANDLE) {
        rc = SQLGetDiagRecA(handle_type, handle, 1, state, &native, message,
                            (SQLSMALLINT)sizeof(message), &length);
    }
    if (SQL_SUCCEEDED(rc) && message[0] != '\0') {
        std::string output = "sqlserver ODBC error ";
        output += reinterpret_cast<const char*>(state);
        output += ": ";
        output += reinterpret_cast<const char*>(message);
        return output;
    }
    return fallback == nullptr ? "sqlserver operation failed" : fallback;
}

void sqlsrv_v8_free_statement(SqlSrvV8Request* request)
{
    if (request == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> guard(request->stmt_mutex);
    if (request->stmt != SQL_NULL_HSTMT) {
        SQLFreeHandle(SQL_HANDLE_STMT, request->stmt);
        request->stmt = SQL_NULL_HSTMT;
    }
}

void sqlsrv_v8_cancel_statement(SqlSrvV8Request* request)
{
    if (request == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> guard(request->stmt_mutex);
    if (request->stmt != SQL_NULL_HSTMT) {
        SQLCancelHandle(SQL_HANDLE_STMT, request->stmt);
    }
}

void sqlsrv_v8_stop_timeout_watch(const std::shared_ptr<SqlSrvV8Request>& request)
{
    if (request == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> guard(request->timeout_mutex);
        request->timeout_stop_requested = true;
    }
    request->timeout_cv.notify_all();
    if (request->timeout_thread.joinable() &&
        request->timeout_thread.get_id() != std::this_thread::get_id())
    {
        request->timeout_thread.join();
    }
}

void sqlsrv_v8_close_connection(SqlSrvV8Connection& connection)
{
    if (connection.request != nullptr) {
        connection.request->terminal.store(true);
        sqlsrv_v8_stop_timeout_watch(connection.request);
        sqlsrv_v8_cancel_statement(connection.request.get());
        sqlsrv_v8_free_statement(connection.request.get());
    }
    if (connection.dbc != SQL_NULL_HDBC) {
        SQLDisconnect(connection.dbc);
        SQLFreeHandle(SQL_HANDLE_DBC, connection.dbc);
        connection.dbc = SQL_NULL_HDBC;
    }
    if (connection.env != SQL_NULL_HENV) {
        SQLFreeHandle(SQL_HANDLE_ENV, connection.env);
        connection.env = SQL_NULL_HENV;
    }
    connection.request.reset();
    connection.transaction_pinned = false;
    connection.state = SqlSrvV8ConnectionState::Closed;
}

void sqlsrv_v8_connection_cleanup(void* ptr, void* user)
{
    SqlSrvV8ConnectionResource* resource = static_cast<SqlSrvV8ConnectionResource*>(ptr);
    (void)user;
    if (resource == nullptr) {
        return;
    }
    for (SqlSrvV8Connection& connection : resource->connections) {
        sqlsrv_v8_close_connection(connection);
    }
    delete resource;
}

bool sqlsrv_v8_result_is_query(SqlSrvV8Operation operation)
{
    return operation == SqlSrvV8Operation::Query || operation == SqlSrvV8Operation::QueryRaw ||
           operation == SqlSrvV8Operation::QueryOne ||
           operation == SqlSrvV8Operation::TransactionQuery ||
           operation == SqlSrvV8Operation::TransactionQueryRaw ||
           operation == SqlSrvV8Operation::TransactionQueryOne;
}

bool sqlsrv_v8_operation_allows_max_rows(SqlSrvV8Operation operation)
{
    return operation == SqlSrvV8Operation::Query || operation == SqlSrvV8Operation::QueryRaw ||
           operation == SqlSrvV8Operation::TransactionQuery ||
           operation == SqlSrvV8Operation::TransactionQueryRaw;
}

bool sqlsrv_v8_result_is_exec(SqlSrvV8Operation operation)
{
    return operation == SqlSrvV8Operation::Exec || operation == SqlSrvV8Operation::TransactionExec;
}

void sqlsrv_v8_finish_request(const std::shared_ptr<SqlSrvV8Request>& request, bool ok)
{
    SqlSrvV8Connection* connection = request == nullptr ? nullptr : request->connection;
    if (connection == nullptr || request->resource == nullptr) {
        return;
    }
    request->terminal.store(true);
    sqlsrv_v8_stop_timeout_watch(request);
    sqlsrv_v8_free_statement(request.get());
    if (!ok) {
        if (request->operation == SqlSrvV8Operation::Begin || request->transaction_terminal) {
            request->resource->transaction_active = false;
        }
        sqlsrv_v8_close_connection(*connection);
    }
    else if (request->transaction_terminal) {
        connection->transaction_pinned = false;
        request->resource->transaction_active = false;
        connection->state = SqlSrvV8ConnectionState::Idle;
    }
    else {
        if (request->operation == SqlSrvV8Operation::Begin) {
            request->resource->transaction_active = true;
            connection->transaction_pinned = true;
        }
        connection->state = SqlSrvV8ConnectionState::Idle;
    }
    connection->request.reset();
}

void sqlsrv_v8_start_timeout_watch(const std::shared_ptr<SqlSrvV8Request>& request)
{
    if (request == nullptr || !request->has_timeout_ms || request->timeout_ms == 0U) {
        return;
    }
    bool expected = false;
    if (!request->timeout_watch_started.compare_exchange_strong(expected, true)) {
        return;
    }
    request->timeout_thread = std::thread([request]() {
        {
            std::unique_lock<std::mutex> lock(request->timeout_mutex);
            if (request->timeout_cv.wait_for(lock, std::chrono::milliseconds(request->timeout_ms),
                                             [&request]() {
                                                 return request->timeout_stop_requested ||
                                                        request->terminal.load();
                                             }))
            {
                return;
            }
        }
        if (request->terminal.load()) {
            return;
        }
        request->timeout_cancelled.store(true);
        std::lock_guard<std::mutex> guard(request->stmt_mutex);
        if (!request->terminal.load() && request->stmt != SQL_NULL_HSTMT) {
            SQLCancelHandle(SQL_HANDLE_STMT, request->stmt);
        }
    });
}

bool sqlsrv_v8_local_string(v8::Isolate* isolate, const std::string& value,
                            v8::Local<v8::String>* out)
{
    return sl_v8_db_local_string_from_std_string(isolate, value, out);
}

bool sqlsrv_v8_make_typed_string_value(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                       const char* kind, const std::string& value,
                                       v8::Local<v8::Value>* out)
{
    if (out == nullptr || kind == nullptr) {
        return false;
    }
    return sl_v8_db_make_typed_string_value(isolate, context, kind,
                                            sl_str_from_parts(value.data(), value.size()), out);
}

bool sqlsrv_v8_is_db_value(v8::Isolate* isolate, v8::Local<v8::Context> context,
                           v8::Local<v8::Value> value)
{
    return sl_v8_db_is_value_wrapper(isolate, context, value);
}

bool sqlsrv_v8_cell_to_value(v8::Isolate* isolate, v8::Local<v8::Context> context,
                             const SqlSrvV8Cell& cell, v8::Local<v8::Value>* out)
{
    v8::Local<v8::String> text;
    if (out == nullptr) {
        return false;
    }
    switch (cell.kind) {
    case SqlSrvV8CellKind::Null:
        *out = v8::Null(isolate);
        return true;
    case SqlSrvV8CellKind::Bool:
        *out = v8::Boolean::New(isolate, cell.bool_value);
        return true;
    case SqlSrvV8CellKind::Int32:
        *out = v8::Number::New(isolate, static_cast<double>(cell.int_value));
        return true;
    case SqlSrvV8CellKind::Int64:
        *out = v8::BigInt::New(isolate, cell.int_value);
        return true;
    case SqlSrvV8CellKind::Float64:
        *out = v8::Number::New(isolate, cell.float_value);
        return true;
    case SqlSrvV8CellKind::Bytes: {
        return sl_v8_db_uint8_array_from_bytes(
            isolate,
            sl_bytes_from_parts(cell.bytes.empty() ? nullptr : cell.bytes.data(),
                                cell.bytes.size()),
            out);
    }
    case SqlSrvV8CellKind::Text:
        if (!sqlsrv_v8_local_string(isolate, cell.text, &text)) {
            return false;
        }
        *out = text;
        return true;
    case SqlSrvV8CellKind::Decimal:
        return sqlsrv_v8_make_typed_string_value(isolate, context, "decimal", cell.text, out);
    case SqlSrvV8CellKind::Uuid:
        return sqlsrv_v8_make_typed_string_value(isolate, context, "uuid", cell.text, out);
    case SqlSrvV8CellKind::Date:
        return sqlsrv_v8_make_typed_string_value(isolate, context, "date", cell.text, out);
    case SqlSrvV8CellKind::Time:
        return sqlsrv_v8_make_typed_string_value(isolate, context, "time", cell.text, out);
    case SqlSrvV8CellKind::LocalDateTime:
        return sqlsrv_v8_make_typed_string_value(isolate, context, "localDateTime", cell.text, out);
    case SqlSrvV8CellKind::OffsetDateTime:
        return sqlsrv_v8_make_typed_string_value(isolate, context, "offsetDateTime", cell.text,
                                                 out);
    }
    return false;
}

bool sqlsrv_v8_prepare_columns(v8::Isolate* isolate, v8::Local<v8::Context> context,
                               const SqlSrvV8Request& request, SlV8DbColumnSet* out)
{
    std::vector<SlStr> names;
    if (out == nullptr) {
        return false;
    }
    names.reserve(request.columns.size());
    for (const SqlSrvV8Column& column : request.columns) {
        names.emplace_back(sl_str_from_parts(column.name.data(), column.name.size()));
    }
    return sl_v8_db_prepare_column_set(isolate, context, names.empty() ? nullptr : names.data(),
                                       names.size(), out);
}

bool sqlsrv_v8_rows_to_array(v8::Isolate* isolate, v8::Local<v8::Context> context,
                             const SqlSrvV8Request& request, v8::Local<v8::Array>* out)
{
    v8::Local<v8::Array> rows = v8::Array::New(isolate, static_cast<int>(request.rows.size()));
    SlV8DbColumnSet columns;
    std::vector<v8::Local<v8::Value>> values(request.columns.size());
    if (out == nullptr) {
        return false;
    }
    if (!sqlsrv_v8_prepare_columns(isolate, context, request, &columns)) {
        return false;
    }
    for (size_t row_index = 0U; row_index < request.rows.size(); row_index += 1U) {
        v8::Local<v8::Object> object;
        const std::vector<SqlSrvV8Cell>& row = request.rows[row_index];
        if (row.size() < request.columns.size()) {
            return false;
        }
        for (size_t column = 0U; column < request.columns.size() && column < row.size();
             column += 1U)
        {
            if (!sqlsrv_v8_cell_to_value(isolate, context, row[column], &values[column])) {
                return false;
            }
        }
        if (!sl_v8_db_make_row_object(isolate, context, &columns, values.data(), values.size(),
                                      &object))
        {
            return false;
        }
        if (!rows->Set(context, static_cast<uint32_t>(row_index), object).FromMaybe(false)) {
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

bool sqlsrv_v8_row_to_object(v8::Isolate* isolate, v8::Local<v8::Context> context,
                             const SqlSrvV8Request& request, const std::vector<SqlSrvV8Cell>& row,
                             v8::Local<v8::Object>* out)
{
    v8::Local<v8::Object> object;
    SlV8DbColumnSet columns;
    std::vector<v8::Local<v8::Value>> values(request.columns.size());
    if (out == nullptr) {
        return false;
    }
    if (!sqlsrv_v8_prepare_columns(isolate, context, request, &columns)) {
        return false;
    }
    if (row.size() < request.columns.size()) {
        return false;
    }
    for (size_t column = 0U; column < request.columns.size() && column < row.size(); column += 1U) {
        if (!sqlsrv_v8_cell_to_value(isolate, context, row[column], &values[column])) {
            return false;
        }
    }
    if (!sl_v8_db_make_row_object(isolate, context, &columns, values.data(), values.size(),
                                  &object) ||
        !sl_v8_db_attach_result_metadata(isolate, context, object, &columns,
                                         SL_V8_DB_STRING_OBJECT))
    {
        return false;
    }
    *out = object;
    return true;
}

bool sqlsrv_v8_rows_to_raw(v8::Isolate* isolate, v8::Local<v8::Context> context,
                           const SqlSrvV8Request& request, v8::Local<v8::Object>* out)
{
    v8::Local<v8::Array> rows = v8::Array::New(isolate, static_cast<int>(request.rows.size()));
    SlV8DbColumnSet columns;
    std::vector<v8::Local<v8::Value>> values(request.columns.size());
    if (out == nullptr) {
        return false;
    }
    if (!sqlsrv_v8_prepare_columns(isolate, context, request, &columns)) {
        return false;
    }
    for (size_t row_index = 0U; row_index < request.rows.size(); row_index += 1U) {
        v8::Local<v8::Array> row_array;
        const std::vector<SqlSrvV8Cell>& row = request.rows[row_index];
        if (row.size() < request.columns.size()) {
            return false;
        }
        for (size_t column = 0U; column < request.columns.size() && column < row.size();
             column += 1U)
        {
            if (!sqlsrv_v8_cell_to_value(isolate, context, row[column], &values[column])) {
                return false;
            }
        }
        if (!sl_v8_db_make_raw_row(isolate, context, values.data(), values.size(), &row_array) ||
            !rows->Set(context, static_cast<uint32_t>(row_index), row_array).FromMaybe(false))
        {
            return false;
        }
    }
    return sl_v8_db_make_raw_result(isolate, context, &columns, rows, out);
}

bool sqlsrv_v8_exec_result(v8::Isolate* isolate, v8::Local<v8::Context> context,
                           const SqlSrvV8Request& request, v8::Local<v8::Value>* out)
{
    v8::Local<v8::Object> object = v8::Object::New(isolate);
    v8::Local<v8::String> affected_key;
    v8::Local<v8::String> known_key;
    if (out == nullptr ||
        !sl_status_is_ok(
            sqlsrv_v8_to_local_string(isolate, sl_str_from_cstr("affectedRows"), &affected_key)) ||
        !sl_status_is_ok(sqlsrv_v8_to_local_string(isolate, sl_str_from_cstr("affectedRowsKnown"),
                                                   &known_key)) ||
        !object
             ->Set(context, affected_key,
                   v8::Number::New(isolate, static_cast<double>(request.affected_rows)))
             .FromMaybe(false) ||
        !object->Set(context, known_key, v8::Boolean::New(isolate, request.affected_rows_known))
             .FromMaybe(false))
    {
        return false;
    }
    *out = object;
    return true;
}

void sqlsrv_v8_settle_request(const std::shared_ptr<SqlSrvV8Request>& request, bool ok)
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
        sl_v8_db_reject_promise(isolate, context, resolver, request->error,
                                "sqlserver operation failed");
        sqlsrv_v8_finish_request(request, false);
        return;
    }

    v8::Local<v8::Value> value;
    bool converted = false;
    if (sqlsrv_v8_result_is_exec(request->operation)) {
        converted = sqlsrv_v8_exec_result(isolate, context, *request, &value);
    }
    else if (sqlsrv_v8_result_is_query(request->operation)) {
        if (request->operation == SqlSrvV8Operation::QueryOne ||
            request->operation == SqlSrvV8Operation::TransactionQueryOne)
        {
            if (request->rows.empty()) {
                value = v8::Null(isolate);
                converted = true;
            }
            else {
                v8::Local<v8::Object> object;
                converted =
                    sqlsrv_v8_row_to_object(isolate, context, *request, request->rows[0], &object);
                value = object;
            }
        }
        else if (request->operation == SqlSrvV8Operation::QueryRaw ||
                 request->operation == SqlSrvV8Operation::TransactionQueryRaw)
        {
            v8::Local<v8::Object> result;
            converted = sqlsrv_v8_rows_to_raw(isolate, context, *request, &result);
            value = result;
        }
        else {
            v8::Local<v8::Array> rows;
            converted = sqlsrv_v8_rows_to_array(isolate, context, *request, &rows);
            value = rows;
        }
    }
    else {
        value = v8::Undefined(isolate);
        converted = true;
    }

    if (!converted) {
        value = v8::Exception::Error(
            v8::String::NewFromUtf8Literal(isolate, "sqlserver result conversion failed"));
        resolver->Reject(context, value).FromMaybe(false);
        sqlsrv_v8_finish_request(request, false);
        return;
    }
    sl_v8_db_resolve_promise(context, resolver, value);
    sqlsrv_v8_finish_request(request, true);
}

SlStatus sqlsrv_v8_completion_dispatch(SlAsyncLoop* loop, const SlAsyncCompletion* completion,
                                       void* user);

void sqlsrv_v8_completion_cleanup(const SlAsyncCompletion* completion, void* user)
{
    SqlSrvV8CompletionPayload* payload =
        completion == nullptr ? nullptr
                              : static_cast<SqlSrvV8CompletionPayload*>(completion->payload);
    (void)user;
    delete payload;
}

void sqlsrv_v8_note_async_activity(SqlSrvV8Request* request)
{
    if (request == nullptr) {
        return;
    }
    request->async_progress_started = true;
    request->async_progress_started_at = std::chrono::steady_clock::now();
}

bool sqlsrv_v8_post_pump(const std::shared_ptr<SqlSrvV8Request>& request)
{
    SlV8Engine* backend = request == nullptr ? nullptr : request->backend;
    const auto now = std::chrono::steady_clock::now();
    if (backend == nullptr || backend->async_loop == nullptr || request->connection == nullptr) {
        return false;
    }
    request->async_poll_count += 1U;
    if (!request->async_progress_started) {
        request->async_progress_started = true;
        request->async_progress_started_at = now;
    }
    if (now - request->async_progress_started_at > sqlsrv_v8_async_progress_timeout) {
        const char* state = "unknown";
        if (request->state == SqlSrvV8RequestState::Connecting) {
            state = "connecting";
        }
        else if (request->state == SqlSrvV8RequestState::Executing) {
            state = "executing";
        }
        else if (request->state == SqlSrvV8RequestState::Fetching) {
            state = "fetching";
        }
        request->error = "sqlserver async ODBC operation did not complete while ";
        request->error += state;
        request->error += "; driver async support is unavailable or stalled for 30 seconds";
        sqlsrv_v8_settle_request(request, false);
        return true;
    }
    request->async_progress_started_at = now;
    auto* payload = new (std::nothrow) SqlSrvV8CompletionPayload();
    if (payload == nullptr) {
        return false;
    }
    payload->request = request;
    payload->connection = request->connection;

    SlAsyncCompletion completion = {};
    completion.kind = SL_ASYNC_COMPLETION_V8_CONTINUATION;
    completion.operation_kind = SL_ASYNC_OPERATION_PROVIDER;
    completion.status = sl_status_ok();
    completion.payload = payload;
    completion.dispatch = sqlsrv_v8_completion_dispatch;
    completion.cleanup = sqlsrv_v8_completion_cleanup;

    SlStatus status = sl_async_loop_post(backend->async_loop, &completion);
    if (!sl_status_is_ok(status)) {
        delete payload;
        return false;
    }
    return true;
}

void sqlsrv_v8_fail_request(const std::shared_ptr<SqlSrvV8Request>& request,
                            const std::string& message)
{
    if (request == nullptr) {
        return;
    }
    if (request->timeout_cancelled.load()) {
        request->error = "sqlserver provider operation deadline was exceeded";
    }
    else {
        request->error = message.empty() ? "sqlserver operation failed" : message;
    }
    sqlsrv_v8_settle_request(request, false);
}

bool sqlsrv_v8_bind_params(SqlSrvV8Request* request)
{
    if (request == nullptr || request->stmt == SQL_NULL_HSTMT) {
        return false;
    }
    for (size_t index = 0U; index < request->params.size(); index += 1U) {
        SqlSrvV8Param& param = request->params[index];
        SQLUSMALLINT sql_index = static_cast<SQLUSMALLINT>(index + 1U);
        SQLRETURN rc = SQL_ERROR;
        switch (param.kind) {
        case SqlSrvV8ParamKind::Null:
            param.indicator = SQL_NULL_DATA;
            rc = SQLBindParameter(request->stmt, sql_index, SQL_PARAM_INPUT, SQL_C_CHAR,
                                  SQL_VARCHAR, 1, 0, nullptr, 0, &param.indicator);
            break;
        case SqlSrvV8ParamKind::Bool:
            param.indicator = 0;
            rc = SQLBindParameter(request->stmt, sql_index, SQL_PARAM_INPUT, SQL_C_BIT, SQL_BIT, 0,
                                  0, &param.bool_value, 0, &param.indicator);
            break;
        case SqlSrvV8ParamKind::Int64:
            param.indicator = 0;
            rc = SQLBindParameter(request->stmt, sql_index, SQL_PARAM_INPUT, SQL_C_SBIGINT,
                                  SQL_BIGINT, 0, 0, &param.int_value, 0, &param.indicator);
            break;
        case SqlSrvV8ParamKind::Float64:
            param.indicator = 0;
            rc = SQLBindParameter(request->stmt, sql_index, SQL_PARAM_INPUT, SQL_C_DOUBLE,
                                  SQL_DOUBLE, 0, 0, &param.float_value, 0, &param.indicator);
            break;
        case SqlSrvV8ParamKind::Text:
            param.indicator = static_cast<SQLLEN>(param.text.size());
            rc =
                SQLBindParameter(request->stmt, sql_index, SQL_PARAM_INPUT, SQL_C_CHAR,
                                 SQL_WVARCHAR, param.text.size(), 0, (SQLPOINTER)param.text.c_str(),
                                 param.indicator, &param.indicator);
            break;
        case SqlSrvV8ParamKind::Decimal:
        case SqlSrvV8ParamKind::Uuid:
        case SqlSrvV8ParamKind::Date:
        case SqlSrvV8ParamKind::Time:
        case SqlSrvV8ParamKind::LocalDateTime:
        case SqlSrvV8ParamKind::OffsetDateTime:
        case SqlSrvV8ParamKind::JsonText:
            param.indicator = static_cast<SQLLEN>(param.text.size());
            rc =
                SQLBindParameter(request->stmt, sql_index, SQL_PARAM_INPUT, SQL_C_CHAR,
                                 param.sql_type, param.text.size(), 0,
                                 (SQLPOINTER)param.text.c_str(), param.indicator, &param.indicator);
            break;
        case SqlSrvV8ParamKind::Bytes:
            param.indicator = static_cast<SQLLEN>(param.bytes.size());
            rc = SQLBindParameter(request->stmt, sql_index, SQL_PARAM_INPUT, SQL_C_BINARY,
                                  SQL_VARBINARY, param.bytes.size(), 0,
                                  param.bytes.empty() ? nullptr : (SQLPOINTER)param.bytes.data(),
                                  param.indicator, &param.indicator);
            break;
        }
        if (!SQL_SUCCEEDED(rc)) {
            request->error = sqlsrv_v8_diag(SQL_HANDLE_STMT, request->stmt,
                                            "sqlserver parameter binding failed");
            return false;
        }
    }
    return true;
}

bool sqlsrv_v8_allocate_connection(SqlSrvV8ConnectionResource* resource,
                                   SqlSrvV8Connection* connection,
                                   std::shared_ptr<SqlSrvV8Request> request)
{
    SQLRETURN rc;
    if (resource == nullptr || connection == nullptr || request == nullptr) {
        return false;
    }
    rc = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &connection->env);
    if (!SQL_SUCCEEDED(rc)) {
        request->error = "sqlserver ODBC environment allocation failed";
        return false;
    }
    rc = SQLSetEnvAttr(connection->env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3_80, 0);
    if (!SQL_SUCCEEDED(rc)) {
        request->error =
            sqlsrv_v8_diag(SQL_HANDLE_ENV, connection->env, "sqlserver ODBC version setup failed");
        return false;
    }
    rc = SQLAllocHandle(SQL_HANDLE_DBC, connection->env, &connection->dbc);
    if (!SQL_SUCCEEDED(rc)) {
        request->error = sqlsrv_v8_diag(SQL_HANDLE_ENV, connection->env,
                                        "sqlserver ODBC connection allocation failed");
        return false;
    }
    rc = SQLSetConnectAttr(connection->dbc, SQL_ATTR_ASYNC_DBC_FUNCTIONS_ENABLE,
                           (SQLPOINTER)SQL_ASYNC_DBC_ENABLE_ON, 0);
    if (!SQL_SUCCEEDED(rc)) {
        rc = SQLSetConnectAttr(connection->dbc, SQL_ATTR_ASYNC_ENABLE,
                               (SQLPOINTER)SQL_ASYNC_ENABLE_ON, 0);
    }
    if (!SQL_SUCCEEDED(rc)) {
        request->error = sqlsrv_v8_diag(
            SQL_HANDLE_DBC, connection->dbc,
            "sqlserver ODBC driver manager does not support asynchronous connections");
        return false;
    }
    SQLSetConnectAttr(connection->dbc, SQL_ATTR_LOGIN_TIMEOUT, (SQLPOINTER)15, 0);
    return true;
}

SQLRETURN sqlsrv_v8_connect(SqlSrvV8Request* request)
{
    SqlSrvV8Connection* connection = request == nullptr ? nullptr : request->connection;
    SqlSrvV8ConnectionResource* resource = request == nullptr ? nullptr : request->resource;
    if (connection == nullptr || resource == nullptr || connection->dbc == SQL_NULL_HDBC) {
        return SQL_ERROR;
    }
    if (request->connect_pending) {
        RETCODE async_rc = SQL_ERROR;
        SQLRETURN rc = SQLCompleteAsync(SQL_HANDLE_DBC, connection->dbc, &async_rc);
        if (rc == SQL_STILL_EXECUTING || (SQL_SUCCEEDED(rc) && async_rc == SQL_STILL_EXECUTING)) {
            return SQL_STILL_EXECUTING;
        }
        request->connect_pending = false;
        if (!SQL_SUCCEEDED(rc)) {
            return rc;
        }
        return async_rc;
    }
    SQLRETURN rc =
        SQLDriverConnectA(connection->dbc, nullptr, (SQLCHAR*)resource->connection_string.c_str(),
                          SQL_NTS, nullptr, 0, nullptr, SQL_DRIVER_NOPROMPT);
    return rc;
}

bool sqlsrv_v8_allocate_statement(SqlSrvV8Request* request)
{
    SQLRETURN rc;
    SqlSrvV8Connection* connection = request == nullptr ? nullptr : request->connection;
    if (request == nullptr || connection == nullptr || connection->dbc == SQL_NULL_HDBC) {
        return false;
    }
    rc = SQLAllocHandle(SQL_HANDLE_STMT, connection->dbc, &request->stmt);
    if (!SQL_SUCCEEDED(rc)) {
        request->error = sqlsrv_v8_diag(SQL_HANDLE_DBC, connection->dbc,
                                        "sqlserver statement allocation failed");
        return false;
    }
    rc = SQLSetStmtAttr(request->stmt, SQL_ATTR_ASYNC_ENABLE, (SQLPOINTER)SQL_ASYNC_ENABLE_ON, 0);
    if (!SQL_SUCCEEDED(rc)) {
        request->error =
            sqlsrv_v8_diag(SQL_HANDLE_STMT, request->stmt,
                           "sqlserver ODBC driver does not support asynchronous statements");
        return false;
    }
    if (request->has_timeout_ms && request->timeout_ms > 0U) {
        SQLULEN timeout_seconds = (request->timeout_ms + 999U) / 1000U;
        rc = SQLSetStmtAttr(request->stmt, SQL_ATTR_QUERY_TIMEOUT,
                            (SQLPOINTER)(uintptr_t)timeout_seconds, 0);
        if (!SQL_SUCCEEDED(rc)) {
            request->error = sqlsrv_v8_diag(SQL_HANDLE_STMT, request->stmt,
                                            "sqlserver ODBC driver does not support query timeout");
            return false;
        }
    }
    if (!sqlsrv_v8_bind_params(request)) {
        return false;
    }
    return true;
}

SQLRETURN sqlsrv_v8_execute(SqlSrvV8Request* request)
{
    if (request == nullptr || request->stmt == SQL_NULL_HSTMT) {
        return SQL_ERROR;
    }
    if (request->execute_pending) {
        RETCODE async_rc = SQL_ERROR;
        SQLRETURN rc = SQLCompleteAsync(SQL_HANDLE_STMT, request->stmt, &async_rc);
        if (rc == SQL_STILL_EXECUTING || (SQL_SUCCEEDED(rc) && async_rc == SQL_STILL_EXECUTING)) {
            return SQL_STILL_EXECUTING;
        }
        request->execute_pending = false;
        if (!SQL_SUCCEEDED(rc)) {
            return rc;
        }
        return async_rc;
    }
    SQLRETURN rc = SQLExecDirectA(request->stmt, (SQLCHAR*)request->sql.c_str(), SQL_NTS);
    return rc;
}

bool sqlsrv_v8_describe_columns(SqlSrvV8Request* request)
{
    SQLSMALLINT column_count = 0;
    SQLRETURN rc = SQLNumResultCols(request->stmt, &column_count);
    if (!SQL_SUCCEEDED(rc)) {
        request->error =
            sqlsrv_v8_diag(SQL_HANDLE_STMT, request->stmt, "sqlserver column metadata failed");
        return false;
    }
    request->columns.clear();
    request->columns.reserve(column_count > 0 ? (size_t)column_count : 0U);
    for (SQLSMALLINT column = 1; column <= column_count; column += 1) {
        char name[512] = {};
        SQLSMALLINT name_length = 0;
        SQLSMALLINT data_type = SQL_VARCHAR;
        rc = SQLDescribeColA(request->stmt, (SQLUSMALLINT)column, (SQLCHAR*)name,
                             (SQLSMALLINT)sizeof(name), &name_length, &data_type, nullptr, nullptr,
                             nullptr);
        if (!SQL_SUCCEEDED(rc)) {
            request->error =
                sqlsrv_v8_diag(SQL_HANDLE_STMT, request->stmt, "sqlserver column metadata failed");
            return false;
        }
        SqlSrvV8Column metadata = {};
        metadata.name = name[0] == '\0' ? "column" + std::to_string(column) : name;
        metadata.sql_type = data_type;
        request->columns.push_back(std::move(metadata));
    }
    return true;
}

bool sqlsrv_v8_is_binary_type(SQLSMALLINT type)
{
    return type == SQL_BINARY || type == SQL_VARBINARY || type == SQL_LONGVARBINARY;
}

bool sqlsrv_v8_is_bool_type(SQLSMALLINT type)
{
    return type == SQL_BIT;
}

bool sqlsrv_v8_is_int_type(SQLSMALLINT type)
{
    return type == SQL_TINYINT || type == SQL_SMALLINT || type == SQL_INTEGER || type == SQL_BIGINT;
}

bool sqlsrv_v8_is_float_type(SQLSMALLINT type)
{
    return type == SQL_REAL || type == SQL_FLOAT || type == SQL_DOUBLE;
}

bool sqlsrv_v8_is_decimal_type(SQLSMALLINT type)
{
    return type == SQL_DECIMAL || type == SQL_NUMERIC;
}

bool sqlsrv_v8_is_date_type(SQLSMALLINT type)
{
    return type == SQL_TYPE_DATE || type == SQL_DATE;
}

bool sqlsrv_v8_is_time_type(SQLSMALLINT type)
{
    return type == SQL_TYPE_TIME || type == SQL_TIME || type == SQL_SS_TIME2;
}

bool sqlsrv_v8_is_timestamp_type(SQLSMALLINT type)
{
    return type == SQL_TYPE_TIMESTAMP || type == SQL_TIMESTAMP;
}

bool sqlsrv_v8_is_offset_datetime_type(SQLSMALLINT type)
{
    return type == SQL_SS_TIMESTAMPOFFSET;
}

bool sqlsrv_v8_is_uuid_type(SQLSMALLINT type)
{
#ifdef SQL_GUID
    return type == SQL_GUID;
#else
    (void)type;
    return false;
#endif
}

bool sqlsrv_v8_read_text_cell(SQLHSTMT stmt, SQLUSMALLINT column, SqlSrvV8Cell* out,
                              std::string* error)
{
    char buffer[4096];
    SQLLEN indicator = 0;
    out->kind = SqlSrvV8CellKind::Text;
    out->text.clear();
    for (;;) {
        buffer[0] = '\0';
        SQLRETURN rc =
            SQLGetData(stmt, column, SQL_C_CHAR, buffer, (SQLLEN)sizeof(buffer), &indicator);
        if (rc == SQL_NO_DATA) {
            return true;
        }
        if (!SQL_SUCCEEDED(rc)) {
            if (error != nullptr) {
                *error =
                    sqlsrv_v8_diag(SQL_HANDLE_STMT, stmt, "sqlserver value materialization failed");
            }
            return false;
        }
        if (indicator == SQL_NULL_DATA) {
            out->kind = SqlSrvV8CellKind::Null;
            out->text.clear();
            return true;
        }
        size_t chunk = 0U;
        while (chunk < sizeof(buffer) && buffer[chunk] != '\0') {
            chunk += 1U;
        }
        out->text.append(buffer, chunk);
        if (rc == SQL_SUCCESS) {
            return true;
        }
    }
}

bool sqlsrv_v8_read_binary_cell(SQLHSTMT stmt, SQLUSMALLINT column, SqlSrvV8Cell* out,
                                std::string* error)
{
    unsigned char buffer[4096];
    SQLLEN indicator = 0;
    out->kind = SqlSrvV8CellKind::Bytes;
    out->bytes.clear();
    for (;;) {
        SQLRETURN rc =
            SQLGetData(stmt, column, SQL_C_BINARY, buffer, (SQLLEN)sizeof(buffer), &indicator);
        if (rc == SQL_NO_DATA) {
            return true;
        }
        if (!SQL_SUCCEEDED(rc)) {
            if (error != nullptr) {
                *error = sqlsrv_v8_diag(SQL_HANDLE_STMT, stmt,
                                        "sqlserver binary materialization failed");
            }
            return false;
        }
        if (indicator == SQL_NULL_DATA) {
            out->kind = SqlSrvV8CellKind::Null;
            out->bytes.clear();
            return true;
        }
        size_t chunk = sizeof(buffer);
        if (indicator >= 0 && indicator < (SQLLEN)sizeof(buffer)) {
            chunk = (size_t)indicator;
        }
        out->bytes.insert(out->bytes.end(), buffer, buffer + chunk);
        if (rc == SQL_SUCCESS) {
            return true;
        }
    }
}

bool sqlsrv_v8_read_cell(SQLHSTMT stmt, const SqlSrvV8Column& column, SQLUSMALLINT index,
                         SqlSrvV8Cell* out, std::string* error)
{
    SQLLEN indicator = 0;
    if (sqlsrv_v8_is_binary_type(column.sql_type)) {
        return sqlsrv_v8_read_binary_cell(stmt, index, out, error);
    }
    if (sqlsrv_v8_is_bool_type(column.sql_type)) {
        unsigned char value = 0U;
        SQLRETURN rc = SQLGetData(stmt, index, SQL_C_BIT, &value, 0, &indicator);
        if (rc == SQL_NO_DATA || indicator == SQL_NULL_DATA) {
            out->kind = SqlSrvV8CellKind::Null;
            return true;
        }
        if (!SQL_SUCCEEDED(rc)) {
            if (error != nullptr) {
                *error =
                    sqlsrv_v8_diag(SQL_HANDLE_STMT, stmt, "sqlserver bool materialization failed");
            }
            return false;
        }
        out->kind = SqlSrvV8CellKind::Bool;
        out->bool_value = value != 0U;
        return true;
    }
    if (sqlsrv_v8_is_decimal_type(column.sql_type) || sqlsrv_v8_is_uuid_type(column.sql_type) ||
        sqlsrv_v8_is_date_type(column.sql_type) || sqlsrv_v8_is_time_type(column.sql_type) ||
        sqlsrv_v8_is_timestamp_type(column.sql_type) ||
        sqlsrv_v8_is_offset_datetime_type(column.sql_type))
    {
        if (!sqlsrv_v8_read_text_cell(stmt, index, out, error)) {
            return false;
        }
        if (out->kind == SqlSrvV8CellKind::Null) {
            return true;
        }
        if (sqlsrv_v8_is_decimal_type(column.sql_type)) {
            out->kind = SqlSrvV8CellKind::Decimal;
        }
        else if (sqlsrv_v8_is_uuid_type(column.sql_type)) {
            out->kind = SqlSrvV8CellKind::Uuid;
        }
        else if (sqlsrv_v8_is_date_type(column.sql_type)) {
            out->kind = SqlSrvV8CellKind::Date;
        }
        else if (sqlsrv_v8_is_time_type(column.sql_type)) {
            out->kind = SqlSrvV8CellKind::Time;
        }
        else if (sqlsrv_v8_is_offset_datetime_type(column.sql_type)) {
            out->kind = SqlSrvV8CellKind::OffsetDateTime;
        }
        else {
            out->kind = SqlSrvV8CellKind::LocalDateTime;
        }
        return true;
    }
    if (sqlsrv_v8_is_int_type(column.sql_type)) {
        int64_t value = 0;
        SQLRETURN rc = SQLGetData(stmt, index, SQL_C_SBIGINT, &value, 0, &indicator);
        if (rc == SQL_NO_DATA || indicator == SQL_NULL_DATA) {
            out->kind = SqlSrvV8CellKind::Null;
            return true;
        }
        if (!SQL_SUCCEEDED(rc)) {
            if (error != nullptr) {
                *error = sqlsrv_v8_diag(SQL_HANDLE_STMT, stmt,
                                        "sqlserver integer materialization failed");
            }
            return false;
        }
        out->kind =
            column.sql_type == SQL_BIGINT ? SqlSrvV8CellKind::Int64 : SqlSrvV8CellKind::Int32;
        out->int_value = value;
        return true;
    }
    if (sqlsrv_v8_is_float_type(column.sql_type)) {
        double value = 0.0;
        SQLRETURN rc = SQLGetData(stmt, index, SQL_C_DOUBLE, &value, 0, &indicator);
        if (rc == SQL_NO_DATA || indicator == SQL_NULL_DATA) {
            out->kind = SqlSrvV8CellKind::Null;
            return true;
        }
        if (!SQL_SUCCEEDED(rc)) {
            if (error != nullptr) {
                *error =
                    sqlsrv_v8_diag(SQL_HANDLE_STMT, stmt, "sqlserver float materialization failed");
            }
            return false;
        }
        out->kind = SqlSrvV8CellKind::Float64;
        out->float_value = value;
        return true;
    }
    return sqlsrv_v8_read_text_cell(stmt, index, out, error);
}

bool sqlsrv_v8_materialize_current_row(SqlSrvV8Request* request)
{
    std::vector<SqlSrvV8Cell> row;
    row.reserve(request->columns.size());
    for (size_t index = 0U; index < request->columns.size(); index += 1U) {
        SqlSrvV8Cell cell = {};
        if (!sqlsrv_v8_read_cell(request->stmt, request->columns[index],
                                 static_cast<SQLUSMALLINT>(index + 1U), &cell, &request->error))
        {
            return false;
        }
        row.push_back(std::move(cell));
    }
    request->rows.push_back(std::move(row));
    return true;
}

bool sqlsrv_v8_prepare_after_execute(SqlSrvV8Request* request)
{
    SQLLEN affected = -1;
    if (!sqlsrv_v8_describe_columns(request)) {
        return false;
    }
    if (!request->columns.empty() && sqlsrv_v8_result_is_query(request->operation)) {
        request->state = SqlSrvV8RequestState::Fetching;
        return true;
    }
    SQLRETURN rc = SQLRowCount(request->stmt, &affected);
    if (SQL_SUCCEEDED(rc) && affected >= 0) {
        request->affected_rows = affected;
        request->affected_rows_known = true;
    }
    else {
        request->affected_rows = -1;
        request->affected_rows_known = false;
    }
    request->state = SqlSrvV8RequestState::Terminal;
    sqlsrv_v8_settle_request(request->connection->request, true);
    return true;
}

void sqlsrv_v8_pump_connection(SqlSrvV8Connection* connection)
{
    if (connection == nullptr || connection->request == nullptr) {
        return;
    }
    std::shared_ptr<SqlSrvV8Request> request = connection->request;
    for (int steps = 0; steps < 32; steps += 1) {
        if (request->state == SqlSrvV8RequestState::Connecting) {
            SQLRETURN rc = sqlsrv_v8_connect(request.get());
            if (rc == SQL_STILL_EXECUTING) {
                request->connect_pending = true;
                if (!sqlsrv_v8_post_pump(request)) {
                    sqlsrv_v8_fail_request(request,
                                           "sqlserver async connection continuation failed");
                }
                return;
            }
            if (!SQL_SUCCEEDED(rc)) {
                sqlsrv_v8_fail_request(request, sqlsrv_v8_diag(SQL_HANDLE_DBC, connection->dbc,
                                                               "sqlserver connection failed"));
                return;
            }
            sqlsrv_v8_note_async_activity(request.get());
            connection->state = SqlSrvV8ConnectionState::Busy;
            request->state = SqlSrvV8RequestState::Executing;
        }
        if (request->state == SqlSrvV8RequestState::Executing) {
            if (request->stmt == SQL_NULL_HSTMT && !sqlsrv_v8_allocate_statement(request.get())) {
                sqlsrv_v8_fail_request(request, request->error);
                return;
            }
            SQLRETURN rc = sqlsrv_v8_execute(request.get());
            if (rc == SQL_STILL_EXECUTING) {
                request->execute_pending = true;
                if (!sqlsrv_v8_post_pump(request)) {
                    sqlsrv_v8_fail_request(request,
                                           "sqlserver async statement continuation failed");
                }
                return;
            }
            if (!SQL_SUCCEEDED(rc)) {
                sqlsrv_v8_fail_request(request, sqlsrv_v8_diag(SQL_HANDLE_STMT, request->stmt,
                                                               "sqlserver statement failed"));
                return;
            }
            sqlsrv_v8_note_async_activity(request.get());
            if (!sqlsrv_v8_prepare_after_execute(request.get())) {
                sqlsrv_v8_fail_request(request, request->error);
                return;
            }
            if (request->state != SqlSrvV8RequestState::Fetching) {
                return;
            }
        }
        if (request->state == SqlSrvV8RequestState::Fetching) {
            SQLRETURN rc = SQL_SUCCESS;
            if (request->fetch_pending) {
                RETCODE async_rc = SQL_ERROR;
                rc = SQLCompleteAsync(SQL_HANDLE_STMT, request->stmt, &async_rc);
                if (rc == SQL_STILL_EXECUTING ||
                    (SQL_SUCCEEDED(rc) && async_rc == SQL_STILL_EXECUTING))
                {
                    if (!sqlsrv_v8_post_pump(request)) {
                        sqlsrv_v8_fail_request(request,
                                               "sqlserver async fetch continuation failed");
                    }
                    return;
                }
                request->fetch_pending = false;
                if (!SQL_SUCCEEDED(rc)) {
                    sqlsrv_v8_fail_request(request, "sqlserver async fetch continuation failed");
                    return;
                }
                sqlsrv_v8_note_async_activity(request.get());
                rc = async_rc;
            }
            else {
                rc = SQLFetch(request->stmt);
            }
            if (rc == SQL_STILL_EXECUTING) {
                request->fetch_pending = true;
                if (!sqlsrv_v8_post_pump(request)) {
                    sqlsrv_v8_fail_request(request, "sqlserver async fetch continuation failed");
                }
                return;
            }
            if (rc == SQL_NO_DATA) {
                sqlsrv_v8_note_async_activity(request.get());
                request->state = SqlSrvV8RequestState::Terminal;
                sqlsrv_v8_settle_request(request, true);
                return;
            }
            if (!SQL_SUCCEEDED(rc)) {
                sqlsrv_v8_fail_request(request, sqlsrv_v8_diag(SQL_HANDLE_STMT, request->stmt,
                                                               "sqlserver fetch failed"));
                return;
            }
            if (sqlsrv_v8_operation_allows_max_rows(request->operation) &&
                request->rows.size() >= request->max_rows)
            {
                sqlsrv_v8_fail_request(request, "sqlserver provider query exceeded max rows");
                return;
            }
            if (!sqlsrv_v8_materialize_current_row(request.get())) {
                sqlsrv_v8_fail_request(request, request->error);
                return;
            }
            sqlsrv_v8_note_async_activity(request.get());
            if (request->operation == SqlSrvV8Operation::QueryOne ||
                request->operation == SqlSrvV8Operation::TransactionQueryOne)
            {
                request->state = SqlSrvV8RequestState::Terminal;
                sqlsrv_v8_settle_request(request, true);
                return;
            }
        }
    }
    if (!sqlsrv_v8_post_pump(request)) {
        sqlsrv_v8_fail_request(request, "sqlserver async continuation failed");
    }
}

SlStatus sqlsrv_v8_completion_dispatch(SlAsyncLoop* loop, const SlAsyncCompletion* completion,
                                       void* user)
{
    SqlSrvV8CompletionPayload* payload =
        completion == nullptr ? nullptr
                              : static_cast<SqlSrvV8CompletionPayload*>(completion->payload);
    (void)loop;
    (void)user;
    if (payload == nullptr || payload->connection == nullptr) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    sqlsrv_v8_pump_connection(payload->connection);
    return sl_status_ok();
}

bool sqlsrv_v8_attach_request_to_connection(SqlSrvV8ConnectionResource* resource,
                                            SqlSrvV8Connection* connection,
                                            const std::shared_ptr<SqlSrvV8Request>& request)
{
    if (resource == nullptr || connection == nullptr || request == nullptr) {
        return false;
    }
    connection->request = request;
    request->connection = connection;
    connection->state = SqlSrvV8ConnectionState::Busy;
    if (connection->dbc == SQL_NULL_HDBC || connection->state == SqlSrvV8ConnectionState::Empty ||
        connection->state == SqlSrvV8ConnectionState::Closed)
    {
        if (!sqlsrv_v8_allocate_connection(resource, connection, request)) {
            return false;
        }
        connection->state = SqlSrvV8ConnectionState::Connecting;
        request->state = SqlSrvV8RequestState::Connecting;
    }
    else {
        request->state = SqlSrvV8RequestState::Executing;
    }
    sqlsrv_v8_pump_connection(connection);
    return true;
}

SqlSrvV8Connection* sqlsrv_v8_acquire_connection(SqlSrvV8ConnectionResource* resource,
                                                 const std::shared_ptr<SqlSrvV8Request>& request)
{
    if (resource == nullptr || request == nullptr) {
        return nullptr;
    }
    if (request->operation == SqlSrvV8Operation::TransactionExec ||
        request->operation == SqlSrvV8Operation::TransactionQuery ||
        request->operation == SqlSrvV8Operation::TransactionQueryRaw ||
        request->operation == SqlSrvV8Operation::TransactionQueryOne ||
        request->operation == SqlSrvV8Operation::Commit ||
        request->operation == SqlSrvV8Operation::Rollback)
    {
        if (!resource->transaction_active ||
            resource->transaction_index >= resource->connections.size())
        {
            request->error = "sqlserver transaction is not active";
            return nullptr;
        }
        if (resource->connections[resource->transaction_index].request != nullptr) {
            request->error = "sqlserver transaction connection is busy";
            return nullptr;
        }
        return &resource->connections[resource->transaction_index];
    }
    for (SqlSrvV8Connection& connection : resource->connections) {
        if (connection.state == SqlSrvV8ConnectionState::Idle && connection.request == nullptr &&
            !connection.transaction_pinned)
        {
            return &connection;
        }
    }
    for (SqlSrvV8Connection& connection : resource->connections) {
        if (connection.state == SqlSrvV8ConnectionState::Empty ||
            connection.state == SqlSrvV8ConnectionState::Closed)
        {
            return &connection;
        }
    }
    request->error = "sqlserver provider pool is exhausted";
    return nullptr;
}

bool sqlsrv_v8_submit_request(const std::shared_ptr<SqlSrvV8Request>& request)
{
    SqlSrvV8ConnectionResource* resource = request == nullptr ? nullptr : request->resource;
    SqlSrvV8Connection* connection = sqlsrv_v8_acquire_connection(resource, request);
    if (connection == nullptr) {
        return false;
    }
    if (request->operation == SqlSrvV8Operation::Begin) {
        request->sql = "BEGIN TRANSACTION";
        for (size_t index = 0U; index < resource->connections.size(); index += 1U) {
            if (&resource->connections[index] == connection) {
                resource->transaction_index = index;
                break;
            }
        }
        resource->transaction_active = true;
    }
    else if (request->operation == SqlSrvV8Operation::Commit) {
        request->sql = "COMMIT TRANSACTION";
        request->transaction_terminal = true;
    }
    else if (request->operation == SqlSrvV8Operation::Rollback) {
        request->sql = "ROLLBACK TRANSACTION";
        request->transaction_terminal = true;
    }
    if (!sqlsrv_v8_attach_request_to_connection(resource, connection, request)) {
        return false;
    }
    sqlsrv_v8_start_timeout_watch(request);
    return true;
}

bool sqlsrv_v8_convert_db_value_param(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                      v8::Local<v8::Value> item, SqlSrvV8Param* param)
{
    std::string kind;
    v8::Local<v8::Value> kind_value;
    v8::Local<v8::Value> raw_value;
    v8::Local<v8::String> json_text;

    if (param == nullptr || !item->IsObject()) {
        return false;
    }
    v8::Local<v8::Object> object = item.As<v8::Object>();
    if (!sl_v8_db_get_object_property_key(isolate, context, object, SL_V8_DB_STRING_KIND,
                                          &kind_value) ||
        !kind_value->IsString() || !sl_v8_std_string_from_value(isolate, kind_value, &kind) ||
        !sl_v8_db_get_object_property_key(isolate, context, object, SL_V8_DB_STRING_VALUE,
                                          &raw_value))
    {
        return false;
    }
    if (kind == "json") {
        if (!v8::JSON::Stringify(context, raw_value).ToLocal(&json_text) ||
            !sl_v8_std_string_from_value(isolate, json_text, &param->text))
        {
            sqlsrv_v8_throw_type_error(isolate,
                                       "sqlserver json parameters must be JSON-serializable");
            return false;
        }
        param->kind = SqlSrvV8ParamKind::JsonText;
        param->sql_type = SQL_VARCHAR;
        return true;
    }
    if (kind == "rawJson") {
        if (!raw_value->IsString() ||
            !sl_v8_std_string_from_value(isolate, raw_value, &param->text))
        {
            return false;
        }
        param->kind = SqlSrvV8ParamKind::JsonText;
        param->sql_type = SQL_VARCHAR;
        return true;
    }
    if (kind == "bytes") {
        if (!sl_v8_db_copy_uint8_array(raw_value, &param->bytes)) {
            sqlsrv_v8_throw_type_error(isolate,
                                       "sqlserver bytes value wrapper must hold Uint8Array");
            return false;
        }
        param->kind = SqlSrvV8ParamKind::Bytes;
        return true;
    }
    if (!raw_value->IsString() || !sl_v8_std_string_from_value(isolate, raw_value, &param->text)) {
        return false;
    }
    if (kind == "decimal") {
        param->kind = SqlSrvV8ParamKind::Decimal;
        param->sql_type = SQL_DECIMAL;
    }
    else if (kind == "uuid") {
        param->kind = SqlSrvV8ParamKind::Uuid;
#ifdef SQL_GUID
        param->sql_type = SQL_GUID;
#else
        param->sql_type = SQL_VARCHAR;
#endif
    }
    else if (kind == "date") {
        param->kind = SqlSrvV8ParamKind::Date;
        param->sql_type = SQL_TYPE_DATE;
    }
    else if (kind == "time") {
        param->kind = SqlSrvV8ParamKind::Time;
        param->sql_type = SQL_SS_TIME2;
    }
    else if (kind == "localDateTime") {
        param->kind = SqlSrvV8ParamKind::LocalDateTime;
        param->sql_type = SQL_TYPE_TIMESTAMP;
    }
    else if (kind == "instant" || kind == "offsetDateTime") {
        param->kind = SqlSrvV8ParamKind::OffsetDateTime;
        param->sql_type = SQL_SS_TIMESTAMPOFFSET;
    }
    else {
        sqlsrv_v8_throw_type_error(isolate, "sqlserver sql value wrapper kind is not supported");
        return false;
    }
    return true;
}

bool sqlsrv_v8_convert_params(v8::Isolate* isolate, v8::Local<v8::Context> context,
                              v8::Local<v8::Value> value, std::vector<SqlSrvV8Param>* out)
{
    if (out == nullptr) {
        return false;
    }
    out->clear();
    if (value->IsUndefined() || value->IsNull()) {
        return true;
    }
    if (!value->IsArray()) {
        sqlsrv_v8_throw_type_error(isolate, "sqlserver parameters must be an array when supplied");
        return false;
    }
    v8::Local<v8::Array> array = value.As<v8::Array>();
    if (array->Length() > SL_SQLSERVER_MAX_PARAMS) {
        sqlsrv_v8_throw_type_error(isolate,
                                   "sqlserver parameter array exceeds supported parameter count");
        return false;
    }
    out->reserve(array->Length());
    for (uint32_t index = 0U; index < array->Length(); index += 1U) {
        v8::Local<v8::Value> item;
        SqlSrvV8Param param = {};
        if (!array->Get(context, index).ToLocal(&item)) {
            return false;
        }
        if (item->IsUndefined()) {
            sqlsrv_v8_throw_type_error(isolate,
                                       "sqlserver undefined parameters are not SQL NULL; use null");
            return false;
        }
        if (item->IsNull()) {
            param.kind = SqlSrvV8ParamKind::Null;
            param.indicator = SQL_NULL_DATA;
        }
        else if (item->IsBoolean()) {
            param.kind = SqlSrvV8ParamKind::Bool;
            param.bool_value = item->BooleanValue(isolate) ? 1U : 0U;
        }
        else if (item->IsNumber()) {
            double number = item.As<v8::Number>()->Value();
            if (!std::isfinite(number)) {
                sqlsrv_v8_throw_type_error(isolate, "sqlserver number parameters must be finite");
                return false;
            }
            if (std::trunc(number) == number &&
                (number < sqlsrv_v8_min_safe_integer || number > sqlsrv_v8_max_safe_integer))
            {
                sqlsrv_v8_throw_type_error(
                    isolate,
                    "sqlserver integer parameters outside JS safe integer range must use BigInt");
                return false;
            }
            if (std::trunc(number) == number) {
                param.kind = SqlSrvV8ParamKind::Int64;
                param.int_value = static_cast<int64_t>(number);
            }
            else {
                param.kind = SqlSrvV8ParamKind::Float64;
                param.float_value = number;
            }
        }
        else if (item->IsBigInt()) {
            bool lossless = false;
            int64_t value64 = item.As<v8::BigInt>()->Int64Value(&lossless);
            if (!lossless) {
                sqlsrv_v8_throw_type_error(isolate, "sqlserver bigint parameters must fit int64");
                return false;
            }
            param.kind = SqlSrvV8ParamKind::Int64;
            param.int_value = value64;
        }
        else if (item->IsUint8Array()) {
            if (!sl_v8_db_copy_uint8_array(item, &param.bytes)) {
                return false;
            }
            param.kind = SqlSrvV8ParamKind::Bytes;
        }
        else if (sqlsrv_v8_is_db_value(isolate, context, item)) {
            if (!sqlsrv_v8_convert_db_value_param(isolate, context, item, &param)) {
                return false;
            }
        }
        else if (item->IsString()) {
            param.kind = SqlSrvV8ParamKind::Text;
            if (!sqlsrv_v8_value_to_std_string(isolate, item, &param.text)) {
                return false;
            }
        }
        else {
            sqlsrv_v8_throw_type_error(
                isolate,
                "sqlserver parameters support null, boolean, number, bigint, string, bytes, and "
                "sql value wrappers");
            return false;
        }
        out->push_back(std::move(param));
    }
    return true;
}

bool sqlsrv_v8_parse_open_options(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                  v8::Local<v8::Value> value, SqlSrvV8ConnectionResource* out)
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
    if (!sqlsrv_v8_get_optional_object_string(isolate, context, object, "connectionString",
                                              &out->connection_string, &connection_present) ||
        !sqlsrv_v8_get_optional_object_string(isolate, context, object, "access", &access,
                                              &access_present) ||
        !sqlsrv_v8_get_optional_object_string(isolate, context, object, "capability",
                                              &out->capability, &capability_present) ||
        !sqlsrv_v8_get_optional_object_string(isolate, context, object, "provider",
                                              &out->provider_token, &provider_present))
    {
        return false;
    }
    if (!connection_present && provider_present) {
        const SlPlanDataProvider* provider = sqlsrv_v8_find_provider(
            static_cast<SlV8Engine*>(isolate->GetData(0)),
            sl_str_from_parts(out->provider_token.data(), out->provider_token.size()));
        if (provider == nullptr || !sl_str_equal(provider->provider, sl_str_from_cstr("sqlserver")))
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
        out->capability = out->provider_token.empty() ? "data.sqlserver" : out->provider_token;
    }
    if (!access_present || access == "readwrite") {
        out->access = SL_SQLSERVER_ACCESS_READWRITE;
    }
    else if (access == "read") {
        out->access = SL_SQLSERVER_ACCESS_READ;
    }
    else {
        return false;
    }

    v8::Local<v8::String> max_key;
    v8::Local<v8::Value> max_value;
    v8::Maybe<bool> has_max = v8::Nothing<bool>();
    if (!sl_status_is_ok(
            sqlsrv_v8_to_local_string(isolate, sl_str_from_cstr("maxConnections"), &max_key)))
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
    if (out->max_connections == 0U ||
        out->max_connections > SL_SQLSERVER_MAX_RUNTIME_POOL_CONNECTIONS)
    {
        return false;
    }
    return true;
}

void sqlsrv_v8_open_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    std::unique_ptr<SqlSrvV8ConnectionResource> resource(new (std::nothrow)
                                                             SqlSrvV8ConnectionResource());
    SlResourceId id = sl_resource_id_invalid();
    SlDiag diag = {};
    v8::Local<v8::Object> handle;
    unsigned char storage[4096];
    SlArena arena = {};

    if (resource == nullptr) {
        sqlsrv_v8_throw_error(isolate, "sqlserver bridge could not allocate a connection resource");
        return;
    }
    if (args.Length() != 1 ||
        !sqlsrv_v8_parse_open_options(isolate, context, args[0], resource.get()))
    {
        sqlsrv_v8_throw_type_error(isolate, "__sloppy.data.sqlserver.open requires open options");
        return;
    }
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));
    if (!sl_status_is_ok(status) ||
        !sqlsrv_v8_check_capability(isolate, backend, &arena, resource.get(),
                                    sqlsrv_v8_open_capability_operation(resource->access)))
    {
        return;
    }
    try {
        resource->connections.resize(resource->max_connections);
    } catch (...) {
        sqlsrv_v8_throw_error(isolate, "sqlserver bridge could not allocate a connection resource");
        return;
    }
    status =
        sl_resource_table_insert(&backend->resources, SL_RESOURCE_KIND_SQLSERVER_CONNECTION,
                                 resource.get(), sqlsrv_v8_connection_cleanup, nullptr, &id, &diag);
    if (!sl_status_is_ok(status)) {
        sqlsrv_v8_connection_cleanup(resource.release(), nullptr);
        sqlsrv_v8_throw_error(isolate, "sqlserver resource registration failed");
        return;
    }
    resource.release();
    if (!sqlsrv_v8_make_resource_handle(isolate, context, id, &handle)) {
        sl_resource_table_close_kind(&backend->resources, id, SL_RESOURCE_KIND_SQLSERVER_CONNECTION,
                                     &diag);
        sqlsrv_v8_throw_error(isolate, "sqlserver resource registration failed");
        return;
    }
    args.GetReturnValue().Set(handle);
}

void sqlsrv_v8_close_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    SlResourceId id = {};
    SlDiag diag = {};
    SqlSrvV8ConnectionResource* resource = nullptr;

    if (args.Length() != 1 || !sqlsrv_v8_get_resource_id(isolate, context, args[0], &id)) {
        sqlsrv_v8_throw_type_error(isolate,
                                   "__sloppy.data.sqlserver.close requires a resource handle");
        return;
    }
    resource = sqlsrv_v8_lookup_connection(isolate, context, backend, args[0]);
    if (resource == nullptr) {
        return;
    }
    for (SqlSrvV8Connection& connection : resource->connections) {
        if (connection.request != nullptr || connection.transaction_pinned) {
            sqlsrv_v8_throw_error(isolate, "sqlserver connection has pending operations");
            return;
        }
    }
    SlStatus status = sl_resource_table_close_kind(&backend->resources, id,
                                                   SL_RESOURCE_KIND_SQLSERVER_CONNECTION, &diag);
    if (!sl_status_is_ok(status)) {
        sqlsrv_v8_throw_error(isolate, "sqlserver close failed");
    }
}

bool sqlsrv_v8_make_promise(v8::Isolate* isolate, v8::Local<v8::Context> context,
                            const std::shared_ptr<SqlSrvV8Request>& request,
                            v8::Local<v8::Promise>* out)
{
    if (request == nullptr || !sl_v8_db_make_promise(isolate, context, &request->resolver, out)) {
        return false;
    }
    return true;
}

void sqlsrv_v8_operation_callback(const v8::FunctionCallbackInfo<v8::Value>& args,
                                  SqlSrvV8Operation operation, const char* signature)
{
    v8::Isolate* isolate = args.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    SlV8Engine* backend = static_cast<SlV8Engine*>(isolate->GetData(0));
    SqlSrvV8ConnectionResource* resource = nullptr;
    auto request = std::make_shared<SqlSrvV8Request>();
    v8::Local<v8::Promise> promise;
    unsigned char storage[4096];
    SlArena arena = {};

    if (args.Length() < 1) {
        sqlsrv_v8_throw_type_error(isolate, signature);
        return;
    }
    resource = sqlsrv_v8_lookup_connection(isolate, context, backend, args[0]);
    if (resource == nullptr) {
        return;
    }
    if (operation != SqlSrvV8Operation::Begin && operation != SqlSrvV8Operation::Commit &&
        operation != SqlSrvV8Operation::Rollback)
    {
        if (args.Length() < 2 || !sqlsrv_v8_value_to_std_string(isolate, args[1], &request->sql) ||
            request->sql.empty())
        {
            sqlsrv_v8_throw_type_error(isolate, signature);
            return;
        }
        if (!sqlsrv_v8_convert_params(isolate, context,
                                      args.Length() >= 3 ? args[2] : v8::Undefined(isolate),
                                      &request->params))
        {
            return;
        }
        if (sqlsrv_v8_operation_allows_max_rows(operation) &&
            !sl_v8_db_parse_max_rows_option(
                isolate, context, args.Length() >= 4 ? args[3] : v8::Undefined(isolate),
                SL_SQLSERVER_DEFAULT_MAX_ROWS, &request->max_rows, "sqlserver query"))
        {
            return;
        }
        if (!sl_v8_db_parse_timeout_ms_option(
                isolate, context, args.Length() >= 4 ? args[3] : v8::Undefined(isolate),
                &request->has_timeout_ms, &request->timeout_ms, "sqlserver operation"))
        {
            return;
        }
    }
    request->backend = backend;
    request->resource = resource;
    request->operation = operation;
    if (!sqlsrv_v8_make_promise(isolate, context, request, &promise)) {
        sqlsrv_v8_throw_error(isolate, "sqlserver bridge could not create a promise");
        return;
    }
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));
    if (!sl_status_is_ok(status) ||
        !sqlsrv_v8_check_capability(isolate, backend, &arena, resource,
                                    sqlsrv_v8_request_capability(operation)))
    {
        return;
    }
    if (operation == SqlSrvV8Operation::Begin && resource->transaction_active) {
        sqlsrv_v8_throw_error(isolate, "sqlserver nested transactions are not supported");
        return;
    }
    if (!sqlsrv_v8_submit_request(request)) {
        sqlsrv_v8_finish_request(request, false);
        request->resolver.Reset(isolate, v8::Local<v8::Promise::Resolver>());
        sqlsrv_v8_throw_error(isolate, request->error.empty()
                                           ? "sqlserver operation could not start"
                                           : request->error);
        return;
    }
    args.GetReturnValue().Set(promise);
}

void sqlsrv_v8_exec_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    sqlsrv_v8_operation_callback(
        args, SqlSrvV8Operation::Exec,
        "__sloppy.data.sqlserver.exec requires a handle, SQL string, and optional params");
}

void sqlsrv_v8_query_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    sqlsrv_v8_operation_callback(
        args, SqlSrvV8Operation::Query,
        "__sloppy.data.sqlserver.query requires a handle, SQL string, and optional params");
}

void sqlsrv_v8_query_raw_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    sqlsrv_v8_operation_callback(
        args, SqlSrvV8Operation::QueryRaw,
        "__sloppy.data.sqlserver.queryRaw requires a handle, SQL string, and optional params");
}

void sqlsrv_v8_query_one_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    sqlsrv_v8_operation_callback(
        args, SqlSrvV8Operation::QueryOne,
        "__sloppy.data.sqlserver.queryOne requires a handle, SQL string, and optional params");
}

void sqlsrv_v8_begin_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    sqlsrv_v8_operation_callback(args, SqlSrvV8Operation::Begin,
                                 "__sloppy.data.sqlserver.transactionBegin requires a handle");
}

void sqlsrv_v8_commit_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    sqlsrv_v8_operation_callback(args, SqlSrvV8Operation::Commit,
                                 "__sloppy.data.sqlserver.transactionCommit requires a handle");
}

void sqlsrv_v8_rollback_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    sqlsrv_v8_operation_callback(args, SqlSrvV8Operation::Rollback,
                                 "__sloppy.data.sqlserver.transactionRollback requires a handle");
}

void sqlsrv_v8_transaction_exec_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    sqlsrv_v8_operation_callback(args, SqlSrvV8Operation::TransactionExec,
                                 "__sloppy.data.sqlserver.transactionExec requires a handle, SQL "
                                 "string, and optional params");
}

void sqlsrv_v8_transaction_query_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    sqlsrv_v8_operation_callback(args, SqlSrvV8Operation::TransactionQuery,
                                 "__sloppy.data.sqlserver.transactionQuery requires a handle, SQL "
                                 "string, and optional params");
}

void sqlsrv_v8_transaction_query_raw_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    sqlsrv_v8_operation_callback(args, SqlSrvV8Operation::TransactionQueryRaw,
                                 "__sloppy.data.sqlserver.transactionQueryRaw requires a handle, "
                                 "SQL string, and optional params");
}

void sqlsrv_v8_transaction_query_one_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    sqlsrv_v8_operation_callback(args, SqlSrvV8Operation::TransactionQueryOne,
                                 "__sloppy.data.sqlserver.transactionQueryOne requires a handle, "
                                 "SQL string, and optional params");
}

#else

void sqlsrv_v8_open_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    sqlsrv_v8_throw_error(args.GetIsolate(),
                          "sqlserver ODBC provider is unavailable in this build");
}

void sqlsrv_v8_close_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    sqlsrv_v8_throw_error(args.GetIsolate(),
                          "sqlserver ODBC provider is unavailable in this build");
}

void sqlsrv_v8_exec_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    sqlsrv_v8_open_callback(args);
}

void sqlsrv_v8_query_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    sqlsrv_v8_open_callback(args);
}

void sqlsrv_v8_query_raw_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    sqlsrv_v8_open_callback(args);
}

void sqlsrv_v8_query_one_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    sqlsrv_v8_open_callback(args);
}

void sqlsrv_v8_begin_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    sqlsrv_v8_open_callback(args);
}

void sqlsrv_v8_commit_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    sqlsrv_v8_open_callback(args);
}

void sqlsrv_v8_rollback_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    sqlsrv_v8_open_callback(args);
}

void sqlsrv_v8_transaction_exec_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    sqlsrv_v8_open_callback(args);
}

void sqlsrv_v8_transaction_query_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    sqlsrv_v8_open_callback(args);
}

void sqlsrv_v8_transaction_query_raw_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    sqlsrv_v8_open_callback(args);
}

void sqlsrv_v8_transaction_query_one_callback(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    sqlsrv_v8_open_callback(args);
}

#endif

bool sqlsrv_v8_set_function(v8::Isolate* isolate, v8::Local<v8::Context> context,
                            v8::Local<v8::Object> object, const char* name,
                            v8::FunctionCallback callback)
{
    v8::Local<v8::String> key;
    v8::Local<v8::Function> function;
    if (!sl_status_is_ok(sqlsrv_v8_to_local_string(isolate, sl_str_from_cstr(name), &key)) ||
        !v8::Function::New(context, callback).ToLocal(&function))
    {
        return false;
    }
    return object->Set(context, key, function).FromMaybe(false);
}

} // namespace

void sl_v8_append_sqlserver_external_references(std::vector<intptr_t>* refs)
{
    if (refs == nullptr) {
        return;
    }
    refs->push_back(reinterpret_cast<intptr_t>(sqlsrv_v8_open_callback));
    refs->push_back(reinterpret_cast<intptr_t>(sqlsrv_v8_close_callback));
    refs->push_back(reinterpret_cast<intptr_t>(sqlsrv_v8_exec_callback));
    refs->push_back(reinterpret_cast<intptr_t>(sqlsrv_v8_query_callback));
    refs->push_back(reinterpret_cast<intptr_t>(sqlsrv_v8_query_raw_callback));
    refs->push_back(reinterpret_cast<intptr_t>(sqlsrv_v8_query_one_callback));
    refs->push_back(reinterpret_cast<intptr_t>(sqlsrv_v8_begin_callback));
    refs->push_back(reinterpret_cast<intptr_t>(sqlsrv_v8_commit_callback));
    refs->push_back(reinterpret_cast<intptr_t>(sqlsrv_v8_rollback_callback));
    refs->push_back(reinterpret_cast<intptr_t>(sqlsrv_v8_transaction_exec_callback));
    refs->push_back(reinterpret_cast<intptr_t>(sqlsrv_v8_transaction_query_callback));
    refs->push_back(reinterpret_cast<intptr_t>(sqlsrv_v8_transaction_query_raw_callback));
    refs->push_back(reinterpret_cast<intptr_t>(sqlsrv_v8_transaction_query_one_callback));
}

size_t sl_v8_sqlserver_pending_native_activity(SlV8Engine* backend)
{
#ifdef SLOPPY_ENABLE_SQLSERVER_PROVIDER
    size_t pending = 0U;
    if (backend == nullptr || backend->resources.entries == nullptr) {
        return 0U;
    }
    for (size_t index = 0U; index < backend->resources.capacity; ++index) {
        SlResourceEntry* entry = &backend->resources.entries[index];
        if (!entry->occupied || entry->kind != SL_RESOURCE_KIND_SQLSERVER_CONNECTION ||
            entry->ptr == nullptr)
        {
            continue;
        }
        auto* resource = static_cast<SqlSrvV8ConnectionResource*>(entry->ptr);
        for (const SqlSrvV8Connection& connection : resource->connections) {
            if (connection.request != nullptr) {
                ++pending;
            }
        }
    }
    return pending;
#else
    (void)backend;
    return 0U;
#endif
}

bool sl_v8_install_sqlserver_intrinsics(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                        v8::Local<v8::Object> data)
{
    v8::Local<v8::Object> sqlserver = v8::Object::New(isolate);
    v8::Local<v8::String> sqlserver_key;

    if (isolate == nullptr || !sl_status_is_ok(sqlsrv_v8_to_local_string(
                                  isolate, sl_str_from_cstr("sqlserver"), &sqlserver_key)))
    {
        return false;
    }
    if (!sqlsrv_v8_set_function(isolate, context, sqlserver, "open", sqlsrv_v8_open_callback) ||
        !sqlsrv_v8_set_function(isolate, context, sqlserver, "close", sqlsrv_v8_close_callback) ||
        !sqlsrv_v8_set_function(isolate, context, sqlserver, "exec", sqlsrv_v8_exec_callback) ||
        !sqlsrv_v8_set_function(isolate, context, sqlserver, "query", sqlsrv_v8_query_callback) ||
        !sqlsrv_v8_set_function(isolate, context, sqlserver, "queryRaw",
                                sqlsrv_v8_query_raw_callback) ||
        !sqlsrv_v8_set_function(isolate, context, sqlserver, "queryOne",
                                sqlsrv_v8_query_one_callback) ||
        !sqlsrv_v8_set_function(isolate, context, sqlserver, "transactionBegin",
                                sqlsrv_v8_begin_callback) ||
        !sqlsrv_v8_set_function(isolate, context, sqlserver, "transactionCommit",
                                sqlsrv_v8_commit_callback) ||
        !sqlsrv_v8_set_function(isolate, context, sqlserver, "transactionRollback",
                                sqlsrv_v8_rollback_callback) ||
        !sqlsrv_v8_set_function(isolate, context, sqlserver, "transactionExec",
                                sqlsrv_v8_transaction_exec_callback) ||
        !sqlsrv_v8_set_function(isolate, context, sqlserver, "transactionQuery",
                                sqlsrv_v8_transaction_query_callback) ||
        !sqlsrv_v8_set_function(isolate, context, sqlserver, "transactionQueryRaw",
                                sqlsrv_v8_transaction_query_raw_callback) ||
        !sqlsrv_v8_set_function(isolate, context, sqlserver, "transactionQueryOne",
                                sqlsrv_v8_transaction_query_one_callback) ||
        !data->Set(context, sqlserver_key, sqlserver).FromMaybe(false))
    {
        return false;
    }
    return true;
}

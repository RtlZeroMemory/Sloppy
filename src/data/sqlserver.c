/*
 * src/data/sqlserver.c
 *
 * Implements Sloppy's bounded SQL Server provider boundary over ODBC.
 *
 * This module opens caller-owned connection wrappers, executes blocking ODBC calls, binds
 * lowered `?` parameters, materializes small results into caller-provided arenas, exposes
 * explicit transactions, provides a tiny bounded pool, and formats missing-driver
 * diagnostics. It remains the native C synchronous boundary. JavaScript provider work uses
 * src/engine/v8/intrinsics_sqlserver.cc, which enables ODBC asynchronous connection and
 * statement mode. This module does not add worker-pool offload, migrations, ORM behavior,
 * cancellation/deadlines, installer behavior, or JavaScript handles.
 *
 * Safety invariants:
 * - ODBC headers and native handle casts stay in this provider-specific file;
 * - statement handles are freed on every path;
 * - environment and connection handles are disconnected/freed through close/pool close;
 * - result rows, column names, diagnostics, and text values are copied into caller arenas;
 * - diagnostics never include unredacted connection strings.
 *
 * Tests: tests/unit/data/test_sqlserver.c.
 */
#include "sloppy/data_sqlserver.h"

#include "sloppy/checked_math.h"

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>

#ifdef SLOPPY_ENABLE_SQLSERVER_PROVIDER
#if defined(_WIN32) && !defined(_WINDOWS_)
/*
 * The Windows SDK ODBC headers require a handful of base Windows typedefs but do not pull
 * them in themselves. Keep the dependency provider-local without including windows.h in a
 * core/provider module.
 */
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

static SlStr sl_sqlsrv_literal(const char* ptr, size_t length)
{
    return sl_str_from_parts(ptr, length);
}

static bool sl_sqlsrv_str_valid(SlStr str)
{
    return str.length == 0U || str.ptr != NULL;
}

static SlStatus sl_sqlsrv_copy_str(SlArena* arena, SlStr src, SlStr* out)
{
    void* ptr = NULL;
    char* dst = NULL;
    SlStatus status;

    if (arena == NULL || out == NULL || !sl_sqlsrv_str_valid(src)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (src.length == 0U) {
        *out = sl_str_empty();
        return sl_status_ok();
    }
    status = sl_arena_alloc(arena, src.length, 1U, &ptr);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    dst = (char*)ptr;
    for (size_t index = 0U; index < src.length; index += 1U) {
        dst[index] = src.ptr[index];
    }
    *out = sl_str_from_parts(dst, src.length);
    return sl_status_ok();
}

static SlStatus sl_sqlsrv_copy_bytes(SlArena* arena, SlBytes src, SlBytes* out)
{
    void* ptr = NULL;
    unsigned char* dst = NULL;
    SlStatus status;

    if (arena == NULL || out == NULL || (src.length != 0U && src.ptr == NULL)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (src.length == 0U) {
        *out = sl_bytes_empty();
        return sl_status_ok();
    }
    status = sl_arena_alloc(arena, src.length, 1U, &ptr);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    dst = (unsigned char*)ptr;
    for (size_t index = 0U; index < src.length; index += 1U) {
        dst[index] = src.ptr[index];
    }
    *out = sl_bytes_from_parts(dst, src.length);
    return sl_status_ok();
}

#ifdef SLOPPY_ENABLE_SQLSERVER_PROVIDER
static SlStatus sl_sqlsrv_copy_cstr(SlArena* arena, SlStr src, char** out)
{
    void* ptr = NULL;
    char* dst = NULL;
    size_t alloc_size = 0U;
    SlStatus status;

    if (arena == NULL || out == NULL || !sl_sqlsrv_str_valid(src)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_checked_add_size(src.length, 1U, &alloc_size);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(arena, alloc_size, 1U, &ptr);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    dst = (char*)ptr;
    for (size_t index = 0U; index < src.length; index += 1U) {
        dst[index] = src.ptr[index];
    }
    dst[src.length] = '\0';
    *out = dst;
    return sl_status_ok();
}

static bool sl_sqlsrv_has_case_insensitive_at(SlStr text, size_t index, const char* word)
{
    const char* start = text.ptr;

    if (word == NULL || index > text.length || !sl_sqlsrv_str_valid(text)) {
        return false;
    }
    if (index < text.length) {
        start = text.ptr + index;
    }

    return sl_str_starts_with_ci_ascii(sl_str_from_parts(start, text.length - index),
                                       sl_str_from_cstr(word));
}
#endif

static size_t sl_sqlsrv_skip_spaces(SlStr text, size_t index)
{
    while (index < text.length && (text.ptr[index] == ' ' || text.ptr[index] == '\t')) {
        index += 1U;
    }
    return index;
}

static size_t sl_sqlsrv_trim_trailing_spaces(SlStr text, size_t start, size_t end)
{
    while (end > start && (text.ptr[end - 1U] == ' ' || text.ptr[end - 1U] == '\t')) {
        end -= 1U;
    }
    return end;
}

static bool sl_sqlsrv_key_equal_ci(SlStr text, size_t start, size_t end, const char* key)
{
    const char* key_ptr = text.ptr;

    if (key == NULL || start > end || end > text.length || !sl_sqlsrv_str_valid(text)) {
        return false;
    }
    if (start < text.length) {
        key_ptr = text.ptr + start;
    }

    return sl_str_equal_ci_ascii(sl_str_from_parts(key_ptr, end - start), sl_str_from_cstr(key));
}

static bool sl_sqlsrv_is_secret_key(SlStr text, size_t key_start, size_t key_end)
{
    static const char* keys[] = {"password", "pwd", "access token", "accesstoken"};

    for (size_t key_index = 0U; key_index < sizeof(keys) / sizeof(keys[0]); key_index += 1U) {
        if (sl_sqlsrv_key_equal_ci(text, key_start, key_end, keys[key_index])) {
            return true;
        }
    }
    return false;
}

static size_t sl_sqlsrv_redact_odbc_value(char* dst, size_t length, size_t value_start)
{
    size_t cursor = value_start;

    if (cursor >= length) {
        return cursor;
    }
    if (dst[cursor] == '{') {
        dst[cursor] = '*';
        cursor += 1U;
        while (cursor < length) {
            const bool escaped =
                dst[cursor] == '}' && cursor + 1U < length && dst[cursor + 1U] == '}';

            if (dst[cursor] == '}' && !escaped) {
                dst[cursor] = '*';
                cursor += 1U;
                return cursor;
            }
            dst[cursor] = '*';
            cursor += 1U;
            if (escaped) {
                dst[cursor] = '*';
                cursor += 1U;
            }
        }
        return cursor;
    }
    while (cursor < length && dst[cursor] != ';') {
        dst[cursor] = '*';
        cursor += 1U;
    }
    return cursor;
}

SlStatus sl_sqlserver_redact_connection_string(SlArena* arena, SlStr connection_string, SlStr* out)
{
    void* ptr = NULL;
    char* dst = NULL;
    size_t alloc_size = 0U;
    SlStatus status;

    if (arena == NULL || out == NULL || !sl_sqlsrv_str_valid(connection_string)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_checked_add_size(connection_string.length, 1U, &alloc_size);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(arena, alloc_size, 1U, &ptr);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    dst = (char*)ptr;
    for (size_t index = 0U; index < connection_string.length; index += 1U) {
        dst[index] = connection_string.ptr[index];
    }
    dst[connection_string.length] = '\0';

    for (size_t index = 0U; index < connection_string.length;) {
        size_t key_start = 0U;
        size_t key_end = 0U;
        size_t value_start = 0U;

        while (index < connection_string.length && connection_string.ptr[index] == ';') {
            index += 1U;
        }
        key_start = sl_sqlsrv_skip_spaces(connection_string, index);
        index = key_start;
        while (index < connection_string.length && connection_string.ptr[index] != '=' &&
               connection_string.ptr[index] != ';')
        {
            index += 1U;
        }
        if (index >= connection_string.length || connection_string.ptr[index] != '=') {
            while (index < connection_string.length && connection_string.ptr[index] != ';') {
                index += 1U;
            }
            continue;
        }
        key_end = sl_sqlsrv_trim_trailing_spaces(connection_string, key_start, index);
        index += 1U;
        value_start = sl_sqlsrv_skip_spaces(connection_string, index);
        if (sl_sqlsrv_is_secret_key(connection_string, key_start, key_end)) {
            index = sl_sqlsrv_redact_odbc_value(dst, connection_string.length, value_start);
        }
        else {
            while (index < connection_string.length && connection_string.ptr[index] != ';') {
                index += 1U;
            }
        }
    }
    *out = sl_str_from_parts(dst, connection_string.length);
    return sl_status_ok();
}

SlStatus sl_sqlserver_extract_driver_name(SlArena* arena, SlStr connection_string, SlStr* out)
{
    size_t index = 0U;

    if (arena == NULL || out == NULL || !sl_sqlsrv_str_valid(connection_string)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = sl_str_empty();
    while (index < connection_string.length) {
        size_t key_start = index;
        size_t key_end = 0U;
        size_t value_start = 0U;
        size_t value_end = 0U;

        while (index < connection_string.length && connection_string.ptr[index] == ';') {
            index += 1U;
        }
        key_start = sl_sqlsrv_skip_spaces(connection_string, index);
        index = key_start;
        while (index < connection_string.length && connection_string.ptr[index] != '=' &&
               connection_string.ptr[index] != ';')
        {
            index += 1U;
        }
        if (index >= connection_string.length || connection_string.ptr[index] != '=') {
            while (index < connection_string.length && connection_string.ptr[index] != ';') {
                index += 1U;
            }
            continue;
        }
        key_end = sl_sqlsrv_trim_trailing_spaces(connection_string, key_start, index);
        if (!sl_sqlsrv_key_equal_ci(connection_string, key_start, key_end, "driver")) {
            index += 1U;
            while (index < connection_string.length && connection_string.ptr[index] != ';') {
                index += 1U;
            }
            continue;
        }
        index += 1U;
        index = sl_sqlsrv_skip_spaces(connection_string, index);
        value_start = index;
        if (index < connection_string.length && connection_string.ptr[index] == '{') {
            value_start = index + 1U;
            index += 1U;
            while (index < connection_string.length) {
                if (connection_string.ptr[index] == '}') {
                    if (index + 1U < connection_string.length &&
                        connection_string.ptr[index + 1U] == '}')
                    {
                        index += 2U;
                        continue;
                    }
                    value_end = index;
                    break;
                }
                index += 1U;
            }
            if (value_end == 0U) {
                value_end = connection_string.length;
            }
        }
        else {
            while (index < connection_string.length && connection_string.ptr[index] != ';') {
                index += 1U;
            }
            value_end = index;
            value_end = sl_sqlsrv_trim_trailing_spaces(connection_string, value_start, value_end);
        }
        return sl_sqlsrv_copy_str(
            arena, sl_str_from_parts(connection_string.ptr + value_start, value_end - value_start),
            out);
    }
    return sl_status_ok();
}

static SlStatus sl_sqlsrv_diag(SlArena* arena, SlDiag* out_diag, SlDiagCode code, SlStr message,
                               SlStr operation, SlStr hint_a, SlStr hint_b, SlStr hint_c,
                               SlStatus status)
{
    SlDiagBuilder builder;
    SlStatus diag_status;

    if (out_diag == NULL) {
        return status;
    }
    *out_diag = (SlDiag){0};
    if (arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    diag_status = sl_diag_builder_init(&builder, arena, SL_DIAG_SEVERITY_ERROR, code, message);
    if (!sl_status_is_ok(diag_status)) {
        return diag_status;
    }
    diag_status = sl_diag_builder_add_hint(
        &builder, sl_sqlsrv_literal("provider: sqlserver", sizeof("provider: sqlserver") - 1U));
    if (!sl_status_is_ok(diag_status)) {
        return diag_status;
    }
    diag_status = sl_diag_builder_add_hint(&builder, operation);
    if (!sl_status_is_ok(diag_status)) {
        return diag_status;
    }
    if (!sl_str_is_empty(hint_a)) {
        diag_status = sl_diag_builder_add_hint(&builder, hint_a);
        if (!sl_status_is_ok(diag_status)) {
            return diag_status;
        }
    }
    if (!sl_str_is_empty(hint_b)) {
        diag_status = sl_diag_builder_add_hint(&builder, hint_b);
        if (!sl_status_is_ok(diag_status)) {
            return diag_status;
        }
    }
    if (!sl_str_is_empty(hint_c)) {
        diag_status = sl_diag_builder_add_hint(&builder, hint_c);
        if (!sl_status_is_ok(diag_status)) {
            return diag_status;
        }
    }
    diag_status = sl_diag_builder_finish(&builder, out_diag);
    if (!sl_status_is_ok(diag_status)) {
        return diag_status;
    }
    return status;
}

SlSqlServerOpenOptions sl_sqlserver_open_options_connection_string(SlStr connection_string)
{
    SlSqlServerOpenOptions options;

    options.connection_string = connection_string;
    options.access = SL_SQLSERVER_ACCESS_READWRITE;
    return options;
}

SlSqlServerPoolOptions sl_sqlserver_pool_options_connection_string(SlStr connection_string,
                                                                   size_t max_connections)
{
    SlSqlServerPoolOptions options;

    options.connection_string = connection_string;
    options.access = SL_SQLSERVER_ACCESS_READWRITE;
    options.max_connections = max_connections;
    return options;
}

#ifdef SLOPPY_ENABLE_SQLSERVER_PROVIDER
static SlStatus sl_sqlsrv_safe_config_hint(SlArena* arena, SlStr connection_string, SlStr* out)
{
    SlStr redacted = sl_str_empty();

    return sl_sqlserver_redact_connection_string(arena, connection_string, &redacted).code ==
                   SL_STATUS_OK
               ? sl_sqlsrv_copy_str(arena, redacted, out)
               : sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
}

#endif

static SlStatus sl_sqlsrv_set_doctor(SlArena* arena, SlSqlServerDoctorResult* out_result, bool ok,
                                     SlStr driver_manager, SlStr driver, SlStr message,
                                     SlStr hint_a, SlStr hint_b)
{
    SlStatus status;

    if (arena == NULL || out_result == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_result = (SlSqlServerDoctorResult){0};
    out_result->ok = ok;
    status = sl_sqlsrv_copy_str(arena, sl_sqlsrv_literal("sqlserver", sizeof("sqlserver") - 1U),
                                &out_result->provider);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_sqlsrv_copy_str(arena, driver_manager, &out_result->driver_manager);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_sqlsrv_copy_str(arena, driver, &out_result->driver);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_sqlsrv_copy_str(arena, message, &out_result->message);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (!sl_str_is_empty(hint_a)) {
        status = sl_sqlsrv_copy_str(arena, hint_a, &out_result->hints[out_result->hint_count]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        out_result->hint_count += 1U;
    }
    if (!sl_str_is_empty(hint_b)) {
        status = sl_sqlsrv_copy_str(arena, hint_b, &out_result->hints[out_result->hint_count]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        out_result->hint_count += 1U;
    }
    return sl_status_ok();
}

#ifndef SLOPPY_ENABLE_SQLSERVER_PROVIDER

SlStatus sl_sqlserver_doctor(SlArena* arena, const SlSqlServerOpenOptions* options,
                             SlSqlServerDoctorResult* out_result, SlDiag* out_diag)
{
    (void)options;
    SlStatus status = sl_sqlsrv_set_doctor(
        arena, out_result, false, sl_sqlsrv_literal("unavailable", sizeof("unavailable") - 1U),
        sl_sqlsrv_literal("unchecked", sizeof("unchecked") - 1U),
        sl_sqlsrv_literal("SQL Server provider was built without ODBC support",
                          sizeof("SQL Server provider was built without ODBC support") - 1U),
        sl_sqlsrv_literal("enable SLOPPY_ENABLE_SQLSERVER with ODBC headers/libraries",
                          sizeof("enable SLOPPY_ENABLE_SQLSERVER with ODBC headers/libraries") -
                              1U),
        sl_str_empty());

    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_sqlsrv_diag(
        arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
        sl_sqlsrv_literal("sqlserver provider ODBC support is unavailable",
                          sizeof("sqlserver provider ODBC support is unavailable") - 1U),
        sl_sqlsrv_literal("operation: doctor", sizeof("operation: doctor") - 1U), sl_str_empty(),
        sl_sqlsrv_literal("enable SLOPPY_ENABLE_SQLSERVER with ODBC headers/libraries",
                          sizeof("enable SLOPPY_ENABLE_SQLSERVER with ODBC headers/libraries") -
                              1U),
        sl_str_empty(), sl_status_from_code(SL_STATUS_UNSUPPORTED));
}

SlStatus sl_sqlserver_open(SlArena* diag_arena, const SlSqlServerOpenOptions* options,
                           SlSqlServerConnection* out_connection, SlDiag* out_diag)
{
    (void)options;
    if (out_connection != NULL) {
        *out_connection = (SlSqlServerConnection){0};
    }
    return sl_sqlsrv_diag(
        diag_arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
        sl_sqlsrv_literal("sqlserver provider ODBC support is unavailable",
                          sizeof("sqlserver provider ODBC support is unavailable") - 1U),
        sl_sqlsrv_literal("operation: open", sizeof("operation: open") - 1U), sl_str_empty(),
        sl_sqlsrv_literal("enable SLOPPY_ENABLE_SQLSERVER with ODBC headers/libraries",
                          sizeof("enable SLOPPY_ENABLE_SQLSERVER with ODBC headers/libraries") -
                              1U),
        sl_str_empty(), sl_status_from_code(SL_STATUS_UNSUPPORTED));
}

SlStatus sl_sqlserver_close(SlSqlServerConnection* connection)
{
    if (connection == NULL || !connection->open) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    *connection = (SlSqlServerConnection){0};
    return sl_status_ok();
}

SlStatus sl_sqlserver_exec(SlArena* arena, SlSqlServerConnection* connection, SlStr sql,
                           const SlSqlServerParam* params, size_t param_count,
                           SlSqlServerExecResult* out_result, SlDiag* out_diag)
{
    (void)connection;
    (void)sql;
    (void)params;
    (void)param_count;
    if (out_result != NULL) {
        *out_result = (SlSqlServerExecResult){0};
    }
    return sl_sqlsrv_diag(
        arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
        sl_sqlsrv_literal("sqlserver provider ODBC support is unavailable",
                          sizeof("sqlserver provider ODBC support is unavailable") - 1U),
        sl_sqlsrv_literal("operation: exec", sizeof("operation: exec") - 1U), sl_str_empty(),
        sl_str_empty(), sl_str_empty(), sl_status_from_code(SL_STATUS_UNSUPPORTED));
}

SlStatus sl_sqlserver_query(SlArena* arena, SlSqlServerConnection* connection, SlStr sql,
                            const SlSqlServerParam* params, size_t param_count,
                            const SlSqlServerQueryOptions* options, SlSqlServerResult* out_result,
                            SlDiag* out_diag)
{
    (void)connection;
    (void)sql;
    (void)params;
    (void)param_count;
    (void)options;
    if (out_result != NULL) {
        *out_result = (SlSqlServerResult){0};
    }
    return sl_sqlsrv_diag(
        arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
        sl_sqlsrv_literal("sqlserver provider ODBC support is unavailable",
                          sizeof("sqlserver provider ODBC support is unavailable") - 1U),
        sl_sqlsrv_literal("operation: query", sizeof("operation: query") - 1U), sl_str_empty(),
        sl_str_empty(), sl_str_empty(), sl_status_from_code(SL_STATUS_UNSUPPORTED));
}

SlStatus sl_sqlserver_query_one(SlArena* arena, SlSqlServerConnection* connection, SlStr sql,
                                const SlSqlServerParam* params, size_t param_count,
                                SlSqlServerQueryOneResult* out_result, SlDiag* out_diag)
{
    (void)connection;
    (void)sql;
    (void)params;
    (void)param_count;
    if (out_result != NULL) {
        *out_result = (SlSqlServerQueryOneResult){0};
    }
    return sl_sqlsrv_diag(
        arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
        sl_sqlsrv_literal("sqlserver provider ODBC support is unavailable",
                          sizeof("sqlserver provider ODBC support is unavailable") - 1U),
        sl_sqlsrv_literal("operation: queryOne", sizeof("operation: queryOne") - 1U),
        sl_str_empty(), sl_str_empty(), sl_str_empty(), sl_status_from_code(SL_STATUS_UNSUPPORTED));
}

#else

static SQLHDBC sl_sqlsrv_dbc(SlSqlServerConnection* connection)
{
    if (connection == NULL || !connection->open) {
        return SQL_NULL_HDBC;
    }
    return (SQLHDBC)connection->dbc_handle;
}

static bool sl_sqlsrv_success(SQLRETURN rc)
{
    return rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO;
}

static SlStatus sl_sqlsrv_diag_from_handle(SlArena* arena, SlDiag* out_diag, SlDiagCode code,
                                           SlStr message, SlStr operation, SQLSMALLINT type,
                                           SQLHANDLE handle, SlStr safe_config, SlStr sql,
                                           SlStatus status)
{
    char state[6] = {0};
    char text[512] = {0};
    SQLINTEGER native_error = 0;
    SQLSMALLINT text_length = 0;
    SlStr detail = sl_str_empty();
    SQLRETURN rc;

    rc = SQLGetDiagRecA(type, handle, 1, (SQLCHAR*)state, &native_error, (SQLCHAR*)text,
                        (SQLSMALLINT)sizeof(text), &text_length);
    if (sl_sqlsrv_success(rc)) {
        detail = sl_str_from_cstr(text);
        if (sl_sqlsrv_has_case_insensitive_at(detail, 0U, "[microsoft]")) {
            /* Keep driver details as hints, but never combine them with secrets. */
        }
    }
    if (!sl_str_is_empty(safe_config)) {
        return sl_sqlsrv_diag(arena, out_diag, code, message, operation, safe_config, detail, sql,
                              status);
    }
    return sl_sqlsrv_diag(arena, out_diag, code, message, operation, detail, sql, sl_str_empty(),
                          status);
}

static SlStatus sl_sqlsrv_invalid_state_diag(SlArena* arena, SlDiag* out_diag, SlStr operation)
{
    return sl_sqlsrv_diag(
        arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
        sl_sqlsrv_literal("sqlserver provider resource is closed or inactive",
                          sizeof("sqlserver provider resource is closed or inactive") - 1U),
        operation, sl_str_empty(), sl_str_empty(), sl_str_empty(),
        sl_status_from_code(SL_STATUS_INVALID_STATE));
}

static SlStatus sl_sqlsrv_alloc_env(SQLHENV* out_env, SlDiag* out_diag, SlArena* arena,
                                    SlStr operation)
{
    SQLHENV env = SQL_NULL_HENV;
    SQLRETURN rc;

    if (out_env == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_env = SQL_NULL_HENV;
    rc = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);
    if (!sl_sqlsrv_success(rc)) {
        return sl_sqlsrv_diag(
            arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
            sl_sqlsrv_literal("ODBC driver manager is unavailable",
                              sizeof("ODBC driver manager is unavailable") - 1U),
            operation,
            sl_sqlsrv_literal("install or repair the platform ODBC driver manager",
                              sizeof("install or repair the platform ODBC driver manager") - 1U),
            sl_str_empty(), sl_str_empty(), sl_status_from_code(SL_STATUS_UNSUPPORTED));
    }
    rc = SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
    if (!sl_sqlsrv_success(rc)) {
        SlStatus status = sl_sqlsrv_diag_from_handle(
            arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
            sl_sqlsrv_literal("ODBC driver manager rejected ODBC 3 mode",
                              sizeof("ODBC driver manager rejected ODBC 3 mode") - 1U),
            operation, SQL_HANDLE_ENV, env, sl_str_empty(), sl_str_empty(),
            sl_status_from_code(SL_STATUS_UNSUPPORTED));
        SQLFreeHandle(SQL_HANDLE_ENV, env);
        return status;
    }
    *out_env = env;
    return sl_status_ok();
}

static bool sl_sqlsrv_driver_name_equal(const char* actual, SlStr expected)
{
    if (actual == NULL || !sl_sqlsrv_str_valid(expected)) {
        return false;
    }

    return sl_str_equal_ci_ascii(sl_str_from_cstr(actual), expected);
}

static SlStatus sl_sqlsrv_driver_visible(SlArena* arena, SQLHENV env, SlStr driver_name,
                                         bool* out_visible, SlDiag* out_diag)
{
    SQLUSMALLINT direction = SQL_FETCH_FIRST;
    char description[512] = {0};
    char attributes[1024] = {0};
    SQLSMALLINT description_length = 0;
    SQLSMALLINT attributes_length = 0;
    SQLRETURN rc;

    if (out_visible == NULL || !sl_sqlsrv_str_valid(driver_name)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_visible = false;
    while (true) {
        rc = SQLDriversA(env, direction, (SQLCHAR*)description, (SQLSMALLINT)sizeof(description),
                         &description_length, (SQLCHAR*)attributes, (SQLSMALLINT)sizeof(attributes),
                         &attributes_length);
        if (rc == SQL_NO_DATA) {
            return sl_status_ok();
        }
        if (!sl_sqlsrv_success(rc)) {
            return sl_sqlsrv_diag_from_handle(
                arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
                sl_sqlsrv_literal("ODBC driver enumeration failed",
                                  sizeof("ODBC driver enumeration failed") - 1U),
                sl_sqlsrv_literal("operation: doctor", sizeof("operation: doctor") - 1U),
                SQL_HANDLE_ENV, env, sl_str_empty(), sl_str_empty(),
                sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
        }
        if (sl_sqlsrv_driver_name_equal(description, driver_name)) {
            *out_visible = true;
            return sl_status_ok();
        }
        direction = SQL_FETCH_NEXT;
    }
}

SlStatus sl_sqlserver_doctor(SlArena* arena, const SlSqlServerOpenOptions* options,
                             SlSqlServerDoctorResult* out_result, SlDiag* out_diag)
{
    SQLHENV env = SQL_NULL_HENV;
    SlStr driver = sl_str_empty();
    bool visible = false;
    SlStatus status;

    if (options == NULL || !sl_sqlsrv_str_valid(options->connection_string) ||
        sl_str_is_empty(options->connection_string))
    {
        status = sl_sqlsrv_set_doctor(
            arena, out_result, false, sl_sqlsrv_literal("available", sizeof("available") - 1U),
            sl_sqlsrv_literal("unknown", sizeof("unknown") - 1U),
            sl_sqlsrv_literal("SQL Server connectionString is required",
                              sizeof("SQL Server connectionString is required") - 1U),
            sl_sqlsrv_literal("check connection string", sizeof("check connection string") - 1U),
            sl_str_empty());
        if (!sl_status_is_ok(status)) {
            return status;
        }
        return sl_sqlsrv_diag(
            arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
            sl_sqlsrv_literal("sqlserver provider connection string is required",
                              sizeof("sqlserver provider connection string is required") - 1U),
            sl_sqlsrv_literal("operation: doctor", sizeof("operation: doctor") - 1U),
            sl_str_empty(), sl_str_empty(), sl_str_empty(),
            sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
    }
    if (options->access != SL_SQLSERVER_ACCESS_READ &&
        options->access != SL_SQLSERVER_ACCESS_READWRITE)
    {
        return sl_sqlsrv_diag(
            arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
            sl_sqlsrv_literal("sqlserver provider access option is invalid",
                              sizeof("sqlserver provider access option is invalid") - 1U),
            sl_sqlsrv_literal("operation: doctor", sizeof("operation: doctor") - 1U),
            sl_str_empty(), sl_str_empty(), sl_str_empty(),
            sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
    }
    status = sl_sqlserver_extract_driver_name(arena, options->connection_string, &driver);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (sl_str_is_empty(driver)) {
        status = sl_sqlsrv_set_doctor(
            arena, out_result, false, sl_sqlsrv_literal("available", sizeof("available") - 1U),
            sl_sqlsrv_literal("unknown", sizeof("unknown") - 1U),
            sl_sqlsrv_literal("SQL Server ODBC Driver name is missing",
                              sizeof("SQL Server ODBC Driver name is missing") - 1U),
            sl_sqlsrv_literal("use Driver={ODBC Driver 18 for SQL Server}",
                              sizeof("use Driver={ODBC Driver 18 for SQL Server}") - 1U),
            sl_sqlsrv_literal("check connection string", sizeof("check connection string") - 1U));
        if (!sl_status_is_ok(status)) {
            return status;
        }
        return sl_sqlsrv_diag(
            arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
            sl_sqlsrv_literal("sqlserver provider driver name is required",
                              sizeof("sqlserver provider driver name is required") - 1U),
            sl_sqlsrv_literal("operation: doctor", sizeof("operation: doctor") - 1U),
            sl_sqlsrv_literal("use Driver={ODBC Driver 18 for SQL Server}",
                              sizeof("use Driver={ODBC Driver 18 for SQL Server}") - 1U),
            sl_str_empty(), sl_str_empty(), sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
    }
    status = sl_sqlsrv_alloc_env(
        &env, out_diag, arena,
        sl_sqlsrv_literal("operation: doctor", sizeof("operation: doctor") - 1U));
    if (!sl_status_is_ok(status)) {
        (void)sl_sqlsrv_set_doctor(
            arena, out_result, false, sl_sqlsrv_literal("unavailable", sizeof("unavailable") - 1U),
            sl_sqlsrv_literal("unchecked", sizeof("unchecked") - 1U),
            sl_sqlsrv_literal("ODBC driver manager is unavailable",
                              sizeof("ODBC driver manager is unavailable") - 1U),
            sl_sqlsrv_literal("install or repair the platform ODBC driver manager",
                              sizeof("install or repair the platform ODBC driver manager") - 1U),
            sl_str_empty());
        return status;
    }
    status = sl_sqlsrv_driver_visible(arena, env, driver, &visible, out_diag);
    SQLFreeHandle(SQL_HANDLE_ENV, env);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (!visible) {
        status = sl_sqlsrv_set_doctor(
            arena, out_result, false, sl_sqlsrv_literal("available", sizeof("available") - 1U),
            sl_sqlsrv_literal("missing", sizeof("missing") - 1U),
            sl_sqlsrv_literal("Microsoft ODBC Driver for SQL Server was not found",
                              sizeof("Microsoft ODBC Driver for SQL Server was not found") - 1U),
            sl_sqlsrv_literal("install Microsoft ODBC Driver 18 for SQL Server",
                              sizeof("install Microsoft ODBC Driver 18 for SQL Server") - 1U),
            sl_sqlsrv_literal("check driver name", sizeof("check driver name") - 1U));
        if (!sl_status_is_ok(status)) {
            return status;
        }
        return sl_sqlsrv_diag(
            arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
            sl_sqlsrv_literal("sqlserver provider ODBC driver is missing",
                              sizeof("sqlserver provider ODBC driver is missing") - 1U),
            sl_sqlsrv_literal("operation: doctor", sizeof("operation: doctor") - 1U), driver,
            sl_sqlsrv_literal("install Microsoft ODBC Driver 18 for SQL Server",
                              sizeof("install Microsoft ODBC Driver 18 for SQL Server") - 1U),
            sl_str_empty(), sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
    }
    return sl_sqlsrv_set_doctor(
        arena, out_result, true, sl_sqlsrv_literal("available", sizeof("available") - 1U),
        sl_sqlsrv_literal("available", sizeof("available") - 1U),
        sl_sqlsrv_literal("SQL Server ODBC driver is visible",
                          sizeof("SQL Server ODBC driver is visible") - 1U),
        sl_sqlsrv_literal("verify SQL Server is reachable",
                          sizeof("verify SQL Server is reachable") - 1U),
        sl_sqlsrv_literal(
            "use TrustServerCertificate=yes for local dev only when appropriate",
            sizeof("use TrustServerCertificate=yes for local dev only when appropriate") - 1U));
}

SlStatus sl_sqlserver_open(SlArena* diag_arena, const SlSqlServerOpenOptions* options,
                           SlSqlServerConnection* out_connection, SlDiag* out_diag)
{
    SQLHENV env = SQL_NULL_HENV;
    SQLHDBC dbc = SQL_NULL_HDBC;
    char* connection_string = NULL;
    SlStr safe = sl_str_empty();
    SlArenaMark mark;
    SQLRETURN rc;
    SlStatus status;

    if (out_connection == NULL || options == NULL ||
        !sl_sqlsrv_str_valid(options->connection_string) ||
        sl_str_is_empty(options->connection_string))
    {
        return sl_sqlsrv_diag(
            diag_arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
            sl_sqlsrv_literal("sqlserver provider connection string is required",
                              sizeof("sqlserver provider connection string is required") - 1U),
            sl_sqlsrv_literal("operation: open", sizeof("operation: open") - 1U), sl_str_empty(),
            sl_str_empty(), sl_str_empty(), sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
    }
    if (options->access != SL_SQLSERVER_ACCESS_READ &&
        options->access != SL_SQLSERVER_ACCESS_READWRITE)
    {
        return sl_sqlsrv_diag(
            diag_arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
            sl_sqlsrv_literal("sqlserver provider access option is invalid",
                              sizeof("sqlserver provider access option is invalid") - 1U),
            sl_sqlsrv_literal("operation: open", sizeof("operation: open") - 1U), sl_str_empty(),
            sl_str_empty(), sl_str_empty(), sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
    }
    *out_connection = (SlSqlServerConnection){0};
    mark = sl_arena_mark(diag_arena);
    status = sl_sqlsrv_copy_cstr(diag_arena, options->connection_string, &connection_string);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_sqlsrv_safe_config_hint(diag_arena, options->connection_string, &safe);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status =
        sl_sqlsrv_alloc_env(&env, out_diag, diag_arena,
                            sl_sqlsrv_literal("operation: open", sizeof("operation: open") - 1U));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    rc = SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc);
    if (!sl_sqlsrv_success(rc)) {
        status = sl_sqlsrv_diag_from_handle(
            diag_arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
            sl_sqlsrv_literal("sqlserver provider connection handle allocation failed",
                              sizeof("sqlserver provider connection handle allocation failed") -
                                  1U),
            sl_sqlsrv_literal("operation: open", sizeof("operation: open") - 1U), SQL_HANDLE_ENV,
            env, safe, sl_str_empty(), sl_status_from_code(SL_STATUS_OUT_OF_MEMORY));
        SQLFreeHandle(SQL_HANDLE_ENV, env);
        return status;
    }
    rc = SQLSetConnectAttr(dbc, SQL_ATTR_ACCESS_MODE,
                           (SQLPOINTER)(uintptr_t)(options->access == SL_SQLSERVER_ACCESS_READ
                                                       ? SQL_MODE_READ_ONLY
                                                       : SQL_MODE_READ_WRITE),
                           0);
    if (!sl_sqlsrv_success(rc)) {
        status = sl_sqlsrv_diag_from_handle(
            diag_arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
            sl_sqlsrv_literal("sqlserver provider access mode setup failed",
                              sizeof("sqlserver provider access mode setup failed") - 1U),
            sl_sqlsrv_literal("operation: open", sizeof("operation: open") - 1U), SQL_HANDLE_DBC,
            dbc, safe, sl_str_empty(), sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
        SQLFreeHandle(SQL_HANDLE_DBC, dbc);
        SQLFreeHandle(SQL_HANDLE_ENV, env);
        return status;
    }
    rc = SQLDriverConnectA(dbc, NULL, (SQLCHAR*)connection_string, SQL_NTS, NULL, 0, NULL,
                           SQL_DRIVER_NOPROMPT);
    if (!sl_sqlsrv_success(rc)) {
        status = sl_sqlsrv_diag_from_handle(
            diag_arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
            sl_sqlsrv_literal("sqlserver provider connection failed",
                              sizeof("sqlserver provider connection failed") - 1U),
            sl_sqlsrv_literal("operation: open", sizeof("operation: open") - 1U), SQL_HANDLE_DBC,
            dbc, safe,
            sl_sqlsrv_literal(
                "check connection string, authentication, network reachability, database name, and "
                "TLS/TrustServerCertificate settings",
                sizeof("check connection string, authentication, network reachability, database "
                       "name, and TLS/TrustServerCertificate settings") -
                    1U),
            sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
        SQLFreeHandle(SQL_HANDLE_DBC, dbc);
        SQLFreeHandle(SQL_HANDLE_ENV, env);
        return status;
    }
    out_connection->env_handle = env;
    out_connection->dbc_handle = dbc;
    out_connection->open = true;
    out_connection->transaction_active = false;
    out_connection->access = options->access;
    (void)sl_arena_reset_to(diag_arena, mark);
    return sl_status_ok();
}

SlStatus sl_sqlserver_close(SlSqlServerConnection* connection)
{
    SQLHDBC dbc;
    SQLHENV env;

    if (connection == NULL || !connection->open) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    dbc = (SQLHDBC)connection->dbc_handle;
    env = (SQLHENV)connection->env_handle;
    if (connection->transaction_active) {
        (void)SQLEndTran(SQL_HANDLE_DBC, dbc, SQL_ROLLBACK);
    }
    (void)SQLDisconnect(dbc);
    (void)SQLFreeHandle(SQL_HANDLE_DBC, dbc);
    (void)SQLFreeHandle(SQL_HANDLE_ENV, env);
    *connection = (SlSqlServerConnection){0};
    return sl_status_ok();
}

static SlStatus sl_sqlsrv_bind_params(SlArena* arena, SQLHSTMT stmt, const SlSqlServerParam* params,
                                      size_t param_count, SlDiag* out_diag, SlStr operation,
                                      SlStr sql)
{
    SQLSMALLINT bind_count = 0;
    SQLRETURN rc;
    SQLLEN indicators[SL_SQLSERVER_MAX_PARAMS] = {0};
    SQLBIGINT integers[SL_SQLSERVER_MAX_PARAMS] = {0};
    double numbers[SL_SQLSERVER_MAX_PARAMS] = {0};
    unsigned char booleans[SL_SQLSERVER_MAX_PARAMS] = {0};
    char empty_text[1] = {0};
    unsigned char empty_bytes[1] = {0};

    if (param_count > SL_SQLSERVER_MAX_PARAMS || (param_count > 0U && params == NULL)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    rc = SQLNumParams(stmt, &bind_count);
    if (sl_sqlsrv_success(rc) && bind_count >= 0 && (size_t)bind_count != param_count) {
        return sl_sqlsrv_diag(
            arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
            sl_sqlsrv_literal("sqlserver provider parameter count mismatch",
                              sizeof("sqlserver provider parameter count mismatch") - 1U),
            operation, sql, sl_str_empty(), sl_str_empty(),
            sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
    }
    for (size_t index = 0U; index < param_count; index += 1U) {
        const SQLUSMALLINT sql_index = (SQLUSMALLINT)(index + 1U);
        const SlSqlServerParam* param = &params[index];

        switch (param->kind) {
        case SL_SQLSERVER_PARAM_NULL:
            indicators[index] = SQL_NULL_DATA;
            rc = SQLBindParameter(stmt, sql_index, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 1, 0,
                                  NULL, 0, &indicators[index]);
            break;
        case SL_SQLSERVER_PARAM_TEXT: {
            SQLPOINTER value_ptr = empty_text;

            if (!sl_sqlsrv_str_valid(param->value.text)) {
                return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            }
            if (param->value.text.length > 2147483647U) {
                return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
            }
            if (param->value.text.length > 0U) {
                value_ptr = (SQLPOINTER)param->value.text.ptr;
            }
            indicators[index] = (SQLLEN)param->value.text.length;
            rc = SQLBindParameter(stmt, sql_index, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
                                  param->value.text.length, 0, value_ptr,
                                  (SQLLEN)param->value.text.length, &indicators[index]);
            break;
        }
        case SL_SQLSERVER_PARAM_INTEGER:
            integers[index] = (SQLBIGINT)param->value.integer;
            indicators[index] = 0;
            rc = SQLBindParameter(stmt, sql_index, SQL_PARAM_INPUT, SQL_C_SBIGINT, SQL_BIGINT, 0, 0,
                                  &integers[index], 0, &indicators[index]);
            break;
        case SL_SQLSERVER_PARAM_FLOAT:
            numbers[index] = param->value.number;
            indicators[index] = 0;
            rc = SQLBindParameter(stmt, sql_index, SQL_PARAM_INPUT, SQL_C_DOUBLE, SQL_DOUBLE, 0, 0,
                                  &numbers[index], 0, &indicators[index]);
            break;
        case SL_SQLSERVER_PARAM_BOOL:
            booleans[index] = param->value.boolean ? 1U : 0U;
            indicators[index] = 0;
            rc = SQLBindParameter(stmt, sql_index, SQL_PARAM_INPUT, SQL_C_BIT, SQL_BIT, 0, 0,
                                  &booleans[index], 0, &indicators[index]);
            break;
        case SL_SQLSERVER_PARAM_BYTES: {
            SQLPOINTER value_ptr = empty_bytes;

            if (param->value.bytes.length > 2147483647U ||
                (param->value.bytes.length != 0U && param->value.bytes.ptr == NULL))
            {
                return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            }
            if (param->value.bytes.length > 0U) {
                value_ptr = (SQLPOINTER)param->value.bytes.ptr;
            }
            indicators[index] = (SQLLEN)param->value.bytes.length;
            rc = SQLBindParameter(stmt, sql_index, SQL_PARAM_INPUT, SQL_C_BINARY, SQL_VARBINARY,
                                  param->value.bytes.length, 0, value_ptr,
                                  (SQLLEN)param->value.bytes.length, &indicators[index]);
            break;
        }
        case SL_SQLSERVER_PARAM_DECIMAL:
            if (!sl_sqlsrv_str_valid(param->value.decimal)) {
                return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            }
            indicators[index] = (SQLLEN)param->value.decimal.length;
            rc = SQLBindParameter(stmt, sql_index, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_DECIMAL,
                                  param->value.decimal.length, 0,
                                  (SQLPOINTER)param->value.decimal.ptr, indicators[index],
                                  &indicators[index]);
            break;
        case SL_SQLSERVER_PARAM_UUID:
            if (!sl_sqlsrv_str_valid(param->value.uuid)) {
                return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            }
            indicators[index] = (SQLLEN)param->value.uuid.length;
            rc = SQLBindParameter(stmt, sql_index, SQL_PARAM_INPUT, SQL_C_CHAR,
#ifdef SQL_GUID
                                  SQL_GUID,
#else
                                  SQL_VARCHAR,
#endif
                                  param->value.uuid.length, 0, (SQLPOINTER)param->value.uuid.ptr,
                                  indicators[index], &indicators[index]);
            break;
        case SL_SQLSERVER_PARAM_JSON:
            if (!sl_sqlsrv_str_valid(param->value.json)) {
                return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            }
            indicators[index] = (SQLLEN)param->value.json.length;
            rc = SQLBindParameter(stmt, sql_index, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_WVARCHAR,
                                  param->value.json.length, 0, (SQLPOINTER)param->value.json.ptr,
                                  indicators[index], &indicators[index]);
            break;
        case SL_SQLSERVER_PARAM_DATE:
            if (!sl_sqlsrv_str_valid(param->value.date)) {
                return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            }
            indicators[index] = (SQLLEN)param->value.date.length;
            rc = SQLBindParameter(stmt, sql_index, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_TYPE_DATE,
                                  param->value.date.length, 0, (SQLPOINTER)param->value.date.ptr,
                                  indicators[index], &indicators[index]);
            break;
        case SL_SQLSERVER_PARAM_TIME:
            if (!sl_sqlsrv_str_valid(param->value.time)) {
                return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            }
            indicators[index] = (SQLLEN)param->value.time.length;
            rc = SQLBindParameter(stmt, sql_index, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_SS_TIME2,
                                  param->value.time.length, 0, (SQLPOINTER)param->value.time.ptr,
                                  indicators[index], &indicators[index]);
            break;
        case SL_SQLSERVER_PARAM_TIMESTAMP:
            if (!sl_sqlsrv_str_valid(param->value.timestamp)) {
                return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            }
            indicators[index] = (SQLLEN)param->value.timestamp.length;
            rc = SQLBindParameter(stmt, sql_index, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_TYPE_TIMESTAMP,
                                  param->value.timestamp.length, 0,
                                  (SQLPOINTER)param->value.timestamp.ptr, indicators[index],
                                  &indicators[index]);
            break;
        case SL_SQLSERVER_PARAM_INSTANT:
            if (!sl_sqlsrv_str_valid(param->value.instant)) {
                return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            }
            indicators[index] = (SQLLEN)param->value.instant.length;
            rc = SQLBindParameter(stmt, sql_index, SQL_PARAM_INPUT, SQL_C_CHAR,
                                  SQL_SS_TIMESTAMPOFFSET, param->value.instant.length, 0,
                                  (SQLPOINTER)param->value.instant.ptr, indicators[index],
                                  &indicators[index]);
            break;
        default:
            return sl_sqlsrv_diag(
                arena, out_diag, SL_DIAG_DATABASE_UNSUPPORTED_VALUE,
                sl_sqlsrv_literal("unsupported sqlserver parameter value",
                                  sizeof("unsupported sqlserver parameter value") - 1U),
                operation, sql, sl_str_empty(), sl_str_empty(),
                sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
        }
        if (!sl_sqlsrv_success(rc)) {
            return sl_sqlsrv_diag_from_handle(
                arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
                sl_sqlsrv_literal("sqlserver provider bind failed",
                                  sizeof("sqlserver provider bind failed") - 1U),
                operation, SQL_HANDLE_STMT, stmt, sl_str_empty(), sql,
                sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
        }
    }
    rc = SQLExecute(stmt);
    if (!sl_sqlsrv_success(rc)) {
        return sl_sqlsrv_diag_from_handle(
            arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
            sl_sqlsrv_literal("sqlserver provider statement execution failed",
                              sizeof("sqlserver provider statement execution failed") - 1U),
            operation, SQL_HANDLE_STMT, stmt, sl_str_empty(), sql,
            sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
    }
    return sl_status_ok();
}

static SlStatus sl_sqlsrv_prepare_execute(SlArena* arena, SlSqlServerConnection* connection,
                                          SlStr sql, const SlSqlServerParam* params,
                                          size_t param_count, SlStr operation, SQLHSTMT* out_stmt,
                                          SlDiag* out_diag)
{
    SQLHDBC dbc = sl_sqlsrv_dbc(connection);
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    SQLRETURN rc;
    SlStatus status;

    if (out_stmt == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_stmt = SQL_NULL_HSTMT;
    if (dbc == SQL_NULL_HDBC) {
        return sl_sqlsrv_invalid_state_diag(arena, out_diag, operation);
    }
    if (!sl_sqlsrv_str_valid(sql) || sl_str_is_empty(sql)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (sql.length > 2147483647U) {
        return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
    }
    rc = SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);
    if (!sl_sqlsrv_success(rc)) {
        return sl_sqlsrv_diag_from_handle(
            arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
            sl_sqlsrv_literal("sqlserver provider statement allocation failed",
                              sizeof("sqlserver provider statement allocation failed") - 1U),
            operation, SQL_HANDLE_DBC, dbc, sl_str_empty(), sql,
            sl_status_from_code(SL_STATUS_OUT_OF_MEMORY));
    }
    rc = SQLPrepareA(stmt, (SQLCHAR*)sql.ptr, (SQLINTEGER)sql.length);
    if (!sl_sqlsrv_success(rc)) {
        status = sl_sqlsrv_diag_from_handle(
            arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
            sl_sqlsrv_literal("sqlserver provider prepare failed",
                              sizeof("sqlserver provider prepare failed") - 1U),
            operation, SQL_HANDLE_STMT, stmt, sl_str_empty(), sql,
            sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return status;
    }
    status = sl_sqlsrv_bind_params(arena, stmt, params, param_count, out_diag, operation, sql);
    if (!sl_status_is_ok(status)) {
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return status;
    }
    *out_stmt = stmt;
    return sl_status_ok();
}

SlStatus sl_sqlserver_exec(SlArena* arena, SlSqlServerConnection* connection, SlStr sql,
                           const SlSqlServerParam* params, size_t param_count,
                           SlSqlServerExecResult* out_result, SlDiag* out_diag)
{
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    SQLLEN row_count = 0;
    SlStatus status;

    if (out_result == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_result = (SlSqlServerExecResult){0};
    if (connection != NULL && connection->open && connection->access == SL_SQLSERVER_ACCESS_READ) {
        return sl_sqlsrv_diag(
            arena, out_diag, SL_DIAG_PERMISSION_DENIED,
            sl_sqlsrv_literal("sqlserver provider read-only connection rejected exec",
                              sizeof("sqlserver provider read-only connection rejected exec") - 1U),
            sl_sqlsrv_literal("operation: exec", sizeof("operation: exec") - 1U), sql,
            sl_str_empty(), sl_str_empty(), sl_status_from_code(SL_STATUS_INVALID_STATE));
    }
    status = sl_sqlsrv_prepare_execute(
        arena, connection, sql, params, param_count,
        sl_sqlsrv_literal("operation: exec", sizeof("operation: exec") - 1U), &stmt, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (sl_sqlsrv_success(SQLRowCount(stmt, &row_count)) && row_count >= 0) {
        out_result->affected_rows = (int64_t)row_count;
        out_result->affected_rows_known = true;
    }
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return sl_status_ok();
}

static SlStatus sl_sqlsrv_copy_columns(SlArena* arena, SQLHSTMT stmt, size_t column_count,
                                       SlStr** out_column_names)
{
    void* ptr = NULL;
    SlStr* names = NULL;
    size_t alloc_size = 0U;
    SlStatus status;

    if (arena == NULL || stmt == SQL_NULL_HSTMT || out_column_names == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_column_names = NULL;
    if (column_count == 0U) {
        return sl_status_ok();
    }
    status = sl_checked_mul_size(column_count, sizeof(SlStr), &alloc_size);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(arena, alloc_size, _Alignof(SlStr), &ptr);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    names = (SlStr*)ptr;
    for (size_t index = 0U; index < column_count; index += 1U) {
        char name[256] = {0};
        SQLSMALLINT name_length = 0;
        SQLSMALLINT data_type = 0;
        SQLULEN column_size = 0;
        SQLSMALLINT decimal_digits = 0;
        SQLSMALLINT nullable = 0;
        SQLRETURN rc = SQLDescribeColA(stmt, (SQLUSMALLINT)(index + 1U), (SQLCHAR*)name,
                                       (SQLSMALLINT)sizeof(name), &name_length, &data_type,
                                       &column_size, &decimal_digits, &nullable);
        (void)data_type;
        (void)column_size;
        (void)decimal_digits;
        (void)nullable;
        if (!sl_sqlsrv_success(rc)) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        if (rc == SQL_SUCCESS_WITH_INFO || name_length < 0 || (size_t)name_length >= sizeof(name)) {
            return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
        }
        status =
            sl_sqlsrv_copy_str(arena, sl_str_from_parts(name, (size_t)name_length), &names[index]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    *out_column_names = names;
    return sl_status_ok();
}

static SlStatus sl_sqlsrv_get_col_type(SQLHSTMT stmt, size_t column, SQLSMALLINT* out_type)
{
    char name[4] = {0};
    SQLSMALLINT name_length = 0;
    SQLULEN column_size = 0;
    SQLSMALLINT decimal_digits = 0;
    SQLSMALLINT nullable = 0;
    SQLRETURN rc;

    if (out_type == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_type = SQL_VARCHAR;
    rc = SQLDescribeColA(stmt, (SQLUSMALLINT)(column + 1U), (SQLCHAR*)name,
                         (SQLSMALLINT)sizeof(name), &name_length, out_type, &column_size,
                         &decimal_digits, &nullable);
    return sl_sqlsrv_success(rc) ? sl_status_ok() : sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
}

static bool sl_sqlsrv_type_is_integer(SQLSMALLINT type)
{
    return type == SQL_INTEGER || type == SQL_SMALLINT || type == SQL_TINYINT || type == SQL_BIGINT;
}

static bool sl_sqlsrv_type_is_float(SQLSMALLINT type)
{
    return type == SQL_REAL || type == SQL_FLOAT || type == SQL_DOUBLE;
}

static bool sl_sqlsrv_type_is_decimal(SQLSMALLINT type)
{
    return type == SQL_DECIMAL || type == SQL_NUMERIC;
}

static bool sl_sqlsrv_type_is_binary(SQLSMALLINT type)
{
    return type == SQL_BINARY || type == SQL_VARBINARY || type == SQL_LONGVARBINARY;
}

static bool sl_sqlsrv_type_is_date(SQLSMALLINT type)
{
    return type == SQL_TYPE_DATE || type == SQL_DATE;
}

static bool sl_sqlsrv_type_is_time(SQLSMALLINT type)
{
    return type == SQL_TYPE_TIME || type == SQL_TIME || type == SQL_SS_TIME2;
}

static bool sl_sqlsrv_type_is_timestamp(SQLSMALLINT type)
{
    return type == SQL_TYPE_TIMESTAMP || type == SQL_TIMESTAMP;
}

static bool sl_sqlsrv_type_is_instant(SQLSMALLINT type)
{
    return type == SQL_SS_TIMESTAMPOFFSET;
}

static bool sl_sqlsrv_type_is_uuid(SQLSMALLINT type)
{
#ifdef SQL_GUID
    return type == SQL_GUID;
#else
    (void)type;
    return false;
#endif
}

static size_t sl_sqlsrv_chunk_length(const char* buffer, size_t capacity)
{
    size_t length = 0U;

    while (length < capacity && buffer[length] != '\0') {
        length += 1U;
    }
    return length;
}

static SlStatus sl_sqlsrv_append_chunk(SlArena* arena, SlStr current, SlStr chunk, SlStr* out)
{
    SlOwnedStr copied = {0};
    SlStatus status;

    if (arena == NULL || out == NULL || !sl_sqlsrv_str_valid(current) ||
        !sl_sqlsrv_str_valid(chunk))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_str_concat_to_arena(arena, current, chunk, &copied);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    *out = sl_owned_str_as_view(copied);
    return sl_status_ok();
}

static SlStatus sl_sqlsrv_copy_streamed_text(SlArena* arena, SQLHSTMT stmt, size_t column,
                                             char* first_buffer, size_t first_capacity,
                                             SQLRETURN first_rc, SlSqlServerValue* out)
{
    SlStr text = sl_str_empty();
    SQLRETURN rc = first_rc;
    SlStatus status = sl_sqlsrv_append_chunk(
        arena, text,
        sl_str_from_parts(first_buffer, sl_sqlsrv_chunk_length(first_buffer, first_capacity)),
        &text);

    if (!sl_status_is_ok(status)) {
        return status;
    }
    while (rc == SQL_SUCCESS_WITH_INFO) {
        char buffer[4096] = {0};
        SQLLEN indicator = 0;

        rc = SQLGetData(stmt, (SQLUSMALLINT)(column + 1U), SQL_C_CHAR, buffer,
                        (SQLLEN)sizeof(buffer), &indicator);
        if (rc == SQL_NO_DATA) {
            break;
        }
        if (!sl_sqlsrv_success(rc) || indicator == SQL_NULL_DATA) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        status = sl_sqlsrv_append_chunk(
            arena, text, sl_str_from_parts(buffer, sl_sqlsrv_chunk_length(buffer, sizeof(buffer))),
            &text);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    out->kind = SL_SQLSERVER_VALUE_TEXT;
    out->value.text = text;
    return sl_status_ok();
}

static SlStatus sl_sqlsrv_copy_binary_value(SlArena* arena, SQLHSTMT stmt, size_t column,
                                            SlSqlServerValue* out)
{
    unsigned char buffer[4096];
    SQLLEN indicator = 0;
    SlBytes current = sl_bytes_empty();
    SlStatus status;

    if (arena == NULL || stmt == SQL_NULL_HSTMT || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    for (;;) {
        SQLRETURN rc = SQLGetData(stmt, (SQLUSMALLINT)(column + 1U), SQL_C_BINARY, buffer,
                                  (SQLLEN)sizeof(buffer), &indicator);
        if (rc == SQL_NO_DATA) {
            out->kind = SL_SQLSERVER_VALUE_BYTES;
            out->value.bytes = current;
            return sl_status_ok();
        }
        if (!sl_sqlsrv_success(rc)) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        if (indicator == SQL_NULL_DATA) {
            *out = (SlSqlServerValue){.kind = SL_SQLSERVER_VALUE_NULL};
            return sl_status_ok();
        }
        size_t chunk = sizeof(buffer);
        SlBytes combined;
        if (indicator >= 0 && indicator < (SQLLEN)sizeof(buffer)) {
            chunk = (size_t)indicator;
        }
        if (current.length == 0U) {
            status = sl_sqlsrv_copy_bytes(arena, sl_bytes_from_parts(buffer, chunk), &current);
        }
        else {
            void* ptr = NULL;
            unsigned char* dst = NULL;
            size_t total = 0U;
            status = sl_checked_add_size(current.length, chunk, &total);
            if (sl_status_is_ok(status)) {
                status = sl_arena_alloc(arena, total, 1U, &ptr);
            }
            if (sl_status_is_ok(status)) {
                dst = (unsigned char*)ptr;
                for (size_t index = 0U; index < current.length; index += 1U) {
                    dst[index] = current.ptr[index];
                }
                for (size_t index = 0U; index < chunk; index += 1U) {
                    dst[current.length + index] = buffer[index];
                }
                combined = sl_bytes_from_parts(dst, total);
                current = combined;
            }
        }
        if (!sl_status_is_ok(status)) {
            return status;
        }
        if (rc == SQL_SUCCESS) {
            out->kind = SL_SQLSERVER_VALUE_BYTES;
            out->value.bytes = current;
            return sl_status_ok();
        }
    }
}

static SlStatus sl_sqlsrv_copy_value(SlArena* arena, SQLHSTMT stmt, size_t column,
                                     SlSqlServerValue* out)
{
    char buffer[4096] = {0};
    SQLLEN indicator = 0;
    SQLSMALLINT type = 0;
    SQLRETURN rc;
    SlStatus status;

    if (arena == NULL || stmt == SQL_NULL_HSTMT || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_sqlsrv_get_col_type(stmt, column, &type);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (sl_sqlsrv_type_is_binary(type)) {
        return sl_sqlsrv_copy_binary_value(arena, stmt, column, out);
    }
    rc = SQLGetData(stmt, (SQLUSMALLINT)(column + 1U), SQL_C_CHAR, buffer, (SQLLEN)sizeof(buffer),
                    &indicator);
    if (rc == SQL_NO_DATA || indicator == SQL_NULL_DATA) {
        *out = (SlSqlServerValue){.kind = SL_SQLSERVER_VALUE_NULL};
        return sl_status_ok();
    }
    if (!sl_sqlsrv_success(rc)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (rc == SQL_SUCCESS_WITH_INFO || indicator == SQL_NO_TOTAL ||
        indicator >= (SQLLEN)sizeof(buffer))
    {
        return sl_sqlsrv_copy_streamed_text(arena, stmt, column, buffer, sizeof(buffer), rc, out);
    }
    if (type == SQL_BIT && indicator > 0) {
        out->kind = SL_SQLSERVER_VALUE_BOOL;
        out->value.boolean = buffer[0] != '0';
        return sl_status_ok();
    }
    if (sl_sqlsrv_type_is_integer(type)) {
        char* end = NULL;
        long long value = 0;

        errno = 0;
        value = strtoll(buffer, &end, 10);
        if (errno == 0 && end == buffer + indicator) {
            out->kind = SL_SQLSERVER_VALUE_INTEGER;
            out->value.integer = (int64_t)value;
            return sl_status_ok();
        }
    }
    if (sl_sqlsrv_type_is_float(type)) {
        char* end = NULL;
        double value = 0.0;

        errno = 0;
        value = strtod(buffer, &end);
        if (errno == 0 && end == buffer + indicator) {
            out->kind = SL_SQLSERVER_VALUE_FLOAT;
            out->value.number = value;
            return sl_status_ok();
        }
    }
    if (sl_sqlsrv_type_is_decimal(type)) {
        out->kind = SL_SQLSERVER_VALUE_DECIMAL;
        return sl_sqlsrv_copy_str(arena, sl_str_from_parts(buffer, (size_t)indicator),
                                  &out->value.decimal);
    }
    if (sl_sqlsrv_type_is_uuid(type)) {
        out->kind = SL_SQLSERVER_VALUE_UUID;
        return sl_sqlsrv_copy_str(arena, sl_str_from_parts(buffer, (size_t)indicator),
                                  &out->value.uuid);
    }
    if (sl_sqlsrv_type_is_date(type)) {
        out->kind = SL_SQLSERVER_VALUE_DATE;
        return sl_sqlsrv_copy_str(arena, sl_str_from_parts(buffer, (size_t)indicator),
                                  &out->value.date);
    }
    if (sl_sqlsrv_type_is_time(type)) {
        out->kind = SL_SQLSERVER_VALUE_TIME;
        return sl_sqlsrv_copy_str(arena, sl_str_from_parts(buffer, (size_t)indicator),
                                  &out->value.time);
    }
    if (sl_sqlsrv_type_is_timestamp(type)) {
        out->kind = SL_SQLSERVER_VALUE_TIMESTAMP;
        return sl_sqlsrv_copy_str(arena, sl_str_from_parts(buffer, (size_t)indicator),
                                  &out->value.timestamp);
    }
    if (sl_sqlsrv_type_is_instant(type)) {
        out->kind = SL_SQLSERVER_VALUE_INSTANT;
        return sl_sqlsrv_copy_str(arena, sl_str_from_parts(buffer, (size_t)indicator),
                                  &out->value.instant);
    }
    out->kind = SL_SQLSERVER_VALUE_TEXT;
    return sl_sqlsrv_copy_str(arena, sl_str_from_parts(buffer, (size_t)indicator),
                              &out->value.text);
}

static SlStatus sl_sqlsrv_materialize_rows(SlArena* arena, SQLHSTMT stmt, size_t max_rows,
                                           SlSqlServerResult* out_result, SlDiag* out_diag,
                                           SlStr operation, SlStr sql)
{
    SQLSMALLINT column_count_small = 0;
    size_t column_count = 0U;
    size_t row_count = 0U;
    void* row_ptr = NULL;
    void* cell_ptr = NULL;
    SlSqlServerRow* rows = NULL;
    SlSqlServerValue* cells = NULL;
    size_t alloc_size = 0U;
    size_t cell_count = 0U;
    SQLRETURN rc;
    SlStatus status;

    if (arena == NULL || stmt == SQL_NULL_HSTMT || out_result == NULL || max_rows == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_result = (SlSqlServerResult){0};
    rc = SQLNumResultCols(stmt, &column_count_small);
    if (!sl_sqlsrv_success(rc) || column_count_small < 0) {
        return sl_sqlsrv_diag_from_handle(
            arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
            sl_sqlsrv_literal("sqlserver provider result metadata failed",
                              sizeof("sqlserver provider result metadata failed") - 1U),
            operation, SQL_HANDLE_STMT, stmt, sl_str_empty(), sql,
            sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
    }
    column_count = (size_t)column_count_small;
    status = sl_sqlsrv_copy_columns(arena, stmt, column_count, &out_result->column_names);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_checked_mul_size(max_rows, sizeof(SlSqlServerRow), &alloc_size);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(arena, alloc_size, _Alignof(SlSqlServerRow), &row_ptr);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (row_ptr == NULL) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    rows = (SlSqlServerRow*)row_ptr;
    status = sl_checked_mul_size(max_rows, column_count, &cell_count);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (cell_count > 0U) {
        status = sl_checked_mul_size(cell_count, sizeof(SlSqlServerValue), &alloc_size);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_arena_alloc(arena, alloc_size, _Alignof(SlSqlServerValue), &cell_ptr);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        if (cell_ptr == NULL) {
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
        cells = (SlSqlServerValue*)cell_ptr;
    }
    while ((rc = SQLFetch(stmt)) != SQL_NO_DATA) {
        if (!sl_sqlsrv_success(rc)) {
            return sl_sqlsrv_diag_from_handle(
                arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
                sl_sqlsrv_literal("sqlserver provider fetch failed",
                                  sizeof("sqlserver provider fetch failed") - 1U),
                operation, SQL_HANDLE_STMT, stmt, sl_str_empty(), sql,
                sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
        }
        if (row_count >= max_rows) {
            return sl_sqlsrv_diag(
                arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
                sl_sqlsrv_literal("sqlserver provider query exceeded max rows",
                                  sizeof("sqlserver provider query exceeded max rows") - 1U),
                operation, sql, sl_str_empty(), sl_str_empty(),
                sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED));
        }
        rows[row_count].values = column_count == 0U ? NULL : cells + (row_count * column_count);
        for (size_t column = 0U; column < column_count; column += 1U) {
            status = sl_sqlsrv_copy_value(arena, stmt, column, &rows[row_count].values[column]);
            if (!sl_status_is_ok(status)) {
                return status;
            }
        }
        row_count += 1U;
    }
    out_result->column_count = column_count;
    out_result->row_count = row_count;
    out_result->rows = rows;
    return sl_status_ok();
}

static SlStatus sl_sqlsrv_materialize_first_row(SlArena* arena, SQLHSTMT stmt,
                                                SlSqlServerQueryOneResult* out_result,
                                                SlDiag* out_diag, SlStr operation, SlStr sql)
{
    SQLSMALLINT column_count_small = 0;
    size_t column_count = 0U;
    void* cell_ptr = NULL;
    SlSqlServerValue* values = NULL;
    size_t alloc_size = 0U;
    SQLRETURN rc;
    SlStatus status;

    if (arena == NULL || stmt == SQL_NULL_HSTMT || out_result == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_result = (SlSqlServerQueryOneResult){0};
    rc = SQLNumResultCols(stmt, &column_count_small);
    if (!sl_sqlsrv_success(rc) || column_count_small < 0) {
        return sl_sqlsrv_diag_from_handle(
            arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
            sl_sqlsrv_literal("sqlserver provider result metadata failed",
                              sizeof("sqlserver provider result metadata failed") - 1U),
            operation, SQL_HANDLE_STMT, stmt, sl_str_empty(), sql,
            sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
    }
    column_count = (size_t)column_count_small;
    status = sl_sqlsrv_copy_columns(arena, stmt, column_count, &out_result->column_names);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    out_result->column_count = column_count;
    rc = SQLFetch(stmt);
    if (rc == SQL_NO_DATA) {
        return sl_status_ok();
    }
    if (!sl_sqlsrv_success(rc)) {
        return sl_sqlsrv_diag_from_handle(
            arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
            sl_sqlsrv_literal("sqlserver provider fetch failed",
                              sizeof("sqlserver provider fetch failed") - 1U),
            operation, SQL_HANDLE_STMT, stmt, sl_str_empty(), sql,
            sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
    }
    if (column_count > 0U) {
        status = sl_checked_mul_size(column_count, sizeof(SlSqlServerValue), &alloc_size);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_arena_alloc(arena, alloc_size, _Alignof(SlSqlServerValue), &cell_ptr);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        values = (SlSqlServerValue*)cell_ptr;
        for (size_t column = 0U; column < column_count; column += 1U) {
            status = sl_sqlsrv_copy_value(arena, stmt, column, &values[column]);
            if (!sl_status_is_ok(status)) {
                return status;
            }
        }
    }
    out_result->found = true;
    out_result->values = values;
    return sl_status_ok();
}

SlStatus sl_sqlserver_query(SlArena* arena, SlSqlServerConnection* connection, SlStr sql,
                            const SlSqlServerParam* params, size_t param_count,
                            const SlSqlServerQueryOptions* options, SlSqlServerResult* out_result,
                            SlDiag* out_diag)
{
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    size_t max_rows = SL_SQLSERVER_DEFAULT_MAX_ROWS;
    SlArenaMark mark;
    SlSqlServerResult temp = {0};
    SlStatus status;
    SlStr operation = sl_sqlsrv_literal("operation: query", sizeof("operation: query") - 1U);

    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (out_result == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_result = (SlSqlServerResult){0};
    if (arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    mark = sl_arena_mark(arena);
    if (options != NULL && options->max_rows > 0U) {
        max_rows = options->max_rows;
    }
    status = sl_sqlsrv_prepare_execute(arena, connection, sql, params, param_count, operation,
                                       &stmt, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_sqlsrv_materialize_rows(arena, stmt, max_rows, &temp, out_diag, operation, sql);
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    if (!sl_status_is_ok(status)) {
        if (out_diag == NULL || out_diag->code == SL_DIAG_NONE) {
            (void)sl_arena_reset_to(arena, mark);
        }
        *out_result = (SlSqlServerResult){0};
        return status;
    }
    *out_result = temp;
    return status;
}

SlStatus sl_sqlserver_query_one(SlArena* arena, SlSqlServerConnection* connection, SlStr sql,
                                const SlSqlServerParam* params, size_t param_count,
                                SlSqlServerQueryOneResult* out_result, SlDiag* out_diag)
{
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    SlArenaMark mark;
    SlSqlServerQueryOneResult temp = {0};
    SlStatus status;
    SlStr operation = sl_sqlsrv_literal("operation: queryOne", sizeof("operation: queryOne") - 1U);

    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (out_result == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_result = (SlSqlServerQueryOneResult){0};
    if (arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    mark = sl_arena_mark(arena);
    status = sl_sqlsrv_prepare_execute(arena, connection, sql, params, param_count, operation,
                                       &stmt, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_sqlsrv_materialize_first_row(arena, stmt, &temp, out_diag, operation, sql);
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    if (!sl_status_is_ok(status)) {
        if (out_diag == NULL || out_diag->code == SL_DIAG_NONE) {
            (void)sl_arena_reset_to(arena, mark);
        }
        *out_result = (SlSqlServerQueryOneResult){0};
        return status;
    }
    *out_result = temp;
    return status;
}

SlStatus sl_sqlserver_transaction_begin(SlArena* arena, SlSqlServerConnection* connection,
                                        SlSqlServerTransaction* out_tx, SlDiag* out_diag)
{
    SQLHDBC dbc = sl_sqlsrv_dbc(connection);
    SQLRETURN rc;

    if (out_tx == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_tx = (SlSqlServerTransaction){0};
    if (dbc == SQL_NULL_HDBC) {
        return sl_sqlsrv_invalid_state_diag(
            arena, out_diag,
            sl_sqlsrv_literal("operation: transaction.begin",
                              sizeof("operation: transaction.begin") - 1U));
    }
    if (connection->transaction_active) {
        return sl_sqlsrv_diag(
            arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
            sl_sqlsrv_literal("sqlserver provider nested transactions are not supported",
                              sizeof("sqlserver provider nested transactions are not supported") -
                                  1U),
            sl_sqlsrv_literal("operation: transaction.begin",
                              sizeof("operation: transaction.begin") - 1U),
            sl_str_empty(), sl_str_empty(), sl_str_empty(),
            sl_status_from_code(SL_STATUS_INVALID_STATE));
    }
    rc = SQLSetConnectAttr(dbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_OFF, 0);
    if (!sl_sqlsrv_success(rc)) {
        return sl_sqlsrv_diag_from_handle(
            arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
            sl_sqlsrv_literal("sqlserver provider failed to begin transaction",
                              sizeof("sqlserver provider failed to begin transaction") - 1U),
            sl_sqlsrv_literal("operation: transaction.begin",
                              sizeof("operation: transaction.begin") - 1U),
            SQL_HANDLE_DBC, dbc, sl_str_empty(), sl_str_empty(),
            sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
    }
    connection->transaction_active = true;
    out_tx->connection = connection;
    out_tx->active = true;
    return sl_status_ok();
}

static SlStatus sl_sqlsrv_end_transaction(SlArena* arena, SlSqlServerTransaction* tx,
                                          SQLSMALLINT completion, SlStr operation, SlDiag* out_diag)
{
    SQLHDBC dbc;
    SQLRETURN rc;

    if (tx == NULL || tx->connection == NULL || !tx->active) {
        return sl_sqlsrv_invalid_state_diag(arena, out_diag, operation);
    }
    dbc = sl_sqlsrv_dbc(tx->connection);
    if (dbc == SQL_NULL_HDBC) {
        return sl_sqlsrv_invalid_state_diag(arena, out_diag, operation);
    }
    rc = SQLEndTran(SQL_HANDLE_DBC, dbc, completion);
    if (!sl_sqlsrv_success(rc)) {
        return sl_sqlsrv_diag_from_handle(
            arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
            sl_sqlsrv_literal("sqlserver provider failed to finish transaction",
                              sizeof("sqlserver provider failed to finish transaction") - 1U),
            operation, SQL_HANDLE_DBC, dbc, sl_str_empty(), sl_str_empty(),
            sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
    }
    rc = SQLSetConnectAttr(dbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
    if (!sl_sqlsrv_success(rc)) {
        SlSqlServerConnection* connection = tx->connection;
        SlStatus status = sl_sqlsrv_diag_from_handle(
            arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
            sl_sqlsrv_literal("sqlserver provider failed to restore autocommit",
                              sizeof("sqlserver provider failed to restore autocommit") - 1U),
            operation, SQL_HANDLE_DBC, dbc, sl_str_empty(), sl_str_empty(),
            sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));

        /*
         * SQLEndTran already completed. If autocommit cannot be restored, the transaction
         * object must be inactive and the connection must not return to the pool silently.
         */
        connection->transaction_active = false;
        tx->connection = NULL;
        tx->active = false;
        (void)sl_sqlserver_close(connection);
        return status;
    }
    tx->connection->transaction_active = false;
    tx->connection = NULL;
    tx->active = false;
    return sl_status_ok();
}

SlStatus sl_sqlserver_transaction_commit(SlArena* arena, SlSqlServerTransaction* tx,
                                         SlDiag* out_diag)
{
    return sl_sqlsrv_end_transaction(
        arena, tx, SQL_COMMIT,
        sl_sqlsrv_literal("operation: transaction.commit",
                          sizeof("operation: transaction.commit") - 1U),
        out_diag);
}

SlStatus sl_sqlserver_transaction_rollback(SlArena* arena, SlSqlServerTransaction* tx,
                                           SlDiag* out_diag)
{
    return sl_sqlsrv_end_transaction(
        arena, tx, SQL_ROLLBACK,
        sl_sqlsrv_literal("operation: transaction.rollback",
                          sizeof("operation: transaction.rollback") - 1U),
        out_diag);
}

SlStatus sl_sqlserver_transaction_exec(SlArena* arena, SlSqlServerTransaction* tx, SlStr sql,
                                       const SlSqlServerParam* params, size_t param_count,
                                       SlSqlServerExecResult* out_result, SlDiag* out_diag)
{
    if (tx == NULL || tx->connection == NULL || !tx->active) {
        return sl_sqlsrv_invalid_state_diag(
            arena, out_diag,
            sl_sqlsrv_literal("operation: transaction.exec",
                              sizeof("operation: transaction.exec") - 1U));
    }
    return sl_sqlserver_exec(arena, tx->connection, sql, params, param_count, out_result, out_diag);
}

SlStatus sl_sqlserver_transaction_query(SlArena* arena, SlSqlServerTransaction* tx, SlStr sql,
                                        const SlSqlServerParam* params, size_t param_count,
                                        const SlSqlServerQueryOptions* options,
                                        SlSqlServerResult* out_result, SlDiag* out_diag)
{
    if (tx == NULL || tx->connection == NULL || !tx->active) {
        return sl_sqlsrv_invalid_state_diag(
            arena, out_diag,
            sl_sqlsrv_literal("operation: transaction.query",
                              sizeof("operation: transaction.query") - 1U));
    }
    return sl_sqlserver_query(arena, tx->connection, sql, params, param_count, options, out_result,
                              out_diag);
}

SlStatus sl_sqlserver_transaction_query_one(SlArena* arena, SlSqlServerTransaction* tx, SlStr sql,
                                            const SlSqlServerParam* params, size_t param_count,
                                            SlSqlServerQueryOneResult* out_result, SlDiag* out_diag)
{
    if (tx == NULL || tx->connection == NULL || !tx->active) {
        return sl_sqlsrv_invalid_state_diag(
            arena, out_diag,
            sl_sqlsrv_literal("operation: transaction.queryOne",
                              sizeof("operation: transaction.queryOne") - 1U));
    }
    return sl_sqlserver_query_one(arena, tx->connection, sql, params, param_count, out_result,
                                  out_diag);
}

SlStatus sl_sqlserver_pool_open(SlArena* arena, const SlSqlServerPoolOptions* options,
                                SlSqlServerPool* out_pool, SlDiag* out_diag)
{
    SlStatus status;

    if (out_pool == NULL || options == NULL || !sl_sqlsrv_str_valid(options->connection_string) ||
        sl_str_is_empty(options->connection_string) || options->max_connections == 0U ||
        options->max_connections > SL_SQLSERVER_MAX_POOL_CONNECTIONS ||
        (options->access != SL_SQLSERVER_ACCESS_READ &&
         options->access != SL_SQLSERVER_ACCESS_READWRITE))
    {
        return sl_sqlsrv_diag(
            arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
            sl_sqlsrv_literal("sqlserver provider pool options are invalid",
                              sizeof("sqlserver provider pool options are invalid") - 1U),
            sl_sqlsrv_literal("operation: pool.open", sizeof("operation: pool.open") - 1U),
            sl_str_empty(), sl_str_empty(), sl_str_empty(),
            sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
    }
    *out_pool = (SlSqlServerPool){0};
    status = sl_sqlsrv_copy_str(arena, options->connection_string, &out_pool->connection_string);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    out_pool->access = options->access;
    out_pool->max_connections = options->max_connections;
    return sl_status_ok();
}

SlStatus sl_sqlserver_pool_acquire(SlArena* arena, SlSqlServerPool* pool,
                                   SlSqlServerConnection** out_connection, SlDiag* out_diag)
{
    SlSqlServerOpenOptions options;
    SlStatus status;

    if (pool == NULL || out_connection == NULL || pool->closed) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_connection = NULL;
    for (size_t index = 0U; index < pool->open_count; index += 1U) {
        if (pool->idle[index]) {
            pool->idle[index] = false;
            *out_connection = &pool->connections[index];
            return sl_status_ok();
        }
    }
    if (pool->open_count >= pool->max_connections) {
        return sl_sqlsrv_diag(
            arena, out_diag, SL_DIAG_SQLSERVER_POOL_EXHAUSTED,
            sl_sqlsrv_literal("sqlserver provider pool is exhausted",
                              sizeof("sqlserver provider pool is exhausted") - 1U),
            sl_sqlsrv_literal("operation: pool.acquire", sizeof("operation: pool.acquire") - 1U),
            sl_str_empty(), sl_str_empty(), sl_str_empty(),
            sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED));
    }
    options = sl_sqlserver_open_options_connection_string(pool->connection_string);
    options.access = pool->access;
    status = sl_sqlserver_open(arena, &options, &pool->connections[pool->open_count], out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    *out_connection = &pool->connections[pool->open_count];
    pool->idle[pool->open_count] = false;
    pool->open_count += 1U;
    return sl_status_ok();
}

SlStatus sl_sqlserver_pool_release(SlSqlServerPool* pool, SlSqlServerConnection* connection)
{
    if (pool == NULL || connection == NULL || pool->closed) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    for (size_t index = 0U; index < pool->open_count; index += 1U) {
        if (&pool->connections[index] == connection) {
            if (pool->idle[index] || !connection->open || connection->transaction_active) {
                return sl_status_from_code(SL_STATUS_INVALID_STATE);
            }
            pool->idle[index] = true;
            return sl_status_ok();
        }
    }
    return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
}

SlStatus sl_sqlserver_pool_close(SlSqlServerPool* pool)
{
    if (pool == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (pool->closed) {
        return sl_status_ok();
    }
    for (size_t index = 0U; index < pool->open_count; index += 1U) {
        if (pool->connections[index].open) {
            (void)sl_sqlserver_close(&pool->connections[index]);
        }
        pool->idle[index] = false;
    }
    pool->closed = true;
    pool->open_count = 0U;
    return sl_status_ok();
}

#endif

#ifndef SLOPPY_ENABLE_SQLSERVER_PROVIDER
SlStatus sl_sqlserver_transaction_begin(SlArena* arena, SlSqlServerConnection* connection,
                                        SlSqlServerTransaction* out_tx, SlDiag* out_diag)
{
    (void)connection;
    if (out_tx != NULL) {
        *out_tx = (SlSqlServerTransaction){0};
    }
    return sl_sqlsrv_diag(
        arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
        sl_sqlsrv_literal("sqlserver provider ODBC support is unavailable",
                          sizeof("sqlserver provider ODBC support is unavailable") - 1U),
        sl_sqlsrv_literal("operation: transaction.begin",
                          sizeof("operation: transaction.begin") - 1U),
        sl_str_empty(), sl_str_empty(), sl_str_empty(), sl_status_from_code(SL_STATUS_UNSUPPORTED));
}

SlStatus sl_sqlserver_transaction_commit(SlArena* arena, SlSqlServerTransaction* tx,
                                         SlDiag* out_diag)
{
    (void)tx;
    return sl_sqlsrv_diag(
        arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
        sl_sqlsrv_literal("sqlserver provider ODBC support is unavailable",
                          sizeof("sqlserver provider ODBC support is unavailable") - 1U),
        sl_sqlsrv_literal("operation: transaction.commit",
                          sizeof("operation: transaction.commit") - 1U),
        sl_str_empty(), sl_str_empty(), sl_str_empty(), sl_status_from_code(SL_STATUS_UNSUPPORTED));
}

SlStatus sl_sqlserver_transaction_rollback(SlArena* arena, SlSqlServerTransaction* tx,
                                           SlDiag* out_diag)
{
    (void)tx;
    return sl_sqlsrv_diag(
        arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
        sl_sqlsrv_literal("sqlserver provider ODBC support is unavailable",
                          sizeof("sqlserver provider ODBC support is unavailable") - 1U),
        sl_sqlsrv_literal("operation: transaction.rollback",
                          sizeof("operation: transaction.rollback") - 1U),
        sl_str_empty(), sl_str_empty(), sl_str_empty(), sl_status_from_code(SL_STATUS_UNSUPPORTED));
}

SlStatus sl_sqlserver_transaction_exec(SlArena* arena, SlSqlServerTransaction* tx, SlStr sql,
                                       const SlSqlServerParam* params, size_t param_count,
                                       SlSqlServerExecResult* out_result, SlDiag* out_diag)
{
    (void)tx;
    return sl_sqlserver_exec(arena, NULL, sql, params, param_count, out_result, out_diag);
}

SlStatus sl_sqlserver_transaction_query(SlArena* arena, SlSqlServerTransaction* tx, SlStr sql,
                                        const SlSqlServerParam* params, size_t param_count,
                                        const SlSqlServerQueryOptions* options,
                                        SlSqlServerResult* out_result, SlDiag* out_diag)
{
    (void)tx;
    return sl_sqlserver_query(arena, NULL, sql, params, param_count, options, out_result, out_diag);
}

SlStatus sl_sqlserver_transaction_query_one(SlArena* arena, SlSqlServerTransaction* tx, SlStr sql,
                                            const SlSqlServerParam* params, size_t param_count,
                                            SlSqlServerQueryOneResult* out_result, SlDiag* out_diag)
{
    (void)tx;
    return sl_sqlserver_query_one(arena, NULL, sql, params, param_count, out_result, out_diag);
}

SlStatus sl_sqlserver_pool_open(SlArena* arena, const SlSqlServerPoolOptions* options,
                                SlSqlServerPool* out_pool, SlDiag* out_diag)
{
    (void)options;
    if (out_pool != NULL) {
        *out_pool = (SlSqlServerPool){0};
    }
    return sl_sqlsrv_diag(
        arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
        sl_sqlsrv_literal("sqlserver provider ODBC support is unavailable",
                          sizeof("sqlserver provider ODBC support is unavailable") - 1U),
        sl_sqlsrv_literal("operation: pool.open", sizeof("operation: pool.open") - 1U),
        sl_str_empty(), sl_str_empty(), sl_str_empty(), sl_status_from_code(SL_STATUS_UNSUPPORTED));
}

SlStatus sl_sqlserver_pool_acquire(SlArena* arena, SlSqlServerPool* pool,
                                   SlSqlServerConnection** out_connection, SlDiag* out_diag)
{
    (void)pool;
    if (out_connection != NULL) {
        *out_connection = NULL;
    }
    return sl_sqlsrv_diag(
        arena, out_diag, SL_DIAG_SQLSERVER_PROVIDER_ERROR,
        sl_sqlsrv_literal("sqlserver provider ODBC support is unavailable",
                          sizeof("sqlserver provider ODBC support is unavailable") - 1U),
        sl_sqlsrv_literal("operation: pool.acquire", sizeof("operation: pool.acquire") - 1U),
        sl_str_empty(), sl_str_empty(), sl_str_empty(), sl_status_from_code(SL_STATUS_UNSUPPORTED));
}

SlStatus sl_sqlserver_pool_release(SlSqlServerPool* pool, SlSqlServerConnection* connection)
{
    (void)pool;
    (void)connection;
    return sl_status_from_code(SL_STATUS_UNSUPPORTED);
}

SlStatus sl_sqlserver_pool_close(SlSqlServerPool* pool)
{
    (void)pool;
    return sl_status_from_code(SL_STATUS_UNSUPPORTED);
}
#endif

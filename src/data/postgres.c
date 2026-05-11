/*
 * src/data/postgres.c
 *
 * Implements Sloppy's bounded PostgreSQL provider boundary over libpq.
 *
 * This module opens caller-owned connection wrappers, executes blocking libpq calls,
 * binds lowered `$1` parameters with PQexecParams, materializes small results into
 * caller-provided arenas, exposes explicit transactions, and provides a tiny bounded pool.
 * It does not add async socket integration, worker-pool offload, migrations, ORM behavior,
 * cancellation/deadlines, or a JavaScript native bridge.
 *
 * Safety invariants:
 * - libpq headers and native handle casts stay in this provider-specific file;
 * - PGresult objects are cleared on every path;
 * - PGconn objects are finished through sl_postgres_close or pool close;
 * - result rows, column names, diagnostics, and text values are copied into caller arenas;
 * - diagnostics never include unredacted connection strings.
 *
 * Tests: tests/unit/data/test_postgres.c.
 */
#include "sloppy/data_postgres.h"

#include "sloppy/builder.h"
#include "sloppy/checked_math.h"
#include "sloppy/container.h"

#include <libpq-fe.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>

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
    SL_PG_OID_JSON = 114,
    SL_PG_OID_NUMERIC = 1700,
    SL_PG_OID_UUID = 2950,
    SL_PG_OID_JSONB = 3802
};

static SlStr sl_pg_literal(const char* ptr, size_t length)
{
    return sl_str_from_parts(ptr, length);
}

static bool sl_pg_str_valid(SlStr str)
{
    return str.length == 0U || str.ptr != NULL;
}

static PGconn* sl_pg_conn(SlPostgresConnection* connection)
{
    if (connection == NULL || !connection->open) {
        return NULL;
    }
    return (PGconn*)connection->handle;
}

static SlStatus sl_pg_copy_str(SlArena* arena, SlStr src, SlStr* out)
{
    if (arena == NULL || out == NULL || !sl_pg_str_valid(src)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_str_copy_view_to_arena(arena, src, out);
}

static SlStatus sl_pg_copy_cstr(SlArena* arena, SlStr src, char** out)
{
    SlSlice storage = {0};
    char* dst = NULL;
    size_t alloc_size = 0U;
    size_t index = 0U;
    SlStatus status;

    if (arena == NULL || out == NULL || !sl_pg_str_valid(src)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_checked_add_size(src.length, 1U, &alloc_size);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_array_alloc(arena, alloc_size, sizeof(char), _Alignof(char), &storage);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    dst = (char*)storage.ptr;
    for (index = 0U; index < src.length; index += 1U) {
        dst[index] = src.ptr[index];
    }
    dst[src.length] = '\0';
    *out = dst;
    return sl_status_ok();
}

static SlStatus sl_pg_copy_bytes(SlArena* arena, SlBytes src, SlBytes* out)
{
    SlOwnedBytes copied = {0};
    SlStatus status;

    if (arena == NULL || out == NULL || (src.length != 0U && src.ptr == NULL)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (src.length == 0U) {
        *out = sl_bytes_empty();
        return sl_status_ok();
    }
    status = sl_bytes_copy_to_arena(arena, src, &copied);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    *out = sl_owned_bytes_as_view(copied);
    return sl_status_ok();
}

static SlStatus sl_pg_diag(SlArena* arena, SlDiag* out_diag, SlDiagCode code, SlStr message,
                           SlStr operation, const char* pg_message, SlStr safe_config, SlStr sql,
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
        &builder, sl_pg_literal("provider: postgres", sizeof("provider: postgres") - 1U));
    if (!sl_status_is_ok(diag_status)) {
        return diag_status;
    }
    diag_status = sl_diag_builder_add_hint(&builder, operation);
    if (!sl_status_is_ok(diag_status)) {
        return diag_status;
    }
    if (!sl_str_is_empty(safe_config)) {
        diag_status = sl_diag_builder_add_hint(&builder, safe_config);
        if (!sl_status_is_ok(diag_status)) {
            return diag_status;
        }
    }
    if (pg_message != NULL && pg_message[0] != '\0') {
        diag_status = sl_diag_builder_add_hint(&builder, sl_str_from_cstr(pg_message));
        if (!sl_status_is_ok(diag_status)) {
            return diag_status;
        }
    }
    else if (!sl_str_is_empty(sql)) {
        diag_status = sl_diag_builder_add_hint(&builder, sql);
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

static SlStatus sl_pg_invalid_state_diag(SlArena* arena, SlDiag* out_diag, SlStr operation)
{
    return sl_pg_diag(
        arena, out_diag, SL_DIAG_POSTGRES_PROVIDER_ERROR,
        sl_pg_literal("postgres provider resource is closed or inactive",
                      sizeof("postgres provider resource is closed or inactive") - 1U),
        operation, NULL, sl_str_empty(), sl_str_empty(),
        sl_status_from_code(SL_STATUS_INVALID_STATE));
}

static bool sl_pg_has_case_insensitive_at(SlStr text, size_t index, const char* word)
{
    size_t offset = 0U;

    while (word[offset] != '\0') {
        char actual;
        char expected = word[offset];

        if (index + offset >= text.length) {
            return false;
        }
        actual = text.ptr[index + offset];
        if (actual >= 'A' && actual <= 'Z') {
            actual = (char)(actual - 'A' + 'a');
        }
        if (actual != expected) {
            return false;
        }
        offset += 1U;
    }
    return true;
}

static size_t sl_pg_redact_keyword_value(char* dst, size_t length, size_t value_start)
{
    size_t cursor = value_start;
    char quote = '\0';

    if (cursor >= length) {
        return cursor;
    }
    if (dst[cursor] == '\'' || dst[cursor] == '"') {
        quote = dst[cursor];
        dst[cursor] = '*';
        cursor += 1U;
        while (cursor < length) {
            const bool escaped = dst[cursor] == '\\';

            if (!escaped && dst[cursor] == quote) {
                dst[cursor] = '*';
                cursor += 1U;
                return cursor;
            }
            dst[cursor] = '*';
            cursor += 1U;
            if (escaped && cursor < length) {
                dst[cursor] = '*';
                cursor += 1U;
            }
        }
        return cursor;
    }
    while (cursor < length && dst[cursor] != ' ' && dst[cursor] != '\t' && dst[cursor] != '&') {
        dst[cursor] = '*';
        cursor += 1U;
    }
    return cursor;
}

SlStatus sl_postgres_redact_connection_string(SlArena* arena, SlStr connection_string, SlStr* out)
{
    SlSlice storage = {0};
    char* dst = NULL;
    size_t alloc_size = 0U;
    size_t index = 0U;
    SlStatus status;

    if (arena == NULL || out == NULL || !sl_pg_str_valid(connection_string)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_checked_add_size(connection_string.length, 1U, &alloc_size);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_array_alloc(arena, alloc_size, sizeof(char), _Alignof(char), &storage);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    dst = (char*)storage.ptr;
    for (index = 0U; index < connection_string.length; index += 1U) {
        dst[index] = connection_string.ptr[index];
    }
    dst[connection_string.length] = '\0';

    for (index = 0U; index < connection_string.length; index += 1U) {
        if (sl_pg_has_case_insensitive_at(connection_string, index, "password=")) {
            index = sl_pg_redact_keyword_value(dst, connection_string.length,
                                               index + sizeof("password=") - 1U);
        }
        if (index + 3U < connection_string.length && connection_string.ptr[index] == ':' &&
            connection_string.ptr[index + 1U] == '/' && connection_string.ptr[index + 2U] == '/')
        {
            size_t authority_start = index + 3U;
            size_t cursor = authority_start;
            size_t colon = connection_string.length;
            size_t at = connection_string.length;

            while (cursor < connection_string.length && connection_string.ptr[cursor] != '/' &&
                   connection_string.ptr[cursor] != ' ' && connection_string.ptr[cursor] != '\t')
            {
                if (connection_string.ptr[cursor] == ':' && colon == connection_string.length) {
                    colon = cursor;
                }
                if (connection_string.ptr[cursor] == '@') {
                    at = cursor;
                    break;
                }
                cursor += 1U;
            }
            if (colon > authority_start && at < connection_string.length && colon + 1U < at) {
                cursor = colon + 1U;
                while (cursor < at) {
                    dst[cursor] = '*';
                    cursor += 1U;
                }
            }
        }
    }
    *out = sl_str_from_parts(dst, connection_string.length);
    return sl_status_ok();
}

static SlStatus sl_pg_safe_config_hint(SlArena* arena, SlStr connection_string, SlStr* out)
{
    SlStr redacted = {0};
    SlStatus status = sl_postgres_redact_connection_string(arena, connection_string, &redacted);

    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_pg_copy_str(arena, redacted, out);
}

SlPostgresOpenOptions sl_postgres_open_options_connection_string(SlStr connection_string)
{
    SlPostgresOpenOptions options;

    options.connection_string = connection_string;
    options.access = SL_POSTGRES_ACCESS_READWRITE;
    return options;
}

SlPostgresPoolOptions sl_postgres_pool_options_connection_string(SlStr connection_string,
                                                                 size_t max_connections)
{
    SlPostgresPoolOptions options;

    options.connection_string = connection_string;
    options.access = SL_POSTGRES_ACCESS_READWRITE;
    options.max_connections = max_connections;
    return options;
}

SlStatus sl_postgres_doctor(SlArena* arena, const SlPostgresOpenOptions* options, SlDiag* out_diag)
{
    SlStr safe = sl_str_empty();
    SlStatus status;

    if (options == NULL || !sl_pg_str_valid(options->connection_string) ||
        sl_str_is_empty(options->connection_string))
    {
        return sl_pg_diag(
            arena, out_diag, SL_DIAG_POSTGRES_PROVIDER_ERROR,
            sl_pg_literal("postgres provider connection string is required",
                          sizeof("postgres provider connection string is required") - 1U),
            sl_pg_literal("operation: doctor", sizeof("operation: doctor") - 1U), NULL,
            sl_str_empty(), sl_str_empty(), sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
    }
    if (options->access != SL_POSTGRES_ACCESS_READ &&
        options->access != SL_POSTGRES_ACCESS_READWRITE)
    {
        return sl_pg_diag(arena, out_diag, SL_DIAG_POSTGRES_PROVIDER_ERROR,
                          sl_pg_literal("postgres provider access option is invalid",
                                        sizeof("postgres provider access option is invalid") - 1U),
                          sl_pg_literal("operation: doctor", sizeof("operation: doctor") - 1U),
                          NULL, sl_str_empty(), sl_str_empty(),
                          sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
    }
    status = sl_pg_safe_config_hint(arena, options->connection_string, &safe);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_pg_diag(
        arena, out_diag, SL_DIAG_POSTGRES_PROVIDER_ERROR,
        sl_pg_literal(
            "postgres provider configuration can be checked by opening a connection",
            sizeof("postgres provider configuration can be checked by opening a connection") - 1U),
        sl_pg_literal("operation: doctor", sizeof("operation: doctor") - 1U), NULL, safe,
        sl_str_empty(), sl_status_ok());
}

SlStatus sl_postgres_open(SlArena* diag_arena, const SlPostgresOpenOptions* options,
                          SlPostgresConnection* out_connection, SlDiag* out_diag)
{
    char* connection_string = NULL;
    PGconn* conn = NULL;
    SlStr safe = sl_str_empty();
    SlStatus status;

    if (out_connection == NULL || options == NULL || !sl_pg_str_valid(options->connection_string) ||
        sl_str_is_empty(options->connection_string))
    {
        return sl_pg_diag(
            diag_arena, out_diag, SL_DIAG_POSTGRES_PROVIDER_ERROR,
            sl_pg_literal("postgres provider connection string is required",
                          sizeof("postgres provider connection string is required") - 1U),
            sl_pg_literal("operation: open", sizeof("operation: open") - 1U), NULL, sl_str_empty(),
            sl_str_empty(), sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
    }
    if (options->access != SL_POSTGRES_ACCESS_READ &&
        options->access != SL_POSTGRES_ACCESS_READWRITE)
    {
        return sl_pg_diag(diag_arena, out_diag, SL_DIAG_POSTGRES_PROVIDER_ERROR,
                          sl_pg_literal("postgres provider access option is invalid",
                                        sizeof("postgres provider access option is invalid") - 1U),
                          sl_pg_literal("operation: open", sizeof("operation: open") - 1U), NULL,
                          sl_str_empty(), sl_str_empty(),
                          sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
    }

    *out_connection = (SlPostgresConnection){0};
    status = sl_pg_copy_cstr(diag_arena, options->connection_string, &connection_string);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_pg_safe_config_hint(diag_arena, options->connection_string, &safe);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    conn = PQconnectdb(connection_string);
    if (conn == NULL) {
        return sl_pg_diag(
            diag_arena, out_diag, SL_DIAG_POSTGRES_PROVIDER_ERROR,
            sl_pg_literal("postgres provider connection allocation failed",
                          sizeof("postgres provider connection allocation failed") - 1U),
            sl_pg_literal("operation: open", sizeof("operation: open") - 1U), NULL, safe,
            sl_str_empty(), sl_status_from_code(SL_STATUS_OUT_OF_MEMORY));
    }
    if (PQstatus(conn) != CONNECTION_OK) {
        const char* message = PQerrorMessage(conn);
        char* message_copy = NULL;

        if (message != NULL && message[0] != '\0') {
            status = sl_pg_copy_cstr(diag_arena, sl_str_from_cstr(message), &message_copy);
            if (!sl_status_is_ok(status)) {
                PQfinish(conn);
                return status;
            }
        }

        PQfinish(conn);
        return sl_pg_diag(diag_arena, out_diag, SL_DIAG_POSTGRES_PROVIDER_ERROR,
                          sl_pg_literal("postgres provider connection failed",
                                        sizeof("postgres provider connection failed") - 1U),
                          sl_pg_literal("operation: open", sizeof("operation: open") - 1U),
                          message_copy, safe, sl_str_empty(),
                          sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
    }

    out_connection->handle = conn;
    out_connection->open = true;
    out_connection->transaction_active = false;
    return sl_status_ok();
}

SlStatus sl_postgres_close(SlPostgresConnection* connection)
{
    if (connection == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (!connection->open) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    PQfinish((PGconn*)connection->handle);
    *connection = (SlPostgresConnection){0};
    return sl_status_ok();
}

static SlStatus sl_pg_param_text(SlArena* arena, const SlPostgresParam* param, char* buffer,
                                 size_t buffer_size, const char** out_value)
{
    char* copied = NULL;
    SlStringBuilder builder = {0};
    SlStr formatted = {0};
    SlStatus status;

    if (param == NULL || out_value == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    switch (param->kind) {
    case SL_POSTGRES_PARAM_NULL:
        *out_value = NULL;
        return sl_status_ok();
    case SL_POSTGRES_PARAM_TEXT:
        if (!sl_pg_str_valid(param->value.text)) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        status = sl_pg_copy_cstr(arena, param->value.text, &copied);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        *out_value = copied;
        return sl_status_ok();
    case SL_POSTGRES_PARAM_INTEGER:
        status = sl_string_builder_init_fixed(&builder, buffer, buffer_size);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_string_builder_append_i64(&builder, param->value.integer);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_string_builder_view_with_nul(&builder, &formatted);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        *out_value = formatted.ptr;
        return sl_status_ok();
    case SL_POSTGRES_PARAM_FLOAT:
        status = sl_string_builder_init_fixed(&builder, buffer, buffer_size);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_string_builder_append_f64(&builder, param->value.number);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_string_builder_view_with_nul(&builder, &formatted);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        *out_value = formatted.ptr;
        return sl_status_ok();
    case SL_POSTGRES_PARAM_BOOL:
        *out_value = param->value.boolean ? "true" : "false";
        return sl_status_ok();
    case SL_POSTGRES_PARAM_DECIMAL:
        if (!sl_pg_str_valid(param->value.decimal)) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        status = sl_pg_copy_cstr(arena, param->value.decimal, &copied);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        *out_value = copied;
        return sl_status_ok();
    case SL_POSTGRES_PARAM_UUID:
        if (!sl_pg_str_valid(param->value.uuid)) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        status = sl_pg_copy_cstr(arena, param->value.uuid, &copied);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        *out_value = copied;
        return sl_status_ok();
    case SL_POSTGRES_PARAM_JSON:
        if (!sl_pg_str_valid(param->value.json)) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        status = sl_pg_copy_cstr(arena, param->value.json, &copied);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        *out_value = copied;
        return sl_status_ok();
    case SL_POSTGRES_PARAM_DATE:
        if (!sl_pg_str_valid(param->value.date)) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        status = sl_pg_copy_cstr(arena, param->value.date, &copied);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        *out_value = copied;
        return sl_status_ok();
    case SL_POSTGRES_PARAM_TIME:
        if (!sl_pg_str_valid(param->value.time)) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        status = sl_pg_copy_cstr(arena, param->value.time, &copied);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        *out_value = copied;
        return sl_status_ok();
    case SL_POSTGRES_PARAM_TIMESTAMP:
        if (!sl_pg_str_valid(param->value.timestamp)) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        status = sl_pg_copy_cstr(arena, param->value.timestamp, &copied);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        *out_value = copied;
        return sl_status_ok();
    case SL_POSTGRES_PARAM_INSTANT:
        if (!sl_pg_str_valid(param->value.instant)) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        status = sl_pg_copy_cstr(arena, param->value.instant, &copied);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        *out_value = copied;
        return sl_status_ok();
    default:
        return sl_status_from_code(SL_STATUS_UNSUPPORTED);
    }
}

static Oid sl_pg_param_oid(SlPostgresParamKind kind)
{
    switch (kind) {
    case SL_POSTGRES_PARAM_BOOL:
        return SL_PG_OID_BOOL;
    case SL_POSTGRES_PARAM_INTEGER:
        return SL_PG_OID_INT8;
    case SL_POSTGRES_PARAM_FLOAT:
        return SL_PG_OID_FLOAT8;
    case SL_POSTGRES_PARAM_TEXT:
        return SL_PG_OID_TEXT;
    case SL_POSTGRES_PARAM_BYTES:
        return SL_PG_OID_BYTEA;
    case SL_POSTGRES_PARAM_DECIMAL:
        return SL_PG_OID_NUMERIC;
    case SL_POSTGRES_PARAM_UUID:
        return SL_PG_OID_UUID;
    case SL_POSTGRES_PARAM_JSON:
        return SL_PG_OID_JSONB;
    case SL_POSTGRES_PARAM_DATE:
        return SL_PG_OID_DATE;
    case SL_POSTGRES_PARAM_TIME:
        return SL_PG_OID_TIME;
    case SL_POSTGRES_PARAM_TIMESTAMP:
        return SL_PG_OID_TIMESTAMP;
    case SL_POSTGRES_PARAM_INSTANT:
        return SL_PG_OID_TIMESTAMPTZ;
    case SL_POSTGRES_PARAM_NULL:
    default:
        return 0;
    }
}

static SlStatus sl_pg_exec_params(SlArena* arena, SlPostgresConnection* connection, SlStr sql,
                                  const SlPostgresParam* params, size_t param_count,
                                  SlStr operation, ExecStatusType expected, PGresult** out_result,
                                  SlDiag* out_diag)
{
    PGconn* conn = sl_pg_conn(connection);
    char* sql_cstr = NULL;
    const char* values[SL_POSTGRES_MAX_PARAMS] = {0};
    Oid types[SL_POSTGRES_MAX_PARAMS] = {0};
    int lengths[SL_POSTGRES_MAX_PARAMS] = {0};
    int formats[SL_POSTGRES_MAX_PARAMS] = {0};
    char buffers[SL_POSTGRES_MAX_PARAMS][96] = {{0}};
    static const char empty_bytes[1] = {0};
    PGresult* result = NULL;
    SlStatus status;

    if (out_result == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_result = NULL;
    if (conn == NULL) {
        return sl_pg_invalid_state_diag(arena, out_diag, operation);
    }
    if (!sl_pg_str_valid(sql) || sl_str_is_empty(sql) || param_count > SL_POSTGRES_MAX_PARAMS ||
        (param_count > 0U && params == NULL))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_pg_copy_cstr(arena, sql, &sql_cstr);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    for (size_t index = 0U; index < param_count; index += 1U) {
        if (params[index].kind == SL_POSTGRES_PARAM_BYTES) {
            if (params[index].value.bytes.length > (size_t)INT32_MAX ||
                (params[index].value.bytes.length != 0U && params[index].value.bytes.ptr == NULL))
            {
                return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            }
            values[index] = params[index].value.bytes.length == 0U
                                ? empty_bytes
                                : (const char*)params[index].value.bytes.ptr;
            types[index] = sl_pg_param_oid(params[index].kind);
            lengths[index] = (int)params[index].value.bytes.length;
            formats[index] = 1;
            continue;
        }
        status = sl_pg_param_text(arena, &params[index], buffers[index], sizeof(buffers[index]),
                                  &values[index]);
        if (sl_status_code(status) == SL_STATUS_UNSUPPORTED) {
            return sl_pg_diag(arena, out_diag, SL_DIAG_DATABASE_UNSUPPORTED_VALUE,
                              sl_pg_literal("unsupported postgres parameter value",
                                            sizeof("unsupported postgres parameter value") - 1U),
                              operation, NULL, sl_str_empty(), sql,
                              sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
        }
        if (!sl_status_is_ok(status)) {
            return status;
        }
        types[index] = sl_pg_param_oid(params[index].kind);
    }

    result = PQexecParams(conn, sql_cstr, (int)param_count, types, values, lengths, formats, 0);
    if (result == NULL) {
        return sl_pg_diag(arena, out_diag, SL_DIAG_POSTGRES_PROVIDER_ERROR,
                          sl_pg_literal("postgres provider query allocation failed",
                                        sizeof("postgres provider query allocation failed") - 1U),
                          operation, PQerrorMessage(conn), sl_str_empty(), sql,
                          sl_status_from_code(SL_STATUS_OUT_OF_MEMORY));
    }
    if (PQresultStatus(result) != expected) {
        const char* message = PQresultErrorMessage(result);

        PQclear(result);
        return sl_pg_diag(arena, out_diag, SL_DIAG_POSTGRES_PROVIDER_ERROR,
                          sl_pg_literal("postgres provider query failed",
                                        sizeof("postgres provider query failed") - 1U),
                          operation, message, sl_str_empty(), sql,
                          sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
    }
    *out_result = result;
    return sl_status_ok();
}

static SlStatus sl_pg_copy_columns(SlArena* arena, PGresult* result, size_t column_count,
                                   SlStr** out_column_names)
{
    SlSlice names_slice = {0};
    SlStr* names = NULL;
    SlStatus status;

    if (arena == NULL || result == NULL || out_column_names == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_column_names = NULL;
    if (column_count == 0U) {
        return sl_status_ok();
    }
    status =
        sl_arena_array_alloc(arena, column_count, sizeof(SlStr), _Alignof(SlStr), &names_slice);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    names = (SlStr*)names_slice.ptr;
    for (size_t index = 0U; index < column_count; index += 1U) {
        status =
            sl_pg_copy_str(arena, sl_str_from_cstr(PQfname(result, (int)index)), &names[index]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    *out_column_names = names;
    return sl_status_ok();
}

static SlStatus sl_pg_copy_value(SlArena* arena, PGresult* result, int row, int column,
                                 SlPostgresValue* out)
{
    SlStr text;
    char* end = NULL;
    long long integer = 0;
    double number = 0.0;
    const int oid = (int)PQftype(result, column);

    if (arena == NULL || result == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (PQgetisnull(result, row, column) != 0) {
        *out = (SlPostgresValue){.kind = SL_POSTGRES_VALUE_NULL};
        return sl_status_ok();
    }

    text = sl_str_from_parts(PQgetvalue(result, row, column),
                             (size_t)PQgetlength(result, row, column));
    if (oid == SL_PG_OID_BOOL && text.length == 1U) {
        out->kind = SL_POSTGRES_VALUE_BOOL;
        out->value.boolean = text.ptr[0] == 't';
        return sl_status_ok();
    }
    if (oid == SL_PG_OID_INT2 || oid == SL_PG_OID_INT4 || oid == SL_PG_OID_INT8) {
        errno = 0;
        integer = strtoll(text.ptr, &end, 10);
        if (errno == 0 && end == text.ptr + text.length) {
            out->kind = SL_POSTGRES_VALUE_INTEGER;
            out->value.integer = (int64_t)integer;
            return sl_status_ok();
        }
    }
    if (oid == SL_PG_OID_FLOAT4 || oid == SL_PG_OID_FLOAT8 || oid == SL_PG_OID_NUMERIC) {
        if (oid == SL_PG_OID_NUMERIC) {
            out->kind = SL_POSTGRES_VALUE_DECIMAL;
            return sl_pg_copy_str(arena, text, &out->value.decimal);
        }
        errno = 0;
        number = strtod(text.ptr, &end);
        if (errno == 0 && end == text.ptr + text.length) {
            out->kind = SL_POSTGRES_VALUE_FLOAT;
            out->value.number = number;
            return sl_status_ok();
        }
    }
    if (oid == SL_PG_OID_BYTEA) {
        size_t length = 0U;
        unsigned char* unescaped = PQunescapeBytea((const unsigned char*)text.ptr, &length);
        SlStatus status;
        if (unescaped == NULL) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        out->kind = SL_POSTGRES_VALUE_BYTES;
        status = sl_pg_copy_bytes(arena, sl_bytes_from_parts(unescaped, length), &out->value.bytes);
        PQfreemem(unescaped);
        return status;
    }
    if (oid == SL_PG_OID_UUID) {
        out->kind = SL_POSTGRES_VALUE_UUID;
        return sl_pg_copy_str(arena, text, &out->value.uuid);
    }
    if (oid == SL_PG_OID_JSON || oid == SL_PG_OID_JSONB) {
        out->kind = SL_POSTGRES_VALUE_JSON;
        return sl_pg_copy_str(arena, text, &out->value.json);
    }
    if (oid == SL_PG_OID_DATE) {
        out->kind = SL_POSTGRES_VALUE_DATE;
        return sl_pg_copy_str(arena, text, &out->value.date);
    }
    if (oid == SL_PG_OID_TIME) {
        out->kind = SL_POSTGRES_VALUE_TIME;
        return sl_pg_copy_str(arena, text, &out->value.time);
    }
    if (oid == SL_PG_OID_TIMESTAMP) {
        out->kind = SL_POSTGRES_VALUE_TIMESTAMP;
        return sl_pg_copy_str(arena, text, &out->value.timestamp);
    }
    if (oid == SL_PG_OID_TIMESTAMPTZ) {
        out->kind = SL_POSTGRES_VALUE_INSTANT;
        return sl_pg_copy_str(arena, text, &out->value.instant);
    }
    (void)SL_PG_OID_TEXT;
    (void)SL_PG_OID_VARCHAR;
    out->kind = SL_POSTGRES_VALUE_TEXT;
    return sl_pg_copy_str(arena, text, &out->value.text);
}

static SlStatus sl_pg_materialize_rows(SlArena* arena, PGresult* result, size_t max_rows,
                                       SlPostgresResult* out_result)
{
    const size_t row_count = (size_t)PQntuples(result);
    const size_t column_count = (size_t)PQnfields(result);
    SlSlice row_slice = {0};
    SlSlice cell_slice = {0};
    SlPostgresRow* rows = NULL;
    SlPostgresValue* cells = NULL;
    size_t cell_count = 0U;
    SlStatus status;

    if (arena == NULL || result == NULL || out_result == NULL || max_rows == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (row_count > max_rows) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }
    *out_result = (SlPostgresResult){0};
    status = sl_pg_copy_columns(arena, result, column_count, &out_result->column_names);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (row_count > 0U) {
        status = sl_arena_array_alloc(arena, row_count, sizeof(SlPostgresRow),
                                      _Alignof(SlPostgresRow), &row_slice);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        rows = (SlPostgresRow*)row_slice.ptr;

        status = sl_checked_mul_size(row_count, column_count, &cell_count);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        if (cell_count > 0U) {
            status = sl_arena_array_alloc(arena, cell_count, sizeof(SlPostgresValue),
                                          _Alignof(SlPostgresValue), &cell_slice);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            cells = (SlPostgresValue*)cell_slice.ptr;
        }
        for (size_t row = 0U; row < row_count; row += 1U) {
            rows[row].values = cells + (row * column_count);
            for (size_t column = 0U; column < column_count; column += 1U) {
                status = sl_pg_copy_value(arena, result, (int)row, (int)column,
                                          &rows[row].values[column]);
                if (!sl_status_is_ok(status)) {
                    return status;
                }
            }
        }
    }
    out_result->column_count = column_count;
    out_result->row_count = row_count;
    out_result->rows = rows;
    return sl_status_ok();
}

SlStatus sl_postgres_exec(SlArena* arena, SlPostgresConnection* connection, SlStr sql,
                          const SlPostgresParam* params, size_t param_count,
                          SlPostgresExecResult* out_result, SlDiag* out_diag)
{
    PGresult* result = NULL;
    const char* tuples = NULL;
    SlStatus status;

    if (out_result == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_result = (SlPostgresExecResult){0};
    status = sl_pg_exec_params(arena, connection, sql, params, param_count,
                               sl_pg_literal("operation: exec", sizeof("operation: exec") - 1U),
                               PGRES_COMMAND_OK, &result, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    tuples = PQcmdTuples(result);
    if (tuples != NULL && tuples[0] != '\0') {
        out_result->affected_rows = (int64_t)strtoll(tuples, NULL, 10);
        out_result->affected_rows_known = true;
    }
    PQclear(result);
    return sl_status_ok();
}

SlStatus sl_postgres_exec_batch(SlArena* arena, SlPostgresConnection* connection, SlStr sql,
                                SlPostgresExecResult* out_result, SlDiag* out_diag)
{
    PGconn* conn = sl_pg_conn(connection);
    char* sql_cstr = NULL;
    PGresult* result = NULL;
    const char* tuples = NULL;
    ExecStatusType result_status;
    SlStatus status;

    if (out_result == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_result = (SlPostgresExecResult){0};
    if (conn == NULL) {
        return sl_pg_invalid_state_diag(
            arena, out_diag,
            sl_pg_literal("operation: execBatch", sizeof("operation: execBatch") - 1U));
    }
    if (!sl_pg_str_valid(sql) || sl_str_is_empty(sql)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_pg_copy_cstr(arena, sql, &sql_cstr);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    result = PQexec(conn, sql_cstr);
    if (result == NULL) {
        return sl_pg_diag(
            arena, out_diag, SL_DIAG_POSTGRES_PROVIDER_ERROR,
            sl_pg_literal("postgres provider batch allocation failed",
                          sizeof("postgres provider batch allocation failed") - 1U),
            sl_pg_literal("operation: execBatch", sizeof("operation: execBatch") - 1U),
            PQerrorMessage(conn), sl_str_empty(), sql,
            sl_status_from_code(SL_STATUS_OUT_OF_MEMORY));
    }
    result_status = PQresultStatus(result);
    if (result_status != PGRES_COMMAND_OK && result_status != PGRES_TUPLES_OK) {
        const char* message = PQresultErrorMessage(result);

        PQclear(result);
        return sl_pg_diag(
            arena, out_diag, SL_DIAG_POSTGRES_PROVIDER_ERROR,
            sl_pg_literal("postgres provider batch failed",
                          sizeof("postgres provider batch failed") - 1U),
            sl_pg_literal("operation: execBatch", sizeof("operation: execBatch") - 1U), message,
            sl_str_empty(), sql, sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
    }
    tuples = PQcmdTuples(result);
    if (tuples != NULL && tuples[0] != '\0') {
        out_result->affected_rows = (int64_t)strtoll(tuples, NULL, 10);
        out_result->affected_rows_known = true;
    }
    PQclear(result);
    return sl_status_ok();
}

SlStatus sl_postgres_query(SlArena* arena, SlPostgresConnection* connection, SlStr sql,
                           const SlPostgresParam* params, size_t param_count,
                           const SlPostgresQueryOptions* options, SlPostgresResult* out_result,
                           SlDiag* out_diag)
{
    PGresult* result = NULL;
    SlArenaMark mark = {0};
    SlPostgresResult temp = {0};
    size_t max_rows = SL_POSTGRES_DEFAULT_MAX_ROWS;
    SlStatus status;

    if (out_result == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_result = (SlPostgresResult){0};
    if (options != NULL && options->max_rows > 0U) {
        max_rows = options->max_rows;
    }
    status = sl_pg_exec_params(arena, connection, sql, params, param_count,
                               sl_pg_literal("operation: query", sizeof("operation: query") - 1U),
                               PGRES_TUPLES_OK, &result, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    mark = sl_arena_mark(arena);
    status = sl_pg_materialize_rows(arena, result, max_rows, &temp);
    PQclear(result);
    if (!sl_status_is_ok(status)) {
        sl_arena_reset_to(arena, mark);
        *out_result = (SlPostgresResult){0};
        return status;
    }
    *out_result = temp;
    return status;
}

static SlStatus sl_pg_materialize_first_row(SlArena* arena, PGresult* result,
                                            SlPostgresQueryOneResult* out_result)
{
    const size_t row_count = (size_t)PQntuples(result);
    const size_t column_count = (size_t)PQnfields(result);
    SlSlice value_slice = {0};
    SlPostgresValue* values = NULL;
    SlStatus status;

    if (arena == NULL || result == NULL || out_result == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_result = (SlPostgresQueryOneResult){0};
    status = sl_pg_copy_columns(arena, result, column_count, &out_result->column_names);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    out_result->column_count = column_count;
    if (row_count == 0U) {
        return sl_status_ok();
    }
    if (column_count > 0U) {
        status = sl_arena_array_alloc(arena, column_count, sizeof(SlPostgresValue),
                                      _Alignof(SlPostgresValue), &value_slice);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        values = (SlPostgresValue*)value_slice.ptr;
        for (size_t column = 0U; column < column_count; column += 1U) {
            status = sl_pg_copy_value(arena, result, 0, (int)column, &values[column]);
            if (!sl_status_is_ok(status)) {
                return status;
            }
        }
    }
    out_result->found = true;
    out_result->values = values;
    return sl_status_ok();
}

SlStatus sl_postgres_query_one(SlArena* arena, SlPostgresConnection* connection, SlStr sql,
                               const SlPostgresParam* params, size_t param_count,
                               SlPostgresQueryOneResult* out_result, SlDiag* out_diag)
{
    PGresult* result = NULL;
    SlArenaMark mark = {0};
    SlPostgresQueryOneResult temp = {0};
    SlStatus status;

    if (out_result == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_result = (SlPostgresQueryOneResult){0};
    status =
        sl_pg_exec_params(arena, connection, sql, params, param_count,
                          sl_pg_literal("operation: queryOne", sizeof("operation: queryOne") - 1U),
                          PGRES_TUPLES_OK, &result, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    mark = sl_arena_mark(arena);
    status = sl_pg_materialize_first_row(arena, result, &temp);
    PQclear(result);
    if (!sl_status_is_ok(status)) {
        sl_arena_reset_to(arena, mark);
        *out_result = (SlPostgresQueryOneResult){0};
        return status;
    }
    *out_result = temp;
    return status;
}

SlStatus sl_postgres_transaction_begin(SlArena* arena, SlPostgresConnection* connection,
                                       SlPostgresTransaction* out_tx, SlDiag* out_diag)
{
    SlPostgresExecResult result = {0};
    SlStatus status;

    if (out_tx == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_tx = (SlPostgresTransaction){0};
    if (connection == NULL || !connection->open) {
        return sl_pg_invalid_state_diag(arena, out_diag,
                                        sl_pg_literal("operation: transaction.begin",
                                                      sizeof("operation: transaction.begin") - 1U));
    }
    if (connection->transaction_active) {
        return sl_pg_diag(
            arena, out_diag, SL_DIAG_POSTGRES_PROVIDER_ERROR,
            sl_pg_literal("postgres provider nested transactions are not supported",
                          sizeof("postgres provider nested transactions are not supported") - 1U),
            sl_pg_literal("operation: transaction.begin",
                          sizeof("operation: transaction.begin") - 1U),
            NULL, sl_str_empty(), sl_str_empty(), sl_status_from_code(SL_STATUS_INVALID_STATE));
    }
    status = sl_postgres_exec(arena, connection, sl_pg_literal("BEGIN", sizeof("BEGIN") - 1U), NULL,
                              0U, &result, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    connection->transaction_active = true;
    out_tx->connection = connection;
    out_tx->active = true;
    return sl_status_ok();
}

SlStatus sl_postgres_transaction_commit(SlArena* arena, SlPostgresTransaction* tx, SlDiag* out_diag)
{
    SlPostgresExecResult result = {0};
    SlStatus status;

    if (tx == NULL || tx->connection == NULL || !tx->active) {
        return sl_pg_invalid_state_diag(
            arena, out_diag,
            sl_pg_literal("operation: transaction.commit",
                          sizeof("operation: transaction.commit") - 1U));
    }
    status = sl_postgres_exec(arena, tx->connection, sl_pg_literal("COMMIT", sizeof("COMMIT") - 1U),
                              NULL, 0U, &result, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    tx->connection->transaction_active = false;
    tx->connection = NULL;
    tx->active = false;
    return sl_status_ok();
}

SlStatus sl_postgres_transaction_rollback(SlArena* arena, SlPostgresTransaction* tx,
                                          SlDiag* out_diag)
{
    SlPostgresExecResult result = {0};
    SlStatus status;

    if (tx == NULL || tx->connection == NULL || !tx->active) {
        return sl_pg_invalid_state_diag(
            arena, out_diag,
            sl_pg_literal("operation: transaction.rollback",
                          sizeof("operation: transaction.rollback") - 1U));
    }
    status =
        sl_postgres_exec(arena, tx->connection, sl_pg_literal("ROLLBACK", sizeof("ROLLBACK") - 1U),
                         NULL, 0U, &result, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    tx->connection->transaction_active = false;
    tx->connection = NULL;
    tx->active = false;
    return sl_status_ok();
}

SlStatus sl_postgres_transaction_exec(SlArena* arena, SlPostgresTransaction* tx, SlStr sql,
                                      const SlPostgresParam* params, size_t param_count,
                                      SlPostgresExecResult* out_result, SlDiag* out_diag)
{
    if (tx == NULL || tx->connection == NULL || !tx->active) {
        return sl_pg_invalid_state_diag(
            arena, out_diag, sl_pg_literal("operation: exec", sizeof("operation: exec") - 1U));
    }
    return sl_postgres_exec(arena, tx->connection, sql, params, param_count, out_result, out_diag);
}

SlStatus sl_postgres_transaction_exec_batch(SlArena* arena, SlPostgresTransaction* tx, SlStr sql,
                                            SlPostgresExecResult* out_result, SlDiag* out_diag)
{
    if (tx == NULL || tx->connection == NULL || !tx->active) {
        return sl_pg_invalid_state_diag(
            arena, out_diag,
            sl_pg_literal("operation: execBatch", sizeof("operation: execBatch") - 1U));
    }
    return sl_postgres_exec_batch(arena, tx->connection, sql, out_result, out_diag);
}

SlStatus sl_postgres_transaction_query(SlArena* arena, SlPostgresTransaction* tx, SlStr sql,
                                       const SlPostgresParam* params, size_t param_count,
                                       const SlPostgresQueryOptions* options,
                                       SlPostgresResult* out_result, SlDiag* out_diag)
{
    if (tx == NULL || tx->connection == NULL || !tx->active) {
        return sl_pg_invalid_state_diag(
            arena, out_diag, sl_pg_literal("operation: query", sizeof("operation: query") - 1U));
    }
    return sl_postgres_query(arena, tx->connection, sql, params, param_count, options, out_result,
                             out_diag);
}

SlStatus sl_postgres_transaction_query_one(SlArena* arena, SlPostgresTransaction* tx, SlStr sql,
                                           const SlPostgresParam* params, size_t param_count,
                                           SlPostgresQueryOneResult* out_result, SlDiag* out_diag)
{
    if (tx == NULL || tx->connection == NULL || !tx->active) {
        return sl_pg_invalid_state_diag(
            arena, out_diag,
            sl_pg_literal("operation: queryOne", sizeof("operation: queryOne") - 1U));
    }
    return sl_postgres_query_one(arena, tx->connection, sql, params, param_count, out_result,
                                 out_diag);
}

SlStatus sl_postgres_pool_open(SlArena* arena, const SlPostgresPoolOptions* options,
                               SlPostgresPool* out_pool, SlDiag* out_diag)
{
    SlStatus status;

    if (out_pool == NULL || options == NULL || !sl_pg_str_valid(options->connection_string) ||
        sl_str_is_empty(options->connection_string) || options->max_connections == 0U ||
        options->max_connections > SL_POSTGRES_MAX_POOL_CONNECTIONS ||
        (options->access != SL_POSTGRES_ACCESS_READ &&
         options->access != SL_POSTGRES_ACCESS_READWRITE))
    {
        return sl_pg_diag(
            arena, out_diag, SL_DIAG_POSTGRES_PROVIDER_ERROR,
            sl_pg_literal("postgres provider pool options are invalid",
                          sizeof("postgres provider pool options are invalid") - 1U),
            sl_pg_literal("operation: pool.open", sizeof("operation: pool.open") - 1U), NULL,
            sl_str_empty(), sl_str_empty(), sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
    }
    *out_pool = (SlPostgresPool){0};
    status = sl_pg_copy_str(arena, options->connection_string, &out_pool->connection_string);
    if (!sl_status_is_ok(status)) {
        return sl_pg_diag(
            arena, out_diag, SL_DIAG_POSTGRES_PROVIDER_ERROR,
            sl_pg_literal("postgres provider pool connection string copy failed",
                          sizeof("postgres provider pool connection string copy failed") - 1U),
            sl_pg_literal("operation: pool.open", sizeof("operation: pool.open") - 1U), NULL,
            sl_str_empty(), sl_str_empty(), status);
    }
    out_pool->access = options->access;
    out_pool->max_connections = options->max_connections;
    return sl_status_ok();
}

SlStatus sl_postgres_pool_acquire(SlArena* arena, SlPostgresPool* pool,
                                  SlPostgresConnection** out_connection, SlDiag* out_diag)
{
    SlPostgresOpenOptions options;
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
        return sl_pg_diag(
            arena, out_diag, SL_DIAG_POSTGRES_POOL_EXHAUSTED,
            sl_pg_literal("postgres provider pool is exhausted",
                          sizeof("postgres provider pool is exhausted") - 1U),
            sl_pg_literal("operation: pool.acquire", sizeof("operation: pool.acquire") - 1U), NULL,
            sl_str_empty(), sl_str_empty(), sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED));
    }
    options = sl_postgres_open_options_connection_string(pool->connection_string);
    options.access = pool->access;
    status = sl_postgres_open(arena, &options, &pool->connections[pool->open_count], out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    *out_connection = &pool->connections[pool->open_count];
    pool->idle[pool->open_count] = false;
    pool->open_count += 1U;
    return sl_status_ok();
}

SlStatus sl_postgres_pool_release(SlPostgresPool* pool, SlPostgresConnection* connection)
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

SlStatus sl_postgres_pool_close(SlPostgresPool* pool)
{
    if (pool == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (pool->closed) {
        return sl_status_ok();
    }
    for (size_t index = 0U; index < pool->open_count; index += 1U) {
        if (pool->connections[index].open) {
            sl_postgres_close(&pool->connections[index]);
        }
        pool->idle[index] = false;
    }
    pool->closed = true;
    pool->open_count = 0U;
    return sl_status_ok();
}

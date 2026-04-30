/*
 * src/data/sqlite.c
 *
 * Implements Sloppy's first real database provider boundary over SQLite.
 *
 * This module is intentionally direct: it opens caller-owned connection wrappers, binds
 * lowered `?` parameters, materializes small query results into caller-provided arenas,
 * and exposes explicit transaction begin/commit/rollback helpers. It does not add pooling,
 * migrations, async worker execution, SQL parsing, a generic provider registry, or JS
 * native bindings.
 *
 * Safety invariants:
 * - sqlite3 headers and native handle casts stay in this provider-specific file;
 * - SQLite statements are finalized on every path;
 * - result rows, column names, diagnostics, and text/blob values are copied into caller
 *   arenas before SQLite statement lifetime can invalidate them;
 * - parameter text/blob copy helpers provide operation-owned inputs for future offload;
 * - JavaScript-facing bootstrap code never receives this module's native pointer.
 *
 * Tests: tests/unit/data/test_sqlite.c.
 */
#include "sloppy/data_sqlite.h"

#include "sloppy/checked_math.h"

#include <limits.h>
#include <sqlite3.h>

static const unsigned char sl_sqlite_empty_blob_sentinel = 0U;

static SlStr sl_sqlite_literal(const char* ptr, size_t length)
{
    return sl_str_from_parts(ptr, length);
}

static bool sl_sqlite_str_valid(SlStr str)
{
    return str.length == 0U || str.ptr != NULL;
}

static sqlite3* sl_sqlite_db(SlSqliteConnection* connection)
{
    if (connection == NULL || !connection->open) {
        return NULL;
    }

    return (sqlite3*)connection->handle;
}

static SlStatus sl_sqlite_diag(SlArena* arena, SlDiag* out_diag, SlDiagCode code, SlStr message,
                               SlStr operation, const char* sqlite_message, SlStr sql,
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
        &builder, sl_sqlite_literal("provider: sqlite", sizeof("provider: sqlite") - 1U));
    if (!sl_status_is_ok(diag_status)) {
        return diag_status;
    }

    diag_status = sl_diag_builder_add_hint(&builder, operation);
    if (!sl_status_is_ok(diag_status)) {
        return diag_status;
    }

    if (sqlite_message != NULL && sqlite_message[0] != '\0') {
        diag_status = sl_diag_builder_add_hint(&builder, sl_str_from_cstr(sqlite_message));
        if (!sl_status_is_ok(diag_status)) {
            return diag_status;
        }
    }

    if (!sl_str_is_empty(sql)) {
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

static SlStatus sl_sqlite_invalid_state_diag(SlArena* arena, SlDiag* out_diag, SlStr operation)
{
    return sl_sqlite_diag(
        arena, out_diag, SL_DIAG_SQLITE_PROVIDER_ERROR,
        sl_sqlite_literal("sqlite provider resource is closed or inactive",
                          sizeof("sqlite provider resource is closed or inactive") - 1U),
        operation, NULL, sl_str_empty(), sl_status_from_code(SL_STATUS_INVALID_STATE));
}

static void sl_sqlite_sync_transaction_state(SlSqliteConnection* connection)
{
    sqlite3* db = sl_sqlite_db(connection);

    if (db != NULL) {
        connection->transaction_active = sqlite3_get_autocommit(db) == 0;
    }
}

static bool sl_sqlite_is_ascii_space(char value)
{
    return value == ' ' || value == '\t' || value == '\n' || value == '\r' || value == '\v' ||
           value == '\f';
}

static SlStatus sl_sqlite_bind_params(sqlite3_stmt* stmt, const SlSqliteParam* params,
                                      size_t param_count, SlArena* arena, SlDiag* out_diag,
                                      SlStr operation, SlStr sql)
{
    const int bind_count = sqlite3_bind_parameter_count(stmt);
    size_t index = 0U;
    int rc = SQLITE_OK;

    if (param_count > 0U && params == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (param_count > (size_t)INT_MAX) {
        return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
    }

    if (bind_count < 0 || (size_t)bind_count != param_count) {
        return sl_sqlite_diag(
            arena, out_diag, SL_DIAG_SQLITE_PROVIDER_ERROR,
            sl_sqlite_literal("sqlite provider parameter count mismatch",
                              sizeof("sqlite provider parameter count mismatch") - 1U),
            operation, sqlite3_errmsg(sqlite3_db_handle(stmt)), sql,
            sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
    }

    for (index = 0U; index < param_count; index += 1U) {
        const int sqlite_index = (int)index + 1;
        const SlSqliteParam* param = &params[index];

        switch (param->kind) {
        case SL_SQLITE_PARAM_NULL:
            rc = sqlite3_bind_null(stmt, sqlite_index);
            break;
        case SL_SQLITE_PARAM_TEXT:
            if (!sl_sqlite_str_valid(param->value.text) ||
                param->value.text.length > (size_t)INT_MAX)
            {
                return sl_sqlite_diag(
                    arena, out_diag, SL_DIAG_DATABASE_UNSUPPORTED_VALUE,
                    sl_sqlite_literal("unsupported sqlite parameter value",
                                      sizeof("unsupported sqlite parameter value") - 1U),
                    operation, NULL, sql, sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
            }
            rc = sqlite3_bind_text(stmt, sqlite_index, param->value.text.ptr,
                                   (int)param->value.text.length, SQLITE_TRANSIENT);
            break;
        case SL_SQLITE_PARAM_INTEGER:
            rc = sqlite3_bind_int64(stmt, sqlite_index, (sqlite3_int64)param->value.integer);
            break;
        case SL_SQLITE_PARAM_FLOAT:
            rc = sqlite3_bind_double(stmt, sqlite_index, param->value.number);
            break;
        case SL_SQLITE_PARAM_BOOL:
            rc = sqlite3_bind_int(stmt, sqlite_index, param->value.boolean ? 1 : 0);
            break;
        case SL_SQLITE_PARAM_BLOB:
            if ((param->value.blob.length != 0U && param->value.blob.ptr == NULL) ||
                param->value.blob.length > (size_t)INT_MAX)
            {
                return sl_sqlite_diag(
                    arena, out_diag, SL_DIAG_DATABASE_UNSUPPORTED_VALUE,
                    sl_sqlite_literal("unsupported sqlite parameter value",
                                      sizeof("unsupported sqlite parameter value") - 1U),
                    operation, NULL, sql, sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
            }
            rc = sqlite3_bind_blob(stmt, sqlite_index,
                                   param->value.blob.length == 0U ? &sl_sqlite_empty_blob_sentinel
                                                                  : param->value.blob.ptr,
                                   (int)param->value.blob.length, SQLITE_TRANSIENT);
            break;
        default:
            return sl_sqlite_diag(
                arena, out_diag, SL_DIAG_DATABASE_UNSUPPORTED_VALUE,
                sl_sqlite_literal("unsupported sqlite parameter value",
                                  sizeof("unsupported sqlite parameter value") - 1U),
                operation, NULL, sql, sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
        }

        if (rc != SQLITE_OK) {
            return sl_sqlite_diag(arena, out_diag, SL_DIAG_SQLITE_PROVIDER_ERROR,
                                  sl_sqlite_literal("sqlite provider bind failed",
                                                    sizeof("sqlite provider bind failed") - 1U),
                                  operation, sqlite3_errmsg(sqlite3_db_handle(stmt)), sql,
                                  sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
        }
    }

    return sl_status_ok();
}

static SlStatus sl_sqlite_prepare(SlArena* arena, SlSqliteConnection* connection, SlStr sql,
                                  SlStr operation, sqlite3_stmt** out_stmt, SlDiag* out_diag)
{
    sqlite3* db = sl_sqlite_db(connection);
    sqlite3_stmt* stmt = NULL;
    const char* tail = NULL;
    const char* end = NULL;
    int rc = SQLITE_OK;

    if (out_stmt == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_stmt = NULL;

    if (db == NULL) {
        return sl_sqlite_invalid_state_diag(arena, out_diag, operation);
    }

    if (!sl_sqlite_str_valid(sql) || sl_str_is_empty(sql) || sql.length > (size_t)INT_MAX) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    rc = sqlite3_prepare_v2(db, sql.ptr, (int)sql.length, &stmt, &tail);
    if (rc != SQLITE_OK) {
        return sl_sqlite_diag(arena, out_diag, SL_DIAG_SQLITE_PROVIDER_ERROR,
                              sl_sqlite_literal("sqlite provider prepare failed",
                                                sizeof("sqlite provider prepare failed") - 1U),
                              operation, sqlite3_errmsg(db), sql,
                              sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
    }

    if (stmt == NULL) {
        return sl_sqlite_diag(
            arena, out_diag, SL_DIAG_SQLITE_PROVIDER_ERROR,
            sl_sqlite_literal("sqlite provider prepare produced no statement",
                              sizeof("sqlite provider prepare produced no statement") - 1U),
            operation, sqlite3_errmsg(db), sql, sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
    }

    end = sql.ptr + sql.length;
    while (tail != NULL && tail < end && sl_sqlite_is_ascii_space(*tail)) {
        tail += 1;
    }
    if (tail != NULL && tail < end) {
        sqlite3_finalize(stmt);
        return sl_sqlite_diag(
            arena, out_diag, SL_DIAG_SQLITE_PROVIDER_ERROR,
            sl_sqlite_literal("sqlite provider rejected trailing SQL",
                              sizeof("sqlite provider rejected trailing SQL") - 1U),
            operation, sqlite3_errmsg(db), sql, sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
    }

    *out_stmt = stmt;
    return sl_status_ok();
}

static SlStatus sl_sqlite_copy_columns(SlArena* arena, sqlite3_stmt* stmt, size_t column_count,
                                       SlStr** out_column_names)
{
    void* ptr = NULL;
    SlStr* names = NULL;
    size_t alloc_size = 0U;
    size_t index = 0U;
    SlStatus status;

    if (arena == NULL || stmt == NULL || out_column_names == NULL) {
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
    for (index = 0U; index < column_count; index += 1U) {
        const char* name = sqlite3_column_name(stmt, (int)index);
        status = sl_sqlite_copy_result_text_to_arena(arena, sl_str_from_cstr(name), &names[index]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    *out_column_names = names;
    return sl_status_ok();
}

static SlStatus sl_sqlite_copy_value(SlArena* arena, sqlite3_stmt* stmt, size_t column,
                                     SlSqliteValue* out)
{
    int type = SQLITE_NULL;
    int byte_count = 0;
    const unsigned char* text = NULL;
    const void* blob = NULL;

    if (arena == NULL || stmt == NULL || out == NULL || column > (size_t)INT_MAX) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    type = sqlite3_column_type(stmt, (int)column);
    switch (type) {
    case SQLITE_NULL:
        *out = (SlSqliteValue){.kind = SL_SQLITE_VALUE_NULL};
        return sl_status_ok();
    case SQLITE_INTEGER:
        *out = (SlSqliteValue){.kind = SL_SQLITE_VALUE_INTEGER,
                               .value.integer = (int64_t)sqlite3_column_int64(stmt, (int)column)};
        return sl_status_ok();
    case SQLITE_FLOAT:
        *out = (SlSqliteValue){.kind = SL_SQLITE_VALUE_FLOAT,
                               .value.number = sqlite3_column_double(stmt, (int)column)};
        return sl_status_ok();
    case SQLITE_TEXT:
        text = sqlite3_column_text(stmt, (int)column);
        byte_count = sqlite3_column_bytes(stmt, (int)column);
        if (byte_count < 0) {
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
        out->kind = SL_SQLITE_VALUE_TEXT;
        return sl_sqlite_copy_result_text_to_arena(
            arena, sl_str_from_parts((const char*)text, (size_t)byte_count), &out->value.text);
    case SQLITE_BLOB:
        blob = sqlite3_column_blob(stmt, (int)column);
        byte_count = sqlite3_column_bytes(stmt, (int)column);
        if (byte_count < 0) {
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
        out->kind = SL_SQLITE_VALUE_BLOB;
        return sl_sqlite_copy_result_blob_to_arena(
            arena, sl_bytes_from_parts((const unsigned char*)blob, (size_t)byte_count),
            &out->value.blob);
    default:
        return sl_status_from_code(SL_STATUS_UNSUPPORTED);
    }
}

static SlStatus sl_sqlite_allocate_rows(SlArena* arena, size_t max_rows, size_t column_count,
                                        SlSqliteRow** out_rows, SlSqliteValue** out_cells)
{
    void* row_ptr = NULL;
    void* cell_ptr = NULL;
    size_t row_alloc_size = 0U;
    size_t cell_count = 0U;
    size_t cell_alloc_size = 0U;
    SlStatus status;

    if (arena == NULL || out_rows == NULL || out_cells == NULL || max_rows == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_rows = NULL;
    *out_cells = NULL;

    status = sl_checked_mul_size(max_rows, sizeof(SlSqliteRow), &row_alloc_size);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(arena, row_alloc_size, _Alignof(SlSqliteRow), &row_ptr);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (column_count > 0U) {
        status = sl_checked_mul_size(max_rows, column_count, &cell_count);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_checked_mul_size(cell_count, sizeof(SlSqliteValue), &cell_alloc_size);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_arena_alloc(arena, cell_alloc_size, _Alignof(SlSqliteValue), &cell_ptr);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    *out_rows = (SlSqliteRow*)row_ptr;
    *out_cells = (SlSqliteValue*)cell_ptr;
    return sl_status_ok();
}

SlSqliteOpenOptions sl_sqlite_open_options_memory(void)
{
    SlSqliteOpenOptions options = {0};

    options.path = sl_str_from_cstr(":memory:");
    options.access = SL_SQLITE_ACCESS_READWRITE;
    return options;
}

SlStatus sl_sqlite_copy_result_text_to_arena(SlArena* arena, SlStr text, SlStr* out)
{
    SlOwnedStr copied = {0};
    SlStatus status;

    if (arena == NULL || out == NULL || !sl_sqlite_str_valid(text)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_str_copy_to_arena(arena, text, &copied);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    *out = sl_owned_str_as_view(copied);
    return sl_status_ok();
}

SlStatus sl_sqlite_copy_result_blob_to_arena(SlArena* arena, SlBytes blob, SlBytes* out)
{
    SlOwnedBytes copied = {0};
    SlStatus status;

    if (arena == NULL || out == NULL || (blob.length != 0U && blob.ptr == NULL)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_bytes_copy_to_arena(arena, blob, &copied);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    *out = sl_owned_bytes_as_view(copied);
    return sl_status_ok();
}

SlStatus sl_sqlite_param_copy_text_to_arena(SlArena* arena, SlStr text, SlSqliteParam* out_param)
{
    SlStr copied = {0};
    SlStatus status;

    if (out_param == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_sqlite_copy_result_text_to_arena(arena, text, &copied);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    *out_param = (SlSqliteParam){.kind = SL_SQLITE_PARAM_TEXT, .value.text = copied};
    return sl_status_ok();
}

SlStatus sl_sqlite_param_copy_blob_to_arena(SlArena* arena, SlBytes blob, SlSqliteParam* out_param)
{
    SlBytes copied = {0};
    SlStatus status;

    if (out_param == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_sqlite_copy_result_blob_to_arena(arena, blob, &copied);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    *out_param = (SlSqliteParam){.kind = SL_SQLITE_PARAM_BLOB, .value.blob = copied};
    return sl_status_ok();
}

SlStatus sl_sqlite_open(SlArena* diag_arena, const SlSqliteOpenOptions* options,
                        SlSqliteConnection* out_connection, SlDiag* out_diag)
{
    sqlite3* db = NULL;
    char* path = NULL;
    int flags = 0;
    int rc = SQLITE_OK;
    SlStatus status = sl_status_ok();

    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }

    if (out_connection == NULL || options == NULL || !sl_sqlite_str_valid(options->path) ||
        sl_str_is_empty(options->path))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_connection = (SlSqliteConnection){0};

    if (options->path.length > (size_t)INT_MAX) {
        return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
    }

    switch (options->access) {
    case SL_SQLITE_ACCESS_READ:
        flags = SQLITE_OPEN_READONLY;
        break;
    case SL_SQLITE_ACCESS_READWRITE:
        flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
        break;
    default:
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    path = sqlite3_mprintf("%.*s", (int)options->path.length, options->path.ptr);
    if (path == NULL) {
        return sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
    }

    rc = sqlite3_open_v2(path, &db, flags, NULL);
    if (rc != SQLITE_OK) {
        status =
            sl_sqlite_diag(diag_arena, out_diag, SL_DIAG_SQLITE_PROVIDER_ERROR,
                           sl_sqlite_literal("sqlite provider open failed",
                                             sizeof("sqlite provider open failed") - 1U),
                           sl_sqlite_literal("operation: open", sizeof("operation: open") - 1U),
                           db != NULL ? sqlite3_errmsg(db) : "sqlite3_open_v2 failed",
                           sl_str_empty(), sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
        if (db != NULL) {
            sqlite3_close(db);
        }
        sqlite3_free(path);
        return status;
    }

    if (options->access == SL_SQLITE_ACCESS_READWRITE && sqlite3_db_readonly(db, "main") != 0) {
        status = sl_sqlite_diag(
            diag_arena, out_diag, SL_DIAG_SQLITE_PROVIDER_ERROR,
            sl_sqlite_literal("sqlite provider readwrite open produced read-only handle",
                              sizeof("sqlite provider readwrite open produced read-only handle") -
                                  1U),
            sl_sqlite_literal("operation: open", sizeof("operation: open") - 1U),
            sqlite3_errmsg(db), sl_str_empty(), sl_status_from_code(SL_STATUS_INVALID_STATE));
        sqlite3_close(db);
        sqlite3_free(path);
        return status;
    }

    sqlite3_free(path);
    out_connection->handle = db;
    out_connection->open = true;
    out_connection->transaction_active = false;
    return sl_status_ok();
}

SlStatus sl_sqlite_close(SlSqliteConnection* connection)
{
    sqlite3* db = NULL;

    if (connection == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (!connection->open) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    db = (sqlite3*)connection->handle;
    if (connection->transaction_active) {
        (void)sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        connection->transaction_active = false;
    }

    if (sqlite3_close(db) != SQLITE_OK) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    *connection = (SlSqliteConnection){0};
    return sl_status_ok();
}

SlStatus sl_sqlite_exec(SlArena* arena, SlSqliteConnection* connection, SlStr sql,
                        const SlSqliteParam* params, size_t param_count,
                        SlSqliteExecResult* out_result, SlDiag* out_diag)
{
    sqlite3_stmt* stmt = NULL;
    sqlite3* db = sl_sqlite_db(connection);
    int rc = SQLITE_OK;
    SlStatus status;
    SlStr operation = sl_sqlite_literal("operation: exec", sizeof("operation: exec") - 1U);

    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (out_result == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_result = (SlSqliteExecResult){0};

    status = sl_sqlite_prepare(arena, connection, sql, operation, &stmt, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_sqlite_bind_params(stmt, params, param_count, arena, out_diag, operation, sql);
    if (!sl_status_is_ok(status)) {
        sqlite3_finalize(stmt);
        return status;
    }

    do {
        rc = sqlite3_step(stmt);
    } while (rc == SQLITE_ROW);

    if (rc != SQLITE_DONE) {
        status = sl_sqlite_diag(arena, out_diag, SL_DIAG_SQLITE_PROVIDER_ERROR,
                                sl_sqlite_literal("sqlite provider exec failed",
                                                  sizeof("sqlite provider exec failed") - 1U),
                                operation, sqlite3_errmsg(db), sql,
                                sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
        sqlite3_finalize(stmt);
        return status;
    }

    out_result->changes = sqlite3_changes(db);
    rc = sqlite3_finalize(stmt);
    if (rc != SQLITE_OK) {
        return sl_sqlite_diag(arena, out_diag, SL_DIAG_SQLITE_PROVIDER_ERROR,
                              sl_sqlite_literal("sqlite provider finalize failed",
                                                sizeof("sqlite provider finalize failed") - 1U),
                              operation, sqlite3_errmsg(db), sql,
                              sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
    }

    sl_sqlite_sync_transaction_state(connection);
    return sl_status_ok();
}

SlStatus sl_sqlite_query(SlArena* arena, SlSqliteConnection* connection, SlStr sql,
                         const SlSqliteParam* params, size_t param_count,
                         const SlSqliteQueryOptions* options, SlSqliteResult* out_result,
                         SlDiag* out_diag)
{
    sqlite3_stmt* stmt = NULL;
    sqlite3* db = sl_sqlite_db(connection);
    SlArenaMark mark = {0};
    SlSqliteRow* rows = NULL;
    SlSqliteValue* cells = NULL;
    size_t max_rows = SL_SQLITE_DEFAULT_MAX_ROWS;
    size_t row_count = 0U;
    size_t column_count = 0U;
    int rc = SQLITE_OK;
    SlStatus status;
    SlStr operation = sl_sqlite_literal("operation: query", sizeof("operation: query") - 1U);

    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (arena == NULL || out_result == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_result = (SlSqliteResult){0};
    mark = sl_arena_mark(arena);

    if (options != NULL && options->max_rows > 0U) {
        max_rows = options->max_rows;
    }

    status = sl_sqlite_prepare(arena, connection, sql, operation, &stmt, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_sqlite_bind_params(stmt, params, param_count, arena, out_diag, operation, sql);
    if (!sl_status_is_ok(status)) {
        sqlite3_finalize(stmt);
        return status;
    }

    column_count = (size_t)sqlite3_column_count(stmt);
    status = sl_sqlite_copy_columns(arena, stmt, column_count, &out_result->column_names);
    if (!sl_status_is_ok(status)) {
        sqlite3_finalize(stmt);
        (void)sl_arena_reset_to(arena, mark);
        return status;
    }

    status = sl_sqlite_allocate_rows(arena, max_rows, column_count, &rows, &cells);
    if (!sl_status_is_ok(status)) {
        sqlite3_finalize(stmt);
        (void)sl_arena_reset_to(arena, mark);
        return status;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        size_t column = 0U;

        if (row_count >= max_rows) {
            sqlite3_finalize(stmt);
            (void)sl_arena_reset_to(arena, mark);
            return sl_sqlite_diag(
                arena, out_diag, SL_DIAG_SQLITE_PROVIDER_ERROR,
                sl_sqlite_literal("sqlite provider query exceeded max rows",
                                  sizeof("sqlite provider query exceeded max rows") - 1U),
                operation, NULL, sql, sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED));
        }

        rows[row_count].values = column_count == 0U ? NULL : &cells[row_count * column_count];
        for (column = 0U; column < column_count; column += 1U) {
            status = sl_sqlite_copy_value(arena, stmt, column, &rows[row_count].values[column]);
            if (!sl_status_is_ok(status)) {
                sqlite3_finalize(stmt);
                (void)sl_arena_reset_to(arena, mark);
                return status;
            }
        }

        row_count += 1U;
    }

    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        (void)sl_arena_reset_to(arena, mark);
        return sl_sqlite_diag(arena, out_diag, SL_DIAG_SQLITE_PROVIDER_ERROR,
                              sl_sqlite_literal("sqlite provider query failed",
                                                sizeof("sqlite provider query failed") - 1U),
                              operation, sqlite3_errmsg(db), sql,
                              sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
    }

    rc = sqlite3_finalize(stmt);
    if (rc != SQLITE_OK) {
        (void)sl_arena_reset_to(arena, mark);
        return sl_sqlite_diag(arena, out_diag, SL_DIAG_SQLITE_PROVIDER_ERROR,
                              sl_sqlite_literal("sqlite provider finalize failed",
                                                sizeof("sqlite provider finalize failed") - 1U),
                              operation, sqlite3_errmsg(db), sql,
                              sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
    }

    out_result->column_count = column_count;
    out_result->row_count = row_count;
    out_result->rows = rows;
    sl_sqlite_sync_transaction_state(connection);
    return sl_status_ok();
}

SlStatus sl_sqlite_query_one(SlArena* arena, SlSqliteConnection* connection, SlStr sql,
                             const SlSqliteParam* params, size_t param_count,
                             SlSqliteQueryOneResult* out_result, SlDiag* out_diag)
{
    sqlite3_stmt* stmt = NULL;
    sqlite3* db = sl_sqlite_db(connection);
    SlArenaMark mark = {0};
    size_t column_count = 0U;
    void* value_ptr = NULL;
    size_t value_alloc_size = 0U;
    size_t column = 0U;
    int rc = SQLITE_OK;
    SlStatus status;
    SlStr operation = sl_sqlite_literal("operation: queryOne", sizeof("operation: queryOne") - 1U);

    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (out_result == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_result = (SlSqliteQueryOneResult){0};
    mark = sl_arena_mark(arena);

    status = sl_sqlite_prepare(arena, connection, sql, operation, &stmt, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_sqlite_bind_params(stmt, params, param_count, arena, out_diag, operation, sql);
    if (!sl_status_is_ok(status)) {
        sqlite3_finalize(stmt);
        return status;
    }

    column_count = (size_t)sqlite3_column_count(stmt);
    status = sl_sqlite_copy_columns(arena, stmt, column_count, &out_result->column_names);
    if (!sl_status_is_ok(status)) {
        sqlite3_finalize(stmt);
        (void)sl_arena_reset_to(arena, mark);
        return status;
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        rc = sqlite3_finalize(stmt);
        if (rc != SQLITE_OK) {
            (void)sl_arena_reset_to(arena, mark);
            return sl_sqlite_diag(arena, out_diag, SL_DIAG_SQLITE_PROVIDER_ERROR,
                                  sl_sqlite_literal("sqlite provider finalize failed",
                                                    sizeof("sqlite provider finalize failed") - 1U),
                                  operation, sqlite3_errmsg(db), sql,
                                  sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
        }

        out_result->column_count = column_count;
        out_result->found = false;
        out_result->values = NULL;
        sl_sqlite_sync_transaction_state(connection);
        return sl_status_ok();
    }

    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        (void)sl_arena_reset_to(arena, mark);
        return sl_sqlite_diag(arena, out_diag, SL_DIAG_SQLITE_PROVIDER_ERROR,
                              sl_sqlite_literal("sqlite provider queryOne failed",
                                                sizeof("sqlite provider queryOne failed") - 1U),
                              operation, sqlite3_errmsg(db), sql,
                              sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
    }

    if (column_count > 0U) {
        status = sl_checked_mul_size(column_count, sizeof(SlSqliteValue), &value_alloc_size);
        if (!sl_status_is_ok(status)) {
            sqlite3_finalize(stmt);
            (void)sl_arena_reset_to(arena, mark);
            return status;
        }

        status = sl_arena_alloc(arena, value_alloc_size, _Alignof(SlSqliteValue), &value_ptr);
        if (!sl_status_is_ok(status)) {
            sqlite3_finalize(stmt);
            (void)sl_arena_reset_to(arena, mark);
            return status;
        }
    }

    out_result->values = (SlSqliteValue*)value_ptr;
    for (column = 0U; column < column_count; column += 1U) {
        status = sl_sqlite_copy_value(arena, stmt, column, &out_result->values[column]);
        if (!sl_status_is_ok(status)) {
            sqlite3_finalize(stmt);
            (void)sl_arena_reset_to(arena, mark);
            return status;
        }
    }

    rc = sqlite3_finalize(stmt);
    if (rc != SQLITE_OK) {
        (void)sl_arena_reset_to(arena, mark);
        return sl_sqlite_diag(arena, out_diag, SL_DIAG_SQLITE_PROVIDER_ERROR,
                              sl_sqlite_literal("sqlite provider finalize failed",
                                                sizeof("sqlite provider finalize failed") - 1U),
                              operation, sqlite3_errmsg(db), sql,
                              sl_status_from_code(SL_STATUS_INVALID_ARGUMENT));
    }

    out_result->column_count = column_count;
    out_result->found = true;
    sl_sqlite_sync_transaction_state(connection);
    return sl_status_ok();
}

SlStatus sl_sqlite_transaction_begin(SlArena* arena, SlSqliteConnection* connection,
                                     SlSqliteTransaction* out_tx, SlDiag* out_diag)
{
    SlSqliteExecResult exec_result = {0};
    SlStatus status;

    if (out_tx == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_tx = (SlSqliteTransaction){0};
    if (sl_sqlite_db(connection) == NULL) {
        return sl_sqlite_invalid_state_diag(
            arena, out_diag,
            sl_sqlite_literal("operation: transaction.begin",
                              sizeof("operation: transaction.begin") - 1U));
    }

    if (connection->transaction_active) {
        return sl_sqlite_diag(
            arena, out_diag, SL_DIAG_SQLITE_PROVIDER_ERROR,
            sl_sqlite_literal("sqlite nested transactions are not supported",
                              sizeof("sqlite nested transactions are not supported") - 1U),
            sl_sqlite_literal("operation: transaction.begin",
                              sizeof("operation: transaction.begin") - 1U),
            NULL, sl_str_empty(), sl_status_from_code(SL_STATUS_INVALID_STATE));
    }

    status = sl_sqlite_exec(arena, connection, sl_str_from_cstr("BEGIN"), NULL, 0U, &exec_result,
                            out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    connection->transaction_active = true;
    out_tx->connection = connection;
    out_tx->active = true;
    return sl_status_ok();
}

SlStatus sl_sqlite_transaction_commit(SlArena* arena, SlSqliteTransaction* tx, SlDiag* out_diag)
{
    SlSqliteExecResult exec_result = {0};
    SlStatus status;

    if (tx == NULL || tx->connection == NULL || !tx->active) {
        return sl_sqlite_invalid_state_diag(
            arena, out_diag,
            sl_sqlite_literal("operation: transaction.commit",
                              sizeof("operation: transaction.commit") - 1U));
    }

    status = sl_sqlite_exec(arena, tx->connection, sl_str_from_cstr("COMMIT"), NULL, 0U,
                            &exec_result, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    tx->connection->transaction_active = false;
    tx->active = false;
    tx->connection = NULL;
    return sl_status_ok();
}

SlStatus sl_sqlite_transaction_rollback(SlArena* arena, SlSqliteTransaction* tx, SlDiag* out_diag)
{
    SlSqliteExecResult exec_result = {0};
    SlStatus status;

    if (tx == NULL || tx->connection == NULL || !tx->active) {
        return sl_sqlite_invalid_state_diag(
            arena, out_diag,
            sl_sqlite_literal("operation: transaction.rollback",
                              sizeof("operation: transaction.rollback") - 1U));
    }

    status = sl_sqlite_exec(arena, tx->connection, sl_str_from_cstr("ROLLBACK"), NULL, 0U,
                            &exec_result, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    tx->connection->transaction_active = false;
    tx->active = false;
    tx->connection = NULL;
    return sl_status_ok();
}

SlStatus sl_sqlite_transaction_exec(SlArena* arena, SlSqliteTransaction* tx, SlStr sql,
                                    const SlSqliteParam* params, size_t param_count,
                                    SlSqliteExecResult* out_result, SlDiag* out_diag)
{
    SlStatus status;

    if (tx == NULL || tx->connection == NULL || !tx->active) {
        return sl_sqlite_invalid_state_diag(
            arena, out_diag,
            sl_sqlite_literal("operation: transaction.exec",
                              sizeof("operation: transaction.exec") - 1U));
    }

    status = sl_sqlite_exec(arena, tx->connection, sql, params, param_count, out_result, out_diag);
    if (sl_status_is_ok(status) && !tx->connection->transaction_active) {
        tx->active = false;
        tx->connection = NULL;
    }

    return status;
}

SlStatus sl_sqlite_transaction_query(SlArena* arena, SlSqliteTransaction* tx, SlStr sql,
                                     const SlSqliteParam* params, size_t param_count,
                                     const SlSqliteQueryOptions* options,
                                     SlSqliteResult* out_result, SlDiag* out_diag)
{
    SlStatus status;

    if (tx == NULL || tx->connection == NULL || !tx->active) {
        return sl_sqlite_invalid_state_diag(
            arena, out_diag,
            sl_sqlite_literal("operation: transaction.query",
                              sizeof("operation: transaction.query") - 1U));
    }

    status = sl_sqlite_query(arena, tx->connection, sql, params, param_count, options, out_result,
                             out_diag);
    if (sl_status_is_ok(status) && !tx->connection->transaction_active) {
        tx->active = false;
        tx->connection = NULL;
    }

    return status;
}

SlStatus sl_sqlite_transaction_query_one(SlArena* arena, SlSqliteTransaction* tx, SlStr sql,
                                         const SlSqliteParam* params, size_t param_count,
                                         SlSqliteQueryOneResult* out_result, SlDiag* out_diag)
{
    SlStatus status;

    if (tx == NULL || tx->connection == NULL || !tx->active) {
        return sl_sqlite_invalid_state_diag(
            arena, out_diag,
            sl_sqlite_literal("operation: transaction.queryOne",
                              sizeof("operation: transaction.queryOne") - 1U));
    }

    status =
        sl_sqlite_query_one(arena, tx->connection, sql, params, param_count, out_result, out_diag);
    if (sl_status_is_ok(status) && !tx->connection->transaction_active) {
        tx->active = false;
        tx->connection = NULL;
    }

    return status;
}

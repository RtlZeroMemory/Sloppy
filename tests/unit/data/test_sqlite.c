#include "sloppy/data_sqlite.h"

#include <stdbool.h>
#include <stddef.h>

#define TEST_ARENA_SIZE 131072U

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static int expect_str_equal(SlStr actual, const char* expected)
{
    return expect_true(sl_str_equal(actual, sl_str_from_cstr(expected)));
}

static int expect_bytes_equal(SlBytes actual, const unsigned char* expected, size_t length)
{
    size_t index = 0U;

    if (actual.length != length || (length != 0U && (actual.ptr == NULL || expected == NULL))) {
        return 1;
    }

    for (index = 0U; index < length; index += 1U) {
        if (actual.ptr[index] != expected[index]) {
            return 1;
        }
    }

    return 0;
}

static bool diag_has_hint(const SlDiag* diag, const char* hint)
{
    size_t index = 0U;
    SlStr expected = sl_str_from_cstr(hint);

    if (diag == NULL) {
        return false;
    }

    for (index = 0U; index < diag->hint_count; index += 1U) {
        if (sl_str_equal(diag->hints[index], expected)) {
            return true;
        }
    }

    return false;
}

static bool str_contains_text(SlStr haystack, SlStr needle)
{
    size_t index = 0U;
    size_t inner = 0U;

    if (needle.length == 0U) {
        return true;
    }
    if (haystack.length < needle.length || haystack.ptr == NULL || needle.ptr == NULL) {
        return false;
    }

    for (index = 0U; index <= haystack.length - needle.length; index += 1U) {
        for (inner = 0U; inner < needle.length; inner += 1U) {
            if (haystack.ptr[index + inner] != needle.ptr[inner]) {
                break;
            }
        }
        if (inner == needle.length) {
            return true;
        }
    }

    return false;
}

static bool diag_contains_text(const SlDiag* diag, const char* text)
{
    size_t hint_index = 0U;
    SlStr expected = sl_str_from_cstr(text);

    if (diag == NULL || text == NULL) {
        return false;
    }

    if (str_contains_text(diag->message, expected)) {
        return true;
    }

    for (hint_index = 0U; hint_index < diag->hint_count; hint_index += 1U) {
        if (str_contains_text(diag->hints[hint_index], expected)) {
            return true;
        }
    }

    return false;
}

static SlSqliteParam text_param(const char* value)
{
    SlSqliteParam param = {0};

    param.kind = SL_SQLITE_PARAM_TEXT;
    param.value.text = sl_str_from_cstr(value);
    return param;
}

static SlSqliteParam blob_param(const unsigned char* value, size_t length)
{
    SlSqliteParam param = {0};

    param.kind = SL_SQLITE_PARAM_BLOB;
    param.value.blob = sl_bytes_from_parts(value, length);
    return param;
}

static SlSqliteParam int_param(int value)
{
    SlSqliteParam param = {0};

    param.kind = SL_SQLITE_PARAM_INTEGER;
    param.value.integer = value;
    return param;
}

static SlSqliteParam float_param(double value)
{
    SlSqliteParam param = {0};

    param.kind = SL_SQLITE_PARAM_FLOAT;
    param.value.number = value;
    return param;
}

static SlSqliteParam bool_param(bool value)
{
    SlSqliteParam param = {0};

    param.kind = SL_SQLITE_PARAM_BOOL;
    param.value.boolean = value;
    return param;
}

static SlSqliteParam null_param(void)
{
    SlSqliteParam param = {0};

    param.kind = SL_SQLITE_PARAM_NULL;
    return param;
}

static int open_memory(SlArena* arena, SlSqliteConnection* connection)
{
    SlDiag diag = {0};
    SlSqliteOpenOptions options = sl_sqlite_open_options_memory();
    SlStatus status = sl_sqlite_open(arena, &options, connection, &diag);

    if (expect_status(status, SL_STATUS_OK) != 0 || !connection->open) {
        return 1;
    }

    return 0;
}

static int exec_sql(SlArena* arena, SlSqliteConnection* connection, const char* sql)
{
    SlSqliteExecResult result = {0};
    SlStatus status =
        sl_sqlite_exec(arena, connection, sl_str_from_cstr(sql), NULL, 0U, &result, NULL);

    return expect_status(status, SL_STATUS_OK);
}

static int close_and_return(SlSqliteConnection* connection, int result)
{
    if (connection != NULL && connection->open) {
        (void)sl_sqlite_close(connection);
    }

    return result;
}

static int test_open_close_and_use_after_close(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlSqliteConnection connection = {0};
    SlSqliteExecResult result = {0};
    SlDiag diag = {0};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 1;
    }

    if (open_memory(&arena, &connection) != 0) {
        return 2;
    }

    status = sl_sqlite_close(&connection);
    if (expect_status(status, SL_STATUS_OK) != 0 || connection.open) {
        return close_and_return(&connection, 3);
    }

    status =
        sl_sqlite_exec(&arena, &connection, sl_str_from_cstr("select 1"), NULL, 0U, &result, &diag);
    if (expect_status(status, SL_STATUS_INVALID_STATE) != 0 ||
        diag.code != SL_DIAG_SQLITE_PROVIDER_ERROR || !diag_has_hint(&diag, "provider: sqlite") ||
        !diag_has_hint(&diag, "operation: exec"))
    {
        return close_and_return(&connection, 4);
    }

    return 0;
}

static int test_exec_insert_and_query_rows(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlSqliteConnection connection = {0};
    SlSqliteParam insert_params[] = {text_param("Ada")};
    SlSqliteExecResult exec_result = {0};
    SlSqliteResult query_result = {0};
    SlSqliteQueryOneResult one = {0};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 10;
    }
    if (open_memory(&arena, &connection) != 0) {
        return 11;
    }
    if (exec_sql(&arena, &connection,
                 "create table users (id integer primary key, name text not null)") != 0)
    {
        return close_and_return(&connection, 12);
    }

    status =
        sl_sqlite_exec(&arena, &connection, sl_str_from_cstr("insert into users (name) values (?)"),
                       insert_params, 1U, &exec_result, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || exec_result.changes != 1) {
        return close_and_return(&connection, 13);
    }

    status = sl_sqlite_query(&arena, &connection,
                             sl_str_from_cstr("select id, name from users where name = ?"),
                             insert_params, 1U, NULL, &query_result, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || query_result.column_count != 2U ||
        query_result.row_count != 1U)
    {
        return close_and_return(&connection, 14);
    }
    if (expect_str_equal(query_result.column_names[0], "id") != 0 ||
        expect_str_equal(query_result.column_names[1], "name") != 0 ||
        query_result.rows[0].values[0].kind != SL_SQLITE_VALUE_INTEGER ||
        query_result.rows[0].values[0].value.integer != 1 ||
        query_result.rows[0].values[1].kind != SL_SQLITE_VALUE_TEXT ||
        expect_str_equal(query_result.rows[0].values[1].value.text, "Ada") != 0)
    {
        return close_and_return(&connection, 15);
    }

    status = sl_sqlite_query_one(&arena, &connection,
                                 sl_str_from_cstr("select name from users order by id"), NULL, 0U,
                                 &one, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || !one.found || one.column_count != 1U ||
        one.values[0].kind != SL_SQLITE_VALUE_TEXT ||
        expect_str_equal(one.values[0].value.text, "Ada") != 0)
    {
        return close_and_return(&connection, 16);
    }

    status = sl_sqlite_query_one(&arena, &connection,
                                 sl_str_from_cstr("select name from users where name = 'Grace'"),
                                 NULL, 0U, &one, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || one.found) {
        return close_and_return(&connection, 17);
    }

    return expect_status(sl_sqlite_close(&connection), SL_STATUS_OK) == 0 ? 0 : 18;
}

static int test_parameter_binding_and_types(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlSqliteConnection connection = {0};
    unsigned char blob_bytes[] = {0x00U, 0x41U, 0xffU, 0x20U};
    unsigned char expected_blob[] = {0x00U, 0x41U, 0xffU, 0x20U};
    SlSqliteParam params[] = {null_param(),        text_param("Ada'); drop table values_test; --"),
                              int_param(42),       float_param(3.5),
                              bool_param(true),    blob_param(blob_bytes, sizeof(blob_bytes)),
                              blob_param(NULL, 0U)};
    SlSqliteExecResult exec_result = {0};
    SlSqliteQueryOneResult row = {0};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 20;
    }
    if (open_memory(&arena, &connection) != 0) {
        return 21;
    }
    if (exec_sql(&arena, &connection,
                 "create table values_test (n, s text, i integer, f real, b integer, raw blob, "
                 "empty_raw blob)") != 0)
    {
        return close_and_return(&connection, 22);
    }

    status = sl_sqlite_exec(
        &arena, &connection,
        sl_str_from_cstr(
            "insert into values_test (n, s, i, f, b, raw, empty_raw) values (?, ?, ?, ?, ?, ?, ?)"),
        params, 7U, &exec_result, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || exec_result.changes != 1) {
        return close_and_return(&connection, 23);
    }

    blob_bytes[1] = 0x5aU;

    status = sl_sqlite_query_one(
        &arena, &connection,
        sl_str_from_cstr("select n, s, i, f, b, raw, empty_raw from values_test"), NULL, 0U, &row,
        NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || !row.found || row.column_count != 7U) {
        return close_and_return(&connection, 24);
    }
    if (row.values[0].kind != SL_SQLITE_VALUE_NULL || row.values[1].kind != SL_SQLITE_VALUE_TEXT ||
        expect_str_equal(row.values[1].value.text, "Ada'); drop table values_test; --") != 0 ||
        row.values[2].kind != SL_SQLITE_VALUE_INTEGER || row.values[2].value.integer != 42 ||
        row.values[3].kind != SL_SQLITE_VALUE_FLOAT || row.values[3].value.number != 3.5 ||
        row.values[4].kind != SL_SQLITE_VALUE_INTEGER || row.values[4].value.integer != 1 ||
        row.values[5].kind != SL_SQLITE_VALUE_BLOB ||
        expect_bytes_equal(row.values[5].value.blob, expected_blob, sizeof(expected_blob)) != 0 ||
        row.values[6].kind != SL_SQLITE_VALUE_BLOB || row.values[6].value.blob.length != 0U)
    {
        return close_and_return(&connection, 25);
    }

    status = sl_sqlite_query_one(&arena, &connection,
                                 sl_str_from_cstr("select count(*) from values_test"), NULL, 0U,
                                 &row, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || !row.found || row.values[0].value.integer != 1)
    {
        return close_and_return(&connection, 26);
    }

    return expect_status(sl_sqlite_close(&connection), SL_STATUS_OK) == 0 ? 0 : 27;
}

static int test_result_and_parameter_lifetimes(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlSqliteConnection connection = {0};
    char name_bytes[] = {'A', 'd', 'a'};
    unsigned char blob_bytes[] = {0x10U, 0x00U, 0xffU};
    const unsigned char expected_blob[] = {0x10U, 0x00U, 0xffU};
    SlSqliteParam params[] = {
        {.kind = SL_SQLITE_PARAM_TEXT,
         .value.text = {.ptr = name_bytes, .length = sizeof(name_bytes)}},
        blob_param(blob_bytes, sizeof(blob_bytes)),
    };
    SlSqliteExecResult exec_result = {0};
    SlSqliteResult rows = {0};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 120;
    }
    if (open_memory(&arena, &connection) != 0) {
        return 121;
    }
    if (exec_sql(&arena, &connection, "create table lifetime_test (name text, raw blob)") != 0) {
        return close_and_return(&connection, 122);
    }

    status = sl_sqlite_exec(&arena, &connection,
                            sl_str_from_cstr("insert into lifetime_test (name, raw) values (?, ?)"),
                            params, 2U, &exec_result, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || exec_result.changes != 1) {
        return close_and_return(&connection, 123);
    }

    name_bytes[0] = 'E';
    blob_bytes[0] = 0x77U;

    status = sl_sqlite_query(&arena, &connection,
                             sl_str_from_cstr("select name, raw from lifetime_test"), NULL, 0U,
                             NULL, &rows, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || rows.row_count != 1U || rows.column_count != 2U)
    {
        return close_and_return(&connection, 124);
    }

    status = sl_sqlite_close(&connection);
    if (expect_status(status, SL_STATUS_OK) != 0) {
        return 125;
    }

    if (rows.rows[0].values[0].kind != SL_SQLITE_VALUE_TEXT ||
        expect_str_equal(rows.rows[0].values[0].value.text, "Ada") != 0 ||
        rows.rows[0].values[1].kind != SL_SQLITE_VALUE_BLOB ||
        expect_bytes_equal(rows.rows[0].values[1].value.blob, expected_blob,
                           sizeof(expected_blob)) != 0)
    {
        return 126;
    }

    return 0;
}

static int test_empty_result_and_duplicate_column_names(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlSqliteConnection connection = {0};
    SlSqliteResult rows = {0};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 130;
    }
    if (open_memory(&arena, &connection) != 0) {
        return 131;
    }

    status = sl_sqlite_query(&arena, &connection,
                             sl_str_from_cstr("select 1 as value, 2 as value where 0"), NULL, 0U,
                             NULL, &rows, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || rows.row_count != 0U ||
        rows.column_count != 2U || expect_str_equal(rows.column_names[0], "value") != 0 ||
        expect_str_equal(rows.column_names[1], "value") != 0)
    {
        return close_and_return(&connection, 132);
    }

    status = sl_sqlite_query(&arena, &connection, sl_str_from_cstr("select 1 as value, 2 as value"),
                             NULL, 0U, NULL, &rows, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || rows.row_count != 1U ||
        rows.column_count != 2U || expect_str_equal(rows.column_names[0], "value") != 0 ||
        expect_str_equal(rows.column_names[1], "value") != 0 ||
        rows.rows[0].values[0].value.integer != 1 || rows.rows[0].values[1].value.integer != 2)
    {
        return close_and_return(&connection, 133);
    }

    return expect_status(sl_sqlite_close(&connection), SL_STATUS_OK) == 0 ? 0 : 134;
}

static int test_sqlite_text_blob_interop_helpers(void)
{
    unsigned char storage[128];
    SlArena arena = {0};
    SlStr text = sl_str_from_cstr("owned-text");
    SlStr copied_text = sl_str_from_cstr("stale-text");
    SlBytes blob = sl_bytes_from_parts((const unsigned char*)"abc", 3U);
    SlBytes copied_blob = sl_bytes_from_parts((const unsigned char*)"old", 3U);
    SlSqliteParam param = {.kind = SL_SQLITE_PARAM_INTEGER, .value.integer = 99};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 110;
    }

    status = sl_sqlite_copy_result_text_to_arena(&arena, text, &copied_text);
    if (expect_status(status, SL_STATUS_OK) != 0 ||
        !sl_str_equal(copied_text, sl_str_from_cstr("owned-text")) || copied_text.ptr == text.ptr)
    {
        return 111;
    }

    status = sl_sqlite_copy_result_blob_to_arena(&arena, blob, &copied_blob);
    if (expect_status(status, SL_STATUS_OK) != 0 ||
        expect_bytes_equal(copied_blob, blob.ptr, blob.length) != 0 || copied_blob.ptr == blob.ptr)
    {
        return 112;
    }

    status = sl_sqlite_param_copy_text_to_arena(&arena, sl_str_from_parts(NULL, 4U), &param);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
        param.kind != SL_SQLITE_PARAM_INTEGER || param.value.integer != 99)
    {
        return 113;
    }

    status = sl_sqlite_param_copy_blob_to_arena(&arena, blob, &param);
    if (expect_status(status, SL_STATUS_OK) != 0 || param.kind != SL_SQLITE_PARAM_BLOB ||
        expect_bytes_equal(param.value.blob, blob.ptr, blob.length) != 0 ||
        param.value.blob.ptr == blob.ptr)
    {
        return 114;
    }

    status = sl_sqlite_param_copy_text_to_arena(&arena, sl_str_empty(), &param);
    if (expect_status(status, SL_STATUS_OK) != 0 || param.kind != SL_SQLITE_PARAM_TEXT ||
        param.value.text.length != 0U)
    {
        return 115;
    }

    status = sl_sqlite_param_copy_blob_to_arena(&arena, sl_bytes_empty(), &param);
    if (expect_status(status, SL_STATUS_OK) != 0 || param.kind != SL_SQLITE_PARAM_BLOB ||
        param.value.blob.length != 0U)
    {
        return 116;
    }

    return 0;
}

static int test_unsupported_parameter_diagnostic(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlSqliteConnection connection = {0};
    SlSqliteParam param = {.kind = SL_SQLITE_PARAM_UNSUPPORTED};
    SlSqliteExecResult result = {0};
    SlDiag diag = {0};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 30;
    }
    if (open_memory(&arena, &connection) != 0) {
        return 31;
    }

    status = sl_sqlite_exec(&arena, &connection, sl_str_from_cstr("select ?"), &param, 1U, &result,
                            &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_DATABASE_UNSUPPORTED_VALUE ||
        !diag_has_hint(&diag, "provider: sqlite") || !diag_has_hint(&diag, "operation: exec"))
    {
        return close_and_return(&connection, 32);
    }

    return expect_status(sl_sqlite_close(&connection), SL_STATUS_OK) == 0 ? 0 : 33;
}

static int test_transactions_commit_rollback_and_lifetime(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlSqliteConnection connection = {0};
    SlSqliteTransaction tx = {0};
    SlSqliteTransaction nested = {0};
    SlSqliteExecResult exec_result = {0};
    SlSqliteQueryOneResult one = {0};
    SlDiag diag = {0};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 40;
    }
    if (open_memory(&arena, &connection) != 0) {
        return 41;
    }
    if (exec_sql(&arena, &connection, "create table tx_test (name text)") != 0) {
        return close_and_return(&connection, 42);
    }

    status = sl_sqlite_transaction_begin(&arena, &connection, &tx, &diag);
    if (expect_status(status, SL_STATUS_OK) != 0 || !tx.active) {
        return close_and_return(&connection, 43);
    }
    status = sl_sqlite_transaction_begin(&arena, &connection, &nested, &diag);
    if (expect_status(status, SL_STATUS_INVALID_STATE) != 0 ||
        diag.code != SL_DIAG_SQLITE_PROVIDER_ERROR)
    {
        return close_and_return(&connection, 44);
    }
    status = sl_sqlite_transaction_exec(
        &arena, &tx, sl_str_from_cstr("insert into tx_test (name) values (?)"),
        &(SlSqliteParam){.kind = SL_SQLITE_PARAM_TEXT, .value.text = sl_str_from_cstr("commit")},
        1U, &exec_result, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0) {
        return close_and_return(&connection, 45);
    }
    if (expect_status(sl_sqlite_transaction_commit(&arena, &tx, NULL), SL_STATUS_OK) != 0) {
        return close_and_return(&connection, 46);
    }
    status = sl_sqlite_transaction_exec(&arena, &tx, sl_str_from_cstr("select 1"), NULL, 0U,
                                        &exec_result, &diag);
    if (expect_status(status, SL_STATUS_INVALID_STATE) != 0) {
        return close_and_return(&connection, 47);
    }

    status = sl_sqlite_transaction_begin(&arena, &connection, &tx, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0) {
        return close_and_return(&connection, 48);
    }
    status = sl_sqlite_transaction_exec(
        &arena, &tx, sl_str_from_cstr("insert into tx_test (name) values ('rollback')"), NULL, 0U,
        &exec_result, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0) {
        return close_and_return(&connection, 49);
    }
    if (expect_status(sl_sqlite_transaction_rollback(&arena, &tx, NULL), SL_STATUS_OK) != 0) {
        return close_and_return(&connection, 50);
    }

    status =
        sl_sqlite_query_one(&arena, &connection, sl_str_from_cstr("select count(*) from tx_test"),
                            NULL, 0U, &one, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || !one.found ||
        one.values[0].kind != SL_SQLITE_VALUE_INTEGER || one.values[0].value.integer != 1)
    {
        return close_and_return(&connection, 51);
    }

    return expect_status(sl_sqlite_close(&connection), SL_STATUS_OK) == 0 ? 0 : 52;
}

static int test_sql_diagnostics(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlSqliteConnection connection = {0};
    SlSqliteExecResult exec_result = {0};
    SlSqliteResult query_result = {0};
    SlDiag diag = {0};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 60;
    }
    if (open_memory(&arena, &connection) != 0) {
        return 61;
    }

    status = sl_sqlite_exec(&arena, &connection, sl_str_from_cstr("select from"), NULL, 0U,
                            &exec_result, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_SQLITE_PROVIDER_ERROR || !diag_has_hint(&diag, "provider: sqlite") ||
        !diag_has_hint(&diag, "operation: exec"))
    {
        return close_and_return(&connection, 62);
    }

    status = sl_sqlite_query(&arena, &connection, sl_str_from_cstr("select * from missing_table"),
                             NULL, 0U, NULL, &query_result, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_SQLITE_PROVIDER_ERROR || !diag_has_hint(&diag, "provider: sqlite") ||
        !diag_has_hint(&diag, "operation: query"))
    {
        return close_and_return(&connection, 63);
    }

    return expect_status(sl_sqlite_close(&connection), SL_STATUS_OK) == 0 ? 0 : 64;
}

static int test_constraint_and_parameter_redaction_diagnostics(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlSqliteConnection connection = {0};
    SlSqliteExecResult exec_result = {0};
    SlDiag diag = {0};
    SlSqliteParam secret_param = text_param("SECRET_SHOULD_NOT_APPEAR");
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 65;
    }
    if (open_memory(&arena, &connection) != 0) {
        return 66;
    }
    if (exec_sql(&arena, &connection, "create table constraint_test (name text unique)") != 0) {
        return close_and_return(&connection, 67);
    }

    status = sl_sqlite_exec(&arena, &connection,
                            sl_str_from_cstr("insert into constraint_test (name) values (?)"),
                            &secret_param, 1U, &exec_result, &diag);
    if (expect_status(status, SL_STATUS_OK) != 0) {
        return close_and_return(&connection, 68);
    }

    status = sl_sqlite_exec(&arena, &connection,
                            sl_str_from_cstr("insert into constraint_test (name) values (?)"),
                            &secret_param, 1U, &exec_result, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_SQLITE_PROVIDER_ERROR || !diag_has_hint(&diag, "provider: sqlite") ||
        !diag_has_hint(&diag, "operation: exec") ||
        diag_contains_text(&diag, "SECRET_SHOULD_NOT_APPEAR"))
    {
        return close_and_return(&connection, 69);
    }

    return expect_status(sl_sqlite_close(&connection), SL_STATUS_OK) == 0 ? 0 : 70;
}

static int test_trailing_sql_rejected(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlSqliteConnection connection = {0};
    SlSqliteExecResult exec_result = {0};
    SlSqliteResult query_result = {0};
    SlSqliteQueryOneResult one = {0};
    SlDiag diag = {0};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 80;
    }
    if (open_memory(&arena, &connection) != 0) {
        return 81;
    }

    status = sl_sqlite_exec(
        &arena, &connection,
        sl_str_from_cstr("create table tail_test (value integer); create table ignored (value)"),
        NULL, 0U, &exec_result, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_SQLITE_PROVIDER_ERROR || !diag_has_hint(&diag, "operation: exec"))
    {
        return close_and_return(&connection, 82);
    }

    status = sl_sqlite_query(&arena, &connection, sl_str_from_cstr("select 1; invalid trailing"),
                             NULL, 0U, NULL, &query_result, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_SQLITE_PROVIDER_ERROR || !diag_has_hint(&diag, "operation: query"))
    {
        return close_and_return(&connection, 83);
    }

    status = sl_sqlite_query_one(&arena, &connection, sl_str_from_cstr("select 1; select 2"), NULL,
                                 0U, &one, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_SQLITE_PROVIDER_ERROR || !diag_has_hint(&diag, "operation: queryOne"))
    {
        return close_and_return(&connection, 84);
    }

    status = sl_sqlite_query_one(&arena, &connection, sl_str_from_cstr("select 1 \n\t"), NULL, 0U,
                                 &one, &diag);
    if (expect_status(status, SL_STATUS_OK) != 0 || !one.found) {
        return close_and_return(&connection, 85);
    }

    return expect_status(sl_sqlite_close(&connection), SL_STATUS_OK) == 0 ? 0 : 86;
}

static int test_parameter_arity_mismatch_rejected(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlSqliteConnection connection = {0};
    SlSqliteTransaction tx = {0};
    SlSqliteParam param = int_param(7);
    SlSqliteExecResult exec_result = {0};
    SlSqliteResult query_result = {0};
    SlSqliteQueryOneResult one = {0};
    SlDiag diag = {0};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 90;
    }
    if (open_memory(&arena, &connection) != 0) {
        return 91;
    }

    status = sl_sqlite_query_one(&arena, &connection, sl_str_from_cstr("select ?"), NULL, 0U, &one,
                                 &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_SQLITE_PROVIDER_ERROR || !diag_has_hint(&diag, "operation: queryOne"))
    {
        return close_and_return(&connection, 92);
    }

    status = sl_sqlite_exec(&arena, &connection, sl_str_from_cstr("select 1"), &param, 1U,
                            &exec_result, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_SQLITE_PROVIDER_ERROR || !diag_has_hint(&diag, "operation: exec"))
    {
        return close_and_return(&connection, 93);
    }

    status = sl_sqlite_transaction_begin(&arena, &connection, &tx, &diag);
    if (expect_status(status, SL_STATUS_OK) != 0) {
        return close_and_return(&connection, 94);
    }

    status = sl_sqlite_transaction_exec(&arena, &tx, sl_str_from_cstr("select ?"), NULL, 0U,
                                        &exec_result, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_SQLITE_PROVIDER_ERROR || !diag_has_hint(&diag, "operation: exec"))
    {
        return close_and_return(&connection, 95);
    }

    status = sl_sqlite_transaction_query(&arena, &tx, sl_str_from_cstr("select 1"), &param, 1U,
                                         NULL, &query_result, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_SQLITE_PROVIDER_ERROR || !diag_has_hint(&diag, "operation: query"))
    {
        return close_and_return(&connection, 96);
    }

    status = sl_sqlite_transaction_query_one(&arena, &tx, sl_str_from_cstr("select ?"), NULL, 0U,
                                             &one, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_SQLITE_PROVIDER_ERROR || !diag_has_hint(&diag, "operation: queryOne"))
    {
        return close_and_return(&connection, 97);
    }

    if (expect_status(sl_sqlite_transaction_rollback(&arena, &tx, NULL), SL_STATUS_OK) != 0) {
        return close_and_return(&connection, 98);
    }

    return expect_status(sl_sqlite_close(&connection), SL_STATUS_OK) == 0 ? 0 : 99;
}

static int test_generic_sql_resynchronizes_transaction_state(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlSqliteConnection connection = {0};
    SlSqliteTransaction tx = {0};
    SlSqliteExecResult exec_result = {0};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 100;
    }
    if (open_memory(&arena, &connection) != 0) {
        return 101;
    }

    status = sl_sqlite_exec(&arena, &connection, sl_str_from_cstr("BEGIN"), NULL, 0U, &exec_result,
                            NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || !connection.transaction_active) {
        return close_and_return(&connection, 102);
    }

    status = sl_sqlite_exec(&arena, &connection, sl_str_from_cstr("ROLLBACK"), NULL, 0U,
                            &exec_result, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || connection.transaction_active) {
        return close_and_return(&connection, 103);
    }

    status = sl_sqlite_transaction_begin(&arena, &connection, &tx, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || !tx.active || !connection.transaction_active) {
        return close_and_return(&connection, 104);
    }

    status = sl_sqlite_transaction_exec(&arena, &tx, sl_str_from_cstr("COMMIT"), NULL, 0U,
                                        &exec_result, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || tx.active || tx.connection != NULL ||
        connection.transaction_active)
    {
        return close_and_return(&connection, 105);
    }

    return expect_status(sl_sqlite_close(&connection), SL_STATUS_OK) == 0 ? 0 : 106;
}

static int test_invalid_open_options(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlSqliteConnection connection = {0};
    SlSqliteOpenOptions options = {.path = sl_str_from_cstr("missing-parent-dir/missing.db"),
                                   .access = SL_SQLITE_ACCESS_READ};
    SlDiag diag = {0};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 70;
    }

    status = sl_sqlite_open(&arena, &options, &connection, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_SQLITE_PROVIDER_ERROR || !diag_has_hint(&diag, "provider: sqlite") ||
        !diag_has_hint(&diag, "operation: open"))
    {
        return 71;
    }

    return 0;
}

int main(void)
{
    int result = test_open_close_and_use_after_close();
    if (result != 0) {
        return result;
    }

    result = test_exec_insert_and_query_rows();
    if (result != 0) {
        return result;
    }

    result = test_parameter_binding_and_types();
    if (result != 0) {
        return result;
    }

    result = test_result_and_parameter_lifetimes();
    if (result != 0) {
        return result;
    }

    result = test_empty_result_and_duplicate_column_names();
    if (result != 0) {
        return result;
    }

    result = test_unsupported_parameter_diagnostic();
    if (result != 0) {
        return result;
    }

    result = test_transactions_commit_rollback_and_lifetime();
    if (result != 0) {
        return result;
    }

    result = test_sql_diagnostics();
    if (result != 0) {
        return result;
    }

    result = test_constraint_and_parameter_redaction_diagnostics();
    if (result != 0) {
        return result;
    }

    result = test_trailing_sql_rejected();
    if (result != 0) {
        return result;
    }

    result = test_parameter_arity_mismatch_rejected();
    if (result != 0) {
        return result;
    }

    result = test_generic_sql_resynchronizes_transaction_state();
    if (result != 0) {
        return result;
    }

    result = test_sqlite_text_blob_interop_helpers();
    if (result != 0) {
        return result;
    }

    return test_invalid_open_options();
}

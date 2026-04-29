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

static SlSqliteParam text_param(const char* value)
{
    SlSqliteParam param = {0};

    param.kind = SL_SQLITE_PARAM_TEXT;
    param.value.text = sl_str_from_cstr(value);
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
        return 3;
    }

    status =
        sl_sqlite_exec(&arena, &connection, sl_str_from_cstr("select 1"), NULL, 0U, &result, &diag);
    if (expect_status(status, SL_STATUS_INVALID_STATE) != 0 ||
        diag.code != SL_DIAG_SQLITE_PROVIDER_ERROR || !diag_has_hint(&diag, "provider: sqlite") ||
        !diag_has_hint(&diag, "operation: exec"))
    {
        return 4;
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
        return 12;
    }

    status =
        sl_sqlite_exec(&arena, &connection, sl_str_from_cstr("insert into users (name) values (?)"),
                       insert_params, 1U, &exec_result, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || exec_result.changes != 1) {
        return 13;
    }

    status = sl_sqlite_query(&arena, &connection,
                             sl_str_from_cstr("select id, name from users where name = ?"),
                             insert_params, 1U, NULL, &query_result, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || query_result.column_count != 2U ||
        query_result.row_count != 1U)
    {
        return 14;
    }
    if (expect_str_equal(query_result.column_names[0], "id") != 0 ||
        expect_str_equal(query_result.column_names[1], "name") != 0 ||
        query_result.rows[0].values[0].kind != SL_SQLITE_VALUE_INTEGER ||
        query_result.rows[0].values[0].value.integer != 1 ||
        query_result.rows[0].values[1].kind != SL_SQLITE_VALUE_TEXT ||
        expect_str_equal(query_result.rows[0].values[1].value.text, "Ada") != 0)
    {
        return 15;
    }

    status = sl_sqlite_query_one(&arena, &connection,
                                 sl_str_from_cstr("select name from users order by id"), NULL, 0U,
                                 &one, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || !one.found || one.column_count != 1U ||
        one.values[0].kind != SL_SQLITE_VALUE_TEXT ||
        expect_str_equal(one.values[0].value.text, "Ada") != 0)
    {
        return 16;
    }

    status = sl_sqlite_query_one(&arena, &connection,
                                 sl_str_from_cstr("select name from users where name = 'Grace'"),
                                 NULL, 0U, &one, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || one.found) {
        return 17;
    }

    return expect_status(sl_sqlite_close(&connection), SL_STATUS_OK) == 0 ? 0 : 18;
}

static int test_parameter_binding_and_types(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlSqliteConnection connection = {0};
    SlSqliteParam params[] = {null_param(), text_param("Ada'); drop table values_test; --"),
                              int_param(42), float_param(3.5), bool_param(true)};
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
                 "create table values_test (n, s text, i integer, f real, b integer)") != 0)
    {
        return 22;
    }

    status = sl_sqlite_exec(
        &arena, &connection,
        sl_str_from_cstr("insert into values_test (n, s, i, f, b) values (?, ?, ?, ?, ?)"), params,
        5U, &exec_result, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || exec_result.changes != 1) {
        return 23;
    }

    status = sl_sqlite_query_one(&arena, &connection,
                                 sl_str_from_cstr("select n, s, i, f, b from values_test"), NULL,
                                 0U, &row, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || !row.found || row.column_count != 5U) {
        return 24;
    }
    if (row.values[0].kind != SL_SQLITE_VALUE_NULL || row.values[1].kind != SL_SQLITE_VALUE_TEXT ||
        expect_str_equal(row.values[1].value.text, "Ada'); drop table values_test; --") != 0 ||
        row.values[2].kind != SL_SQLITE_VALUE_INTEGER || row.values[2].value.integer != 42 ||
        row.values[3].kind != SL_SQLITE_VALUE_FLOAT || row.values[3].value.number != 3.5 ||
        row.values[4].kind != SL_SQLITE_VALUE_INTEGER || row.values[4].value.integer != 1)
    {
        return 25;
    }

    status = sl_sqlite_query_one(&arena, &connection,
                                 sl_str_from_cstr("select count(*) from values_test"), NULL, 0U,
                                 &row, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || !row.found || row.values[0].value.integer != 1)
    {
        return 26;
    }

    return expect_status(sl_sqlite_close(&connection), SL_STATUS_OK) == 0 ? 0 : 27;
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
        return 32;
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
        return 42;
    }

    status = sl_sqlite_transaction_begin(&arena, &connection, &tx, &diag);
    if (expect_status(status, SL_STATUS_OK) != 0 || !tx.active) {
        return 43;
    }
    status = sl_sqlite_transaction_begin(&arena, &connection, &nested, &diag);
    if (expect_status(status, SL_STATUS_INVALID_STATE) != 0 ||
        diag.code != SL_DIAG_SQLITE_PROVIDER_ERROR)
    {
        return 44;
    }
    status = sl_sqlite_transaction_exec(
        &arena, &tx, sl_str_from_cstr("insert into tx_test (name) values (?)"),
        &(SlSqliteParam){.kind = SL_SQLITE_PARAM_TEXT, .value.text = sl_str_from_cstr("commit")},
        1U, &exec_result, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0) {
        return 45;
    }
    if (expect_status(sl_sqlite_transaction_commit(&arena, &tx, NULL), SL_STATUS_OK) != 0) {
        return 46;
    }
    status = sl_sqlite_transaction_exec(&arena, &tx, sl_str_from_cstr("select 1"), NULL, 0U,
                                        &exec_result, &diag);
    if (expect_status(status, SL_STATUS_INVALID_STATE) != 0) {
        return 47;
    }

    status = sl_sqlite_transaction_begin(&arena, &connection, &tx, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0) {
        return 48;
    }
    status = sl_sqlite_transaction_exec(
        &arena, &tx, sl_str_from_cstr("insert into tx_test (name) values ('rollback')"), NULL, 0U,
        &exec_result, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0) {
        return 49;
    }
    if (expect_status(sl_sqlite_transaction_rollback(&arena, &tx, NULL), SL_STATUS_OK) != 0) {
        return 50;
    }

    status =
        sl_sqlite_query_one(&arena, &connection, sl_str_from_cstr("select count(*) from tx_test"),
                            NULL, 0U, &one, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || !one.found ||
        one.values[0].kind != SL_SQLITE_VALUE_INTEGER || one.values[0].value.integer != 1)
    {
        return 51;
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
        return 62;
    }

    status = sl_sqlite_query(&arena, &connection, sl_str_from_cstr("select * from missing_table"),
                             NULL, 0U, NULL, &query_result, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_SQLITE_PROVIDER_ERROR || !diag_has_hint(&diag, "provider: sqlite") ||
        !diag_has_hint(&diag, "operation: query"))
    {
        return 63;
    }

    return expect_status(sl_sqlite_close(&connection), SL_STATUS_OK) == 0 ? 0 : 64;
}

static int test_invalid_open_options(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlSqliteConnection connection = {0};
    SlSqliteOpenOptions options = {.path = sl_str_from_cstr("missing-sloppy-sqlite-test.db"),
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

    return test_invalid_open_options();
}

#define _CRT_SECURE_NO_WARNINGS

#include "sloppy/data_sqlserver.h"

#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ARENA_SIZE (192U * 1024U)

static int expect_status(SlStatus status, SlStatusCode code)
{
    return sl_status_code(status) == code ? 0 : 1;
}

static int expect_str_equal(SlStr actual, const char* expected)
{
    const size_t length = strlen(expected);

    if (actual.length != length) {
        return 1;
    }
    return memcmp(actual.ptr, expected, length) == 0 ? 0 : 1;
}

static bool diag_has_hint_containing(const SlDiag* diag, const char* expected)
{
    const size_t expected_length = strlen(expected);

    if (diag == NULL) {
        return false;
    }
    for (size_t index = 0U; index < diag->hint_count; index += 1U) {
        SlStr hint = diag->hints[index];
        for (size_t offset = 0U; offset + expected_length <= hint.length; offset += 1U) {
            if (memcmp(hint.ptr + offset, expected, expected_length) == 0) {
                return true;
            }
        }
    }
    return false;
}

static void print_diag(const SlDiag* diag)
{
    if (diag == NULL) {
        return;
    }
    printf("diag code: %d\n", (int)diag->code);
    for (size_t index = 0U; index < diag->hint_count; index += 1U) {
        printf("hint %zu: %.*s\n", index, (int)diag->hints[index].length, diag->hints[index].ptr);
    }
}

static SlSqlServerParam text_param(const char* text)
{
    SlSqlServerParam param;

    param.kind = SL_SQLSERVER_PARAM_TEXT;
    param.value.text = sl_str_from_cstr(text);
    return param;
}

static SlSqlServerParam bool_param(bool value)
{
    SlSqlServerParam param;

    param.kind = SL_SQLSERVER_PARAM_BOOL;
    param.value.boolean = value;
    return param;
}

static int close_and_return(SlSqlServerConnection* connection, int code)
{
    if (connection != NULL && connection->open) {
        (void)sl_sqlserver_close(connection);
    }
    return code;
}

static int test_redaction_driver_parse_and_doctor(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlStr redacted = {0};
    SlStr driver = {0};
    SlStr oversized = {.ptr = "", .length = SIZE_MAX};
    SlDiag diag = {0};
    SlSqlServerDoctorResult doctor = {0};
    SlSqlServerOpenOptions options = sl_sqlserver_open_options_connection_string(sl_str_from_cstr(
        "Driver={Sloppy Missing Driver For "
        "Tests};Server=localhost;Database=sloppy;UID=sa;PWD=secret;TrustServerCertificate=yes;"));
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 10;
    }
    status = sl_sqlserver_redact_connection_string(
        &arena,
        sl_str_from_cstr(
            "Driver={ODBC Driver 18 for SQL Server};UID=sa;PWD=secret;Password={top;secret};Access "
            "Token=abc;Server=localhost"),
        &redacted);
    if (expect_status(status, SL_STATUS_OK) != 0 ||
        expect_str_equal(
            redacted,
            "Driver={ODBC Driver 18 for SQL Server};UID=sa;PWD=******;Password=************;Access "
            "Token=***;Server=localhost") != 0)
    {
        return 11;
    }
    status = sl_sqlserver_redact_connection_string(
        &arena,
        sl_str_from_cstr("Driver = {ODBC Driver 18 for SQL Server};UID=sa;PWD = secret;Password "
                         "={top;secret};Access Token = abc;Server=localhost"),
        &redacted);
    if (expect_status(status, SL_STATUS_OK) != 0 ||
        expect_str_equal(redacted,
                         "Driver = {ODBC Driver 18 for SQL Server};UID=sa;PWD = "
                         "******;Password =************;Access Token = ***;Server=localhost") != 0)
    {
        return 15;
    }
    status = sl_sqlserver_redact_connection_string(&arena, oversized, &redacted);
    if (expect_status(status, SL_STATUS_OVERFLOW) != 0) {
        return 16;
    }
    status = sl_sqlserver_extract_driver_name(
        &arena,
        sl_str_from_cstr("Server=localhost; Driver = {ODBC Driver 18 for SQL Server};Database=x"),
        &driver);
    if (expect_status(status, SL_STATUS_OK) != 0 ||
        expect_str_equal(driver, "ODBC Driver 18 for SQL Server") != 0)
    {
        return 12;
    }
    status = sl_sqlserver_doctor(&arena, &options, &doctor, &diag);
    if (sl_status_code(status) == SL_STATUS_UNSUPPORTED) {
        if (doctor.ok || expect_str_equal(doctor.driver_manager, "unavailable") != 0 ||
            diag.code != SL_DIAG_SQLSERVER_PROVIDER_ERROR)
        {
            return 13;
        }
        return 0;
    }
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 || doctor.ok ||
        expect_str_equal(doctor.provider, "sqlserver") != 0 ||
        expect_str_equal(doctor.driver_manager, "available") != 0 ||
        expect_str_equal(doctor.driver, "missing") != 0 ||
        diag.code != SL_DIAG_SQLSERVER_PROVIDER_ERROR ||
        !diag_has_hint_containing(&diag, "Sloppy Missing Driver For Tests") ||
        !diag_has_hint_containing(&diag, "Microsoft ODBC Driver 18 for SQL Server") ||
        diag_has_hint_containing(&diag, "PWD=secret"))
    {
        return 14;
    }
    return 0;
}

static int test_invalid_options_and_use_after_close(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlSqlServerConnection connection = {0};
#ifdef SLOPPY_ENABLE_SQLSERVER_PROVIDER
    SlSqlServerConnection read_only = {.open = true, .access = SL_SQLSERVER_ACCESS_READ};
#endif
    SlSqlServerExecResult result = {0};
    SlSqlServerOpenOptions options = sl_sqlserver_open_options_connection_string(sl_str_empty());
    SlSqlServerPool pool = {0};
    SlSqlServerPoolOptions pool_options =
        sl_sqlserver_pool_options_connection_string(sl_str_from_cstr("Driver={x};Server=x"), 1U);
    SlDiag diag = {0};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 20;
    }
#ifndef SLOPPY_ENABLE_SQLSERVER_PROVIDER
    status = sl_sqlserver_open(&arena, &options, &connection, &diag);
    if (expect_status(status, SL_STATUS_UNSUPPORTED) != 0 ||
        diag.code != SL_DIAG_SQLSERVER_PROVIDER_ERROR ||
        !diag_has_hint_containing(&diag, "provider: sqlserver") ||
        !diag_has_hint_containing(&diag, "operation: open"))
    {
        return 21;
    }
    status = sl_sqlserver_exec(&arena, &connection, sl_str_from_cstr("select 1"), NULL, 0U, &result,
                               &diag);
    if (expect_status(status, SL_STATUS_UNSUPPORTED) != 0 ||
        diag.code != SL_DIAG_SQLSERVER_PROVIDER_ERROR)
    {
        return 22;
    }
    pool_options.access = (SlSqlServerAccess)99;
    status = sl_sqlserver_pool_open(&arena, &pool_options, &pool, &diag);
    if (expect_status(status, SL_STATUS_UNSUPPORTED) != 0 ||
        diag.code != SL_DIAG_SQLSERVER_PROVIDER_ERROR)
    {
        return 24;
    }
    return 0;
#else
    status = sl_sqlserver_open(&arena, &options, &connection, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_SQLSERVER_PROVIDER_ERROR ||
        !diag_has_hint_containing(&diag, "provider: sqlserver") ||
        !diag_has_hint_containing(&diag, "operation: open"))
    {
        return 21;
    }
    status = sl_sqlserver_exec(&arena, &connection, sl_str_from_cstr("select 1"), NULL, 0U, &result,
                               &diag);
    if (sl_status_code(status) != SL_STATUS_INVALID_STATE &&
        sl_status_code(status) != SL_STATUS_UNSUPPORTED)
    {
        return 22;
    }
    status = sl_sqlserver_exec(&arena, &read_only, sl_str_from_cstr("insert into t values (1)"),
                               NULL, 0U, &result, &diag);
    if (expect_status(status, SL_STATUS_INVALID_STATE) != 0 ||
        diag.code != SL_DIAG_PERMISSION_DENIED)
    {
        return 25;
    }
    options.connection_string = sl_str_from_cstr("Driver={x};Server=x");
    options.access = (SlSqlServerAccess)99;
    status = sl_sqlserver_open(&arena, &options, &connection, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_SQLSERVER_PROVIDER_ERROR)
    {
        return 23;
    }
    pool_options.access = (SlSqlServerAccess)99;
    status = sl_sqlserver_pool_open(&arena, &pool_options, &pool, &diag);
    if (sl_status_code(status) != SL_STATUS_INVALID_ARGUMENT &&
        sl_status_code(status) != SL_STATUS_UNSUPPORTED)
    {
        return 24;
    }
    return 0;
#endif
}

static int test_pool_state_machine_without_live_connection(void)
{
#ifndef SLOPPY_ENABLE_SQLSERVER_PROVIDER
    return 0;
#else
    SlSqlServerPool pool = {0};
    SlSqlServerConnection foreign = {.open = true};

    pool.open_count = 1U;
    pool.max_connections = 1U;
    pool.connections[0].open = true;
    pool.connections[0].transaction_active = true;

    if (expect_status(sl_sqlserver_pool_release(&pool, &pool.connections[0]),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        return 30;
    }
    pool.connections[0].transaction_active = false;
    if (expect_status(sl_sqlserver_pool_release(&pool, &foreign), SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 31;
    }
    if (expect_status(sl_sqlserver_pool_release(&pool, &pool.connections[0]), SL_STATUS_OK) != 0) {
        return 32;
    }
    if (expect_status(sl_sqlserver_pool_release(&pool, &pool.connections[0]),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        return 33;
    }
    return 0;
#endif
}

static int open_live(SlArena* arena, SlSqlServerConnection* connection)
{
    const char* connection_string = getenv("SLOPPY_SQLSERVER_TEST_CONNECTION_STRING");
    SlSqlServerOpenOptions options;
    SlStatus status;

    if (connection_string == NULL || connection_string[0] == '\0') {
        printf("SKIP: SLOPPY_SQLSERVER_TEST_CONNECTION_STRING not set; live SQL Server tests "
               "disabled.\n");
        return 77;
    }
    options = sl_sqlserver_open_options_connection_string(sl_str_from_cstr(connection_string));
    status = sl_sqlserver_open(arena, &options, connection, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0) {
        return 1;
    }
    return 0;
}

static int test_live_query_exec_and_transactions(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlSqlServerConnection connection = {0};
    SlSqlServerExecResult exec_result = {0};
    SlSqlServerQueryOneResult one = {0};
    SlSqlServerTransaction tx = {0};
    SlSqlServerParam ada_param = text_param("Ada");
    SlSqlServerParam insert_params[] = {text_param("Ada"), bool_param(true)};
    SlSqlServerParam rollback_params[] = {text_param("rollback"), bool_param(false)};
    SlSqlServerParam unsupported_param = {.kind = SL_SQLSERVER_PARAM_UNSUPPORTED};
    SlDiag diag = {0};
    char long_alias_sql[180] = {0};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));
    int open_result = 0;

    if (!sl_status_is_ok(status)) {
        return 40;
    }
    open_result = open_live(&arena, &connection);
    if (open_result == 77) {
        return 0;
    }
    if (open_result != 0) {
        return 41;
    }
    (void)sl_sqlserver_exec(
        &arena, &connection,
        sl_str_from_cstr("drop table if exists dbo.sloppy_sqlserver_provider_test"), NULL, 0U,
        &exec_result, NULL);
    status =
        sl_sqlserver_exec(&arena, &connection,
                          sl_str_from_cstr("create table dbo.sloppy_sqlserver_provider_test (id "
                                           "int identity primary key, name nvarchar(100), ok bit)"),
                          NULL, 0U, &exec_result, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0) {
        return close_and_return(&connection, 42);
    }
    status = sl_sqlserver_exec(
        &arena, &connection,
        sl_str_from_cstr("insert into dbo.sloppy_sqlserver_provider_test (name, ok) values (?, ?)"),
        insert_params, 2U, &exec_result, &diag);
    if (expect_status(status, SL_STATUS_OK) != 0 || !exec_result.affected_rows_known ||
        exec_result.affected_rows != 1)
    {
        print_diag(&diag);
        return close_and_return(&connection, 43);
    }
    status = sl_sqlserver_query_one(
        &arena, &connection,
        sl_str_from_cstr("select name, ok from dbo.sloppy_sqlserver_provider_test where name = ?"),
        &ada_param, 1U, &one, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || !one.found || one.column_count != 2U ||
        one.values[0].kind != SL_SQLSERVER_VALUE_TEXT ||
        expect_str_equal(one.values[0].value.text, "Ada") != 0 ||
        one.values[1].kind != SL_SQLSERVER_VALUE_BOOL || !one.values[1].value.boolean)
    {
        return close_and_return(&connection, 44);
    }
    status = sl_sqlserver_query_one(
        &arena, &connection,
        sl_str_from_cstr("select cast(7 as bigint) as whole, cast(1.25 as float) as fractional"),
        NULL, 0U, &one, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || !one.found ||
        one.values[0].kind != SL_SQLSERVER_VALUE_INTEGER || one.values[0].value.integer != 7 ||
        one.values[1].kind != SL_SQLSERVER_VALUE_FLOAT || one.values[1].value.number < 1.24 ||
        one.values[1].value.number > 1.26)
    {
        return close_and_return(&connection, 45);
    }
    status = sl_sqlserver_query_one(
        &arena, &connection,
        sl_str_from_cstr("select cast(replicate('x', 5000) as varchar(max)) as payload"), NULL, 0U,
        &one, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || !one.found ||
        one.values[0].kind != SL_SQLSERVER_VALUE_TEXT || one.values[0].value.text.length != 5000U ||
        one.values[0].value.text.ptr[0] != 'x' || one.values[0].value.text.ptr[4999] != 'x')
    {
        return close_and_return(&connection, 53);
    }
    (void)memcpy(long_alias_sql, "select 1 as [", sizeof("select 1 as [") - 1U);
    for (size_t index = sizeof("select 1 as [") - 1U; index < 141U; index += 1U) {
        long_alias_sql[index] = 'a';
    }
    long_alias_sql[141] = ']';
    long_alias_sql[142] = '\0';
    status = sl_sqlserver_query_one(&arena, &connection, sl_str_from_cstr(long_alias_sql), NULL, 0U,
                                    &one, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || !one.found || one.column_count != 1U ||
        one.column_names[0].length != 128U)
    {
        return close_and_return(&connection, 54);
    }
    status = sl_sqlserver_transaction_begin(&arena, &connection, &tx, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0) {
        return close_and_return(&connection, 46);
    }
    status = sl_sqlserver_transaction_exec(
        &arena, &tx,
        sl_str_from_cstr("insert into dbo.sloppy_sqlserver_provider_test (name, ok) values (?, ?)"),
        rollback_params, 2U, &exec_result, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0) {
        return close_and_return(&connection, 47);
    }
    if (expect_status(sl_sqlserver_transaction_rollback(&arena, &tx, NULL), SL_STATUS_OK) != 0) {
        return close_and_return(&connection, 48);
    }
    status = sl_sqlserver_query_one(
        &arena, &connection,
        sl_str_from_cstr("select count(*) from dbo.sloppy_sqlserver_provider_test"), NULL, 0U, &one,
        NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || !one.found ||
        one.values[0].kind != SL_SQLSERVER_VALUE_INTEGER || one.values[0].value.integer != 1)
    {
        return close_and_return(&connection, 49);
    }
    status = sl_sqlserver_exec(&arena, &connection, sl_str_from_cstr("select ?"),
                               &unsupported_param, 1U, &exec_result, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_DATABASE_UNSUPPORTED_VALUE)
    {
        return close_and_return(&connection, 50);
    }
    status = sl_sqlserver_exec(&arena, &connection, sl_str_from_cstr("select from"), NULL, 0U,
                               &exec_result, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_SQLSERVER_PROVIDER_ERROR)
    {
        return close_and_return(&connection, 51);
    }
    (void)sl_sqlserver_exec(
        &arena, &connection,
        sl_str_from_cstr("drop table if exists dbo.sloppy_sqlserver_provider_test"), NULL, 0U,
        &exec_result, NULL);
    return expect_status(sl_sqlserver_close(&connection), SL_STATUS_OK) == 0 ? 0 : 52;
}

static int test_live_pool(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlSqlServerPool pool = {0};
    SlSqlServerConnection* first = NULL;
    SlSqlServerConnection* second = NULL;
    SlSqlServerConnection* third = NULL;
    SlDiag diag = {0};
    const char* connection_string = getenv("SLOPPY_SQLSERVER_TEST_CONNECTION_STRING");
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));
    SlSqlServerPoolOptions options;

    if (!sl_status_is_ok(status)) {
        return 60;
    }
    if (connection_string == NULL || connection_string[0] == '\0') {
        printf("SKIP: SLOPPY_SQLSERVER_TEST_CONNECTION_STRING not set; live SQL Server pool tests "
               "disabled.\n");
        return 0;
    }
    options = sl_sqlserver_pool_options_connection_string(sl_str_from_cstr(connection_string), 2U);
    status = sl_sqlserver_pool_open(&arena, &options, &pool, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0) {
        return 61;
    }
    if (expect_status(sl_sqlserver_pool_acquire(&arena, &pool, &first, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_sqlserver_pool_acquire(&arena, &pool, &second, NULL), SL_STATUS_OK) != 0)
    {
        (void)sl_sqlserver_pool_close(&pool);
        return 62;
    }
    status = sl_sqlserver_pool_acquire(&arena, &pool, &third, &diag);
    if (expect_status(status, SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        diag.code != SL_DIAG_SQLSERVER_POOL_EXHAUSTED)
    {
        (void)sl_sqlserver_pool_close(&pool);
        return 63;
    }
    if (expect_status(sl_sqlserver_pool_release(&pool, first), SL_STATUS_OK) != 0 ||
        expect_status(sl_sqlserver_pool_acquire(&arena, &pool, &third, NULL), SL_STATUS_OK) != 0 ||
        third != first)
    {
        (void)sl_sqlserver_pool_close(&pool);
        return 64;
    }
    return expect_status(sl_sqlserver_pool_close(&pool), SL_STATUS_OK) == 0 ? 0 : 65;
}

int main(void)
{
    int result = test_redaction_driver_parse_and_doctor();
    if (result != 0) {
        return result;
    }
    result = test_invalid_options_and_use_after_close();
    if (result != 0) {
        return result;
    }
    result = test_pool_state_machine_without_live_connection();
    if (result != 0) {
        return result;
    }
    result = test_live_query_exec_and_transactions();
    if (result != 0) {
        return result;
    }
    return test_live_pool();
}

#define _CRT_SECURE_NO_WARNINGS

#include "sloppy/data_postgres.h"

#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stdlib.h>
#include <string.h>

#define TEST_ARENA_SIZE (128U * 1024U)

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

static bool diag_has_hint(const SlDiag* diag, const char* expected)
{
    size_t expected_length = strlen(expected);

    for (size_t index = 0U; index < diag->hint_count; index += 1U) {
        SlStr hint = diag->hints[index];
        if (hint.length == expected_length && memcmp(hint.ptr, expected, expected_length) == 0) {
            return true;
        }
    }
    return false;
}

static SlPostgresParam text_param(const char* text)
{
    SlPostgresParam param;

    param.kind = SL_POSTGRES_PARAM_TEXT;
    param.value.text = sl_str_from_cstr(text);
    return param;
}

static SlPostgresParam bool_param(bool value)
{
    SlPostgresParam param;

    param.kind = SL_POSTGRES_PARAM_BOOL;
    param.value.boolean = value;
    return param;
}

static int close_and_return(SlPostgresConnection* connection, int code)
{
    if (connection != NULL && connection->open) {
        (void)sl_postgres_close(connection);
    }
    return code;
}

static int test_redaction_and_doctor(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlStr redacted = {0};
    SlDiag diag = {0};
    SlPostgresOpenOptions options = sl_postgres_open_options_connection_string(
        sl_str_from_cstr("host=localhost password=secret user=ada"));
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 10;
    }
    status = sl_postgres_redact_connection_string(
        &arena, sl_str_from_cstr("host=localhost password=secret user=ada"), &redacted);
    if (expect_status(status, SL_STATUS_OK) != 0 ||
        expect_str_equal(redacted, "host=localhost password=****** user=ada") != 0)
    {
        return 11;
    }
    status = sl_postgres_redact_connection_string(
        &arena, sl_str_from_cstr("postgres://ada:secret@localhost/sloppy_test"), &redacted);
    if (expect_status(status, SL_STATUS_OK) != 0 ||
        expect_str_equal(redacted, "postgres://ada:******@localhost/sloppy_test") != 0)
    {
        return 14;
    }

    status = sl_postgres_doctor(&arena, &options, &diag);
    if (expect_status(status, SL_STATUS_OK) != 0 || diag.code != SL_DIAG_POSTGRES_PROVIDER_ERROR ||
        !diag_has_hint(&diag, "provider: postgres") || !diag_has_hint(&diag, "operation: doctor") ||
        diag_has_hint(&diag, "host=localhost password=secret user=ada"))
    {
        return 12;
    }

    options.connection_string = sl_str_empty();
    status = sl_postgres_doctor(&arena, &options, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_POSTGRES_PROVIDER_ERROR)
    {
        return 13;
    }

    return 0;
}

static int test_invalid_options_and_use_after_close(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlPostgresConnection connection = {0};
    SlPostgresExecResult result = {0};
    SlDiag diag = {0};
    SlPostgresOpenOptions options = sl_postgres_open_options_connection_string(sl_str_empty());
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 20;
    }

    status = sl_postgres_open(&arena, &options, &connection, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_POSTGRES_PROVIDER_ERROR ||
        !diag_has_hint(&diag, "provider: postgres") || !diag_has_hint(&diag, "operation: open"))
    {
        return 21;
    }

    status = sl_postgres_exec(&arena, &connection, sl_str_from_cstr("select 1"), NULL, 0U, &result,
                              &diag);
    if (expect_status(status, SL_STATUS_INVALID_STATE) != 0 ||
        diag.code != SL_DIAG_POSTGRES_PROVIDER_ERROR || !diag_has_hint(&diag, "operation: exec"))
    {
        return 22;
    }

    return 0;
}

static int test_unsupported_parameter_without_live_connection(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlPostgresConnection connection = {0};
    SlPostgresExecResult result = {0};
    SlDiag diag = {0};
    SlPostgresParam param = {.kind = SL_POSTGRES_PARAM_UNSUPPORTED};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 30;
    }
    status = sl_postgres_exec(&arena, &connection, sl_str_from_cstr("select $1"), &param, 1U,
                              &result, &diag);
    if (expect_status(status, SL_STATUS_INVALID_STATE) != 0) {
        return 31;
    }

    return 0;
}

static int open_live(SlArena* arena, SlPostgresConnection* connection)
{
    const char* url = getenv("SLOPPY_POSTGRES_TEST_URL");
    SlPostgresOpenOptions options;
    SlStatus status;

    if (url == NULL || url[0] == '\0') {
        return 77;
    }
    options = sl_postgres_open_options_connection_string(sl_str_from_cstr(url));
    status = sl_postgres_open(arena, &options, connection, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0) {
        return 1;
    }
    return 0;
}

static int test_live_query_exec_and_transactions(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlPostgresConnection connection = {0};
    SlPostgresExecResult exec_result = {0};
    SlPostgresQueryOneResult one = {0};
    SlPostgresTransaction tx = {0};
    SlPostgresParam ada_param = text_param("Ada");
    SlPostgresParam unsupported_param = {.kind = SL_POSTGRES_PARAM_UNSUPPORTED};
    SlPostgresParam insert_params[] = {text_param("Ada"), bool_param(true)};
    SlPostgresParam rollback_params[] = {text_param("rollback"), bool_param(false)};
    SlDiag diag = {0};
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

    status = sl_postgres_exec(
        &arena, &connection,
        sl_str_from_cstr(
            "create temp table sloppy_pg_test (id serial primary key, name text, ok boolean)"),
        NULL, 0U, &exec_result, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0) {
        return close_and_return(&connection, 42);
    }

    status =
        sl_postgres_exec(&arena, &connection,
                         sl_str_from_cstr("insert into sloppy_pg_test (name, ok) values ($1, $2)"),
                         insert_params, 2U, &exec_result, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || !exec_result.affected_rows_known ||
        exec_result.affected_rows != 1)
    {
        return close_and_return(&connection, 43);
    }

    status = sl_postgres_query_one(
        &arena, &connection,
        sl_str_from_cstr("select name, ok from sloppy_pg_test where name = $1"), &ada_param, 1U,
        &one, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || !one.found || one.column_count != 2U ||
        one.values[0].kind != SL_POSTGRES_VALUE_TEXT ||
        expect_str_equal(one.values[0].value.text, "Ada") != 0 ||
        one.values[1].kind != SL_POSTGRES_VALUE_BOOL || !one.values[1].value.boolean)
    {
        return close_and_return(&connection, 44);
    }

    status = sl_postgres_transaction_begin(&arena, &connection, &tx, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0) {
        return close_and_return(&connection, 45);
    }
    status = sl_postgres_transaction_exec(
        &arena, &tx, sl_str_from_cstr("insert into sloppy_pg_test (name, ok) values ($1, $2)"),
        rollback_params, 2U, &exec_result, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0) {
        return close_and_return(&connection, 46);
    }
    if (expect_status(sl_postgres_transaction_rollback(&arena, &tx, NULL), SL_STATUS_OK) != 0) {
        return close_and_return(&connection, 47);
    }

    status = sl_postgres_query_one(&arena, &connection,
                                   sl_str_from_cstr("select count(*) from sloppy_pg_test"), NULL,
                                   0U, &one, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || !one.found ||
        one.values[0].kind != SL_POSTGRES_VALUE_INTEGER || one.values[0].value.integer != 1)
    {
        return close_and_return(&connection, 48);
    }

    status = sl_postgres_exec(&arena, &connection, sl_str_from_cstr("select $1"),
                              &unsupported_param, 1U, &exec_result, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_DATABASE_UNSUPPORTED_VALUE)
    {
        return close_and_return(&connection, 56);
    }

    return expect_status(sl_postgres_close(&connection), SL_STATUS_OK) == 0 ? 0 : 49;
}

static int test_live_pool(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlPostgresPool pool = {0};
    SlPostgresConnection* first = NULL;
    SlPostgresConnection* second = NULL;
    SlPostgresConnection* third = NULL;
    SlDiag diag = {0};
    const char* url = getenv("SLOPPY_POSTGRES_TEST_URL");
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));
    SlPostgresPoolOptions options;

    if (!sl_status_is_ok(status)) {
        return 50;
    }
    if (url == NULL || url[0] == '\0') {
        return 0;
    }
    options = sl_postgres_pool_options_connection_string(sl_str_from_cstr(url), 2U);
    status = sl_postgres_pool_open(&arena, &options, &pool, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0) {
        return 51;
    }
    if (expect_status(sl_postgres_pool_acquire(&arena, &pool, &first, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_postgres_pool_acquire(&arena, &pool, &second, NULL), SL_STATUS_OK) != 0)
    {
        (void)sl_postgres_pool_close(&pool);
        return 52;
    }
    status = sl_postgres_pool_acquire(&arena, &pool, &third, &diag);
    if (expect_status(status, SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        diag.code != SL_DIAG_POSTGRES_POOL_EXHAUSTED)
    {
        (void)sl_postgres_pool_close(&pool);
        return 53;
    }
    if (expect_status(sl_postgres_pool_release(&pool, first), SL_STATUS_OK) != 0 ||
        expect_status(sl_postgres_pool_acquire(&arena, &pool, &third, NULL), SL_STATUS_OK) != 0 ||
        third != first)
    {
        (void)sl_postgres_pool_close(&pool);
        return 54;
    }
    return expect_status(sl_postgres_pool_close(&pool), SL_STATUS_OK) == 0 ? 0 : 55;
}

int main(void)
{
    int result = test_redaction_and_doctor();
    if (result != 0) {
        return result;
    }
    result = test_invalid_options_and_use_after_close();
    if (result != 0) {
        return result;
    }
    result = test_unsupported_parameter_without_live_connection();
    if (result != 0) {
        return result;
    }
    result = test_live_query_exec_and_transactions();
    if (result != 0) {
        return result;
    }
    return test_live_pool();
}

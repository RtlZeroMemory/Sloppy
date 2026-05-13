#define _CRT_SECURE_NO_WARNINGS

#include "sloppy/data_postgres.h"

#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stdio.h>
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

static bool diag_contains_text(const SlDiag* diag, const char* expected)
{
    const size_t expected_length = strlen(expected);

    if (diag == NULL) {
        return false;
    }
    if (diag->message.ptr != NULL) {
        for (size_t offset = 0U; offset + expected_length <= diag->message.length; offset += 1U) {
            if (memcmp(diag->message.ptr + offset, expected, expected_length) == 0) {
                return true;
            }
        }
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

static const char* classify_live_open_failure(const SlDiag* diag)
{
    if (diag_contains_text(diag, "password authentication failed") ||
        diag_contains_text(diag, "authentication failed") ||
        diag_contains_text(diag, "no password supplied"))
    {
        return "credentials rejected";
    }
    if (diag_contains_text(diag, "Connection refused") ||
        diag_contains_text(diag, "could not connect") || diag_contains_text(diag, "timeout") ||
        diag_contains_text(diag, "No such file") ||
        diag_contains_text(diag, "could not translate host name"))
    {
        return "service unreachable";
    }
    return "test failure";
}

static void report_live_provider_not_configured(const char* provider, const char* env_var)
{
    printf("SKIP: live %s provider tests are not configured; set %s to enable them. Secret "
           "values are never printed.\n",
           provider, env_var);
}

static void report_live_provider_open_failure(const char* provider, const SlDiag* diag)
{
    printf("FAIL: live %s provider open failed; category: %s. Diagnostics are redacted and "
           "connection strings are not printed.\n",
           provider, classify_live_open_failure(diag));
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

static SlPostgresParam bytes_param(const unsigned char* value, size_t length)
{
    SlPostgresParam param;

    param.kind = SL_POSTGRES_PARAM_BYTES;
    param.value.bytes = sl_bytes_from_parts(value, length);
    return param;
}

static int close_and_return(SlPostgresConnection* connection, int code)
{
    if (connection != NULL && connection->open) {
        sl_postgres_close(connection);
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
        &arena, sl_str_from_cstr("host=localhost Password='secret value' user=ada"), &redacted);
    if (expect_status(status, SL_STATUS_OK) != 0 ||
        expect_str_equal(redacted, "host=localhost Password=************** user=ada") != 0)
    {
        return 15;
    }
    status = sl_postgres_redact_connection_string(
        &arena, sl_str_from_cstr("password='secret\\' value' user=ada"), &redacted);
    if (expect_status(status, SL_STATUS_OK) != 0 ||
        expect_str_equal(redacted, "password=**************** user=ada") != 0)
    {
        return 16;
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

    diag = (SlDiag){0};
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
    SlPostgresResult query_result = {.column_count = 7U,
                                     .column_names = (SlStr*)storage,
                                     .row_count = 3U,
                                     .rows = (SlPostgresRow*)storage};
    SlPostgresQueryOneResult one = {.found = true,
                                    .column_count = 5U,
                                    .column_names = (SlStr*)storage,
                                    .values = (SlPostgresValue*)storage};
    SlDiag diag = {0};
    SlPostgresOpenOptions options = sl_postgres_open_options_connection_string(sl_str_empty());
    SlPostgresPool pool = {0};
    SlPostgresPoolOptions pool_options =
        sl_postgres_pool_options_connection_string(sl_str_from_cstr("postgres://localhost/db"), 1U);
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 20;
    }
    if (SL_POSTGRES_MAX_POOL_CONNECTIONS != 16U || SL_POSTGRES_MAX_RUNTIME_POOL_CONNECTIONS != 256U)
    {
        return 27;
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
    status =
        sl_postgres_exec_batch(&arena, &connection, sl_str_from_cstr("select 1"), &result, &diag);
    if (expect_status(status, SL_STATUS_INVALID_STATE) != 0 ||
        diag.code != SL_DIAG_POSTGRES_PROVIDER_ERROR ||
        !diag_has_hint(&diag, "operation: execBatch"))
    {
        return 28;
    }
    status = sl_postgres_query(&arena, &connection, sl_str_from_cstr("select 1"), NULL, 0U, NULL,
                               &query_result, &diag);
    if (expect_status(status, SL_STATUS_INVALID_STATE) != 0 || query_result.column_count != 0U ||
        query_result.column_names != NULL || query_result.row_count != 0U ||
        query_result.rows != NULL)
    {
        return 25;
    }
    status = sl_postgres_query_one(&arena, &connection, sl_str_from_cstr("select 1"), NULL, 0U,
                                   &one, &diag);
    if (expect_status(status, SL_STATUS_INVALID_STATE) != 0 || one.found ||
        one.column_count != 0U || one.column_names != NULL || one.values != NULL)
    {
        return 26;
    }

    diag = (SlDiag){0};
    options.connection_string = sl_str_from_cstr("postgres://localhost/db");
    options.access = (SlPostgresAccess)99;
    status = sl_postgres_open(&arena, &options, &connection, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_POSTGRES_PROVIDER_ERROR || !diag_has_hint(&diag, "operation: open"))
    {
        return 23;
    }

    diag = (SlDiag){0};
    pool_options = sl_postgres_pool_options_connection_string(
        sl_str_from_cstr("postgres://localhost/db"), SL_POSTGRES_MAX_POOL_CONNECTIONS);
    status = sl_postgres_pool_open(&arena, &pool_options, &pool, &diag);
    if (expect_status(status, SL_STATUS_OK) != 0 ||
        pool.max_connections != SL_POSTGRES_MAX_POOL_CONNECTIONS ||
        expect_status(sl_postgres_pool_close(&pool), SL_STATUS_OK) != 0)
    {
        return 28;
    }

    diag = (SlDiag){0};
    pool_options = sl_postgres_pool_options_connection_string(
        sl_str_from_cstr("postgres://localhost/db"), SL_POSTGRES_MAX_POOL_CONNECTIONS + 1U);
    status = sl_postgres_pool_open(&arena, &pool_options, &pool, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_POSTGRES_PROVIDER_ERROR ||
        !diag_has_hint(&diag, "operation: pool.open"))
    {
        return 29;
    }

    diag = (SlDiag){0};
    pool_options =
        sl_postgres_pool_options_connection_string(sl_str_from_cstr("postgres://localhost/db"), 0U);
    status = sl_postgres_pool_open(&arena, &pool_options, &pool, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_POSTGRES_PROVIDER_ERROR ||
        !diag_has_hint(&diag, "operation: pool.open"))
    {
        return 30;
    }

    diag = (SlDiag){0};
    pool_options =
        sl_postgres_pool_options_connection_string(sl_str_from_cstr("postgres://localhost/db"), 1U);
    pool_options.access = (SlPostgresAccess)99;
    status = sl_postgres_pool_open(&arena, &pool_options, &pool, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
        diag.code != SL_DIAG_POSTGRES_PROVIDER_ERROR ||
        !diag_has_hint(&diag, "operation: pool.open"))
    {
        return 24;
    }

    return 0;
}

static int test_pool_lifecycle_without_live_connection(void)
{
    SlPostgresPool pool = {0};
    SlPostgresConnection foreign = {.open = true};

    pool.open_count = 1U;
    pool.max_connections = 1U;
    pool.connections[0].open = true;
    pool.connections[0].transaction_active = true;

    if (expect_status(sl_postgres_pool_release(&pool, &pool.connections[0]),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        return 70;
    }
    pool.connections[0].transaction_active = false;
    if (expect_status(sl_postgres_pool_release(&pool, &foreign), SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 71;
    }
    if (expect_status(sl_postgres_pool_release(&pool, &pool.connections[0]), SL_STATUS_OK) != 0) {
        return 72;
    }
    if (expect_status(sl_postgres_pool_release(&pool, &pool.connections[0]),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        return 73;
    }
    pool.connections[0].open = false;
    if (expect_status(sl_postgres_pool_close(&pool), SL_STATUS_OK) != 0 ||
        expect_status(sl_postgres_pool_close(&pool), SL_STATUS_OK) != 0)
    {
        return 74;
    }
    return 0;
}

static int test_invalid_outputs_are_preserved_without_live_connection(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlPostgresConnection sentinel = {.handle = storage, .open = true, .transaction_active = true};
    SlPostgresConnection* acquired = &sentinel;
    SlPostgresOpenOptions options = sl_postgres_open_options_connection_string(sl_str_empty());
    SlPostgresPool pool = {.closed = true};
    SlDiag diag = {0};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 75;
    }

    status = sl_postgres_open(&arena, &options, &sentinel, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 || sentinel.handle != storage ||
        !sentinel.open || !sentinel.transaction_active)
    {
        return 76;
    }

    options.connection_string = sl_str_from_cstr("postgres://localhost/db");
    options.access = (SlPostgresAccess)99;
    status = sl_postgres_open(&arena, &options, &sentinel, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 || sentinel.handle != storage ||
        !sentinel.open || !sentinel.transaction_active)
    {
        return 77;
    }

    status = sl_postgres_pool_acquire(&arena, &pool, &acquired, &diag);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 || acquired != &sentinel) {
        return 78;
    }

    return 0;
}

static int test_transaction_failure_keeps_state(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena = {0};
    SlPostgresConnection connection = {.transaction_active = true};
    SlPostgresTransaction tx = {.connection = &connection, .active = true};
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));

    if (!sl_status_is_ok(status)) {
        return 60;
    }
    status = sl_postgres_transaction_commit(&arena, &tx, NULL);
    if (expect_status(status, SL_STATUS_INVALID_STATE) != 0 || !tx.active ||
        tx.connection != &connection || !connection.transaction_active)
    {
        return 61;
    }
    status = sl_postgres_transaction_rollback(&arena, &tx, NULL);
    if (expect_status(status, SL_STATUS_INVALID_STATE) != 0 || !tx.active ||
        tx.connection != &connection || !connection.transaction_active)
    {
        return 62;
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
    SlDiag diag = {0};
    SlStatus status;

    if (url == NULL || url[0] == '\0') {
        report_live_provider_not_configured("PostgreSQL", "SLOPPY_POSTGRES_TEST_URL");
        return 77;
    }
    options = sl_postgres_open_options_connection_string(sl_str_from_cstr(url));
    status = sl_postgres_open(arena, &options, connection, &diag);
    if (expect_status(status, SL_STATUS_OK) != 0) {
        report_live_provider_open_failure("PostgreSQL", &diag);
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
    SlPostgresParam grace_param = text_param("Grace");
    SlPostgresParam unsupported_param = {.kind = SL_POSTGRES_PARAM_UNSUPPORTED};
    SlPostgresParam insert_params[] = {text_param("Ada"), bool_param(true)};
    SlPostgresParam rollback_params[] = {text_param("rollback"), bool_param(false)};
    const char attack_text[] = "Robert'); drop table sloppy_pg_test; --";
    const char unicode_text[] = {'u', 'n', 'i',        'c',        'o',        'd',
                                 'e', ' ', (char)0xe2, (char)0x98, (char)0x83, '\0'};
    const unsigned char raw_bytes[] = {0x00U, 0x27U, 0x3bU, 0x2dU, 0x2dU, 0xffU};
    SlPostgresParam safety_params[] = {text_param(attack_text), text_param(unicode_text),
                                       bytes_param(raw_bytes, sizeof(raw_bytes))};
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

    status = sl_postgres_query_one(
        &arena, &connection,
        sl_str_from_cstr("select $1::text as payload, $2::text as utf8, $3::bytea as raw"),
        safety_params, 3U, &one, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || !one.found || one.column_count != 3U ||
        one.values[0].kind != SL_POSTGRES_VALUE_TEXT ||
        expect_str_equal(one.values[0].value.text, attack_text) != 0 ||
        one.values[1].kind != SL_POSTGRES_VALUE_TEXT ||
        expect_str_equal(one.values[1].value.text, unicode_text) != 0 ||
        one.values[2].kind != SL_POSTGRES_VALUE_BYTES ||
        one.values[2].value.bytes.length != sizeof(raw_bytes) ||
        one.values[2].value.bytes.ptr == NULL ||
        memcmp(one.values[2].value.bytes.ptr, raw_bytes, sizeof(raw_bytes)) != 0)
    {
        return close_and_return(&connection, 61);
    }

    status = sl_postgres_query_one(&arena, &connection,
                                   sl_str_from_cstr("select count(*) from sloppy_pg_test"), NULL,
                                   0U, &one, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || !one.found ||
        one.values[0].kind != SL_POSTGRES_VALUE_INTEGER || one.values[0].value.integer != 0)
    {
        return close_and_return(&connection, 62);
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

    status =
        sl_postgres_exec(&arena, &connection,
                         sl_str_from_cstr("insert into sloppy_pg_test (name, ok) values ($1, $2)"),
                         insert_params, 2U, &exec_result, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0) {
        return close_and_return(&connection, 57);
    }

    status = sl_postgres_query_one(
        &arena, &connection,
        sl_str_from_cstr("select name from sloppy_pg_test where name = $1 order by id"), &ada_param,
        1U, &one, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || !one.found ||
        one.values[0].kind != SL_POSTGRES_VALUE_TEXT ||
        expect_str_equal(one.values[0].value.text, "Ada") != 0)
    {
        return close_and_return(&connection, 58);
    }

    status = sl_postgres_query_one(
        &arena, &connection,
        sl_str_from_cstr("select 7::int8 as whole, 1.25::float8 as fractional, $1::text as name"),
        &grace_param, 1U, &one, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || !one.found ||
        one.values[0].kind != SL_POSTGRES_VALUE_INTEGER || one.values[0].value.integer != 7 ||
        one.values[1].kind != SL_POSTGRES_VALUE_FLOAT || one.values[1].value.number < 1.24 ||
        one.values[1].value.number > 1.26 || one.values[2].kind != SL_POSTGRES_VALUE_TEXT ||
        expect_str_equal(one.values[2].value.text, "Grace") != 0)
    {
        return close_and_return(&connection, 59);
    }

    status = sl_postgres_query_one(
        &arena, &connection,
        sl_str_from_cstr("select 12345678901234567890.1234::numeric as amount, "
                         "'00000000-0000-4000-8000-000000000001'::uuid as id, "
                         "'{\"ok\":true}'::jsonb as payload, "
                         "'2026-05-08'::date as day, "
                         "'12:34:56'::time as clock, "
                         "'2026-05-08 12:34:56'::timestamp as local_time, "
                         "'2026-05-08 12:34:56+04'::timestamptz as instant, "
                         "decode('0041ff', 'hex')::bytea as raw"),
        NULL, 0U, &one, NULL);
    if (expect_status(status, SL_STATUS_OK) != 0 || !one.found || one.column_count != 8U ||
        one.values[0].kind != SL_POSTGRES_VALUE_DECIMAL ||
        expect_str_equal(one.values[0].value.decimal, "12345678901234567890.1234") != 0 ||
        one.values[1].kind != SL_POSTGRES_VALUE_UUID ||
        expect_str_equal(one.values[1].value.uuid, "00000000-0000-4000-8000-000000000001") != 0 ||
        one.values[2].kind != SL_POSTGRES_VALUE_JSON ||
        expect_str_equal(one.values[2].value.json, "{\"ok\": true}") != 0 ||
        one.values[3].kind != SL_POSTGRES_VALUE_DATE ||
        expect_str_equal(one.values[3].value.date, "2026-05-08") != 0 ||
        one.values[4].kind != SL_POSTGRES_VALUE_TIME ||
        expect_str_equal(one.values[4].value.time, "12:34:56") != 0 ||
        one.values[5].kind != SL_POSTGRES_VALUE_TIMESTAMP ||
        expect_str_equal(one.values[5].value.timestamp, "2026-05-08 12:34:56") != 0 ||
        one.values[6].kind != SL_POSTGRES_VALUE_INSTANT ||
        one.values[7].kind != SL_POSTGRES_VALUE_BYTES || one.values[7].value.bytes.length != 3U ||
        one.values[7].value.bytes.ptr[0] != 0U || one.values[7].value.bytes.ptr[1] != 0x41U ||
        one.values[7].value.bytes.ptr[2] != 0xffU)
    {
        return close_and_return(&connection, 60);
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
        one.values[0].kind != SL_POSTGRES_VALUE_INTEGER || one.values[0].value.integer != 2)
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
    SlPostgresTransaction tx = {0};
    SlDiag diag = {0};
    const char* url = getenv("SLOPPY_POSTGRES_TEST_URL");
    SlStatus status = sl_arena_init(&arena, storage, sizeof(storage));
    SlPostgresPoolOptions options;

    if (!sl_status_is_ok(status)) {
        return 50;
    }
    if (url == NULL || url[0] == '\0') {
        report_live_provider_not_configured("PostgreSQL pool", "SLOPPY_POSTGRES_TEST_URL");
        return 77;
    }
    char copied_url[512];
    size_t copied_url_length = strlen(url);
    if (copied_url_length >= sizeof(copied_url)) {
        return 56;
    }
    for (size_t index = 0U; index <= copied_url_length; index += 1U) {
        copied_url[index] = url[index];
    }
    options = sl_postgres_pool_options_connection_string(sl_str_from_cstr(copied_url), 2U);
    diag = (SlDiag){0};
    status = sl_postgres_pool_open(&arena, &options, &pool, &diag);
    if (expect_status(status, SL_STATUS_OK) != 0) {
        printf("FAIL: live PostgreSQL pool open failed; category: %s. Diagnostics are redacted "
               "and connection strings are not printed.\n",
               classify_live_open_failure(&diag));
        for (size_t index = 0U; index < copied_url_length; index += 1U) {
            copied_url[index] = 'x';
        }
        copied_url[copied_url_length] = '\0';
        return 51;
    }
    for (size_t index = 0U; index < copied_url_length; index += 1U) {
        copied_url[index] = 'x';
    }
    copied_url[copied_url_length] = '\0';
    if (expect_status(sl_postgres_pool_acquire(&arena, &pool, &first, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_postgres_pool_acquire(&arena, &pool, &second, NULL), SL_STATUS_OK) != 0)
    {
        sl_postgres_pool_close(&pool);
        return 52;
    }
    status = sl_postgres_pool_acquire(&arena, &pool, &third, &diag);
    if (expect_status(status, SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        diag.code != SL_DIAG_POSTGRES_POOL_EXHAUSTED)
    {
        sl_postgres_pool_close(&pool);
        return 53;
    }
    if (expect_status(sl_postgres_transaction_begin(&arena, first, &tx, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_postgres_pool_release(&pool, first), SL_STATUS_INVALID_STATE) != 0 ||
        expect_status(sl_postgres_transaction_rollback(&arena, &tx, NULL), SL_STATUS_OK) != 0)
    {
        sl_postgres_pool_close(&pool);
        return 57;
    }
    if (expect_status(sl_postgres_pool_release(&pool, first), SL_STATUS_OK) != 0 ||
        expect_status(sl_postgres_pool_acquire(&arena, &pool, &third, NULL), SL_STATUS_OK) != 0 ||
        third != first)
    {
        sl_postgres_pool_close(&pool);
        return 54;
    }
    if (expect_status(sl_postgres_close(third), SL_STATUS_OK) != 0 ||
        expect_status(sl_postgres_pool_release(&pool, third), SL_STATUS_INVALID_STATE) != 0)
    {
        sl_postgres_pool_close(&pool);
        return 58;
    }
    return expect_status(sl_postgres_pool_close(&pool), SL_STATUS_OK) == 0 &&
                   expect_status(sl_postgres_pool_close(&pool), SL_STATUS_OK) == 0
               ? 0
               : 55;
}

static int run_default_tests(void)
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
    result = test_transaction_failure_keeps_state();
    if (result != 0) {
        return result;
    }
    result = test_pool_lifecycle_without_live_connection();
    if (result != 0) {
        return result;
    }
    return test_invalid_outputs_are_preserved_without_live_connection();
}

static int run_live_tests(void)
{
    int result = test_live_query_exec_and_transactions();
    if (result != 0) {
        return result;
    }
    return test_live_pool();
}

int main(int argc, char** argv)
{
    if (argc > 1 && strcmp(argv[1], "--live") == 0) {
        return run_live_tests();
    }
    return run_default_tests();
}

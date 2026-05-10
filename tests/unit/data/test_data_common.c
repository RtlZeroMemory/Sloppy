#include "sloppy/data.h"

#include <stdbool.h>
#include <stddef.h>

#define TEST_ARENA_SIZE 8192U

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static bool str_contains(SlStr haystack, SlStr needle)
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

static int test_value_constructors_cover_common_kinds(void)
{
    const unsigned char blob[] = {0U, 0x7fU, 0xffU};
    SlDbValue values[2];
    SlDbValue array;

    values[0] = sl_db_value_int32(7);
    values[1] = sl_db_value_text(sl_str_from_cstr("ada"));
    array = sl_db_value_array(values, 2U);

    if (sl_db_value_null().kind != SL_DB_VALUE_NULL || !sl_db_value_bool(true).as.bool_value ||
        sl_db_value_int32(-3).as.int32_value != -3 ||
        sl_db_value_int64(9000000000LL).as.int64_value != 9000000000LL ||
        sl_db_value_float64(1.5).as.float64_value != 1.5 ||
        !sl_str_equal(sl_db_value_decimal(sl_str_from_cstr("12.34")).as.decimal,
                      sl_str_from_cstr("12.34")) ||
        !sl_str_equal(
            sl_db_value_uuid(sl_str_from_cstr("00000000-0000-0000-0000-000000000001")).as.uuid,
            sl_str_from_cstr("00000000-0000-0000-0000-000000000001")) ||
        sl_db_value_bytes(sl_bytes_from_parts(blob, sizeof(blob))).as.bytes.length !=
            sizeof(blob) ||
        !sl_str_equal(sl_db_value_kind_name(SL_DB_VALUE_JSON), sl_str_from_cstr("json")) ||
        array.kind != SL_DB_VALUE_ARRAY || array.as.array.count != 2U ||
        array.as.array.values != values)
    {
        return 1;
    }

    return 0;
}

static int test_value_invalid_views_become_unsupported(void)
{
    SlStr bad_text = {0};
    SlBytes bad_bytes = {0};

    bad_text.length = 3U;
    bad_bytes.length = 2U;

    if (sl_db_value_text(bad_text).kind != SL_DB_VALUE_UNSUPPORTED ||
        sl_db_value_json(bad_text).kind != SL_DB_VALUE_UNSUPPORTED ||
        sl_db_value_bytes(bad_bytes).kind != SL_DB_VALUE_UNSUPPORTED ||
        sl_db_value_array(NULL, 1U).kind != SL_DB_VALUE_UNSUPPORTED)
    {
        return 1;
    }

    return 0;
}

static int test_statement_init_preserves_output_on_failure(void)
{
    SlDbSqlStatement statement = {0};
    SlDbSqlStatement sentinel = {0};
    SlDbParameter params[1];
    SlStr bad_text = {0};
    SlStr bad_label = {0};

    params[0].value = sl_db_value_text(sl_str_from_cstr("ada"));
    sentinel.text = sl_str_from_cstr("unchanged");
    sentinel.parameters = params;
    sentinel.parameter_count = 7U;
    sentinel.placeholder_style = SL_DB_PLACEHOLDER_NAMED;
    sentinel.statement_label = sl_str_from_cstr("unchanged.label");
    statement = sentinel;
    bad_text.length = 4U;
    bad_label.length = 4U;

    if (expect_status(sl_db_sql_statement_init(&statement, sl_str_from_cstr("select ?"), params, 1U,
                                               SL_DB_PLACEHOLDER_QUESTION,
                                               sl_str_from_cstr("users.lookup")),
                      SL_STATUS_OK) != 0 ||
        statement.parameter_count != 1U || statement.parameters != params ||
        statement.placeholder_style != SL_DB_PLACEHOLDER_QUESTION ||
        !sl_str_equal(statement.statement_label, sl_str_from_cstr("users.lookup")))
    {
        return 1;
    }

    statement = sentinel;
    if (expect_status(sl_db_sql_statement_init(&statement, bad_text, params, 1U,
                                               SL_DB_PLACEHOLDER_QUESTION, sl_str_empty()),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        !sl_str_equal(statement.text, sentinel.text) ||
        statement.parameters != sentinel.parameters ||
        statement.parameter_count != sentinel.parameter_count ||
        statement.placeholder_style != sentinel.placeholder_style ||
        !sl_str_equal(statement.statement_label, sentinel.statement_label))
    {
        return 2;
    }

    statement = sentinel;
    if (expect_status(sl_db_sql_statement_init(&statement, sl_str_from_cstr("select ?"), NULL, 1U,
                                               SL_DB_PLACEHOLDER_QUESTION, sl_str_empty()),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        !sl_str_equal(statement.text, sentinel.text) ||
        statement.parameters != sentinel.parameters ||
        statement.parameter_count != sentinel.parameter_count ||
        statement.placeholder_style != sentinel.placeholder_style ||
        !sl_str_equal(statement.statement_label, sentinel.statement_label))
    {
        return 3;
    }

    statement = sentinel;
    if (expect_status(sl_db_sql_statement_init(&statement, sl_str_from_cstr("select ?"), params, 1U,
                                               (SlDbPlaceholderStyle)99,
                                               sl_str_from_cstr("users.lookup")),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        !sl_str_equal(statement.text, sentinel.text) ||
        statement.parameters != sentinel.parameters ||
        statement.parameter_count != sentinel.parameter_count ||
        statement.placeholder_style != sentinel.placeholder_style ||
        !sl_str_equal(statement.statement_label, sentinel.statement_label))
    {
        return 4;
    }

    statement = sentinel;
    if (expect_status(sl_db_sql_statement_init(&statement, sl_str_from_cstr("select ?"), params, 1U,
                                               SL_DB_PLACEHOLDER_QUESTION, bad_label),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        !sl_str_equal(statement.text, sentinel.text) ||
        statement.parameters != sentinel.parameters ||
        statement.parameter_count != sentinel.parameter_count ||
        statement.placeholder_style != sentinel.placeholder_style ||
        !sl_str_equal(statement.statement_label, sentinel.statement_label))
    {
        return 5;
    }

    return 0;
}

static int test_statement_redaction_never_prints_parameter_values(void)
{
    unsigned char storage[TEST_ARENA_SIZE];
    SlArena arena;
    SlDbSqlStatement statement = {0};
    SlDbParameter params[2];
    SlStr redacted = {0};

    params[0].value = sl_db_value_mark_secret(sl_db_value_text(sl_str_from_cstr("super-secret")));
    params[1].value = sl_db_value_json(sl_str_from_cstr("{\"token\":\"hidden\"}"));

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_db_sql_statement_init(
                          &statement, sl_str_from_cstr("select * from users where a=?"), params, 2U,
                          SL_DB_PLACEHOLDER_QUESTION, sl_str_from_cstr("users.bySecret")),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_db_sql_statement_redacted(&arena, &statement, &redacted), SL_STATUS_OK) !=
            0)
    {
        return 1;
    }

    if (!str_contains(redacted, sl_str_from_cstr("select * from users")) ||
        !str_contains(redacted, sl_str_from_cstr("parameters=2 redacted")) ||
        str_contains(redacted, sl_str_from_cstr("super-secret")) ||
        str_contains(redacted, sl_str_from_cstr("hidden")))
    {
        return 2;
    }

    return 0;
}

static int test_result_shapes_are_provider_neutral(void)
{
    SlDbColumnInfo columns[2];
    SlDbValue row_values[2];
    SlDbRow row;
    SlDbRowSet row_set;
    SlDbExecuteResult execute_result;

    columns[0].name = sl_str_from_cstr("id");
    columns[0].value_kind = SL_DB_VALUE_INT64;
    columns[0].provider_type = sl_str_from_cstr("integer");
    columns[0].nullable = false;
    columns[1].name = sl_str_from_cstr("payload");
    columns[1].value_kind = SL_DB_VALUE_JSON;
    columns[1].provider_type = sl_str_from_cstr("json");
    columns[1].nullable = true;
    row_values[0] = sl_db_value_int64(42);
    row_values[1] = sl_db_value_json(sl_str_from_cstr("{\"ok\":true}"));
    row.values = row_values;
    row.value_count = 2U;
    row_set.columns = columns;
    row_set.column_count = 2U;
    row_set.rows = &row;
    row_set.row_count = 1U;
    execute_result.affected_rows_known = true;
    execute_result.affected_rows = 3;
    execute_result.statement_label = sl_str_from_cstr("users.update");

    if (row_set.column_count != 2U || row_set.row_count != 1U ||
        row_set.rows[0].values[0].as.int64_value != 42 ||
        row_set.rows[0].values[1].kind != SL_DB_VALUE_JSON || !execute_result.affected_rows_known ||
        execute_result.affected_rows != 3 ||
        !sl_str_equal(execute_result.statement_label, sl_str_from_cstr("users.update")))
    {
        return 1;
    }

    return 0;
}

static int test_statement_redaction_preserves_output_on_tiny_arena_failure(void)
{
    unsigned char storage[8];
    unsigned char tiny_storage[16];
    SlArena arena;
    SlArena tiny_arena;
    SlDbSqlStatement statement = {0};
    SlDbParameter params[2];
    SlStr redacted = sl_str_from_cstr("unchanged");

    params[0].value = sl_db_value_mark_secret(sl_db_value_text(sl_str_from_cstr("secret")));
    params[1].value = sl_db_value_json(sl_str_from_cstr("{\"token\":true}"));

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&tiny_arena, tiny_storage, sizeof(tiny_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_db_sql_statement_init(
                          &statement, sl_str_from_cstr("select * from users where a=?"), params, 2U,
                          SL_DB_PLACEHOLDER_QUESTION, sl_str_from_cstr("users.bySecret")),
                      SL_STATUS_OK) != 0)
    {
        return 1;
    }

    if (expect_status(sl_db_sql_statement_redacted(&tiny_arena, &statement, &redacted),
                      SL_STATUS_OUT_OF_MEMORY) != 0 ||
        !sl_str_equal(redacted, sl_str_from_cstr("unchanged")))
    {
        return 2;
    }

    return 0;
}

int main(void)
{
    if (test_value_constructors_cover_common_kinds() != 0) {
        return 1;
    }
    if (test_value_invalid_views_become_unsupported() != 0) {
        return 2;
    }
    if (test_statement_init_preserves_output_on_failure() != 0) {
        return 3;
    }
    if (test_statement_redaction_never_prints_parameter_values() != 0) {
        return 4;
    }
    if (test_statement_redaction_preserves_output_on_tiny_arena_failure() != 0) {
        return 5;
    }
    if (test_result_shapes_are_provider_neutral() != 0) {
        return 6;
    }

    return 0;
}

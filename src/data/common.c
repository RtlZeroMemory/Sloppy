/*
 * src/data/common.c
 *
 * Provider-neutral database value and statement helpers. Provider-specific files own
 * driver handles and wire conversions; this file owns the shared Sloppy Db contract.
 */
#include "sloppy/data.h"

#include "sloppy/builder.h"
#include "sloppy/checked_math.h"

static bool sl_db_str_valid(SlStr value)
{
    return value.length == 0U || value.ptr != NULL;
}

static bool sl_db_bytes_valid(SlBytes value)
{
    return value.length == 0U || value.ptr != NULL;
}

SlStr sl_db_value_kind_name(SlDbValueKind kind)
{
    switch (kind) {
    case SL_DB_VALUE_NULL:
        return sl_str_from_cstr("null");
    case SL_DB_VALUE_BOOL:
        return sl_str_from_cstr("boolean");
    case SL_DB_VALUE_INT32:
        return sl_str_from_cstr("int32");
    case SL_DB_VALUE_INT64:
        return sl_str_from_cstr("int64");
    case SL_DB_VALUE_FLOAT64:
        return sl_str_from_cstr("float64");
    case SL_DB_VALUE_DECIMAL:
        return sl_str_from_cstr("decimal");
    case SL_DB_VALUE_TEXT:
        return sl_str_from_cstr("text");
    case SL_DB_VALUE_BYTES:
        return sl_str_from_cstr("bytes");
    case SL_DB_VALUE_UUID:
        return sl_str_from_cstr("uuid");
    case SL_DB_VALUE_DATE:
        return sl_str_from_cstr("date");
    case SL_DB_VALUE_TIME:
        return sl_str_from_cstr("time");
    case SL_DB_VALUE_TIMESTAMP:
        return sl_str_from_cstr("timestamp");
    case SL_DB_VALUE_INSTANT:
        return sl_str_from_cstr("instant");
    case SL_DB_VALUE_JSON:
        return sl_str_from_cstr("json");
    case SL_DB_VALUE_ARRAY:
        return sl_str_from_cstr("array");
    default:
        return sl_str_from_cstr("unsupported");
    }
}

SlDbValue sl_db_value_null(void)
{
    SlDbValue value = {0};

    value.kind = SL_DB_VALUE_NULL;
    return value;
}

SlDbValue sl_db_value_bool(bool input)
{
    SlDbValue value = {0};

    value.kind = SL_DB_VALUE_BOOL;
    value.as.bool_value = input;
    return value;
}

SlDbValue sl_db_value_int32(int32_t input)
{
    SlDbValue value = {0};

    value.kind = SL_DB_VALUE_INT32;
    value.as.int32_value = input;
    return value;
}

SlDbValue sl_db_value_int64(int64_t input)
{
    SlDbValue value = {0};

    value.kind = SL_DB_VALUE_INT64;
    value.as.int64_value = input;
    return value;
}

SlDbValue sl_db_value_float64(double input)
{
    SlDbValue value = {0};

    value.kind = SL_DB_VALUE_FLOAT64;
    value.as.float64_value = input;
    return value;
}

SlDbValue sl_db_value_decimal(SlStr input)
{
    SlDbValue value = {0};

    value.kind = sl_db_str_valid(input) ? SL_DB_VALUE_DECIMAL : SL_DB_VALUE_UNSUPPORTED;
    value.as.decimal = sl_db_str_valid(input) ? input : sl_str_empty();
    return value;
}

SlDbValue sl_db_value_text(SlStr input)
{
    SlDbValue value = {0};

    value.kind = sl_db_str_valid(input) ? SL_DB_VALUE_TEXT : SL_DB_VALUE_UNSUPPORTED;
    value.as.text = sl_db_str_valid(input) ? input : sl_str_empty();
    return value;
}

SlDbValue sl_db_value_bytes(SlBytes input)
{
    SlDbValue value = {0};

    value.kind = sl_db_bytes_valid(input) ? SL_DB_VALUE_BYTES : SL_DB_VALUE_UNSUPPORTED;
    value.as.bytes = sl_db_bytes_valid(input) ? input : sl_bytes_empty();
    return value;
}

SlDbValue sl_db_value_uuid(SlStr input)
{
    SlDbValue value = {0};

    value.kind = sl_db_str_valid(input) ? SL_DB_VALUE_UUID : SL_DB_VALUE_UNSUPPORTED;
    value.as.uuid = sl_db_str_valid(input) ? input : sl_str_empty();
    return value;
}

SlDbValue sl_db_value_date(SlStr input)
{
    SlDbValue value = {0};

    value.kind = sl_db_str_valid(input) ? SL_DB_VALUE_DATE : SL_DB_VALUE_UNSUPPORTED;
    value.as.date = sl_db_str_valid(input) ? input : sl_str_empty();
    return value;
}

SlDbValue sl_db_value_time(SlStr input)
{
    SlDbValue value = {0};

    value.kind = sl_db_str_valid(input) ? SL_DB_VALUE_TIME : SL_DB_VALUE_UNSUPPORTED;
    value.as.time = sl_db_str_valid(input) ? input : sl_str_empty();
    return value;
}

SlDbValue sl_db_value_timestamp(SlStr input)
{
    SlDbValue value = {0};

    value.kind = sl_db_str_valid(input) ? SL_DB_VALUE_TIMESTAMP : SL_DB_VALUE_UNSUPPORTED;
    value.as.timestamp = sl_db_str_valid(input) ? input : sl_str_empty();
    return value;
}

SlDbValue sl_db_value_instant(SlStr input)
{
    SlDbValue value = {0};

    value.kind = sl_db_str_valid(input) ? SL_DB_VALUE_INSTANT : SL_DB_VALUE_UNSUPPORTED;
    value.as.instant = sl_db_str_valid(input) ? input : sl_str_empty();
    return value;
}

SlDbValue sl_db_value_json(SlStr input)
{
    SlDbValue value = {0};

    value.kind = sl_db_str_valid(input) ? SL_DB_VALUE_JSON : SL_DB_VALUE_UNSUPPORTED;
    value.as.json = sl_db_str_valid(input) ? input : sl_str_empty();
    return value;
}

SlDbValue sl_db_value_array(const SlDbValue* values, size_t count)
{
    SlDbValue value = {0};

    value.kind = (count == 0U || values != NULL) ? SL_DB_VALUE_ARRAY : SL_DB_VALUE_UNSUPPORTED;
    value.as.array.values = value.kind == SL_DB_VALUE_ARRAY ? values : NULL;
    value.as.array.count = value.kind == SL_DB_VALUE_ARRAY ? count : 0U;
    return value;
}

SlDbValue sl_db_value_mark_secret(SlDbValue value)
{
    value.is_secret = true;
    return value;
}

SlStatus sl_db_sql_statement_init(SlDbSqlStatement* out, SlStr text,
                                  const SlDbParameter* parameters, size_t parameter_count,
                                  SlDbPlaceholderStyle placeholder_style, SlStr statement_label)
{
    SlDbSqlStatement statement = {0};

    if (out == NULL || text.length == 0U || !sl_db_str_valid(text) ||
        (parameter_count != 0U && parameters == NULL) || !sl_db_str_valid(statement_label) ||
        placeholder_style > SL_DB_PLACEHOLDER_NAMED)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    statement.text = text;
    statement.parameters = parameters;
    statement.parameter_count = parameter_count;
    statement.placeholder_style = placeholder_style;
    statement.statement_label = statement_label;
    *out = statement;
    return sl_status_ok();
}

SlStatus sl_db_sql_statement_redacted(SlArena* arena, const SlDbSqlStatement* statement, SlStr* out)
{
    SlStringBuilder builder = {0};
    size_t max_capacity = 0U;
    SlStatus status;

    if (arena == NULL || statement == NULL || out == NULL || !sl_db_str_valid(statement->text) ||
        (statement->parameter_count != 0U && statement->parameters == NULL))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_checked_add_size(statement->text.length, 64U, &max_capacity);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_string_builder_init_arena(&builder, arena, max_capacity, max_capacity);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_string_builder_append_str(&builder, statement->text);
    if (sl_status_is_ok(status)) {
        status = sl_string_builder_append_cstr(&builder, " [parameters=");
    }
    if (sl_status_is_ok(status)) {
        status = sl_string_builder_append_size(&builder, statement->parameter_count);
    }
    if (sl_status_is_ok(status)) {
        status = sl_string_builder_append_cstr(&builder, " redacted]");
    }
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_string_builder_view_with_nul(&builder, out);
}

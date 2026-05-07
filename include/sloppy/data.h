#ifndef SLOPPY_DATA_H
#define SLOPPY_DATA_H

#include "sloppy/arena.h"
#include "sloppy/bytes.h"
#include "sloppy/data_postgres.h"
#include "sloppy/data_sqlserver.h"
#include "sloppy/data_sqlite.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SlDbValueKind
{
    SL_DB_VALUE_NULL = 0,
    SL_DB_VALUE_BOOL = 1,
    SL_DB_VALUE_INT32 = 2,
    SL_DB_VALUE_INT64 = 3,
    SL_DB_VALUE_FLOAT64 = 4,
    SL_DB_VALUE_DECIMAL = 5,
    SL_DB_VALUE_TEXT = 6,
    SL_DB_VALUE_BYTES = 7,
    SL_DB_VALUE_UUID = 8,
    SL_DB_VALUE_DATE = 9,
    SL_DB_VALUE_TIME = 10,
    SL_DB_VALUE_TIMESTAMP = 11,
    SL_DB_VALUE_INSTANT = 12,
    SL_DB_VALUE_JSON = 13,
    SL_DB_VALUE_ARRAY = 14,
    SL_DB_VALUE_UNSUPPORTED = 255
} SlDbValueKind;

typedef struct SlDbValue SlDbValue;

typedef struct SlDbArray
{
    const SlDbValue* values;
    size_t count;
} SlDbArray;

struct SlDbValue
{
    SlDbValueKind kind;
    bool is_secret;
    union {
        bool bool_value;
        int32_t int32_value;
        int64_t int64_value;
        double float64_value;
        SlStr decimal;
        SlStr text;
        SlBytes bytes;
        SlStr uuid;
        SlStr date;
        SlStr time;
        SlStr timestamp;
        SlStr instant;
        SlStr json;
        SlDbArray array;
    } as;
};

typedef enum SlDbPlaceholderStyle
{
    SL_DB_PLACEHOLDER_QUESTION = 0,
    SL_DB_PLACEHOLDER_POSTGRES_POSITIONAL = 1,
    SL_DB_PLACEHOLDER_NAMED = 2
} SlDbPlaceholderStyle;

typedef struct SlDbParameter
{
    SlStr name;
    SlDbValue value;
} SlDbParameter;

typedef struct SlDbSqlStatement
{
    SlStr text;
    const SlDbParameter* parameters;
    size_t parameter_count;
    SlDbPlaceholderStyle placeholder_style;
    SlStr statement_label;
} SlDbSqlStatement;

typedef struct SlDbColumnInfo
{
    SlStr name;
    SlDbValueKind value_kind;
    SlStr provider_type;
    bool nullable;
} SlDbColumnInfo;

typedef struct SlDbRow
{
    const SlDbValue* values;
    size_t value_count;
} SlDbRow;

typedef struct SlDbRowSet
{
    const SlDbColumnInfo* columns;
    size_t column_count;
    const SlDbRow* rows;
    size_t row_count;
} SlDbRowSet;

typedef struct SlDbExecuteResult
{
    bool affected_rows_known;
    int64_t affected_rows;
    SlStr statement_label;
} SlDbExecuteResult;

typedef enum SlDbIsolationLevel
{
    SL_DB_ISOLATION_DEFAULT = 0,
    SL_DB_ISOLATION_READ_COMMITTED = 1,
    SL_DB_ISOLATION_REPEATABLE_READ = 2,
    SL_DB_ISOLATION_SERIALIZABLE = 3
} SlDbIsolationLevel;

typedef struct SlDbTransactionOptions
{
    SlDbIsolationLevel isolation_level;
    bool read_only;
} SlDbTransactionOptions;

typedef struct SlDbOperationOptions
{
    void* deadline;
    size_t max_rows;
} SlDbOperationOptions;

SlStr sl_db_value_kind_name(SlDbValueKind kind);
SlDbValue sl_db_value_null(void);
SlDbValue sl_db_value_bool(bool value);
SlDbValue sl_db_value_int32(int32_t value);
SlDbValue sl_db_value_int64(int64_t value);
SlDbValue sl_db_value_float64(double value);
SlDbValue sl_db_value_decimal(SlStr value);
SlDbValue sl_db_value_text(SlStr value);
SlDbValue sl_db_value_bytes(SlBytes value);
SlDbValue sl_db_value_uuid(SlStr value);
SlDbValue sl_db_value_date(SlStr value);
SlDbValue sl_db_value_time(SlStr value);
SlDbValue sl_db_value_timestamp(SlStr value);
SlDbValue sl_db_value_instant(SlStr value);
SlDbValue sl_db_value_json(SlStr value);
SlDbValue sl_db_value_array(const SlDbValue* values, size_t count);
SlDbValue sl_db_value_mark_secret(SlDbValue value);

SlStatus sl_db_sql_statement_init(SlDbSqlStatement* out, SlStr text,
                                  const SlDbParameter* parameters, size_t parameter_count,
                                  SlDbPlaceholderStyle placeholder_style, SlStr statement_label);
SlStatus sl_db_sql_statement_redacted(SlArena* arena, const SlDbSqlStatement* statement,
                                      SlStr* out);

#ifdef __cplusplus
}
#endif

#endif

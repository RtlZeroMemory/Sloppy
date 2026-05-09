#ifndef SLOPPY_DATA_SQLSERVER_H
#define SLOPPY_DATA_SQLSERVER_H

#include "sloppy/arena.h"
#include "sloppy/bytes.h"
#include "sloppy/diagnostics.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SL_SQLSERVER_DEFAULT_MAX_ROWS 128U
#define SL_SQLSERVER_DEFAULT_MAX_CONNECTIONS 4U
#define SL_SQLSERVER_MAX_PARAMS 64U
#define SL_SQLSERVER_MAX_POOL_CONNECTIONS 16U
#define SL_SQLSERVER_MAX_DOCTOR_HINTS 4U

typedef enum SlSqlServerAccess
{
    SL_SQLSERVER_ACCESS_READ = 1,
    SL_SQLSERVER_ACCESS_READWRITE = 2
} SlSqlServerAccess;

typedef struct SlSqlServerOpenOptions
{
    SlStr connection_string;
    SlSqlServerAccess access;
} SlSqlServerOpenOptions;

/*
 * Caller-owned ODBC connection wrapper.
 *
 * The provider owns ODBC environment/connection handles stored behind the opaque fields;
 * callers must close them through sl_sqlserver_close. ODBC headers and handle casts stay
 * in the provider implementation, and these handles must never be surfaced to JavaScript.
 * This native C boundary is synchronous and single-thread-owned. The JavaScript provider
 * bridge is separate and enables ODBC asynchronous connection/statement mode before
 * claiming TRUE_ASYNC behavior.
 */
typedef struct SlSqlServerConnection
{
    void* env_handle;
    void* dbc_handle;
    bool open;
    bool transaction_active;
    SlSqlServerAccess access;
} SlSqlServerConnection;

typedef enum SlSqlServerParamKind
{
    SL_SQLSERVER_PARAM_NULL = 0,
    SL_SQLSERVER_PARAM_TEXT = 1,
    SL_SQLSERVER_PARAM_INTEGER = 2,
    SL_SQLSERVER_PARAM_FLOAT = 3,
    SL_SQLSERVER_PARAM_BOOL = 4,
    SL_SQLSERVER_PARAM_BYTES = 5,
    SL_SQLSERVER_PARAM_DECIMAL = 6,
    SL_SQLSERVER_PARAM_UUID = 7,
    SL_SQLSERVER_PARAM_JSON = 8,
    SL_SQLSERVER_PARAM_DATE = 9,
    SL_SQLSERVER_PARAM_TIME = 10,
    SL_SQLSERVER_PARAM_TIMESTAMP = 11,
    SL_SQLSERVER_PARAM_INSTANT = 12,
    SL_SQLSERVER_PARAM_UNSUPPORTED = 255
} SlSqlServerParamKind;

typedef struct SlSqlServerParam
{
    SlSqlServerParamKind kind;
    union {
        SlStr text;
        SlBytes bytes;
        int64_t integer;
        double number;
        bool boolean;
        SlStr decimal;
        SlStr uuid;
        SlStr json;
        SlStr date;
        SlStr time;
        SlStr timestamp;
        SlStr instant;
    } value;
} SlSqlServerParam;

typedef enum SlSqlServerValueKind
{
    SL_SQLSERVER_VALUE_NULL = 0,
    SL_SQLSERVER_VALUE_TEXT = 1,
    SL_SQLSERVER_VALUE_INTEGER = 2,
    SL_SQLSERVER_VALUE_FLOAT = 3,
    SL_SQLSERVER_VALUE_BOOL = 4,
    SL_SQLSERVER_VALUE_BYTES = 5,
    SL_SQLSERVER_VALUE_DECIMAL = 6,
    SL_SQLSERVER_VALUE_UUID = 7,
    SL_SQLSERVER_VALUE_JSON = 8,
    SL_SQLSERVER_VALUE_DATE = 9,
    SL_SQLSERVER_VALUE_TIME = 10,
    SL_SQLSERVER_VALUE_TIMESTAMP = 11,
    SL_SQLSERVER_VALUE_INSTANT = 12
} SlSqlServerValueKind;

typedef struct SlSqlServerValue
{
    SlSqlServerValueKind kind;
    union {
        SlStr text;
        SlBytes bytes;
        int64_t integer;
        double number;
        bool boolean;
        SlStr decimal;
        SlStr uuid;
        SlStr json;
        SlStr date;
        SlStr time;
        SlStr timestamp;
        SlStr instant;
    } value;
} SlSqlServerValue;

typedef struct SlSqlServerRow
{
    SlSqlServerValue* values;
} SlSqlServerRow;

typedef struct SlSqlServerQueryOptions
{
    size_t max_rows;
} SlSqlServerQueryOptions;

typedef struct SlSqlServerResult
{
    size_t column_count;
    SlStr* column_names;
    size_t row_count;
    SlSqlServerRow* rows;
} SlSqlServerResult;

typedef struct SlSqlServerQueryOneResult
{
    bool found;
    size_t column_count;
    SlStr* column_names;
    SlSqlServerValue* values;
} SlSqlServerQueryOneResult;

typedef struct SlSqlServerExecResult
{
    int64_t affected_rows;
    bool affected_rows_known;
} SlSqlServerExecResult;

typedef struct SlSqlServerTransaction
{
    SlSqlServerConnection* connection;
    bool active;
} SlSqlServerTransaction;

typedef struct SlSqlServerPoolOptions
{
    SlStr connection_string;
    SlSqlServerAccess access;
    size_t max_connections;
} SlSqlServerPoolOptions;

/*
 * Tiny caller-owned pool. It has no waiting queue, background threads, health
 * checks, idle pruning, timeout machinery, or thread-safety contract. Exhaustion returns a
 * diagnostic immediately. sl_sqlserver_pool_open copies the connection string into the
 * supplied arena; the pool remains valid only until that arena is reset or destroyed.
 */
typedef struct SlSqlServerPool
{
    SlSqlServerConnection connections[SL_SQLSERVER_MAX_POOL_CONNECTIONS];
    bool idle[SL_SQLSERVER_MAX_POOL_CONNECTIONS];
    size_t open_count;
    size_t max_connections;
    SlStr connection_string;
    SlSqlServerAccess access;
    bool closed;
} SlSqlServerPool;

typedef struct SlSqlServerDoctorResult
{
    bool ok;
    SlStr provider;
    SlStr driver_manager;
    SlStr driver;
    SlStr message;
    SlStr hints[SL_SQLSERVER_MAX_DOCTOR_HINTS];
    size_t hint_count;
} SlSqlServerDoctorResult;

SlSqlServerOpenOptions sl_sqlserver_open_options_connection_string(SlStr connection_string);
SlSqlServerPoolOptions sl_sqlserver_pool_options_connection_string(SlStr connection_string,
                                                                   size_t max_connections);
SlStatus sl_sqlserver_redact_connection_string(SlArena* arena, SlStr connection_string, SlStr* out);
SlStatus sl_sqlserver_extract_driver_name(SlArena* arena, SlStr connection_string, SlStr* out);
SlStatus sl_sqlserver_doctor(SlArena* arena, const SlSqlServerOpenOptions* options,
                             SlSqlServerDoctorResult* out_result, SlDiag* out_diag);
SlStatus sl_sqlserver_open(SlArena* diag_arena, const SlSqlServerOpenOptions* options,
                           SlSqlServerConnection* out_connection, SlDiag* out_diag);
SlStatus sl_sqlserver_close(SlSqlServerConnection* connection);
SlStatus sl_sqlserver_exec(SlArena* arena, SlSqlServerConnection* connection, SlStr sql,
                           const SlSqlServerParam* params, size_t param_count,
                           SlSqlServerExecResult* out_result, SlDiag* out_diag);
SlStatus sl_sqlserver_query(SlArena* arena, SlSqlServerConnection* connection, SlStr sql,
                            const SlSqlServerParam* params, size_t param_count,
                            const SlSqlServerQueryOptions* options, SlSqlServerResult* out_result,
                            SlDiag* out_diag);
SlStatus sl_sqlserver_query_one(SlArena* arena, SlSqlServerConnection* connection, SlStr sql,
                                const SlSqlServerParam* params, size_t param_count,
                                SlSqlServerQueryOneResult* out_result, SlDiag* out_diag);
SlStatus sl_sqlserver_transaction_begin(SlArena* arena, SlSqlServerConnection* connection,
                                        SlSqlServerTransaction* out_tx, SlDiag* out_diag);
SlStatus sl_sqlserver_transaction_commit(SlArena* arena, SlSqlServerTransaction* tx,
                                         SlDiag* out_diag);
SlStatus sl_sqlserver_transaction_rollback(SlArena* arena, SlSqlServerTransaction* tx,
                                           SlDiag* out_diag);
SlStatus sl_sqlserver_transaction_exec(SlArena* arena, SlSqlServerTransaction* tx, SlStr sql,
                                       const SlSqlServerParam* params, size_t param_count,
                                       SlSqlServerExecResult* out_result, SlDiag* out_diag);
SlStatus sl_sqlserver_transaction_query(SlArena* arena, SlSqlServerTransaction* tx, SlStr sql,
                                        const SlSqlServerParam* params, size_t param_count,
                                        const SlSqlServerQueryOptions* options,
                                        SlSqlServerResult* out_result, SlDiag* out_diag);
SlStatus sl_sqlserver_transaction_query_one(SlArena* arena, SlSqlServerTransaction* tx, SlStr sql,
                                            const SlSqlServerParam* params, size_t param_count,
                                            SlSqlServerQueryOneResult* out_result,
                                            SlDiag* out_diag);
SlStatus sl_sqlserver_pool_open(SlArena* arena, const SlSqlServerPoolOptions* options,
                                SlSqlServerPool* out_pool, SlDiag* out_diag);
SlStatus sl_sqlserver_pool_acquire(SlArena* arena, SlSqlServerPool* pool,
                                   SlSqlServerConnection** out_connection, SlDiag* out_diag);
SlStatus sl_sqlserver_pool_release(SlSqlServerPool* pool, SlSqlServerConnection* connection);
SlStatus sl_sqlserver_pool_close(SlSqlServerPool* pool);

#ifdef __cplusplus
}
#endif

#endif

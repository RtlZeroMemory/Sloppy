#ifndef SLOPPY_DATA_POSTGRES_H
#define SLOPPY_DATA_POSTGRES_H

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

#define SL_POSTGRES_DEFAULT_MAX_ROWS 128U
#define SL_POSTGRES_DEFAULT_MAX_CONNECTIONS 4U
#define SL_POSTGRES_MAX_PARAMS 64U
/* Public caller-owned C pool ABI remains a fixed 16-slot struct. */
#define SL_POSTGRES_MAX_POOL_CONNECTIONS 16U
/* Runtime/JS provider pools are out-of-line and may use the higher production cap. */
#define SL_POSTGRES_MAX_RUNTIME_POOL_CONNECTIONS 256U

typedef enum SlPostgresAccess
{
    SL_POSTGRES_ACCESS_READ = 1,
    SL_POSTGRES_ACCESS_READWRITE = 2
} SlPostgresAccess;

typedef struct SlPostgresOpenOptions
{
    SlStr connection_string;
    SlPostgresAccess access;
} SlPostgresOpenOptions;

/*
 * Caller-owned libpq connection wrapper.
 *
 * The provider owns the native PGconn handle stored behind `handle`; callers must close it
 * through sl_postgres_close. The handle is intentionally opaque and must never be surfaced
 * to JavaScript. This provider is synchronous and single-thread-owned.
 */
typedef struct SlPostgresConnection
{
    void* handle;
    bool open;
    bool transaction_active;
    SlPostgresAccess access;
} SlPostgresConnection;

typedef enum SlPostgresParamKind
{
    SL_POSTGRES_PARAM_NULL = 0,
    SL_POSTGRES_PARAM_TEXT = 1,
    SL_POSTGRES_PARAM_INTEGER = 2,
    SL_POSTGRES_PARAM_FLOAT = 3,
    SL_POSTGRES_PARAM_BOOL = 4,
    SL_POSTGRES_PARAM_BYTES = 5,
    SL_POSTGRES_PARAM_DECIMAL = 6,
    SL_POSTGRES_PARAM_UUID = 7,
    SL_POSTGRES_PARAM_JSON = 8,
    SL_POSTGRES_PARAM_DATE = 9,
    SL_POSTGRES_PARAM_TIME = 10,
    SL_POSTGRES_PARAM_TIMESTAMP = 11,
    SL_POSTGRES_PARAM_INSTANT = 12,
    SL_POSTGRES_PARAM_UNSUPPORTED = 255
} SlPostgresParamKind;

typedef struct SlPostgresParam
{
    SlPostgresParamKind kind;
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
} SlPostgresParam;

typedef enum SlPostgresValueKind
{
    SL_POSTGRES_VALUE_NULL = 0,
    SL_POSTGRES_VALUE_TEXT = 1,
    SL_POSTGRES_VALUE_INTEGER = 2,
    SL_POSTGRES_VALUE_FLOAT = 3,
    SL_POSTGRES_VALUE_BOOL = 4,
    SL_POSTGRES_VALUE_BYTES = 5,
    SL_POSTGRES_VALUE_DECIMAL = 6,
    SL_POSTGRES_VALUE_UUID = 7,
    SL_POSTGRES_VALUE_JSON = 8,
    SL_POSTGRES_VALUE_DATE = 9,
    SL_POSTGRES_VALUE_TIME = 10,
    SL_POSTGRES_VALUE_TIMESTAMP = 11,
    SL_POSTGRES_VALUE_INSTANT = 12
} SlPostgresValueKind;

typedef struct SlPostgresValue
{
    SlPostgresValueKind kind;
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
} SlPostgresValue;

typedef struct SlPostgresRow
{
    SlPostgresValue* values;
} SlPostgresRow;

typedef struct SlPostgresQueryOptions
{
    size_t max_rows;
} SlPostgresQueryOptions;

typedef struct SlPostgresResult
{
    size_t column_count;
    SlStr* column_names;
    size_t row_count;
    SlPostgresRow* rows;
} SlPostgresResult;

typedef struct SlPostgresQueryOneResult
{
    bool found;
    size_t column_count;
    SlStr* column_names;
    SlPostgresValue* values;
} SlPostgresQueryOneResult;

typedef struct SlPostgresExecResult
{
    int64_t affected_rows;
    bool affected_rows_known;
} SlPostgresExecResult;

typedef struct SlPostgresTransaction
{
    SlPostgresConnection* connection;
    bool active;
} SlPostgresTransaction;

typedef struct SlPostgresPoolOptions
{
    SlStr connection_string;
    SlPostgresAccess access;
    size_t max_connections;
} SlPostgresPoolOptions;

/*
 * Tiny caller-owned pool. It has no waiting queue, background threads, health
 * checks, idle pruning, or thread-safety. Exhaustion returns a diagnostic immediately.
 * sl_postgres_pool_open copies the connection string into the supplied arena; the pool
 * remains valid only until that arena is reset or destroyed.
 */
typedef struct SlPostgresPool
{
    SlPostgresConnection connections[SL_POSTGRES_MAX_POOL_CONNECTIONS];
    bool idle[SL_POSTGRES_MAX_POOL_CONNECTIONS];
    size_t open_count;
    size_t max_connections;
    SlStr connection_string;
    SlPostgresAccess access;
    bool closed;
} SlPostgresPool;

SlPostgresOpenOptions sl_postgres_open_options_connection_string(SlStr connection_string);
SlPostgresPoolOptions sl_postgres_pool_options_connection_string(SlStr connection_string,
                                                                 size_t max_connections);
SlStatus sl_postgres_redact_connection_string(SlArena* arena, SlStr connection_string, SlStr* out);
SlStatus sl_postgres_doctor(SlArena* arena, const SlPostgresOpenOptions* options, SlDiag* out_diag);
SlStatus sl_postgres_open(SlArena* diag_arena, const SlPostgresOpenOptions* options,
                          SlPostgresConnection* out_connection, SlDiag* out_diag);
SlStatus sl_postgres_close(SlPostgresConnection* connection);
SlStatus sl_postgres_exec(SlArena* arena, SlPostgresConnection* connection, SlStr sql,
                          const SlPostgresParam* params, size_t param_count,
                          SlPostgresExecResult* out_result, SlDiag* out_diag);
SlStatus sl_postgres_exec_batch(SlArena* arena, SlPostgresConnection* connection, SlStr sql,
                                SlPostgresExecResult* out_result, SlDiag* out_diag);
SlStatus sl_postgres_query(SlArena* arena, SlPostgresConnection* connection, SlStr sql,
                           const SlPostgresParam* params, size_t param_count,
                           const SlPostgresQueryOptions* options, SlPostgresResult* out_result,
                           SlDiag* out_diag);
SlStatus sl_postgres_query_one(SlArena* arena, SlPostgresConnection* connection, SlStr sql,
                               const SlPostgresParam* params, size_t param_count,
                               SlPostgresQueryOneResult* out_result, SlDiag* out_diag);
SlStatus sl_postgres_transaction_begin(SlArena* arena, SlPostgresConnection* connection,
                                       SlPostgresTransaction* out_tx, SlDiag* out_diag);
SlStatus sl_postgres_transaction_commit(SlArena* arena, SlPostgresTransaction* tx,
                                        SlDiag* out_diag);
SlStatus sl_postgres_transaction_rollback(SlArena* arena, SlPostgresTransaction* tx,
                                          SlDiag* out_diag);
SlStatus sl_postgres_transaction_exec(SlArena* arena, SlPostgresTransaction* tx, SlStr sql,
                                      const SlPostgresParam* params, size_t param_count,
                                      SlPostgresExecResult* out_result, SlDiag* out_diag);
SlStatus sl_postgres_transaction_exec_batch(SlArena* arena, SlPostgresTransaction* tx, SlStr sql,
                                            SlPostgresExecResult* out_result, SlDiag* out_diag);
SlStatus sl_postgres_transaction_query(SlArena* arena, SlPostgresTransaction* tx, SlStr sql,
                                       const SlPostgresParam* params, size_t param_count,
                                       const SlPostgresQueryOptions* options,
                                       SlPostgresResult* out_result, SlDiag* out_diag);
SlStatus sl_postgres_transaction_query_one(SlArena* arena, SlPostgresTransaction* tx, SlStr sql,
                                           const SlPostgresParam* params, size_t param_count,
                                           SlPostgresQueryOneResult* out_result, SlDiag* out_diag);
SlStatus sl_postgres_pool_open(SlArena* arena, const SlPostgresPoolOptions* options,
                               SlPostgresPool* out_pool, SlDiag* out_diag);
SlStatus sl_postgres_pool_acquire(SlArena* arena, SlPostgresPool* pool,
                                  SlPostgresConnection** out_connection, SlDiag* out_diag);
SlStatus sl_postgres_pool_release(SlPostgresPool* pool, SlPostgresConnection* connection);
SlStatus sl_postgres_pool_close(SlPostgresPool* pool);

#ifdef __cplusplus
}
#endif

#endif

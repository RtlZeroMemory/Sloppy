#ifndef SLOPPY_DATA_SQLITE_H
#define SLOPPY_DATA_SQLITE_H

#include "sloppy/arena.h"
#include "sloppy/diagnostics.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SL_SQLITE_DEFAULT_MAX_ROWS 128U

typedef enum SlSqliteAccess
{
    SL_SQLITE_ACCESS_READ = 1,
    SL_SQLITE_ACCESS_READWRITE = 2
} SlSqliteAccess;

typedef struct SlSqliteOpenOptions
{
    SlStr path;
    SlSqliteAccess access;
} SlSqliteOpenOptions;

/*
 * Caller-owned SQLite connection wrapper.
 *
 * The provider owns the native sqlite3 handle stored behind `handle`; callers must close it
 * through sl_sqlite_close. The handle is intentionally opaque to Sloppy callers and must
 * never be surfaced to JavaScript. This provider is synchronous and single-thread-owned.
 */
typedef struct SlSqliteConnection
{
    void* handle;
    bool open;
    bool transaction_active;
} SlSqliteConnection;

typedef enum SlSqliteParamKind
{
    SL_SQLITE_PARAM_NULL = 0,
    SL_SQLITE_PARAM_TEXT = 1,
    SL_SQLITE_PARAM_INTEGER = 2,
    SL_SQLITE_PARAM_FLOAT = 3,
    SL_SQLITE_PARAM_BOOL = 4,
    SL_SQLITE_PARAM_UNSUPPORTED = 255
} SlSqliteParamKind;

typedef struct SlSqliteParam
{
    SlSqliteParamKind kind;
    union {
        SlStr text;
        int64_t integer;
        double number;
        bool boolean;
    } value;
} SlSqliteParam;

typedef enum SlSqliteValueKind
{
    SL_SQLITE_VALUE_NULL = 0,
    SL_SQLITE_VALUE_TEXT = 1,
    SL_SQLITE_VALUE_INTEGER = 2,
    SL_SQLITE_VALUE_FLOAT = 3
} SlSqliteValueKind;

typedef struct SlSqliteValue
{
    SlSqliteValueKind kind;
    union {
        /* Text values returned by query APIs are copied into the caller arena. */
        SlStr text;
        int64_t integer;
        double number;
    } value;
} SlSqliteValue;

/* Row cell storage is arena-owned and invalid after the associated arena is reset. */
typedef struct SlSqliteRow
{
    SlSqliteValue* values;
} SlSqliteRow;

typedef struct SlSqliteQueryOptions
{
    size_t max_rows;
} SlSqliteQueryOptions;

/*
 * Query results borrow no SQLite statement storage. Column names, rows, and cell values are
 * copied into the caller-provided arena and become invalid when that arena is reset/freed.
 */
typedef struct SlSqliteResult
{
    size_t column_count;
    SlStr* column_names;
    size_t row_count;
    SlSqliteRow* rows;
} SlSqliteResult;

/*
 * queryOne uses the same arena-owned ownership model as SlSqliteResult. Copy values out if
 * they must outlive the arena passed to sl_sqlite_query_one.
 */
typedef struct SlSqliteQueryOneResult
{
    bool found;
    size_t column_count;
    SlStr* column_names;
    SlSqliteValue* values;
} SlSqliteQueryOneResult;

typedef struct SlSqliteExecResult
{
    int changes;
} SlSqliteExecResult;

/*
 * Transaction handles are single-use. Commit or rollback completes the transaction and makes
 * this wrapper unusable; callers must begin a new transaction for more transactional work.
 */
typedef struct SlSqliteTransaction
{
    SlSqliteConnection* connection;
    bool active;
} SlSqliteTransaction;

SlSqliteOpenOptions sl_sqlite_open_options_memory(void);

SlStatus sl_sqlite_open(SlArena* diag_arena, const SlSqliteOpenOptions* options,
                        SlSqliteConnection* out_connection, SlDiag* out_diag);
SlStatus sl_sqlite_close(SlSqliteConnection* connection);

SlStatus sl_sqlite_exec(SlArena* arena, SlSqliteConnection* connection, SlStr sql,
                        const SlSqliteParam* params, size_t param_count,
                        SlSqliteExecResult* out_result, SlDiag* out_diag);
SlStatus sl_sqlite_query(SlArena* arena, SlSqliteConnection* connection, SlStr sql,
                         const SlSqliteParam* params, size_t param_count,
                         const SlSqliteQueryOptions* options, SlSqliteResult* out_result,
                         SlDiag* out_diag);
SlStatus sl_sqlite_query_one(SlArena* arena, SlSqliteConnection* connection, SlStr sql,
                             const SlSqliteParam* params, size_t param_count,
                             SlSqliteQueryOneResult* out_result, SlDiag* out_diag);

SlStatus sl_sqlite_transaction_begin(SlArena* arena, SlSqliteConnection* connection,
                                     SlSqliteTransaction* out_tx, SlDiag* out_diag);
SlStatus sl_sqlite_transaction_commit(SlArena* arena, SlSqliteTransaction* tx, SlDiag* out_diag);
SlStatus sl_sqlite_transaction_rollback(SlArena* arena, SlSqliteTransaction* tx, SlDiag* out_diag);
SlStatus sl_sqlite_transaction_exec(SlArena* arena, SlSqliteTransaction* tx, SlStr sql,
                                    const SlSqliteParam* params, size_t param_count,
                                    SlSqliteExecResult* out_result, SlDiag* out_diag);
SlStatus sl_sqlite_transaction_query(SlArena* arena, SlSqliteTransaction* tx, SlStr sql,
                                     const SlSqliteParam* params, size_t param_count,
                                     const SlSqliteQueryOptions* options,
                                     SlSqliteResult* out_result, SlDiag* out_diag);
SlStatus sl_sqlite_transaction_query_one(SlArena* arena, SlSqliteTransaction* tx, SlStr sql,
                                         const SlSqliteParam* params, size_t param_count,
                                         SlSqliteQueryOneResult* out_result, SlDiag* out_diag);

#ifdef __cplusplus
}
#endif

#endif

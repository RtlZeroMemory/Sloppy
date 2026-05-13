#ifndef SLOPPY_DATA_SQLITE_H
#define SLOPPY_DATA_SQLITE_H

#include "sloppy/arena.h"
#include "sloppy/bytes.h"
#include "sloppy/diagnostics.h"
#include "sloppy/provider_executor.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SL_SQLITE_DEFAULT_MAX_ROWS 128U
#define SL_SQLITE_DEFAULT_CURSOR_BATCH_SIZE 128U

typedef enum SlSqliteAccess
{
    SL_SQLITE_ACCESS_READ = 1,
    /*
     * SQLite does not expose a true write-only connection mode. The JS bridge still
     * records write-only intent on the resource and denies reads before provider work.
     */
    SL_SQLITE_ACCESS_WRITE = 2,
    SL_SQLITE_ACCESS_READWRITE = 3
} SlSqliteAccess;

typedef struct SlSqliteOpenOptions
{
    SlStr path;
    SlSqliteAccess access;
} SlSqliteOpenOptions;

typedef struct SlSqliteProviderConfig
{
    SlStr instance_id;
    SlStr provider_token;
    size_t queue_capacity;
    const SlCapabilityRegistry* capability_registry;
    SlProviderCapabilityCheckFn capability_check;
    void* capability_check_user;
    void* app_owner;
    void* config_binding;
} SlSqliteProviderConfig;

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
    SL_SQLITE_PARAM_BLOB = 5,
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
        SlBytes blob;
    } value;
} SlSqliteParam;

typedef enum SlSqliteValueKind
{
    SL_SQLITE_VALUE_NULL = 0,
    SL_SQLITE_VALUE_TEXT = 1,
    SL_SQLITE_VALUE_INTEGER = 2,
    SL_SQLITE_VALUE_FLOAT = 3,
    SL_SQLITE_VALUE_BLOB = 4
} SlSqliteValueKind;

typedef struct SlSqliteValue
{
    SlSqliteValueKind kind;
    union {
        /* Text values returned by query APIs are copied into the caller arena. */
        SlStr text;
        SlBytes blob;
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

typedef struct SlSqliteQueryOptionsV2
{
    size_t max_rows;
    uint32_t timeout_ms;
} SlSqliteQueryOptionsV2;

typedef struct SlSqliteCursorOptions
{
    size_t batch_size;
    size_t max_rows;
    uint32_t timeout_ms;
} SlSqliteCursorOptions;

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

typedef struct SlSqliteCursor
{
    SlSqliteConnection* connection;
    void* statement;
    SlStr* column_names;
    size_t column_count;
    size_t batch_size;
    size_t max_rows;
    size_t rows_read;
    uint32_t timeout_ms;
    uint64_t timeout_started_ns;
    uint64_t timeout_ns;
    bool timeout_expired;
    bool timeout_installed;
    bool open;
    bool done;
} SlSqliteCursor;

typedef struct SlSqliteCursorNextResult
{
    bool done;
    SlSqliteRow row;
} SlSqliteCursorNextResult;

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
SlSqliteProviderConfig sl_sqlite_provider_config_default(SlStr instance_id, SlStr provider_token);
SlStatus sl_sqlite_provider_executor_config(const SlSqliteProviderConfig* config,
                                            SlProviderExecutorConfig* out_config);

/*
 * Copies SQLite transient text/blob result storage into `arena`.
 *
 * These helpers are the provider boundary adapters for sqlite3_column_text/blob and future
 * statement-owned result storage. They use explicit lengths, make no NUL-termination
 * assumption, and leave `out` unchanged on failure. Returned views are arena-owned and
 * invalid when that arena is reset or its backing storage ends.
 * Zero-length inputs may use NULL storage and are treated as empty views. Zero-length
 * outputs have length zero and may return NULL storage; callers must not assume
 * NUL-termination.
 */
SlStatus sl_sqlite_copy_result_text_to_arena(SlArena* arena, SlStr text, SlStr* out);
SlStatus sl_sqlite_copy_result_blob_to_arena(SlArena* arena, SlBytes blob, SlBytes* out);

/*
 * Copies text/blob parameters into `arena` and writes an operation-owned parameter view.
 *
 * Synchronous SQLite calls may bind borrowed text/blob with SQLITE_TRANSIENT, but
 * async/offloaded provider submission requires operation-owned parameter bytes. These
 * helpers encode that ownership rule. On failure, `out_param` is left unchanged.
 * Zero-length text/blob inputs may use NULL storage; nonzero length requires non-NULL
 * storage.
 */
SlStatus sl_sqlite_param_copy_text_to_arena(SlArena* arena, SlStr text, SlSqliteParam* out_param);
SlStatus sl_sqlite_param_copy_blob_to_arena(SlArena* arena, SlBytes blob, SlSqliteParam* out_param);

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
SlStatus sl_sqlite_query_v2(SlArena* arena, SlSqliteConnection* connection, SlStr sql,
                            const SlSqliteParam* params, size_t param_count,
                            const SlSqliteQueryOptionsV2* options, SlSqliteResult* out_result,
                            SlDiag* out_diag);
SlStatus sl_sqlite_query_one(SlArena* arena, SlSqliteConnection* connection, SlStr sql,
                             const SlSqliteParam* params, size_t param_count,
                             SlSqliteQueryOneResult* out_result, SlDiag* out_diag);
SlStatus sl_sqlite_cursor_open(SlArena* arena, SlSqliteConnection* connection, SlStr sql,
                               const SlSqliteParam* params, size_t param_count,
                               const SlSqliteCursorOptions* options, SlSqliteCursor* out_cursor,
                               SlDiag* out_diag);
SlStatus sl_sqlite_cursor_next(SlArena* arena, SlSqliteCursor* cursor,
                               SlSqliteCursorNextResult* out_result, SlDiag* out_diag);
SlStatus sl_sqlite_cursor_close(SlSqliteCursor* cursor);

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
SlStatus sl_sqlite_transaction_query_v2(SlArena* arena, SlSqliteTransaction* tx, SlStr sql,
                                        const SlSqliteParam* params, size_t param_count,
                                        const SlSqliteQueryOptionsV2* options,
                                        SlSqliteResult* out_result, SlDiag* out_diag);
SlStatus sl_sqlite_transaction_query_one(SlArena* arena, SlSqliteTransaction* tx, SlStr sql,
                                         const SlSqliteParam* params, size_t param_count,
                                         SlSqliteQueryOneResult* out_result, SlDiag* out_diag);
SlStatus sl_sqlite_transaction_cursor_open(SlArena* arena, SlSqliteTransaction* tx, SlStr sql,
                                           const SlSqliteParam* params, size_t param_count,
                                           const SlSqliteCursorOptions* options,
                                           SlSqliteCursor* out_cursor, SlDiag* out_diag);

#ifdef __cplusplus
}
#endif

#endif

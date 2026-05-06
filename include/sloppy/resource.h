#ifndef SLOPPY_RESOURCE_H
#define SLOPPY_RESOURCE_H

#include "sloppy/arena.h"
#include "sloppy/diagnostics.h"
#include "sloppy/status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Resource IDs are the only native handle shape that future JavaScript bridge code may
 * expose. They are plain slot/generation values and never contain native pointers.
 *
 * Slot zero and generation zero are invalid. The table validates kind, generation, and
 * liveness on every lookup; callers must not trust kind information supplied by JS.
 */
typedef struct SlResourceId
{
    uint32_t slot;
    uint32_t generation;
} SlResourceId;

typedef enum SlResourceKind
{
    SL_RESOURCE_KIND_NONE = 0,
    SL_RESOURCE_KIND_SQLITE_CONNECTION = 1,
    SL_RESOURCE_KIND_SQLITE_STATEMENT = 2,
    SL_RESOURCE_KIND_POSTGRES_CONNECTION = 3,
    SL_RESOURCE_KIND_SQLSERVER_CONNECTION = 4,
    SL_RESOURCE_KIND_TEST_RESOURCE = 5,
    SL_RESOURCE_KIND_FS_FILE_HANDLE = 6,
    SL_RESOURCE_KIND_FS_LOCK = 7,
    SL_RESOURCE_KIND_FS_WATCH = 8,
    SL_RESOURCE_KIND_TCP_CONNECTION = 9,
    SL_RESOURCE_KIND_TCP_LISTENER = 10,
    SL_RESOURCE_KIND_LOCAL_CONNECTION = 11,
    SL_RESOURCE_KIND_LOCAL_SERVER = 12,
    SL_RESOURCE_KIND_LIMIT = 13
} SlResourceKind;

#define SL_RESOURCE_KIND_COUNT ((size_t)SL_RESOURCE_KIND_LIMIT)

typedef void (*SlResourceCleanupFn)(void* ptr, void* user);

typedef struct SlResourceEntry
{
    void* ptr;
    SlResourceKind kind;
    SlResourceCleanupFn cleanup;
    void* cleanup_user;
    uint32_t generation;
    bool occupied;
} SlResourceEntry;

typedef struct SlResourceTable
{
    SlResourceEntry* entries;
    size_t capacity;
    bool initialized;
} SlResourceTable;

/*
 * Point-in-time test/debug snapshot of a resource table.
 *
 * A NULL or uninitialized table snapshots as zero values for non-asserting diagnostics.
 * For initialized tables, `live_count` is the sum of `live_by_kind[]`, slot zero
 * (`SL_RESOURCE_KIND_NONE`) is always zero, and each other slot is indexed by the
 * matching `SlResourceKind` value. Counts are current observations, not peaks, and are
 * not atomic.
 */
typedef struct SlResourceTableSnapshot
{
    size_t capacity;
    size_t live_count;
    size_t closed_count;
    size_t leaked_resource_count;
    size_t live_by_kind[SL_RESOURCE_KIND_COUNT];
} SlResourceTableSnapshot;

SlResourceId sl_resource_id_invalid(void);
bool sl_resource_id_is_valid(SlResourceId id);
SlStr sl_resource_kind_name(SlResourceKind kind);

/*
 * Initializes a fixed-capacity table over caller-owned storage.
 *
 * `table` is required and must be zero-initialized before first init. Reinitializing the
 * same table object is rejected, including after dispose, so old IDs cannot be made valid
 * by resetting slot generations. `storage` is required when `capacity` is nonzero.
 * Capacity must fit in the uint32_t slot range used by `SlResourceId`. The table owns
 * entries after insertion, but it owns only the native resource pointer lifecycle through
 * the optional cleanup callback. Storage remains owned by the caller and must outlive the
 * table. The table is not thread-safe.
 */
SlStatus sl_resource_table_init(SlResourceTable* table, SlResourceEntry* storage, size_t capacity);

/*
 * Initializes a fixed-capacity table with entry storage allocated from `arena`.
 *
 * The table does not own the arena. Entries remain valid until the arena resets, resets to
 * a mark before the storage allocation, or its backing buffer ends. Reinitialization and
 * unrepresentable capacities are rejected before arena storage is consumed.
 */
SlStatus sl_resource_table_init_from_arena(SlResourceTable* table, SlArena* arena, size_t capacity);

/*
 * Inserts one live native resource and returns a JS-safe ID.
 *
 * `table`, `ptr`, and `out_id` are required. `kind` must not be NONE. `cleanup` may be
 * NULL for borrowed/test resources. `out_diag` may be NULL. Failed insertion leaves
 * `out_id` untouched and does not run cleanup. When the table is full, returns
 * SL_STATUS_CAPACITY_EXCEEDED and emits `SL_DIAG_RESOURCE_TABLE_EXHAUSTED` when requested.
 */
SlStatus sl_resource_table_insert(SlResourceTable* table, SlResourceKind kind, void* ptr,
                                  SlResourceCleanupFn cleanup, void* cleanup_user,
                                  SlResourceId* out_id, SlDiag* out_diag);

/*
 * Looks up a live resource by ID and expected kind.
 *
 * `out_ptr` is required and is not changed on failure. `out_diag` may be NULL; when
 * provided, it receives a deterministic non-owning diagnostic with no native pointer
 * values. Wrong-kind, stale, invalid, closed, and missing-slot failures all return
 * machine-checkable statuses and diagnostics.
 */
SlStatus sl_resource_table_get(const SlResourceTable* table, SlResourceId id,
                               SlResourceKind expected_kind, void** out_ptr, SlDiag* out_diag);

/*
 * Closes a live resource by ID.
 *
 * Cleanup is invoked at most once. Closing an ID after its slot has already been closed
 * fails as a stale handle because close advances the generation. Passing the current ID for
 * an already-empty slot fails as invalid state only if the generation still matches.
 */
SlStatus sl_resource_table_close(SlResourceTable* table, SlResourceId id, SlDiag* out_diag);
SlStatus sl_resource_table_close_kind(SlResourceTable* table, SlResourceId id,
                                      SlResourceKind expected_kind, SlDiag* out_diag);

bool sl_resource_table_contains(const SlResourceTable* table, SlResourceId id,
                                SlResourceKind expected_kind);
bool sl_resource_table_is_alive(const SlResourceTable* table, SlResourceId id);
size_t sl_resource_table_capacity(const SlResourceTable* table);
size_t sl_resource_table_live_count(const SlResourceTable* table);

/*
 * Returns the current table counters for test/debug assertions.
 *
 * This helper is non-failing and deterministic for invalid inputs; use
 * `sl_resource_table_assert_no_leaks` when invalid tables must fail the check.
 */
SlResourceTableSnapshot sl_resource_table_snapshot(const SlResourceTable* table);

/*
 * Fails unless the table is valid and has no live resources.
 *
 * Invalid tables never pass as empty. On failure, `out_diag` receives a stable diagnostic
 * when provided; diagnostics expose only resource IDs/kinds, never native pointers.
 */
SlStatus sl_resource_table_assert_no_leaks(const SlResourceTable* table, SlDiag* out_diag);

/*
 * Closes all remaining live entries in ascending slot order and clears the table.
 *
 * Cleanup callbacks are void/no-fail. Dispose is deterministic and idempotent for an
 * already-disposed table.
 */
void sl_resource_table_dispose(SlResourceTable* table);

#ifdef __cplusplus
}
#endif

#endif

/*
 * src/core/resource.c
 *
 * Implements Sloppy's fixed-capacity native resource table.
 *
 * The table exists so future JS-native bridges can hand JavaScript plain resource IDs
 * while native code keeps pointer ownership private. Slots are 1-based in public IDs;
 * generation counters advance on close so stale IDs cannot reach reused resources.
 *
 * Safety invariants:
 * - the table uses caller-owned or arena-owned entry storage and never calls malloc;
 * - JS-visible IDs never contain pointers;
 * - lookup validates slot, generation, liveness, and expected kind;
 * - cleanup callbacks are void/no-fail and invoked at most once per inserted resource;
 * - diagnostics never include native pointer values.
 *
 * Tests: tests/unit/core/test_resource.c.
 */
#include "sloppy/resource.h"

#include "sloppy/checked_math.h"

static SlStr sl_resource_literal(const char* ptr, size_t length)
{
    return sl_str_from_parts(ptr, length);
}

static bool sl_resource_kind_is_valid(SlResourceKind kind)
{
    return kind != SL_RESOURCE_KIND_NONE && kind < SL_RESOURCE_KIND_LIMIT;
}

static bool sl_resource_capacity_is_valid(size_t capacity)
{
    return capacity <= (size_t)UINT32_MAX;
}

static SlStatus sl_resource_next_generation(uint32_t current, uint32_t* out)
{
    uint32_t next = 0U;

    if (out == NULL || current == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (current == UINT32_MAX) {
        return sl_status_from_code(SL_STATUS_OVERFLOW);
    }

    next = current + 1U;
    if (next == 0U) {
        return sl_status_from_code(SL_STATUS_OVERFLOW);
    }

    *out = next;
    return sl_status_ok();
}

static void sl_resource_clear_entry(SlResourceEntry* entry)
{
    entry->ptr = NULL;
    entry->kind = SL_RESOURCE_KIND_NONE;
    entry->cleanup = NULL;
    entry->cleanup_user = NULL;
    entry->occupied = false;
}

static void sl_resource_diag(SlDiag* out_diag, SlDiagCode code, SlStr message, SlResourceId id,
                             SlResourceKind expected, SlResourceKind actual, SlStr operation)
{
    if (out_diag == NULL) {
        return;
    }

    *out_diag = (SlDiag){0};
    out_diag->severity = SL_DIAG_SEVERITY_ERROR;
    out_diag->code = code;
    out_diag->message = message;
    out_diag->primary_span = sl_source_span_unknown();
    out_diag->hints[0] = operation;
    out_diag->hints[1] = sl_resource_kind_name(expected);
    out_diag->hints[2] = sl_resource_kind_name(actual);
    out_diag->hint_count = 3U;

    (void)id;
}

static SlStatus sl_resource_validate_table(const SlResourceTable* table)
{
    if (table == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (!table->initialized) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    if (table->entries == NULL && table->capacity != 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    return sl_status_ok();
}

static SlStatus sl_resource_entry_for_id(const SlResourceTable* table, SlResourceId id,
                                         SlResourceEntry** out_entry, SlDiag* out_diag,
                                         SlStr operation)
{
    SlStatus status = sl_resource_validate_table(table);
    size_t index = 0U;

    if (!sl_status_is_ok(status)) {
        sl_resource_diag(out_diag, SL_DIAG_RESOURCE_INVALID_ID,
                         sl_resource_literal("resource table is not initialized",
                                             sizeof("resource table is not initialized") - 1U),
                         id, SL_RESOURCE_KIND_NONE, SL_RESOURCE_KIND_NONE, operation);
        return status;
    }

    if (out_entry == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (!sl_resource_id_is_valid(id)) {
        sl_resource_diag(
            out_diag, SL_DIAG_RESOURCE_INVALID_ID,
            sl_resource_literal("resource id is invalid", sizeof("resource id is invalid") - 1U),
            id, SL_RESOURCE_KIND_NONE, SL_RESOURCE_KIND_NONE, operation);
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if ((size_t)id.slot > table->capacity) {
        sl_resource_diag(out_diag, SL_DIAG_RESOURCE_INVALID_ID,
                         sl_resource_literal("resource slot does not exist",
                                             sizeof("resource slot does not exist") - 1U),
                         id, SL_RESOURCE_KIND_NONE, SL_RESOURCE_KIND_NONE, operation);
        return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
    }

    index = (size_t)id.slot - 1U;
    *out_entry = &table->entries[index];
    return sl_status_ok();
}

SlResourceId sl_resource_id_invalid(void)
{
    SlResourceId id = {0U, 0U};
    return id;
}

bool sl_resource_id_is_valid(SlResourceId id)
{
    return id.slot != 0U && id.generation != 0U;
}

SlStr sl_resource_kind_name(SlResourceKind kind)
{
    switch (kind) {
    case SL_RESOURCE_KIND_NONE:
        return sl_resource_literal("none", sizeof("none") - 1U);
    case SL_RESOURCE_KIND_SQLITE_CONNECTION:
        return sl_resource_literal("sqlite.connection", sizeof("sqlite.connection") - 1U);
    case SL_RESOURCE_KIND_SQLITE_STATEMENT:
        return sl_resource_literal("sqlite.statement", sizeof("sqlite.statement") - 1U);
    case SL_RESOURCE_KIND_POSTGRES_CONNECTION:
        return sl_resource_literal("postgres.connection", sizeof("postgres.connection") - 1U);
    case SL_RESOURCE_KIND_SQLSERVER_CONNECTION:
        return sl_resource_literal("sqlserver.connection", sizeof("sqlserver.connection") - 1U);
    case SL_RESOURCE_KIND_TEST_RESOURCE:
        return sl_resource_literal("test.resource", sizeof("test.resource") - 1U);
    case SL_RESOURCE_KIND_FS_FILE_HANDLE:
        return sl_resource_literal("fs.file_handle", sizeof("fs.file_handle") - 1U);
    case SL_RESOURCE_KIND_FS_LOCK:
        return sl_resource_literal("fs.lock", sizeof("fs.lock") - 1U);
    case SL_RESOURCE_KIND_FS_WATCH:
        return sl_resource_literal("fs.watch", sizeof("fs.watch") - 1U);
    case SL_RESOURCE_KIND_TCP_CONNECTION:
        return sl_resource_literal("tcp.connection", sizeof("tcp.connection") - 1U);
    default:
        return sl_resource_literal("unknown", sizeof("unknown") - 1U);
    }
}

SlStatus sl_resource_table_init(SlResourceTable* table, SlResourceEntry* storage, size_t capacity)
{
    size_t index = 0U;

    if (table == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (table->initialized) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    if (storage == NULL && capacity != 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (!sl_resource_capacity_is_valid(capacity)) {
        return sl_status_from_code(SL_STATUS_OVERFLOW);
    }

    table->entries = storage;
    table->capacity = capacity;
    table->initialized = true;

    for (index = 0U; index < capacity; index += 1U) {
        storage[index] = (SlResourceEntry){0};
        storage[index].generation = 1U;
    }

    return sl_status_ok();
}

SlStatus sl_resource_table_init_from_arena(SlResourceTable* table, SlArena* arena, size_t capacity)
{
    SlStatus status;
    size_t storage_size = 0U;
    void* storage = NULL;

    if (table == NULL || arena == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (table->initialized) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    if (!sl_resource_capacity_is_valid(capacity)) {
        return sl_status_from_code(SL_STATUS_OVERFLOW);
    }

    if (capacity == 0U) {
        return sl_resource_table_init(table, NULL, 0U);
    }

    status = sl_checked_mul_size(capacity, sizeof(SlResourceEntry), &storage_size);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_arena_alloc(arena, storage_size, _Alignof(SlResourceEntry), &storage);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_resource_table_init(table, (SlResourceEntry*)storage, capacity);
}

SlStatus sl_resource_table_insert(SlResourceTable* table, SlResourceKind kind, void* ptr,
                                  SlResourceCleanupFn cleanup, void* cleanup_user,
                                  SlResourceId* out_id, SlDiag* out_diag)
{
    SlStatus status = sl_resource_validate_table(table);
    size_t index = 0U;
    SlStr operation = sl_resource_literal("insert", sizeof("insert") - 1U);

    if (!sl_status_is_ok(status)) {
        sl_resource_diag(out_diag, SL_DIAG_RESOURCE_INVALID_ID,
                         sl_resource_literal("resource table is not initialized",
                                             sizeof("resource table is not initialized") - 1U),
                         sl_resource_id_invalid(), kind, SL_RESOURCE_KIND_NONE, operation);
        return status;
    }

    if (!sl_resource_kind_is_valid(kind) || ptr == NULL || out_id == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    for (index = 0U; index < table->capacity; index += 1U) {
        SlResourceEntry* entry = &table->entries[index];

        if (!entry->occupied) {
            SlResourceId id;

            if (entry->generation == 0U) {
                entry->generation = 1U;
            }

            if (index + 1U > (size_t)UINT32_MAX) {
                return sl_status_from_code(SL_STATUS_OVERFLOW);
            }

            entry->ptr = ptr;
            entry->kind = kind;
            entry->cleanup = cleanup;
            entry->cleanup_user = cleanup_user;
            entry->occupied = true;

            id.slot = (uint32_t)(index + 1U);
            id.generation = entry->generation;
            *out_id = id;
            return sl_status_ok();
        }
    }

    sl_resource_diag(out_diag, SL_DIAG_RESOURCE_TABLE_EXHAUSTED,
                     sl_resource_literal("resource table is exhausted",
                                         sizeof("resource table is exhausted") - 1U),
                     sl_resource_id_invalid(), kind, SL_RESOURCE_KIND_NONE, operation);
    return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
}

SlStatus sl_resource_table_get(const SlResourceTable* table, SlResourceId id,
                               SlResourceKind expected_kind, void** out_ptr, SlDiag* out_diag)
{
    SlResourceEntry* entry = NULL;
    SlStatus status;
    SlStr operation = sl_resource_literal("lookup", sizeof("lookup") - 1U);

    if (out_ptr == NULL || !sl_resource_kind_is_valid(expected_kind)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_resource_entry_for_id(table, id, &entry, out_diag, operation);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (entry == NULL) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    if (entry->generation != id.generation) {
        sl_resource_diag(
            out_diag, SL_DIAG_RESOURCE_STALE_ID,
            sl_resource_literal("resource id is stale", sizeof("resource id is stale") - 1U), id,
            expected_kind, entry->kind, operation);
        return sl_status_from_code(SL_STATUS_STALE_RESOURCE);
    }

    if (!entry->occupied || entry->ptr == NULL) {
        sl_resource_diag(
            out_diag, SL_DIAG_RESOURCE_CLOSED,
            sl_resource_literal("resource is closed", sizeof("resource is closed") - 1U), id,
            expected_kind, entry->kind, operation);
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    if (entry->kind != expected_kind) {
        sl_resource_diag(out_diag, SL_DIAG_RESOURCE_WRONG_KIND,
                         sl_resource_literal("resource kind does not match",
                                             sizeof("resource kind does not match") - 1U),
                         id, expected_kind, entry->kind, operation);
        return sl_status_from_code(SL_STATUS_WRONG_RESOURCE_KIND);
    }

    *out_ptr = entry->ptr;
    return sl_status_ok();
}

SlStatus sl_resource_table_close_kind(SlResourceTable* table, SlResourceId id,
                                      SlResourceKind expected_kind, SlDiag* out_diag)
{
    SlResourceEntry* entry = NULL;
    SlResourceCleanupFn cleanup = NULL;
    void* ptr = NULL;
    void* cleanup_user = NULL;
    uint32_t next_generation = 0U;
    SlStatus status;
    SlStr operation = sl_resource_literal("close", sizeof("close") - 1U);

    if (!sl_resource_kind_is_valid(expected_kind)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_resource_entry_for_id(table, id, &entry, out_diag, operation);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (entry == NULL) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    if (entry->generation != id.generation) {
        sl_resource_diag(
            out_diag, SL_DIAG_RESOURCE_STALE_ID,
            sl_resource_literal("resource id is stale", sizeof("resource id is stale") - 1U), id,
            SL_RESOURCE_KIND_NONE, entry->kind, operation);
        return sl_status_from_code(SL_STATUS_STALE_RESOURCE);
    }

    if (!entry->occupied || entry->ptr == NULL) {
        sl_resource_diag(out_diag, SL_DIAG_RESOURCE_CLOSED,
                         sl_resource_literal("resource is already closed",
                                             sizeof("resource is already closed") - 1U),
                         id, SL_RESOURCE_KIND_NONE, entry->kind, operation);
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    if (entry->kind != expected_kind) {
        sl_resource_diag(out_diag, SL_DIAG_RESOURCE_WRONG_KIND,
                         sl_resource_literal("resource kind does not match",
                                             sizeof("resource kind does not match") - 1U),
                         id, expected_kind, entry->kind, operation);
        return sl_status_from_code(SL_STATUS_WRONG_RESOURCE_KIND);
    }

    status = sl_resource_next_generation(entry->generation, &next_generation);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    cleanup = entry->cleanup;
    ptr = entry->ptr;
    cleanup_user = entry->cleanup_user;
    sl_resource_clear_entry(entry);
    entry->generation = next_generation;

    if (cleanup != NULL) {
        cleanup(ptr, cleanup_user);
    }

    return sl_status_ok();
}

SlStatus sl_resource_table_close(SlResourceTable* table, SlResourceId id, SlDiag* out_diag)
{
    SlResourceEntry* entry = NULL;
    SlStatus status;

    status = sl_resource_entry_for_id(table, id, &entry, out_diag,
                                      sl_resource_literal("close", sizeof("close") - 1U));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (entry == NULL) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    if (!sl_resource_kind_is_valid(entry->kind)) {
        return sl_resource_table_close_kind(table, id, SL_RESOURCE_KIND_TEST_RESOURCE, out_diag);
    }

    return sl_resource_table_close_kind(table, id, entry->kind, out_diag);
}

bool sl_resource_table_contains(const SlResourceTable* table, SlResourceId id,
                                SlResourceKind expected_kind)
{
    void* ptr = NULL;
    return sl_status_is_ok(sl_resource_table_get(table, id, expected_kind, &ptr, NULL));
}

bool sl_resource_table_is_alive(const SlResourceTable* table, SlResourceId id)
{
    SlResourceEntry* entry = NULL;
    SlStatus status;

    if (!sl_resource_id_is_valid(id)) {
        return false;
    }

    status = sl_resource_entry_for_id(table, id, &entry, NULL,
                                      sl_resource_literal("alive", sizeof("alive") - 1U));
    if (!sl_status_is_ok(status)) {
        return false;
    }
    if (entry == NULL) {
        return false;
    }

    return entry->occupied && entry->ptr != NULL && entry->generation == id.generation;
}

size_t sl_resource_table_capacity(const SlResourceTable* table)
{
    return table == NULL ? 0U : table->capacity;
}

size_t sl_resource_table_live_count(const SlResourceTable* table)
{
    size_t count = 0U;
    size_t index = 0U;

    if (table == NULL || table->entries == NULL) {
        return 0U;
    }

    for (index = 0U; index < table->capacity; index += 1U) {
        if (table->entries[index].occupied && table->entries[index].ptr != NULL) {
            count += 1U;
        }
    }

    return count;
}

SlResourceTableSnapshot sl_resource_table_snapshot(const SlResourceTable* table)
{
    SlResourceTableSnapshot snapshot = {0};
    size_t index = 0U;

    if (table == NULL || table->entries == NULL) {
        return snapshot;
    }

    snapshot.capacity = table->capacity;
    for (index = 0U; index < table->capacity; index += 1U) {
        const SlResourceEntry* entry = &table->entries[index];

        if (entry->occupied && entry->ptr != NULL) {
            snapshot.live_count += 1U;
            if ((size_t)entry->kind < SL_RESOURCE_KIND_COUNT) {
                snapshot.live_by_kind[(size_t)entry->kind] += 1U;
            }
        }
        else if (entry->generation > 1U) {
            snapshot.closed_count += 1U;
        }
    }
    snapshot.leaked_resource_count = snapshot.live_count;
    return snapshot;
}

SlStatus sl_resource_table_assert_no_leaks(const SlResourceTable* table, SlDiag* out_diag)
{
    SlStatus table_status = sl_resource_validate_table(table);
    SlResourceTableSnapshot snapshot = {0};

    if (out_diag != NULL) {
        *out_diag = (SlDiag){0};
    }
    if (!sl_status_is_ok(table_status)) {
        sl_resource_diag(out_diag, SL_DIAG_RESOURCE_INVALID_ID,
                         sl_resource_literal("resource table is not initialized",
                                             sizeof("resource table is not initialized") - 1U),
                         sl_resource_id_invalid(), SL_RESOURCE_KIND_NONE, SL_RESOURCE_KIND_NONE,
                         sl_resource_literal("leak check", sizeof("leak check") - 1U));
        return table_status;
    }
    snapshot = sl_resource_table_snapshot(table);
    if (snapshot.live_count == 0U) {
        return sl_status_ok();
    }
    if (out_diag != NULL) {
        out_diag->severity = SL_DIAG_SEVERITY_ERROR;
        out_diag->code = SL_DIAG_LIFECYCLE_LEAK_DETECTED;
        out_diag->message = sl_resource_literal("resource table has live resources",
                                                sizeof("resource table has live resources") - 1U);
        out_diag->hints[0] = sl_resource_literal("close or transfer resources before leak check",
                                                 sizeof("close or transfer resources before leak "
                                                        "check") -
                                                     1U);
        out_diag->hint_count = 1U;
    }
    return sl_status_from_code(SL_STATUS_INVALID_STATE);
}

void sl_resource_table_dispose(SlResourceTable* table)
{
    size_t index = 0U;

    if (table == NULL || !table->initialized || table->entries == NULL) {
        return;
    }

    for (index = 0U; index < table->capacity; index += 1U) {
        SlResourceEntry* entry = &table->entries[index];

        if (entry->occupied && entry->ptr != NULL) {
            SlResourceCleanupFn cleanup = entry->cleanup;
            void* ptr = entry->ptr;
            void* cleanup_user = entry->cleanup_user;
            uint32_t next_generation = entry->generation;
            SlStatus status = sl_resource_next_generation(entry->generation, &next_generation);

            sl_resource_clear_entry(entry);
            if (sl_status_is_ok(status)) {
                entry->generation = next_generation;
            }

            if (cleanup != NULL) {
                cleanup(ptr, cleanup_user);
            }
        }
    }

    table->entries = NULL;
    table->capacity = 0U;
}

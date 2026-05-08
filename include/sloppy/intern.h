#ifndef SLOPPY_INTERN_H
#define SLOPPY_INTERN_H

#include "sloppy/arena.h"
#include "sloppy/container.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SlSymbol
{
    size_t index;
    unsigned int generation;
} SlSymbol;

typedef struct SlInternedString
{
    SlSymbol symbol;
    SlStr text;
    uint64_t hash;
} SlInternedString;

typedef struct SlInternEntry
{
    SlOwnedStr text;
    uint64_t hash;
} SlInternEntry;

/*
 * SlInternTable is a bounded app/static-lifetime string table.
 *
 * The caller owns the table object and the arena. Table metadata and string bytes are
 * arena-owned and remain valid until that arena is reset/disposed. The table has no
 * process-global pool and must be used only for stable metadata such as route, module,
 * capability, provider, Plan, HTTP token, or diagnostic-code names. Do not intern request
 * bodies, secrets, connection strings, arbitrary user payloads, or transient diagnostics.
 */
typedef struct SlInternTable
{
    SlArena* arena;
    SlInternEntry* entries;
    SlArenaHashIndex index;
    size_t capacity;
    size_t count;
    unsigned int generation;
    bool initialized;
} SlInternTable;

SlSymbol sl_symbol_invalid(void);
bool sl_symbol_is_valid(SlSymbol symbol);
bool sl_symbol_equal(SlSymbol left, SlSymbol right);

/*
 * Initializes a bounded table over arena-owned metadata storage.
 *
 * `table` must be zero-initialized before first init. Reinitializing an active table is
 * rejected; call sl_intern_table_dispose() before reusing the same table object with new
 * storage. `capacity` and `bucket_count` must be nonzero. Outputs already written by
 * failed calls to intern/find/get remain unchanged by those calls; init itself updates
 * `table` only on success.
 */
SlStatus sl_intern_table_init(SlInternTable* table, SlArena* arena, size_t capacity,
                              size_t bucket_count);

/*
 * Disposes the table object without freeing arena-owned bytes.
 *
 * Symbols from the previous generation become stale for this table instance. The caller
 * remains responsible for resetting or disposing the arena at the app/package boundary.
 */
void sl_intern_table_dispose(SlInternTable* table);

size_t sl_intern_table_count(const SlInternTable* table);
size_t sl_intern_table_capacity(const SlInternTable* table);
SlStatus sl_intern_table_intern(SlInternTable* table, SlStr text, SlInternedString* out);
SlStatus sl_intern_table_find(const SlInternTable* table, SlStr text, SlInternedString* out);
SlStatus sl_intern_table_get(const SlInternTable* table, SlSymbol symbol, SlInternedString* out);

#ifdef __cplusplus
}
#endif

#endif

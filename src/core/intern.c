/*
 * src/core/intern.c
 *
 * Implements Sloppy's bounded string interning primitive for app/static metadata.
 *
 * The table has caller-owned lifetime, arena-owned bytes, and no process-global state.
 * Hash buckets accelerate lookup, but byte equality is the correctness rule for duplicate
 * and collision handling.
 *
 * Tests: tests/unit/core/test_intern.c.
 */
#include "sloppy/intern.h"

typedef struct SlInternFindContext
{
    const SlInternTable* table;
    SlStr text;
    uint64_t hash;
} SlInternFindContext;

static SlStatus sl_intern_table_validate(const SlInternTable* table)
{
    if (table == NULL || !table->initialized || table->entries == NULL ||
        table->index.buckets == NULL || table->index.next_indices == NULL ||
        table->index.bucket_count == 0U)
    {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    return sl_status_ok();
}

static SlInternedString sl_interned_from_entry(const SlInternTable* table, size_t index)
{
    SlInternEntry* entry = &table->entries[index - 1U];
    SlInternedString interned;

    interned.symbol.index = index;
    interned.symbol.generation = table->generation;
    interned.text = sl_owned_str_as_view(entry->text);
    interned.hash = entry->hash;
    return interned;
}

static bool sl_intern_table_entry_matches(size_t entry_index, void* user)
{
    SlInternFindContext* context = (SlInternFindContext*)user;
    const SlInternEntry* entry = NULL;

    if (context == NULL || context->table == NULL || entry_index == 0U ||
        entry_index > context->table->count)
    {
        return false;
    }

    entry = &context->table->entries[entry_index - 1U];
    return entry->hash == context->hash &&
           sl_str_equal(sl_owned_str_as_view(entry->text), context->text);
}

SlSymbol sl_symbol_invalid(void)
{
    SlSymbol symbol = {0U, 0U};
    return symbol;
}

bool sl_symbol_is_valid(SlSymbol symbol)
{
    return symbol.index != 0U && symbol.generation != 0U;
}

bool sl_symbol_equal(SlSymbol left, SlSymbol right)
{
    return left.index == right.index && left.generation == right.generation;
}

SlStatus sl_intern_table_init(SlInternTable* table, SlArena* arena, size_t capacity,
                              size_t bucket_count)
{
    SlInternTable result = {0U};
    SlSlice entries = {0};
    unsigned int next_generation = 1U;
    SlStatus status;

    if (table == NULL || arena == NULL || capacity == 0U || bucket_count == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (table->initialized) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    if (table->generation != 0U) {
        next_generation = table->generation + 1U;
        if (next_generation == 0U) {
            next_generation = 1U;
        }
    }

    status = sl_arena_array_alloc(arena, capacity, sizeof(SlInternEntry), _Alignof(SlInternEntry),
                                  &entries);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_arena_hash_index_init(&result.index, arena, capacity, bucket_count);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    result.arena = arena;
    result.entries = (SlInternEntry*)entries.ptr;
    result.capacity = capacity;
    result.generation = next_generation;
    result.initialized = true;
    *table = result;
    return sl_status_ok();
}

void sl_intern_table_dispose(SlInternTable* table)
{
    unsigned int next_generation = 1U;

    if (table == NULL) {
        return;
    }

    next_generation = table->generation + 1U;
    if (next_generation == 0U) {
        next_generation = 1U;
    }

    table->arena = NULL;
    table->entries = NULL;
    table->index = (SlArenaHashIndex){0};
    table->capacity = 0U;
    table->count = 0U;
    table->generation = next_generation;
    table->initialized = false;
}

size_t sl_intern_table_count(const SlInternTable* table)
{
    return table == NULL || !table->initialized ? 0U : table->count;
}

size_t sl_intern_table_capacity(const SlInternTable* table)
{
    return table == NULL || !table->initialized ? 0U : table->capacity;
}

static SlStatus sl_intern_table_find_hashed(const SlInternTable* table, SlStr text,
                                            SlInternedString* out, uint64_t* out_hash)
{
    uint64_t hash = 0U;
    size_t entry_index = 0U;
    SlStatus status;
    SlInternFindContext context;

    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_intern_table_validate(table);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_str_hash(text, &hash);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (out_hash != NULL) {
        *out_hash = hash;
    }

    context.table = table;
    context.text = text;
    context.hash = hash;
    status = sl_arena_hash_index_find(&table->index, hash, sl_intern_table_entry_matches, &context,
                                      &entry_index);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    *out = sl_interned_from_entry(table, entry_index);
    return sl_status_ok();
}

SlStatus sl_intern_table_find(const SlInternTable* table, SlStr text, SlInternedString* out)
{
    return sl_intern_table_find_hashed(table, text, out, NULL);
}

SlStatus sl_intern_table_intern(SlInternTable* table, SlStr text, SlInternedString* out)
{
    SlInternedString existing;
    SlOwnedStr copied = {0U};
    uint64_t hash = 0U;
    size_t entry_index = 0U;
    SlStatus status;

    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_intern_table_find_hashed(table, text, &existing, &hash);
    if (sl_status_is_ok(status)) {
        *out = existing;
        return sl_status_ok();
    }

    if (sl_status_code(status) != SL_STATUS_OUT_OF_RANGE) {
        return status;
    }

    if (table->count >= table->capacity) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }

    status = sl_str_copy_to_arena(table->arena, text, &copied);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    entry_index = table->count + 1U;
    table->entries[entry_index - 1U].text = copied;
    table->entries[entry_index - 1U].hash = hash;
    status = sl_arena_hash_index_insert(&table->index, hash, entry_index);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    table->count += 1U;

    *out = sl_interned_from_entry(table, entry_index);
    return sl_status_ok();
}

SlStatus sl_intern_table_get(const SlInternTable* table, SlSymbol symbol, SlInternedString* out)
{
    SlStatus status;

    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_intern_table_validate(table);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (!sl_symbol_is_valid(symbol) || symbol.index > table->count) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (symbol.generation != table->generation) {
        return sl_status_from_code(SL_STATUS_STALE_RESOURCE);
    }

    *out = sl_interned_from_entry(table, symbol.index);
    return sl_status_ok();
}

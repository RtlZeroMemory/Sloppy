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

#include "sloppy/checked_math.h"

static SlStatus sl_intern_table_validate(const SlInternTable* table)
{
    if (table == NULL || !table->initialized || table->entries == NULL || table->buckets == NULL ||
        table->bucket_count == 0U)
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
    void* entries = NULL;
    void* buckets = NULL;
    size_t entry_bytes = 0U;
    size_t bucket_bytes = 0U;
    size_t index = 0U;
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

    status = sl_checked_mul_size(capacity, sizeof(SlInternEntry), &entry_bytes);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_checked_mul_size(bucket_count, sizeof(size_t), &bucket_bytes);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_arena_alloc(arena, entry_bytes, _Alignof(SlInternEntry), &entries);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_arena_alloc(arena, bucket_bytes, _Alignof(size_t), &buckets);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    for (index = 0U; index < capacity; index += 1U) {
        ((SlInternEntry*)entries)[index] = (SlInternEntry){0U};
    }
    for (index = 0U; index < bucket_count; index += 1U) {
        ((size_t*)buckets)[index] = 0U;
    }

    result.arena = arena;
    result.entries = (SlInternEntry*)entries;
    result.buckets = (size_t*)buckets;
    result.capacity = capacity;
    result.bucket_count = bucket_count;
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
    table->buckets = NULL;
    table->capacity = 0U;
    table->bucket_count = 0U;
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

SlStatus sl_intern_table_find(const SlInternTable* table, SlStr text, SlInternedString* out)
{
    uint64_t hash = 0U;
    size_t bucket_index = 0U;
    size_t entry_index = 0U;
    SlStatus status;

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

    bucket_index = (size_t)(hash % (uint64_t)table->bucket_count);
    entry_index = table->buckets[bucket_index];
    while (entry_index != 0U) {
        SlInternEntry* entry = &table->entries[entry_index - 1U];

        if (entry->hash == hash && sl_str_equal(sl_owned_str_as_view(entry->text), text)) {
            *out = sl_interned_from_entry(table, entry_index);
            return sl_status_ok();
        }

        entry_index = entry->next_index;
    }

    return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
}

SlStatus sl_intern_table_intern(SlInternTable* table, SlStr text, SlInternedString* out)
{
    SlInternedString existing;
    SlOwnedStr copied = {0U};
    uint64_t hash = 0U;
    size_t bucket_index = 0U;
    size_t entry_index = 0U;
    SlStatus status;

    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_intern_table_find(table, text, &existing);
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

    status = sl_str_hash(text, &hash);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_str_copy_to_arena(table->arena, text, &copied);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    bucket_index = (size_t)(hash % (uint64_t)table->bucket_count);
    entry_index = table->count + 1U;
    table->entries[entry_index - 1U].text = copied;
    table->entries[entry_index - 1U].hash = hash;
    table->entries[entry_index - 1U].next_index = table->buckets[bucket_index];
    table->buckets[bucket_index] = entry_index;
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

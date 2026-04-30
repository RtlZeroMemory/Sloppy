#include "sloppy/intern.h"

#include <stdbool.h>
#include <stddef.h>

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static int test_duplicate_interning_returns_stable_symbol(void)
{
    unsigned char storage[256];
    char first_text[] = {'r', 'o', 'u', 't', 'e'};
    char duplicate_text[] = {'r', 'o', 'u', 't', 'e'};
    SlArena arena;
    SlInternTable table = {0U};
    SlInternedString first;
    SlInternedString duplicate;

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_intern_table_init(&table, &arena, 4U, 4U), SL_STATUS_OK) != 0)
    {
        return 1;
    }

    if (expect_status(sl_intern_table_intern(&table, sl_str_from_parts(first_text, 5U), &first),
                      SL_STATUS_OK) != 0 ||
        expect_status(
            sl_intern_table_intern(&table, sl_str_from_parts(duplicate_text, 5U), &duplicate),
            SL_STATUS_OK) != 0)
    {
        return 2;
    }

    if (!sl_symbol_equal(first.symbol, duplicate.symbol) || first.text.ptr != duplicate.text.ptr ||
        sl_intern_table_count(&table) != 1U)
    {
        return 3;
    }

    if (expect_status(sl_intern_table_init(&table, &arena, 4U, 4U), SL_STATUS_INVALID_STATE) != 0 ||
        sl_intern_table_count(&table) != 1U)
    {
        return 4;
    }

    return 0;
}

static int test_bucket_collision_uses_byte_equality(void)
{
    unsigned char storage[256];
    SlArena arena;
    SlInternTable table = {0U};
    SlInternedString alpha;
    SlInternedString beta;
    SlInternedString found;

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_intern_table_init(&table, &arena, 4U, 1U), SL_STATUS_OK) != 0)
    {
        return 10;
    }

    if (expect_status(sl_intern_table_intern(&table, sl_str_from_cstr("alpha"), &alpha),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_intern_table_intern(&table, sl_str_from_cstr("beta"), &beta),
                      SL_STATUS_OK) != 0)
    {
        return 11;
    }

    if (sl_symbol_equal(alpha.symbol, beta.symbol) || sl_intern_table_count(&table) != 2U) {
        return 12;
    }

    if (expect_status(sl_intern_table_find(&table, sl_str_from_cstr("alpha"), &found),
                      SL_STATUS_OK) != 0 ||
        !sl_symbol_equal(found.symbol, alpha.symbol))
    {
        return 13;
    }

    if (expect_status(sl_intern_table_find(&table, sl_str_from_cstr("beta"), &found),
                      SL_STATUS_OK) != 0 ||
        !sl_symbol_equal(found.symbol, beta.symbol))
    {
        return 14;
    }

    return 0;
}

static int test_capacity_and_failure_outputs(void)
{
    unsigned char storage[256];
    SlArena arena;
    SlInternTable table = {0U};
    SlInternedString first;
    SlInternedString sentinel;

    sentinel.symbol.index = 99U;
    sentinel.symbol.generation = 99U;
    sentinel.text = sl_str_from_cstr("sentinel");
    sentinel.hash = 99U;

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_intern_table_init(&table, &arena, 1U, 1U), SL_STATUS_OK) != 0)
    {
        return 20;
    }

    if (expect_status(sl_intern_table_intern(&table, sl_str_from_cstr("one"), &first),
                      SL_STATUS_OK) != 0)
    {
        return 21;
    }

    if (expect_status(sl_intern_table_intern(&table, sl_str_from_cstr("two"), &sentinel),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        sentinel.symbol.index != 99U || sentinel.hash != 99U || sl_intern_table_count(&table) != 1U)
    {
        return 22;
    }

    if (expect_status(sl_intern_table_find(&table, sl_str_from_cstr("missing"), &sentinel),
                      SL_STATUS_OUT_OF_RANGE) != 0 ||
        sentinel.symbol.index != 99U || sentinel.hash != 99U)
    {
        return 23;
    }

    if (expect_status(sl_intern_table_intern(&table, sl_str_from_parts(NULL, 3U), &sentinel),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        sentinel.symbol.index != 99U || sentinel.hash != 99U)
    {
        return 24;
    }

    return 0;
}

static int test_symbol_get_and_stale_generation(void)
{
    unsigned char storage[512];
    SlArena arena;
    SlInternTable table = {0U};
    SlInternedString first;
    SlInternedString second;
    SlInternedString fetched;
    SlSymbol old_symbol;

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_intern_table_init(&table, &arena, 2U, 2U), SL_STATUS_OK) != 0 ||
        expect_status(sl_intern_table_intern(&table, sl_str_from_cstr("first"), &first),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_intern_table_get(&table, first.symbol, &fetched), SL_STATUS_OK) != 0 ||
        !sl_symbol_equal(fetched.symbol, first.symbol) || !sl_str_equal(fetched.text, first.text))
    {
        return 30;
    }

    old_symbol = first.symbol;
    sl_intern_table_dispose(&table);
    if (expect_status(sl_intern_table_get(&table, old_symbol, &fetched), SL_STATUS_INVALID_STATE) !=
        0)
    {
        return 31;
    }

    if (expect_status(sl_intern_table_init(&table, &arena, 2U, 2U), SL_STATUS_OK) != 0 ||
        expect_status(sl_intern_table_intern(&table, sl_str_from_cstr("second"), &second),
                      SL_STATUS_OK) != 0)
    {
        return 32;
    }

    if (expect_status(sl_intern_table_get(&table, old_symbol, NULL), SL_STATUS_INVALID_ARGUMENT) !=
        0)
    {
        return 33;
    }

    if (expect_status(sl_intern_table_get(&table, old_symbol, &fetched),
                      SL_STATUS_STALE_RESOURCE) != 0)
    {
        return 34;
    }

    if (sl_symbol_is_valid(sl_symbol_invalid())) {
        return 35;
    }

    (void)second;
    return 0;
}

int main(void)
{
    int result = test_duplicate_interning_returns_stable_symbol();
    if (result != 0) {
        return result;
    }

    result = test_bucket_collision_uses_byte_equality();
    if (result != 0) {
        return result;
    }

    result = test_capacity_and_failure_outputs();
    if (result != 0) {
        return result;
    }

    return test_symbol_get_and_stale_generation();
}

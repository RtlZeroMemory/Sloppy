#include "sloppy/string.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static int test_views_and_helpers(void)
{
    const char embedded[] = {'a', '\0', 'b'};
    const char same_embedded[] = {'a', '\0', 'b'};
    const char different[] = {'a', '\0', 'c'};
    SlStr empty = sl_str_empty();
    SlStr from_null_cstr = sl_str_from_cstr(NULL);
    SlStr from_cstr = sl_str_from_cstr("hello");
    SlStr embedded_str = sl_str_from_parts(embedded, sizeof(embedded));
    SlStr same_embedded_str = sl_str_from_parts(same_embedded, sizeof(same_embedded));
    SlStr different_str = sl_str_from_parts(different, sizeof(different));
    SlStr prefix = sl_str_from_parts(embedded, 2U);
    SlStr suffix = sl_str_from_parts(embedded + 1U, 2U);
    SlOwnedStr owned = {(char*)different, sizeof(different)};

    if (expect_true(sl_str_is_empty(empty)) != 0 ||
        expect_true(sl_str_equal(empty, from_null_cstr)) != 0 ||
        expect_true(from_cstr.length == 5U) != 0)
    {
        return 1;
    }

    if (expect_true(sl_str_equal(embedded_str, same_embedded_str)) != 0 ||
        expect_true(!sl_str_equal(embedded_str, different_str)) != 0 ||
        expect_true(!sl_str_equal(sl_str_from_parts(NULL, 3U), sl_str_from_parts(NULL, 3U))) != 0)
    {
        return 2;
    }

    if (expect_true(sl_str_starts_with(embedded_str, prefix)) != 0 ||
        expect_true(sl_str_starts_with(embedded_str, empty)) != 0 ||
        expect_true(sl_str_ends_with(embedded_str, suffix)) != 0)
    {
        return 3;
    }

    if (expect_true(sl_str_equal(sl_owned_str_as_view(owned), different_str)) != 0) {
        return 4;
    }

    return 0;
}

static int test_hashing(void)
{
    const char embedded[] = {'a', '\0', 'b'};
    const char same_embedded[] = {'a', '\0', 'b'};
    SlStr embedded_str = sl_str_from_parts(embedded, sizeof(embedded));
    SlStr same_embedded_str = sl_str_from_parts(same_embedded, sizeof(same_embedded));
    uint64_t hash = 0U;
    uint64_t same_hash = 0U;
    uint64_t sentinel_hash = 123U;

    if (expect_status(sl_str_hash(embedded_str, &hash), SL_STATUS_OK) != 0 ||
        expect_status(sl_str_hash(same_embedded_str, &same_hash), SL_STATUS_OK) != 0 ||
        hash != same_hash)
    {
        return 10;
    }

    if (expect_status(sl_str_hash(sl_str_from_parts(NULL, 1U), &sentinel_hash),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        sentinel_hash != 123U)
    {
        return 11;
    }

    if (expect_status(sl_str_hash(embedded_str, NULL), SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 12;
    }

    return 0;
}

static int test_arena_copies(void)
{
    const char embedded[] = {'a', '\0', 'b'};
    const char different[] = {'a', '\0', 'c'};
    unsigned char arena_storage[64];
    unsigned char tiny_storage[2];
    SlArena arena;
    SlArena tiny_arena;
    SlStr embedded_str = sl_str_from_parts(embedded, sizeof(embedded));
    SlOwnedStr owned = {(char*)different, sizeof(different)};
    SlOwnedStr sentinel = {(char*)different, 99U};

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
        0)
    {
        return 20;
    }

    owned = sentinel;
    if (expect_status(sl_str_copy_to_arena(&arena, embedded_str, &owned), SL_STATUS_OK) != 0 ||
        owned.ptr == embedded || owned.length != embedded_str.length ||
        !sl_str_equal(sl_owned_str_as_view(owned), embedded_str))
    {
        return 21;
    }

    owned = sentinel;
    if (expect_status(sl_str_copy_to_arena(&arena, sl_str_empty(), &owned), SL_STATUS_OK) != 0 ||
        owned.ptr != NULL || owned.length != 0U)
    {
        return 22;
    }

    owned = sentinel;
    if (expect_status(sl_str_copy_to_arena(&arena, sl_str_from_parts(NULL, 3U), &owned),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        owned.ptr != sentinel.ptr || owned.length != sentinel.length)
    {
        return 23;
    }

    owned = sentinel;
    if (expect_status(sl_str_copy_to_arena_nul(&arena, embedded_str, &owned), SL_STATUS_OK) != 0 ||
        owned.length != embedded_str.length || owned.ptr[owned.length] != '\0' ||
        !sl_str_equal(sl_owned_str_as_view(owned), embedded_str))
    {
        return 24;
    }

    owned = sentinel;
    if (expect_status(sl_str_copy_to_arena_nul(&arena, sl_str_from_parts(NULL, 1U), &owned),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        owned.ptr != sentinel.ptr || owned.length != sentinel.length)
    {
        return 25;
    }

    if (expect_status(sl_arena_init(&tiny_arena, tiny_storage, sizeof(tiny_storage)),
                      SL_STATUS_OK) != 0)
    {
        return 26;
    }

    owned = sentinel;
    if (expect_status(sl_str_copy_to_arena_nul(&tiny_arena, embedded_str, &owned),
                      SL_STATUS_OUT_OF_MEMORY) != 0 ||
        owned.ptr != sentinel.ptr || owned.length != sentinel.length)
    {
        return 27;
    }

    return 0;
}

int main(void)
{
    int result = test_views_and_helpers();
    if (result != 0) {
        return result;
    }

    result = test_hashing();
    if (result != 0) {
        return result;
    }

    return test_arena_copies();
}

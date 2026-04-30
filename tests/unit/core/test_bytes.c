#include "sloppy/bytes.h"

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
    const unsigned char bytes[] = {0U, 1U, 2U, 0U};
    const unsigned char same[] = {0U, 1U, 2U, 0U};
    const unsigned char different[] = {0U, 1U, 3U, 0U};
    SlBytes empty = sl_bytes_empty();
    SlBytes view = sl_bytes_from_parts(bytes, sizeof(bytes));
    SlBytes same_view = sl_bytes_from_parts(same, sizeof(same));
    SlBytes different_view = sl_bytes_from_parts(different, sizeof(different));
    SlBytes prefix = sl_bytes_from_parts(bytes, 2U);
    SlBytes suffix = sl_bytes_from_parts(bytes + 2U, 2U);
    SlOwnedBytes owned = {(unsigned char*)different, sizeof(different)};

    if (expect_true(sl_bytes_is_empty(empty)) != 0 ||
        expect_true(sl_bytes_equal(empty, sl_bytes_from_parts(NULL, 0U))) != 0)
    {
        return 1;
    }

    if (expect_true(sl_bytes_equal(view, same_view)) != 0 ||
        expect_true(!sl_bytes_equal(view, different_view)) != 0 ||
        expect_true(
            !sl_bytes_equal(sl_bytes_from_parts(NULL, 4U), sl_bytes_from_parts(NULL, 4U))) != 0 ||
        expect_true(!sl_bytes_equal(view, empty)) != 0)
    {
        return 2;
    }

    if (expect_true(sl_bytes_starts_with(view, prefix)) != 0 ||
        expect_true(sl_bytes_ends_with(view, suffix)) != 0)
    {
        return 3;
    }

    if (expect_true(sl_bytes_equal(sl_owned_bytes_as_view(owned), different_view)) != 0) {
        return 4;
    }

    return 0;
}

static int test_hashing(void)
{
    const unsigned char bytes[] = {0U, 1U, 2U, 0U};
    const unsigned char same[] = {0U, 1U, 2U, 0U};
    SlBytes view = sl_bytes_from_parts(bytes, sizeof(bytes));
    SlBytes same_view = sl_bytes_from_parts(same, sizeof(same));
    uint64_t hash = 0U;
    uint64_t same_hash = 0U;
    uint64_t sentinel_hash = 123U;

    if (expect_status(sl_bytes_hash(view, &hash), SL_STATUS_OK) != 0 ||
        expect_status(sl_bytes_hash(same_view, &same_hash), SL_STATUS_OK) != 0 || hash != same_hash)
    {
        return 10;
    }

    if (expect_status(sl_bytes_hash(sl_bytes_from_parts(NULL, 1U), &sentinel_hash),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        sentinel_hash != 123U)
    {
        return 11;
    }

    if (expect_status(sl_bytes_hash(view, NULL), SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 12;
    }

    return 0;
}

static int test_arena_copies(void)
{
    const unsigned char bytes[] = {0U, 1U, 2U, 0U};
    const unsigned char different[] = {0U, 1U, 3U, 0U};
    unsigned char arena_storage[32];
    unsigned char tiny_storage[2];
    SlArena arena;
    SlArena tiny_arena;
    SlBytes view = sl_bytes_from_parts(bytes, sizeof(bytes));
    SlOwnedBytes owned = {(unsigned char*)different, sizeof(different)};
    SlOwnedBytes sentinel = {(unsigned char*)different, 99U};

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
        0)
    {
        return 20;
    }

    owned = sentinel;
    if (expect_status(sl_bytes_copy_to_arena(&arena, view, &owned), SL_STATUS_OK) != 0 ||
        owned.ptr == bytes || owned.length != view.length ||
        !sl_bytes_equal(sl_owned_bytes_as_view(owned), view))
    {
        return 21;
    }

    owned = sentinel;
    if (expect_status(sl_bytes_copy_to_arena(&arena, sl_bytes_empty(), &owned), SL_STATUS_OK) !=
            0 ||
        owned.ptr != NULL || owned.length != 0U)
    {
        return 22;
    }

    owned = sentinel;
    if (expect_status(sl_bytes_copy_to_arena(&arena, sl_bytes_from_parts(NULL, 4U), &owned),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        owned.ptr != sentinel.ptr || owned.length != sentinel.length)
    {
        return 23;
    }

    if (expect_status(sl_arena_init(&tiny_arena, tiny_storage, sizeof(tiny_storage)),
                      SL_STATUS_OK) != 0)
    {
        return 24;
    }

    owned = sentinel;
    if (expect_status(sl_bytes_copy_to_arena(&tiny_arena, view, &owned), SL_STATUS_OUT_OF_MEMORY) !=
            0 ||
        owned.ptr != sentinel.ptr || owned.length != sentinel.length)
    {
        return 25;
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

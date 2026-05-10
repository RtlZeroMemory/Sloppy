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
    const unsigned char hello[] = {'h', 'e', 'l', 'l', 'o'};
    const unsigned char marker = 0x7fU;
    SlBytes view = sl_bytes_from_parts(bytes, sizeof(bytes));
    SlBytes same_view = sl_bytes_from_parts(same, sizeof(same));
    SlBytes hello_view = sl_bytes_from_parts(hello, sizeof(hello));
    uint64_t hash = 0U;
    uint64_t same_hash = 0U;
    uint64_t hello_hash = 0U;
    uint64_t empty_hash = 0U;
    uint64_t nonnull_empty_hash = 0U;
    uint64_t sentinel_hash = 123U;

    if (expect_status(sl_bytes_hash(view, &hash), SL_STATUS_OK) != 0 ||
        expect_status(sl_bytes_hash(same_view, &same_hash), SL_STATUS_OK) != 0 || hash != same_hash)
    {
        return 10;
    }

    if (expect_status(sl_bytes_hash(hello_view, &hello_hash), SL_STATUS_OK) != 0 ||
        hello_hash != UINT64_C(0x26c7827d889f6da3))
    {
        return 13;
    }

    if (expect_status(sl_bytes_hash(sl_bytes_empty(), &empty_hash), SL_STATUS_OK) != 0 ||
        expect_status(sl_bytes_hash(sl_bytes_from_parts(&marker, 0U), &nonnull_empty_hash),
                      SL_STATUS_OK) != 0 ||
        empty_hash != nonnull_empty_hash)
    {
        return 14;
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

static int test_find_helpers(void)
{
    const unsigned char bytes[] = {'a', 0U, 'b', 0xffU, 'c'};
    const unsigned char needles[] = {0xffU, 'z'};
    unsigned char full_range[256];
    SlBytesFindResult result = {.found = true, .index = 999U, .value = 77U};
    size_t index = 0U;

    if (expect_status(sl_bytes_find(sl_bytes_from_parts(bytes, sizeof(bytes)), 0U, &result),
                      SL_STATUS_OK) != 0 ||
        !result.found || result.index != 1U || result.value != 0U)
    {
        return 13;
    }

    if (expect_status(sl_bytes_find(sl_bytes_from_parts(bytes, sizeof(bytes)), 'q', &result),
                      SL_STATUS_OK) != 0 ||
        result.found || result.index != sizeof(bytes) || result.value != 0U)
    {
        return 14;
    }

    if (expect_status(sl_bytes_find_any(sl_bytes_from_parts(bytes, sizeof(bytes)),
                                        sl_bytes_from_parts(needles, sizeof(needles)), &result),
                      SL_STATUS_OK) != 0 ||
        !result.found || result.index != 3U || result.value != 0xffU)
    {
        return 15;
    }

    if (expect_status(
            sl_bytes_find_any(sl_bytes_from_parts(bytes, sizeof(bytes)), sl_bytes_empty(), &result),
            SL_STATUS_OK) != 0 ||
        result.found || result.index != sizeof(bytes))
    {
        return 16;
    }

    result = (SlBytesFindResult){.found = true, .index = 99U, .value = 7U};
    if (expect_status(sl_bytes_find(sl_bytes_from_parts(NULL, 1U), 1U, &result),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        !result.found || result.index != 99U || result.value != 7U ||
        expect_status(sl_bytes_find(sl_bytes_from_parts(bytes, sizeof(bytes)), 1U, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 17;
    }

    for (index = 0U; index < sizeof(full_range); index += 1U) {
        full_range[index] = (unsigned char)index;
    }
    if (expect_status(sl_bytes_find_any(sl_bytes_from_parts(full_range, sizeof(full_range)),
                                        sl_bytes_from_parts(bytes + 3U, 1U), &result),
                      SL_STATUS_OK) != 0 ||
        !result.found || result.index != 255U || result.value != 0xffU)
    {
        return 18;
    }

    return 0;
}

static int test_failure_atomicity(void)
{
    const unsigned char bytes[] = {1U, 2U, 3U, 4U};
    unsigned char arena_storage[32];
    unsigned char tiny_storage[2];
    SlArena arena;
    SlArena tiny_arena;
    SlBytesFindResult result = {.found = true, .index = 91U, .value = 0xabU};
    SlOwnedBytes sentinel = {(unsigned char*)bytes, 99U};
    SlOwnedBytes out = sentinel;

    if (expect_status(sl_bytes_find_any(sl_bytes_from_parts(bytes, sizeof(bytes)),
                                        sl_bytes_from_parts(NULL, 1U), &result),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        !result.found || result.index != 91U || result.value != 0xabU)
    {
        return 19;
    }

    if (expect_status(sl_bytes_find_any(sl_bytes_from_parts(NULL, 1U),
                                        sl_bytes_from_parts(bytes, 1U), &result),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        !result.found || result.index != 91U || result.value != 0xabU)
    {
        return 20;
    }

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_arena_init(&tiny_arena, tiny_storage, sizeof(tiny_storage)),
                      SL_STATUS_OK) != 0)
    {
        return 21;
    }

    out = sentinel;
    if (expect_status(sl_bytes_copy_to_arena(&arena, sl_bytes_from_parts(NULL, 1U), &out),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        out.ptr != sentinel.ptr || out.length != sentinel.length)
    {
        return 22;
    }

    out = sentinel;
    if (expect_status(
            sl_bytes_copy_to_arena(&tiny_arena, sl_bytes_from_parts(bytes, sizeof(bytes)), &out),
            SL_STATUS_OUT_OF_MEMORY) != 0 ||
        out.ptr != sentinel.ptr || out.length != sentinel.length)
    {
        return 23;
    }

    return 0;
}

static int test_deterministic_byte_scan_properties(void)
{
    unsigned char bytes[64];
    unsigned char needles[3];
    size_t length = 0U;

    for (length = 0U; length <= sizeof(bytes); length += 1U) {
        size_t index = 0U;
        SlBytesFindResult result = {.found = true, .index = 777U, .value = 88U};
        bool expected_found = false;
        size_t expected_index = length;

        for (index = 0U; index < sizeof(bytes); index += 1U) {
            bytes[index] = (unsigned char)((index * 37U + length * 11U) & 0xffU);
        }
        if (length > 0U) {
            bytes[length / 2U] = 0U;
        }

        for (index = 0U; index < length; index += 1U) {
            if (bytes[index] == 0U) {
                expected_found = true;
                expected_index = index;
                break;
            }
        }

        if (expect_status(sl_bytes_find(sl_bytes_from_parts(bytes, length), 0U, &result),
                          SL_STATUS_OK) != 0 ||
            result.found != expected_found || result.index != expected_index ||
            (result.found && result.value != 0U))
        {
            return 30;
        }

        needles[0] = 0xfeU;
        needles[1] = 0xffU;
        needles[2] = 0U;
        if (expect_status(sl_bytes_find_any(sl_bytes_from_parts(bytes, length),
                                            sl_bytes_from_parts(needles, sizeof(needles)), &result),
                          SL_STATUS_OK) != 0)
        {
            return 31;
        }
        if (result.found && result.index >= length) {
            return 32;
        }
    }

    return 0;
}

static int test_find_any_parity_matrix(void)
{
    unsigned char bytes[97];
    const unsigned char needle_sets[][4] = {
        {0U, 0U, 0U, 0U},
        {(unsigned char)'/', (unsigned char)'?', (unsigned char)'#', 0U},
        {0xffU, 0x80U, 0x7fU, 0x40U},
        {(unsigned char)'a', (unsigned char)'z', (unsigned char)'A', (unsigned char)'Z'},
    };
    size_t length = 0U;

    for (length = 0U; length <= sizeof(bytes); length += 1U) {
        size_t index = 0U;
        size_t set_index = 0U;

        for (index = 0U; index < sizeof(bytes); index += 1U) {
            bytes[index] = (unsigned char)((index * 19U + length * 7U + 3U) & 0xffU);
        }
        if (length > 33U) {
            bytes[33U] = 0xffU;
        }
        if (length > 64U) {
            bytes[64U] = (unsigned char)'?';
        }

        for (set_index = 0U; set_index < sizeof(needle_sets) / sizeof(needle_sets[0]);
             set_index += 1U)
        {
            SlBytesFindResult single = {.found = true, .index = 999U, .value = 77U};
            SlBytesFindResult result = {.found = true, .index = 999U, .value = 77U};
            unsigned char single_needle = needle_sets[set_index][0];
            bool single_expected_found = false;
            size_t single_expected_index = length;
            bool expected_found = false;
            size_t expected_index = length;
            unsigned char expected_value = 0U;

            for (index = 0U; index < length; index += 1U) {
                size_t needle_index = 0U;

                if (!single_expected_found && bytes[index] == single_needle) {
                    single_expected_found = true;
                    single_expected_index = index;
                }
                if (!expected_found) {
                    for (needle_index = 0U; needle_index < sizeof(needle_sets[0]);
                         needle_index += 1U)
                    {
                        if (bytes[index] == needle_sets[set_index][needle_index]) {
                            expected_found = true;
                            expected_index = index;
                            expected_value = bytes[index];
                            break;
                        }
                    }
                }
                if (expected_found && single_expected_found) {
                    continue;
                }
            }

            if (expect_status(
                    sl_bytes_find(sl_bytes_from_parts(bytes, length), single_needle, &single),
                    SL_STATUS_OK) != 0 ||
                single.found != single_expected_found || single.index != single_expected_index ||
                (single.found && single.value != single_needle))
            {
                return 34;
            }

            if (expect_status(sl_bytes_find_any(sl_bytes_from_parts(bytes, length),
                                                sl_bytes_from_parts(needle_sets[set_index],
                                                                    sizeof(needle_sets[0])),
                                                &result),
                              SL_STATUS_OK) != 0 ||
                result.found != expected_found || result.index != expected_index ||
                (result.found && result.value != expected_value))
            {
                return 33;
            }
        }
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

    result = test_find_helpers();
    if (result != 0) {
        return result;
    }

    result = test_failure_atomicity();
    if (result != 0) {
        return result;
    }

    result = test_deterministic_byte_scan_properties();
    if (result != 0) {
        return result;
    }

    result = test_find_any_parity_matrix();
    if (result != 0) {
        return result;
    }

    return test_arena_copies();
}

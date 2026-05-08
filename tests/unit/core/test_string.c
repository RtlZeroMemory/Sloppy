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
    SlOwnedStr owned = {.ptr = (char*)different, .length = sizeof(different)};

    if (expect_true(sl_str_is_empty(empty)) != 0 ||
        expect_true(sl_str_equal(empty, from_null_cstr)) != 0 ||
        expect_true(from_cstr.length == 5U) != 0 ||
        expect_true(sl_str_equal(SL_STR_LITERAL("hello"), from_cstr)) != 0)
    {
        return 1;
    }

    if (expect_true(sl_str_equal(embedded_str, same_embedded_str)) != 0 ||
        expect_true(!sl_str_equal(embedded_str, different_str)) != 0 ||
        expect_true(!sl_str_equal(sl_str_from_parts(NULL, 3U), sl_str_from_parts(NULL, 3U))) != 0)
    {
        return 2;
    }

    if (expect_true(sl_str_compare(embedded_str, same_embedded_str) == 0) != 0 ||
        expect_true(sl_str_compare(embedded_str, different_str) < 0) != 0 ||
        expect_true(sl_str_compare(sl_str_from_cstr("app"), sl_str_from_cstr("apple")) < 0) != 0 ||
        expect_true(sl_str_compare(sl_str_from_parts(NULL, 1U), embedded_str) < 0) != 0)
    {
        return 7;
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

static int test_no_nul_boundary_helpers(void)
{
    const char embedded[] = {'h', 'o', 's', 't', '\0', 'x'};
    const char non_ascii[] = {(char)0xc3, (char)0xa9};
    SlStr embedded_str = sl_str_from_parts(embedded, sizeof(embedded));
    SlStr non_ascii_str = sl_str_from_parts(non_ascii, sizeof(non_ascii));

    if (expect_true(sl_str_contains_nul(embedded_str)) != 0 ||
        expect_true(!sl_str_contains_nul(non_ascii_str)) != 0 ||
        expect_true(!sl_str_contains_nul(sl_str_empty())) != 0 ||
        expect_true(!sl_str_contains_nul(sl_str_from_parts(NULL, 1U))) != 0)
    {
        return 5;
    }

    if (expect_status(sl_str_validate_no_nul(sl_str_empty()), SL_STATUS_OK) != 0 ||
        expect_status(sl_str_validate_no_nul(non_ascii_str), SL_STATUS_OK) != 0 ||
        expect_status(sl_str_validate_no_nul(embedded_str), SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_status(sl_str_validate_no_nul(sl_str_from_parts(NULL, 1U)),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 6;
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

static int test_ascii_case_helpers(void)
{
    const char mixed[] = {'H', 'e', 'A', 'D', 'e', 'R', '\0', 'X'};
    const char lower[] = {'h', 'e', 'a', 'd', 'e', 'r', '\0', 'x'};
    const char prefix[] = {'h', 'E', 'A'};
    const char suffix[] = {'\0', 'x'};
    const char non_ascii_left[] = {(char)0xc3, (char)0xa9};
    const char non_ascii_right[] = {(char)0xc3, (char)0x89};

    if (expect_true(sl_str_equal_ci_ascii(sl_str_from_cstr("Content-Type"),
                                          sl_str_from_cstr("content-type"))) != 0 ||
        expect_true(sl_str_equal_ci_ascii(sl_str_from_parts(mixed, sizeof(mixed)),
                                          sl_str_from_parts(lower, sizeof(lower)))) != 0)
    {
        return 30;
    }

    if (expect_true(sl_str_starts_with_ci_ascii(sl_str_from_parts(mixed, sizeof(mixed)),
                                                sl_str_from_parts(prefix, sizeof(prefix)))) != 0 ||
        expect_true(sl_str_ends_with_ci_ascii(sl_str_from_parts(mixed, sizeof(mixed)),
                                              sl_str_from_parts(suffix, sizeof(suffix)))) != 0)
    {
        return 31;
    }

    if (expect_true(!sl_str_equal_ci_ascii(
            sl_str_from_parts(non_ascii_left, sizeof(non_ascii_left)),
            sl_str_from_parts(non_ascii_right, sizeof(non_ascii_right)))) != 0 ||
        expect_true(!sl_str_equal_ci_ascii(sl_str_from_parts(NULL, 1U),
                                           sl_str_from_parts(NULL, 1U))) != 0 ||
        expect_true(sl_str_starts_with_ci_ascii(sl_str_from_cstr("abc"), sl_str_empty())) != 0 ||
        expect_true(sl_str_ends_with_ci_ascii(sl_str_from_cstr("abc"), sl_str_empty())) != 0)
    {
        return 32;
    }

    return 0;
}

static int test_deterministic_string_view_properties(void)
{
    char left[32];
    char right[32];
    size_t length = 0U;

    for (length = 0U; length <= sizeof(left); length += 1U) {
        size_t index = 0U;
        SlStr left_view;
        SlStr right_view;

        for (index = 0U; index < sizeof(left); index += 1U) {
            char ch = (char)('a' + (char)(index % 26U));
            left[index] = ch;
            if ((index % 2U) == 0U) {
                right[index] = (char)('A' + (char)(index % 26U));
            }
            else {
                right[index] = ch;
            }
        }
        if (length > 3U) {
            left[3] = '\0';
            right[3] = '\0';
        }

        left_view = sl_str_from_parts(left, length);
        right_view = sl_str_from_parts(right, length);
        if (expect_true(sl_str_equal_ci_ascii(left_view, right_view)) != 0) {
            return 40;
        }

        if (length > 3U && expect_true(sl_str_contains_nul(left_view)) != 0) {
            return 41;
        }

        if (length <= 3U && expect_true(!sl_str_contains_nul(left_view)) != 0) {
            return 42;
        }

        if (expect_true(sl_str_starts_with(left_view, sl_str_from_parts(left, length / 2U))) != 0) {
            return 43;
        }

        if (expect_true(sl_str_ends_with(
                left_view, sl_str_from_parts(left + (length / 2U), length - (length / 2U)))) != 0)
        {
            return 44;
        }
    }

    return 0;
}

static int test_ascii_case_parity_matrix(void)
{
    char left[129];
    char right[129];
    size_t length = 0U;

    for (length = 0U; length <= sizeof(left); length += 1U) {
        size_t index = 0U;
        bool expected_equal = true;
        bool expected_contains_nul = false;

        for (index = 0U; index < sizeof(left); index += 1U) {
            unsigned char base = (unsigned char)((index * 13U + length * 5U) & 0x7fU);
            if (base >= (unsigned char)'A' && base <= (unsigned char)'Z') {
                left[index] = (char)base;
                right[index] = (char)(base - (unsigned char)'A' + (unsigned char)'a');
            }
            else if (base >= (unsigned char)'a' && base <= (unsigned char)'z') {
                left[index] = (char)base;
                right[index] = (char)(base - (unsigned char)'a' + (unsigned char)'A');
            }
            else {
                left[index] = (char)base;
                right[index] = (char)base;
            }
        }

        if (length > 31U) {
            left[31U] = '[';
            right[31U] = '{';
            expected_equal = false;
        }
        if (length > 65U) {
            left[65U] = (char)0xc3;
            right[65U] = (char)0xc3;
        }
        if (length > 96U) {
            left[96U] = '\0';
            right[96U] = '\0';
            expected_contains_nul = true;
        }
        for (index = 0U; index < length; index += 1U) {
            if (left[index] == '\0') {
                expected_contains_nul = true;
                break;
            }
        }

        if (expect_true(sl_str_equal_ci_ascii(sl_str_from_parts(left, length),
                                              sl_str_from_parts(right, length)) ==
                        expected_equal) != 0)
        {
            return 45;
        }

        if (length <= 31U &&
            (expect_true(sl_str_starts_with_ci_ascii(sl_str_from_parts(left, length),
                                                     sl_str_from_parts(right, length / 2U))) != 0 ||
             expect_true(sl_str_ends_with_ci_ascii(
                 sl_str_from_parts(left, length),
                 sl_str_from_parts(right + (length / 2U), length - (length / 2U)))) != 0))
        {
            return 46;
        }

        if (expect_true(sl_str_contains_nul(sl_str_from_parts(left, length)) ==
                        expected_contains_nul) != 0)
        {
            return 47;
        }
    }

    return 0;
}

static int test_arena_copies(void)
{
    const char embedded[] = {'a', '\0', 'b'};
    const char different[] = {'a', '\0', 'c'};
    const char large[] = "0123456789abcdef0123456789abcdef";
    unsigned char arena_storage[64];
    unsigned char tiny_storage[2];
    SlArena arena;
    SlArena tiny_arena;
    SlStr embedded_str = sl_str_from_parts(embedded, sizeof(embedded));
    SlStr large_str = sl_str_from_parts(large, sizeof(large) - 1U);
    SlOwnedStr owned = {.ptr = (char*)different, .length = sizeof(different)};
    SlOwnedStr sentinel = {.ptr = (char*)different, .length = 99U};
    size_t used_before = 0U;

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
        0)
    {
        return 20;
    }

    owned = sentinel;
    used_before = sl_arena_used(&arena);
    if (expect_status(sl_str_copy_to_arena(&arena, embedded_str, &owned), SL_STATUS_OK) != 0 ||
        owned.ptr == embedded || owned.length != embedded_str.length ||
        sl_arena_used(&arena) != used_before ||
        !sl_str_equal(sl_owned_str_as_view(owned), embedded_str))
    {
        return 21;
    }

    owned = sentinel;
    used_before = sl_arena_used(&arena);
    if (expect_status(
            sl_str_concat_to_arena(&arena, sl_str_from_cstr("ab"), sl_str_from_cstr("cd"), &owned),
            SL_STATUS_OK) != 0 ||
        owned.length != 4U || sl_arena_used(&arena) != used_before ||
        !sl_str_equal(sl_owned_str_as_view(owned), sl_str_from_cstr("abcd")))
    {
        return 31;
    }

    owned = sentinel;
    if (expect_status(sl_str_concat_to_arena(&arena, sl_str_from_parts(NULL, 1U),
                                             sl_str_from_cstr("x"), &owned),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        owned.ptr != sentinel.ptr || owned.length != sentinel.length)
    {
        return 32;
    }

    if (expect_status(sl_arena_init(&tiny_arena, tiny_storage, sizeof(tiny_storage)),
                      SL_STATUS_OK) != 0)
    {
        return 33;
    }

    owned = sentinel;
    if (expect_status(sl_str_concat_to_arena(&tiny_arena, sl_str_from_cstr("ab"),
                                             sl_str_from_cstr("cd"), &owned),
                      SL_STATUS_OK) != 0 ||
        owned.length != 4U ||
        !sl_str_equal(sl_owned_str_as_view(owned), sl_str_from_cstr("abcd")) ||
        sl_arena_used(&tiny_arena) != 0U)
    {
        return 34;
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
    if (expect_status(sl_str_copy_to_arena_cstr(&arena, embedded_str, &owned),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        owned.ptr != sentinel.ptr || owned.length != sentinel.length)
    {
        return 28;
    }

    owned = sentinel;
    if (expect_status(sl_str_copy_to_arena_cstr(&arena, sl_str_from_cstr("host"), &owned),
                      SL_STATUS_OK) != 0 ||
        owned.length != 4U || owned.ptr[owned.length] != '\0' ||
        !sl_str_equal(sl_owned_str_as_view(owned), sl_str_from_cstr("host")))
    {
        return 29;
    }

    owned = sentinel;
    if (expect_status(sl_str_copy_to_arena_cstr(&arena, sl_str_empty(), &owned), SL_STATUS_OK) !=
            0 ||
        owned.ptr == NULL || owned.length != 0U || owned.ptr[0] != '\0')
    {
        return 30;
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
    if (expect_status(sl_str_copy_to_arena_nul(&tiny_arena, embedded_str, &owned), SL_STATUS_OK) !=
            0 ||
        owned.length != embedded_str.length ||
        !sl_str_equal(sl_owned_str_as_view(owned), embedded_str) ||
        sl_arena_used(&tiny_arena) != 0U)
    {
        return 27;
    }

    owned = sentinel;
    if (expect_status(sl_str_copy_to_arena(&tiny_arena, large_str, &owned),
                      SL_STATUS_OUT_OF_MEMORY) != 0 ||
        owned.ptr != sentinel.ptr || owned.length != sentinel.length)
    {
        return 35;
    }

    owned = sentinel;
    if (expect_status(sl_str_copy_to_arena(&arena, large_str, &owned), SL_STATUS_OK) != 0 ||
        owned.length != large_str.length ||
        owned.arena_generation != sl_arena_stats(&arena).generation ||
        !sl_arena_contains_str(&arena, sl_owned_str_as_view(owned)) ||
        !sl_str_equal(sl_owned_str_as_view(owned), large_str))
    {
        return 36;
    }

    if (expect_true(sl_arena_contains_str(&arena, sl_str_empty())) != 0 ||
        expect_true(!sl_arena_contains_str(NULL, sl_str_empty())) != 0 ||
        expect_true(!sl_arena_contains_str(&arena, sl_str_from_cstr("external"))) != 0 ||
        expect_true(!sl_arena_contains_str(&arena, sl_str_from_parts(NULL, 1U))) != 0)
    {
        return 37;
    }

    owned = sentinel;
    if (expect_status(sl_str_copy_to_arena(&arena, sl_str_from_cstr("copy-stable"), &owned),
                      SL_STATUS_OK) != 0)
    {
        return 38;
    }
    {
        SlOwnedStr copied_owned = owned;
        sl_owned_str_rebind(&copied_owned);
        if (expect_true(sl_str_equal(sl_owned_str_as_view(copied_owned),
                                     sl_str_from_cstr("copy-stable"))) != 0)
        {
            return 39;
        }
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

    result = test_no_nul_boundary_helpers();
    if (result != 0) {
        return result;
    }

    result = test_ascii_case_helpers();
    if (result != 0) {
        return result;
    }

    result = test_deterministic_string_view_properties();
    if (result != 0) {
        return result;
    }

    result = test_ascii_case_parity_matrix();
    if (result != 0) {
        return result;
    }

    return test_arena_copies();
}

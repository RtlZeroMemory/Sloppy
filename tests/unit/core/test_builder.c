#include "sloppy/builder.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static int expect_bytes(SlBytes actual, const unsigned char* expected, size_t expected_length)
{
    return expect_true(actual.length == expected_length &&
                       memcmp(actual.ptr, expected, expected_length) == 0);
}

static int expect_str(SlStr actual, const char* expected, size_t expected_length)
{
    return expect_true(actual.length == expected_length &&
                       memcmp(actual.ptr, expected, expected_length) == 0);
}

static int test_fixed_byte_builder(void)
{
    unsigned char storage[4];
    const unsigned char binary[] = {0U, 1U, 2U};
    const unsigned char overflow[] = {3U, 4U};
    SlByteBuilder builder;

    /* --- Arrange. --- */
    if (expect_status(sl_byte_builder_init_fixed(&builder, storage, sizeof(storage)),
                      SL_STATUS_OK) != 0)
    {
        return 1;
    }

    /* --- Act and assert. --- */
    if (expect_status(sl_byte_builder_append_bytes(&builder, sl_bytes_from_parts(binary, 3U)),
                      SL_STATUS_OK) != 0 ||
        expect_bytes(sl_byte_builder_view(&builder), binary, 3U) != 0)
    {
        return 2;
    }

    if (expect_status(sl_byte_builder_append_bytes(&builder, sl_bytes_from_parts(overflow, 2U)),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        expect_bytes(sl_byte_builder_view(&builder), binary, 3U) != 0)
    {
        return 3;
    }

    if (expect_status(sl_byte_builder_append_byte(&builder, 9U), SL_STATUS_OK) != 0 ||
        sl_byte_builder_length(&builder) != 4U)
    {
        return 4;
    }

    sl_byte_builder_reset(&builder);
    if (sl_byte_builder_length(&builder) != 0U ||
        !sl_bytes_is_empty(sl_byte_builder_view(&builder)))
    {
        return 5;
    }

    if (expect_status(sl_byte_builder_init_fixed(&builder, NULL, 1U), SL_STATUS_INVALID_ARGUMENT) !=
        0)
    {
        return 6;
    }

    return 0;
}

static int test_arena_byte_builder_growth_and_failures(void)
{
    unsigned char arena_storage[64];
    const unsigned char first[] = {'a', 'b'};
    const unsigned char second[] = {'c', 'd', 'e'};
    const unsigned char expected[] = {'a', 'b', 'c', 'd', 'e'};
    SlArena arena;
    SlByteBuilder builder;

    /* --- Arrange. --- */
    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
        0)
    {
        return 10;
    }

    if (expect_status(sl_byte_builder_init_arena(&builder, &arena, 2U, 8U), SL_STATUS_OK) != 0) {
        return 11;
    }

    /* --- Act and assert growth. --- */
    if (expect_status(sl_byte_builder_append_bytes(&builder, sl_bytes_from_parts(first, 2U)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_byte_builder_append_bytes(&builder, sl_bytes_from_parts(second, 3U)),
                      SL_STATUS_OK) != 0 ||
        expect_bytes(sl_byte_builder_view(&builder), expected, sizeof(expected)) != 0 ||
        sl_byte_builder_capacity(&builder) < sizeof(expected))
    {
        return 12;
    }

    /* --- Assert failure behavior. --- */
    if (expect_status(sl_byte_builder_reserve(&builder, SIZE_MAX), SL_STATUS_OVERFLOW) != 0 ||
        expect_bytes(sl_byte_builder_view(&builder), expected, sizeof(expected)) != 0)
    {
        return 13;
    }

    if (expect_status(sl_byte_builder_append_bytes(&builder, sl_bytes_from_parts(NULL, 1U)),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_bytes(sl_byte_builder_view(&builder), expected, sizeof(expected)) != 0)
    {
        return 14;
    }

    if (expect_status(sl_byte_builder_reserve(&builder, 4U), SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        expect_bytes(sl_byte_builder_view(&builder), expected, sizeof(expected)) != 0)
    {
        return 15;
    }

    return 0;
}

static int test_small_builder_sso_contract(void)
{
    const unsigned char prefix[] = {'s', 's', 'o'};
    const unsigned char overflow[] = {
        'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
        'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
        'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
        'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x'};
    SlByteBuilder byte_builder;
    SlByteBuilder copied_byte_builder;
    SlStringBuilder string_builder;
    SlStringBuilder copied_string_builder;
    SlByteBuilderStats stats;
    SlStr view = sl_str_from_cstr("sentinel");

    if (expect_status(sl_byte_builder_init_small(&byte_builder), SL_STATUS_OK) != 0 ||
        sl_byte_builder_capacity(&byte_builder) != SL_BYTE_BUILDER_SMALL_CAPACITY)
    {
        return 70;
    }

    if (expect_status(sl_byte_builder_append_bytes(&byte_builder,
                                                   sl_bytes_from_parts(prefix, sizeof(prefix))),
                      SL_STATUS_OK) != 0 ||
        expect_bytes(sl_byte_builder_view(&byte_builder), prefix, sizeof(prefix)) != 0)
    {
        return 71;
    }

    stats = sl_byte_builder_stats(&byte_builder);
    if (stats.storage != SL_BUILDER_STORAGE_SMALL ||
        stats.capacity != SL_BYTE_BUILDER_SMALL_CAPACITY || stats.grow_count != 0U ||
        stats.copied_bytes != 0U)
    {
        return 72;
    }

    copied_byte_builder = byte_builder;
    if (expect_status(sl_byte_builder_append_byte(&copied_byte_builder, (unsigned char)'!'),
                      SL_STATUS_OK) != 0 ||
        expect_bytes(sl_byte_builder_view(&byte_builder), prefix, sizeof(prefix)) != 0 ||
        expect_bytes(sl_byte_builder_view(&copied_byte_builder), (const unsigned char*)"sso!",
                     4U) != 0)
    {
        return 77;
    }

    if (expect_status(sl_byte_builder_append_bytes(&byte_builder,
                                                   sl_bytes_from_parts(overflow, sizeof(overflow))),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        expect_bytes(sl_byte_builder_view(&byte_builder), prefix, sizeof(prefix)) != 0)
    {
        return 73;
    }

    if (expect_status(sl_string_builder_init_small(&string_builder), SL_STATUS_OK) != 0 ||
        expect_status(sl_string_builder_append_cstr(&string_builder, "small"), SL_STATUS_OK) != 0 ||
        expect_status(sl_string_builder_append_char(&string_builder, ':'), SL_STATUS_OK) != 0 ||
        expect_status(sl_string_builder_append_size(&string_builder, 7U), SL_STATUS_OK) != 0 ||
        expect_str(sl_string_builder_view(&string_builder), "small:7", 7U) != 0)
    {
        return 74;
    }

    if (expect_status(sl_string_builder_view_with_nul(&string_builder, &view), SL_STATUS_OK) != 0 ||
        expect_str(view, "small:7", 7U) != 0 || view.ptr[view.length] != '\0')
    {
        return 75;
    }

    copied_string_builder = string_builder;
    if (expect_status(sl_string_builder_append_cstr(&copied_string_builder, ":copy"),
                      SL_STATUS_OK) != 0 ||
        expect_str(sl_string_builder_view(&string_builder), "small:7", 7U) != 0 ||
        expect_str(sl_string_builder_view(&copied_string_builder), "small:7:copy", 12U) != 0)
    {
        return 78;
    }

    sl_string_builder_reset(&string_builder);
    if (sl_string_builder_length(&string_builder) != 0U ||
        sl_string_builder_capacity(&string_builder) != SL_BYTE_BUILDER_SMALL_CAPACITY)
    {
        return 76;
    }

    return 0;
}

static int test_builder_stats_snapshot_contract(void)
{
    unsigned char fixed_storage[4];
    unsigned char arena_storage[64];
    const unsigned char first[] = {'a', 'b'};
    const unsigned char second[] = {'c', 'd', 'e'};
    SlArena arena;
    SlByteBuilder fixed_builder;
    SlByteBuilder arena_builder;
    SlByteBuilderStats stats;

    stats = sl_byte_builder_stats(NULL);
    if (stats.length != 0U || stats.capacity != 0U || stats.storage != SL_BUILDER_STORAGE_INVALID) {
        return 50;
    }

    if (expect_status(
            sl_byte_builder_init_fixed(&fixed_builder, fixed_storage, sizeof(fixed_storage)),
            SL_STATUS_OK) != 0)
    {
        return 51;
    }

    if (expect_status(
            sl_byte_builder_append_bytes(&fixed_builder, sl_bytes_from_parts(first, sizeof(first))),
            SL_STATUS_OK) != 0 ||
        expect_status(sl_byte_builder_append_bytes(&fixed_builder, sl_bytes_empty()),
                      SL_STATUS_OK) != 0)
    {
        return 52;
    }

    stats = sl_byte_builder_stats(&fixed_builder);
    if (stats.length != sizeof(first) || stats.capacity != sizeof(fixed_storage) ||
        stats.max_capacity != sizeof(fixed_storage) || stats.appended_bytes != sizeof(first) ||
        stats.copied_bytes != 0U || stats.grow_count != 0U || stats.failed_reserve_count != 0U ||
        stats.storage != SL_BUILDER_STORAGE_FIXED)
    {
        return 53;
    }

    if (expect_status(sl_byte_builder_reserve(&fixed_builder, sizeof(fixed_storage)),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0)
    {
        return 54;
    }

    stats = sl_byte_builder_stats(&fixed_builder);
    if (stats.failed_reserve_count != 1U || stats.length != sizeof(first)) {
        return 55;
    }

    sl_byte_builder_reset(&fixed_builder);
    stats = sl_byte_builder_stats(&fixed_builder);
    if (stats.length != 0U || stats.appended_bytes != sizeof(first) ||
        stats.failed_reserve_count != 1U)
    {
        return 56;
    }

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_byte_builder_init_arena(&arena_builder, &arena, 2U, 16U), SL_STATUS_OK) !=
            0 ||
        expect_status(
            sl_byte_builder_append_bytes(&arena_builder, sl_bytes_from_parts(first, sizeof(first))),
            SL_STATUS_OK) != 0 ||
        expect_status(sl_byte_builder_append_bytes(&arena_builder,
                                                   sl_bytes_from_parts(second, sizeof(second))),
                      SL_STATUS_OK) != 0)
    {
        return 57;
    }

    stats = sl_byte_builder_stats(&arena_builder);
    if (stats.length != sizeof(first) + sizeof(second) || stats.capacity < stats.length ||
        stats.appended_bytes != stats.length || stats.copied_bytes != sizeof(first) ||
        stats.grow_count != 1U || stats.storage != SL_BUILDER_STORAGE_ARENA)
    {
        return 58;
    }

    return 0;
}

static int test_fixed_builder_self_overlap_append(void)
{
    unsigned char byte_storage[8] = {'a', 'b', 'c', 'd', 'Z', 0U, 0U, 0U};
    char string_storage[12] = {'a', 'b', 'c', 'd', 'Z', '\0'};
    const unsigned char expected_bytes[] = {'a', 'b', 'c', 'd', 'd', 'Z'};
    SlByteBuilder byte_builder;
    SlStringBuilder string_builder;

    if (expect_status(sl_byte_builder_init_fixed(&byte_builder, byte_storage, sizeof(byte_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(
            sl_byte_builder_append_bytes(&byte_builder, sl_bytes_from_parts(byte_storage, 4U)),
            SL_STATUS_OK) != 0)
    {
        return 16;
    }

    if (expect_status(
            sl_byte_builder_append_bytes(&byte_builder, sl_bytes_from_parts(byte_storage + 3U, 2U)),
            SL_STATUS_OK) != 0 ||
        expect_bytes(sl_byte_builder_view(&byte_builder), expected_bytes, sizeof(expected_bytes)) !=
            0)
    {
        return 17;
    }

    if (expect_status(
            sl_string_builder_init_fixed(&string_builder, string_storage, sizeof(string_storage)),
            SL_STATUS_OK) != 0 ||
        expect_status(
            sl_string_builder_append_str(&string_builder, sl_str_from_parts(string_storage, 3U)),
            SL_STATUS_OK) != 0)
    {
        return 18;
    }

    if (expect_status(sl_string_builder_append_str(&string_builder,
                                                   sl_str_from_parts(string_storage + 1U, 3U)),
                      SL_STATUS_OK) != 0 ||
        expect_str(sl_string_builder_view(&string_builder), "abcbcd", 6U) != 0)
    {
        return 19;
    }

    return 0;
}

static int test_string_builder_formatting_and_nul(void)
{
    char storage[64];
    char exact[3];
    char format_storage[SL_STRING_FORMAT_F64_CAPACITY];
    SlStringBuilder builder;
    SlStringBuilder exact_builder;
    SlStr view;
    SlStr formatted;
    SlStr sentinel = sl_str_from_cstr("sentinel");

    /* --- Arrange. --- */
    if (expect_status(sl_string_builder_init_fixed(&builder, storage, sizeof(storage)),
                      SL_STATUS_OK) != 0)
    {
        return 20;
    }

    /* --- Act and assert formatting. --- */
    if (expect_status(sl_string_builder_append_str(&builder, sl_str_from_parts("ab", 2U)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_string_builder_append_char(&builder, ':'), SL_STATUS_OK) != 0 ||
        expect_status(sl_string_builder_append_u64(&builder, 42U), SL_STATUS_OK) != 0 ||
        expect_status(sl_string_builder_append_char(&builder, ':'), SL_STATUS_OK) != 0 ||
        expect_status(sl_string_builder_append_i64(&builder, -17), SL_STATUS_OK) != 0 ||
        expect_status(sl_string_builder_append_char(&builder, ':'), SL_STATUS_OK) != 0 ||
        expect_status(sl_string_builder_append_size(&builder, 5U), SL_STATUS_OK) != 0 ||
        expect_status(sl_string_builder_append_char(&builder, ':'), SL_STATUS_OK) != 0 ||
        expect_status(sl_string_builder_append_f64(&builder, 3.5), SL_STATUS_OK) != 0)
    {
        return 21;
    }

    if (expect_str(sl_string_builder_view(&builder), "ab:42:-17:5:3.5", 15U) != 0) {
        return 22;
    }

    /* --- Assert boundary NUL behavior. --- */
    view = sentinel;
    if (expect_status(sl_string_builder_view_with_nul(&builder, &view), SL_STATUS_OK) != 0 ||
        view.ptr[view.length] != '\0' || expect_str(view, "ab:42:-17:5:3.5", 15U) != 0)
    {
        return 23;
    }

    formatted = sentinel;
    if (expect_status(
            sl_string_format_u64(format_storage, sizeof(format_storage), UINT64_MAX, &formatted),
            SL_STATUS_OK) != 0 ||
        formatted.ptr != format_storage ||
        expect_str(formatted, "18446744073709551615", 20U) != 0 ||
        format_storage[formatted.length] != '\0')
    {
        return 24;
    }

    formatted = sentinel;
    if (expect_status(sl_string_format_i64(format_storage, sizeof(format_storage),
                                           -9223372036854775807LL - 1LL, &formatted),
                      SL_STATUS_OK) != 0 ||
        formatted.ptr != format_storage ||
        expect_str(formatted, "-9223372036854775808", 20U) != 0 ||
        format_storage[formatted.length] != '\0')
    {
        return 25;
    }

    formatted = sentinel;
    if (expect_status(
            sl_string_format_f32(format_storage, sizeof(format_storage), 0.25F, &formatted),
            SL_STATUS_OK) != 0 ||
        formatted.ptr != format_storage || expect_str(formatted, "0.25", 4U) != 0 ||
        format_storage[formatted.length] != '\0')
    {
        return 26;
    }

    formatted = sentinel;
    if (expect_status(sl_string_format_f64(format_storage, sizeof(format_storage), 3.5, &formatted),
                      SL_STATUS_OK) != 0 ||
        formatted.ptr != format_storage || expect_str(formatted, "3.5", 3U) != 0 ||
        format_storage[formatted.length] != '\0')
    {
        return 27;
    }

    formatted = sentinel;
    if (expect_status(sl_string_format_f64(format_storage, SL_STRING_FORMAT_F64_CAPACITY - 1U, 3.5,
                                           &formatted),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        formatted.ptr != sentinel.ptr || formatted.length != sentinel.length)
    {
        return 28;
    }

    if (expect_status(sl_string_builder_init_fixed(&exact_builder, exact, sizeof(exact)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_string_builder_append_str(&exact_builder, sl_str_from_parts("abc", 3U)),
                      SL_STATUS_OK) != 0)
    {
        return 29;
    }

    view = sentinel;
    if (expect_status(sl_string_builder_view_with_nul(&exact_builder, &view),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        view.ptr != sentinel.ptr || view.length != sentinel.length ||
        expect_str(sl_string_builder_view(&exact_builder), "abc", 3U) != 0)
    {
        return 30;
    }

    return 0;
}

static int test_string_builder_stats_forward_byte_builder_stats(void)
{
    char storage[16];
    SlStringBuilder builder;
    SlByteBuilderStats stats;

    stats = sl_string_builder_stats(NULL);
    if (stats.storage != SL_BUILDER_STORAGE_INVALID || stats.length != 0U) {
        return 60;
    }

    if (expect_status(sl_string_builder_init_fixed(&builder, storage, sizeof(storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_string_builder_append_cstr(&builder, "abc"), SL_STATUS_OK) != 0 ||
        expect_status(sl_string_builder_append_char(&builder, ':'), SL_STATUS_OK) != 0 ||
        expect_status(sl_string_builder_append_size(&builder, 12U), SL_STATUS_OK) != 0)
    {
        return 61;
    }

    stats = sl_string_builder_stats(&builder);
    if (stats.length != 6U || stats.appended_bytes != 6U ||
        stats.storage != SL_BUILDER_STORAGE_FIXED)
    {
        return 62;
    }

    return 0;
}

static int test_string_builder_arena_growth_preserves_failed_prefix(void)
{
    unsigned char arena_storage[64];
    SlArena arena;
    SlStringBuilder builder;

    /* --- Arrange. --- */
    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
        0)
    {
        return 30;
    }

    if (expect_status(sl_string_builder_init_arena(&builder, &arena, 0U, 6U), SL_STATUS_OK) != 0) {
        return 31;
    }

    /* --- Act and assert preserved prefix. --- */
    if (expect_status(sl_string_builder_append_cstr(&builder, "abc"), SL_STATUS_OK) != 0 ||
        expect_status(sl_string_builder_append_i64(&builder, -9223372036854775807LL - 1LL),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        expect_str(sl_string_builder_view(&builder), "abc", 3U) != 0 ||
        expect_status(sl_string_builder_append_cstr(&builder, NULL), SL_STATUS_INVALID_ARGUMENT) !=
            0 ||
        expect_str(sl_string_builder_view(&builder), "abc", 3U) != 0 ||
        expect_status(sl_string_builder_append_cstr(&builder, "def"), SL_STATUS_OK) != 0 ||
        expect_str(sl_string_builder_view(&builder), "abcdef", 6U) != 0)
    {
        return 32;
    }

    return 0;
}

static int test_builder_invalid_and_corrupt_state_failures(void)
{
    unsigned char fixed_storage[4];
    unsigned char arena_storage[32];
    char string_storage[4];
    SlArena arena;
    SlByteBuilder byte_builder = {0};
    SlByteBuilder corrupt_byte_builder;
    SlStringBuilder string_builder = {0};
    SlStringBuilder corrupt_string_builder;

    if (expect_status(sl_byte_builder_append_byte(&byte_builder, 1U), SL_STATUS_INVALID_ARGUMENT) !=
            0 ||
        expect_status(sl_byte_builder_reserve(&byte_builder, 1U), SL_STATUS_INVALID_ARGUMENT) !=
            0 ||
        expect_status(sl_string_builder_append_cstr(&string_builder, "x"),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_status(sl_string_builder_reserve(&string_builder, 1U), SL_STATUS_INVALID_ARGUMENT) !=
            0)
    {
        return 80;
    }

    if (expect_status(sl_byte_builder_init_fixed(&byte_builder, NULL, 0U), SL_STATUS_OK) != 0 ||
        expect_status(sl_byte_builder_append_bytes(&byte_builder, sl_bytes_empty()),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_byte_builder_append_byte(&byte_builder, 1U),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        sl_byte_builder_length(&byte_builder) != 0U)
    {
        return 81;
    }

    if (expect_status(
            sl_byte_builder_init_fixed(&byte_builder, fixed_storage, sizeof(fixed_storage)),
            SL_STATUS_OK) != 0 ||
        expect_status(sl_byte_builder_append_bytes(
                          &byte_builder, sl_bytes_from_parts((const unsigned char*)"ab", 2U)),
                      SL_STATUS_OK) != 0)
    {
        return 82;
    }

    corrupt_byte_builder = byte_builder;
    corrupt_byte_builder.length = SIZE_MAX;
    if (expect_status(sl_byte_builder_reserve(&corrupt_byte_builder, 1U), SL_STATUS_OVERFLOW) !=
            0 ||
        expect_bytes(sl_byte_builder_view(&byte_builder), (const unsigned char*)"ab", 2U) != 0)
    {
        return 83;
    }

    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_byte_builder_init_arena(&byte_builder, &arena, 0U, 2U), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_byte_builder_append_bytes(
                          &byte_builder, sl_bytes_from_parts((const unsigned char*)"ab", 2U)),
                      SL_STATUS_OK) != 0)
    {
        return 84;
    }

    if (expect_status(sl_byte_builder_append_bytes(
                          &byte_builder, sl_bytes_from_parts((const unsigned char*)"c", 1U)),
                      SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
        expect_bytes(sl_byte_builder_view(&byte_builder), (const unsigned char*)"ab", 2U) != 0)
    {
        return 85;
    }

    if (expect_status(
            sl_string_builder_init_fixed(&string_builder, string_storage, sizeof(string_storage)),
            SL_STATUS_OK) != 0 ||
        expect_status(sl_string_builder_append_cstr(&string_builder, "xy"), SL_STATUS_OK) != 0)
    {
        return 86;
    }

    corrupt_string_builder = string_builder;
    corrupt_string_builder.bytes.length = SIZE_MAX;
    if (expect_status(sl_string_builder_append_cstr(&corrupt_string_builder, "z"),
                      SL_STATUS_OVERFLOW) != 0 ||
        expect_str(sl_string_builder_view(&string_builder), "xy", 2U) != 0)
    {
        return 87;
    }

    return 0;
}

static int test_formatters_reject_invalid_arguments_atomically(void)
{
    char buffer[SL_STRING_FORMAT_F64_CAPACITY];
    SlStr sentinel = sl_str_from_cstr("sentinel");
    SlStr out = sentinel;

    if (expect_status(sl_string_format_u64(NULL, sizeof(buffer), 1U, &out),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        out.ptr != sentinel.ptr || out.length != sentinel.length ||
        expect_status(sl_string_format_u64(buffer, sizeof(buffer), 1U, NULL),
                      SL_STATUS_INVALID_ARGUMENT) != 0)
    {
        return 90;
    }

    out = sentinel;
    buffer[0] = 'x';
    if (expect_status(sl_string_format_u64(buffer, 1U, 10U, &out), SL_STATUS_INVALID_ARGUMENT) !=
            0 ||
        out.ptr != sentinel.ptr || out.length != sentinel.length || buffer[0] != 'x')
    {
        return 91;
    }

    out = sentinel;
    buffer[0] = 'x';
    if (expect_status(sl_string_format_i64(buffer, 3U, -100, &out), SL_STATUS_INVALID_ARGUMENT) !=
            0 ||
        out.ptr != sentinel.ptr || out.length != sentinel.length || buffer[0] != '\0')
    {
        return 92;
    }

    out = sentinel;
    buffer[0] = 'x';
    if (expect_status(sl_string_format_f32(buffer, SL_STRING_FORMAT_F32_CAPACITY - 1U, 1.0F, &out),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        out.ptr != sentinel.ptr || out.length != sentinel.length || buffer[0] != 'x')
    {
        return 93;
    }

    out = sentinel;
    buffer[0] = 'x';
    if (expect_status(sl_string_format_f64(buffer, SL_STRING_FORMAT_F64_CAPACITY - 1U, 1.0, &out),
                      SL_STATUS_INVALID_ARGUMENT) != 0 ||
        out.ptr != sentinel.ptr || out.length != sentinel.length || buffer[0] != 'x')
    {
        return 94;
    }

    return 0;
}

static int test_arena_builders_fail_cleanly_after_dispose(void)
{
    unsigned char byte_arena_storage[64];
    unsigned char string_arena_storage[64];
    const unsigned char first[] = {'a', 'b'};
    const unsigned char grow[] = {'c', 'd', 'e'};
    SlArena byte_arena;
    SlArena string_arena;
    SlByteBuilder byte_builder;
    SlStringBuilder string_builder;

    if (expect_status(sl_arena_init(&byte_arena, byte_arena_storage, sizeof(byte_arena_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_byte_builder_init_arena(&byte_builder, &byte_arena, 2U, 8U),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_byte_builder_append_bytes(&byte_builder, sl_bytes_from_parts(first, 2U)),
                      SL_STATUS_OK) != 0)
    {
        return 40;
    }

    sl_arena_dispose(&byte_arena);
    /* Arena-backed builder views are invalid after arena dispose; only status is checked. */
    if (expect_status(sl_byte_builder_append_bytes(&byte_builder, sl_bytes_from_parts(grow, 3U)),
                      SL_STATUS_OUT_OF_MEMORY) != 0 ||
        expect_status(sl_byte_builder_reserve(&byte_builder, 3U), SL_STATUS_OUT_OF_MEMORY) != 0)
    {
        return 41;
    }

    if (expect_status(
            sl_arena_init(&string_arena, string_arena_storage, sizeof(string_arena_storage)),
            SL_STATUS_OK) != 0 ||
        expect_status(sl_string_builder_init_arena(&string_builder, &string_arena, 2U, 8U),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_string_builder_append_cstr(&string_builder, "ab"), SL_STATUS_OK) != 0)
    {
        return 42;
    }

    sl_arena_dispose(&string_arena);
    /* Arena-backed builder views are invalid after arena dispose; only status is checked. */
    if (expect_status(sl_string_builder_append_cstr(&string_builder, "cde"),
                      SL_STATUS_OUT_OF_MEMORY) != 0 ||
        expect_status(sl_string_builder_reserve(&string_builder, 3U), SL_STATUS_OUT_OF_MEMORY) != 0)
    {
        return 43;
    }

    return 0;
}

int main(void)
{
    int result = test_fixed_byte_builder();
    if (result != 0) {
        return result;
    }

    result = test_arena_byte_builder_growth_and_failures();
    if (result != 0) {
        return result;
    }

    result = test_small_builder_sso_contract();
    if (result != 0) {
        return result;
    }

    result = test_builder_stats_snapshot_contract();
    if (result != 0) {
        return result;
    }

    result = test_fixed_builder_self_overlap_append();
    if (result != 0) {
        return result;
    }

    result = test_string_builder_formatting_and_nul();
    if (result != 0) {
        return result;
    }

    result = test_string_builder_stats_forward_byte_builder_stats();
    if (result != 0) {
        return result;
    }

    result = test_string_builder_arena_growth_preserves_failed_prefix();
    if (result != 0) {
        return result;
    }

    result = test_builder_invalid_and_corrupt_state_failures();
    if (result != 0) {
        return result;
    }

    result = test_formatters_reject_invalid_arguments_atomically();
    if (result != 0) {
        return result;
    }

    return test_arena_builders_fail_cleanly_after_dispose();
}

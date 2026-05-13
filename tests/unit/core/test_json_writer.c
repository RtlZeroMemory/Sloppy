#include "sloppy/json_writer.h"

#include "sloppy/builder.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static int expect_bytes_equal(SlBytes actual, const char* expected)
{
    size_t expected_length = strlen(expected);

    return expect_true(actual.length == expected_length && actual.ptr != NULL &&
                       memcmp(actual.ptr, expected, expected_length) == 0);
}

static int test_writes_literals_strings_and_scalars(void)
{
    unsigned char storage[128];
    SlJsonWriter writer = {0};
    SlStatus status = sl_json_writer_init_fixed(&writer, storage, sizeof(storage));

    if (expect_status(status, SL_STATUS_OK) != 0) {
        return 1;
    }
    status = sl_json_writer_write_char(&writer, '{');
    if (sl_status_is_ok(status)) {
        status = sl_json_writer_write_str(&writer, sl_str_from_cstr("\"name\":"));
    }
    if (sl_status_is_ok(status)) {
        status = sl_json_writer_write_string(&writer, sl_str_from_cstr("Ada"));
    }
    if (sl_status_is_ok(status)) {
        status = sl_json_writer_write_str(&writer, sl_str_from_cstr(",\"count\":"));
    }
    if (sl_status_is_ok(status)) {
        status = sl_json_writer_write_i64(&writer, -42);
    }
    if (sl_status_is_ok(status)) {
        status = sl_json_writer_write_str(&writer, sl_str_from_cstr(",\"score\":"));
    }
    if (sl_status_is_ok(status)) {
        status = sl_json_writer_write_f64(&writer, 1.5);
    }
    if (sl_status_is_ok(status)) {
        status = sl_json_writer_write_char(&writer, '}');
    }

    return expect_status(status, SL_STATUS_OK) != 0 ||
                   expect_bytes_equal(sl_json_writer_view(&writer),
                                      "{\"name\":\"Ada\",\"count\":-42,\"score\":1.5}") != 0
               ? 2
               : 0;
}

static int test_escapes_required_json_string_bytes(void)
{
    unsigned char storage[128];
    SlJsonWriter writer = {0};
    SlStatus status = sl_json_writer_init_fixed(&writer, storage, sizeof(storage));

    if (expect_status(status, SL_STATUS_OK) != 0) {
        return 10;
    }
    status = sl_json_writer_write_string(&writer, sl_str_from_cstr("line\nquote\"slash\\\x01"));

    return expect_status(status, SL_STATUS_OK) != 0 ||
                   expect_bytes_equal(sl_json_writer_view(&writer),
                                      "\"line\\nquote\\\"slash\\\\\\u0001\"") != 0
               ? 11
               : 0;
}

static int test_shared_escaped_string_builder_and_length_match(void)
{
    const char expected[] = "\"line\\nquote\\\"slash\\\\\\b\\f\\u0002\"";
    const size_t expected_length = sizeof(expected) - 1U;
    char storage[128];
    SlStringBuilder builder = {0};
    SlStr text = sl_str_from_cstr("line\nquote\"slash\\\b\f\x02");
    size_t length = 0U;
    SlStatus status = sl_json_writer_escaped_string_length(text, &length);

    if (expect_status(status, SL_STATUS_OK) != 0) {
        return 80;
    }
    status = sl_string_builder_init_fixed(&builder, storage, sizeof(storage));
    if (expect_status(status, SL_STATUS_OK) != 0) {
        return 81;
    }
    status = sl_json_writer_append_escaped_string(&builder, text);

    return expect_status(status, SL_STATUS_OK) != 0 ||
                   expect_true(length == expected_length) != 0 ||
                   expect_true(sl_string_builder_view(&builder).length == expected_length) != 0 ||
                   expect_true(memcmp(storage, expected, expected_length) == 0) != 0
               ? 82
               : 0;
}

static int test_codepoint_control_string_escape_mode(void)
{
    const char expected[] = "\"line\\u000a\\u0009\\u000d\\u0008\\u000c\\u0002\"";
    const size_t expected_length = sizeof(expected) - 1U;
    char storage[128];
    SlStringBuilder builder = {0};
    SlStr text = sl_str_from_cstr("line\n\t\r\b\f\x02");
    size_t length = 0U;
    SlStatus status = sl_json_writer_escaped_string_codepoint_controls_length(text, &length);

    if (expect_status(status, SL_STATUS_OK) != 0 || expect_true(length == expected_length) != 0) {
        return 90;
    }
    status = sl_string_builder_init_fixed(&builder, storage, sizeof(storage));

    if (expect_status(status, SL_STATUS_OK) != 0) {
        return 91;
    }
    status = sl_json_writer_append_escaped_string_codepoint_controls(&builder, text);

    return expect_status(status, SL_STATUS_OK) != 0 ||
                   expect_true(sl_string_builder_view(&builder).length == expected_length) != 0 ||
                   expect_true(memcmp(storage, expected, expected_length) == 0) != 0
               ? 92
               : 0;
}

static int test_no_escape_string_uses_json_quotes_only(void)
{
    unsigned char storage[32];
    SlJsonWriter writer = {0};
    SlStatus status = sl_json_writer_init_fixed(&writer, storage, sizeof(storage));

    if (expect_status(status, SL_STATUS_OK) != 0) {
        return 30;
    }
    status = sl_json_writer_write_string(&writer, sl_str_from_cstr("plain_ascii_123"));

    return expect_status(status, SL_STATUS_OK) != 0 ||
                   expect_bytes_equal(sl_json_writer_view(&writer), "\"plain_ascii_123\"") != 0
               ? 31
               : 0;
}

static int test_utf8_string_bytes_pass_through(void)
{
    unsigned char storage[32];
    const char utf8_text[] = "caf\xc3\xa9";
    SlJsonWriter writer = {0};
    SlStatus status = sl_json_writer_init_fixed(&writer, storage, sizeof(storage));

    if (expect_status(status, SL_STATUS_OK) != 0) {
        return 40;
    }
    status =
        sl_json_writer_write_string(&writer, sl_str_from_parts(utf8_text, sizeof(utf8_text) - 1U));

    return expect_status(status, SL_STATUS_OK) != 0 ||
                   expect_bytes_equal(sl_json_writer_view(&writer), "\"caf\xc3\xa9\"") != 0
               ? 41
               : 0;
}

static int test_capacity_failure_preserves_prefix(void)
{
    unsigned char storage[4];
    SlJsonWriter writer = {0};
    SlStatus status = sl_json_writer_init_fixed(&writer, storage, sizeof(storage));

    if (expect_status(status, SL_STATUS_OK) != 0) {
        return 20;
    }
    status = sl_json_writer_write_str(&writer, sl_str_from_cstr("abcd"));
    if (expect_status(status, SL_STATUS_OK) != 0 ||
        expect_bytes_equal(sl_json_writer_view(&writer), "abcd") != 0)
    {
        return 21;
    }
    status = sl_json_writer_write_char(&writer, 'e');

    return expect_status(status, SL_STATUS_CAPACITY_EXCEEDED) != 0 ||
                   expect_bytes_equal(sl_json_writer_view(&writer), "abcd") != 0
               ? 22
               : 0;
}

static int test_i64_min_and_max_are_formatted(void)
{
    unsigned char storage[64];
    SlJsonWriter writer = {0};
    SlStatus status = sl_json_writer_init_fixed(&writer, storage, sizeof(storage));

    if (expect_status(status, SL_STATUS_OK) != 0) {
        return 50;
    }
    status = sl_json_writer_write_i64(&writer, INT64_MIN);
    if (sl_status_is_ok(status)) {
        status = sl_json_writer_write_char(&writer, ',');
    }
    if (sl_status_is_ok(status)) {
        status = sl_json_writer_write_i64(&writer, INT64_MAX);
    }

    return expect_status(status, SL_STATUS_OK) != 0 ||
                   expect_bytes_equal(sl_json_writer_view(&writer),
                                      "-9223372036854775808,9223372036854775807") != 0
               ? 51
               : 0;
}

static int test_f64_rejects_non_finite_values(void)
{
    unsigned char storage[64];
    double nan_value = strtod("nan", NULL);
    double inf_value = strtod("inf", NULL);
    SlJsonWriter writer = {0};
    SlStatus status = sl_json_writer_init_fixed(&writer, storage, sizeof(storage));

    if (expect_status(status, SL_STATUS_OK) != 0) {
        return 60;
    }
    status = sl_json_writer_write_f64(&writer, 12.25);
    if (expect_status(status, SL_STATUS_OK) != 0 ||
        expect_bytes_equal(sl_json_writer_view(&writer), "12.25") != 0)
    {
        return 61;
    }
    status = sl_json_writer_write_f64(&writer, nan_value);
    if (expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
        expect_bytes_equal(sl_json_writer_view(&writer), "12.25") != 0)
    {
        return 62;
    }
    status = sl_json_writer_write_f64(&writer, inf_value);
    return expect_status(status, SL_STATUS_INVALID_ARGUMENT) != 0 ||
                   expect_bytes_equal(sl_json_writer_view(&writer), "12.25") != 0
               ? 63
               : 0;
}

static int test_zero_capacity_writer_rejects_non_empty_writes(void)
{
    SlJsonWriter writer = {0};
    SlBytes view = {0};
    SlStatus status = sl_json_writer_init_fixed(&writer, NULL, 0U);

    if (expect_status(status, SL_STATUS_OK) != 0) {
        return 70;
    }
    view = sl_json_writer_view(&writer);
    if (view.ptr != NULL || view.length != 0U) {
        return 71;
    }
    status = sl_json_writer_write_bytes(&writer, sl_bytes_from_parts(NULL, 0U));
    if (expect_status(status, SL_STATUS_OK) != 0) {
        return 72;
    }
    status = sl_json_writer_write_char(&writer, 'x');
    if (expect_status(status, SL_STATUS_CAPACITY_EXCEEDED) != 0) {
        return 73;
    }
    view = sl_json_writer_view(&writer);
    return view.ptr != NULL || view.length != 0U ? 74 : 0;
}

int main(void)
{
    int (*tests[])(void) = {test_writes_literals_strings_and_scalars,
                            test_escapes_required_json_string_bytes,
                            test_shared_escaped_string_builder_and_length_match,
                            test_codepoint_control_string_escape_mode,
                            test_no_escape_string_uses_json_quotes_only,
                            test_utf8_string_bytes_pass_through,
                            test_capacity_failure_preserves_prefix,
                            test_i64_min_and_max_are_formatted,
                            test_f64_rejects_non_finite_values,
                            test_zero_capacity_writer_rejects_non_empty_writes};
    size_t index = 0U;

    for (index = 0U; index < sizeof(tests) / sizeof(tests[0]); index += 1U) {
        int result = tests[index]();
        if (result != 0) {
            return result;
        }
    }
    return 0;
}

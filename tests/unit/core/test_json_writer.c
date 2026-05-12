#include "sloppy/json_writer.h"

#include <stdbool.h>
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

int main(void)
{
    int (*tests[])(void) = {test_writes_literals_strings_and_scalars,
                            test_escapes_required_json_string_bytes,
                            test_capacity_failure_preserves_prefix};
    size_t index = 0U;

    for (index = 0U; index < sizeof(tests) / sizeof(tests[0]); index += 1U) {
        int result = tests[index]();
        if (result != 0) {
            return result;
        }
    }
    return 0;
}

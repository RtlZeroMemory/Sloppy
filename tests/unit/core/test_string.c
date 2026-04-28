#include "sloppy/string.h"

#include <stdbool.h>
#include <stddef.h>

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

int main(void)
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

    if (expect_true(sl_str_is_empty(empty)) != 0) {
        return 1;
    }

    if (expect_true(sl_str_equal(empty, from_null_cstr)) != 0) {
        return 2;
    }

    if (expect_true(from_cstr.length == 5U) != 0) {
        return 3;
    }

    if (expect_true(sl_str_equal(embedded_str, same_embedded_str)) != 0) {
        return 4;
    }

    if (expect_true(!sl_str_equal(embedded_str, different_str)) != 0) {
        return 5;
    }

    if (expect_true(!sl_str_equal(sl_str_from_parts(NULL, 3U), sl_str_from_parts(NULL, 3U))) != 0) {
        return 6;
    }

    if (expect_true(sl_str_starts_with(embedded_str, prefix)) != 0) {
        return 7;
    }

    if (expect_true(sl_str_starts_with(embedded_str, empty)) != 0) {
        return 8;
    }

    return 0;
}

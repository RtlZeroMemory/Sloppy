#include "sloppy/bytes.h"

#include <stdbool.h>
#include <stddef.h>

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

int main(void)
{
    const unsigned char bytes[] = {0U, 1U, 2U, 0U};
    const unsigned char same[] = {0U, 1U, 2U, 0U};
    const unsigned char different[] = {0U, 1U, 3U, 0U};
    SlBytes empty = sl_bytes_empty();
    SlBytes view = sl_bytes_from_parts(bytes, sizeof(bytes));
    SlBytes same_view = sl_bytes_from_parts(same, sizeof(same));
    SlBytes different_view = sl_bytes_from_parts(different, sizeof(different));

    if (expect_true(sl_bytes_is_empty(empty)) != 0) {
        return 1;
    }

    if (expect_true(sl_bytes_equal(empty, sl_bytes_from_parts(NULL, 0U))) != 0) {
        return 2;
    }

    if (expect_true(sl_bytes_equal(view, same_view)) != 0) {
        return 3;
    }

    if (expect_true(!sl_bytes_equal(view, different_view)) != 0) {
        return 4;
    }

    if (expect_true(!sl_bytes_equal(view, empty)) != 0) {
        return 5;
    }

    return 0;
}

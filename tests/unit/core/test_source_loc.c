#include "sloppy/source_loc.h"

#include <stdbool.h>
#include <stddef.h>

static SlSourceLoc capture_current(void)
{
    return SL_SOURCE_LOC_CURRENT;
}

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int test_partial_fields_are_not_unknown(void)
{
    const SlSourceLoc file_only = {"file.c", 0U, NULL};
    const SlSourceLoc line_only = {NULL, 42U, NULL};
    const SlSourceLoc function_only = {NULL, 0U, "fn"};
    const SlSourceLoc mixed = {"file.c", 7U, NULL};

    if (expect_true(!sl_source_loc_is_unknown(file_only)) != 0 ||
        expect_true(!sl_source_loc_is_unknown(line_only)) != 0 ||
        expect_true(!sl_source_loc_is_unknown(function_only)) != 0 ||
        expect_true(!sl_source_loc_is_unknown(mixed)) != 0)
    {
        return 1;
    }

    return 0;
}

int main(void)
{
    SlSourceLoc unknown = sl_source_loc_unknown();
    SlSourceLoc current = capture_current();

    if (expect_true(sl_source_loc_is_unknown(unknown)) != 0) {
        return 10;
    }

    if (expect_true(!sl_source_loc_is_unknown(current)) != 0) {
        return 11;
    }

    if (expect_true(current.file != NULL) != 0) {
        return 12;
    }

    if (expect_true(current.line != 0U) != 0) {
        return 13;
    }

    if (expect_true(current.function != NULL) != 0) {
        return 14;
    }

    return test_partial_fields_are_not_unknown();
}

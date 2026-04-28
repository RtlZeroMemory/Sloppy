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

int main(void)
{
    SlSourceLoc unknown = sl_source_loc_unknown();
    SlSourceLoc current = capture_current();

    if (expect_true(sl_source_loc_is_unknown(unknown)) != 0) {
        return 1;
    }

    if (expect_true(!sl_source_loc_is_unknown(current)) != 0) {
        return 2;
    }

    if (expect_true(current.file != NULL) != 0) {
        return 3;
    }

    if (expect_true(current.line != 0U) != 0) {
        return 4;
    }

    if (expect_true(current.function != NULL) != 0) {
        return 5;
    }

    return 0;
}

#include "sloppy/alloc.h"

#include <stddef.h>

static int expect_true(int condition)
{
    return condition ? 0 : 1;
}

int main(void)
{
    void* ptr = NULL;
    SlStatus status = sl_status_ok();

    status = sl_alloc_bytes(0U, &ptr);
    if (expect_true(sl_status_code(status) == SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 1;
    }
    if (expect_true(ptr == NULL) != 0) {
        return 2;
    }

    status = sl_alloc_bytes(1U, NULL);
    if (expect_true(sl_status_code(status) == SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 3;
    }

    status = sl_alloc_bytes(32U, &ptr);
    if (expect_true(sl_status_is_ok(status)) != 0) {
        return 4;
    }
    if (expect_true(ptr != NULL) != 0) {
        return 5;
    }

    sl_alloc_release(ptr);
    sl_alloc_release(NULL);
    return 0;
}

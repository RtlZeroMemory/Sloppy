/*
 * src/core/bytes.c
 *
 * Implements borrowed byte-view helpers. SlBytes never owns memory and accepts zero-length
 * views without requiring a non-NULL pointer.
 *
 * Tests: tests/unit/core/test_bytes.c.
 */
#include "sloppy/bytes.h"

#include <string.h>

SlBytes sl_bytes_from_parts(const unsigned char* ptr, size_t length)
{
    SlBytes bytes = {ptr, length};
    return bytes;
}

SlBytes sl_bytes_empty(void)
{
    SlBytes bytes = {NULL, 0U};
    return bytes;
}

bool sl_bytes_is_empty(SlBytes bytes)
{
    return bytes.length == 0U;
}

bool sl_bytes_equal(SlBytes left, SlBytes right)
{
    if (left.length != right.length) {
        return false;
    }

    if (left.length == 0U) {
        return true;
    }

    if (left.ptr == NULL || right.ptr == NULL) {
        return false;
    }

    if (left.ptr == right.ptr) {
        return true;
    }

    return memcmp(left.ptr, right.ptr, left.length) == 0;
}

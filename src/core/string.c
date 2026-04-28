/*
 * src/core/string.c
 *
 * Implements borrowed string-view helpers. SlStr never owns memory and never assumes NUL
 * termination except in the explicit C-string boundary adapter.
 *
 * Tests: tests/unit/core/test_string.c.
 */
#include "sloppy/string.h"

#include <string.h>

SlStr sl_str_from_parts(const char* ptr, size_t length)
{
    SlStr str = {ptr, length};
    return str;
}

SlStr sl_str_from_cstr(const char* cstr)
{
    if (cstr == NULL) {
        return sl_str_empty();
    }

    return sl_str_from_parts(cstr, strlen(cstr));
}

SlStr sl_str_empty(void)
{
    SlStr str = {NULL, 0U};
    return str;
}

bool sl_str_is_empty(SlStr str)
{
    return str.length == 0U;
}

bool sl_str_equal(SlStr left, SlStr right)
{
    if (left.length != right.length) {
        return false;
    }

    if (left.length == 0U) {
        return true;
    }

    if (left.ptr == right.ptr) {
        return true;
    }

    if (left.ptr == NULL || right.ptr == NULL) {
        return false;
    }

    return memcmp(left.ptr, right.ptr, left.length) == 0;
}

bool sl_str_starts_with(SlStr str, SlStr prefix)
{
    if (prefix.length == 0U) {
        return true;
    }

    if (str.length < prefix.length || str.ptr == NULL || prefix.ptr == NULL) {
        return false;
    }

    return memcmp(str.ptr, prefix.ptr, prefix.length) == 0;
}

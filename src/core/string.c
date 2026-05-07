/*
 * src/core/string.c
 *
 * Implements borrowed string-view helpers. SlStr never owns memory and never assumes NUL
 * termination except in the explicit C-string boundary adapter.
 *
 * Tests: tests/unit/core/test_string.c.
 */
#include "sloppy/string.h"

#include "sloppy/checked_math.h"

#include <string.h>

#define SL_STR_FNV1A_OFFSET 14695981039346656037ULL
#define SL_STR_FNV1A_PRIME 1099511628211ULL

static bool sl_str_has_valid_storage(SlStr str)
{
    return str.length == 0U || str.ptr != NULL;
}

static unsigned char sl_str_ascii_lower(unsigned char ch)
{
    if (ch >= (unsigned char)'A' && ch <= (unsigned char)'Z') {
        return (unsigned char)(ch - (unsigned char)'A' + (unsigned char)'a');
    }
    return ch;
}

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

    if (left.ptr == NULL || right.ptr == NULL) {
        return false;
    }

    if (left.ptr == right.ptr) {
        return true;
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

bool sl_str_ends_with(SlStr str, SlStr suffix)
{
    size_t offset = 0U;

    if (suffix.length == 0U) {
        return true;
    }

    if (str.length < suffix.length || str.ptr == NULL || suffix.ptr == NULL) {
        return false;
    }

    offset = str.length - suffix.length;
    return memcmp(str.ptr + offset, suffix.ptr, suffix.length) == 0;
}

bool sl_str_equal_ci_ascii(SlStr left, SlStr right)
{
    size_t index = 0U;

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

    for (index = 0U; index < left.length; index += 1U) {
        if (sl_str_ascii_lower((unsigned char)left.ptr[index]) !=
            sl_str_ascii_lower((unsigned char)right.ptr[index]))
        {
            return false;
        }
    }
    return true;
}

bool sl_str_starts_with_ci_ascii(SlStr str, SlStr prefix)
{
    size_t index = 0U;

    if (prefix.length == 0U) {
        return true;
    }
    if (str.length < prefix.length || str.ptr == NULL || prefix.ptr == NULL) {
        return false;
    }

    for (index = 0U; index < prefix.length; index += 1U) {
        if (sl_str_ascii_lower((unsigned char)str.ptr[index]) !=
            sl_str_ascii_lower((unsigned char)prefix.ptr[index]))
        {
            return false;
        }
    }
    return true;
}

bool sl_str_ends_with_ci_ascii(SlStr str, SlStr suffix)
{
    size_t offset = 0U;
    size_t index = 0U;

    if (suffix.length == 0U) {
        return true;
    }
    if (str.length < suffix.length || str.ptr == NULL || suffix.ptr == NULL) {
        return false;
    }

    offset = str.length - suffix.length;
    for (index = 0U; index < suffix.length; index += 1U) {
        if (sl_str_ascii_lower((unsigned char)str.ptr[offset + index]) !=
            sl_str_ascii_lower((unsigned char)suffix.ptr[index]))
        {
            return false;
        }
    }
    return true;
}

bool sl_str_contains_nul(SlStr str)
{
    size_t index = 0U;

    if (!sl_str_has_valid_storage(str)) {
        return false;
    }

    for (index = 0U; index < str.length; index += 1U) {
        if (str.ptr[index] == '\0') {
            return true;
        }
    }
    return false;
}

SlStr sl_owned_str_as_view(SlOwnedStr str)
{
    return sl_str_from_parts(str.ptr, str.length);
}

SlStatus sl_str_hash(SlStr str, uint64_t* out_hash)
{
    uint64_t hash = SL_STR_FNV1A_OFFSET;
    size_t index = 0U;

    if (out_hash == NULL || !sl_str_has_valid_storage(str)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    for (index = 0U; index < str.length; index += 1U) {
        hash ^= (uint64_t)(unsigned char)str.ptr[index];
        hash *= SL_STR_FNV1A_PRIME;
    }

    *out_hash = hash;
    return sl_status_ok();
}

SlStatus sl_str_copy_to_arena(SlArena* arena, SlStr src, SlOwnedStr* out)
{
    void* copied = NULL;
    size_t index = 0U;
    SlOwnedStr result = {NULL, 0U};
    SlStatus status;

    if (arena == NULL || out == NULL || !sl_str_has_valid_storage(src)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (src.length == 0U) {
        *out = result;
        return sl_status_ok();
    }

    status = sl_arena_alloc(arena, src.length, _Alignof(char), &copied);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    for (index = 0U; index < src.length; index += 1U) {
        ((char*)copied)[index] = src.ptr[index];
    }
    result.ptr = (char*)copied;
    result.length = src.length;
    *out = result;
    return sl_status_ok();
}

SlStatus sl_str_validate_no_nul(SlStr str)
{
    if (!sl_str_has_valid_storage(str)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    return sl_str_contains_nul(str) ? sl_status_from_code(SL_STATUS_INVALID_ARGUMENT)
                                    : sl_status_ok();
}

SlStatus sl_str_copy_to_arena_nul(SlArena* arena, SlStr src, SlOwnedStr* out)
{
    size_t alloc_size = 0U;
    size_t index = 0U;
    void* copied = NULL;
    SlOwnedStr result = {NULL, 0U};
    SlStatus status;

    if (arena == NULL || out == NULL || !sl_str_has_valid_storage(src)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_checked_add_size(src.length, 1U, &alloc_size);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_arena_alloc(arena, alloc_size, _Alignof(char), &copied);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    for (index = 0U; index < src.length; index += 1U) {
        ((char*)copied)[index] = src.ptr[index];
    }
    ((char*)copied)[src.length] = '\0';

    result.ptr = (char*)copied;
    result.length = src.length;
    *out = result;
    return sl_status_ok();
}

SlStatus sl_str_copy_to_arena_cstr(SlArena* arena, SlStr src, SlOwnedStr* out)
{
    SlStatus status;

    status = sl_str_validate_no_nul(src);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    return sl_str_copy_to_arena_nul(arena, src, out);
}

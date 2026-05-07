/*
 * src/core/bytes.c
 *
 * Implements borrowed byte-view helpers. SlBytes never owns memory and accepts zero-length
 * views without requiring a non-NULL pointer.
 *
 * Tests: tests/unit/core/test_bytes.c.
 */
#include "sloppy/bytes.h"

#include "bytes_internal.h"

#include <string.h>

#define SL_BYTES_FNV1A_OFFSET 14695981039346656037ULL
#define SL_BYTES_FNV1A_PRIME 1099511628211ULL

static bool sl_bytes_has_valid_storage(SlBytes bytes)
{
    return bytes.length == 0U || bytes.ptr != NULL;
}

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

bool sl_bytes_starts_with(SlBytes bytes, SlBytes prefix)
{
    if (prefix.length == 0U) {
        return true;
    }

    if (bytes.length < prefix.length || bytes.ptr == NULL || prefix.ptr == NULL) {
        return false;
    }

    return memcmp(bytes.ptr, prefix.ptr, prefix.length) == 0;
}

bool sl_bytes_ends_with(SlBytes bytes, SlBytes suffix)
{
    size_t offset = 0U;

    if (suffix.length == 0U) {
        return true;
    }

    if (bytes.length < suffix.length || bytes.ptr == NULL || suffix.ptr == NULL) {
        return false;
    }

    offset = bytes.length - suffix.length;
    return memcmp(bytes.ptr + offset, suffix.ptr, suffix.length) == 0;
}

SlBytes sl_owned_bytes_as_view(SlOwnedBytes bytes)
{
    return sl_bytes_from_parts(bytes.ptr, bytes.length);
}

SlStatus sl_bytes_find_scalar(SlBytes bytes, unsigned char needle, SlBytesFindResult* out)
{
    size_t index = 0U;
    SlBytesFindResult result = {.found = false, .index = bytes.length, .value = 0U};

    if (out == NULL || !sl_bytes_has_valid_storage(bytes)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    for (index = 0U; index < bytes.length; index += 1U) {
        if (bytes.ptr[index] == needle) {
            result.found = true;
            result.index = index;
            result.value = needle;
            break;
        }
    }

    *out = result;
    return sl_status_ok();
}

SlStatus sl_bytes_find(SlBytes bytes, unsigned char needle, SlBytesFindResult* out)
{
    if (out == NULL || !sl_bytes_has_valid_storage(bytes)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

#if SL_BYTES_SIMD_AVX2
    return sl_bytes_find_avx2(bytes, needle, out);
#elif SL_BYTES_SIMD_SSE2
    return sl_bytes_find_sse2(bytes, needle, out);
#else
    return sl_bytes_find_scalar(bytes, needle, out);
#endif
}

SlStatus sl_bytes_find_any_scalar(SlBytes bytes, SlBytes needles, SlBytesFindResult* out)
{
    size_t index = 0U;
    size_t needle_index = 0U;
    SlBytesFindResult result = {.found = false, .index = bytes.length, .value = 0U};

    if (out == NULL || !sl_bytes_has_valid_storage(bytes) || !sl_bytes_has_valid_storage(needles)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (needles.length == 0U) {
        *out = result;
        return sl_status_ok();
    }

    for (index = 0U; index < bytes.length; index += 1U) {
        for (needle_index = 0U; needle_index < needles.length; needle_index += 1U) {
            if (bytes.ptr[index] == needles.ptr[needle_index]) {
                result.found = true;
                result.index = index;
                result.value = bytes.ptr[index];
                *out = result;
                return sl_status_ok();
            }
        }
    }

    *out = result;
    return sl_status_ok();
}

SlStatus sl_bytes_find_any(SlBytes bytes, SlBytes needles, SlBytesFindResult* out)
{
    if (out == NULL || !sl_bytes_has_valid_storage(bytes) || !sl_bytes_has_valid_storage(needles)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

#if SL_BYTES_SIMD_AVX2
    return sl_bytes_find_any_avx2(bytes, needles, out);
#elif SL_BYTES_SIMD_SSE2
    return sl_bytes_find_any_sse2(bytes, needles, out);
#else
    return sl_bytes_find_any_scalar(bytes, needles, out);
#endif
}

SlStatus sl_bytes_hash(SlBytes bytes, uint64_t* out_hash)
{
    uint64_t hash = SL_BYTES_FNV1A_OFFSET;
    size_t index = 0U;

    if (out_hash == NULL || !sl_bytes_has_valid_storage(bytes)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    for (index = 0U; index < bytes.length; index += 1U) {
        hash ^= (uint64_t)bytes.ptr[index];
        hash *= SL_BYTES_FNV1A_PRIME;
    }

    *out_hash = hash;
    return sl_status_ok();
}

SlStatus sl_bytes_copy_to_arena(SlArena* arena, SlBytes src, SlOwnedBytes* out)
{
    void* copied = NULL;
    size_t index = 0U;
    SlOwnedBytes result = {NULL, 0U};
    SlStatus status;

    if (arena == NULL || out == NULL || !sl_bytes_has_valid_storage(src)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (src.length == 0U) {
        *out = result;
        return sl_status_ok();
    }

    status = sl_arena_alloc(arena, src.length, _Alignof(unsigned char), &copied);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    for (index = 0U; index < src.length; index += 1U) {
        ((unsigned char*)copied)[index] = src.ptr[index];
    }
    result.ptr = (unsigned char*)copied;
    result.length = src.length;
    *out = result;
    return sl_status_ok();
}

/*
 * src/core/string.c
 *
 * Implements borrowed string-view helpers. SlStr never owns memory and never assumes NUL
 * termination except in the explicit C-string boundary adapter.
 *
 * Tests: tests/unit/core/test_string.c.
 */
#include "sloppy/string.h"

#include "string_internal.h"

#include "sloppy/checked_math.h"

#define SL_STR_FNV1A_OFFSET 14695981039346656037ULL
#define SL_STR_FNV1A_PRIME 1099511628211ULL
#define SL_STR_WORD_ALIGNMENT _Alignof(uint64_t)
#define SL_STR_WORD_SIZE sizeof(uint64_t)

static bool sl_str_has_valid_storage(SlStr str)
{
    return str.length == 0U || str.ptr != NULL;
}

static uint64_t sl_internal_load_u64(const char* restrict src)
{
    return ((uint64_t)(unsigned char)src[0]) | ((uint64_t)(unsigned char)src[1] << 8U) |
           ((uint64_t)(unsigned char)src[2] << 16U) | ((uint64_t)(unsigned char)src[3] << 24U) |
           ((uint64_t)(unsigned char)src[4] << 32U) | ((uint64_t)(unsigned char)src[5] << 40U) |
           ((uint64_t)(unsigned char)src[6] << 48U) | ((uint64_t)(unsigned char)src[7] << 56U);
}

static void sl_internal_store_u64(char* restrict dest, uint64_t word)
{
    dest[0] = (char)(word & 0xffU);
    dest[1] = (char)((word >> 8U) & 0xffU);
    dest[2] = (char)((word >> 16U) & 0xffU);
    dest[3] = (char)((word >> 24U) & 0xffU);
    dest[4] = (char)((word >> 32U) & 0xffU);
    dest[5] = (char)((word >> 40U) & 0xffU);
    dest[6] = (char)((word >> 48U) & 0xffU);
    dest[7] = (char)((word >> 56U) & 0xffU);
}

static void sl_internal_copy(char* restrict dest, const char* restrict src, size_t length)
{
    size_t index = 0U;
    size_t word_count = 0U;
    size_t word_index = 0U;

    if (length == 0U || dest == src) {
        return;
    }

    word_count = length / SL_STR_WORD_SIZE;
    for (word_index = 0U; word_index < word_count; word_index += 1U) {
        sl_internal_store_u64(dest + index, sl_internal_load_u64(src + index));
        index += SL_STR_WORD_SIZE;
    }

    for (; index < length; index += 1U) {
        dest[index] = src[index];
    }
}

static int sl_internal_compare_word_bytes(const char* left, const char* right)
{
    size_t index = 0U;

    for (index = 0U; index < SL_STR_WORD_SIZE; index += 1U) {
        unsigned char left_byte = (unsigned char)left[index];
        unsigned char right_byte = (unsigned char)right[index];
        if (left_byte != right_byte) {
            return left_byte < right_byte ? -1 : 1;
        }
    }
    return 0;
}

static int sl_internal_compare(const char* left, const char* right, size_t length)
{
    size_t index = 0U;
    size_t word_count = 0U;
    size_t word_index = 0U;

    if (length == 0U || left == right) {
        return 0;
    }

    word_count = length / SL_STR_WORD_SIZE;
    for (word_index = 0U; word_index < word_count; word_index += 1U) {
        uint64_t left_word = sl_internal_load_u64(left + index);
        uint64_t right_word = sl_internal_load_u64(right + index);
        if (left_word != right_word) {
            return sl_internal_compare_word_bytes(left + index, right + index);
        }
        index += SL_STR_WORD_SIZE;
    }

    for (; index < length; index += 1U) {
        unsigned char left_byte = (unsigned char)left[index];
        unsigned char right_byte = (unsigned char)right[index];
        if (left_byte != right_byte) {
            return left_byte < right_byte ? -1 : 1;
        }
    }
    return 0;
}

static unsigned char sl_str_ascii_lower(unsigned char ch)
{
    unsigned int upper_delta = (unsigned int)ch - (unsigned int)(unsigned char)'A';
    unsigned char mask = (unsigned char)((upper_delta <= 25U) << 5U);
    return (unsigned char)(ch | mask);
}

static SlOwnedStr sl_owned_str_sso_result(SlStr src, bool append_nul)
{
    SlOwnedStr result = {0};

    result.length = src.length;
    result.is_sso = true;
    if (src.length != 0U) {
        sl_internal_copy(result.sso, src.ptr, src.length);
    }
    if (append_nul) {
        result.sso[src.length] = '\0';
    }
    return result;
}

static void sl_owned_str_publish(SlOwnedStr* restrict out, SlOwnedStr result)
{
    *out = result;
    sl_owned_str_rebind(out);
}

SlStr sl_str_from_parts(const char* ptr, size_t length)
{
    SlStr str = {ptr, length};
    return str;
}

SlStr sl_str_from_cstr(const char* cstr)
{
    size_t length = 0U;

    if (cstr == NULL) {
        return sl_str_empty();
    }

    while (cstr[length] != '\0') {
        length += 1U;
    }
    return sl_str_from_parts(cstr, length);
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

    return sl_internal_compare(left.ptr, right.ptr, left.length) == 0;
}

int sl_str_compare(SlStr left, SlStr right)
{
    size_t common = left.length < right.length ? left.length : right.length;
    int result = 0;

    if (!sl_str_has_valid_storage(left) || !sl_str_has_valid_storage(right)) {
        if (!sl_str_has_valid_storage(left) && !sl_str_has_valid_storage(right)) {
            if (left.length == right.length) {
                return 0;
            }
            return left.length < right.length ? -1 : 1;
        }
        return !sl_str_has_valid_storage(left) ? -1 : 1;
    }

    if (common != 0U) {
        result = sl_internal_compare(left.ptr, right.ptr, common);
        if (result != 0) {
            return result < 0 ? -1 : 1;
        }
    }

    if (left.length == right.length) {
        return 0;
    }
    return left.length < right.length ? -1 : 1;
}

bool sl_str_starts_with(SlStr str, SlStr prefix)
{
    if (prefix.length == 0U) {
        return true;
    }

    if (str.length < prefix.length || str.ptr == NULL || prefix.ptr == NULL) {
        return false;
    }

    return sl_internal_compare(str.ptr, prefix.ptr, prefix.length) == 0;
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
    return sl_internal_compare(str.ptr + offset, suffix.ptr, suffix.length) == 0;
}

bool sl_str_equal_ci_ascii_scalar(SlStr left, SlStr right)
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

bool sl_str_equal_ci_ascii(SlStr left, SlStr right)
{
#if SL_STRING_SIMD_AVX2
    return sl_str_equal_ci_ascii_avx2(left, right);
#elif SL_STRING_SIMD_SSE2
    return sl_str_equal_ci_ascii_sse2(left, right);
#else
    return sl_str_equal_ci_ascii_scalar(left, right);
#endif
}

bool sl_str_starts_with_ci_ascii(SlStr str, SlStr prefix)
{
    if (prefix.length == 0U) {
        return true;
    }
    if (str.length < prefix.length || str.ptr == NULL || prefix.ptr == NULL) {
        return false;
    }

    return sl_str_equal_ci_ascii(sl_str_from_parts(str.ptr, prefix.length), prefix);
}

bool sl_str_ends_with_ci_ascii(SlStr str, SlStr suffix)
{
    size_t offset = 0U;

    if (suffix.length == 0U) {
        return true;
    }
    if (str.length < suffix.length || str.ptr == NULL || suffix.ptr == NULL) {
        return false;
    }

    offset = str.length - suffix.length;
    return sl_str_equal_ci_ascii(sl_str_from_parts(str.ptr + offset, suffix.length), suffix);
}

bool sl_str_contains_nul_scalar(SlStr str)
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

bool sl_str_contains_nul(SlStr str)
{
    if (!sl_str_has_valid_storage(str)) {
        return false;
    }

#if SL_STRING_SIMD_AVX2
    return sl_str_contains_nul_avx2(str);
#elif SL_STRING_SIMD_SSE2
    return sl_str_contains_nul_sse2(str);
#else
    return sl_str_contains_nul_scalar(str);
#endif
}

SlStr sl_owned_str_as_view_ref(const SlOwnedStr* str)
{
    if (str == NULL) {
        return sl_str_empty();
    }
    return sl_str_from_parts(str->is_sso ? str->sso : str->ptr, str->length);
}

void sl_owned_str_rebind(SlOwnedStr* str)
{
    if (str != NULL && str->is_sso) {
        str->ptr = str->sso;
    }
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
    SlOwnedStr result = {0};
    SlStatus status;

    if (arena == NULL || out == NULL || !sl_str_has_valid_storage(src)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (src.length == 0U) {
        *out = result;
        return sl_status_ok();
    }

    if (src.length <= SL_OWNED_STR_SSO_MAX_LENGTH) {
        sl_owned_str_publish(out, sl_owned_str_sso_result(src, false));
        return sl_status_ok();
    }

    status = sl_arena_alloc(arena, src.length, SL_STR_WORD_ALIGNMENT, &copied);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    sl_internal_copy((char*)copied, src.ptr, src.length);
    result.ptr = (char*)copied;
    result.length = src.length;
    result.arena_generation = arena->generation;
    sl_owned_str_publish(out, result);
    return sl_status_ok();
}

SlStatus sl_str_copy_view_to_arena(SlArena* arena, SlStr src, SlStr* out)
{
    void* copied = NULL;
    SlStatus status;

    if (arena == NULL || out == NULL || !sl_str_has_valid_storage(src)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (src.length == 0U) {
        *out = sl_str_empty();
        return sl_status_ok();
    }

    status = sl_arena_alloc(arena, src.length, SL_STR_WORD_ALIGNMENT, &copied);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    sl_internal_copy((char*)copied, src.ptr, src.length);
    *out = sl_str_from_parts((char*)copied, src.length);
    return sl_status_ok();
}

SlStatus sl_str_concat_to_arena(SlArena* arena, SlStr left, SlStr right, SlOwnedStr* out)
{
    size_t length = 0U;
    void* copied = NULL;
    SlOwnedStr result = {0};
    SlStatus status;

    if (arena == NULL || out == NULL || !sl_str_has_valid_storage(left) ||
        !sl_str_has_valid_storage(right))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_checked_add_size(left.length, right.length, &length);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (length == 0U) {
        *out = result;
        return sl_status_ok();
    }

    if (length <= SL_OWNED_STR_SSO_MAX_LENGTH) {
        char combined[SL_OWNED_STR_SSO_CAPACITY] = {0};
        SlStr combined_view;

        if (left.length != 0U) {
            sl_internal_copy(combined, left.ptr, left.length);
        }
        if (right.length != 0U) {
            sl_internal_copy(combined + left.length, right.ptr, right.length);
        }
        combined_view = sl_str_from_parts(combined, length);
        sl_owned_str_publish(out, sl_owned_str_sso_result(combined_view, false));
        return sl_status_ok();
    }

    status = sl_arena_alloc(arena, length, SL_STR_WORD_ALIGNMENT, &copied);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    sl_internal_copy((char*)copied, left.ptr, left.length);
    sl_internal_copy((char*)copied + left.length, right.ptr, right.length);

    result.ptr = (char*)copied;
    result.length = length;
    result.arena_generation = arena->generation;
    sl_owned_str_publish(out, result);
    return sl_status_ok();
}

SlStatus sl_str_concat_view_to_arena(SlArena* arena, SlStr left, SlStr right, SlStr* out)
{
    size_t length = 0U;
    void* copied = NULL;
    SlStatus status;

    if (arena == NULL || out == NULL || !sl_str_has_valid_storage(left) ||
        !sl_str_has_valid_storage(right))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_checked_add_size(left.length, right.length, &length);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (length == 0U) {
        *out = sl_str_empty();
        return sl_status_ok();
    }

    status = sl_arena_alloc(arena, length, SL_STR_WORD_ALIGNMENT, &copied);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    sl_internal_copy((char*)copied, left.ptr, left.length);
    sl_internal_copy((char*)copied + left.length, right.ptr, right.length);
    *out = sl_str_from_parts((char*)copied, length);
    return sl_status_ok();
}

SlStatus sl_str_validate_no_nul(SlStr str)
{
    if (!sl_str_has_valid_storage(str)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    /* C-string boundary validation checks storage before using the scan predicate. */
    return sl_str_contains_nul(str) ? sl_status_from_code(SL_STATUS_INVALID_ARGUMENT)
                                    : sl_status_ok();
}

SlStatus sl_str_copy_to_arena_nul(SlArena* arena, SlStr src, SlOwnedStr* out)
{
    size_t alloc_size = 0U;
    void* copied = NULL;
    SlOwnedStr result = {0};
    SlStatus status;

    if (arena == NULL || out == NULL || !sl_str_has_valid_storage(src)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_checked_add_size(src.length, 1U, &alloc_size);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (src.length <= SL_OWNED_STR_SSO_MAX_LENGTH) {
        sl_owned_str_publish(out, sl_owned_str_sso_result(src, true));
        return sl_status_ok();
    }

    status = sl_arena_alloc(arena, alloc_size, SL_STR_WORD_ALIGNMENT, &copied);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    sl_internal_copy((char*)copied, src.ptr, src.length);
    ((char*)copied)[src.length] = '\0';

    result.ptr = (char*)copied;
    result.length = src.length;
    result.arena_generation = arena->generation;
    sl_owned_str_publish(out, result);
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

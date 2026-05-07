/*
 * src/core/builder.c
 *
 * Implements bounded byte and string builders for scoped runtime output.
 *
 * Builders either write into caller-owned fixed storage or allocate replacement buffers
 * from a caller-owned arena. They never allocate from hidden global state, never free arena
 * memory, and preserve already-written content after failed append/reserve calls.
 *
 * Tests: tests/unit/core/test_builder.c.
 */
#include "sloppy/builder.h"

#include "sloppy/checked_math.h"

#include <limits.h>
#include <stdint.h>

#define SL_BUILDER_MIN_GROW_CAPACITY 8U

static void sl_byte_builder_copy(unsigned char* dst, const unsigned char* src, size_t length)
{
    size_t index = 0U;
    uintptr_t dst_start = (uintptr_t)dst;
    uintptr_t src_start = (uintptr_t)src;
    uintptr_t dst_end = 0U;
    uintptr_t src_end = 0U;

    if (length == 0U || dst == src) {
        return;
    }

    if (dst_start <= UINTPTR_MAX - length && src_start <= UINTPTR_MAX - length) {
        dst_end = dst_start + length;
        src_end = src_start + length;
        if (dst_start > src_start && dst_start < src_end) {
            for (index = length; index > 0U; index -= 1U) {
                dst[index - 1U] = src[index - 1U];
            }
            return;
        }
        if (src_start > dst_start && src_start < dst_end) {
            for (index = 0U; index < length; index += 1U) {
                dst[index] = src[index];
            }
            return;
        }
    }

    for (index = 0U; index < length; index += 1U) {
        dst[index] = src[index];
    }
}

static void sl_builder_counter_add(size_t* counter, size_t amount)
{
    if (*counter > SIZE_MAX - amount) {
        *counter = SIZE_MAX;
        return;
    }

    *counter += amount;
}

static bool sl_byte_builder_is_initialized(const SlByteBuilder* builder)
{
    return builder != NULL && builder->storage != SL_BUILDER_STORAGE_INVALID;
}

static SlStatus sl_byte_builder_validate_span(SlBytes bytes)
{
    if (bytes.length != 0U && bytes.ptr == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    return sl_status_ok();
}

static SlStatus sl_byte_builder_next_capacity(const SlByteBuilder* builder, size_t required,
                                              size_t* out_capacity)
{
    size_t candidate = builder->capacity;
    SlStatus status;

    if (required > builder->max_capacity) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }

    if (candidate == 0U) {
        candidate = SL_BUILDER_MIN_GROW_CAPACITY;
    }
    if (candidate > builder->max_capacity) {
        candidate = builder->max_capacity;
    }

    while (candidate < required) {
        size_t doubled = 0U;

        status = sl_checked_mul_size(candidate, 2U, &doubled);
        if (!sl_status_is_ok(status)) {
            candidate = builder->max_capacity;
            break;
        }

        candidate = doubled;
        if (candidate > builder->max_capacity) {
            candidate = builder->max_capacity;
            break;
        }
    }

    if (candidate < required) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }

    *out_capacity = candidate;
    return sl_status_ok();
}

static SlStatus sl_byte_builder_grow(SlByteBuilder* builder, size_t required)
{
    size_t next_capacity = 0U;
    void* next_data = NULL;
    SlStatus status;

    if (builder->storage == SL_BUILDER_STORAGE_FIXED) {
        return sl_status_from_code(SL_STATUS_CAPACITY_EXCEEDED);
    }

    status = sl_byte_builder_next_capacity(builder, required, &next_capacity);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    status = sl_arena_alloc(builder->arena, next_capacity, _Alignof(unsigned char), &next_data);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (builder->length != 0U) {
        sl_byte_builder_copy((unsigned char*)next_data, builder->data, builder->length);
        sl_builder_counter_add(&builder->copied_bytes, builder->length);
    }

    builder->data = (unsigned char*)next_data;
    builder->capacity = next_capacity;
    sl_builder_counter_add(&builder->grow_count, 1U);
    return sl_status_ok();
}

SlStatus sl_byte_builder_init_fixed(SlByteBuilder* builder, unsigned char* buffer, size_t capacity)
{
    SlByteBuilder result = {0U};

    if (builder == NULL || (buffer == NULL && capacity != 0U)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    result.data = buffer;
    result.capacity = capacity;
    result.max_capacity = capacity;
    result.storage = SL_BUILDER_STORAGE_FIXED;
    *builder = result;
    return sl_status_ok();
}

SlStatus sl_byte_builder_init_arena(SlByteBuilder* builder, SlArena* arena, size_t initial_capacity,
                                    size_t max_capacity)
{
    SlByteBuilder result = {0U};
    void* initial_data = NULL;
    SlStatus status;

    if (builder == NULL || arena == NULL || initial_capacity > max_capacity) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (initial_capacity != 0U) {
        status = sl_arena_alloc(arena, initial_capacity, _Alignof(unsigned char), &initial_data);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    result.data = (unsigned char*)initial_data;
    result.capacity = initial_capacity;
    result.max_capacity = max_capacity;
    result.arena = arena;
    result.storage = SL_BUILDER_STORAGE_ARENA;
    *builder = result;
    return sl_status_ok();
}

void sl_byte_builder_reset(SlByteBuilder* builder)
{
    if (builder == NULL) {
        return;
    }

    builder->length = 0U;
}

size_t sl_byte_builder_length(const SlByteBuilder* builder)
{
    return builder == NULL ? 0U : builder->length;
}

size_t sl_byte_builder_capacity(const SlByteBuilder* builder)
{
    return builder == NULL ? 0U : builder->capacity;
}

SlByteBuilderStats sl_byte_builder_stats(const SlByteBuilder* builder)
{
    SlByteBuilderStats stats = {0U};

    if (builder == NULL) {
        return stats;
    }

    stats.length = builder->length;
    stats.capacity = builder->capacity;
    stats.max_capacity = builder->max_capacity;
    stats.grow_count = builder->grow_count;
    stats.copied_bytes = builder->copied_bytes;
    stats.appended_bytes = builder->appended_bytes;
    stats.failed_reserve_count = builder->failed_reserve_count;
    stats.storage = builder->storage;
    return stats;
}

SlBytes sl_byte_builder_view(const SlByteBuilder* builder)
{
    if (builder == NULL || builder->length == 0U) {
        return sl_bytes_empty();
    }

    return sl_bytes_from_parts(builder->data, builder->length);
}

SlStatus sl_byte_builder_reserve(SlByteBuilder* builder, size_t additional)
{
    size_t required = 0U;
    SlStatus status;

    if (!sl_byte_builder_is_initialized(builder)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_checked_add_size(builder->length, additional, &required);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (required <= builder->capacity) {
        return sl_status_ok();
    }

    status = sl_byte_builder_grow(builder, required);
    if (!sl_status_is_ok(status)) {
        sl_builder_counter_add(&builder->failed_reserve_count, 1U);
    }
    return status;
}

SlStatus sl_byte_builder_append_bytes(SlByteBuilder* builder, SlBytes bytes)
{
    SlStatus status;

    if (!sl_byte_builder_is_initialized(builder)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_byte_builder_validate_span(bytes);
    if (!sl_status_is_ok(status) || bytes.length == 0U) {
        return status;
    }

    status = sl_byte_builder_reserve(builder, bytes.length);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    sl_byte_builder_copy(builder->data + builder->length, bytes.ptr, bytes.length);
    builder->length += bytes.length;
    sl_builder_counter_add(&builder->appended_bytes, bytes.length);
    return sl_status_ok();
}

SlStatus sl_byte_builder_append_byte(SlByteBuilder* builder, unsigned char byte)
{
    SlStatus status;

    if (!sl_byte_builder_is_initialized(builder)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_byte_builder_reserve(builder, 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    builder->data[builder->length] = byte;
    builder->length += 1U;
    sl_builder_counter_add(&builder->appended_bytes, 1U);
    return sl_status_ok();
}

SlStatus sl_string_builder_init_fixed(SlStringBuilder* builder, char* buffer, size_t capacity)
{
    if (builder == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    return sl_byte_builder_init_fixed(&builder->bytes, (unsigned char*)buffer, capacity);
}

SlStatus sl_string_builder_init_arena(SlStringBuilder* builder, SlArena* arena,
                                      size_t initial_capacity, size_t max_capacity)
{
    if (builder == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    return sl_byte_builder_init_arena(&builder->bytes, arena, initial_capacity, max_capacity);
}

void sl_string_builder_reset(SlStringBuilder* builder)
{
    if (builder == NULL) {
        return;
    }

    sl_byte_builder_reset(&builder->bytes);
}

size_t sl_string_builder_length(const SlStringBuilder* builder)
{
    return builder == NULL ? 0U : sl_byte_builder_length(&builder->bytes);
}

size_t sl_string_builder_capacity(const SlStringBuilder* builder)
{
    return builder == NULL ? 0U : sl_byte_builder_capacity(&builder->bytes);
}

SlByteBuilderStats sl_string_builder_stats(const SlStringBuilder* builder)
{
    if (builder == NULL) {
        return sl_byte_builder_stats(NULL);
    }

    return sl_byte_builder_stats(&builder->bytes);
}

SlStr sl_string_builder_view(const SlStringBuilder* builder)
{
    SlBytes bytes;

    if (builder == NULL) {
        return sl_str_empty();
    }

    bytes = sl_byte_builder_view(&builder->bytes);
    return sl_str_from_parts((const char*)bytes.ptr, bytes.length);
}

SlStatus sl_string_builder_view_with_nul(SlStringBuilder* builder, SlStr* out)
{
    SlStatus status;

    if (builder == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_byte_builder_reserve(&builder->bytes, 1U);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    builder->bytes.data[builder->bytes.length] = '\0';
    *out = sl_string_builder_view(builder);
    return sl_status_ok();
}

SlStatus sl_string_builder_reserve(SlStringBuilder* builder, size_t additional)
{
    if (builder == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    return sl_byte_builder_reserve(&builder->bytes, additional);
}

SlStatus sl_string_builder_append_str(SlStringBuilder* builder, SlStr str)
{
    if (builder == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    return sl_byte_builder_append_bytes(
        &builder->bytes, sl_bytes_from_parts((const unsigned char*)str.ptr, str.length));
}

SlStatus sl_string_builder_append_cstr(SlStringBuilder* builder, const char* cstr)
{
    if (builder == NULL || cstr == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    return sl_string_builder_append_str(builder, sl_str_from_cstr(cstr));
}

SlStatus sl_string_builder_append_char(SlStringBuilder* builder, char value)
{
    if (builder == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    return sl_byte_builder_append_byte(&builder->bytes, (unsigned char)value);
}

SlStatus sl_string_builder_append_u64(SlStringBuilder* builder, uint64_t value)
{
    char digits[20];
    size_t length = 0U;
    size_t index = 0U;
    SlStatus status;

    if (builder == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    do {
        digits[length] = (char)('0' + (char)(value % 10U));
        value /= 10U;
        length += 1U;
    } while (value != 0U);

    status = sl_string_builder_reserve(builder, length);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    for (index = 0U; index < length; index += 1U) {
        builder->bytes.data[builder->bytes.length + index] =
            (unsigned char)digits[length - index - 1U];
    }

    builder->bytes.length += length;
    sl_builder_counter_add(&builder->bytes.appended_bytes, length);
    return sl_status_ok();
}

SlStatus sl_string_builder_append_i64(SlStringBuilder* builder, int64_t value)
{
    char digits[21];
    uint64_t magnitude = 0U;
    size_t length = 0U;
    size_t index = 0U;
    SlStatus status;

    if (builder == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (value < 0) {
        digits[length] = '-';
        length += 1U;
        magnitude = (uint64_t)(-(value + 1)) + 1U;
    }
    else {
        magnitude = (uint64_t)value;
    }

    do {
        digits[length] = (char)('0' + (char)(magnitude % 10U));
        magnitude /= 10U;
        length += 1U;
    } while (magnitude != 0U);

    status = sl_string_builder_reserve(builder, length);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    if (digits[0] == '-') {
        builder->bytes.data[builder->bytes.length] = (unsigned char)digits[0];
        for (index = 1U; index < length; index += 1U) {
            builder->bytes.data[builder->bytes.length + index] =
                (unsigned char)digits[length - index];
        }
    }
    else {
        for (index = 0U; index < length; index += 1U) {
            builder->bytes.data[builder->bytes.length + index] =
                (unsigned char)digits[length - index - 1U];
        }
    }

    builder->bytes.length += length;
    sl_builder_counter_add(&builder->bytes.appended_bytes, length);
    return sl_status_ok();
}

SlStatus sl_string_builder_append_size(SlStringBuilder* builder, size_t value)
{
    return sl_string_builder_append_u64(builder, (uint64_t)value);
}

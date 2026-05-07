#ifndef SLOPPY_BUILDER_H
#define SLOPPY_BUILDER_H

#include "sloppy/arena.h"
#include "sloppy/bytes.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SlBuilderStorageKind
{
    SL_BUILDER_STORAGE_INVALID = 0,
    SL_BUILDER_STORAGE_FIXED = 1,
    SL_BUILDER_STORAGE_ARENA = 2
} SlBuilderStorageKind;

/*
 * SlByteBuilder is a bounded mutable byte output target.
 *
 * Fixed builders write into caller-owned storage and never grow. Arena builders allocate
 * replacement buffers from a caller-supplied arena as they grow, up to `max_capacity`.
 * The builder never owns or frees an arena. Failed append/reserve calls leave the existing
 * builder contents valid and unchanged, so callers may keep using or viewing the prefix
 * already written. Appends may source bytes from the builder's current storage; self-overlap
 * preserves the source bytes as if copied through a temporary view.
 */
typedef struct SlByteBuilder
{
    unsigned char* data;
    size_t length;
    size_t capacity;
    size_t max_capacity;
    SlArena* arena;
    SlBuilderStorageKind storage;
} SlByteBuilder;

/*
 * SlStringBuilder is a bounded text builder over SlByteBuilder storage.
 *
 * It appends SlStr views by explicit length and never assumes source NUL termination.
 * Decimal formatting helpers are intentionally small and do not use sprintf-family APIs.
 */
typedef struct SlStringBuilder
{
    SlByteBuilder bytes;
} SlStringBuilder;

SlStatus sl_byte_builder_init_fixed(SlByteBuilder* builder, unsigned char* buffer, size_t capacity);
SlStatus sl_byte_builder_init_arena(SlByteBuilder* builder, SlArena* arena, size_t initial_capacity,
                                    size_t max_capacity);
void sl_byte_builder_reset(SlByteBuilder* builder);
size_t sl_byte_builder_length(const SlByteBuilder* builder);
size_t sl_byte_builder_capacity(const SlByteBuilder* builder);
SlBytes sl_byte_builder_view(const SlByteBuilder* builder);
SlStatus sl_byte_builder_reserve(SlByteBuilder* builder, size_t additional);
SlStatus sl_byte_builder_append_bytes(SlByteBuilder* builder, SlBytes bytes);
SlStatus sl_byte_builder_append_byte(SlByteBuilder* builder, unsigned char byte);

SlStatus sl_string_builder_init_fixed(SlStringBuilder* builder, char* buffer, size_t capacity);
SlStatus sl_string_builder_init_arena(SlStringBuilder* builder, SlArena* arena,
                                      size_t initial_capacity, size_t max_capacity);
void sl_string_builder_reset(SlStringBuilder* builder);
size_t sl_string_builder_length(const SlStringBuilder* builder);
size_t sl_string_builder_capacity(const SlStringBuilder* builder);
SlStr sl_string_builder_view(const SlStringBuilder* builder);
/*
 * Produces a view whose storage has a trailing NUL byte after the returned length.
 *
 * `builder` and `out` are required. NULL inputs return INVALID_ARGUMENT. If the builder
 * cannot reserve the terminator, the reserve failure is returned and `out` is unchanged.
 */
SlStatus sl_string_builder_view_with_nul(SlStringBuilder* builder, SlStr* out);
SlStatus sl_string_builder_reserve(SlStringBuilder* builder, size_t additional);
SlStatus sl_string_builder_append_str(SlStringBuilder* builder, SlStr str);
/*
 * Appends a boundary C string.
 *
 * `cstr` must be a valid NUL-terminated string. NULL or malformed builder inputs return
 * INVALID_ARGUMENT; reservation/allocation failures leave the builder prefix unchanged.
 */
SlStatus sl_string_builder_append_cstr(SlStringBuilder* builder, const char* cstr);
SlStatus sl_string_builder_append_char(SlStringBuilder* builder, char value);
SlStatus sl_string_builder_append_u64(SlStringBuilder* builder, uint64_t value);
SlStatus sl_string_builder_append_i64(SlStringBuilder* builder, int64_t value);
SlStatus sl_string_builder_append_size(SlStringBuilder* builder, size_t value);

#ifdef __cplusplus
}
#endif

#endif

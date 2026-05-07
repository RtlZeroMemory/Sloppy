#ifndef SLOPPY_STRING_H
#define SLOPPY_STRING_H

#include "sloppy/arena.h"
#include "sloppy/status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SlStr is a borrowed string view: pointer plus length.
 *
 * SlStr does not own memory, does not require NUL termination, and may contain embedded NUL
 * bytes. A non-empty view requires `ptr` to remain valid for `length` bytes for the caller's
 * documented lifetime.
 */
typedef struct SlStr
{
    const char* ptr;
    size_t length;
} SlStr;

/*
 * SlOwnedStr describes mutable bytes owned by the function/lifetime documented by the API
 * that produced it. The type itself does not free memory. For arena-copy helpers below,
 * `ptr` remains valid until the arena is reset or its backing storage ends.
 */
typedef struct SlOwnedStr
{
    char* ptr;
    size_t length;
} SlOwnedStr;

#define SL_STR_LITERAL(value) sl_str_from_parts((value), sizeof(value) - 1U)

SlStr sl_str_from_parts(const char* ptr, size_t length);

/*
 * Boundary adapter for valid C strings. `cstr` must point to a NUL-terminated string and
 * remain alive for the returned borrowed view. Passing NULL returns the empty view.
 */
SlStr sl_str_from_cstr(const char* cstr);

SlStr sl_str_empty(void);
bool sl_str_is_empty(SlStr str);
bool sl_str_equal(SlStr left, SlStr right);
bool sl_str_starts_with(SlStr str, SlStr prefix);
bool sl_str_ends_with(SlStr str, SlStr suffix);

/*
 * ASCII-only case-insensitive helpers for protocol grammars such as HTTP header
 * names and media types. Non-ASCII bytes compare by exact byte value.
 */
bool sl_str_equal_ci_ascii(SlStr left, SlStr right);
bool sl_str_starts_with_ci_ascii(SlStr str, SlStr prefix);
bool sl_str_ends_with_ci_ascii(SlStr str, SlStr suffix);

/*
 * Scans a valid string view for embedded NUL bytes. Malformed non-empty views with NULL
 * storage return false because there is no valid storage to scan. C-string boundaries must
 * call `sl_str_validate_no_nul` or `sl_str_copy_to_arena_cstr`, not this predicate alone.
 */
bool sl_str_contains_nul(SlStr str);

SlStr sl_owned_str_as_view(SlOwnedStr str);

/*
 * Hashes the exact bytes in `str` with a deterministic non-cryptographic hash.
 *
 * `out_hash` is required. Non-empty strings require a non-NULL pointer. Failed calls leave
 * `*out_hash` unchanged.
 */
SlStatus sl_str_hash(SlStr str, uint64_t* out_hash);

/*
 * Copies `src` into `arena` and writes an arena-owned string to `out`.
 *
 * `src` may be non-NUL-terminated and may contain embedded NUL bytes. Empty strings do not
 * allocate. On failure, `out` is left unchanged.
 */
SlStatus sl_str_copy_to_arena(SlArena* arena, SlStr src, SlOwnedStr* out);

/*
 * Validates that `str` can safely cross a C-string boundary.
 *
 * Empty strings are valid. Non-empty strings require non-NULL storage and must not contain
 * embedded NUL bytes. Binary-preserving SlStr users must not call this helper.
 */
SlStatus sl_str_validate_no_nul(SlStr str);

/*
 * Copies `src` into `arena`, appends a NUL byte, and writes the arena-owned string to
 * `out`.
 *
 * The returned length excludes the NUL terminator. This helper preserves embedded NUL bytes;
 * C-string boundaries should call `sl_str_copy_to_arena_cstr` instead. On failure, `out` is
 * left unchanged.
 */
SlStatus sl_str_copy_to_arena_nul(SlArena* arena, SlStr src, SlOwnedStr* out);

/*
 * Validates `src` for a C-string boundary, copies it into `arena`, appends the boundary NUL
 * byte, and writes the arena-owned string to `out`.
 *
 * The returned length excludes the NUL terminator. Embedded NUL bytes are rejected so C
 * APIs cannot silently truncate length-based SlStr values. On failure, `out` is left
 * unchanged.
 */
SlStatus sl_str_copy_to_arena_cstr(SlArena* arena, SlStr src, SlOwnedStr* out);

#ifdef __cplusplus
}
#endif

#endif

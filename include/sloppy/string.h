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

#define SL_OWNED_STR_SSO_CAPACITY 16U
#define SL_OWNED_STR_SSO_MAX_LENGTH (SL_OWNED_STR_SSO_CAPACITY - 1U)

/*
 * SlOwnedStr describes mutable bytes owned by the function/lifetime documented by the API
 * that produced it. The type itself does not free memory.
 *
 * Arena-copy helpers store strings up to SL_OWNED_STR_SSO_MAX_LENGTH bytes inline in
 * `sso` and set `is_sso`. Longer strings point into the arena and record the arena
 * generation that produced them. Arena-backed `ptr` values remain valid until the arena
 * is reset, reset to a mark that invalidates their offset range, disposed, or its backing
 * storage ends. Inline SSO strings remain valid for the SlOwnedStr object's lifetime.
 * Copying an SSO SlOwnedStr copies the bytes; call `sl_owned_str_as_view` on the copied
 * object to read from that object's inline buffer.
 */
typedef struct SlOwnedStr
{
    char* ptr;
    size_t length;
    bool is_sso;
    unsigned int arena_generation;
    char sso[SL_OWNED_STR_SSO_CAPACITY];
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
/*
 * Lexicographic byte ordering for SlStr views. Returns exactly -1 when `left` sorts
 * before `right`, 0 when equal, and 1 when `left` sorts after `right`; use <0, ==0,
 * or >0 checks at call sites. Embedded NUL and non-UTF-8 bytes compare by raw byte
 * value. Malformed non-empty NULL views sort before valid non-empty views, and this
 * borrowed-view helper does not take ownership or change either input lifetime.
 */
int sl_str_compare(SlStr left, SlStr right);
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

SlStr sl_owned_str_as_view_ref(const SlOwnedStr* str);
#define sl_owned_str_as_view(str) sl_owned_str_as_view_ref(&(str))
void sl_owned_str_rebind(SlOwnedStr* str);

/*
 * Returns true when `str` is empty or its full pointer range is inside `arena`'s backing
 * storage. This is a range check only; callers that retain arena-backed views must still
 * treat arena generation changes from reset/reset_to/dispose as invalidating events.
 */
bool sl_arena_contains_str(const SlArena* arena, SlStr str);

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
 * Copies `src` into `arena` and writes an arena-backed borrowed view to `out`.
 *
 * Unlike SlOwnedStr copies, this helper never uses inline SSO storage because the returned
 * SlStr has no owner object. Empty strings do not allocate. On failure, `out` is left
 * unchanged.
 */
SlStatus sl_str_copy_view_to_arena(SlArena* arena, SlStr src, SlStr* out);

/*
 * Concatenates two valid string views into arena-owned storage.
 *
 * Empty output does not allocate. Non-empty inputs require non-NULL storage. On failure,
 * `out` is left unchanged.
 */
SlStatus sl_str_concat_to_arena(SlArena* arena, SlStr left, SlStr right, SlOwnedStr* out);

/*
 * Concatenates two valid string views into arena-backed storage and writes a borrowed view.
 *
 * This helper never uses inline SSO storage because the returned SlStr has no owner object.
 * Empty output does not allocate. On failure, `out` is left unchanged.
 */
SlStatus sl_str_concat_view_to_arena(SlArena* arena, SlStr left, SlStr right, SlStr* out);

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

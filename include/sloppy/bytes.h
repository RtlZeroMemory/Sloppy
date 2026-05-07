#ifndef SLOPPY_BYTES_H
#define SLOPPY_BYTES_H

#include "sloppy/arena.h"
#include "sloppy/status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SlBytes is a borrowed byte view: pointer plus length.
 *
 * SlBytes does not own memory. A zero-length view may have a NULL pointer. A non-empty view
 * requires `ptr` to remain valid for `length` bytes for the caller's documented lifetime.
 */
typedef struct SlBytes
{
    const unsigned char* ptr;
    size_t length;
} SlBytes;

/*
 * SlOwnedBytes describes mutable bytes owned by the function/lifetime documented by the API
 * that produced it. The type itself does not free memory. For arena-copy helpers below,
 * `ptr` remains valid until the arena is reset or its backing storage ends.
 */
typedef struct SlOwnedBytes
{
    unsigned char* ptr;
    size_t length;
} SlOwnedBytes;

typedef struct SlBytesFindResult
{
    bool found;
    size_t index;
    unsigned char value;
} SlBytesFindResult;

SlBytes sl_bytes_from_parts(const unsigned char* ptr, size_t length);
SlBytes sl_bytes_empty(void);
bool sl_bytes_is_empty(SlBytes bytes);
bool sl_bytes_equal(SlBytes left, SlBytes right);
bool sl_bytes_starts_with(SlBytes bytes, SlBytes prefix);
bool sl_bytes_ends_with(SlBytes bytes, SlBytes suffix);
SlBytes sl_owned_bytes_as_view(SlOwnedBytes bytes);
SlStatus sl_bytes_find(SlBytes bytes, unsigned char needle, SlBytesFindResult* out);
SlStatus sl_bytes_find_any(SlBytes bytes, SlBytes needles, SlBytesFindResult* out);

/*
 * Hashes the exact bytes in `bytes` with a deterministic non-cryptographic hash.
 *
 * `out_hash` is required. Non-empty spans require a non-NULL pointer. Failed calls leave
 * `*out_hash` unchanged.
 */
SlStatus sl_bytes_hash(SlBytes bytes, uint64_t* out_hash);

/*
 * Copies `src` into `arena` and writes an arena-owned byte buffer to `out`.
 *
 * Empty spans do not allocate. Binary data, including embedded zero bytes, is copied
 * exactly. On failure, `out` is left unchanged.
 */
SlStatus sl_bytes_copy_to_arena(SlArena* arena, SlBytes src, SlOwnedBytes* out);

#ifdef __cplusplus
}
#endif

#endif

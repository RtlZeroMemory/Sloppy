#ifndef SLOPPY_BYTES_H
#define SLOPPY_BYTES_H

#include <stdbool.h>
#include <stddef.h>

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

SlBytes sl_bytes_from_parts(const unsigned char* ptr, size_t length);
SlBytes sl_bytes_empty(void);
bool sl_bytes_is_empty(SlBytes bytes);
bool sl_bytes_equal(SlBytes left, SlBytes right);

#ifdef __cplusplus
}
#endif

#endif

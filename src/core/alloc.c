/*
 * src/core/alloc.c
 *
 * Centralizes Sloppy's narrow heap allocation boundary for independently owned
 * byte buffers that cannot be caller-backed arena storage.
 */
#include "sloppy/alloc.h"

#include <stdlib.h>

SlStatus sl_alloc_bytes(size_t size, unsigned char** out)
{
    if (out == NULL || size == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out = malloc(size);
    if (*out == NULL) {
        return sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
    }
    return sl_status_ok();
}

void sl_alloc_release(void* ptr)
{
    free(ptr);
}

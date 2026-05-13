/*
 * src/core/alloc.c
 *
 * Centralizes Sloppy's narrow heap allocation boundary for independently owned
 * byte buffers that cannot use fixed caller-backed arena storage because the
 * required capacity is runtime-configured.
 */
#include "sloppy/alloc.h"

#include <stdlib.h>

SlStatus sl_alloc_bytes(size_t size, unsigned char** out)
{
    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = NULL;
    if (size == 0U) {
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

SlStatus sl_heap_buffer_alloc(SlHeapBuffer* out, size_t length)
{
    void* ptr = NULL;

    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlHeapBuffer){0};
    if (length == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    ptr = malloc(length);
    if (ptr == NULL) {
        return sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
    }

    out->ptr = (unsigned char*)ptr;
    out->length = length;
    return sl_status_ok();
}

void sl_heap_buffer_dispose(SlHeapBuffer* buffer)
{
    if (buffer == NULL) {
        return;
    }

    free(buffer->ptr);
    buffer->ptr = NULL;
    buffer->length = 0U;
}

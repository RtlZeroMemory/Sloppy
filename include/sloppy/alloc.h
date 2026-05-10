#ifndef SLOPPY_ALLOC_H
#define SLOPPY_ALLOC_H

#include "sloppy/status.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

SlStatus sl_alloc_bytes(size_t size, unsigned char** out);
void sl_alloc_release(void* ptr);

typedef struct SlHeapBuffer
{
    unsigned char* ptr;
    size_t length;
} SlHeapBuffer;

SlStatus sl_heap_buffer_alloc(SlHeapBuffer* out, size_t length);
void sl_heap_buffer_dispose(SlHeapBuffer* buffer);

#ifdef __cplusplus
}
#endif

#endif

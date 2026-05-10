#ifndef SLOPPY_ALLOC_H
#define SLOPPY_ALLOC_H

#include "sloppy/status.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

SlStatus sl_alloc_bytes(size_t size, unsigned char** out);
void sl_alloc_release(void* ptr);

#ifdef __cplusplus
}
#endif

#endif

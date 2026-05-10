#include "sloppy/alloc.h"

#include <stddef.h>

static int test_invalid_arguments(void)
{
    SlHeapBuffer buffer = {0};

    if (sl_status_code(sl_heap_buffer_alloc(NULL, 16U)) != SL_STATUS_INVALID_ARGUMENT) {
        return 1;
    }
    if (sl_status_code(sl_heap_buffer_alloc(&buffer, 0U)) != SL_STATUS_INVALID_ARGUMENT) {
        return 1;
    }
    if (buffer.ptr != NULL || buffer.length != 0U) {
        return 1;
    }

    return 0;
}

static int test_alloc_and_dispose(void)
{
    SlHeapBuffer buffer = {0};
    size_t index = 0U;

    if (!sl_status_is_ok(sl_heap_buffer_alloc(&buffer, 32U))) {
        return 1;
    }
    if (buffer.ptr == NULL || buffer.length != 32U) {
        sl_heap_buffer_dispose(&buffer);
        return 1;
    }
    for (index = 0U; index < buffer.length; index += 1U) {
        buffer.ptr[index] = (unsigned char)index;
    }
    sl_heap_buffer_dispose(&buffer);
    if (buffer.ptr != NULL || buffer.length != 0U) {
        return 1;
    }
    sl_heap_buffer_dispose(NULL);

    return 0;
}

int main(void)
{
    if (test_invalid_arguments() != 0) {
        return 1;
    }
    return test_alloc_and_dispose();
}

#include "sloppy/alloc.h"

#include <stddef.h>

static int expect_true(int condition)
{
    return condition ? 0 : 1;
}

static int test_alloc_bytes(void)
{
    unsigned char sentinel = 0U;
    unsigned char* ptr = &sentinel;
    SlStatus status = sl_status_ok();

    status = sl_alloc_bytes(0U, &ptr);
    if (expect_true(sl_status_code(status) == SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 1;
    }
    if (expect_true(ptr == NULL) != 0) {
        return 2;
    }

    status = sl_alloc_bytes(1U, NULL);
    if (expect_true(sl_status_code(status) == SL_STATUS_INVALID_ARGUMENT) != 0) {
        return 3;
    }

    status = sl_alloc_bytes(32U, &ptr);
    if (expect_true(sl_status_is_ok(status)) != 0) {
        return 4;
    }
    if (expect_true(ptr != NULL) != 0) {
        return 5;
    }

    sl_alloc_release(ptr);
    sl_alloc_release(NULL);
    return 0;
}

static int test_heap_buffer_invalid_arguments(void)
{
    unsigned char sentinel = 0U;
    SlHeapBuffer buffer = {.ptr = &sentinel, .length = 99U};

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

static int test_heap_buffer_alloc_and_dispose(void)
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
    if (test_alloc_bytes() != 0) {
        return 1;
    }
    if (test_heap_buffer_invalid_arguments() != 0) {
        return 2;
    }
    return test_heap_buffer_alloc_and_dispose();
}

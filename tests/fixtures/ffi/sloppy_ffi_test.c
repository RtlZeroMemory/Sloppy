#include <stdint.h>
#include <stddef.h>

#if defined(_WIN32)
#define SLOPPY_FFI_EXPORT __declspec(dllexport)
#else
#define SLOPPY_FFI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct SloppyFfiPoint
{
    int32_t x;
    int32_t y;
} SloppyFfiPoint;

SLOPPY_FFI_EXPORT int32_t sloppy_ffi_add_i32(int32_t left, int32_t right)
{
    return left + right;
}

SLOPPY_FFI_EXPORT uint64_t sloppy_ffi_add_u64(uint64_t left, uint64_t right)
{
    return left + right;
}

SLOPPY_FFI_EXPORT double sloppy_ffi_add_f64(double left, double right)
{
    return left + right;
}

SLOPPY_FFI_EXPORT uint32_t sloppy_ffi_strlen(const char* text)
{
    uint32_t length = 0U;

    if (text == NULL) {
        return 0U;
    }
    while (text[length] != '\0') {
        length += 1U;
    }
    return length;
}

SLOPPY_FFI_EXPORT uint32_t sloppy_ffi_sum_bytes(const uint8_t* bytes, uintptr_t length)
{
    uint32_t total = 0U;

    if (bytes == NULL) {
        return 0U;
    }
    for (uintptr_t index = 0U; index < length; index += 1U) {
        total += bytes[index];
    }
    return total;
}

SLOPPY_FFI_EXPORT void sloppy_ffi_fill(uint8_t* bytes, uintptr_t length, uint8_t value)
{
    if (bytes == NULL) {
        return;
    }
    for (uintptr_t index = 0U; index < length; index += 1U) {
        bytes[index] = value;
    }
}

SLOPPY_FFI_EXPORT void sloppy_ffi_write_u32(uint32_t* value)
{
    if (value != NULL) {
        *value = 0xdecafbadU;
    }
}

SLOPPY_FFI_EXPORT int32_t sloppy_ffi_point_sum(const SloppyFfiPoint* point)
{
    return point == NULL ? 0 : point->x + point->y;
}

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

typedef struct SloppyFfiMatrix
{
    float values[16];
} SloppyFfiMatrix;

typedef struct SloppyFfiNested
{
    SloppyFfiPoint origin;
    SloppyFfiPoint size;
    uint32_t flags;
} SloppyFfiNested;

typedef struct SloppyFfiTaggedPoint
{
    uint8_t tag;
    SloppyFfiPoint point;
} SloppyFfiTaggedPoint;

typedef struct SloppyFfiCounter
{
    int32_t value;
} SloppyFfiCounter;

typedef int32_t (*SloppyFfiCallback)(int32_t value, void* user_data);
typedef int32_t (*SloppyFfiI32Callback)(int32_t value);
typedef uint32_t (*SloppyFfiU32Callback)(uint32_t value);
typedef void (*SloppyFfiVoidCallback)(int32_t value);

static SloppyFfiPoint sloppy_ffi_static_point = {19, 23};

SLOPPY_FFI_EXPORT int32_t sloppy_ffi_add_i32(int32_t left, int32_t right);
SLOPPY_FFI_EXPORT uint64_t sloppy_ffi_add_u64(uint64_t left, uint64_t right);
SLOPPY_FFI_EXPORT double sloppy_ffi_add_f64(double left, double right);
SLOPPY_FFI_EXPORT uint32_t sloppy_ffi_strlen(const char* text);
SLOPPY_FFI_EXPORT uint32_t sloppy_ffi_sum_bytes(const uint8_t* bytes, uintptr_t length);
SLOPPY_FFI_EXPORT void sloppy_ffi_fill(uint8_t* bytes, uintptr_t length, uint8_t value);
SLOPPY_FFI_EXPORT void sloppy_ffi_write_u32(uint32_t* value);
SLOPPY_FFI_EXPORT int32_t sloppy_ffi_write_out_values(int32_t* signed_value,
                                                      uint32_t* unsigned_value,
                                                      size_t* length_value);
SLOPPY_FFI_EXPORT int32_t sloppy_ffi_counter_create_out(int32_t initial,
                                                        SloppyFfiCounter** out_counter);
SLOPPY_FFI_EXPORT int32_t sloppy_ffi_copy_message(const char* text, char* output,
                                                  size_t output_length, size_t* written);
SLOPPY_FFI_EXPORT int32_t sloppy_ffi_mutate_bytes(uint8_t* bytes, size_t length);
SLOPPY_FFI_EXPORT int32_t sloppy_ffi_point_sum(const SloppyFfiPoint* point);
SLOPPY_FFI_EXPORT int32_t sloppy_ffi_nested_total(const SloppyFfiNested* nested);
SLOPPY_FFI_EXPORT int32_t sloppy_ffi_matrix_first4_sum(const SloppyFfiMatrix* matrix);
SLOPPY_FFI_EXPORT void* sloppy_ffi_null_pointer(void);
SLOPPY_FFI_EXPORT void* sloppy_ffi_static_point_pointer(void);
SLOPPY_FFI_EXPORT size_t sloppy_ffi_sizeof_point(void);
SLOPPY_FFI_EXPORT size_t sloppy_ffi_offsetof_point_x(void);
SLOPPY_FFI_EXPORT size_t sloppy_ffi_offsetof_point_y(void);
SLOPPY_FFI_EXPORT size_t sloppy_ffi_sizeof_matrix(void);
SLOPPY_FFI_EXPORT size_t sloppy_ffi_offsetof_matrix_values(void);
SLOPPY_FFI_EXPORT size_t sloppy_ffi_sizeof_nested(void);
SLOPPY_FFI_EXPORT size_t sloppy_ffi_offsetof_nested_origin(void);
SLOPPY_FFI_EXPORT size_t sloppy_ffi_offsetof_nested_size(void);
SLOPPY_FFI_EXPORT size_t sloppy_ffi_offsetof_nested_flags(void);
SLOPPY_FFI_EXPORT size_t sloppy_ffi_sizeof_tagged_point(void);
SLOPPY_FFI_EXPORT size_t sloppy_ffi_offsetof_tagged_point_tag(void);
SLOPPY_FFI_EXPORT size_t sloppy_ffi_offsetof_tagged_point_point(void);
SLOPPY_FFI_EXPORT SloppyFfiCounter* sloppy_ffi_counter_create(int32_t initial);
SLOPPY_FFI_EXPORT int32_t sloppy_ffi_counter_add(SloppyFfiCounter* counter, int32_t delta);
SLOPPY_FFI_EXPORT int32_t sloppy_ffi_counter_value(SloppyFfiCounter* counter);
SLOPPY_FFI_EXPORT void sloppy_ffi_counter_destroy(SloppyFfiCounter* counter);
SLOPPY_FFI_EXPORT int32_t sloppy_ffi_call_callback(SloppyFfiCallback callback, void* user_data,
                                                   int32_t value);
SLOPPY_FFI_EXPORT int32_t sloppy_ffi_call_i32_callback(SloppyFfiI32Callback callback,
                                                       int32_t value);
SLOPPY_FFI_EXPORT int32_t sloppy_ffi_visit_i32(int32_t count, SloppyFfiI32Callback callback);
SLOPPY_FFI_EXPORT uint32_t sloppy_ffi_call_u32_callback(SloppyFfiU32Callback callback,
                                                        uint32_t value);
SLOPPY_FFI_EXPORT void sloppy_ffi_call_void_callback(SloppyFfiVoidCallback callback, int32_t value);
SLOPPY_FFI_EXPORT void* sloppy_ffi_resolve_symbol(const char* name);

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

SLOPPY_FFI_EXPORT int32_t sloppy_ffi_write_out_values(int32_t* signed_value,
                                                      uint32_t* unsigned_value,
                                                      size_t* length_value)
{
    if (signed_value == NULL || unsigned_value == NULL || length_value == NULL) {
        return -1;
    }
    *signed_value = -17;
    *unsigned_value = 42U;
    *length_value = 9U;
    return 0;
}

SLOPPY_FFI_EXPORT int32_t sloppy_ffi_counter_create_out(int32_t initial,
                                                        SloppyFfiCounter** out_counter)
{
    SloppyFfiCounter* counter = NULL;

    if (out_counter == NULL) {
        return -1;
    }
    counter = sloppy_ffi_counter_create(initial);
    if (counter == NULL) {
        *out_counter = NULL;
        return -2;
    }
    *out_counter = counter;
    return 0;
}

SLOPPY_FFI_EXPORT int32_t sloppy_ffi_copy_message(const char* text, char* output,
                                                  size_t output_length, size_t* written)
{
    size_t length = 0U;
    size_t copy_length = 0U;

    if (text == NULL || output == NULL || written == NULL || output_length == 0U) {
        return -1;
    }
    length = strlen(text);
    *written = length;

    copy_length = length;
    if (copy_length >= output_length) {
        copy_length = output_length - 1U;
    }
    if (copy_length > 0U) {
        memcpy(output, text, copy_length);
    }
    output[copy_length] = '\0';
    return length >= output_length ? 1 : 0;
}

SLOPPY_FFI_EXPORT int32_t sloppy_ffi_mutate_bytes(uint8_t* bytes, size_t length)
{
    if (bytes == NULL) {
        return -1;
    }
    for (size_t index = 0U; index < length; index += 1U) {
        bytes[index] = (uint8_t)(bytes[index] + 1U);
    }
    return 0;
}

SLOPPY_FFI_EXPORT int32_t sloppy_ffi_point_sum(const SloppyFfiPoint* point)
{
    return point == NULL ? 0 : point->x + point->y;
}

SLOPPY_FFI_EXPORT int32_t sloppy_ffi_nested_total(const SloppyFfiNested* nested)
{
    int64_t total = 0;

    if (nested == NULL) {
        return -1;
    }
    if (nested->flags > (uint32_t)INT32_MAX) {
        return -2;
    }
    total = (int64_t)nested->origin.x + (int64_t)nested->origin.y + (int64_t)nested->size.x +
            (int64_t)nested->size.y + (int64_t)nested->flags;
    if (total < INT32_MIN || total > INT32_MAX) {
        return -2;
    }
    return (int32_t)total;
}

SLOPPY_FFI_EXPORT int32_t sloppy_ffi_matrix_first4_sum(const SloppyFfiMatrix* matrix)
{
    double total = 0.0;

    if (matrix == NULL) {
        return -1;
    }
    for (size_t index = 0U; index < 4U; index += 1U) {
        total += (double)matrix->values[index];
    }
    if (!isfinite(total) || total < (double)INT32_MIN || total > (double)INT32_MAX) {
        return -2;
    }
    return (int32_t)total;
}

SLOPPY_FFI_EXPORT void* sloppy_ffi_null_pointer(void)
{
    return NULL;
}

SLOPPY_FFI_EXPORT void* sloppy_ffi_static_point_pointer(void)
{
    return &sloppy_ffi_static_point;
}

SLOPPY_FFI_EXPORT size_t sloppy_ffi_sizeof_point(void)
{
    return sizeof(SloppyFfiPoint);
}

SLOPPY_FFI_EXPORT size_t sloppy_ffi_offsetof_point_x(void)
{
    return offsetof(SloppyFfiPoint, x);
}

SLOPPY_FFI_EXPORT size_t sloppy_ffi_offsetof_point_y(void)
{
    return offsetof(SloppyFfiPoint, y);
}

SLOPPY_FFI_EXPORT size_t sloppy_ffi_sizeof_matrix(void)
{
    return sizeof(SloppyFfiMatrix);
}

SLOPPY_FFI_EXPORT size_t sloppy_ffi_offsetof_matrix_values(void)
{
    return offsetof(SloppyFfiMatrix, values);
}

SLOPPY_FFI_EXPORT size_t sloppy_ffi_sizeof_nested(void)
{
    return sizeof(SloppyFfiNested);
}

SLOPPY_FFI_EXPORT size_t sloppy_ffi_offsetof_nested_origin(void)
{
    return offsetof(SloppyFfiNested, origin);
}

SLOPPY_FFI_EXPORT size_t sloppy_ffi_offsetof_nested_size(void)
{
    return offsetof(SloppyFfiNested, size);
}

SLOPPY_FFI_EXPORT size_t sloppy_ffi_offsetof_nested_flags(void)
{
    return offsetof(SloppyFfiNested, flags);
}

SLOPPY_FFI_EXPORT size_t sloppy_ffi_sizeof_tagged_point(void)
{
    return sizeof(SloppyFfiTaggedPoint);
}

SLOPPY_FFI_EXPORT size_t sloppy_ffi_offsetof_tagged_point_tag(void)
{
    return offsetof(SloppyFfiTaggedPoint, tag);
}

SLOPPY_FFI_EXPORT size_t sloppy_ffi_offsetof_tagged_point_point(void)
{
    return offsetof(SloppyFfiTaggedPoint, point);
}

SLOPPY_FFI_EXPORT SloppyFfiCounter* sloppy_ffi_counter_create(int32_t initial)
{
    SloppyFfiCounter* counter = (SloppyFfiCounter*)malloc(sizeof(SloppyFfiCounter));
    if (counter != NULL) {
        counter->value = initial;
    }
    return counter;
}

SLOPPY_FFI_EXPORT int32_t sloppy_ffi_counter_add(SloppyFfiCounter* counter, int32_t delta)
{
    if (counter == NULL) {
        return 0;
    }
    counter->value += delta;
    return counter->value;
}

SLOPPY_FFI_EXPORT int32_t sloppy_ffi_counter_value(SloppyFfiCounter* counter)
{
    return counter == NULL ? 0 : counter->value;
}

SLOPPY_FFI_EXPORT void sloppy_ffi_counter_destroy(SloppyFfiCounter* counter)
{
    free(counter);
}

SLOPPY_FFI_EXPORT int32_t sloppy_ffi_call_callback(SloppyFfiCallback callback, void* user_data,
                                                   int32_t value)
{
    return callback == NULL ? 0 : callback(value, user_data);
}

SLOPPY_FFI_EXPORT int32_t sloppy_ffi_call_i32_callback(SloppyFfiI32Callback callback, int32_t value)
{
    return callback == NULL ? 0 : callback(value);
}

SLOPPY_FFI_EXPORT int32_t sloppy_ffi_visit_i32(int32_t count, SloppyFfiI32Callback callback)
{
    int64_t total = 0;

    if (callback == NULL || count < 0) {
        return -1;
    }
    for (int32_t value = 0; value < count; value += 1) {
        total += callback(value);
        if (total < INT32_MIN || total > INT32_MAX) {
            return -2;
        }
    }
    return (int32_t)total;
}

SLOPPY_FFI_EXPORT uint32_t sloppy_ffi_call_u32_callback(SloppyFfiU32Callback callback,
                                                        uint32_t value)
{
    return callback == NULL ? 0U : callback(value);
}

SLOPPY_FFI_EXPORT void sloppy_ffi_call_void_callback(SloppyFfiVoidCallback callback, int32_t value)
{
    if (callback != NULL) {
        callback(value);
    }
}

SLOPPY_FFI_EXPORT void* sloppy_ffi_resolve_symbol(const char* name)
{
    if (name == NULL) {
        return NULL;
    }
    if (strcmp(name, "sloppy_ffi_add_i32") == 0) {
        return (void*)&sloppy_ffi_add_i32;
    }
    return NULL;
}

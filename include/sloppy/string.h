#ifndef SLOPPY_STRING_H
#define SLOPPY_STRING_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SlStr is a borrowed string view: pointer plus length.
 *
 * SlStr does not own memory, does not require NUL termination, and may contain embedded NUL
 * bytes. A non-empty view requires `ptr` to remain valid for `length` bytes for the caller's
 * documented lifetime.
 */
typedef struct SlStr
{
    const char* ptr;
    size_t length;
} SlStr;

SlStr sl_str_from_parts(const char* ptr, size_t length);

/*
 * Boundary adapter for valid C strings. `cstr` must point to a NUL-terminated string and
 * remain alive for the returned borrowed view. Passing NULL returns the empty view.
 */
SlStr sl_str_from_cstr(const char* cstr);

SlStr sl_str_empty(void);
bool sl_str_is_empty(SlStr str);
bool sl_str_equal(SlStr left, SlStr right);
bool sl_str_starts_with(SlStr str, SlStr prefix);

#ifdef __cplusplus
}
#endif

#endif

#include "sloppy/platform_dynlib.h"

#include <dlfcn.h>

#define SL_DYNLIB_MAX_PATH_CHARS 4096U
#define SL_DYNLIB_MAX_SYMBOL_CHARS 1024U

static bool sl_dynlib_str_valid(SlStr value)
{
    return value.length == 0U || value.ptr != NULL;
}

static bool sl_dynlib_contains_nul(SlStr value)
{
    size_t index = 0U;

    if (!sl_dynlib_str_valid(value)) {
        return true;
    }
    for (index = 0U; index < value.length; index += 1U) {
        if (value.ptr[index] == '\0') {
            return true;
        }
    }
    return false;
}

static bool sl_dynlib_copy_cstr(SlStr value, char* out, size_t capacity)
{
    if (out == NULL || capacity == 0U || value.length == 0U || value.length >= capacity ||
        sl_dynlib_contains_nul(value))
    {
        return false;
    }
    for (size_t index = 0U; index < value.length; index += 1U) {
        out[index] = value.ptr[index];
    }
    out[value.length] = '\0';
    return true;
}

static void sl_dynlib_diag(SlDiag* out_diag, SlDiagCode code, SlStr message, SlStr detail)
{
    if (out_diag == NULL) {
        return;
    }
    *out_diag = (SlDiag){0};
    out_diag->severity = SL_DIAG_SEVERITY_ERROR;
    out_diag->code = code;
    out_diag->message = message;
    out_diag->primary_span = sl_source_span_unknown();
    if (!sl_str_is_empty(detail)) {
        out_diag->hints[0] = detail;
        out_diag->hint_count = 1U;
    }
}

SlStatus sl_platform_dynlib_open(SlStr path, SlPlatformDynlib* out, SlDiag* out_diag)
{
    char cpath[SL_DYNLIB_MAX_PATH_CHARS];
    void* handle = NULL;

    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlPlatformDynlib){0};
    if (!sl_dynlib_copy_cstr(path, cpath, sizeof(cpath))) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    handle = dlopen(cpath, RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) {
        const char* message = dlerror();
        sl_dynlib_diag(out_diag, SL_DIAG_FFI_LIBRARY_NOT_FOUND,
                       sl_str_from_cstr("FFI library could not be loaded"),
                       message == NULL ? sl_str_empty() : sl_str_from_cstr(message));
        return sl_status_from_code(SL_STATUS_UNSUPPORTED);
    }
    out->handle = handle;
    return sl_status_ok();
}

SlStatus sl_platform_dynlib_symbol(const SlPlatformDynlib* library, SlStr symbol, void** out,
                                   SlDiag* out_diag)
{
    char csymbol[SL_DYNLIB_MAX_SYMBOL_CHARS];
    void* pointer = NULL;

    if (library == NULL || library->handle == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = NULL;
    if (!sl_dynlib_copy_cstr(symbol, csymbol, sizeof(csymbol))) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    dlerror();
    pointer = dlsym(library->handle, csymbol);
    const char* message = dlerror();
    if (message != NULL) {
        sl_dynlib_diag(out_diag, SL_DIAG_FFI_SYMBOL_NOT_FOUND,
                       sl_str_from_cstr("FFI symbol could not be resolved"),
                       sl_str_from_cstr(message));
        return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
    }
    *out = pointer;
    return sl_status_ok();
}

void sl_platform_dynlib_close(SlPlatformDynlib* library)
{
    if (library != NULL && library->handle != NULL) {
        dlclose(library->handle);
        library->handle = NULL;
    }
}

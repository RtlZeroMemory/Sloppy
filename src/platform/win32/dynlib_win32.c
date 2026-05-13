#include "sloppy/platform_dynlib.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <limits.h>

#define SL_DYNLIB_MAX_PATH_CHARS 4096U
#define SL_DYNLIB_MAX_SYMBOL_CHARS 1024U

#ifndef LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
#define LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR 0x00000100
#endif
#ifndef LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
#define LOAD_LIBRARY_SEARCH_DEFAULT_DIRS 0x00001000
#endif

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

static bool sl_dynlib_copy_wide(SlStr value, wchar_t* out, size_t capacity)
{
    int count = 0;

    if (out == NULL || capacity == 0U || capacity > (size_t)INT_MAX || value.length == 0U ||
        value.length > (size_t)INT_MAX || sl_dynlib_contains_nul(value))
    {
        return false;
    }
    count =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.ptr, (int)value.length, NULL, 0);
    if (count <= 0 || (size_t)count + 1U > capacity) {
        return false;
    }
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.ptr, (int)value.length, out,
                            count) != count)
    {
        return false;
    }
    out[count] = L'\0';
    return true;
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
    wchar_t wide[SL_DYNLIB_MAX_PATH_CHARS];
    HMODULE handle = NULL;

    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlPlatformDynlib){0};
    if (!sl_dynlib_copy_wide(path, wide, sizeof(wide) / sizeof(wide[0]))) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    handle = LoadLibraryExW(
        wide, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (handle == NULL) {
        sl_dynlib_diag(out_diag, SL_DIAG_FFI_LIBRARY_NOT_FOUND,
                       sl_str_from_cstr("FFI library could not be loaded"),
                       sl_str_from_cstr("LoadLibraryExW failed"));
        return sl_status_from_code(SL_STATUS_UNSUPPORTED);
    }
    out->handle = (void*)handle;
    return sl_status_ok();
}

SlStatus sl_platform_dynlib_symbol(const SlPlatformDynlib* library, SlStr symbol, void** out,
                                   SlDiag* out_diag)
{
    char csymbol[SL_DYNLIB_MAX_SYMBOL_CHARS];
    FARPROC pointer = NULL;

    if (library == NULL || library->handle == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = NULL;
    if (!sl_dynlib_copy_cstr(symbol, csymbol, sizeof(csymbol))) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    pointer = GetProcAddress((HMODULE)library->handle, csymbol);
    if (pointer == NULL) {
        sl_dynlib_diag(out_diag, SL_DIAG_FFI_SYMBOL_NOT_FOUND,
                       sl_str_from_cstr("FFI symbol could not be resolved"),
                       sl_str_from_cstr("GetProcAddress failed"));
        return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
    }
    *out = (void*)pointer;
    return sl_status_ok();
}

void sl_platform_dynlib_close(SlPlatformDynlib* library)
{
    if (library != NULL && library->handle != NULL) {
        if (FreeLibrary((HMODULE)library->handle) != 0) {
            library->handle = NULL;
        }
    }
}

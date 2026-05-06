#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "../../core/os_platform.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char** environ;

static SlStatus sl_os_posix_copy_cstr(SlArena* arena, const char* value, SlOwnedStr* out)
{
    return sl_str_copy_to_arena(arena, sl_str_from_cstr(value == NULL ? "" : value), out);
}

static SlStr sl_os_posix_platform(void)
{
#if defined(__APPLE__)
    return sl_str_from_cstr("darwin");
#elif defined(__linux__)
    return sl_str_from_cstr("linux");
#elif defined(__FreeBSD__)
    return sl_str_from_cstr("freebsd");
#else
    return sl_str_from_cstr("unknown");
#endif
}

static SlStr sl_os_posix_arch(void)
{
#if defined(__x86_64__) || defined(_M_X64)
    return sl_str_from_cstr("x64");
#elif defined(__aarch64__) || defined(_M_ARM64)
    return sl_str_from_cstr("arm64");
#elif defined(__i386__) || defined(_M_IX86)
    return sl_str_from_cstr("ia32");
#else
    return sl_str_from_cstr("unknown");
#endif
}

static uint32_t sl_os_posix_cpu_count(void)
{
#ifdef _SC_NPROCESSORS_ONLN
    long value = sysconf(_SC_NPROCESSORS_ONLN);
    if (value > 0L && value <= (long)UINT32_MAX) {
        return (uint32_t)value;
    }
#endif
    return 1U;
}

SlStatus sl_os_platform_system_info(SlArena* arena, SlOsSystemInfo* out, SlDiag* out_diag)
{
    char host[256];
    const char* temp = getenv("TMPDIR");

    (void)out_diag;
    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlOsSystemInfo){0};
    if (gethostname(host, sizeof(host)) != 0) {
        host[0] = '\0';
    }
    host[sizeof(host) - 1U] = '\0';
    if (temp == NULL || temp[0] == '\0') {
        temp = "/tmp";
    }
    SlStatus status = sl_str_copy_to_arena(arena, sl_os_posix_platform(), &out->platform);
    if (sl_status_is_ok(status)) {
        status = sl_str_copy_to_arena(arena, sl_os_posix_arch(), &out->arch);
    }
    if (sl_status_is_ok(status)) {
        status = sl_os_posix_copy_cstr(arena, temp, &out->temp_directory);
    }
    if (sl_status_is_ok(status)) {
        status = sl_os_posix_copy_cstr(arena, host, &out->hostname);
    }
    if (sl_status_is_ok(status)) {
        status = sl_str_copy_to_arena(arena, sl_str_from_cstr("\n"), &out->end_of_line);
    }
    out->cpu_count = sl_os_posix_cpu_count();
    return status;
}

SlStatus sl_os_platform_environment_get(SlArena* arena, SlStr key, SlOwnedStr* out_value,
                                        bool* out_found, SlDiag* out_diag)
{
    SlOwnedStr key_cstr = {0};
    const char* value = NULL;
    SlStatus status;

    (void)out_diag;
    if (arena == NULL || out_value == NULL || out_found == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_value = (SlOwnedStr){0};
    *out_found = false;
    status = sl_str_copy_to_arena_nul(arena, key, &key_cstr);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    value = getenv(key_cstr.ptr);
    if (value == NULL) {
        return sl_status_ok();
    }
    *out_found = true;
    return sl_str_copy_to_arena(arena, sl_str_from_cstr(value), out_value);
}

SlStatus sl_os_platform_environment_has(SlStr key, bool* out_found, SlDiag* out_diag)
{
    unsigned char storage[512];
    SlArena arena = {0};
    SlOwnedStr key_cstr = {0};
    SlStatus status;

    (void)out_diag;
    if (out_found == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_found = false;
    status = sl_arena_init(&arena, storage, sizeof(storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_str_copy_to_arena_nul(&arena, key, &key_cstr);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    *out_found = getenv(key_cstr.ptr) != NULL;
    return sl_status_ok();
}

SlStatus sl_os_platform_environment_list(SlArena* arena, SlStr prefix, SlOsEnvironmentList* out,
                                         SlDiag* out_diag)
{
    size_t count = 0U;
    size_t index = 0U;
    char** cursor = environ;
    void* memory = NULL;
    SlStatus status;

    (void)out_diag;
    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlOsEnvironmentList){0};
    while (cursor != NULL && cursor[count] != NULL) {
        SlStr entry = sl_str_from_cstr(cursor[count]);
        const char* equals = strchr(cursor[count], '=');
        SlStr key = sl_str_from_parts(
            cursor[count], equals == NULL ? entry.length : (size_t)(equals - cursor[count]));
        if (sl_str_starts_with(key, prefix)) {
            out->count += 1U;
        }
        count += 1U;
    }
    if (out->count == 0U) {
        return sl_status_ok();
    }
    status = sl_arena_alloc(arena, out->count * sizeof(SlOsEnvironmentEntry),
                            _Alignof(SlOsEnvironmentEntry), &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    out->entries = (SlOsEnvironmentEntry*)memory;
    for (size_t scan = 0U; scan < count; scan += 1U) {
        SlStr entry = sl_str_from_cstr(environ[scan]);
        const char* equals = strchr(environ[scan], '=');
        SlStr key = sl_str_from_parts(
            environ[scan], equals == NULL ? entry.length : (size_t)(equals - environ[scan]));
        if (sl_str_starts_with(key, prefix)) {
            status = sl_str_copy_to_arena(arena, key, &out->entries[index].key);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            index += 1U;
        }
    }
    return sl_status_ok();
}

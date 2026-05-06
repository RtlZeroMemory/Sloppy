#include "../../core/os_platform.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <stdint.h>
#include <string.h>

static SlStatus sl_os_win32_copy_cstr(SlArena* arena, const char* value, SlOwnedStr* out)
{
    return sl_str_copy_to_arena(arena, sl_str_from_cstr(value == NULL ? "" : value), out);
}

static SlStr sl_os_win32_arch(void)
{
#if defined(_M_X64) || defined(__x86_64__)
    return sl_str_from_cstr("x64");
#elif defined(_M_ARM64) || defined(__aarch64__)
    return sl_str_from_cstr("arm64");
#elif defined(_M_IX86) || defined(__i386__)
    return sl_str_from_cstr("ia32");
#else
    return sl_str_from_cstr("unknown");
#endif
}

SlStatus sl_os_platform_system_info(SlArena* arena, SlOsSystemInfo* out, SlDiag* out_diag)
{
    SYSTEM_INFO system_info;
    char temp[MAX_PATH + 1U];
    char host[MAX_COMPUTERNAME_LENGTH + 1U];
    DWORD host_len = MAX_COMPUTERNAME_LENGTH + 1U;
    DWORD temp_len = 0U;
    SlStatus status;

    (void)out_diag;
    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlOsSystemInfo){0};
    GetSystemInfo(&system_info);
    temp_len = GetTempPathA((DWORD)sizeof(temp), temp);
    if (temp_len == 0U || temp_len >= sizeof(temp)) {
        temp[0] = '\0';
    }
    if (!GetComputerNameA(host, &host_len)) {
        host[0] = '\0';
    }
    status = sl_str_copy_to_arena(arena, sl_str_from_cstr("windows"), &out->platform);
    if (sl_status_is_ok(status)) {
        status = sl_str_copy_to_arena(arena, sl_os_win32_arch(), &out->arch);
    }
    if (sl_status_is_ok(status)) {
        status = sl_os_win32_copy_cstr(arena, temp, &out->temp_directory);
    }
    if (sl_status_is_ok(status)) {
        status = sl_os_win32_copy_cstr(arena, host, &out->hostname);
    }
    if (sl_status_is_ok(status)) {
        status = sl_str_copy_to_arena(arena, sl_str_from_cstr("\r\n"), &out->end_of_line);
    }
    out->cpu_count = system_info.dwNumberOfProcessors == 0U ? 1U : system_info.dwNumberOfProcessors;
    return status;
}

SlStatus sl_os_platform_environment_get(SlArena* arena, SlStr key, SlOwnedStr* out_value,
                                        bool* out_found, SlDiag* out_diag)
{
    SlOwnedStr key_cstr = {0};
    DWORD required = 0U;
    void* memory = NULL;
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
    required = GetEnvironmentVariableA(key_cstr.ptr, NULL, 0U);
    if (required == 0U) {
        return sl_status_ok();
    }
    status = sl_arena_alloc(arena, (size_t)required, _Alignof(char), &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (GetEnvironmentVariableA(key_cstr.ptr, (char*)memory, required) >= required) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    out_value->ptr = (char*)memory;
    out_value->length = sl_str_from_cstr((const char*)memory).length;
    *out_found = true;
    return sl_status_ok();
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
    *out_found = GetEnvironmentVariableA(key_cstr.ptr, NULL, 0U) != 0U;
    return sl_status_ok();
}

SlStatus sl_os_platform_environment_list(SlArena* arena, SlStr prefix, SlOsEnvironmentList* out,
                                         SlDiag* out_diag)
{
    LPCH block = NULL;
    const char* cursor = NULL;
    size_t count = 0U;
    size_t index = 0U;
    void* memory = NULL;
    SlStatus status;

    (void)out_diag;
    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlOsEnvironmentList){0};
    block = GetEnvironmentStringsA();
    if (block == NULL) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    for (cursor = block; *cursor != '\0';) {
        SlStr entry = sl_str_from_cstr(cursor);
        const char* equals = strchr(cursor, '=');
        SlStr key =
            sl_str_from_parts(cursor, equals == NULL ? entry.length : (size_t)(equals - cursor));
        if (key.length != 0U && sl_str_starts_with(key, prefix)) {
            count += 1U;
        }
        cursor += entry.length + 1U;
    }
    if (count != 0U) {
        status = sl_arena_alloc(arena, count * sizeof(SlOsEnvironmentEntry),
                                _Alignof(SlOsEnvironmentEntry), &memory);
        if (!sl_status_is_ok(status)) {
            FreeEnvironmentStringsA(block);
            return status;
        }
        out->entries = (SlOsEnvironmentEntry*)memory;
        out->count = count;
        for (cursor = block; *cursor != '\0';) {
            SlStr entry = sl_str_from_cstr(cursor);
            const char* equals = strchr(cursor, '=');
            SlStr key = sl_str_from_parts(cursor, equals == NULL ? entry.length
                                                                 : (size_t)(equals - cursor));
            if (key.length != 0U && sl_str_starts_with(key, prefix)) {
                status = sl_str_copy_to_arena(arena, key, &out->entries[index].key);
                if (!sl_status_is_ok(status)) {
                    FreeEnvironmentStringsA(block);
                    return status;
                }
                index += 1U;
            }
            cursor += entry.length + 1U;
        }
    }
    FreeEnvironmentStringsA(block);
    return sl_status_ok();
}

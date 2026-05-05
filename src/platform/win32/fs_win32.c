/*
 * src/platform/win32/fs_win32.c
 *
 * Win32 filesystem backend. UTF-8 to UTF-16 conversion stays in this platform boundary.
 */
#include "../../core/fs_platform.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <limits.h>
#include <stdint.h>
#include <string.h>

static SlDiag sl_fs_win32_diag(SlStr message)
{
    SlDiag diag = {0};

    diag.severity = SL_DIAG_SEVERITY_ERROR;
    diag.code = SL_DIAG_PERMISSION_DENIED;
    diag.message = message;
    diag.primary_span = sl_source_span_unknown();
    return diag;
}

static SlStatus sl_fs_win32_status(DWORD error, SlDiag* out_diag, SlStr message)
{
    if (out_diag != NULL) {
        *out_diag = sl_fs_win32_diag(message);
    }
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
        return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
    }
    if (error == ERROR_ACCESS_DENIED) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    return sl_status_from_code(SL_STATUS_INTERNAL);
}

static SlStatus sl_fs_win32_path_to_wide(SlArena* arena, SlStr path, wchar_t** out)
{
    int required = 0;
    void* memory = NULL;
    wchar_t* wide = NULL;
    SlStatus status;

    if (arena == NULL || out == NULL || path.length == 0U || path.length > INT_MAX) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = NULL;
    required =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.ptr, (int)path.length, NULL, 0);
    if (required <= 0) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_arena_alloc(arena, ((size_t)required + 1U) * sizeof(wchar_t), _Alignof(wchar_t),
                            &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    wide = (wchar_t*)memory;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.ptr, (int)path.length, wide,
                            required) != required)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    wide[required] = L'\0';
    *out = wide;
    return sl_status_ok();
}

SlStatus sl_fs_platform_read_file(SlArena* arena, SlStr path, SlOwnedBytes* out, SlDiag* out_diag)
{
    SlArenaMark mark;
    wchar_t* wide = NULL;
    HANDLE file = INVALID_HANDLE_VALUE;
    LARGE_INTEGER size;
    void* memory = NULL;
    DWORD read = 0;
    SlStatus status;

    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlOwnedBytes){0};
    mark = sl_arena_mark(arena);
    status = sl_fs_win32_path_to_wide(arena, path, &wide);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    file = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        (void)sl_arena_reset_to(arena, mark);
        return sl_fs_win32_status(GetLastError(), out_diag,
                                  sl_str_from_cstr("filesystem read failed"));
    }
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        (ULONGLONG)size.QuadPart > (ULONGLONG)UINT32_MAX)
    {
        CloseHandle(file);
        (void)sl_arena_reset_to(arena, mark);
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    if (size.QuadPart == 0) {
        CloseHandle(file);
        return sl_status_ok();
    }
    status = sl_arena_alloc(arena, (size_t)size.QuadPart, _Alignof(unsigned char), &memory);
    if (!sl_status_is_ok(status)) {
        CloseHandle(file);
        return status;
    }
    if (!ReadFile(file, memory, (DWORD)size.QuadPart, &read, NULL) || read != (DWORD)size.QuadPart)
    {
        CloseHandle(file);
        (void)sl_arena_reset_to(arena, mark);
        return sl_fs_win32_status(GetLastError(), out_diag,
                                  sl_str_from_cstr("filesystem read failed"));
    }
    CloseHandle(file);
    out->ptr = (unsigned char*)memory;
    out->length = (size_t)size.QuadPart;
    return sl_status_ok();
}

SlStatus sl_fs_platform_write_file(SlStr path, SlBytes bytes, bool append, SlDiag* out_diag)
{
    unsigned char scratch[4096];
    SlArena arena = {0};
    wchar_t* wide = NULL;
    HANDLE file = INVALID_HANDLE_VALUE;
    DWORD written = 0;
    SlStatus status;

    status = sl_arena_init(&arena, scratch, sizeof(scratch));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_fs_win32_path_to_wide(&arena, path, &wide);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    file = CreateFileW(wide, append ? FILE_APPEND_DATA : GENERIC_WRITE, 0, NULL,
                       append ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return sl_fs_win32_status(GetLastError(), out_diag,
                                  sl_str_from_cstr("filesystem write failed"));
    }
    if (bytes.length != 0U && (!WriteFile(file, bytes.ptr, (DWORD)bytes.length, &written, NULL) ||
                               written != (DWORD)bytes.length))
    {
        DWORD error = GetLastError();
        CloseHandle(file);
        return sl_fs_win32_status(error, out_diag, sl_str_from_cstr("filesystem write failed"));
    }
    CloseHandle(file);
    return sl_status_ok();
}

SlStatus sl_fs_platform_copy_file(SlStr from_path, SlStr to_path, bool overwrite, SlDiag* out_diag)
{
    unsigned char scratch[8192];
    SlArena arena = {0};
    wchar_t* from_wide = NULL;
    wchar_t* to_wide = NULL;
    SlStatus status;

    status = sl_arena_init(&arena, scratch, sizeof(scratch));
    if (!sl_status_is_ok(status) ||
        !sl_status_is_ok((status = sl_fs_win32_path_to_wide(&arena, from_path, &from_wide))) ||
        !sl_status_is_ok((status = sl_fs_win32_path_to_wide(&arena, to_path, &to_wide))))
    {
        return status;
    }
    if (!CopyFileW(from_wide, to_wide, overwrite ? FALSE : TRUE)) {
        return sl_fs_win32_status(GetLastError(), out_diag,
                                  sl_str_from_cstr("filesystem copy failed"));
    }
    return sl_status_ok();
}

SlStatus sl_fs_platform_move_file(SlStr from_path, SlStr to_path, bool overwrite, SlDiag* out_diag)
{
    unsigned char scratch[8192];
    SlArena arena = {0};
    wchar_t* from_wide = NULL;
    wchar_t* to_wide = NULL;
    DWORD flags = MOVEFILE_COPY_ALLOWED;
    SlStatus status;

    if (overwrite) {
        flags |= MOVEFILE_REPLACE_EXISTING;
    }
    status = sl_arena_init(&arena, scratch, sizeof(scratch));
    if (!sl_status_is_ok(status) ||
        !sl_status_is_ok((status = sl_fs_win32_path_to_wide(&arena, from_path, &from_wide))) ||
        !sl_status_is_ok((status = sl_fs_win32_path_to_wide(&arena, to_path, &to_wide))))
    {
        return status;
    }
    if (!MoveFileExW(from_wide, to_wide, flags)) {
        return sl_fs_win32_status(GetLastError(), out_diag,
                                  sl_str_from_cstr("filesystem move failed"));
    }
    return sl_status_ok();
}

SlStatus sl_fs_platform_delete_file(SlStr path, SlDiag* out_diag)
{
    unsigned char scratch[4096];
    SlArena arena = {0};
    wchar_t* wide = NULL;
    SlStatus status;

    status = sl_arena_init(&arena, scratch, sizeof(scratch));
    if (!sl_status_is_ok(status) ||
        !sl_status_is_ok((status = sl_fs_win32_path_to_wide(&arena, path, &wide))))
    {
        return status;
    }
    if (!DeleteFileW(wide)) {
        return sl_fs_win32_status(GetLastError(), out_diag,
                                  sl_str_from_cstr("filesystem delete failed"));
    }
    return sl_status_ok();
}

SlStatus sl_fs_platform_stat(SlStr path, SlFsStat* out, SlDiag* out_diag)
{
    unsigned char scratch[4096];
    SlArena arena = {0};
    wchar_t* wide = NULL;
    WIN32_FILE_ATTRIBUTE_DATA attrs;
    SlStatus status;

    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlFsStat){0};
    status = sl_arena_init(&arena, scratch, sizeof(scratch));
    if (!sl_status_is_ok(status) ||
        !sl_status_is_ok((status = sl_fs_win32_path_to_wide(&arena, path, &wide))))
    {
        return status;
    }
    if (!GetFileAttributesExW(wide, GetFileExInfoStandard, &attrs)) {
        DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
        }
        return sl_fs_win32_status(error, out_diag, sl_str_from_cstr("filesystem stat failed"));
    }
    out->exists = true;
    out->kind = (attrs.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ? SL_FS_NODE_DIRECTORY
                                                                         : SL_FS_NODE_FILE;
    out->size = ((uint64_t)attrs.nFileSizeHigh << 32U) | (uint64_t)attrs.nFileSizeLow;
    return sl_status_ok();
}

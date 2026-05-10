/*
 * src/platform/win32/fs_win32.c
 *
 * Win32 filesystem backend. UTF-8 to UTF-16 conversion stays in this platform boundary.
 */
#include "../../core/fs_platform.h"

#include "sloppy/checked_math.h"
#include "sloppy/container.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#include <winioctl.h>

#include <limits.h>
#include <stdint.h>
#include <string.h>

struct SlFsFileHandle
{
    HANDLE handle;
    bool closed;
};

struct SlFsFileLock
{
    HANDLE handle;
    SlOwnedStr path;
    bool closed;
};

static SlDiagCode sl_fs_win32_diag_code(DWORD error)
{
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
        return SL_DIAG_INVALID_ARGUMENT;
    }
    if (error == ERROR_ACCESS_DENIED || error == ERROR_PRIVILEGE_NOT_HELD) {
        return SL_DIAG_PERMISSION_DENIED;
    }
    if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS) {
        return SL_DIAG_INVALID_ARGUMENT;
    }
    return SL_DIAG_INTERNAL_ERROR;
}

static SlDiag sl_fs_win32_diag(DWORD error, SlStr message)
{
    SlDiag diag = {0};

    diag.severity = SL_DIAG_SEVERITY_ERROR;
    diag.code = sl_fs_win32_diag_code(error);
    diag.message = message;
    diag.primary_span = sl_source_span_unknown();
    return diag;
}

static SlStatus sl_fs_win32_status(DWORD error, SlDiag* out_diag, SlStr message)
{
    if (out_diag != NULL) {
        *out_diag = sl_fs_win32_diag(error, message);
    }
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
        return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
    }
    if (error == ERROR_ACCESS_DENIED) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    if (error == ERROR_PRIVILEGE_NOT_HELD) {
        return sl_status_from_code(SL_STATUS_UNSUPPORTED);
    }
    if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    return sl_status_from_code(SL_STATUS_INTERNAL);
}

static uint64_t sl_fs_win32_filetime_stamp(FILETIME value)
{
    ULARGE_INTEGER ticks;

    ticks.LowPart = value.dwLowDateTime;
    ticks.HighPart = value.dwHighDateTime;
    return ticks.QuadPart * 100U;
}

static SlStatus sl_fs_win32_path_to_wide(SlArena* arena, SlStr path, wchar_t** out)
{
    int required = 0;
    SlSlice storage = {0};
    wchar_t* wide = NULL;
    SlStatus status;

    if (arena == NULL || out == NULL || path.length == 0U || path.length > INT_MAX ||
        !sl_status_is_ok(sl_str_validate_no_nul(path)))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = NULL;
    required =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.ptr, (int)path.length, NULL, 0);
    if (required <= 0) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_arena_array_alloc(arena, (size_t)required + 1U, sizeof(wchar_t), _Alignof(wchar_t),
                                  &storage);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    wide = (wchar_t*)storage.ptr;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.ptr, (int)path.length, wide,
                            required) != required)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    wide[required] = L'\0';
    *out = wide;
    return sl_status_ok();
}

static SlStatus sl_fs_win32_wide_to_utf8(SlArena* arena, const wchar_t* value, SlOwnedStr* out)
{
    int required = 0;
    void* memory = NULL;
    SlStatus status;

    if (arena == NULL || value == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlOwnedStr){0};
    required = WideCharToMultiByte(CP_UTF8, 0, value, -1, NULL, 0, NULL, NULL);
    if (required <= 0) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_arena_alloc(arena, (size_t)required, _Alignof(char), &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (WideCharToMultiByte(CP_UTF8, 0, value, -1, (char*)memory, required, NULL, NULL) != required)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    out->ptr = (char*)memory;
    out->length = (size_t)required - 1U;
    return sl_status_ok();
}

static SlStatus sl_fs_win32_alloc_handle(SlArena* arena, SlFsFileHandle** out)
{
    void* memory = NULL;
    SlStatus status;

    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = NULL;
    status = sl_arena_alloc(arena, sizeof(SlFsFileHandle), _Alignof(SlFsFileHandle), &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (memory == NULL) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    *out = (SlFsFileHandle*)memory;
    **out = (SlFsFileHandle){.handle = INVALID_HANDLE_VALUE, .closed = true};
    return sl_status_ok();
}

static void sl_fs_win32_copy_wchars(wchar_t* dst, const wchar_t* src, size_t length)
{
    size_t index = 0U;

    for (index = 0U; index < length; index += 1U) {
        dst[index] = src[index];
    }
}

static void sl_fs_win32_copy_chars(char* dst, const char* src, size_t length)
{
    size_t index = 0U;

    for (index = 0U; index < length; index += 1U) {
        dst[index] = src[index];
    }
}

static size_t sl_fs_win32_root_length(const wchar_t* path)
{
    size_t cursor = 0U;

    if (path == NULL || path[0] == L'\0') {
        return 0U;
    }
    if (((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) &&
        path[1] == L':')
    {
        return path[2] == L'\\' || path[2] == L'/' ? 3U : 2U;
    }
    if ((path[0] == L'\\' || path[0] == L'/') && (path[1] == L'\\' || path[1] == L'/')) {
        cursor = 2U;
        while (path[cursor] != L'\0' && path[cursor] != L'\\' && path[cursor] != L'/') {
            cursor += 1U;
        }
        while (path[cursor] == L'\\' || path[cursor] == L'/') {
            cursor += 1U;
        }
        while (path[cursor] != L'\0' && path[cursor] != L'\\' && path[cursor] != L'/') {
            cursor += 1U;
        }
        if (path[cursor] == L'\\' || path[cursor] == L'/') {
            cursor += 1U;
        }
        return cursor;
    }
    return path[0] == L'\\' || path[0] == L'/' ? 1U : 0U;
}

static SlStatus sl_fs_win32_join_wide(SlArena* arena, const wchar_t* root, const wchar_t* name,
                                      wchar_t** out)
{
    size_t root_len = 0U;
    size_t name_len = 0U;
    size_t total = 0U;
    size_t allocation_count = 0U;
    SlSlice storage = {0};
    wchar_t* joined = NULL;
    bool needs_sep = false;
    SlStatus status;

    if (arena == NULL || root == NULL || name == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = NULL;
    root_len = wcslen(root);
    name_len = wcslen(name);
    needs_sep = root_len != 0U && root[root_len - 1U] != L'\\' && root[root_len - 1U] != L'/';
    status = sl_checked_add_size(root_len, name_len, &allocation_count);
    if (sl_status_is_ok(status)) {
        status = sl_checked_add_size(allocation_count, needs_sep ? 2U : 1U, &allocation_count);
    }
    if (sl_status_is_ok(status)) {
        status = sl_arena_array_alloc(arena, allocation_count, sizeof(wchar_t), _Alignof(wchar_t),
                                      &storage);
    }
    if (!sl_status_is_ok(status)) {
        return status;
    }
    joined = (wchar_t*)storage.ptr;
    if (root_len != 0U) {
        sl_fs_win32_copy_wchars(joined, root, root_len);
        total = root_len;
    }
    if (needs_sep) {
        joined[total] = L'\\';
        total += 1U;
    }
    if (name_len != 0U) {
        sl_fs_win32_copy_wchars(joined + total, name, name_len);
        total += name_len;
    }
    joined[total] = L'\0';
    *out = joined;
    return sl_status_ok();
}

static SlStatus sl_fs_win32_join_utf8(SlArena* arena, SlStr directory, SlStr name, SlOwnedStr* out)
{
    size_t total = 0U;
    void* memory = NULL;
    bool needs_sep = directory.length != 0U && directory.ptr[directory.length - 1U] != '/' &&
                     directory.ptr[directory.length - 1U] != '\\';
    SlStatus status;

    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlOwnedStr){0};
    status = sl_checked_add_size(directory.length, name.length, &total);
    if (sl_status_is_ok(status)) {
        status = sl_checked_add_size(total, needs_sep ? 2U : 1U, &total);
    }
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(arena, total, _Alignof(char), &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    out->ptr = (char*)memory;
    if (directory.length != 0U) {
        sl_fs_win32_copy_chars(out->ptr, directory.ptr, directory.length);
        out->length = directory.length;
    }
    if (needs_sep) {
        out->ptr[out->length] = '\\';
        out->length += 1U;
    }
    if (name.length != 0U) {
        sl_fs_win32_copy_chars(out->ptr + out->length, name.ptr, name.length);
        out->length += name.length;
    }
    out->ptr[out->length] = '\0';
    return sl_status_ok();
}

static SlStatus sl_fs_win32_random_suffix(char* out, size_t length)
{
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    unsigned char bytes[16];
    size_t index = 0U;

    if (out == NULL || length > sizeof(bytes)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (BCryptGenRandom(NULL, bytes, (ULONG)length, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    for (index = 0U; index < length; index += 1U) {
        out[index] = alphabet[bytes[index] % (sizeof(alphabet) - 1U)];
    }
    return sl_status_ok();
}

SlStatus sl_fs_platform_read_file(SlArena* arena, SlStr path, SlOwnedBytes* out, SlDiag* out_diag)
{
    SlArenaMark mark;
    wchar_t* wide = NULL;
    HANDLE file = INVALID_HANDLE_VALUE;
    LARGE_INTEGER size;
    void* memory = NULL;
    size_t offset = 0U;
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
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0) {
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
    while (offset < (size_t)size.QuadPart) {
        DWORD read = 0;
        size_t remaining = (size_t)size.QuadPart - offset;
        DWORD chunk = remaining > (size_t)UINT32_MAX ? UINT32_MAX : (DWORD)remaining;

        if (!ReadFile(file, (unsigned char*)memory + offset, chunk, &read, NULL) || read != chunk) {
            CloseHandle(file);
            (void)sl_arena_reset_to(arena, mark);
            return sl_fs_win32_status(GetLastError(), out_diag,
                                      sl_str_from_cstr("filesystem read failed"));
        }
        offset += (size_t)read;
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
    {
        size_t offset = 0U;

        while (offset < bytes.length) {
            DWORD written = 0;
            size_t remaining = bytes.length - offset;
            DWORD chunk = remaining > (size_t)UINT32_MAX ? UINT32_MAX : (DWORD)remaining;

            if (!WriteFile(file, bytes.ptr + offset, chunk, &written, NULL) || written != chunk) {
                DWORD error = GetLastError();
                CloseHandle(file);
                return sl_fs_win32_status(error, out_diag,
                                          sl_str_from_cstr("filesystem write failed"));
            }
            offset += (size_t)written;
        }
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
    out->modified_nsec = sl_fs_win32_filetime_stamp(attrs.ftLastWriteTime);
    return sl_status_ok();
}

SlStatus sl_fs_platform_create_directory(SlStr path, bool recursive, SlDiag* out_diag)
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
    if (!recursive) {
        if (!CreateDirectoryW(wide, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
            return sl_fs_win32_status(GetLastError(), out_diag,
                                      sl_str_from_cstr("filesystem directory create failed"));
        }
        return sl_status_ok();
    }
    for (wchar_t* cursor = wide + sl_fs_win32_root_length(wide); *cursor != L'\0'; cursor += 1) {
        if (*cursor == L'/' || *cursor == L'\\') {
            wchar_t saved = *cursor;
            *cursor = L'\0';
            if (wide[0] != L'\0' && !CreateDirectoryW(wide, NULL) &&
                GetLastError() != ERROR_ALREADY_EXISTS)
            {
                DWORD error = GetLastError();
                *cursor = saved;
                return sl_fs_win32_status(error, out_diag,
                                          sl_str_from_cstr("filesystem directory create failed"));
            }
            *cursor = saved;
        }
    }
    if (!CreateDirectoryW(wide, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        return sl_fs_win32_status(GetLastError(), out_diag,
                                  sl_str_from_cstr("filesystem directory create failed"));
    }
    return sl_status_ok();
}

static SlStatus sl_fs_win32_delete_tree(SlArena* arena, const wchar_t* wide, SlDiag* out_diag)
{
    SlArenaMark tree_mark;
    wchar_t* pattern = NULL;
    WIN32_FIND_DATAW data;
    HANDLE find = INVALID_HANDLE_VALUE;
    SlStatus status;

    tree_mark = sl_arena_mark(arena);
    status = sl_fs_win32_join_wide(arena, wide, L"*", &pattern);
    if (!sl_status_is_ok(status)) {
        (void)sl_arena_reset_to(arena, tree_mark);
        return status;
    }
    find = FindFirstFileW(pattern, &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            SlArenaMark child_mark;
            wchar_t* child = NULL;
            if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) {
                continue;
            }
            child_mark = sl_arena_mark(arena);
            status = sl_fs_win32_join_wide(arena, wide, data.cFileName, &child);
            if (!sl_status_is_ok(status)) {
                FindClose(find);
                (void)sl_arena_reset_to(arena, tree_mark);
                return status;
            }
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                status = sl_fs_win32_delete_tree(arena, child, out_diag);
                if (!sl_status_is_ok(status)) {
                    FindClose(find);
                    (void)sl_arena_reset_to(arena, tree_mark);
                    return status;
                }
                if (!RemoveDirectoryW(child)) {
                    DWORD error = GetLastError();
                    FindClose(find);
                    (void)sl_arena_reset_to(arena, tree_mark);
                    return sl_fs_win32_status(
                        error, out_diag, sl_str_from_cstr("filesystem directory delete failed"));
                }
            }
            else if (!DeleteFileW(child)) {
                DWORD error = GetLastError();
                FindClose(find);
                (void)sl_arena_reset_to(arena, tree_mark);
                return sl_fs_win32_status(error, out_diag,
                                          sl_str_from_cstr("filesystem file delete failed"));
            }
            (void)sl_arena_reset_to(arena, child_mark);
        } while (FindNextFileW(find, &data));
        if (GetLastError() != ERROR_NO_MORE_FILES) {
            DWORD error = GetLastError();
            FindClose(find);
            (void)sl_arena_reset_to(arena, tree_mark);
            return sl_fs_win32_status(error, out_diag,
                                      sl_str_from_cstr("filesystem directory enumerate failed"));
        }
        FindClose(find);
    }
    (void)sl_arena_reset_to(arena, tree_mark);
    return sl_status_ok();
}

SlStatus sl_fs_platform_delete_directory(SlStr path, bool recursive, SlDiag* out_diag)
{
    unsigned char scratch[8192];
    SlArena arena = {0};
    wchar_t* wide = NULL;
    SlStatus status;

    status = sl_arena_init(&arena, scratch, sizeof(scratch));
    if (!sl_status_is_ok(status) ||
        !sl_status_is_ok((status = sl_fs_win32_path_to_wide(&arena, path, &wide))))
    {
        return status;
    }
    if (recursive) {
        status = sl_fs_win32_delete_tree(&arena, wide, out_diag);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    if (!RemoveDirectoryW(wide)) {
        return sl_fs_win32_status(GetLastError(), out_diag,
                                  sl_str_from_cstr("filesystem directory delete failed"));
    }
    return sl_status_ok();
}

SlStatus sl_fs_platform_list_directory(SlArena* arena, SlStr path, SlFsDirectoryList* out,
                                       SlDiag* out_diag)
{
    SlArenaMark mark;
    wchar_t* wide = NULL;
    wchar_t* pattern = NULL;
    WIN32_FIND_DATAW data;
    HANDLE find = INVALID_HANDLE_VALUE;
    SlSlice entries = {0};
    size_t count = 0U;
    SlStatus status;

    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlFsDirectoryList){0};
    mark = sl_arena_mark(arena);
    status = sl_fs_win32_path_to_wide(arena, path, &wide);
    if (sl_status_is_ok(status)) {
        status = sl_fs_win32_join_wide(arena, wide, L"*", &pattern);
    }
    if (!sl_status_is_ok(status)) {
        (void)sl_arena_reset_to(arena, mark);
        return status;
    }
    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) {
        (void)sl_arena_reset_to(arena, mark);
        return sl_fs_win32_status(GetLastError(), out_diag,
                                  sl_str_from_cstr("filesystem directory list failed"));
    }
    do {
        if (wcscmp(data.cFileName, L".") != 0 && wcscmp(data.cFileName, L"..") != 0) {
            count += 1U;
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    if (count != 0U) {
        status = sl_arena_array_alloc(arena, count, sizeof(SlFsDirectoryEntry),
                                      _Alignof(SlFsDirectoryEntry), &entries);
        if (!sl_status_is_ok(status)) {
            (void)sl_arena_reset_to(arena, mark);
            return status;
        }
        if (entries.ptr == NULL) {
            (void)sl_arena_reset_to(arena, mark);
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
        out->entries = (SlFsDirectoryEntry*)entries.ptr;
    }
    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) {
        (void)sl_arena_reset_to(arena, mark);
        return sl_fs_win32_status(GetLastError(), out_diag,
                                  sl_str_from_cstr("filesystem directory list failed"));
    }
    do {
        SlFsDirectoryEntry* item = NULL;
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) {
            continue;
        }
        if (out->entries == NULL) {
            FindClose(find);
            (void)sl_arena_reset_to(arena, mark);
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
        item = &out->entries[out->count];
        *item = (SlFsDirectoryEntry){0};
        status = sl_fs_win32_wide_to_utf8(arena, data.cFileName, &item->name);
        if (!sl_status_is_ok(status)) {
            FindClose(find);
            (void)sl_arena_reset_to(arena, mark);
            return status;
        }
        item->kind = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ? SL_FS_NODE_DIRECTORY
                                                                             : SL_FS_NODE_FILE;
        item->size = ((uint64_t)data.nFileSizeHigh << 32U) | (uint64_t)data.nFileSizeLow;
        item->modified_nsec = sl_fs_win32_filetime_stamp(data.ftLastWriteTime);
        out->count += 1U;
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return sl_status_ok();
}

SlStatus sl_fs_platform_create_symlink(SlStr target_path, SlStr link_path, bool directory,
                                       SlDiag* out_diag)
{
    unsigned char scratch[8192];
    SlArena arena = {0};
    wchar_t* target = NULL;
    wchar_t* link = NULL;
    DWORD flags = directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0U;
    SlStatus status;

    status = sl_arena_init(&arena, scratch, sizeof(scratch));
    if (!sl_status_is_ok(status) ||
        !sl_status_is_ok((status = sl_fs_win32_path_to_wide(&arena, target_path, &target))) ||
        !sl_status_is_ok((status = sl_fs_win32_path_to_wide(&arena, link_path, &link))))
    {
        return status;
    }
    if (!CreateSymbolicLinkW(link, target, flags)) {
        return sl_fs_win32_status(GetLastError(), out_diag,
                                  sl_str_from_cstr("filesystem symlink failed"));
    }
    return sl_status_ok();
}

SlStatus sl_fs_platform_read_link(SlArena* arena, SlStr path, SlOwnedStr* out, SlDiag* out_diag)
{
    wchar_t* wide = NULL;
    HANDLE file = INVALID_HANDLE_VALUE;
    wchar_t target[MAXIMUM_REPARSE_DATA_BUFFER_SIZE / sizeof(wchar_t)];
    FILE_ATTRIBUTE_TAG_INFO tag_info = {0};
    DWORD read = 0;
    SlStatus status;

    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlOwnedStr){0};
    status = sl_fs_win32_path_to_wide(arena, path, &wide);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    file = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING,
                       FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return sl_fs_win32_status(GetLastError(), out_diag,
                                  sl_str_from_cstr("filesystem readlink failed"));
    }
    if (!GetFileInformationByHandleEx(file, FileAttributeTagInfo, &tag_info, sizeof(tag_info)) ||
        (tag_info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0U ||
        tag_info.ReparseTag != IO_REPARSE_TAG_SYMLINK)
    {
        CloseHandle(file);
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    read = GetFinalPathNameByHandleW(file, target, (DWORD)(sizeof(target) / sizeof(target[0])),
                                     FILE_NAME_NORMALIZED);
    CloseHandle(file);
    if (read == 0 || read >= (DWORD)(sizeof(target) / sizeof(target[0]))) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    target[read] = L'\0';
    return sl_fs_win32_wide_to_utf8(arena, target, out);
}

static SlStatus sl_fs_win32_temp_path(SlArena* arena, SlStr directory, SlStr prefix,
                                      SlOwnedStr* out)
{
    char suffix[12] = {0};
    SlOwnedStr name = {0};
    size_t total = 0U;
    void* memory = NULL;
    SlStatus status;

    status = sl_fs_win32_random_suffix(suffix, sizeof(suffix));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_checked_add_size(prefix.length, sizeof(suffix), &total);
    if (!sl_status_is_ok(status) ||
        !sl_status_is_ok((status = sl_arena_alloc(arena, total + 1U, _Alignof(char), &memory))))
    {
        return status;
    }
    if (memory == NULL) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    name.ptr = (char*)memory;
    sl_fs_win32_copy_chars(name.ptr, prefix.ptr, prefix.length);
    sl_fs_win32_copy_chars(name.ptr + prefix.length, suffix, sizeof(suffix));
    name.length = total;
    name.ptr[name.length] = '\0';
    return sl_fs_win32_join_utf8(arena, directory, sl_owned_str_as_view(name), out);
}

SlStatus sl_fs_platform_create_temp_file(SlArena* arena, SlStr directory, SlStr prefix,
                                         SlFsTempPath* out, SlDiag* out_diag)
{
    SlOwnedStr path = {0};
    wchar_t* wide = NULL;
    HANDLE file = INVALID_HANDLE_VALUE;
    SlStatus status = sl_fs_win32_temp_path(arena, directory, prefix, &path);

    if (sl_status_is_ok(status)) {
        status = sl_fs_win32_path_to_wide(arena, sl_owned_str_as_view(path), &wide);
    }
    if (!sl_status_is_ok(status)) {
        return status;
    }
    file = CreateFileW(wide, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return sl_fs_win32_status(GetLastError(), out_diag,
                                  sl_str_from_cstr("filesystem temp file failed"));
    }
    CloseHandle(file);
    out->path = path;
    return sl_status_ok();
}

SlStatus sl_fs_platform_create_temp_directory(SlArena* arena, SlStr directory, SlStr prefix,
                                              SlFsTempPath* out, SlDiag* out_diag)
{
    SlOwnedStr path = {0};
    wchar_t* wide = NULL;
    SlStatus status = sl_fs_win32_temp_path(arena, directory, prefix, &path);

    if (sl_status_is_ok(status)) {
        status = sl_fs_win32_path_to_wide(arena, sl_owned_str_as_view(path), &wide);
    }
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (!CreateDirectoryW(wide, NULL)) {
        return sl_fs_win32_status(GetLastError(), out_diag,
                                  sl_str_from_cstr("filesystem temp directory failed"));
    }
    out->path = path;
    return sl_status_ok();
}

SlStatus sl_fs_platform_atomic_write_file(SlArena* arena, SlStr path, SlBytes bytes,
                                          SlDiag* out_diag)
{
    SlArenaMark mark = sl_arena_mark(arena);
    size_t split = path.length;
    SlStr directory = {0};
    SlFsTempPath temp = {0};
    SlStatus status;

    while (split > 0U && path.ptr[split - 1U] != '/' && path.ptr[split - 1U] != '\\') {
        split -= 1U;
    }
    directory = split == 0U ? sl_str_from_cstr(".") : sl_str_from_parts(path.ptr, split);
    status = sl_fs_platform_create_temp_file(arena, directory, sl_str_from_cstr(".sloppy-atomic-"),
                                             &temp, out_diag);
    if (sl_status_is_ok(status)) {
        status = sl_fs_platform_write_file(sl_owned_str_as_view(temp.path), bytes, false, out_diag);
    }
    if (sl_status_is_ok(status)) {
        status = sl_fs_platform_move_file(sl_owned_str_as_view(temp.path), path, true, out_diag);
    }
    if (!sl_status_is_ok(status)) {
        (void)sl_fs_platform_delete_file(sl_owned_str_as_view(temp.path), NULL);
    }
    (void)sl_arena_reset_to(arena, mark);
    return status;
}

SlStatus sl_fs_platform_acquire_lock(SlArena* arena, SlStr path, SlFsFileLock** out_lock,
                                     SlDiag* out_diag)
{
    wchar_t* wide = NULL;
    void* memory = NULL;
    SlFsFileLock* lock = NULL;
    SlStatus status;

    if (arena == NULL || out_lock == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_lock = NULL;
    status = sl_arena_alloc(arena, sizeof(SlFsFileLock), _Alignof(SlFsFileLock), &memory);
    if (sl_status_is_ok(status)) {
        lock = (SlFsFileLock*)memory;
        if (lock == NULL) {
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
        *lock = (SlFsFileLock){.handle = INVALID_HANDLE_VALUE, .path = {0}, .closed = true};
        status = sl_str_copy_to_arena_cstr(arena, path, &lock->path);
    }
    if (sl_status_is_ok(status)) {
        status = sl_fs_win32_path_to_wide(arena, path, &wide);
    }
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (lock == NULL) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    lock->handle =
        CreateFileW(wide, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (lock->handle == INVALID_HANDLE_VALUE) {
        return sl_fs_win32_status(GetLastError(), out_diag,
                                  sl_str_from_cstr("filesystem lock failed"));
    }
    lock->closed = false;
    *out_lock = lock;
    return sl_status_ok();
}

SlStatus sl_fs_platform_release_lock(SlFsFileLock* lock, SlDiag* out_diag)
{
    (void)out_diag;
    if (lock == NULL || lock->closed) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    CloseHandle(lock->handle);
    (void)sl_fs_platform_delete_file(sl_owned_str_as_view(lock->path), NULL);
    lock->handle = INVALID_HANDLE_VALUE;
    lock->closed = true;
    return sl_status_ok();
}

SlStatus sl_fs_platform_open_file(SlArena* arena, SlStr path, SlFsFileAccess access, bool create,
                                  SlFsFileHandle** out_handle, SlDiag* out_diag)
{
    unsigned char scratch[4096];
    SlArena local = {0};
    wchar_t* wide = NULL;
    SlFsFileHandle* handle = NULL;
    DWORD desired = GENERIC_READ;
    DWORD disposition = OPEN_EXISTING;
    SlStatus status;

    status = sl_arena_init(&local, scratch, sizeof(scratch));
    if (!sl_status_is_ok(status) ||
        !sl_status_is_ok((status = sl_fs_win32_path_to_wide(&local, path, &wide))) ||
        !sl_status_is_ok((status = sl_fs_win32_alloc_handle(arena, &handle))))
    {
        return status;
    }
    if (handle == NULL) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    if (access == SL_FS_FILE_ACCESS_WRITE) {
        desired = GENERIC_WRITE;
        disposition = create ? OPEN_ALWAYS : OPEN_EXISTING;
    }
    else if (access == SL_FS_FILE_ACCESS_READWRITE) {
        desired = GENERIC_READ | GENERIC_WRITE;
        disposition = create ? OPEN_ALWAYS : OPEN_EXISTING;
    }
    else if (access == SL_FS_FILE_ACCESS_APPEND) {
        desired = FILE_APPEND_DATA;
        disposition = create ? OPEN_ALWAYS : OPEN_EXISTING;
    }
    handle->handle =
        CreateFileW(wide, desired, FILE_SHARE_READ, NULL, disposition, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle->handle == INVALID_HANDLE_VALUE) {
        return sl_fs_win32_status(GetLastError(), out_diag,
                                  sl_str_from_cstr("filesystem open failed"));
    }
    handle->closed = false;
    *out_handle = handle;
    return sl_status_ok();
}

SlStatus sl_fs_platform_file_read(SlFsFileHandle* handle, SlArena* arena, size_t max_bytes,
                                  SlOwnedBytes* out, SlDiag* out_diag)
{
    void* memory = NULL;
    DWORD read = 0;
    DWORD chunk = max_bytes > (size_t)UINT32_MAX ? UINT32_MAX : (DWORD)max_bytes;
    SlStatus status;

    if (handle == NULL || handle->closed || arena == NULL || out == NULL || max_bytes == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    status = sl_arena_alloc(arena, chunk, _Alignof(unsigned char), &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (!ReadFile(handle->handle, memory, chunk, &read, NULL)) {
        return sl_fs_win32_status(GetLastError(), out_diag,
                                  sl_str_from_cstr("filesystem handle read failed"));
    }
    out->ptr = (unsigned char*)memory;
    out->length = (size_t)read;
    return sl_status_ok();
}

SlStatus sl_fs_platform_file_write(SlFsFileHandle* handle, SlBytes bytes, SlDiag* out_diag)
{
    size_t offset = 0U;

    if (handle == NULL || handle->closed) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    while (offset < bytes.length) {
        DWORD written = 0;
        size_t remaining = bytes.length - offset;
        DWORD chunk = remaining > (size_t)UINT32_MAX ? UINT32_MAX : (DWORD)remaining;

        if (!WriteFile(handle->handle, bytes.ptr + offset, chunk, &written, NULL) ||
            written != chunk)
        {
            return sl_fs_win32_status(GetLastError(), out_diag,
                                      sl_str_from_cstr("filesystem handle write failed"));
        }
        offset += (size_t)written;
    }
    return sl_status_ok();
}

SlStatus sl_fs_platform_file_seek(SlFsFileHandle* handle, int64_t offset, SlFsSeekOrigin origin,
                                  uint64_t* out_position, SlDiag* out_diag)
{
    LARGE_INTEGER distance;
    LARGE_INTEGER position;
    DWORD method = origin == SL_FS_SEEK_CURRENT ? FILE_CURRENT
                   : origin == SL_FS_SEEK_END   ? FILE_END
                                                : FILE_BEGIN;

    if (handle == NULL || handle->closed || out_position == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    distance.QuadPart = offset;
    if (!SetFilePointerEx(handle->handle, distance, &position, method)) {
        return sl_fs_win32_status(GetLastError(), out_diag,
                                  sl_str_from_cstr("filesystem handle seek failed"));
    }
    *out_position = (uint64_t)position.QuadPart;
    return sl_status_ok();
}

SlStatus sl_fs_platform_file_truncate(SlFsFileHandle* handle, uint64_t size, SlDiag* out_diag)
{
    LARGE_INTEGER target;

    if (handle == NULL || handle->closed) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    target.QuadPart = (LONGLONG)size;
    if (!SetFilePointerEx(handle->handle, target, NULL, FILE_BEGIN) ||
        !SetEndOfFile(handle->handle))
    {
        return sl_fs_win32_status(GetLastError(), out_diag,
                                  sl_str_from_cstr("filesystem handle truncate failed"));
    }
    return sl_status_ok();
}

SlStatus sl_fs_platform_file_flush(SlFsFileHandle* handle, SlDiag* out_diag)
{
    (void)out_diag;
    return handle == NULL || handle->closed ? sl_status_from_code(SL_STATUS_INVALID_STATE)
                                            : sl_status_ok();
}

SlStatus sl_fs_platform_file_sync(SlFsFileHandle* handle, SlDiag* out_diag)
{
    if (handle == NULL || handle->closed) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    if (!FlushFileBuffers(handle->handle)) {
        return sl_fs_win32_status(GetLastError(), out_diag,
                                  sl_str_from_cstr("filesystem handle sync failed"));
    }
    return sl_status_ok();
}

SlStatus sl_fs_platform_file_close(SlFsFileHandle* handle, SlDiag* out_diag)
{
    HANDLE owned = INVALID_HANDLE_VALUE;

    (void)out_diag;
    if (handle == NULL || handle->closed) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    owned = handle->handle;
    handle->handle = INVALID_HANDLE_VALUE;
    handle->closed = true;
    return CloseHandle(owned) ? sl_status_ok()
                              : sl_fs_win32_status(GetLastError(), out_diag,
                                                   sl_str_from_cstr("filesystem handle close "
                                                                    "failed"));
}

/*
 * src/platform/posix/fs_posix.c
 *
 * POSIX filesystem backend. POSIX path and file-descriptor behavior stays here.
 */
#include "../../core/fs_platform.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static SlStatus sl_fs_posix_path(SlArena* arena, SlStr path, SlOwnedStr* out)
{
    return sl_str_copy_to_arena_nul(arena, path, out);
}

static SlStatus sl_fs_posix_status(int error)
{
    if (error == ENOENT || error == ENOTDIR) {
        return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
    }
    if (error == EACCES || error == EPERM || error == EEXIST) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    return sl_status_from_code(SL_STATUS_INTERNAL);
}

SlStatus sl_fs_platform_read_file(SlArena* arena, SlStr path, SlOwnedBytes* out, SlDiag* out_diag)
{
    SlArenaMark mark;
    SlOwnedStr native = {0};
    struct stat st;
    int fd = -1;
    void* memory = NULL;
    size_t offset = 0U;
    SlStatus status;

    (void)out_diag;
    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlOwnedBytes){0};
    mark = sl_arena_mark(arena);
    status = sl_fs_posix_path(arena, path, &native);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    fd = open(native.ptr, O_RDONLY);
    if (fd < 0) {
        (void)sl_arena_reset_to(arena, mark);
        return sl_fs_posix_status(errno);
    }
    if (fstat(fd, &st) != 0 || st.st_size < 0) {
        close(fd);
        (void)sl_arena_reset_to(arena, mark);
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }
    if (st.st_size == 0) {
        close(fd);
        return sl_status_ok();
    }
    status = sl_arena_alloc(arena, (size_t)st.st_size, _Alignof(unsigned char), &memory);
    if (!sl_status_is_ok(status)) {
        close(fd);
        return status;
    }
    while (offset < (size_t)st.st_size) {
        ssize_t n = read(fd, (unsigned char*)memory + offset, (size_t)st.st_size - offset);
        if (n <= 0) {
            close(fd);
            (void)sl_arena_reset_to(arena, mark);
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
        offset += (size_t)n;
    }
    close(fd);
    out->ptr = (unsigned char*)memory;
    out->length = (size_t)st.st_size;
    return sl_status_ok();
}

SlStatus sl_fs_platform_write_file(SlStr path, SlBytes bytes, bool append, SlDiag* out_diag)
{
    unsigned char scratch[4096];
    SlArena arena = {0};
    SlOwnedStr native = {0};
    int fd = -1;
    int flags = O_WRONLY | O_CREAT;
    size_t offset = 0U;
    SlStatus status;

    (void)out_diag;
    if (append) {
        flags |= O_APPEND;
    }
    else {
        flags |= O_TRUNC;
    }
    status = sl_arena_init(&arena, scratch, sizeof(scratch));
    if (!sl_status_is_ok(status) ||
        !sl_status_is_ok((status = sl_fs_posix_path(&arena, path, &native))))
    {
        return status;
    }
    fd = open(native.ptr, flags, 0666);
    if (fd < 0) {
        return sl_fs_posix_status(errno);
    }
    while (offset < bytes.length) {
        ssize_t n = write(fd, bytes.ptr + offset, bytes.length - offset);
        if (n <= 0) {
            close(fd);
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
        offset += (size_t)n;
    }
    close(fd);
    return sl_status_ok();
}

SlStatus sl_fs_platform_copy_file(SlStr from_path, SlStr to_path, bool overwrite, SlDiag* out_diag)
{
    unsigned char storage[1048576];
    SlArena arena = {0};
    SlOwnedBytes bytes = {0};
    SlFsStat stat = {0};
    SlStatus status;

    status = sl_arena_init(&arena, storage, sizeof(storage));
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (!overwrite && sl_status_is_ok(sl_fs_platform_stat(to_path, &stat, NULL)) && stat.exists) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    status = sl_fs_platform_read_file(&arena, from_path, &bytes, out_diag);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_fs_platform_write_file(to_path, sl_owned_bytes_as_view(bytes), false, out_diag);
}

SlStatus sl_fs_platform_move_file(SlStr from_path, SlStr to_path, bool overwrite, SlDiag* out_diag)
{
    unsigned char scratch[8192];
    SlArena arena = {0};
    SlOwnedStr from_native = {0};
    SlOwnedStr to_native = {0};
    SlFsStat stat = {0};
    SlStatus status;

    (void)out_diag;
    if (!overwrite && sl_status_is_ok(sl_fs_platform_stat(to_path, &stat, NULL)) && stat.exists) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    status = sl_arena_init(&arena, scratch, sizeof(scratch));
    if (!sl_status_is_ok(status) ||
        !sl_status_is_ok((status = sl_fs_posix_path(&arena, from_path, &from_native))) ||
        !sl_status_is_ok((status = sl_fs_posix_path(&arena, to_path, &to_native))))
    {
        return status;
    }
    if (rename(from_native.ptr, to_native.ptr) != 0) {
        return sl_fs_posix_status(errno);
    }
    return sl_status_ok();
}

SlStatus sl_fs_platform_delete_file(SlStr path, SlDiag* out_diag)
{
    unsigned char scratch[4096];
    SlArena arena = {0};
    SlOwnedStr native = {0};
    SlStatus status;

    (void)out_diag;
    status = sl_arena_init(&arena, scratch, sizeof(scratch));
    if (!sl_status_is_ok(status) ||
        !sl_status_is_ok((status = sl_fs_posix_path(&arena, path, &native))))
    {
        return status;
    }
    if (unlink(native.ptr) != 0) {
        return sl_fs_posix_status(errno);
    }
    return sl_status_ok();
}

SlStatus sl_fs_platform_stat(SlStr path, SlFsStat* out, SlDiag* out_diag)
{
    unsigned char scratch[4096];
    SlArena arena = {0};
    SlOwnedStr native = {0};
    struct stat st;
    SlStatus status;

    (void)out_diag;
    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlFsStat){0};
    status = sl_arena_init(&arena, scratch, sizeof(scratch));
    if (!sl_status_is_ok(status) ||
        !sl_status_is_ok((status = sl_fs_posix_path(&arena, path, &native))))
    {
        return status;
    }
    if (stat(native.ptr, &st) != 0) {
        return sl_fs_posix_status(errno);
    }
    out->exists = true;
    if (S_ISREG(st.st_mode)) {
        out->kind = SL_FS_NODE_FILE;
    }
    else if (S_ISDIR(st.st_mode)) {
        out->kind = SL_FS_NODE_DIRECTORY;
    }
    else {
        out->kind = SL_FS_NODE_OTHER;
    }
    out->size = (uint64_t)st.st_size;
    return sl_status_ok();
}

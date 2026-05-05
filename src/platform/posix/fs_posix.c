/*
 * src/platform/posix/fs_posix.c
 *
 * POSIX filesystem backend. POSIX path and file-descriptor behavior stays here.
 */
#include "../../core/fs_platform.h"

#include "sloppy/checked_math.h"

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

struct SlFsFileHandle
{
    int fd;
    bool closed;
};

struct SlFsFileLock
{
    int fd;
    SlOwnedStr path;
    bool closed;
};

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

static uint64_t sl_fs_posix_modified_stamp(const struct stat* st)
{
    if (st == NULL) {
        return 0U;
    }
#if defined(__APPLE__)
    return ((uint64_t)st->st_mtimespec.tv_sec * 1000000000ULL) + (uint64_t)st->st_mtimespec.tv_nsec;
#elif defined(st_mtim)
    return ((uint64_t)st->st_mtim.tv_sec * 1000000000ULL) + (uint64_t)st->st_mtim.tv_nsec;
#else
    return (uint64_t)st->st_mtime * 1000000000ULL;
#endif
}

static SlStatus sl_fs_posix_write_all(int fd, const unsigned char* bytes, size_t length)
{
    size_t offset = 0U;

    while (offset < length) {
        ssize_t n = write(fd, bytes + offset, length - offset);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return sl_status_from_code(SL_STATUS_INTERNAL);
        }
        offset += (size_t)n;
    }
    return sl_status_ok();
}

static void sl_fs_posix_copy_chars(char* dst, const char* src, size_t length)
{
    size_t index = 0U;

    for (index = 0U; index < length; index += 1U) {
        dst[index] = src[index];
    }
}

static SlStatus sl_fs_posix_alloc_handle(SlArena* arena, SlFsFileHandle** out)
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
    *out = (SlFsFileHandle*)memory;
    **out = (SlFsFileHandle){.fd = -1, .closed = true};
    return sl_status_ok();
}

static SlStatus sl_fs_posix_join(SlArena* arena, SlStr directory, SlStr name, SlOwnedStr* out)
{
    size_t total = 0U;
    void* memory = NULL;
    SlStatus status;
    bool needs_sep = directory.length != 0U && directory.ptr[directory.length - 1U] != '/';

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
        sl_fs_posix_copy_chars(out->ptr, directory.ptr, directory.length);
        out->length = directory.length;
    }
    if (needs_sep) {
        out->ptr[out->length] = '/';
        out->length += 1U;
    }
    if (name.length != 0U) {
        sl_fs_posix_copy_chars(out->ptr + out->length, name.ptr, name.length);
        out->length += name.length;
    }
    out->ptr[out->length] = '\0';
    return sl_status_ok();
}

static SlStatus sl_fs_posix_temp_template(SlArena* arena, SlStr directory, SlStr prefix,
                                          SlOwnedStr* out)
{
    static const char suffix[] = "XXXXXX";
    SlOwnedStr name = {0};
    size_t length = 0U;
    void* memory = NULL;
    SlStatus status;

    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_checked_add_size(prefix.length, sizeof(suffix) - 1U, &length);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_arena_alloc(arena, length + 1U, _Alignof(char), &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    name.ptr = (char*)memory;
    if (prefix.length != 0U) {
        sl_fs_posix_copy_chars(name.ptr, prefix.ptr, prefix.length);
    }
    sl_fs_posix_copy_chars(name.ptr + prefix.length, suffix, sizeof(suffix) - 1U);
    name.length = length;
    name.ptr[name.length] = '\0';
    return sl_fs_posix_join(arena, directory, sl_owned_str_as_view(name), out);
}

static SlStatus sl_fs_posix_remove_tree_fd(int dirfd)
{
    DIR* dir = fdopendir(dirfd);
    struct dirent* entry = NULL;

    if (dir == NULL) {
        close(dirfd);
        return sl_fs_posix_status(errno);
    }
    errno = 0;
    while ((entry = readdir(dir)) != NULL) {
        int child = -1;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            errno = 0;
            continue;
        }
        if (unlinkat(dirfd, entry->d_name, 0) == 0) {
            errno = 0;
            continue;
        }
        if (errno != EISDIR && errno != EPERM) {
            int error = errno;
            closedir(dir);
            return sl_fs_posix_status(error);
        }
        child = openat(dirfd, entry->d_name, O_RDONLY | O_DIRECTORY);
        if (child < 0) {
            int error = errno;
            closedir(dir);
            return sl_fs_posix_status(error);
        }
        {
            SlStatus status = sl_fs_posix_remove_tree_fd(child);
            if (!sl_status_is_ok(status)) {
                closedir(dir);
                return status;
            }
        }
        if (unlinkat(dirfd, entry->d_name, AT_REMOVEDIR) != 0) {
            int error = errno;
            closedir(dir);
            return sl_fs_posix_status(error);
        }
        errno = 0;
    }
    if (errno != 0) {
        int error = errno;
        closedir(dir);
        return sl_fs_posix_status(error);
    }
    closedir(dir);
    return sl_status_ok();
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
        if (n < 0 && errno == EINTR) {
            continue;
        }
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
    fd = open(native.ptr, flags, 0600);
    if (fd < 0) {
        return sl_fs_posix_status(errno);
    }
    status = sl_fs_posix_write_all(fd, bytes.ptr, bytes.length);
    close(fd);
    return status;
}

SlStatus sl_fs_platform_copy_file(SlStr from_path, SlStr to_path, bool overwrite, SlDiag* out_diag)
{
    unsigned char scratch[8192];
    unsigned char chunk[65536];
    SlArena arena = {0};
    SlOwnedStr from_native = {0};
    SlOwnedStr to_native = {0};
    int from_fd = -1;
    int to_fd = -1;
    int to_flags = O_WRONLY | O_CREAT | O_TRUNC;
    SlStatus status;

    (void)out_diag;
    if (!overwrite) {
        to_flags |= O_EXCL;
    }
    status = sl_arena_init(&arena, scratch, sizeof(scratch));
    if (!sl_status_is_ok(status) ||
        !sl_status_is_ok((status = sl_fs_posix_path(&arena, from_path, &from_native))) ||
        !sl_status_is_ok((status = sl_fs_posix_path(&arena, to_path, &to_native))))
    {
        return status;
    }
    from_fd = open(from_native.ptr, O_RDONLY);
    if (from_fd < 0) {
        return sl_fs_posix_status(errno);
    }
    to_fd = open(to_native.ptr, to_flags, 0600);
    if (to_fd < 0) {
        status = sl_fs_posix_status(errno);
        close(from_fd);
        return status;
    }

    for (;;) {
        ssize_t n = read(from_fd, chunk, sizeof(chunk));
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0) {
            status = sl_status_from_code(SL_STATUS_INTERNAL);
            break;
        }
        if (n == 0) {
            status = sl_status_ok();
            break;
        }
        status = sl_fs_posix_write_all(to_fd, chunk, (size_t)n);
        if (!sl_status_is_ok(status)) {
            break;
        }
    }
    close(from_fd);
    close(to_fd);
    if (!sl_status_is_ok(status)) {
        (void)unlink(to_native.ptr);
    }
    return status;
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
    out->modified_nsec = sl_fs_posix_modified_stamp(&st);
    return sl_status_ok();
}

SlStatus sl_fs_platform_create_directory(SlStr path, bool recursive, SlDiag* out_diag)
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
    if (!recursive) {
        return mkdir(native.ptr, 0700) == 0 || errno == EEXIST ? sl_status_ok()
                                                               : sl_fs_posix_status(errno);
    }

    for (char* cursor = native.ptr + 1; *cursor != '\0'; cursor += 1) {
        if (*cursor == '/') {
            *cursor = '\0';
            if (native.ptr[0] != '\0' && mkdir(native.ptr, 0700) != 0 && errno != EEXIST) {
                int error = errno;
                *cursor = '/';
                return sl_fs_posix_status(error);
            }
            *cursor = '/';
        }
    }
    return mkdir(native.ptr, 0700) == 0 || errno == EEXIST ? sl_status_ok()
                                                           : sl_fs_posix_status(errno);
}

SlStatus sl_fs_platform_delete_directory(SlStr path, bool recursive, SlDiag* out_diag)
{
    unsigned char scratch[4096];
    SlArena arena = {0};
    SlOwnedStr native = {0};
    SlStatus status;
    int fd = -1;

    (void)out_diag;
    status = sl_arena_init(&arena, scratch, sizeof(scratch));
    if (!sl_status_is_ok(status) ||
        !sl_status_is_ok((status = sl_fs_posix_path(&arena, path, &native))))
    {
        return status;
    }
    if (!recursive) {
        return rmdir(native.ptr) == 0 ? sl_status_ok() : sl_fs_posix_status(errno);
    }
    fd = open(native.ptr, O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        return sl_fs_posix_status(errno);
    }
    status = sl_fs_posix_remove_tree_fd(fd);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return rmdir(native.ptr) == 0 ? sl_status_ok() : sl_fs_posix_status(errno);
}

SlStatus sl_fs_platform_list_directory(SlArena* arena, SlStr path, SlFsDirectoryList* out,
                                       SlDiag* out_diag)
{
    SlArenaMark mark;
    SlOwnedStr native = {0};
    DIR* dir = NULL;
    struct dirent* entry = NULL;
    size_t count = 0U;
    SlStatus status;
    void* memory = NULL;

    (void)out_diag;
    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlFsDirectoryList){0};
    mark = sl_arena_mark(arena);
    status = sl_fs_posix_path(arena, path, &native);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    dir = opendir(native.ptr);
    if (dir == NULL) {
        (void)sl_arena_reset_to(arena, mark);
        return sl_fs_posix_status(errno);
    }
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            count += 1U;
        }
    }
    rewinddir(dir);
    if (count != 0U) {
        size_t bytes = 0U;
        status = sl_checked_mul_size(count, sizeof(SlFsDirectoryEntry), &bytes);
        if (sl_status_is_ok(status)) {
            status = sl_arena_alloc(arena, bytes, _Alignof(SlFsDirectoryEntry), &memory);
        }
        if (!sl_status_is_ok(status)) {
            closedir(dir);
            (void)sl_arena_reset_to(arena, mark);
            return status;
        }
        out->entries = (SlFsDirectoryEntry*)memory;
    }
    errno = 0;
    while ((entry = readdir(dir)) != NULL) {
        SlFsDirectoryEntry* item = NULL;
        SlOwnedStr child_path = {0};
        SlFsStat stat = {0};
        SlStr name;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        item = &out->entries[out->count];
        *item = (SlFsDirectoryEntry){0};
        name = sl_str_from_cstr(entry->d_name);
        status = sl_str_copy_to_arena(arena, name, &item->name);
        if (sl_status_is_ok(status)) {
            status = sl_fs_posix_join(arena, path, name, &child_path);
        }
        if (sl_status_is_ok(status) &&
            sl_status_is_ok(sl_fs_platform_stat(sl_owned_str_as_view(child_path), &stat, NULL)))
        {
            item->kind = stat.kind;
            item->size = stat.size;
            item->modified_nsec = stat.modified_nsec;
        }
        else if (sl_status_is_ok(status)) {
            item->kind = SL_FS_NODE_OTHER;
        }
        if (!sl_status_is_ok(status)) {
            closedir(dir);
            (void)sl_arena_reset_to(arena, mark);
            return status;
        }
        out->count += 1U;
    }
    if (errno != 0) {
        int error = errno;
        closedir(dir);
        (void)sl_arena_reset_to(arena, mark);
        return sl_fs_posix_status(error);
    }
    closedir(dir);
    return sl_status_ok();
}

SlStatus sl_fs_platform_create_symlink(SlStr target_path, SlStr link_path, bool directory,
                                       SlDiag* out_diag)
{
    unsigned char scratch[8192];
    SlArena arena = {0};
    SlOwnedStr target = {0};
    SlOwnedStr link = {0};
    SlStatus status;

    (void)directory;
    (void)out_diag;
    status = sl_arena_init(&arena, scratch, sizeof(scratch));
    if (!sl_status_is_ok(status) ||
        !sl_status_is_ok((status = sl_fs_posix_path(&arena, target_path, &target))) ||
        !sl_status_is_ok((status = sl_fs_posix_path(&arena, link_path, &link))))
    {
        return status;
    }
    return symlink(target.ptr, link.ptr) == 0 ? sl_status_ok() : sl_fs_posix_status(errno);
}

SlStatus sl_fs_platform_read_link(SlArena* arena, SlStr path, SlOwnedStr* out, SlDiag* out_diag)
{
    SlArenaMark mark;
    SlOwnedStr native = {0};
    void* memory = NULL;
    ssize_t length = 0;
    size_t capacity = 4096U;
    SlStatus status;

    (void)out_diag;
    if (arena == NULL || out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = (SlOwnedStr){0};
    mark = sl_arena_mark(arena);
    status = sl_fs_posix_path(arena, path, &native);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    for (;;) {
        status = sl_arena_alloc(arena, capacity + 1U, _Alignof(char), &memory);
        if (!sl_status_is_ok(status)) {
            (void)sl_arena_reset_to(arena, mark);
            return status;
        }
        length = readlink(native.ptr, (char*)memory, capacity);
        if (length < 0) {
            int error = errno;
            (void)sl_arena_reset_to(arena, mark);
            return sl_fs_posix_status(error);
        }
        if ((size_t)length < capacity) {
            break;
        }
        if (capacity > (SIZE_MAX / 2U)) {
            (void)sl_arena_reset_to(arena, mark);
            return sl_status_from_code(SL_STATUS_OVERFLOW);
        }
        capacity *= 2U;
        (void)sl_arena_reset_to(arena, mark);
        status = sl_fs_posix_path(arena, path, &native);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    ((char*)memory)[length] = '\0';
    out->ptr = (char*)memory;
    out->length = (size_t)length;
    return sl_status_ok();
}

SlStatus sl_fs_platform_create_temp_file(SlArena* arena, SlStr directory, SlStr prefix,
                                         SlFsTempPath* out, SlDiag* out_diag)
{
    SlOwnedStr template_path = {0};
    SlStatus status;
    int fd = -1;

    (void)out_diag;
    status = sl_fs_posix_temp_template(arena, directory, prefix, &template_path);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    fd = mkstemp(template_path.ptr);
    if (fd < 0) {
        return sl_fs_posix_status(errno);
    }
    close(fd);
    out->path = template_path;
    return sl_status_ok();
}

SlStatus sl_fs_platform_create_temp_directory(SlArena* arena, SlStr directory, SlStr prefix,
                                              SlFsTempPath* out, SlDiag* out_diag)
{
    SlOwnedStr template_path = {0};
    SlStatus status;

    (void)out_diag;
    status = sl_fs_posix_temp_template(arena, directory, prefix, &template_path);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (mkdtemp(template_path.ptr) == NULL) {
        return sl_fs_posix_status(errno);
    }
    out->path = template_path;
    return sl_status_ok();
}

SlStatus sl_fs_platform_atomic_write_file(SlArena* arena, SlStr path, SlBytes bytes,
                                          SlDiag* out_diag)
{
    SlArenaMark mark = sl_arena_mark(arena);
    SlOwnedStr native = {0};
    SlOwnedStr target_native = {0};
    SlStr directory = {0};
    size_t split = path.length;
    SlFsTempPath temp = {0};
    int fd = -1;
    SlStatus status;

    while (split > 0U && path.ptr[split - 1U] != '/') {
        split -= 1U;
    }
    directory = split == 0U ? sl_str_from_cstr(".") : sl_str_from_parts(path.ptr, split);
    status = sl_fs_platform_create_temp_file(arena, directory, sl_str_from_cstr(".sloppy-atomic-"),
                                             &temp, out_diag);
    if (!sl_status_is_ok(status)) {
        (void)sl_arena_reset_to(arena, mark);
        return status;
    }
    status = sl_fs_posix_path(arena, sl_owned_str_as_view(temp.path), &native);
    if (sl_status_is_ok(status)) {
        status = sl_fs_posix_path(arena, path, &target_native);
    }
    if (!sl_status_is_ok(status)) {
        if (native.ptr != NULL) {
            (void)unlink(native.ptr);
        }
        (void)sl_arena_reset_to(arena, mark);
        return status;
    }
    fd = open(native.ptr, O_WRONLY);
    if (fd < 0) {
        (void)unlink(native.ptr);
        (void)sl_arena_reset_to(arena, mark);
        return sl_fs_posix_status(errno);
    }
    status = sl_fs_posix_write_all(fd, bytes.ptr, bytes.length);
    if (sl_status_is_ok(status) && fsync(fd) != 0) {
        status = sl_fs_posix_status(errno);
    }
    close(fd);
    if (sl_status_is_ok(status) && rename(native.ptr, target_native.ptr) != 0) {
        status = sl_fs_posix_status(errno);
    }
    if (!sl_status_is_ok(status)) {
        (void)unlink(native.ptr);
    }
    (void)sl_arena_reset_to(arena, mark);
    return status;
}

SlStatus sl_fs_platform_acquire_lock(SlArena* arena, SlStr path, SlFsFileLock** out_lock,
                                     SlDiag* out_diag)
{
    SlOwnedStr native = {0};
    SlStatus status;
    void* memory = NULL;
    SlFsFileLock* lock = NULL;

    (void)out_diag;
    if (arena == NULL || out_lock == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out_lock = NULL;
    status = sl_arena_alloc(arena, sizeof(SlFsFileLock), _Alignof(SlFsFileLock), &memory);
    if (sl_status_is_ok(status)) {
        lock = (SlFsFileLock*)memory;
        *lock = (SlFsFileLock){.fd = -1, .path = {0}, .closed = true};
        status = sl_fs_posix_path(arena, path, &native);
    }
    if (!sl_status_is_ok(status)) {
        return status;
    }
    lock->fd = open(native.ptr, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (lock->fd < 0) {
        return sl_fs_posix_status(errno);
    }
    lock->path = native;
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
    close(lock->fd);
    if (lock->path.ptr != NULL) {
        (void)unlink(lock->path.ptr);
    }
    lock->closed = true;
    lock->fd = -1;
    return sl_status_ok();
}

SlStatus sl_fs_platform_open_file(SlArena* arena, SlStr path, SlFsFileAccess access, bool create,
                                  SlFsFileHandle** out_handle, SlDiag* out_diag)
{
    unsigned char scratch[4096];
    SlArena local = {0};
    SlOwnedStr native = {0};
    SlFsFileHandle* handle = NULL;
    int flags = O_RDONLY;
    SlStatus status;

    (void)out_diag;
    status = sl_arena_init(&local, scratch, sizeof(scratch));
    if (!sl_status_is_ok(status) ||
        !sl_status_is_ok((status = sl_fs_posix_path(&local, path, &native))) ||
        !sl_status_is_ok((status = sl_fs_posix_alloc_handle(arena, &handle))))
    {
        return status;
    }
    if (access == SL_FS_FILE_ACCESS_WRITE) {
        flags = O_WRONLY;
    }
    else if (access == SL_FS_FILE_ACCESS_READWRITE) {
        flags = O_RDWR;
    }
    else if (access == SL_FS_FILE_ACCESS_APPEND) {
        flags = O_WRONLY | O_APPEND;
    }
    if (create) {
        flags |= O_CREAT;
    }
    handle->fd = open(native.ptr, flags, 0600);
    if (handle->fd < 0) {
        return sl_fs_posix_status(errno);
    }
    handle->closed = false;
    *out_handle = handle;
    return sl_status_ok();
}

SlStatus sl_fs_platform_file_read(SlFsFileHandle* handle, SlArena* arena, size_t max_bytes,
                                  SlOwnedBytes* out, SlDiag* out_diag)
{
    void* memory = NULL;
    ssize_t n = 0;
    SlStatus status;

    (void)out_diag;
    if (handle == NULL || handle->closed || arena == NULL || out == NULL || max_bytes == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    status = sl_arena_alloc(arena, max_bytes, _Alignof(unsigned char), &memory);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    do {
        n = read(handle->fd, memory, max_bytes);
    } while (n < 0 && errno == EINTR);
    if (n < 0) {
        return sl_fs_posix_status(errno);
    }
    out->ptr = (unsigned char*)memory;
    out->length = (size_t)n;
    return sl_status_ok();
}

SlStatus sl_fs_platform_file_write(SlFsFileHandle* handle, SlBytes bytes, SlDiag* out_diag)
{
    (void)out_diag;
    if (handle == NULL || handle->closed) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    return sl_fs_posix_write_all(handle->fd, bytes.ptr, bytes.length);
}

SlStatus sl_fs_platform_file_seek(SlFsFileHandle* handle, int64_t offset, SlFsSeekOrigin origin,
                                  uint64_t* out_position, SlDiag* out_diag)
{
    int whence = origin == SL_FS_SEEK_CURRENT ? SEEK_CUR
                 : origin == SL_FS_SEEK_END   ? SEEK_END
                                              : SEEK_SET;
    off_t result = 0;

    (void)out_diag;
    if (handle == NULL || handle->closed || out_position == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    result = lseek(handle->fd, (off_t)offset, whence);
    if (result < 0) {
        return sl_fs_posix_status(errno);
    }
    *out_position = (uint64_t)result;
    return sl_status_ok();
}

SlStatus sl_fs_platform_file_truncate(SlFsFileHandle* handle, uint64_t size, SlDiag* out_diag)
{
    (void)out_diag;
    if (handle == NULL || handle->closed) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    return ftruncate(handle->fd, (off_t)size) == 0 ? sl_status_ok() : sl_fs_posix_status(errno);
}

SlStatus sl_fs_platform_file_flush(SlFsFileHandle* handle, SlDiag* out_diag)
{
    (void)out_diag;
    if (handle == NULL || handle->closed) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    return sl_status_ok();
}

SlStatus sl_fs_platform_file_sync(SlFsFileHandle* handle, SlDiag* out_diag)
{
    (void)out_diag;
    if (handle == NULL || handle->closed) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    return fsync(handle->fd) == 0 ? sl_status_ok() : sl_fs_posix_status(errno);
}

SlStatus sl_fs_platform_file_close(SlFsFileHandle* handle, SlDiag* out_diag)
{
    int fd = -1;

    (void)out_diag;
    if (handle == NULL || handle->closed) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    fd = handle->fd;
    handle->fd = -1;
    handle->closed = true;
    return close(fd) == 0 ? sl_status_ok() : sl_fs_posix_status(errno);
}

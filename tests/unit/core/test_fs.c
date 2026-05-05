#include "sloppy/fs.h"

#include <stdbool.h>
#include <string.h>

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static int test_path_classification_and_policy(void)
{
    unsigned char storage[8192];
    SlArena arena = {0};
    SlFsPathKind kind = SL_FS_PATH_INVALID;
    SlFsResolvedPath resolved = {0};
    SlDiag diag = {0};
    SlFsRoot roots[] = {{sl_str_from_cstr("data"), sl_str_from_cstr("./data")}};
    SlFsPolicy dev = sl_fs_development_policy(sl_str_from_cstr("."));
    SlFsPolicy strict = sl_fs_strict_policy(sl_str_from_cstr("."), roots, 1U, false);

    dev.roots = roots;
    dev.root_count = 1U;
    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_fs_classify_path(sl_str_from_cstr("./data/users.json"), &kind),
                      SL_STATUS_OK) != 0 ||
        kind != SL_FS_PATH_PROJECT_RELATIVE ||
        expect_status(sl_fs_classify_path(sl_str_from_cstr("data:/users.json"), &kind),
                      SL_STATUS_OK) != 0 ||
        kind != SL_FS_PATH_NAMED_ROOT ||
        expect_status(sl_fs_classify_path(sl_str_from_cstr("/etc/app/config"), &kind),
                      SL_STATUS_OK) != 0 ||
        kind != SL_FS_PATH_ABSOLUTE)
    {
        return 1;
    }

    if (expect_status(sl_fs_resolve_path(&arena, &dev, sl_str_from_cstr("data:/users.json"),
                                         &resolved, &diag),
                      SL_STATUS_OK) != 0 ||
        resolved.kind != SL_FS_PATH_NAMED_ROOT ||
        !sl_str_equal(sl_owned_str_as_view(resolved.path), sl_str_from_cstr("./data/users.json")))
    {
        return 2;
    }

    if (expect_status(
            sl_fs_resolve_path(&arena, &dev, sl_str_from_cstr("data:/../secret"), &resolved, &diag),
            SL_STATUS_INVALID_STATE) != 0 ||
        expect_status(sl_fs_resolve_path(&arena, &strict, sl_str_from_cstr("/etc/app/config"),
                                         &resolved, &diag),
                      SL_STATUS_INVALID_STATE) != 0)
    {
        return 3;
    }

    if (expect_status(
            sl_fs_resolve_path(&arena, &dev, sl_str_from_cstr("/etc/app/config"), &resolved, &diag),
            SL_STATUS_OK) != 0 ||
        !resolved.development_absolute_warning)
    {
        return 4;
    }

    return 0;
}

static int test_core_file_operations(void)
{
    unsigned char storage[65536];
    SlArena arena = {0};
    SlOwnedBytes bytes = {0};
    SlFsStat stat = {0};
    bool exists = true;
    SlStr path = sl_str_from_cstr("./sloppy-fs-core-test.txt");
    SlStr copy_path = sl_str_from_cstr("./sloppy-fs-core-test-copy.txt");
    SlStr moved_path = sl_str_from_cstr("./sloppy-fs-core-test-moved.txt");

    (void)sl_fs_delete_file(path, NULL);
    (void)sl_fs_delete_file(copy_path, NULL);
    (void)sl_fs_delete_file(moved_path, NULL);

    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_fs_write_file(path, sl_bytes_from_parts((const unsigned char*)"hello", 5U),
                                       false, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_fs_write_file(path, sl_bytes_from_parts((const unsigned char*)"-fs", 3U),
                                       true, NULL),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_fs_read_file(&arena, path, &bytes, NULL), SL_STATUS_OK) != 0 ||
        bytes.length != 8U || memcmp(bytes.ptr, "hello-fs", 8U) != 0)
    {
        return 10;
    }

    if (expect_status(sl_fs_stat(path, &stat, NULL), SL_STATUS_OK) != 0 || !stat.exists ||
        stat.kind != SL_FS_NODE_FILE || stat.size != 8U ||
        expect_status(sl_fs_exists(path, &exists, NULL), SL_STATUS_OK) != 0 || !exists)
    {
        return 11;
    }

    if (expect_status(sl_fs_copy_file(path, copy_path, false, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_fs_move_file(copy_path, moved_path, false, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_fs_exists(moved_path, &exists, NULL), SL_STATUS_OK) != 0 || !exists ||
        expect_status(sl_fs_delete_file(path, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_fs_delete_file(moved_path, NULL), SL_STATUS_OK) != 0)
    {
        return 12;
    }

    return 0;
}

static int test_directory_temp_atomic_lock_and_handle(void)
{
    unsigned char storage[131072];
    SlArena arena = {0};
    SlStr dir = sl_str_from_cstr("./sloppy-fs-advanced");
    SlStr nested = sl_str_from_cstr("./sloppy-fs-advanced/nested");
    SlStr file = sl_str_from_cstr("./sloppy-fs-advanced/nested/file.txt");
    SlStr atomic = sl_str_from_cstr("./sloppy-fs-advanced/atomic.txt");
    SlStr lock_path = sl_str_from_cstr("./sloppy-fs-advanced/test.lock");
    SlFsDirectoryList list = {0};
    SlFsTempPath temp_file = {0};
    SlFsTempPath temp_dir = {0};
    SlFsFileHandle* handle = NULL;
    SlFsFileLock* first_lock = NULL;
    SlFsFileLock* second_lock = NULL;
    SlOwnedBytes bytes = {0};
    uint64_t position = 0U;
    bool saw_file = false;
    size_t index = 0U;

    (void)sl_fs_delete_directory(dir, true, NULL);
    if (expect_status(sl_arena_init(&arena, storage, sizeof(storage)), SL_STATUS_OK) != 0 ||
        expect_status(sl_fs_create_directory(nested, true, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_fs_write_file(file, sl_bytes_from_parts((const unsigned char*)"one", 3U),
                                       false, NULL),
                      SL_STATUS_OK) != 0)
    {
        return 20;
    }

    if (expect_status(sl_fs_list_directory(&arena, nested, &list, NULL), SL_STATUS_OK) != 0 ||
        list.count == 0U)
    {
        return 21;
    }
    for (index = 0U; index < list.count; index += 1U) {
        if (sl_str_equal(sl_owned_str_as_view(list.entries[index].name),
                         sl_str_from_cstr("file.txt")) &&
            list.entries[index].kind == SL_FS_NODE_FILE)
        {
            saw_file = true;
        }
    }
    if (!saw_file) {
        return 22;
    }

    if (expect_status(
            sl_fs_atomic_write_file(&arena, atomic,
                                    sl_bytes_from_parts((const unsigned char*)"atomic", 6U), NULL),
            SL_STATUS_OK) != 0 ||
        expect_status(sl_fs_read_file(&arena, atomic, &bytes, NULL), SL_STATUS_OK) != 0 ||
        bytes.length != 6U || memcmp(bytes.ptr, "atomic", 6U) != 0)
    {
        return 23;
    }

    if (expect_status(
            sl_fs_create_temp_file(&arena, dir, sl_str_from_cstr("tmp-"), &temp_file, NULL),
            SL_STATUS_OK) != 0 ||
        expect_status(
            sl_fs_create_temp_directory(&arena, dir, sl_str_from_cstr("tmpd-"), &temp_dir, NULL),
            SL_STATUS_OK) != 0 ||
        temp_file.path.length == 0U || temp_dir.path.length == 0U)
    {
        return 24;
    }

    if (expect_status(sl_fs_acquire_lock(&arena, lock_path, &first_lock, NULL), SL_STATUS_OK) !=
            0 ||
        expect_status(sl_fs_acquire_lock(&arena, lock_path, &second_lock, NULL),
                      SL_STATUS_INVALID_STATE) != 0 ||
        expect_status(sl_fs_release_lock(first_lock, NULL), SL_STATUS_OK) != 0)
    {
        return 25;
    }

    if (expect_status(
            sl_fs_open_file(&arena, file, SL_FS_FILE_ACCESS_READWRITE, true, &handle, NULL),
            SL_STATUS_OK) != 0 ||
        expect_status(sl_fs_file_truncate(handle, 0U, NULL), SL_STATUS_OK) != 0 ||
        expect_status(
            sl_fs_file_write(handle, sl_bytes_from_parts((const unsigned char*)"abc\0z", 5U), NULL),
            SL_STATUS_OK) != 0 ||
        expect_status(sl_fs_file_seek(handle, 0, SL_FS_SEEK_START, &position, NULL),
                      SL_STATUS_OK) != 0 ||
        position != 0U ||
        expect_status(sl_fs_file_read(handle, &arena, 5U, &bytes, NULL), SL_STATUS_OK) != 0 ||
        bytes.length != 5U || memcmp(bytes.ptr, "abc\0z", 5U) != 0 ||
        expect_status(sl_fs_file_close(handle, NULL), SL_STATUS_OK) != 0 ||
        expect_status(sl_fs_file_close(handle, NULL), SL_STATUS_INVALID_STATE) != 0)
    {
        return 26;
    }

    if (expect_status(sl_fs_delete_directory(dir, true, NULL), SL_STATUS_OK) != 0) {
        return 27;
    }
    return 0;
}

int main(void)
{
    if (test_path_classification_and_policy() != 0) {
        return 1;
    }
    if (test_core_file_operations() != 0) {
        return 2;
    }
    if (test_directory_temp_atomic_lock_and_handle() != 0) {
        return 3;
    }
    return 0;
}

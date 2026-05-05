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

int main(void)
{
    if (test_path_classification_and_policy() != 0) {
        return 1;
    }
    if (test_core_file_operations() != 0) {
        return 2;
    }
    return 0;
}

#ifndef SLOPPY_FS_H
#define SLOPPY_FS_H

#include "sloppy/arena.h"
#include "sloppy/bytes.h"
#include "sloppy/diagnostics.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SlFsPathKind
{
    SL_FS_PATH_INVALID = 0,
    SL_FS_PATH_PROJECT_RELATIVE = 1,
    SL_FS_PATH_NAMED_ROOT = 2,
    SL_FS_PATH_ABSOLUTE = 3
} SlFsPathKind;

typedef enum SlFsPolicyMode
{
    SL_FS_POLICY_DEVELOPMENT = 0,
    SL_FS_POLICY_STRICT = 1
} SlFsPolicyMode;

typedef enum SlFsNodeKind
{
    SL_FS_NODE_MISSING = 0,
    SL_FS_NODE_FILE = 1,
    SL_FS_NODE_DIRECTORY = 2,
    SL_FS_NODE_OTHER = 3
} SlFsNodeKind;

typedef struct SlFsRoot
{
    SlStr name;
    SlStr path;
} SlFsRoot;

typedef struct SlFsPolicy
{
    SlStr app_root;
    const SlFsRoot* roots;
    size_t root_count;
    SlFsPolicyMode mode;
    bool allow_absolute;
} SlFsPolicy;

typedef struct SlFsResolvedPath
{
    SlFsPathKind kind;
    SlOwnedStr path;
    SlOwnedStr root_name;
    bool development_absolute_warning;
} SlFsResolvedPath;

typedef struct SlFsStat
{
    SlFsNodeKind kind;
    uint64_t size;
    bool exists;
} SlFsStat;

SlFsPolicy sl_fs_development_policy(SlStr app_root);
SlFsPolicy sl_fs_strict_policy(SlStr app_root, const SlFsRoot* roots, size_t root_count,
                               bool allow_absolute);

SlStr sl_fs_path_kind_name(SlFsPathKind kind);
SlStatus sl_fs_classify_path(SlStr input, SlFsPathKind* out_kind);
SlStatus sl_fs_resolve_path(SlArena* arena, const SlFsPolicy* policy, SlStr input,
                            SlFsResolvedPath* out, SlDiag* out_diag);

SlStatus sl_fs_read_file(SlArena* arena, SlStr path, SlOwnedBytes* out, SlDiag* out_diag);
SlStatus sl_fs_write_file(SlStr path, SlBytes bytes, bool append, SlDiag* out_diag);
SlStatus sl_fs_copy_file(SlStr from_path, SlStr to_path, bool overwrite, SlDiag* out_diag);
SlStatus sl_fs_move_file(SlStr from_path, SlStr to_path, bool overwrite, SlDiag* out_diag);
SlStatus sl_fs_delete_file(SlStr path, SlDiag* out_diag);
SlStatus sl_fs_stat(SlStr path, SlFsStat* out, SlDiag* out_diag);
SlStatus sl_fs_exists(SlStr path, bool* out_exists, SlDiag* out_diag);

#ifdef __cplusplus
}
#endif

#endif

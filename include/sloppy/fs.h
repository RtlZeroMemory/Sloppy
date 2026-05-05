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

typedef enum SlFsFileAccess
{
    SL_FS_FILE_ACCESS_READ = 0,
    SL_FS_FILE_ACCESS_WRITE = 1,
    SL_FS_FILE_ACCESS_READWRITE = 2,
    SL_FS_FILE_ACCESS_APPEND = 3
} SlFsFileAccess;

typedef enum SlFsSeekOrigin
{
    SL_FS_SEEK_START = 0,
    SL_FS_SEEK_CURRENT = 1,
    SL_FS_SEEK_END = 2
} SlFsSeekOrigin;

typedef enum SlFsWatchEventKind
{
    SL_FS_WATCH_EVENT_CREATED = 0,
    SL_FS_WATCH_EVENT_MODIFIED = 1,
    SL_FS_WATCH_EVENT_DELETED = 2,
    SL_FS_WATCH_EVENT_OVERFLOW = 3
} SlFsWatchEventKind;

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
    uint64_t modified_nsec;
    bool exists;
} SlFsStat;

typedef struct SlFsDirectoryEntry
{
    SlOwnedStr name;
    SlFsNodeKind kind;
    uint64_t size;
    uint64_t modified_nsec;
} SlFsDirectoryEntry;

typedef struct SlFsDirectoryList
{
    SlFsDirectoryEntry* entries;
    size_t count;
} SlFsDirectoryList;

typedef struct SlFsTempPath
{
    SlOwnedStr path;
} SlFsTempPath;

typedef struct SlFsWatchOptions
{
    bool directory;
    bool recursive;
    size_t queue_capacity;
    size_t snapshot_capacity;
} SlFsWatchOptions;

typedef struct SlFsWatchEvent
{
    SlFsWatchEventKind kind;
    SlOwnedStr path;
    bool is_directory;
    bool overflow;
} SlFsWatchEvent;

typedef struct SlFsFileHandle SlFsFileHandle;
typedef struct SlFsFileLock SlFsFileLock;
typedef struct SlFsWatchHandle SlFsWatchHandle;

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
SlStatus sl_fs_create_directory(SlStr path, bool recursive, SlDiag* out_diag);
SlStatus sl_fs_delete_directory(SlStr path, bool recursive, SlDiag* out_diag);
SlStatus sl_fs_list_directory(SlArena* arena, SlStr path, SlFsDirectoryList* out, SlDiag* out_diag);
SlStatus sl_fs_create_symlink(SlStr target_path, SlStr link_path, bool directory, SlDiag* out_diag);
SlStatus sl_fs_read_link(SlArena* arena, SlStr path, SlOwnedStr* out, SlDiag* out_diag);
SlStatus sl_fs_create_temp_file(SlArena* arena, SlStr directory, SlStr prefix, SlFsTempPath* out,
                                SlDiag* out_diag);
SlStatus sl_fs_create_temp_directory(SlArena* arena, SlStr directory, SlStr prefix,
                                     SlFsTempPath* out, SlDiag* out_diag);
SlStatus sl_fs_atomic_write_file(SlArena* arena, SlStr path, SlBytes bytes, SlDiag* out_diag);
SlStatus sl_fs_acquire_lock(SlArena* arena, SlStr path, SlFsFileLock** out_lock, SlDiag* out_diag);
SlStatus sl_fs_release_lock(SlFsFileLock* lock, SlDiag* out_diag);
SlStatus sl_fs_open_file(SlArena* arena, SlStr path, SlFsFileAccess access, bool create,
                         SlFsFileHandle** out_handle, SlDiag* out_diag);
SlStatus sl_fs_file_read(SlFsFileHandle* handle, SlArena* arena, size_t max_bytes,
                         SlOwnedBytes* out, SlDiag* out_diag);
SlStatus sl_fs_file_write(SlFsFileHandle* handle, SlBytes bytes, SlDiag* out_diag);
SlStatus sl_fs_file_seek(SlFsFileHandle* handle, int64_t offset, SlFsSeekOrigin origin,
                         uint64_t* out_position, SlDiag* out_diag);
SlStatus sl_fs_file_truncate(SlFsFileHandle* handle, uint64_t size, SlDiag* out_diag);
SlStatus sl_fs_file_flush(SlFsFileHandle* handle, SlDiag* out_diag);
SlStatus sl_fs_file_sync(SlFsFileHandle* handle, SlDiag* out_diag);
SlStatus sl_fs_file_close(SlFsFileHandle* handle, SlDiag* out_diag);
SlStatus sl_fs_watch_open(SlArena* arena, SlStr path, const SlFsWatchOptions* options,
                          SlFsWatchHandle** out_watch, SlDiag* out_diag);
SlStatus sl_fs_watch_next(SlFsWatchHandle* watch, SlArena* arena, SlFsWatchEvent* out_event,
                          SlDiag* out_diag);
SlStatus sl_fs_watch_close(SlFsWatchHandle* watch, SlDiag* out_diag);

#ifdef __cplusplus
}
#endif

#endif

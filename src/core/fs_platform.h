#ifndef SLOPPY_CORE_FS_PLATFORM_H
#define SLOPPY_CORE_FS_PLATFORM_H

#include "sloppy/fs.h"

SlStatus sl_fs_platform_read_file(SlArena* arena, SlStr path, SlOwnedBytes* out, SlDiag* out_diag);
SlStatus sl_fs_platform_write_file(SlStr path, SlBytes bytes, bool append, SlDiag* out_diag);
SlStatus sl_fs_platform_copy_file(SlStr from_path, SlStr to_path, bool overwrite, SlDiag* out_diag);
SlStatus sl_fs_platform_move_file(SlStr from_path, SlStr to_path, bool overwrite, SlDiag* out_diag);
SlStatus sl_fs_platform_delete_file(SlStr path, SlDiag* out_diag);
SlStatus sl_fs_platform_stat(SlStr path, SlFsStat* out, SlDiag* out_diag);
SlStatus sl_fs_platform_create_directory(SlStr path, bool recursive, SlDiag* out_diag);
SlStatus sl_fs_platform_delete_directory(SlStr path, bool recursive, SlDiag* out_diag);
SlStatus sl_fs_platform_list_directory(SlArena* arena, SlStr path, SlFsDirectoryList* out,
                                       SlDiag* out_diag);
SlStatus sl_fs_platform_create_symlink(SlStr target_path, SlStr link_path, bool directory,
                                       SlDiag* out_diag);
SlStatus sl_fs_platform_read_link(SlArena* arena, SlStr path, SlOwnedStr* out, SlDiag* out_diag);
SlStatus sl_fs_platform_create_temp_file(SlArena* arena, SlStr directory, SlStr prefix,
                                         SlFsTempPath* out, SlDiag* out_diag);
SlStatus sl_fs_platform_create_temp_directory(SlArena* arena, SlStr directory, SlStr prefix,
                                              SlFsTempPath* out, SlDiag* out_diag);
SlStatus sl_fs_platform_atomic_write_file(SlArena* arena, SlStr path, SlBytes bytes,
                                          SlDiag* out_diag);
SlStatus sl_fs_platform_acquire_lock(SlArena* arena, SlStr path, SlFsFileLock** out_lock,
                                     SlDiag* out_diag);
SlStatus sl_fs_platform_release_lock(SlFsFileLock* lock, SlDiag* out_diag);
SlStatus sl_fs_platform_open_file(SlArena* arena, SlStr path, SlFsFileAccess access, bool create,
                                  SlFsFileHandle** out_handle, SlDiag* out_diag);
SlStatus sl_fs_platform_file_read(SlFsFileHandle* handle, SlArena* arena, size_t max_bytes,
                                  SlOwnedBytes* out, SlDiag* out_diag);
SlStatus sl_fs_platform_file_write(SlFsFileHandle* handle, SlBytes bytes, SlDiag* out_diag);
SlStatus sl_fs_platform_file_seek(SlFsFileHandle* handle, int64_t offset, SlFsSeekOrigin origin,
                                  uint64_t* out_position, SlDiag* out_diag);
SlStatus sl_fs_platform_file_truncate(SlFsFileHandle* handle, uint64_t size, SlDiag* out_diag);
SlStatus sl_fs_platform_file_flush(SlFsFileHandle* handle, SlDiag* out_diag);
SlStatus sl_fs_platform_file_sync(SlFsFileHandle* handle, SlDiag* out_diag);
SlStatus sl_fs_platform_file_close(SlFsFileHandle* handle, SlDiag* out_diag);

#endif

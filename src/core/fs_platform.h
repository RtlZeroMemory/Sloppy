#ifndef SLOPPY_CORE_FS_PLATFORM_H
#define SLOPPY_CORE_FS_PLATFORM_H

#include "sloppy/fs.h"

SlStatus sl_fs_platform_read_file(SlArena* arena, SlStr path, SlOwnedBytes* out, SlDiag* out_diag);
SlStatus sl_fs_platform_write_file(SlStr path, SlBytes bytes, bool append, SlDiag* out_diag);
SlStatus sl_fs_platform_copy_file(SlStr from_path, SlStr to_path, bool overwrite, SlDiag* out_diag);
SlStatus sl_fs_platform_move_file(SlStr from_path, SlStr to_path, bool overwrite, SlDiag* out_diag);
SlStatus sl_fs_platform_delete_file(SlStr path, SlDiag* out_diag);
SlStatus sl_fs_platform_stat(SlStr path, SlFsStat* out, SlDiag* out_diag);

#endif

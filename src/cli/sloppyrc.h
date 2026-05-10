#ifndef SLOPPY_CLI_SLOPPYRC_H
#define SLOPPY_CLI_SLOPPYRC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SL_SLOPPYRC_PATH_MAX_BYTES 1024U
#define SL_SLOPPYRC_ENVIRONMENT_MAX_BYTES 128U
#define SL_SLOPPYRC_KIND_MAX_BYTES 16U
#define SL_SLOPPYRC_MAX_CAPABILITIES 9U
#define SL_SLOPPYRC_CAPABILITY_MAX_BYTES 16U
#define SL_SLOPPYRC_MAX_FFI_LIBRARIES 32U
#define SL_SLOPPYRC_FFI_LIBRARY_NAME_MAX_BYTES 256U
#define SL_SLOPPYRC_FFI_LIBRARY_PATH_MAX_BYTES SL_SLOPPYRC_PATH_MAX_BYTES

typedef struct SlSloppyFfiLibraryConfig
{
    char name[SL_SLOPPYRC_FFI_LIBRARY_NAME_MAX_BYTES];
    char path[SL_SLOPPYRC_FFI_LIBRARY_PATH_MAX_BYTES];
} SlSloppyFfiLibraryConfig;

typedef struct SlSloppyRunConfig
{
    char entry[SL_SLOPPYRC_PATH_MAX_BYTES];
    char out_dir[SL_SLOPPYRC_PATH_MAX_BYTES];
    char environment[SL_SLOPPYRC_ENVIRONMENT_MAX_BYTES];
    char kind[SL_SLOPPYRC_KIND_MAX_BYTES];
    char capabilities[SL_SLOPPYRC_MAX_CAPABILITIES][SL_SLOPPYRC_CAPABILITY_MAX_BYTES];
    size_t capability_count;
    SlSloppyFfiLibraryConfig ffi_libraries[SL_SLOPPYRC_MAX_FFI_LIBRARIES];
    size_t ffi_library_count;
} SlSloppyRunConfig;

int sl_sloppyrc_load(SlSloppyRunConfig* out);
int sl_sloppyrc_load_for_command(SlSloppyRunConfig* out, const char* command_name);

#ifdef __cplusplus
}
#endif

#endif

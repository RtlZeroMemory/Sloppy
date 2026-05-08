#ifndef SLOPPY_CLI_SLOPPYRC_H
#define SLOPPY_CLI_SLOPPYRC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SL_SLOPPYRC_PATH_MAX_BYTES 1024U
#define SL_SLOPPYRC_ENVIRONMENT_MAX_BYTES 128U

typedef struct SlSloppyRunConfig
{
    char entry[SL_SLOPPYRC_PATH_MAX_BYTES];
    char out_dir[SL_SLOPPYRC_PATH_MAX_BYTES];
    char environment[SL_SLOPPYRC_ENVIRONMENT_MAX_BYTES];
} SlSloppyRunConfig;

int sl_sloppyrc_load(SlSloppyRunConfig* out);
int sl_sloppyrc_load_for_command(SlSloppyRunConfig* out, const char* command_name);

#ifdef __cplusplus
}
#endif

#endif

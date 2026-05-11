#ifndef SLOPPY_CLI_DEV_WATCH_PLAN_H
#define SLOPPY_CLI_DEV_WATCH_PLAN_H

#include "sloppyrc.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SL_DEV_WATCH_MAX_ROOTS 96U

typedef struct SlDevWatchRoot
{
    char path[SL_SLOPPYRC_PATH_MAX_BYTES];
    bool recursive;
} SlDevWatchRoot;

typedef struct SlDevWatchPlan
{
    SlDevWatchRoot roots[SL_DEV_WATCH_MAX_ROOTS];
    size_t root_count;
} SlDevWatchPlan;

bool sl_dev_watch_plan_build(const SlSloppyRunConfig* config, const char* input_path,
                             SlDevWatchPlan* plan);

#ifdef __cplusplus
}
#endif

#endif

#ifndef SLOPPY_PLATFORM_PROCESS_H
#define SLOPPY_PLATFORM_PROCESS_H

#include "sloppy/status.h"

#include <stddef.h>

typedef struct SlPlatformProcessArgs
{
    const char* file;
    char** argv;
} SlPlatformProcessArgs;

SlStatus sl_platform_process_executable_path(char* buffer, size_t capacity);
SlStatus sl_platform_process_run(const SlPlatformProcessArgs* args, int* out_exit_code);

#endif

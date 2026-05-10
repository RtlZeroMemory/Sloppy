#ifndef SLOPPY_PLATFORM_PROCESS_H
#define SLOPPY_PLATFORM_PROCESS_H

#include "sloppy/status.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SlPlatformProcessArgs
{
    const char* file;
    char** argv;
} SlPlatformProcessArgs;

SlStatus sl_platform_process_executable_path(char* buffer, size_t capacity);
SlStatus sl_platform_process_current_directory(char* buffer, size_t capacity);
SlStatus sl_platform_process_run(const SlPlatformProcessArgs* args, int* out_exit_code);

#ifdef __cplusplus
}
#endif

#endif

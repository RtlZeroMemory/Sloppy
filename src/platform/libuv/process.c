/*
 * src/platform/libuv/process.c
 *
 * Small Slop-owned process runner. Libuv process handles stay in platform code so callers
 * pass argv directly and never need shell command strings for tool handoff.
 */
#include "sloppy/platform_process.h"

#include <stdbool.h>
#include <stdint.h>
#include <uv.h>

typedef struct SlPlatformProcessRun
{
    uv_process_t process;
    bool exited;
    int exit_code;
} SlPlatformProcessRun;

static SlStatus sl_platform_process_status_from_uv(int status)
{
    if (status == 0) {
        return sl_status_ok();
    }
    if (status == UV_ENOMEM) {
        return sl_status_from_code(SL_STATUS_OUT_OF_MEMORY);
    }
    if (status == UV_EINVAL || status == UV_ENOENT) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    return sl_status_from_code(SL_STATUS_INTERNAL);
}

static void sl_platform_process_on_exit(uv_process_t* process, int64_t exit_status, int term_signal)
{
    SlPlatformProcessRun* run = NULL;

    if (process == NULL) {
        return;
    }

    run = (SlPlatformProcessRun*)process->data;
    if (run != NULL) {
        run->exited = true;
        run->exit_code = term_signal == 0 ? (int)exit_status : 1;
    }
    uv_close((uv_handle_t*)process, NULL);
}

SlStatus sl_platform_process_run(const SlPlatformProcessArgs* args, int* out_exit_code)
{
    uv_loop_t loop;
    SlPlatformProcessRun run = {0};
    uv_process_options_t options = {0};
    uv_stdio_container_t stdio[3];
    int uv_status = 0;
    int close_status = 0;

    if (args == NULL || out_exit_code == NULL || args->file == NULL || args->file[0] == '\0' ||
        args->argv == NULL)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out_exit_code = 1;
    uv_status = uv_loop_init(&loop);
    if (uv_status != 0) {
        return sl_platform_process_status_from_uv(uv_status);
    }

    stdio[0] = (uv_stdio_container_t){.flags = UV_INHERIT_FD, .data.fd = 0};
    stdio[1] = (uv_stdio_container_t){.flags = UV_INHERIT_FD, .data.fd = 1};
    stdio[2] = (uv_stdio_container_t){.flags = UV_INHERIT_FD, .data.fd = 2};

    run.process.data = &run;
    options.file = args->file;
    options.args = args->argv;
    options.exit_cb = sl_platform_process_on_exit;
    options.stdio_count = 3;
    options.stdio = stdio;

    uv_status = uv_spawn(&loop, &run.process, &options);
    if (uv_status == 0) {
        uv_status = uv_run(&loop, UV_RUN_DEFAULT);
    }

    close_status = uv_loop_close(&loop);
    if (uv_status != 0) {
        return sl_platform_process_status_from_uv(uv_status);
    }
    if (close_status != 0 || !run.exited) {
        return sl_status_from_code(SL_STATUS_INTERNAL);
    }

    *out_exit_code = run.exit_code;
    return sl_status_ok();
}

#ifndef SLOPPY_CORE_OS_PLATFORM_H
#define SLOPPY_CORE_OS_PLATFORM_H

#include "sloppy/os.h"

SlStatus sl_os_platform_system_info(SlArena* arena, SlOsSystemInfo* out, SlDiag* out_diag);
SlStatus sl_os_platform_environment_get(SlArena* arena, SlStr key, SlOwnedStr* out_value,
                                        bool* out_found, SlDiag* out_diag);
SlStatus sl_os_platform_environment_has(SlStr key, bool* out_found, SlDiag* out_diag);
SlStatus sl_os_platform_environment_list(SlArena* arena, SlStr prefix, SlOsEnvironmentList* out,
                                         SlDiag* out_diag);
SlStatus sl_os_platform_process_info(SlArena* arena, SlOsProcessInfo* out, SlDiag* out_diag);
SlStatus sl_os_platform_process_run(SlArena* arena, SlStr command, const SlStr* args,
                                    size_t arg_count, const SlOsProcessRunOptions* options,
                                    SlOsProcessRunResult* out, SlDiag* out_diag);
SlStatus sl_os_platform_process_start(SlArena* arena, SlStr command, const SlStr* args,
                                      size_t arg_count, const SlOsProcessStartOptions* options,
                                      SlOsProcessHandle** out, SlDiag* out_diag);
SlStatus sl_os_platform_process_wait(SlOsProcessHandle* handle,
                                     const SlOsProcessWaitOptions* options, SlOsProcessExit* out,
                                     SlDiag* out_diag);
SlStatus sl_os_platform_process_stdout_read(SlArena* arena, SlOsProcessHandle* handle,
                                            size_t max_bytes, SlOsProcessPipeRead* out,
                                            SlDiag* out_diag);
SlStatus sl_os_platform_process_stderr_read(SlArena* arena, SlOsProcessHandle* handle,
                                            size_t max_bytes, SlOsProcessPipeRead* out,
                                            SlDiag* out_diag);
SlStatus sl_os_platform_process_stdin_write(SlOsProcessHandle* handle, SlStr bytes,
                                            size_t* out_written, SlDiag* out_diag);
SlStatus sl_os_platform_process_stdin_close(SlOsProcessHandle* handle, SlDiag* out_diag);
SlStatus sl_os_platform_process_terminate(SlOsProcessHandle* handle, SlDiag* out_diag);
SlStatus sl_os_platform_process_kill(SlOsProcessHandle* handle, SlDiag* out_diag);
SlStatus sl_os_platform_process_cancel(SlOsProcessHandle* handle, SlDiag* out_diag);
void sl_os_platform_process_dispose(SlOsProcessHandle* handle);

#endif

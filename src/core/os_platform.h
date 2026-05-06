#ifndef SLOPPY_CORE_OS_PLATFORM_H
#define SLOPPY_CORE_OS_PLATFORM_H

#include "sloppy/os.h"

SlStatus sl_os_platform_system_info(SlArena* arena, SlOsSystemInfo* out, SlDiag* out_diag);
SlStatus sl_os_platform_environment_get(SlArena* arena, SlStr key, SlOwnedStr* out_value,
                                        bool* out_found, SlDiag* out_diag);
SlStatus sl_os_platform_environment_has(SlStr key, bool* out_found, SlDiag* out_diag);
SlStatus sl_os_platform_environment_list(SlArena* arena, SlStr prefix, SlOsEnvironmentList* out,
                                         SlDiag* out_diag);

#endif

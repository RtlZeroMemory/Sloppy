#ifndef SLOPPY_PLATFORM_DYNLIB_H
#define SLOPPY_PLATFORM_DYNLIB_H

#include "sloppy/diagnostics.h"
#include "sloppy/status.h"
#include "sloppy/string.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SlPlatformDynlib
{
    void* handle;
} SlPlatformDynlib;

SlStatus sl_platform_dynlib_open(SlStr path, SlPlatformDynlib* out, SlDiag* out_diag);
SlStatus sl_platform_dynlib_symbol(const SlPlatformDynlib* library, SlStr symbol, void** out,
                                   SlDiag* out_diag);
void sl_platform_dynlib_close(SlPlatformDynlib* library);

#ifdef __cplusplus
}
#endif

#endif

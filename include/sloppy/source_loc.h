#ifndef SLOPPY_SOURCE_LOC_H
#define SLOPPY_SOURCE_LOC_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SlSourceLoc is borrowed call-site metadata for assertions, allocators, and future
 * diagnostics. The file and function pointers are compiler-provided static strings when
 * captured with SL_SOURCE_LOC_CURRENT; callers never free them.
 */
typedef struct SlSourceLoc
{
    const char* file;
    unsigned int line;
    const char* function;
} SlSourceLoc;

#define SL_SOURCE_LOC_CURRENT ((SlSourceLoc){__FILE__, (unsigned int)__LINE__, __func__})
#define SL_SOURCE_LOC SL_SOURCE_LOC_CURRENT

SlSourceLoc sl_source_loc_unknown(void);
bool sl_source_loc_is_unknown(SlSourceLoc loc);

#ifdef __cplusplus
}
#endif

#endif

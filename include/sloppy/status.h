#ifndef SLOPPY_STATUS_H
#define SLOPPY_STATUS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SlStatus is Sloppy's small machine-checkable result type.
 *
 * It carries only a stable status code. Human diagnostics, source frames, and renderer-owned
 * message text belong to the future diagnostics layer, not to low-level core helpers.
 */
typedef enum SlStatusCode
{
    SL_STATUS_OK = 0,
    SL_STATUS_INVALID_ARGUMENT = 1,
    SL_STATUS_OUT_OF_MEMORY = 2,
    SL_STATUS_OVERFLOW = 3,
    SL_STATUS_OUT_OF_RANGE = 4,
    SL_STATUS_ASSERTION_FAILED = 5,
    SL_STATUS_INTERNAL = 6,
    SL_STATUS_UNSUPPORTED = 7
} SlStatusCode;

typedef struct SlStatus
{
    SlStatusCode code;
} SlStatus;

SlStatus sl_status_ok(void);
SlStatus sl_status_from_code(SlStatusCode code);
SlStatusCode sl_status_code(SlStatus status);
bool sl_status_is_ok(SlStatus status);

#ifdef __cplusplus
}
#endif

#endif

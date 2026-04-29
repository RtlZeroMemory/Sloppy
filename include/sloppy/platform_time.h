#ifndef SLOPPY_PLATFORM_TIME_H
#define SLOPPY_PLATFORM_TIME_H

#include "sloppy/status.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Returns a monotonic timestamp in nanoseconds for elapsed-time measurement.
 *
 * The value has no wall-clock meaning and is intended only for duration deltas inside one
 * process. Platform-specific clock APIs stay under platform implementation directories.
 */
SlStatus sl_platform_monotonic_time_ns(uint64_t* out_ns);

#ifdef __cplusplus
}
#endif

#endif

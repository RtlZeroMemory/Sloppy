#include "sloppy/platform_time.h"

#include <time.h>

SlStatus sl_platform_monotonic_time_ns(uint64_t* out_ns)
{
    struct timespec value;

    if (out_ns == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0 || value.tv_sec < 0 || value.tv_nsec < 0) {
        *out_ns = 0U;
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    *out_ns = ((uint64_t)value.tv_sec * UINT64_C(1000000000)) + (uint64_t)value.tv_nsec;
    return sl_status_ok();
}

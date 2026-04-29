#include "sloppy/platform_time.h"

#include <windows.h>

SlStatus sl_platform_monotonic_time_ns(uint64_t* out_ns)
{
    LARGE_INTEGER counter;
    LARGE_INTEGER frequency;
    uint64_t seconds;
    uint64_t remainder;

    if (out_ns == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (QueryPerformanceFrequency(&frequency) == 0 || frequency.QuadPart <= 0 ||
        QueryPerformanceCounter(&counter) == 0 || counter.QuadPart < 0)
    {
        *out_ns = 0U;
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    seconds = (uint64_t)counter.QuadPart / (uint64_t)frequency.QuadPart;
    remainder = (uint64_t)counter.QuadPart % (uint64_t)frequency.QuadPart;
    *out_ns = (seconds * UINT64_C(1000000000)) +
              ((remainder * UINT64_C(1000000000)) / (uint64_t)frequency.QuadPart);
    return sl_status_ok();
}

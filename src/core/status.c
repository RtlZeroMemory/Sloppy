/*
 * src/core/status.c
 *
 * Implements Sloppy's minimal status value helpers. Status is intentionally only a stable
 * machine-readable code; richer diagnostics are owned by the future diagnostics module.
 *
 * Safety invariants:
 * - no allocation;
 * - no platform or engine dependencies;
 * - status values are passed by value and own no memory.
 *
 * Tests: tests/unit/core/test_status.c.
 */
#include "sloppy/status.h"

SlStatus sl_status_ok(void)
{
    SlStatus status = {SL_STATUS_OK};
    return status;
}

SlStatus sl_status_from_code(SlStatusCode code)
{
    SlStatus status = {code};
    return status;
}

SlStatusCode sl_status_code(SlStatus status)
{
    return status.code;
}

bool sl_status_is_ok(SlStatus status)
{
    return status.code == SL_STATUS_OK;
}

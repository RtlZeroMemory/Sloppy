#include "sloppy/platform_crash.h"

#include <unistd.h>

void sl_platform_disable_interactive_crash_ui(void) {}

void sl_platform_abort_process(void)
{
    _Exit(3);
}

uint64_t sl_platform_process_id(void)
{
    return (uint64_t)getpid();
}

#include "sloppy/platform_crash.h"

#include <stdlib.h>

void sl_platform_disable_interactive_crash_ui(void) {}

void sl_platform_abort_process(void)
{
    abort();
}

#include "sloppy/platform_crash.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

void sl_platform_disable_interactive_crash_ui(void)
{
    SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
}

void sl_platform_abort_process(void)
{
    sl_platform_disable_interactive_crash_ui();
    TerminateProcess(GetCurrentProcess(), 3U);
    ExitProcess(3U);
}

uint64_t sl_platform_process_id(void)
{
    return (uint64_t)GetCurrentProcessId();
}

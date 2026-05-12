#ifndef SLOPPY_PLATFORM_CRASH_H
#define SLOPPY_PLATFORM_CRASH_H

#ifdef __cplusplus
extern "C" {
#endif

void sl_platform_disable_interactive_crash_ui(void);
void sl_platform_abort_process(void);

#ifdef __cplusplus
}
#endif

#endif

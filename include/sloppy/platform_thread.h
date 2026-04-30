#ifndef SLOPPY_PLATFORM_THREAD_H
#define SLOPPY_PLATFORM_THREAD_H

#include "sloppy/arena.h"
#include "sloppy/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SlPlatformMutex SlPlatformMutex;
typedef struct SlPlatformCond SlPlatformCond;
typedef struct SlPlatformThread SlPlatformThread;

typedef void (*SlPlatformThreadMainFn)(void* user);

/*
 * Platform-owned synchronization wrappers.
 *
 * The returned handles are allocated from `arena` and remain valid until that arena is
 * reset after the corresponding destroy/join calls. These APIs intentionally expose no OS
 * or libuv types to core runtime modules. Lock, unlock, signal, broadcast, wait, destroy,
 * and join tolerate NULL handles so cleanup paths can stay simple.
 */
SlStatus sl_platform_mutex_create(SlArena* arena, SlPlatformMutex** out_mutex);
void sl_platform_mutex_lock(SlPlatformMutex* mutex);
void sl_platform_mutex_unlock(SlPlatformMutex* mutex);
void sl_platform_mutex_destroy(SlPlatformMutex* mutex);

SlStatus sl_platform_cond_create(SlArena* arena, SlPlatformCond** out_cond);
void sl_platform_cond_wait(SlPlatformCond* cond, SlPlatformMutex* mutex);
void sl_platform_cond_signal(SlPlatformCond* cond);
void sl_platform_cond_broadcast(SlPlatformCond* cond);
void sl_platform_cond_destroy(SlPlatformCond* cond);

SlStatus sl_platform_thread_start(SlArena* arena, SlPlatformThreadMainFn main_fn, void* user,
                                  SlPlatformThread** out_thread);
void sl_platform_thread_join(SlPlatformThread* thread);

#ifdef __cplusplus
}
#endif

#endif

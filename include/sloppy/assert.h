#ifndef SLOPPY_ASSERT_H
#define SLOPPY_ASSERT_H

/*
 * Internal invariant assertions.
 *
 * Assertions are for programmer errors inside Sloppy. They must not replace public API input
 * validation, and release builds must still return safe status values for external mistakes.
 */
#ifndef SL_ENABLE_ASSERTS
#define SL_ENABLE_ASSERTS 1
#endif

#if SL_ENABLE_ASSERTS
#include <stdlib.h>
/*
 * Use Sloppy's assertion toggle directly instead of <assert.h> so NDEBUG does not silently
 * disable invariants when SL_ENABLE_ASSERTS is still enabled.
 */
#define SL_ASSERT(expr)                                                                            \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            exit(EXIT_FAILURE);                                                                    \
        }                                                                                          \
    } while (0)
#define SL_ASSERT_MSG(expr, msg)                                                                   \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            (void)(msg);                                                                           \
            exit(EXIT_FAILURE);                                                                    \
        }                                                                                          \
    } while (0)
#define SL_UNREACHABLE() SL_ASSERT_MSG(0, "unreachable")
#else
#define SL_ASSERT(expr)                                                                            \
    do {                                                                                           \
        if (0 && (expr)) {                                                                         \
        }                                                                                          \
    } while (0)
#define SL_ASSERT_MSG(expr, msg)                                                                   \
    do {                                                                                           \
        if (0 && (expr) && (msg)) {                                                                \
        }                                                                                          \
    } while (0)
#define SL_UNREACHABLE()                                                                           \
    do {                                                                                           \
    } while (0)
#endif

#endif

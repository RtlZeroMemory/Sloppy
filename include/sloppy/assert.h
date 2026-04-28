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
#include <assert.h>
#define SL_ASSERT(expr) assert(expr)
#define SL_ASSERT_MSG(expr, msg) assert((expr) && (msg))
#define SL_UNREACHABLE() assert(!"unreachable")
#else
#define SL_ASSERT(expr) ((void)sizeof(expr))
#define SL_ASSERT_MSG(expr, msg) ((void)sizeof(expr), (void)sizeof(msg))
#define SL_UNREACHABLE() ((void)0)
#endif

#endif

/*
 * Sloppy platform and compiler detection.
 *
 * This header centralizes public preprocessor detection so platform checks do not spread
 * through runtime modules. Keep this file small and add capability macros only when code
 * actually needs them. Detection macros are not permission to scatter platform branches
 * through core runtime code; OS behavior belongs behind platform implementations.
 */
#ifndef SLOPPY_PLATFORM_H
#define SLOPPY_PLATFORM_H

#ifdef _WIN32
#define SL_PLATFORM_WINDOWS 1
#else
#define SL_PLATFORM_WINDOWS 0
#endif

#ifdef __APPLE__
#define SL_PLATFORM_APPLE 1
#else
#define SL_PLATFORM_APPLE 0
#endif

#ifdef __linux__
#define SL_PLATFORM_LINUX 1
#else
#define SL_PLATFORM_LINUX 0
#endif

#ifdef __clang__
#define SL_COMPILER_CLANG 1
#else
#define SL_COMPILER_CLANG 0
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define SL_COMPILER_MSVC 1
#else
#define SL_COMPILER_MSVC 0
#endif

#if defined(__GNUC__) && !defined(__clang__)
#define SL_COMPILER_GCC 1
#else
#define SL_COMPILER_GCC 0
#endif

#if SL_PLATFORM_WINDOWS
#define SL_PATH_SEPARATOR '\\'
#else
#define SL_PATH_SEPARATOR '/'
#endif

#endif

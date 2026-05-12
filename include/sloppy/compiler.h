/*
 * Sloppy compile-time version metadata.
 *
 * Keep release-facing tool versions aligned with the npm package line. Build metadata
 * should flow through this boundary without exposing build-system internals to modules.
 */
#ifndef SLOPPY_COMPILER_H
#define SLOPPY_COMPILER_H

#define SL_VERSION_MAJOR 0
#define SL_VERSION_MINOR 1
#define SL_VERSION_PATCH 1
#define SL_VERSION_PRERELEASE "alpha.1"
#define SL_VERSION_STRING "0.1.1-alpha.1"

#endif

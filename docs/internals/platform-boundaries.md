# Platform boundaries

Sloppy is cross-platform by design. Core code never calls an OS API
directly — every OS-touching primitive is a Sloppy-owned abstraction
implemented per platform under `src/platform/`.

Windows x64 is the most validated lane today; that is local
development polish, not permission to write Windows-only core code.

## Layout

```text
include/sloppy/
  platform.h                 detection macros and platform feature flags
  platform_*.h               platform-API headers (process, fs, net, time, ...)

src/platform/
  README.md
  common/                    portable helpers shared across platforms
  win32/                     Win32 implementations (fs, net, os, crypto, time)
  posix/                     POSIX implementations
  linux/                     Linux-specific extensions over posix/
  macos/                     macOS-specific extensions over posix/
  libuv/                     transport backends + async loop driver
                             (async_backend, net_tcp, http_transport, process,
                              thread)
  crypto_password_sodium.c   libsodium-backed password hashing (cross-platform)
```

CMake presets pick which platform subset to build. Boundary scanners in
`tools/windows/` (and Unix equivalents) fail PRs that include OS
headers in core code.

## Boundary rules

1. **No OS headers in core.** Files under `src/core/`, `src/data/`,
   `src/engine/`, and the public headers in `include/sloppy/` may not
   include `windows.h`, `unistd.h`, `sys/*.h`, etc.
2. **No raw OS calls in core.** Core code calls Sloppy-owned APIs
   (`sl_fs_*`, `sl_net_*`, `sl_os_*`, `sl_time_*`, etc.). The platform
   directories implement those APIs against the OS.
3. **No `#ifdef` for OS branches in core.** Branching on `_WIN32` /
   `__APPLE__` / `__linux__` happens in `src/platform/` only.
4. **No libuv types outside `src/platform/libuv/`.** `uv_loop_t`,
   `uv_handle_t`, etc. are platform-internal.
5. **No platform-handle leakage to JS.** Sockets, file descriptors,
   process handles surfaced to JS go through the resource table; JS
   sees an opaque handle, not a number.
6. **Public headers stay portable.** `include/sloppy/*.h` doesn't
   carry `HANDLE`, `int fd`, `uv_*`, or `pthread_t`.

These rules are mechanically enforced — the boundary scanner runs in
CI.

## Subdirectory roles

| Directory                | Owns                                                                 |
| ------------------------ | -------------------------------------------------------------------- |
| `src/platform/common/`   | Portable helpers (UTF-8 path handling, deterministic sort, …)        |
| `src/platform/win32/`    | Win32-specific FS, net, OS, crypto, time                             |
| `src/platform/posix/`    | POSIX baseline used by Linux/macOS                                   |
| `src/platform/linux/`    | Linux-specific (epoll specifics, procfs, …) on top of posix/        |
| `src/platform/macos/`    | macOS-specific (kqueue specifics, security framework, …) on top of posix/ |
| `src/platform/libuv/`    | Async loop, TCP transport, HTTP transport, process spawn, threads    |

A platform may implement *some* but not all of the platform API. The
build system reports unsupported platform features as build-time or
startup errors, never silent fallbacks.

## libuv

libuv is the async backend. It powers:

- The HTTP transport (`http_transport_libuv.c`) — TCP accept, read,
  write, close.
- The async backend (`async_backend_libuv.c`) — readiness signalling
  for off-thread operations to wake the owner thread.
- TCP client connections (`net_tcp_libuv.c`).
- Process spawning (`process.c`) and worker threads (`thread.c`).

libuv types are confined to `src/platform/libuv/`. Core code sees a
Sloppy-shaped async backend interface.

This isolation means swapping libuv for a different async runtime (a
direct epoll/kqueue driver, IO completion ports, io_uring) is a
platform-internal change. None of `src/core/` would have to move.

## Platform feature flags

`include/sloppy/platform.h` defines compile-time flags for things like:

- `SL_PLATFORM_WINDOWS` / `SL_PLATFORM_POSIX` / `SL_PLATFORM_LINUX` /
  `SL_PLATFORM_MACOS`
- `SL_PLATFORM_HAS_<feature>` flags for optional capabilities

Platform implementation files use these to select code paths. Core code
should not need them — if you're tempted to `#ifdef` in core, lift the
divergence into a platform API instead.

## Dynamic dependencies

| Dependency  | Required for                  | Where it's allowed                    |
| ----------- | ----------------------------- | ------------------------------------- |
| libuv       | async loop, transport         | `src/platform/libuv/`                 |
| libpq       | PostgreSQL provider           | `src/data/postgres.c`                 |
| ODBC driver | SQL Server provider           | `src/data/sqlserver.c`                |
| OpenSSL     | inbound TLS                   | `src/platform/libuv/http_transport_libuv.c` |
| libsodium   | password hashing              | `src/platform/crypto_password_sodium.c` |
| V8          | JS execution                  | `src/engine/v8/`                      |
| yyjson      | JSON parsing (Plan, etc.)     | core                                  |

Each one is opt-in at build time when applicable. Missing dependencies
either produce a build error or report unavailability at runtime via
`sloppy doctor` — never silent fallback.

## Tests

- **Boundary scanner** (`tools/windows/check-platform-boundaries.ps1`
  and Unix equivalents) walks the source tree and fails when forbidden
  headers/calls appear in disallowed directories.
- **Per-platform CI lanes** build and run the test suite on Windows,
  Linux, and macOS. A pass in one lane is not a pass in others.
- **Platform-specific tests** live alongside their implementation
  (e.g. Win32 FS edge cases under `tests/unit/platform/win32/`).

## Adding a platform-backed feature

1. Define the abstraction in `include/sloppy/platform_<area>.h`.
2. Implement it under `src/platform/<platform>/`.
3. Use Sloppy types only at the boundary — no OS handles in the
   interface.
4. Add tests for the portable contract and backend-specific edge
   cases.
5. Update `sloppy doctor` if the feature has a runtime availability
   story (drivers, optional libraries).

The boundary is the most rigid part of the codebase. That's
deliberate — it's what keeps "Sloppy is cross-platform" honest.

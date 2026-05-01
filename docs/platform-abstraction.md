# Platform Abstraction

Windows-first is a workflow priority, not permission to write Windows-only core runtime
code.

Sloppy's primary development loop is Windows x64 today, but the runtime architecture must
remain portable to Linux and macOS without rewriting core modules. Platform behavior belongs
behind Sloppy-owned abstraction boundaries.

## Purpose

This document defines the platform boundary that all runtime implementation stories must
respect. It lets Sloppy use Windows x64, `clang-cl`, `lld-link`, CMake, Ninja, and
PowerShell as the first developer loop without letting WinAPI assumptions leak into core
runtime code.

## Scope

This document covers:

- definitions for Windows-first and cross-platform by design;
- directory layout;
- forbidden OS headers and direct OS calls;
- future platform abstraction categories;
- dependency boundaries;
- event loop and dynamic library boundaries;
- scanner behavior;
- tests, CI gates, implementation tasks, and acceptance criteria.

## Non-Goals

The foundation phase does not implement real OS abstraction functions. Do not add WinAPI,
POSIX, Linux, or macOS calls as part of platform foundation work unless a later
implementation story explicitly needs them and places them under `src/platform/*`.

## Current Phase

The repository has:

- `src/platform/` directory skeleton;
- `src/platform/common/`, `win32/`, `posix/`, `linux/`, and `macos/` README files;
- `tools/windows/check-platform-boundaries.ps1`;
- `tools/unix/check-platform-boundaries.sh`;
- scanner self-tests that create temporary positive and allowed-boundary fixtures;
- lint integration for the scanner.
- default CI gates on Windows, Linux, and macOS for non-V8 builds.

## Future Phase

Phase 1 introduces minimal platform-backed APIs only when core primitives require them. The
likely first APIs are page allocation, clocks, environment reads, and file/path functions.

## Definitions

Windows-first means the primary developer path uses Windows x64, `clang-cl`, `lld-link`,
CMake, Ninja, and PowerShell.

Cross-platform by design means the runtime architecture must be portable to Linux and macOS.
Core runtime modules use portable C and Sloppy-owned platform abstractions instead of
calling OS APIs directly.

## Directory Layout

Target structure:

```text
src/platform/
  common/
  win32/
  posix/
  linux/
  macos/
```

Responsibilities:

- `common/` contains shared platform abstraction helpers that do not call platform APIs
  directly;
- `win32/` contains Windows-specific implementations;
- `posix/` contains POSIX-generic implementations;
- `linux/` contains Linux-specific implementations;
- `macos/` contains macOS-specific implementations.

Expected future header/source layout:

```text
include/sloppy/platform.h       # detection macros only
include/sloppy/os.h             # possible public/internal OS abstraction surface later
src/platform/common/
src/platform/win32/
src/platform/posix/
src/platform/linux/
src/platform/macos/
```

`include/sloppy/platform.h` may define compiler/platform detection macros. It must not
become a dumping ground for behavior or permission to scatter `#ifdef _WIN32` across core
logic.

## Core Include Rules

Core runtime modules must not include OS-specific headers.

Forbidden outside platform implementation directories:

- `<windows.h>`;
- `<winsock2.h>`;
- `<io.h>`;
- `<unistd.h>`;
- `<pthread.h>`;
- `<sys/epoll.h>`;
- `<sys/event.h>`.

Direct WinAPI, POSIX, Linux, or macOS calls are also forbidden in core runtime modules.

Allowed locations:

- WinAPI calls in `src/platform/win32/`;
- POSIX calls in `src/platform/posix/`;
- Linux-specific calls in `src/platform/linux/`;
- macOS-specific calls in `src/platform/macos/`.

Forbidden examples outside platform implementation directories:

```c
#include <windows.h>
#include <unistd.h>

CreateFileW(...);
read(...);
pthread_create(...);
```

Core modules should call Sloppy-owned functions instead.

## Platform Abstraction Surface

Future Sloppy-owned APIs should cover the categories below. Names are design targets, not
implemented APIs in the foundation phase.

### Memory/Page Allocation

Future APIs:

- `sl_os_page_alloc`;
- `sl_os_page_free`;
- page size query;
- guard page support later.

Used by arenas and low-level allocators after tests exist.

### File I/O

Future APIs:

- `sl_os_file_open`;
- `sl_os_file_read`;
- `sl_os_file_write`;
- `sl_os_file_close`;
- metadata/stat helpers later.

Filesystem permissions must sit above these APIs.

### Paths

Future APIs:

- `sl_os_path_normalize`;
- path join;
- path comparison policy;
- executable/current directory helpers later.

Path behavior must account for Windows and POSIX differences explicitly.

### Clocks/Time

Future APIs:

- `sl_os_clock_now`;
- monotonic clock;
- wall clock;
- high-resolution timer for benchmarks later.

### Environment

Future APIs:

- `sl_os_env_get`;
- environment enumeration later.

Secret values must not be printed by diagnostics.

### Dynamic Libraries

Future APIs:

- `sl_os_library_load`;
- `sl_os_symbol_get`;
- `sl_os_library_close`.

These are needed for future native plugin/provider loading only.

### Process, Threads, TLS, Terminal

Future APIs should cover:

- process executable path and exit behavior;
- signal/console handling;
- thread creation only if not owned by libuv or another backend;
- thread-local storage;
- terminal color/TTY detection.

Do not expose these broadly until concrete runtime stories need them.

### Filesystem Watch

Filesystem watching is future tooling/runtime work. If implemented, it must live behind the
platform abstraction or a backend boundary, not in core app-host logic.

## Dependency Boundaries

Cross-platform dependencies such as V8, libuv, llhttp, yyjson, sqlite, and Oxc are
acceptable when introduced in their proper phases. They do not remove the need for
Sloppy-owned platform boundaries. Core modules should depend on Sloppy abstractions, not on
whatever platform APIs a dependency uses internally.

## Event Loop

libuv is planned as the first event loop backend, but Sloppy still owns the `SlLoop`
abstraction. Platform-specific event loop experiments later must live behind backend
boundaries rather than leaking into core host logic.

OS thread, thread-local storage, event, wakeup, and timer primitives belong behind platform
abstraction unless they are fully owned by libuv or another explicit backend dependency.
Worker-pool platform dependencies later must live under `src/platform/*` or a documented
backend boundary. See `docs/concurrency.md`.

## Dynamic Libraries

Future native plugins/providers use platform-specific loading only behind platform
implementations:

- `LoadLibrary`/`GetProcAddress` only in `src/platform/win32/`;
- `dlopen`/`dlsym` only in `src/platform/posix/` or platform-specific directories.

Native dynamic plugins remain future work.

## Platform-Boundary Scanner

`tools/windows/check-platform-boundaries.ps1` scans `include/` and `src/` for forbidden
headers outside allowed platform directories.

Expected behavior:

- fail when a forbidden header appears in `include/` or core `src/`;
- allow forbidden headers under platform implementation directories;
- print offending file and header;
- run a self-test with temporary fixtures before the repository scan;
- run from `tools/windows/dev.ps1 lint`;
- fail in CI.

The self-test proves two fixture classes:

- positive cases where forbidden headers under `include/` or core `src/` fail and name the
  offending file/header;
- allowed-boundary cases where matching headers under their platform implementation
  directories do not fail.

The scanner is intentionally lexical and conservative. It proves the documented forbidden
header boundary; it is not a C parser and does not claim to detect every possible direct OS
symbol reference without a forbidden include.

## Testing

Platform abstraction tests should include:

- core tests that do not depend on a specific OS;
- Windows-specific tests only for `src/platform/win32/`;
- POSIX/Linux/macOS tests later in their own CI jobs;
- no test that requires platform APIs from core modules;
- scanner tests proving forbidden include detection.

## Build Expectations

Windows presets exist first. Linux and macOS presets now back default CI:

- `linux-clang`;
- `linux-gcc`;
- `macos-clang`.

These presets are intentionally small Debug non-V8 gates. They prove the portable core,
compiler, default provider tests, and static boundaries on hosted runners. They do not
claim V8 SDK validation, live database services, release package smoke, sanitizers, or
full platform feature parity. CMake should not bake Windows-only assumptions into core
targets. Platform selection should be explicit and visible.

## Tooling

Tooling layout:

```text
tools/
  windows/
  unix/
  common/
```

`common/` is optional later. PowerShell scripts are Windows tooling and live under
`tools/windows/`. Future Bash scripts belong under `tools/unix/`. Root scripts may exist
only as convenience forwarders.

## Error And Diagnostic Behavior

Platform errors should report:

- operation attempted;
- platform backend;
- normalized path/resource when safe;
- OS error code as detail;
- Sloppy diagnostic code;
- fix suggestion when the failure is environment/tooling related.

Examples:

- missing SDK/tool in bootstrap;
- dynamic library load failure later;
- unsupported platform backend;
- path normalization failure.

## Quality Gates

- platform-boundary scanner passes;
- Windows/Linux/macOS default CI passes for non-V8 builds;
- CMake build does not require platform-specific source outside selected backend;
- core code uses Sloppy abstractions, not OS APIs;
- docs update whenever a new platform abstraction category is introduced.

## Implementation Tasks

### Platform Skeleton

Tasks:

- keep platform directories in place;
- keep README files explicit about allowed responsibilities;
- keep root tools as forwarders only.

Acceptance:

- directories exist;
- no real OS functions are required;
- scanner runs in lint.

### Scanner Hardening

Tasks:

- scan tracked C/C++ source and header files;
- allow platform implementation directories;
- fail in CI;
- keep scanner self-tests in the normal lint path.

Acceptance:

- introducing `<windows.h>` in core fails lint;
- platform README examples do not trigger false positives.
- allowed platform implementation fixtures pass the scanner self-test.

### Memory/Page API

Tasks:

- design `sl_os_page_alloc/free`;
- implement Windows and POSIX backends when allocator needs them;
- test allocation, alignment, and failure behavior.

Acceptance:

- no direct `VirtualAlloc`/`mmap` in core allocator code;
- tests pass on Windows backend first.

### Clock API

Tasks:

- design monotonic and wall-clock calls;
- implement Windows backend first;
- add POSIX backend later.

Acceptance:

- diagnostics/benchmarks use Sloppy clock API;
- tests do not depend on exact wall time.

### Environment API

Tasks:

- design env get semantics with `SlStr`/owned buffer rules;
- implement redaction-aware diagnostics;
- test missing and present variables.

Acceptance:

- config layer can read env through abstraction;
- diagnostics show key names, not secret values.

### Dynamic Library API

Tasks:

- design load/symbol/close API;
- keep behind plugin/provider future gate;
- implement only when native dynamic plugins begin.

Acceptance:

- `LoadLibrary`/`dlopen` appear only under platform implementation dirs.

### File/Path API

Tasks:

- design path normalization and file open/read/write;
- integrate with capability checker;
- test Windows path edge cases first.

Acceptance:

- filesystem capability layer never calls OS APIs directly;
- path diagnostics are deterministic.

## Acceptance Criteria

Platform abstraction foundation is accepted when:

- docs define forbidden headers and allowed directories;
- scanner runs from lint and CI;
- scanner self-tests prove positive and allowed-boundary fixture behavior;
- tool layout separates Windows and future Unix scripts;
- `include/sloppy/platform.h` remains detection-only;
- Phase 1 stories know where OS-backed APIs belong.

## Open Questions

- Whether `include/sloppy/os.h` is public or internal-only.
- Exact naming of OS abstraction functions.
- Whether libuv owns all thread and socket behavior.
- How platform-specific tests are selected in CMake.

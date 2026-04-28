# Dependencies

## Purpose

Sloppy should use dependencies where they reduce specialized risk, but not where they define
the product model.

## Scope

This document defines dependency policy, planned dependency order, ownership expectations,
platform-boundary expectations, and acceptance criteria for adding a new dependency.

## Non-Goals

The foundation phase did not add V8, Oxc, libuv, llhttp, sqlite, libpq, ODBC, TLS,
compression, or other runtime dependencies before their relevant phases.

Use dependencies for:

- JavaScript engine implementation;
- TypeScript parser and compiler tooling;
- event loop backend;
- HTTP parser;
- JSON parser;
- SQLite;
- PostgreSQL client library;
- SQL Server ODBC driver access;
- TLS, crypto, and compression;
- testing and fuzzing tools.

Do not outsource:

- application lifecycle;
- routing semantics;
- middleware model;
- dependency-injection semantics;
- permissions model;
- diagnostics style;
- resource lifetime model;
- public API shape.

## Planned Dependencies

- V8: first JavaScript engine backend;
- Oxc: primary TypeScript parser, transform, and app-plan extraction foundation;
- libuv: future event loop abstraction backend;
- llhttp: future HTTP/1 parser;
- yyjson: current Plan v1 JSON parser and future config parser candidate;
- sqlite3: future first-class SQLite integration;
- libpq: future PostgreSQL provider backend;
- Microsoft ODBC Driver for SQL Server / ODBC API: future SQL Server provider backend on
  Windows;
- munit: future C unit test framework;
- future TLS, crypto, and compression dependencies as requirements become concrete.

V8 is special and is not managed by vcpkg initially. It should be consumed as a verified SDK
through Sloppy tooling so normal contributors do not need to build V8 from source.

TASK 07.A adds the first phase-gated V8 SDK detection path. V8 remains optional for normal
foundation builds and CI. V8 validation runs only when requested with
`-DSLOPPY_ENABLE_V8=ON` or `-DSLOPPY_ENGINE=v8`. In that mode `SLOPPY_V8_ROOT` must point
to a prebuilt SDK root with this minimal Windows layout:

```text
<SLOPPY_V8_ROOT>/
  include/v8.h
  include/libplatform/libplatform.h
  lib/v8.lib or lib/v8_monolith*.lib
  lib/v8_libplatform*.lib
  lib/v8_libbase*.lib
  bin/  # optional runtime DLLs for dynamic SDKs
```

CMake validates the headers, `lib/` directory, and the documented library families before
creating the `Sloppy::V8` interface target. The exact V8 version, artifact source,
checksum manifest, and source-build workflow are still deferred. CMake must not download
V8 or hardcode machine-local SDK paths.

SQLite may be consumed through vcpkg, a static build, or a bundled source strategy once the
provider implementation phase begins. libpq may be a vcpkg/build dependency, but release
packaging needs an explicit DLL strategy. SQL Server support depends on Microsoft ODBC
Driver availability on Windows; `sloppy doctor` should later detect whether the external
driver is installed and usable.

All dependencies need explicit ownership, update, security, license, and test strategy
before they become required in the default build.

## Current Dependencies

### yyjson

`yyjson` is added in TASK 06.B through vcpkg manifest mode as the Plan v1 JSON parser.

Why it is used:

- `docs/dependencies.md` already identified yyjson as the intended C JSON/config/plan
  parser;
- parsing JSON correctly is specialized enough that a dependency is safer than a custom
  parser;
- the Plan parser keeps yyjson private to `src/core/plan_parse.c`, so Sloppy's public plan
  API remains `SlBytes`, `SlArena`, `SlPlan`, `SlStatus`, and `SlDiag`.

Build wiring:

- `vcpkg.json` lists `yyjson`;
- CMake uses `find_package(yyjson CONFIG REQUIRED)`;
- `sloppy_core` links `yyjson::yyjson`;
- Windows dev scripts pass the vcpkg toolchain file from local `.sdeps/vcpkg`, `VCPKG_ROOT`,
  or a PATH install.

Ownership and lifetime:

- yyjson document storage is temporary parser storage;
- no yyjson pointer escapes `src/core/plan_parse.c`;
- parsed strings and handler arrays are copied into the caller-provided `SlArena`.

License/update/security:

- vcpkg currently installs yyjson 0.12.0 from the pinned manifest baseline;
- yyjson declares the MIT license in vcpkg;
- update cadence and broader security review remain lightweight until release packaging
  begins.

Tests:

- `tests/unit/core/test_plan_parse.c` covers valid parsing, malformed JSON, wrong field
  types, missing fields, duplicate handler IDs, invalid handler IDs, empty handler arrays,
  and unknown field allowance.

Cross-platform dependencies do not remove the need for Sloppy-owned platform boundaries.
libuv may hide event-loop details internally, but Sloppy still owns the future `SlLoop`
abstraction. Core runtime modules should depend on Sloppy abstractions rather than direct OS
APIs or dependency-specific platform behavior.

## Acceptance Criteria For Adding A Dependency

A dependency can be added when:

- the roadmap epic for that dependency has started;
- docs explain why the dependency is needed;
- build and packaging impact is documented;
- license/security/update ownership is documented;
- tests cover the dependency boundary;
- CI behavior is clear when the dependency is unavailable;
- the dependency does not define Sloppy's public product model.

## Open Questions

- Exact V8 SDK version pinning and update cadence.
- Exact Sloppy V8 SDK prebuilt source, manifest, and checksum policy.
- Exact sqlite packaging strategy.
- Whether PostgreSQL libpq comes from vcpkg or provider package tooling.

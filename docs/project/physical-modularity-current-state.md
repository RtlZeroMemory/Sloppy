# Physical Modularity Current State

This document records the current internal source/build ownership map for the
pre-alpha repository. It is a guardrail for maintainers and scanners, not a
public alpha readiness claim.

## Source Roots

- `include/sloppy/` owns public-ish C API and shared boundary headers. These
  headers must stay self-contained and must not expose V8, libuv, OS, or
  provider dependency headers.
- `src/core/` owns the portable runtime kernel: memory, diagnostics, Plan,
  routing, HTTP semantics, app host, capability checks, async records, and
  Sloppy-owned abstractions for OS/provider/engine work.
- `src/data/` owns native database provider implementations and third-party
  provider dependency headers such as SQLite, libpq, and ODBC.
- `src/engine/` owns engine-neutral dispatch. `src/engine/v8/` is the only
  implementation root that may use V8 types, except V8-specific tests.
- `src/platform/` owns OS, libuv, crypto backend, filesystem, process, network,
  timer, and thread backends.
- `src/cli/` owns CLI implementation fragments included by `src/main.c`.
- `compiler/src/` owns the Rust `sloppyc` compiler modules.
- `stdlib/sloppy/` owns the bootstrap JavaScript facade and internal runtime
  modules.
- `tests/` owns unit, integration, conformance, live, fuzz, fixture, and golden
  coverage by the existing test-domain conventions.

## Dependency Direction

Portable core may depend on public Sloppy headers and core-private headers, but
must not include V8 internals, platform backend internals, provider-specific
headers, CLI fragments, or compiler modules. Platform and data backends may
depend inward on Sloppy-owned public/core contracts. V8 bridge code may depend
on Sloppy-owned runtime/provider contracts while keeping all V8 handles inside
`src/engine/v8/`.

Accepted scanner exceptions are narrow:

- `tests/unit/engine/*` may use V8 types to test V8 bridge internals.
- `tests/unit/core/test_http_transport.cc` and
  `tests/unit/core/test_net_tcp_client.cc` may use libuv as integration-test
  clients.
- `src/engine/v8/intrinsics_postgres.cc`,
  `src/engine/v8/intrinsics_sqlite.cc`, and
  `src/engine/v8/intrinsics_sqlserver.cc` may include provider dependency
  headers for scoped bridge integration.

## CMake Ownership

`cmake/SloppySources.cmake` is the native source ownership index:

- `SLOPPY_CORE_KERNEL_SOURCES` lists portable kernel sources.
- `SLOPPY_DATA_SOURCES` lists provider implementation sources.
- `SLOPPY_ENGINE_SOURCES` lists engine-neutral sources.
- `SLOPPY_PLATFORM_COMMON_SOURCES` and `SLOPPY_PLATFORM_SYSTEM_SOURCES` list
  platform/libuv and OS-family backend sources.
- `SLOPPY_V8_SOURCES` lists V8-only C++ bridge sources when V8 is enabled.
- `SLOPPY_C_LINT_SOURCES` lists C/C++ lint and analysis coverage.

The root `CMakeLists.txt` consumes those lists; new native sources should enter
through the owning module list rather than being appended directly to the root.

## Scanner Coverage

`tools/windows/dev.ps1 lint` runs the physical boundary scanner before the
language standards scanners. The current scanner coverage includes:

- public header dependency leaks;
- core-to-engine/platform/provider/CLI include violations;
- V8 type/header leakage outside V8 bridge and V8-specific tests;
- libuv type/header leakage outside platform/libuv and explicit integration
  tests;
- provider dependency leakage outside provider implementations and scoped V8
  provider bridge files;
- provider implementation attempts to enter V8 directly;
- public examples importing `../../stdlib` instead of the Sloppy facade;
- CMake ownership variables and tracked native source registration;
- `src/cli/*.inc` coverage through the C standards scanner.

The initial inventory found no tracked generated/build artifacts. The active
source-shape fixes in the guardrail PR were public examples using direct
`../../stdlib` imports and CMake source ownership living in one mixed native
source list.

## Deferred Boundaries

This map does not move feature modules into a final `src/modules/<name>/`
layout, split all public/internal headers, or create a dynamic module-loading
system. Those are broader consolidation or feature tracks and must not be
smuggled into this guardrail slice.

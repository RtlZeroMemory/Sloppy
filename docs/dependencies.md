# Dependencies

## Purpose

Sloppy should use dependencies where they reduce specialized risk, but not where they define
the product model.

## Scope

This document defines dependency policy, planned dependency order, ownership expectations,
platform-boundary expectations, and acceptance criteria for adding a new dependency.

## Non-Goals

The foundation phase did not add V8, Oxc, sqlite, libpq, ODBC, TLS, compression, or other
runtime dependencies before their relevant phases. libuv and llhttp are now allowed for the
HTTP/router and async-runtime foundation slices. ODBC is allowed for EPIC-18 SQL Server
provider work and remains isolated to that provider boundary.

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
- libuv: current primary internal async backend implementation;
- llhttp: future HTTP/1 parser;
- yyjson: current Plan v1 JSON parser and future config parser candidate;
- sqlite3: current first-class native SQLite integration;
- libpq: current PostgreSQL provider backend;
- Microsoft ODBC Driver for SQL Server / ODBC API: current SQL Server provider backend on
  Windows;
- munit: future C unit test framework;
- future TLS, crypto, and compression dependencies as requirements become concrete.

V8 is special and is not managed by vcpkg initially. It is consumed as a local SDK through
Sloppy tooling so normal contributors do not need to build V8 from source for default
work.

TASK 07.A adds the first phase-gated V8 SDK detection path. V8 remains optional for normal
foundation builds and CI. V8 validation runs only when requested through the Windows
wrapper with `-EnableV8` or by passing the equivalent CMake options
`-DSLOPPY_ENABLE_V8=ON` or `-DSLOPPY_ENGINE=v8`. The Windows wrapper resolves the SDK
through `tools/windows/v8-sdk.ps1`: `-V8Root` wins for one command, `SLOPPY_V8_ROOT` wins
for one shell/agent, `SLOPPY_V8_SDK_HINTS` can point at portable cache roots, and
`.sdeps/v8/windows-x64` is searched in this and other registered git worktrees. Direct
CMake users must still pass `-DSLOPPY_V8_ROOT=<sdk-root>`. The SDK root must have this
minimal Windows layout:

```text
<SLOPPY_V8_ROOT>/
  include/v8.h
  include/libplatform/libplatform.h
  lib/v8_monolith*.lib
  lib/v8_libplatform*.lib
  lib/v8_libbase*.lib
  lib/libc++*.lib
  support/libcxx/include/
  support/libcxx/buildtools/__config_site
  bin/  # optional runtime DLLs for dynamic SDKs
  share/sloppy-v8-sdk.json
```

CMake validates the headers, `lib/` directory, documented library families, and SDK
manifest before creating the `Sloppy::V8` interface target. The manifest must match the
pinned V8 revision and ABI flags that CMake applies to the bridge compile. TASK 08.A uses
the official V8/depot_tools source workflow, GN, and Ninja to create an ignored local SDK
under `.sdeps/v8/windows-x64` or another explicit ignored path. CMake must not download V8
or hardcode machine-local SDK paths.

The current Windows source SDK is a monolithic release build with Chromium libc++ support.
Use `windows-relwithdebinfo` for V8 execution tests with this SDK. The `windows-dev` Debug
CRT preset remains the default non-V8 contributor path. The monolithic Windows library
contains Chromium allocator-shim overrides for CRT allocation symbols, so the CMake V8
interface target applies the required Windows link override explicitly for V8-enabled
executables.

Use `.\tools\windows\resolve-v8-sdk.ps1` to check what a fresh worktree will use, then
`.\tools\windows\dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8` for local V8
configure. The wrapper imports the MSVC/Windows SDK environment, adds the vcpkg toolchain
on fresh configure, and repairs stale partial CMake caches that were created without the
repo toolchain.

TASK 07.C uses that opt-in target to compile the minimal bridge under `src/engine/v8/` when
V8 is enabled. Default builds still do not require the SDK. V8 tests are registered only
after the SDK gate succeeds, so CI can keep the non-V8 path green without local V8
artifacts.

EPIC-25 distribution policy separates build-time SDKs from runtime packages:

- source builds may use `.sdeps/v8/<platform-arch>` or an explicit `SLOPPY_V8_ROOT`;
- release packages must not require end users to install a V8 SDK;
- SDK headers, import libraries, source trees, and build outputs are never committed and
  are not copied by package scripts;
- static/monolithic V8 linking is preferred when practical because it keeps runtime
  archives simple;
- dynamic runtime DLL/shared-library bundling is the fallback, and only runtime files from
  an SDK `bin/` directory may be copied into `lib/sloppy/engines/v8/`;
- default local packages record `containsV8Sdk: false` and are non-V8 unless a V8-linked
  executable plus required runtime files are explicitly staged.
- package smoke can validate V8 runtime-file staging with an explicit V8-runtime flag, but
  runtime-file presence is not V8 execution evidence by itself.
- V8-enabled package validation must also run the V8-gated artifact execution smoke from
  the extracted package layout and must pass the packaged bootstrap stdlib root explicitly
  until executable-relative lookup is implemented.

SQLite and libpq are consumed through vcpkg manifest mode for their provider implementation
phases. libpq release packaging still needs an explicit DLL strategy. SQL Server support
uses the platform ODBC headers/libraries discovered by CMake and depends on Microsoft ODBC
Driver availability for live connections. The native SQL Server doctor helper detects
driver-manager availability, missing/invalid driver names, and redacted connection
configuration issues ahead of a future CLI `sloppy doctor`.

Default CI restores the vcpkg manifest dependencies on Windows, Linux, and macOS, but it
does not install live database servers or vendor database credentials. SQLite is exercised
in-memory by default. PostgreSQL live tests run only when `SLOPPY_POSTGRES_TEST_URL` is set.
SQL Server live tests run only when `SLOPPY_SQLSERVER_TEST_CONNECTION_STRING` is set and a
runner has ODBC support and a reachable server/driver. Linux/macOS default jobs configure
`SLOPPY_ENABLE_SQLSERVER=OFF` and verify the unavailable/stub path rather than pretending a
SQL Server driver exists.

CTest registers live PostgreSQL and SQL Server tests separately from default provider
tests. Missing live environment variables are reported as CTest skips, not provider
success. When a configured live provider cannot open, the test output classifies the
failure as dependency/driver missing where applicable, service unreachable, credentials
rejected, or test failure without printing connection strings.

EPIC-25 Windows local packages copy the runtime DLLs restored by vcpkg into `bin/` so the
CLI tools can start after extraction outside the checkout. This is local package smoke
plumbing, not a complete public release dependency policy. Package smoke also builds a
tiny supported app with the packaged `sloppyc` so the compiler can run from the extracted
layout. Database drivers, V8 SDK files, and package-manager prerequisites are not
installed or bundled. Default non-V8 package smoke does not connect to PostgreSQL or SQL
Server, does not prove SQL Server ODBC driver installation, does not prove V8 execution,
and does not prove the deferred JS-to-native data bridge.

ENGINE-27 feature descriptors are the source of truth for runtime activation and future
package include-only-used work. They do not currently remove dependencies from the build
graph by themselves. vcpkg/CMake may still compile or link SQLite, libpq, ODBC, libuv, and
stdlib assets even when a particular Plan does not activate those features. Runtime startup
must still initialize only the active Plan features and report unavailable/deferred feature
diagnostics deterministically.

The optional V8 CI job is manual and gated. It requires a runner-local preinstalled SDK
path through the `v8_root` workflow input. If that path is not supplied or does not exist,
the job reports skipped/not configured and does not claim V8 validation. Required CI keeps
`SLOPPY_ENABLE_V8=OFF`.

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
libuv hides backend wakeup details internally, but Sloppy owns `SlAsyncLoop`,
`SlAsyncCompletion`, queue capacity, completion ownership, and the V8 owner-thread rule.
Core runtime modules should depend on Sloppy abstractions rather than direct OS APIs or
dependency-specific platform behavior.

Provider/offload policy is also Sloppy-owned. libuv may provide eventing, timers, wakeups,
and completion plumbing, but libuv's global threadpool is not the provider runtime.
Blocking provider work must go through Sloppy provider executors with explicit capacity,
operation ownership, cancellation/deadline, shutdown, and diagnostics semantics.

### llhttp

`llhttp` is added in TASK 10.B through vcpkg manifest mode as the HTTP/1 request-head
parser.

Why it is used:

- HTTP request parsing is specialized protocol work and should not be hand-rolled;
- TASK 10.B needs a bounded parser skeleton before route dispatch grows;
- Sloppy keeps the public/internal API as `SlBytes`, `SlArena`, `SlHttpRequestHead`,
  `SlStatus`, and `SlDiag`, so llhttp types do not leak through `include/sloppy/http.h`.

Build wiring:

- `vcpkg.json` lists `llhttp`;
- CMake uses `find_package(llhttp CONFIG REQUIRED)`;
- `sloppy_core` links the detected llhttp CMake target, currently `llhttp::llhttp_static`
  with the pinned vcpkg baseline.

Ownership and lifetime:

- llhttp parser state is stack-local inside `src/core/http.c`;
- parsed request target, path, header names, and header values are copied into the
  caller-provided `SlArena`;
- no llhttp pointer escapes the parser wrapper.

License/update/security:

- vcpkg currently installs llhttp 9.3.1 from the pinned manifest baseline;
- llhttp declares the MIT license in vcpkg;
- broader update and security review remain lightweight until release packaging begins.

Tests:

- `tests/unit/core/test_http.c` covers valid request-head parsing, malformed/incomplete
  input, unsupported methods, header capture, max-header enforcement, diagnostics, and route
  matcher reuse with the parsed path.

### libuv

`libuv` is added through vcpkg manifest mode and is now the primary internal
implementation for the ENGINE-12 async backend abstraction.

Why it is used:

- libuv provides a mature cross-platform loop and thread-safe async wakeup primitive;
- Sloppy still owns the async model, queue capacity, completion ownership, diagnostics, and
  V8 owner-thread rule;
- using libuv internally avoids inventing low-level platform wakeup behavior while keeping
  JS/framework APIs independent of libuv.

Build wiring:

- `vcpkg.json` lists `libuv`;
- CMake uses `find_package(libuv CONFIG REQUIRED)`;
- `sloppy_core` links `libuv::uv_a` when available, otherwise `libuv::uv`;
- `src/platform/libuv/async_backend_libuv.c` contains the backend implementation.

Ownership and lifetime:

- `uv_loop_t`, `uv_async_t`, `uv_handle_t`, mutexes, and thread IDs stay inside
  `src/platform/libuv/async_backend_libuv.c`;
- public code uses `include/sloppy/async_backend.h`, `SlAsyncLoop`, and
  `SlAsyncCompletion`;
- no libuv type or compatibility promise is exposed to JavaScript or framework contracts.

License/update/security:

- vcpkg currently installs libuv 1.52.1 from the pinned manifest baseline;
- libuv declares the BSD-3-Clause license in vcpkg;
- broader update and security review remain lightweight until release packaging begins.

Tests:

- `core.async.backend_libuv` covers libuv loop create/dispose, cross-thread completion
  posting, owner-thread dispatch, deterministic overflow, and discard cleanup;
- existing HTTP parser tests still cover the older libuv smoke without network I/O.

### sqlite3

`sqlite3` is added in EPIC-16 through vcpkg manifest mode as the first real Sloppy data
provider backend.

Why it is used:

- SQLite is the planned first provider after the common data/capability foundation;
- using SQLite's library avoids hand-rolling SQL execution, storage, transactions, or
  parameter binding;
- Sloppy keeps the native API as `SlSqliteConnection`, `SlSqliteParam`, `SlSqliteResult`,
  `SlStatus`, and `SlDiag`, so SQLite types do not leak through generic data headers.

Build wiring:

- `vcpkg.json` lists `sqlite3`;
- CMake uses `find_package(unofficial-sqlite3 CONFIG REQUIRED)`;
- `sloppy_core` links `unofficial::sqlite3::sqlite3`;
- SQLite is part of the default dependency restore/build path.

Ownership and lifetime:

- `src/data/sqlite.c` is the only Sloppy source file that includes `sqlite3.h`;
- `SlSqliteConnection` is caller-owned and closes deterministically through
  `sl_sqlite_close`;
- prepared statements are finalized on every path;
- rows, column names, text values, and diagnostics are copied into caller-provided arenas;
- JavaScript sees only the stdlib `data.sqlite` shape today, never a native pointer.

License/update/security:

- sqlite3 is consumed from the pinned vcpkg baseline;
- broader update and release packaging policy remain lightweight until release packaging
  begins.

Tests:

- `tests/unit/data/test_sqlite.c` covers in-memory open/close, use after close, exec,
  parameterized insert, query row shape, queryOne found/missing behavior, primitive
  parameter binding, unsupported parameter diagnostics, commit/rollback, nested transaction
  rejection, transaction use after complete, SQL diagnostics, and invalid open diagnostics.

### libpq

`libpq` is added in EPIC-17 through vcpkg manifest mode as the PostgreSQL client library.

Why it is used:

- PostgreSQL support should use the supported client library instead of a custom wire
  protocol;
- parameter binding, server authentication, connection-string parsing, and result
  lifetimes are specialized provider concerns;
- Sloppy keeps the native API as `SlPostgresConnection`, `SlPostgresParam`,
  `SlPostgresResult`, `SlPostgresPool`, `SlStatus`, and `SlDiag`, so libpq types do not
  leak through generic data headers.

Build wiring:

- `vcpkg.json` lists `libpq`;
- CMake uses `find_package(PostgreSQL REQUIRED)`;
- `sloppy_core` links `PostgreSQL::PostgreSQL`;
- libpq is part of the default dependency restore/build path.

Ownership and lifetime:

- `src/data/postgres.c` is the only Sloppy source file that includes `libpq-fe.h`;
- `SlPostgresConnection` is caller-owned and closes deterministically through
  `sl_postgres_close`;
- `PGresult` values are cleared on every path;
- rows, column names, text values, and diagnostics are copied into caller-provided arenas;
- JavaScript sees only the stdlib `data.postgres` shape today, never a native pointer.

License/update/security:

- libpq is consumed from the pinned vcpkg baseline;
- broader update, CVE, and release packaging policy remain lightweight until release
  packaging begins.

Tests:

- `tests/unit/data/test_postgres.c` covers redaction, invalid options, use after close,
  doctor diagnostics, tiny pool lifecycle behavior, and separately registered env-gated
  live connection/query/transaction/pool behavior.

### ODBC / SQL Server

EPIC-18 adds SQL Server through ODBC without vendoring or installing any SQL Server driver
binaries.

Why it is used:

- SQL Server support should use the platform driver-manager model instead of a custom wire
  protocol;
- the Microsoft ODBC Driver owns SQL Server authentication, TLS negotiation, connection
  string interpretation, and protocol details;
- Sloppy keeps the native API as `SlSqlServerConnection`, `SlSqlServerParam`,
  `SlSqlServerResult`, `SlSqlServerPool`, `SlStatus`, and `SlDiag`, so ODBC handles do not
  leak through generic data headers or JavaScript.

Build wiring:

- `SLOPPY_ENABLE_SQLSERVER` defaults to `ON` on Windows and `OFF` elsewhere;
- when enabled, CMake uses `find_package(ODBC REQUIRED)` and links `ODBC::ODBC`;
- when disabled, the provider symbols remain available as stubs that report ODBC support
  unavailable;
- the Microsoft ODBC Driver for SQL Server is a runtime prerequisite for live use and is
  not downloaded, installed, or vendored by Sloppy.

Ownership and lifetime:

- `src/data/sqlserver.c` is the only Sloppy source file that includes ODBC headers;
- `SlSqlServerConnection` is caller-owned and closes deterministically through
  `sl_sqlserver_close`;
- `SQLHSTMT`, `SQLHDBC`, and `SQLHENV` handles are freed on every path;
- rows, column names, text values, and diagnostics are copied into caller-provided arenas;
- JavaScript sees only the stdlib `data.sqlserver` shape today, never a native pointer.

License/update/security:

- OS ODBC headers/libraries are toolchain/platform components;
- Microsoft ODBC Driver license/update/CVE handling belongs to the local runtime
  environment until release packaging policy is defined;
- diagnostics and docs must redact `PWD`, `Password`, and access-token connection string
  fields.

Tests:

- `tests/unit/data/test_sqlserver.c` covers redaction, driver-name parsing, missing-driver
  diagnostics, invalid options, use after close, unsupported values, pool lifecycle
  behavior, and separately registered env-gated live ODBC connection/query/transaction/pool
  behavior.

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

- Exact V8 SDK update cadence.
- Exact Sloppy V8 SDK prebuilt source and checksum policy.
- Complete sqlite runtime packaging and notice strategy.
- Complete libpq DLL/shared-library packaging and notice strategy.
- Exact dynamic V8 runtime file list for non-monolithic builds.
- Whether default CI should eventually use a documented PostgreSQL service/container.

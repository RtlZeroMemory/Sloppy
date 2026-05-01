# Architecture

## Purpose

This document defines Sloppy's system architecture at a level that future implementation
tasks can use without guessing intent. Sloppy is a TypeScript application runtime with a C
runtime kernel, an isolated C++ V8 bridge, and a Rust compiler/build tool named `sloppyc`.

The product stance is: AI-slop branding, zero-slop architecture. MVP means narrow, not bad.

## Scope

This document covers:

- runtime/compiler/tooling architecture;
- process and artifact boundaries;
- C runtime kernel responsibilities;
- C++ V8 bridge responsibilities;
- Rust `sloppyc` responsibilities;
- JavaScript bootstrap responsibilities;
- app-host responsibilities;
- platform abstraction rules;
- implementation epics and acceptance criteria.

## Non-Goals

The current foundation/runtime-contract work still does not implement:

- production V8 runtime startup with true ESM module loading, arbitrary module graphs, and
  broad bootstrap intrinsics;
- a production HTTP server, request body parser, middleware pipeline, or full framework
  request/response model;
- native app module execution from a compiler-emitted plan;
- JavaScript-to-native database resource/intrinsic integration outside the narrow
  V8-gated SQLite bridge;
- production Sloppy Plan loading from compiler output with compatibility/hash checks;
- TypeScript compilation or app graph extraction;
- native dynamic plugins.

## Current Phase

The repository is in post-Core-MVP foundation work. `sloppyc build` is a real supported-
subset compiler path that emits Plan, bundle, and source-map artifacts for the current
one-file app shape. `sloppy run --artifacts` is a dev/runtime host for those artifacts when
the required runtime lane is configured; direct source-input `sloppy run app.js` remains
deferred.

The C runtime now includes core primitives, Plan parsing, diagnostics, HTTP backend
semantics, libuv-backed localhost transport for the one-request-per-connection MVP,
capability checks, provider executor infrastructure, and native provider boundaries.
Optional V8 builds execute registered handlers, bounded direct Promise/microtask
settlement, request-context/result conversion, and the SQLite bridge. Default gates still
do not prove V8, live providers, public alpha readiness, production HTTP, or benchmark
claims.
ENGINE-03 adds the first V8 async handler runtime cut. In V8-enabled builds, handler calls
drain V8 microtasks explicitly on the owning engine thread, fulfilled Promises are
converted through the existing result conversion path, rejected Promises produce
deterministic diagnostics, and Promises that remain pending after the bounded microtask
drain fail instead of being serialized or reported as success. It also adds a small native
cancellation token shape that can represent cancellation, deadlines, shutdown, and
backpressure snapshots. This is not a Node event loop, timer/fetch/fs/process layer,
worker-thread scheduler, or native async provider completion queue.
TASK 09.C adds the first `SlWorkerPool` design skeleton. It is inline/fake only: work runs
on the caller thread and posts completion back to `SlLoop` for deterministic tests. It has
no real worker threads, cross-thread posting, libuv, OS event-loop integration, blocking
DB/filesystem work, cancellation/deadlines/backpressure, HTTP, or V8 integration.
TASK 10.A adds the first native route pattern parser and matcher foundation. It parses one
minimal path pattern into arena-owned segments, matches one path against that pattern, and
captures route parameters. It is not an HTTP server, request parser, route table/trie,
method dispatcher, public TypeScript API, `app.mapGet`, llhttp/libuv integration, V8
integration, middleware, or compiler work.
TASK 10.B adds the first llhttp/libuv integration skeleton. The normal vcpkg-backed build
now includes llhttp and libuv. The HTTP module can parse one complete in-memory HTTP/1
request head into arena-owned method, raw target, path, and headers, and it has a libuv
loop init/close smoke. It still does not implement socket accept/read/write, streaming
parser state, request bodies, response writing, route dispatch, request lifecycle,
middleware, public TypeScript API, app host behavior, V8 integration, or compiler work.
TASK 10.C adds the first synthetic in-memory GET dispatch path from parsed HTTP request
head to numeric Sloppy Plan handler ID. It uses a manual borrowed route binding table,
matches the parsed path with the existing route matcher, validates the handler ID against
the parsed plan, and invokes the existing runtime-contract/engine boundary. It still does
not implement TCP sockets, a real HTTP server, response writing, body parsing, request
contexts, middleware, public TypeScript API, plan route sections, or compiler extraction.
TASK 11.A adds the source-controlled bootstrap stdlib layout under `stdlib/sloppy/` and
copies it into the build tree at `lib/sloppy/bootstrap/sloppy/`. TASK 11.B/11.C adds the
first tiny ES module facade there: frozen `Results` helpers for text/json descriptors,
`Sloppy.create()`, in-memory `app.mapGet(...)` route registration, `.withName(...)`, and
`app.__getRoutes()` for bootstrap tests/debugging. It does not implement compiler import
rewriting, `app.plan.json` emission, runtime intrinsic binding, module resolution, app
run/build/freeze semantics, HTTP serving, modules, services, middleware, or validation.
TASK 11.D adds `examples/hello/` as a static bootstrap API-shape example that uses a
relative import from `stdlib/sloppy/index.js`. It is not compiler output and is not a
runnable HTTP app.
TASK 12.A/12.B/12.C/12.D adds the first bootstrap app-host foundation skeleton:
`Sloppy.createBuilder()`, `builder.build()`, structural `app.freeze()`, object-backed
config, deterministic memory logging, and string-token singleton/transient services. It is
still JavaScript-only bootstrap state. It does not implement native app-host validation,
`app.run`/`app.listen`, compiler extraction, `app.plan.json` emission, HTTP serving,
modules, middleware, validation, data providers, config file/env providers, native logging,
or real request-scoped service lifetimes.
TASK 13.A/13.B/13.C/13.D adds the bounded developer ergonomics layer on top of
that facade: in-memory route groups and grouped GET registration, a fuller bounded
`Results.*` descriptor helper set, a small `schema` validation skeleton, route metadata
storage for validation shapes, and `examples/ergonomics/`. TASK 14 adds the bootstrap
module skeleton: `Sloppy.module(...)`, `builder.addModule(...)`, dependency graph
validation/topological ordering, services/routes module phases, route/service attribution,
and plan-like module debug metadata. These are still JavaScript-only bootstrap structures
and do not implement compiler extraction, real `app.plan.json` emission, native module
loading, package/module distribution, native plugins, data providers, HTTP serving,
middleware execution, OpenAPI generation, request parsing, automatic validation responses,
route params in native JavaScript contexts, or app run/listen behavior.
TASK 15.A/15.B/15.C/15.D added the JavaScript-only data/capabilities foundation on top of
the bootstrap app-host/module skeleton: database capability metadata, module capability
phase attribution, query template lowering, a fake data provider contract for tests and
examples, transaction callback semantics, and `examples/data-foundation/`. That slice did
not implement real SQLite/PostgreSQL/SQL Server providers, database connections, SQL
execution, native provider resources, permission enforcement, compiler template extraction,
or app-plan data provider emission.
EPIC-16 adds the first real native SQLite provider boundary. It is a C/runtime provider
backed by the vcpkg `sqlite3` dependency and covers in-memory open/close, exec, query,
queryOne, primitive parameter binding, transaction commit/rollback, and diagnostics in
native tests. The JavaScript stdlib exposes `data.sqlite` as the intended public entry
point. MAIN1-08 adds the first V8-gated stdlib-to-native SQLite bridge through safe
resource IDs; non-V8/bootstrap contexts still fail honestly. Compiler extraction,
app-plan data provider emission, and broader HTTP/app-host runtime integration remain
future work.
EPIC-17 and EPIC-18 add native PostgreSQL/libpq and SQL Server/ODBC provider boundaries.
They cover native open/close, parameterized exec/query/queryOne, transactions, redaction
diagnostics, tiny bounded pool skeletons, and env-gated live tests. The JavaScript stdlib
exposes `data.postgres` and `data.sqlserver` metadata/open shapes, but JavaScript-to-native
provider calls still fail honestly until their own V8 intrinsic modules and resource-kind
integration exist. Framework-specific bridge code belongs in dedicated sibling files such as
`src/engine/v8/http_bridge.cc`. Provider bridge code belongs under
`src/engine/v8/intrinsics_<provider>.cc`, registered through `intrinsics.cc`, not in
`engine_v8.cc`.
EPIC-19 adds metadata-only CLI introspection commands: `sloppy routes`, `sloppy doctor`,
`sloppy audit`, and `sloppy openapi`. They read plan-compatible metadata fixtures/artifacts
and do not compile apps, execute handlers, start HTTP, enter V8, or run live database
checks by default.
EPIC-20 adds the first benchmark harness and scenarios for implemented foundations:
route matching/parsing, complete-buffer HTTP request-head parsing, handler plan lookup,
noop dispatch, and synthetic GET dispatch. Benchmark smoke/list checks are correctness
smoke only and are not performance claims.
EPIC-21 adds the first compiler extraction path for one tiny source file. ENGINE-02 expands
that path to supported route-method metadata, direct async-handler metadata, minimal SQLite
provider/capability metadata, deterministic `app.plan.json`/`app.js`, and handler-line
source-map artifacts. EPIC-22 adds the first dev-only `sloppy run --artifacts` path.
EPIC-23 extends that path so V8-enabled builds can load those artifacts, parse route
metadata, materialize route/query/request context, start a tiny local libuv HTTP server or
`--once` synthetic dispatch, and serialize supported result descriptors through the native
response writer. EPIC-24 adds the classic bootstrap runtime asset load, the compiler
rewrite for the public `"sloppy"` import, the internal
`__sloppy_register_handler(id, handler)` intrinsic, and registered-handler validation
before dispatch. ENGINE-04 broadens the dev HTTP runtime to
GET/POST/PUT/PATCH/DELETE route metadata, request headers, bounded JSON/text bodies,
deterministic body/content-type failures, custom response headers, and safe error
responses. Source input handoff, true ESM module loading, production server hardening,
streaming bodies/responses, middleware, hot reload, package-manager behavior, npm
resolution, and Node compatibility remain out of scope.

EPIC-26 adds default non-V8 CI gates for Windows clang-cl, Linux clang/gcc, and macOS
clang, plus optional/manual V8 validation and explicit provider gate reporting. These jobs
prove the current portable non-V8 foundation across hosted OS runners; they do not prove
V8 SDK execution or live database services unless those optional gates are configured and
reported separately.

## Future Phase

The next implementation batch should connect framework/app-layer ergonomics, source-input
run, request binding/validation/config, Strong Plan doctor/OpenAPI work, HTTP
keep-alive/streaming, and later provider expansion. See
`docs/project/post-core-mvp-next-roadmap.md`.

## System Shape

Sloppy uses a compiler-planned, runtime-hosted, engine-executed model:

```text
TypeScript source
  -> sloppyc emits app.js, app.js.map, app.plan.json
  -> C runtime loads and validates app.plan.json
  -> C runtime builds native host graph
  -> C++ V8 bridge loads classic bootstrap runtime and app.js
  -> generated JS registers handlers through an internal intrinsic
  -> C runtime dispatches work by numeric handler ID
```

V8 executes JavaScript, not TypeScript. The runtime does not type-check. Official TypeScript
checking through `tsgo` or `tsc` is future compiler work.

## Concurrency Model

Sloppy uses a JavaScript event-loop model per JS worker/isolate, not thread-per-request.
One V8 isolate is owned by one JS event-loop thread. Native async backends and worker-pool
work may complete elsewhere, but completions are posted back to the owning JS event-loop
thread before JavaScript continuations run.

Multiple JS workers/isolates are future multicore scaling work. See
`docs/concurrency.md` and ADR 0014.

## Process Boundaries

The initial design assumes `sloppy` is one OS process:

- C runtime kernel owns process lifetime;
- V8 runs in-process through the isolated C++ bridge;
- user JavaScript runs inside the V8 isolate/context owned by the bridge;
- native resources are owned by Sloppy resource tables, never by raw JS pointers;
- `sloppyc` is a separate build tool process invoked before runtime or by `sloppy run`.

Future worker processes, process isolation, or OS sandboxing are deferred decisions.

## C Runtime Kernel Responsibilities

The C runtime kernel owns:

- application lifecycle;
- startup and shutdown ordering;
- platform abstraction boundary;
- memory primitives and allocators;
- diagnostics and status model;
- resource tables and generation counters;
- capability and permission checks;
- Sloppy Plan loading and validation later;
- native app graph and graph freeze later;
- route dispatch and request lifecycle later;
- config, logging, services, and app host semantics later;
- event loop backend integration later.

The kernel must not depend on V8 types, Node APIs, Rust internals, or OS-specific APIs in
core modules.

## C++ V8 Bridge Responsibilities

The V8 bridge lives under `src/engine/v8/` and is the only required C++ island.

It will own:

- isolate and context lifecycle;
- classic script evaluation and named global function smoke calls in EPIC-07;
- V8 module loading;
- bootstrap stdlib installation;
- intrinsic/native callback registration;
- handler table registration;
- JS exception and promise rejection conversion into diagnostics;
- conversion between JS values and Sloppy native result structures;
- source map handoff for generated JS locations.

Rules:

- the C runtime talks to engines through `include/sloppy/engine.h`;
- `SlEngine` is opaque and the public ABI exposes only C structs, `SlStatus`, `SlDiag`,
  `SlStr`, and plan handler IDs;
- no `v8::*` type may leak outside `src/engine/v8/`;
- JS never receives raw C pointers;
- native handles exposed to JS use resource IDs with generation counters;
- bridge APIs visible to C runtime must be C-shaped.

## Rust sloppyc Responsibilities

`sloppyc` is a separate Rust project under `compiler/`.

It will eventually own:

- TypeScript project discovery;
- parsing and transform through Oxc;
- import resolution and graph analysis;
- app graph extraction;
- Sloppy Plan validation;
- source map emission;
- JavaScript bundle emission;
- diagnostics with TypeScript source spans;
- official TypeScript checker integration through `tsgo` or `tsc` later.

There is no Rust/C FFI in the current architecture. Artifacts are the boundary:
`app.js`, `app.js.map`, and `app.plan.json`.

## JavaScript Bootstrap Responsibilities

The bootstrap stdlib is future generated or bundled JavaScript loaded before app code.
TASK 11.A establishes the source layout as `stdlib/sloppy/` and the staged package/runtime
support layout as `lib/sloppy/bootstrap/sloppy/`.

It will own:

- public `Sloppy` API facade;
- `Results` helpers;
- module builder objects;
- service/capability wrappers;
- tagged SQL template wrappers;
- handler registration calls into bridge intrinsics;
- ergonomic JS objects over native request/resource IDs.

The bootstrap stdlib must not bypass the runtime's permission/resource model.
The current checked-in modules provide the bounded TASK 11.B/11.C public API shape plus
the TASK 12 app-host foundation skeleton. They deliberately stay as plain JavaScript
descriptors and in-memory registrations until compiler extraction, runtime intrinsic
binding, native app-host validation, and plan emission land in later task-specific PRs.

## App Host Responsibilities

The app host is the product model. It owns:

- builder lifecycle;
- module registration;
- service graph;
- middleware and filters;
- route groups and endpoints;
- graph freeze at `builder.build()`;
- startup validation;
- native dispatch tables;
- diagnostics that explain app structure failures.

The app host should feel like a designed TypeScript backend framework, not raw runtime
callbacks.

Developer ergonomics are a top-level architecture input, not just API polish. The runtime
exists to support builder/app/module semantics, route groups, `Results.*`, validation shape,
services, diagnostics, and Sloppy Plan-powered tooling. See `docs/developer-ergonomics.md`.

## Platform Abstraction Rule

Windows x64 is the first-class development path. Sloppy is cross-platform by design.

Core runtime modules must not include OS-specific headers or call OS APIs directly. Platform
behavior belongs under `src/platform/*`:

- `src/platform/win32/` for WinAPI;
- `src/platform/posix/` for POSIX-generic code;
- `src/platform/linux/` for Linux-specific code;
- `src/platform/macos/` for macOS-specific code.

See `docs/platform-abstraction.md`.

## File And Module Layout

Planned layout:

```text
include/sloppy/          public C headers
src/core/                portable runtime core
src/platform/            platform abstraction backends
src/engine/v8/           isolated C++ V8 bridge
src/modules/             future module graph runtime support
src/data/                future data provider runtime support
stdlib/sloppy/           bootstrap JavaScript stdlib source layout
compiler/                Rust sloppyc project
docs/                    engineering specifications
adr/                     architecture decision records
tests/                   unit, integration, golden, fuzz tests
tools/windows/           first-class Windows tooling
tools/unix/              Linux/macOS tooling and local package smoke
```

## Data Structures

Architecture-level structures to implement later:

- `SlStatus` and `SlStatusCode`;
- `SlStr`, `SlBytes`, `SlBuf`, `SlStringBuilder`;
- `SlArena`;
- `SlResourceId`;
- resource table slot with generation counter;
- diagnostic record with stable code and source span;
- plan model for modules, routes, handlers, services, capabilities, providers;
- handler table keyed by numeric ID.

## Lifecycle Flow

Runtime lifecycle target:

1. initialize platform layer;
2. initialize core allocators and diagnostics;
3. load and validate plan;
4. build native app graph;
5. create an engine through the engine-neutral C ABI;
6. load bootstrap stdlib;
7. load app bundle;
8. verify handler table;
9. enter event loop;
10. drain work;
11. shutdown engine;
12. close resources;
13. report leaks in debug builds.

## Error And Diagnostic Behavior

Architecture errors must produce deterministic diagnostics:

- plan/runtime incompatibility;
- missing handler ID;
- V8 bridge initialization failure;
- platform backend unavailable;
- capability denied;
- stale resource ID;
- app graph cycle;
- service missing;
- source map unavailable.

`SlStatus` is for control flow. Diagnostics are for humans and tools.

## Testing Requirements

Architecture foundation tests should include:

- CLI smoke tests for `sloppy` and `sloppyc`;
- platform-boundary scanner;
- C primitive unit tests as Phase 1 begins;
- plan schema golden tests once the loader exists;
- V8 bridge smoke test only in the engine bridge phase;
- integration test for handwritten `app.js` plus `app.plan.json` before compiler work.

## Quality Gates

Required gates:

- CMake configure/build;
- CTest;
- `clang-format`;
- `clang-tidy`;
- platform-boundary scan;
- `cargo fmt --check`;
- `cargo clippy -- -D warnings`;
- `cargo test`;
- artifact hygiene.

## Development Epics

- EPIC 00: Foundation/spec/tooling.
- EPIC 01: Platform abstraction skeleton.
- EPIC 02: Core basics: status/source location/strings/bytes/checked math.
- EPIC 03: Allocators: SlArena.
- EPIC 04: Diagnostics foundation.
- EPIC 05: Resource table/lifecycle kernel.
- EPIC 06: Plan schema and loader.
- EPIC 07: V8 bridge smoke test.
- EPIC 08: Handwritten artifact execution milestone.
- EPIC 09: Event loop/concurrency foundation.
- EPIC 10: HTTP/router foundation.
- EPIC 11: Public TypeScript API bootstrap.
- EPIC 12: App host foundation.
- EPIC 13: Developer ergonomics layer.
- EPIC 14: Modularity/app modules.
- EPIC 15: Data/capabilities foundation.
- EPIC 16: SQLite provider.
- EPIC 17: PostgreSQL provider.
- EPIC 18: SQL Server provider.
- EPIC 19: CLI introspection.
- EPIC 20: Benchmarks/performance validation.

See `docs/roadmap.md` for full epic detail.

## Acceptance Criteria

Architecture foundation is accepted when:

- docs and ADRs describe runtime/compiler/platform/tooling boundaries;
- core code has no OS-specific includes outside `src/platform/*`;
- CMake builds the runtime with warnings-as-errors;
- CTest covers runtime, compiler, conformance, CLI, and package-smoke evidence lanes where
  configured;
- Rust gates pass for `sloppyc`;
- no generated artifacts are tracked;
- future implementation tasks can identify target files, tests, and constraints.

## Open Questions

- Exact public C API shape for platform abstraction.
- Exact V8 SDK artifact layout.
- Exact file-based plan loader API after the TASK 06.B yyjson byte parser.
- Whether source maps are parsed in C or delegated to a tooling helper.
- How much dynamic mode is allowed before v0.1.

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

The foundation phase does not implement:

- V8 runtime integration;
- HTTP or routing;
- app module execution;
- SQLite, PostgreSQL, or SQL Server;
- Sloppy Plan loading;
- TypeScript compilation;
- native dynamic plugins.

## Current Phase

The repository is in foundation/spec/tooling phase. The placeholder `sloppy` and `sloppyc`
CLIs exist only to prove toolchain wiring. They are not runtime or compiler implementations.
TASK 07.A adds optional V8 SDK detection and normal builds do not require V8. TASK 07.B
adds the engine-neutral `SlEngine` C ABI and a noop engine implementation. TASK 07.C adds a
V8-enabled smoke bridge that can initialize V8, evaluate a classic JavaScript source string,
call a named global zero-argument function, and copy a string result back to C. It does not
execute Sloppy Plan handlers, load modules, run HTTP, or provide the public JS API.
TASK 09.A adds the first `SlLoop` completion queue skeleton. It is caller-backed,
fixed-capacity, and synchronous, with no libuv, OS event loop, threads, HTTP, promise
settlement, or V8 microtask integration.
TASK 09.B adds the first native `SlAsync` promise settlement model skeleton over `SlLoop`.
It is caller-owned, manual/fake native settlement only, with no V8 Promise integration,
microtask handling, request-scope retention, HTTP lifecycle, worker pool, cross-thread
posting, cancellation/deadline/backpressure, or libuv integration.
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

## Future Phase

Implementation starts with platform abstraction skeleton and core C primitives. Runtime
features come only after standards, diagnostics, tests, and quality gates exist for the
supporting layer.

## System Shape

Sloppy uses a compiler-planned, runtime-hosted, engine-executed model:

```text
TypeScript source
  -> sloppyc emits app.js, app.js.map, app.plan.json
  -> C runtime loads and validates app.plan.json
  -> C runtime builds native host graph
  -> C++ V8 bridge loads app.js
  -> JS bootstrap registers handlers
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
The current checked-in modules provide only the bounded TASK 11.B/11.C public API shape.
They deliberately stay as plain JavaScript descriptors and in-memory route registrations
until compiler extraction, runtime intrinsic binding, and app-host graph freeze land in
later task-specific PRs.

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
tools/unix/              future Linux/macOS tooling
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
- EPIC 06: Event loop abstraction.
- EPIC 07: V8 bridge smoke test.
- EPIC 08: Plan schema and loader.
- EPIC 09: Handwritten artifact execution milestone.
- EPIC 10: sloppyc fake emitter.
- EPIC 11: HTTP/router foundation.
- EPIC 12: Public TypeScript API bootstrap.
- EPIC 13: Developer ergonomics layer.
- EPIC 14: Modularity/app modules.
- EPIC 15: Config/logging/services.
- EPIC 16: Filesystem/capabilities.
- EPIC 17: SQLite provider.
- EPIC 18: PostgreSQL provider.
- EPIC 19: SQL Server provider.
- EPIC 20: CLI introspection.
- EPIC 21: Benchmarks/performance validation.

See `docs/roadmap.md` for full epic detail.

## Acceptance Criteria

Architecture foundation is accepted when:

- docs and ADRs describe runtime/compiler/platform/tooling boundaries;
- core code has no OS-specific includes outside `src/platform/*`;
- CMake builds the placeholder runtime with warnings-as-errors;
- CTest covers both placeholder CLIs;
- Rust gates pass for `sloppyc`;
- no generated artifacts are tracked;
- future implementation tasks can identify target files, tests, and constraints.

## Open Questions

- Exact public C API shape for platform abstraction.
- Exact V8 SDK artifact layout.
- Exact file-based plan loader API after the TASK 06.B yyjson byte parser.
- Whether source maps are parsed in C or delegated to a tooling helper.
- How much dynamic mode is allowed before v0.1.

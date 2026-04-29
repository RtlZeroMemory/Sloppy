# Engine V8 Module

## Status

SDK detection implemented for TASK 07.A. TASK 07.B adds the engine-neutral C ABI and noop
engine stub. TASK 07.C adds the first V8-backed smoke path when the build is explicitly
configured with `SLOPPY_ENABLE_V8=ON` and a valid SDK. TASK 07.D adds the first basic V8
exception-to-`SlDiag` mapping skeleton for that smoke path. TASK 08.A adds the first
handwritten `app.plan.json` + `app.js` execution smoke through the V8 bridge.
EPIC-21 compiler output deliberately targets the same classic-script global-function shape
for now. EPIC-22 `sloppy run` requires this V8-enabled path to execute compiler artifacts.

## Purpose

Host V8 behind an isolated C++ bridge without leaking V8 types into the C runtime.

## Scope

Implemented now:

- explicit CMake opt-in for V8 SDK validation;
- `SLOPPY_V8_ROOT` cache path;
- imported `Sloppy::V8` interface target after SDK validation succeeds;
- Windows helper validation through `tools/windows/fetch-v8.ps1 -ValidateOnly`.
- `include/sloppy/engine.h` with opaque `SlEngine`;
- `SlEngineOptions`, `SlEngineInfo`, and explicit engine kind values;
- create/destroy/info lifecycle shape;
- `SlEngineHandlerCall`, `SlEngineResult`, and `sl_engine_call_handler`;
- noop engine creation for `SL_ENGINE_KIND_NONE`;
- deterministic unsupported behavior for noop handler calls and V8-disabled builds;
- V8 isolate/context creation and destruction in `src/engine/v8/`;
- classic script source evaluation through `sl_engine_eval_source`;
- zero-argument global function calls through `sl_engine_call_function0`;
- copied string results through `SlEngineResult`.
- basic exception diagnostics for compile/eval/call failures.
- `sl_runtime_contract_call_handler` maps a parsed plan handler ID to a named JS global
  and invokes it through the engine boundary;
- V8-gated handwritten artifact integration fixtures under
  `tests/integration/execution/handwritten_smoke/`.
- compiler-generated `app.js` artifacts can define `globalThis.__sloppy_handler_N`
  functions that match the current runtime-contract lookup shape.
- `sloppy run --artifacts <dir>` creates a V8 engine, evaluates the artifact `app.js`, and
  dispatches matched GET routes through `sl_runtime_contract_call_handler`.

Later scope:

- handler registration intrinsics/function handles;
- V8 Promise integration that maps resolved/rejected JS promises onto the native `SlAsync`
  settlement model or a documented evolution of it.

## Non-goals

No public JS API bootstrap module loading, ESM resolver, full app host, request context,
handler table registration, V8 Promise integration, microtask policy, workers, inspector,
snapshots, Node compatibility, or package-manager behavior. EPIC-22 HTTP usage is limited
to the dev-only CLI path and does not make this bridge a production server boundary.

## Public/Internal API

Runtime-visible APIs are C-shaped and engine-neutral through `include/sloppy/engine.h`.
`SlEngine` is opaque to callers. Public structs use Sloppy primitives (`SlStatus`,
`SlDiag`, `SlStr`, `SlArena`, and `SlHandlerId`) and do not expose C++ or V8 types. V8
implementation details stay under `src/engine/v8/`.

Current behavior:

- `SL_ENGINE_KIND_NONE` creates an arena-backed noop engine;
- `sl_engine_destroy(NULL)` is allowed;
- `sl_engine_info` returns stable noop metadata for active noop engines;
- `SL_ENGINE_KIND_V8` creates a V8 isolate/context only when V8 is enabled at configure
  time; otherwise it returns `SL_STATUS_UNSUPPORTED`;
- `sl_engine_eval_source` evaluates borrowed classic JavaScript source strings in the
  engine context, using `source_name` as the generated JavaScript diagnostic label;
- `sl_engine_call_function0` looks up a named global function, calls it with no arguments,
  and copies a string return value into the caller-provided arena;
- `sl_runtime_contract_call_handler` looks up a handler ID in `SlPlan`, validates the
  export name, and calls the named global through `sl_engine_call_function0`;
- EPIC-21 generated `app.js` uses classic-script `globalThis.__sloppy_handler_N`
  assignments plus a tiny compiler-generated `Results.text/json` shim that returns strings
  for the current engine result boundary until EPIC-24 owns stdlib bootstrap module loading
  and EPIC-23 owns descriptor conversion;
- `sloppy run` fails clearly with "requires V8-enabled build" when this bridge is not
  compiled in;
- `sl_engine_call_handler` exists as a future engine-owned handler dispatch shape but
  still returns `SL_STATUS_UNSUPPORTED` for the noop engine.

Build options:

- `SLOPPY_ENABLE_V8` defaults to `OFF`. Setting it to `ON` enables the V8 SDK gate.
- `SLOPPY_ENGINE` defaults to `none`. Setting it to `v8` also enables the V8 SDK gate.
- `SLOPPY_V8_ROOT` points to a prebuilt V8 SDK root and is required only when V8 is
  enabled.

Default foundation builds and CI do not require V8.

The current source-built Windows SDK is a release/RelWithDebInfo SDK. Do not link it into
the Debug CRT build. Use `windows-relwithdebinfo` for local V8 execution tests unless a
separate matching Debug V8 SDK is built and packaged.

## Ownership/Lifetime Rules

The noop engine is allocated from the caller-provided arena and owns no external resources.
The V8 engine wrapper is arena-backed only after V8 isolate/context creation succeeds, so
failed creates do not consume caller arena capacity. V8 isolate/context resources are
bridge-owned and released by `sl_engine_destroy`. Callers must destroy an engine before
resetting the arena that backs the opaque handle.

`sl_engine_call_function0` copies supported string results into the caller-provided result
arena. Returned `SlStr` views remain valid until that arena is reset or its backing storage
ends. No V8 handle or raw native pointer escapes the bridge, and JS never receives raw
native pointers.

Diagnostics produced by the V8 bridge are built through `SlDiagBuilder` in the engine
arena. Exception message text, generated source names, hints, and bounded stack summaries
are copied before returning to C. They remain valid until the engine arena is reset; callers
may still pass `out_diag == NULL`, in which case the bridge returns the failure status
without materializing a diagnostic.

## Invariants

One V8 isolate has one owning JS thread.

V8 headers and `v8::*` types may appear only below `src/engine/v8/`.

The current ABI is not thread-safe. TASK 07.C creates and enters the isolate on the calling
thread and documents the owner-thread rule, but does not enforce cross-thread entry yet.
Owner-thread checks remain deferred until later bridge/event-loop tasks.

TASK 09.B's `SlAsync` model lives in the C runtime core and does not include V8 types. The
future bridge should settle returned JS promises by posting back to the owning loop and must
continue to keep V8 handles and microtask policy inside `src/engine/v8/`.

V8 requires process-wide platform initialization. TASK 07.C initializes that state once,
keeps it private to `src/engine/v8/`, and intentionally leaves it alive for process
lifetime. Per-engine destroy releases isolates and contexts only. A future explicit runtime
shutdown task owns any decision to call `v8::V8::Dispose()` / `DisposePlatform()`.

Expected SDK layout:

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
```

The approved local source-build strategy uses official V8/depot_tools source, GN, Ninja,
and an ignored SDK under `.sdeps/v8/windows-x64`. The source checkout may live in an
ignored local work root such as `.sdeps/v8-work` or an explicit local drive path. The
normal default build never fetches or builds V8.

The current GN shape is an embeddable monolithic release build:

```gn
is_debug = false
target_cpu = "x64"
is_component_build = false
v8_monolithic = true
v8_use_external_startup_data = false
v8_enable_temporal_support = false
```

Temporal is disabled for the smoke SDK because it pulls Rust `temporal_rs` link
requirements that are outside the 0.2 runtime contract. Chromium libc++ support headers
and `libc++.lib` are packaged because the current V8 public C++ ABI uses libc++ types in
public functions such as `v8::platform::NewDefaultPlatform`.

Exact prebuilt artifact hosting, checksum policy, and update cadence remain deferred.

## Diagnostics

Current engine diagnostics include `SLOPPY_E_UNSUPPORTED_ENGINE` for unsupported noop
operations, `SLOPPY_E_ENGINE_COMPILE_ERROR` for V8 syntax/compile failures,
`SLOPPY_E_ENGINE_EXCEPTION` for thrown eval/function exceptions, and
`SLOPPY_E_ENGINE_CALL_ERROR` for missing/non-callable smoke globals and unsupported smoke
result types.

The mapping is intentionally basic. It captures V8 exception message text, generated
source/resource name when available, 1-based line and column when V8 reports them, and a
bounded stack string as a related note when practical. V8 reports start columns as
zero-based; the bridge converts them to Sloppy's 1-based diagnostic column convention. No
source maps, TypeScript remapping, rich code frames, async stack policy, route/handler
context, V8 promise rejection policy, Sloppy Plan execution, public JS API, Node
compatibility, or package-manager behavior is implemented here.

## Tests

Current checks:

- default non-V8 configure/build/test gates;
- V8-enabled configure fails during CMake configure when `SLOPPY_V8_ROOT` is empty or
  invalid;
- `tools/windows/fetch-v8.ps1 -ValidateOnly -V8Root <sdk-root>` reports missing layout
  pieces;
- C standards scanner rejects V8 headers and `v8::` references outside `src/engine/v8/`.
- `core.engine.abi` covers noop create/info/destroy, invalid options, V8 unsupported
  creation in non-V8 builds, noop handler-call unsupported behavior, noop eval/call
  unsupported behavior, and invalid handler-call arguments.
- `engine.v8.smoke` is registered only when V8 is enabled and covers classic script
  evaluation, global function call returning `sloppy-ok`, syntax-error diagnostics,
  missing function diagnostics, non-callable global diagnostics, thrown function
  diagnostics, unsupported result diagnostics, and create/destroy/create lifecycle
  behavior.
- `execution.handwritten_artifact` is registered only when V8 is enabled and covers parsing
  the handwritten plan fixture, evaluating handwritten `app.js`, invoking handler ID `1`,
  missing plan handler ID diagnostics, missing JS function diagnostics, and thrown handler
  diagnostics.
- `http.dispatch.execution` is registered only when V8 is enabled and covers synthetic
  in-memory GET dispatch from parsed HTTP request head through a manual route binding to a
  numeric handler ID, plus missing JavaScript function and throwing handler diagnostics.
- `bootstrap.stdlib.assets` runs in the default CTest suite and verifies the bootstrap
  stdlib source files and copied build-tree assets exist. `bootstrap.stdlib.api_shape`
  statically checks the bootstrap JavaScript API shape. When `node` is available,
  `bootstrap.stdlib.app_host_foundation` executes the ESM stdlib to cover the JavaScript
  app-host skeleton. These checks do not prove V8 ESM module loading.
- `examples.hello.api_shape` runs in the default CTest suite and statically checks the
  first hello example. It also does not execute JavaScript or load ESM modules.

Later checks:

- wrong-thread checks.
- bootstrap stdlib loading and intrinsic binding.

EPIC-20's default benchmark harness does not require V8. Handler dispatch benchmarks cover
plan lookup and the current noop engine boundary by default. Any V8 handler-call benchmark
must be explicitly gated by `SLOPPY_ENABLE_V8`, an approved `SLOPPY_V8_ROOT`, and a
Release-compatible SDK, and the runtime `--include-v8` flag before its numbers can be
reported. V8-required benchmark entries are filtered or skipped unless `--include-v8` is
provided.

## Source Docs

- `docs/architecture.md`;
- `docs/execution-model.md`;
- `docs/concurrency.md`;
- `docs/testing-strategy.md`;
- ADR 0003;
- ADR 0014.

## Open Questions

- Exact prebuilt V8 SDK hosting and checksum format.
- Dynamic runtime DLL packaging rules.

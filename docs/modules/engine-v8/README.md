# Engine V8 Module

## Status

SDK detection implemented for TASK 07.A. TASK 07.B adds the engine-neutral C ABI and noop
engine stub. TASK 07.C adds the first V8-backed smoke path when the build is explicitly
configured with `SLOPPY_ENABLE_V8=ON` and a valid SDK. TASK 07.D adds the first basic V8
exception-to-`SlDiag` mapping skeleton for that smoke path.

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

Later scope:

- handler-ID dispatch from Sloppy Plan metadata;
- promise settlement.

## Non-goals

No HTTP, compiler extraction, public JS API bootstrap, module loading, ESM resolver, app
plan handler execution, handler table registration, async/promise model, workers,
inspector, snapshots, Node compatibility, or package-manager behavior.

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
- `sl_engine_call_handler` exists as the future handler dispatch shape but returns
  `SL_STATUS_UNSUPPORTED` for the noop engine and remains unsupported for V8 until handler
  registration/plan mapping lands.

Build options:

- `SLOPPY_ENABLE_V8` defaults to `OFF`. Setting it to `ON` enables the V8 SDK gate.
- `SLOPPY_ENGINE` defaults to `none`. Setting it to `v8` also enables the V8 SDK gate.
- `SLOPPY_V8_ROOT` points to a prebuilt V8 SDK root and is required only when V8 is
  enabled.

Default foundation builds and CI do not require V8.

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

V8 requires process-wide platform initialization. TASK 07.C initializes that state once,
keeps it private to `src/engine/v8/`, and intentionally leaves it alive for process
lifetime. Per-engine destroy releases isolates and contexts only. A future explicit runtime
shutdown task owns any decision to call `v8::V8::Dispose()` / `DisposePlatform()`.

Expected SDK layout:

```text
<SLOPPY_V8_ROOT>/
  include/v8.h
  include/libplatform/libplatform.h
  lib/v8.lib or lib/v8_monolith*.lib
  lib/v8_libplatform*.lib
  lib/v8_libbase*.lib
  bin/  # optional runtime DLLs for dynamic SDKs
```

The exact prebuilt SDK source, version pin, manifest, and DLL packaging strategy remain
deferred.

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
context, promise rejection policy, Sloppy Plan execution, public JS API, Node compatibility,
or package-manager behavior is implemented here.

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

Later checks:

- wrong-thread checks.

## Source Docs

- `docs/architecture.md`;
- `docs/execution-model.md`;
- `docs/concurrency.md`;
- `docs/testing-strategy.md`;
- ADR 0003;
- ADR 0014.

## Open Questions

- Exact V8 SDK source and version pin.
- Sloppy V8 SDK manifest/checksum format.
- Dynamic runtime DLL packaging rules.

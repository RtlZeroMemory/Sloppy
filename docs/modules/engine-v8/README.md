# Engine V8 Module

## Status

SDK detection implemented for TASK 07.A. TASK 07.B adds the engine-neutral C ABI and noop
engine stub. Runtime V8 bridge code is still planned / not implemented yet.

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
- deterministic unsupported behavior for V8 creation and noop handler calls.

Later scope:

- V8-backed bridge implementation behind the existing ABI;
- isolate/context lifecycle;
- handler calls;
- exceptions;
- promise settlement.

## Non-goals

No HTTP, compiler extraction, public JS API bootstrap, V8 initialization, isolate/context
creation, module loading, handler execution, or JavaScript execution in the current
TASK 07.A/07.B implementation state.

## Public/Internal API

Runtime-visible APIs are C-shaped and engine-neutral through `include/sloppy/engine.h`.
`SlEngine` is opaque to callers. Public structs use Sloppy primitives (`SlStatus`,
`SlDiag`, `SlStr`, `SlArena`, and `SlHandlerId`) and do not expose C++ or V8 types. V8
implementation details stay under `src/engine/v8/`.

Current behavior:

- `SL_ENGINE_KIND_NONE` creates an arena-backed noop engine;
- `sl_engine_destroy(NULL)` is allowed;
- `sl_engine_info` returns stable noop metadata for active noop engines;
- `SL_ENGINE_KIND_V8` returns `SL_STATUS_UNSUPPORTED` until real bridge work lands;
- `sl_engine_call_handler` exists as the future handler dispatch shape but returns
  `SL_STATUS_UNSUPPORTED` for the noop engine and can emit `SL_DIAG_UNSUPPORTED_ENGINE`.

Build options:

- `SLOPPY_ENABLE_V8` defaults to `OFF`. Setting it to `ON` enables the V8 SDK gate.
- `SLOPPY_ENGINE` defaults to `none`. Setting it to `v8` also enables the V8 SDK gate.
- `SLOPPY_V8_ROOT` points to a prebuilt V8 SDK root and is required only when V8 is
  enabled.

Default foundation builds and CI do not require V8.

## Ownership/Lifetime Rules

The noop engine is allocated from the caller-provided arena and owns no external resources.
Callers must destroy it before resetting that arena. Future V8 handles are bridge-owned. JS
never receives raw native pointers.

## Invariants

One V8 isolate has one owning JS thread.

V8 headers and `v8::*` types may appear only below `src/engine/v8/`.

The current ABI is not thread-safe. Owner-thread enforcement is documented at the boundary
and remains deferred until later bridge tasks.

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
handler execution. Future bridge diagnostics cover initialization, thrown exception,
rejected promise, and handler mismatch failures.

## Tests

Current checks:

- default non-V8 configure/build/test gates;
- V8-enabled configure fails during CMake configure when `SLOPPY_V8_ROOT` is empty or
  invalid;
- `tools/windows/fetch-v8.ps1 -ValidateOnly -V8Root <sdk-root>` reports missing layout
  pieces;
- C standards scanner rejects V8 headers and `v8::` references outside `src/engine/v8/`.
- `core.engine.abi` covers noop create/info/destroy, invalid options, V8 unsupported
  creation, noop handler-call unsupported behavior, and invalid handler-call arguments.

Later checks:

- bridge smoke;
- thrown exception diagnostic;
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

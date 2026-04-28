# Engine V8 Module

## Status

SDK detection implemented for TASK 07.A. Runtime bridge code is still planned / not
implemented yet.

## Purpose

Host V8 behind an isolated C++ bridge without leaking V8 types into the C runtime.

## Scope

Implemented now:

- explicit CMake opt-in for V8 SDK validation;
- `SLOPPY_V8_ROOT` cache path;
- imported `Sloppy::V8` interface target after SDK validation succeeds;
- Windows helper validation through `tools/windows/fetch-v8.ps1 -ValidateOnly`.

Later scope:

- bridge ABI;
- isolate/context lifecycle;
- handler calls;
- exceptions;
- promise settlement.

## Non-goals

No HTTP, compiler extraction, public JS API bootstrap, V8 initialization, isolate/context
creation, module loading, handler execution, or JavaScript execution in TASK 07.A.

## Public/Internal API

Runtime-visible APIs must be C-shaped and engine-neutral. V8 implementation details stay
under `src/engine/v8/`.

Build options:

- `SLOPPY_ENABLE_V8` defaults to `OFF`. Setting it to `ON` enables the V8 SDK gate.
- `SLOPPY_ENGINE` defaults to `none`. Setting it to `v8` also enables the V8 SDK gate.
- `SLOPPY_V8_ROOT` points to a prebuilt V8 SDK root and is required only when V8 is
  enabled.

Default foundation builds and CI do not require V8.

## Ownership/Lifetime Rules

V8 handles are bridge-owned. JS never receives raw native pointers.

## Invariants

One V8 isolate has one owning JS thread.

V8 headers and `v8::*` types may appear only below `src/engine/v8/`.

Expected SDK layout:

```text
<SLOPPY_V8_ROOT>/
  include/v8.h
  include/libplatform/libplatform.h
  lib/v8*.lib
  lib/v8_libplatform*.lib
  lib/v8_libbase*.lib
  bin/  # optional runtime DLLs for dynamic SDKs
```

The exact prebuilt SDK source, version pin, manifest, and DLL packaging strategy remain
deferred.

## Diagnostics

Bridge initialization, thrown exception, rejected promise, and handler mismatch diagnostics.

## Tests

Current checks:

- default non-V8 configure/build/test gates;
- V8-enabled configure fails during CMake configure when `SLOPPY_V8_ROOT` is empty or
  invalid;
- `tools/windows/fetch-v8.ps1 -ValidateOnly -V8Root <sdk-root>` reports missing layout
  pieces;
- C standards scanner rejects V8 headers and `v8::` references outside `src/engine/v8/`.

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

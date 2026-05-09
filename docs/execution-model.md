# Execution Model

Sloppy executes validated artifacts through a native app-host. It is a
compile-then-run model even when users invoke source-input shortcuts.

## Current Executable Path

The supported path is:

```text
source app -> sloppyc build -> app.plan.json/app.js/app.js.map -> sloppy run artifacts
```

`src/main.c` exposes `build` and `run` as separate commands and keeps artifact
introspection commands (`routes`, `capabilities`, `doctor`, `audit`, `openapi`)
alongside them.

Source-input run is still compile-first. The default source artifact cache path
is explicit (`.sloppy/cache/dev/source-input` in `src/main.c`).

## Startup Sequence

The runtime model is fail-closed:

1. Parse command mode.
2. Compile if the input mode is source.
3. Parse and validate the plan (`src/core/plan_parse.c`).
4. Validate host startup invariants (`src/core/app_host.c`).
5. Initialize engine lane (noop or V8).
6. Register generated handlers in the engine bridge.
7. Dispatch requests only after metadata/handler validation succeeds.

In current app-host validation, artifacts are constrained to a dev-host contract:
`windows-x64` + `v8` target and runtime minimum compatibility checks are enforced
before serving.

## Engine Modes

Default lanes can validate native behavior without V8 execution. V8-enabled lanes
add JavaScript handler execution and bridge behavior. These are separate evidence
lanes by design.

Within `src/engine/v8/engine_v8.cc`, dispatch uses registered handler IDs and
owner-thread checks. Promise and exception outcomes are translated into Sloppy
diagnostics.

## Metadata Contracts

Execution is gated by plan correctness, not best effort:

- plan version, required sections, and metadata relationships are validated;
- secret-bearing plan fields are rejected;
- missing handler references or duplicate route/provider/capability identities
  fail startup.

## Non-Goals

This model does not claim Node compatibility, package-manager app behavior,
production release readiness, or benchmark-backed performance.

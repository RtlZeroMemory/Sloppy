# Architecture

## Purpose

Sloppy is a pre-alpha runtime project with explicit ownership boundaries:

- Rust compiler (`sloppyc`) owns source parsing and artifact generation.
- C app-host/runtime owns plan validation, lifecycle, and native contracts.
- C++ V8 bridge owns JS execution behind Sloppy-owned ABI types.

This document records current architecture reasoning and boundaries. It does not
serve as release marketing or historical task choreography.

## System Layers

```text
source app
  -> sloppyc
  -> Plan + bundle + source map
  -> app host
  -> runtime kernel
  -> engine bridge / providers / platform backends
```

Artifact execution is intentional: startup can validate app metadata before
request dispatch.

## Runtime Kernel

Current core responsibilities visible in source:

- plan parsing and shape validation (`src/core/plan_parse.c`);
- app startup and request lifecycle invariants (`src/core/app_host.c`);
- command routing and mode selection (`src/main.c`);
- provider-neutral and provider-specific native contracts (`src/data/*.c`).

## Engine Boundary

V8 is optional and isolated to `src/engine/v8/*`.

Architecture invariants:

- one owner thread per isolate for mutable runtime entry;
- V8 types and handles do not leak into public runtime headers;
- generated handlers register through bridge-owned callbacks;
- Promise/exception outcomes are translated into Sloppy diagnostics;
- non-V8 lanes do not count as JS execution evidence.

## Platform Boundary

Core runtime modules do not own OS headers or raw OS APIs directly. Platform and
transport details remain implementation layers behind Sloppy-owned interfaces.

## Provider Boundary

Provider architecture is split by layer:

- plan metadata relationships (`dataProviders` and `capabilities`);
- native provider execution in `src/data/*.c`;
- V8 bridge adaptation in provider intrinsics modules.

This keeps pointer ownership and capability checks explicit and auditable.

## Non-Claims

Current architecture does not imply:

- Node/Bun/Deno or package-manager app compatibility;
- production readiness;
- OS sandbox guarantees;
- public release readiness;
- performance claims from default lanes.

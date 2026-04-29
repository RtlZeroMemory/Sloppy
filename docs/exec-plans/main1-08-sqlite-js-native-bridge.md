# MAIN1-08 SQLite JS-Native Bridge Execution Plan

## Goal

Implement the smallest trusted SQLite JavaScript-to-native bridge for V8-backed Sloppy
handlers while keeping native resources behind generation-checked resource IDs.

## Layering Decision

`src/engine/v8/engine_v8.cc` remains the V8 engine core. It owns isolate/context lifecycle,
handler registration, source evaluation, owner-thread checks, and result/request
conversion. Provider-specific conversion code must not live there.

Provider bridge code follows this layout:

- `src/engine/v8/engine_v8_internal.h`: private V8-only backend/resource-table contract.
- `src/engine/v8/intrinsics.cc`: provider intrinsic aggregator for `__sloppy.data`.
- `src/engine/v8/intrinsics_sqlite.cc`: SQLite-specific argument validation, resource
  lookup, parameter conversion, result materialization, cleanup callback, and native
  provider calls.
- `stdlib/sloppy/data.js`: public JavaScript facade and closed-state wrapper.

Future PostgreSQL, SQL Server, or other provider bridges must add
`intrinsics_<provider>.cc` files and register them through `intrinsics.cc`; they must not
grow `engine_v8.cc`.

## Implementation Steps

1. Add SQLite V8 intrinsics for open, close, exec, query, and queryOne.
2. Route SQLite connections through the MAIN1-07 resource table with kind/generation/live
   validation and deterministic cleanup.
3. Add a narrow stdlib wrapper that stores only the opaque handle and rejects use after
   close before entering native code.
4. Keep capability integration hook-only until MAIN1-10 provides the policy engine.
5. Add default JS wrapper tests and V8-gated bridge tests.
6. Add an executable internal SQLite fixture or explicitly defer public demo status.
7. Update public/module/security/testing/project docs with implemented versus deferred
   behavior.

## Current Non-Goals

- ORM/query builder/migrations.
- PostgreSQL or SQL Server JavaScript bridge.
- Provider pooling beyond native provider support.
- Node APIs, package-manager behavior, or filesystem APIs.
- Broad capability policy implementation before MAIN1-10.

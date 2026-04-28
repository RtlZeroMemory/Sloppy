# Plan Module

## Status

Planned / not implemented yet.

## Purpose

Load and validate `app.plan.json`, the compiler/runtime contract.

## Scope

Plan schema, native structs, JSON loading, validation, diagnostics, and golden fixtures.

## Non-goals

No HTTP, V8 execution, compiler extraction, or data provider implementation.

## Public/Internal API

Planned `include/sloppy/plan.h` and core plan loader/validator internals.

## Ownership/Lifetime Rules

Parsed plan storage and string lifetimes must be explicit.

## Invariants

Unsupported schema versions and malformed plans fail before runtime work is served.

## Diagnostics

Plan errors identify section, offending value, and source location when available.

## Tests

Valid minimal plan, unsupported version, missing fields, duplicate handler, malformed JSON,
and golden fixtures.

## Source Docs

- `docs/app-plan.md`;
- `docs/execution-model.md`;
- `docs/diagnostics.md`;
- `docs/testing-strategy.md`;
- ADR 0004.

## Open Questions

- JSON parser choice and unknown field policy.

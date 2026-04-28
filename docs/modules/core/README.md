# Core Module

## Status

Planned / not implemented yet.

## Purpose

Portable runtime core primitives such as status, source locations, string/byte views,
checked math, and assertions.

## Scope

Core C primitives that other runtime modules can depend on.

## Non-goals

No V8, HTTP, database, OS API, or app-host feature behavior.

## Public/Internal API

Planned public C headers under `include/sloppy/`; implementations under `src/core/`.

## Ownership/Lifetime Rules

Core APIs must document borrowed, owned, arena-owned, and caller-owned values explicitly.

## Invariants

Core code stays portable C17 and does not include OS or V8 headers.

## Diagnostics

Low-level status is separate from human diagnostics.

## Tests

Phase 1 tests cover edge cases, invalid inputs, overflow, and ownership/lifetime behavior.

## Source Docs

- `docs/c-standards.md`;
- `docs/c-style.md`;
- `docs/memory.md`;
- `docs/testing-strategy.md`.

## Open Questions

- Exact C unit test framework integration.

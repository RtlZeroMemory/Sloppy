# HTTP Module

## Status

Planned / not implemented yet.

## Purpose

Provide native HTTP parsing, routing, and dispatch once prerequisite runtime contract work
exists.

## Scope

Route pattern parser/matcher, HTTP integration, request lifecycle, and handler dispatch.

## Non-goals

No HTTP implementation before plan, diagnostics, resource, and engine prerequisites.

## Public/Internal API

Future router and HTTP internals should be native, bounded, and diagnostics-aware.

## Ownership/Lifetime Rules

Request data must have documented ownership and may not outlive its scope unsafely.

## Invariants

Route parsing is bounded and cannot rely on unbounded recursion.

## Diagnostics

Duplicate routes, ambiguous routes, malformed route patterns, and request conversion
errors.

## Tests

Route parser/matcher tests, negative route fixtures, fuzz targets, and integration tests.

## Source Docs

- `docs/developer-ergonomics.md`;
- `docs/execution-model.md`;
- `docs/testing-strategy.md`.

## Open Questions

- Exact llhttp/libuv introduction timing.

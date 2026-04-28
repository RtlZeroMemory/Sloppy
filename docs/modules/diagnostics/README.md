# Diagnostics Module

## Status

Planned / not implemented yet.

## Purpose

Provide stable, deterministic diagnostics for humans and tools.

## Scope

Diagnostic codes, severity, source spans, related spans, hints, renderers, and snapshots.

## Non-goals

No source map parser, localization, or IDE protocol in the foundation pass.

## Public/Internal API

Planned C diagnostic structs and builder/renderer APIs.

## Ownership/Lifetime Rules

Diagnostic strings and spans must document borrowed or owned storage clearly.

## Invariants

Diagnostic codes are stable public contracts once released.

## Diagnostics

This module defines diagnostic output and must test redaction behavior.

## Tests

Golden/snapshot text, stable codes, severity, spans, hints, and JSON later.

## Source Docs

- `docs/diagnostics.md`;
- `docs/testing-strategy.md`.

## Open Questions

- Exact JSON output shape.

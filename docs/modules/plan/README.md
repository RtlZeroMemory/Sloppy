# Plan Module

## Purpose

The Plan module is Sloppy's validated runtime metadata contract. It describes artifacts,
routes, handlers, providers, capabilities, features, diagnostics metadata, and other
startup-owned information the app host can validate before execution.

## Current Status

The runtime parses and validates the current alpha Plan schema from JSON. The compiler emits
deterministic Plan artifacts for the supported source subset. App-host startup consumes
Plan metadata for runtime feature activation, route/provider/capability validation,
artifact loading, and selected CLI/doctor/audit surfaces.

## Invariants

- Plan parsing must be deterministic.
- Parsed strings are owned by the documented arena or intern table.
- Validation must fail closed on malformed required fields.
- Secrets must not be interned or exposed in diagnostics.
- Plan schema changes require docs, tests, and fixture updates.

## Non-Claims

The current Plan schema is alpha. It is not a stable public package format or compatibility
promise.

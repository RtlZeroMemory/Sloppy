# Core Module

## Purpose

The core module provides Sloppy's low-level C contracts: status values, source locations,
borrowed string/byte views, checked arithmetic, arenas, builders, interned metadata,
diagnostics integration, and small ownership primitives.

## Current Status

Implemented core primitives are used by Plan parsing, diagnostics, HTTP parsing/response
paths, route matching, app-host startup, provider metadata, and selected V8/native bridge
paths.

Core code must remain independent from OS headers, V8 types, JavaScript object models, and
provider-specific implementations.

## Invariants

- Borrowed views are not NUL-terminated contracts.
- Arena-owned outputs must state the arena lifetime.
- Checked arithmetic is required for potentially overflowing sizes or offsets.
- Failure paths should leave outputs unchanged or document the exception.
- Diagnostics should use stable codes and avoid leaking secrets.

## Tests

Core tests live under `tests/unit/core/` and related integration fixtures. They should
verify failure paths, rollback behavior, ownership, and deterministic diagnostics rather
than implementation accidents.

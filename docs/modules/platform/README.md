# Platform Module

## Status

Planned / skeleton only.

## Purpose

Keep OS-specific behavior behind Sloppy-owned platform boundaries.

## Scope

Platform directories, scanner rules, and future OS abstraction categories.

## Non-goals

No real OS abstraction functions in the foundation pass.

## Public/Internal API

`include/sloppy/platform.h` is detection-oriented. Future OS behavior belongs behind
Sloppy-owned APIs and `src/platform/*`.

## Ownership/Lifetime Rules

Future platform APIs must document buffer, path, handle, and allocation ownership.

## Invariants

No OS headers or direct OS calls outside `src/platform/*`.

## Diagnostics

Platform failures should identify operation, backend, safe path/resource detail, and OS
error when applicable.

## Tests

Scanner checks and future platform-specific tests.

## Source Docs

- `docs/platform-abstraction.md`;
- `docs/quality-gates.md`;
- `docs/testing-strategy.md`.

## Open Questions

- Whether `include/sloppy/os.h` is public or internal.

# Testing Module

## Status

Planned / skeleton only.

## Purpose

Document the test harnesses, structural scanners, golden tests, diagnostics snapshots,
fuzzing, sanitizers, and benchmarks.

## Scope

CTest, Cargo tests, scanner gates, future golden/snapshot/fuzz/benchmark harnesses, and
docs-as-intent discipline.

## Non-goals

No runtime feature tests before the relevant feature exists.

## Public/Internal API

Developer commands under `tools/windows/dev.ps1` and future test harness conventions.

## Ownership/Lifetime Rules

Test fixtures must avoid generated artifact leakage and machine-local paths.

## Invariants

Tests verify documented intended behavior, not accidental current behavior.

## Diagnostics

Test failures should name the behavior/spec they protect where practical.

## Tests

The testing module is validated by the gates and future harness self-tests.

## Source Docs

- `docs/testing.md`;
- `docs/testing-strategy.md`;
- `docs/quality-gates.md`;
- ADR 0008.

## Open Questions

- Golden update workflow and example test runner.

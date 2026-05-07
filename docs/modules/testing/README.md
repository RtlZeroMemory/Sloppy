# Testing Module

## Status

Active documentation and tooling module.

## Purpose

Document the test harnesses, static scanners, golden tests, diagnostics snapshots,
fuzz/property lanes, package smoke, V8-gated checks, sanitizer lanes, torture/stress lanes,
and benchmark boundaries.

## Scope

CTest, Cargo tests, scanner gates, source-input/package fixtures, goldens, diagnostics
snapshots, fuzz/property tests, sanitizer lanes, stress/torture lanes, and benchmark harness
policy.

## Non-goals

No benchmark or smoke result is a performance claim. No optional lane is treated as pass
evidence when it is skipped, unavailable, or deferred.

## Public/Internal API

Developer commands under `tools/windows/dev.ps1`, CTest labels, fixture READMEs, and the
PR evidence format.

## Ownership/Lifetime Rules

Test fixtures must avoid generated artifact leakage, machine-local paths, and regenerated
source-input/package inputs that hide the behavior under test.

## Invariants

Tests verify documented intended behavior, not accidental current behavior. Docs, tests,
and implementation move together when intent changes.

## Diagnostics

Test failures should name the behavior, source doc, lane, or invariant they protect where
practical. Redaction fixtures must not expose real secrets.

## Tests

This module is validated through docs freshness checks, test-governance checks, CTest,
Cargo tests, and fixture-specific README contracts.

## Source Docs

- `docs/testing.md`;
- `docs/testing-strategy.md`;
- `docs/quality-gates.md`;
- `docs/project/test-platform-inventory.md`;
- ADR 0008.

## Open Questions

- Which optional fuzz, sanitizer, torture, and live-provider lanes should become bounded CI
  targets before public alpha?

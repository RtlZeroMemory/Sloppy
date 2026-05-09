# Testing Inventory

## Purpose

This document maps the repository's active test surfaces. `docs/contributor/testing.md`
defines testing principles and evidence lanes; this file is the operational inventory for
where those lanes live.

## Default Non-V8 Lane

The default lane covers C runtime foundations, parser/Plan behavior, diagnostics, routing,
HTTP parser/backend/transport behavior that does not require V8, provider metadata, package
fixture structure, source-input metadata, and static governance checks.

Typical entrypoints:

```powershell
.\tools\windows\dev.ps1 configure
.\tools\windows\dev.ps1 build
.\tools\windows\dev.ps1 test
.\tools\windows\dev.ps1 lint
```

The default lane covers the default preset. Optional V8 execution, live
providers, production HTTP, package release readiness, sanitizer/stress/torture
lanes, and benchmark comparisons are separate.

## Compiler And Plan Lane

Compiler fixtures live under `compiler/tests/fixtures/`. They pin deterministic Plan,
bundle, source-map, and diagnostic output for the supported source subset. Plan and run
fixtures live under `tests/golden/plan/`, `tests/fixtures/run/`, and related integration
directories.

Compiler tests must verify documented source behavior, unsupported syntax diagnostics,
path normalization, deterministic output, and generated metadata. General
TypeScript lowering, Node resolution, npm package behavior, and final Framework
v2 support are tracked separately.

## V8-Gated Lane

V8-gated tests cover the optional engine bridge: smoke execution, registered handlers,
request context/result conversion, bounded Promise settlement, SQLite bridge behavior,
source-map exception remapping, and owner-thread invariants.

V8-gated coverage is separate from the default lane.

## Source-Input Lane

Source-input fixtures live under `tests/fixtures/source-input/`. Each fixture has metadata
that declares lane, mode, source path, config inputs, expected Plan semantics, doctor output,
diagnostics, required features, platform/dependency requirements, V8 requirements, and
expected exit behavior.

Source-input tests should validate that `sloppy run <source.js>` and
compiler-owned artifact generation share the same documented contract.

## Package Lane

Package fixtures live under `tests/fixtures/package/`. They validate package
layout and outside-checkout behavior for the current experimental package
artifacts. Release readiness, installer behavior, hosted distribution, and
release publishing status use separate release lanes.

## Public And Example Checks

Example checks keep checked-in examples structurally aligned with current source
contracts. Public docs remain pre-alpha; their tests guard boundaries rather
than expand tutorial scope.

## Golden Policy

Goldens are semantic contracts. A golden update must explain the intended behavior change,
not simply accept new output. Negative paths should verify diagnostic code, source span,
hint/redaction, cleanup, and rollback behavior where applicable.

## Optional Lanes

Optional lanes include V8-gated, source-input, package outside-checkout, platform-specific,
dependency-backed, live-network/live-provider, advanced static analysis, fuzz/property,
stress/torture, sanitizer/memory-safety, and benchmark evidence. Skipped optional lanes
must be reported as not run.

Benchmark smoke validates that the harness runs. Performance comparisons need
measured benchmark reports.

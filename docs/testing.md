# Testing Inventory

## Purpose

This document maps the repository's active test surfaces. `docs/testing-strategy.md`
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

The default lane does not prove optional V8 execution, live providers, production HTTP,
package release readiness, sanitizer/stress/torture lanes, or benchmark claims.

## Compiler And Plan Lane

Compiler fixtures live under `compiler/tests/fixtures/`. They pin deterministic Plan,
bundle, source-map, and diagnostic output for the supported source subset. Plan and run
fixtures live under `tests/golden/plan/`, `tests/fixtures/run/`, and related integration
directories.

Compiler tests must verify documented source behavior, unsupported syntax diagnostics,
path normalization, deterministic output, and generated metadata. They must not claim
general TypeScript lowering, Node resolution, npm package behavior, or final Framework v2
support.

## V8-Gated Lane

V8-gated tests cover the optional engine bridge: smoke execution, registered handlers,
request context/result conversion, bounded Promise settlement, SQLite bridge behavior,
source-map exception remapping, and owner-thread invariants.

V8-gated evidence is separate from the default lane. A default pass is not V8 evidence.

## Source-Input Lane

Source-input fixtures live under `tests/fixtures/source-input/`. Each fixture has metadata
that declares lane, mode, source path, config inputs, expected Plan semantics, doctor output,
diagnostics, required features, platform/dependency requirements, V8 requirements, and
expected exit behavior.

Source-input tests should prove that `sloppy run <source.js>` and compiler-owned artifact
generation share the same documented contract.

## Package Lane

Package fixtures live under `tests/fixtures/package/`. They prove package layout and
outside-checkout behavior for the current experimental package artifacts. They do not prove
release readiness, installer behavior, hosted distribution, or public release status.

## Public And Example Checks

Example checks verify that checked-in examples remain structurally aligned with current
source contracts and avoid unsupported claims. Public docs remain pre-alpha and
unpublished; their tests should guard boundaries rather than expand tutorial scope.

## Golden Policy

Goldens are semantic contracts. A golden update must explain the intended behavior change,
not simply accept new output. Negative paths should verify diagnostic code, source span,
hint/redaction, cleanup, and rollback behavior where applicable.

## Optional Lanes

Optional lanes include V8-gated, source-input, package outside-checkout, platform-specific,
dependency-backed, live-network/live-provider, advanced static analysis, fuzz/property,
stress/torture, sanitizer/memory-safety, and benchmark evidence. Skipped optional lanes
are not pass evidence.

Benchmark smoke proves that the harness runs. It does not prove performance.

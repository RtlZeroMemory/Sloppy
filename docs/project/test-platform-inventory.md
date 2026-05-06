# TEST-PLATFORM-01 Test Inventory And Audit

Issue: #654.

This inventory records the TEST-PLATFORM-01 audit pass. It is a review aid, not a
replacement for the executable gates.

## Current Inventory

| Surface | Evidence |
| --- | --- |
| C/native unit tests | Core primitives, diagnostics, resource lifecycle, app-host lifecycle, async/backend, worker pool, filesystem, OS, crypto, codec-adjacent native helpers, HTTP parser/backend/dispatch/transport, Plan parser, providers, and engine ABI. |
| Rust compiler tests | `compiler/tests` fixtures, Plan/source-map/diagnostic expectations, and `cargo test --manifest-path compiler/Cargo.toml`. |
| Bootstrap stdlib tests | ESM bootstrap tests for app host, modules, data, codec, codec properties, OS, HTTP client, and workers when Node is available. These are API-shape and JS behavior evidence, not Node compatibility claims. |
| Integration/source-input | `tests/integration`, CMake source-input handoff scripts, positive/negative source-input metadata fixtures, and V8-gated artifact execution. |
| Conformance | CTest aliases and README indexes under `tests/conformance`, including cross-API scenario tracking and V8 bridge templates. |
| Goldens | CLI, doctor, audit, diagnostics, Plan, source-map, compiler fixture, and example expected outputs with semantic/normalization policy. |
| Fuzz/property | Deterministic seed replay plus opt-in libFuzzer targets for Plan, route pattern, HTTP request, and diagnostics rendering. |
| Package | Windows package smoke, metadata fixture, outside-checkout compile/artifact execution, and archive hygiene checks. |
| Static governance | `AGENTS.md`, `CONTRIBUTING.md`, PR template, `tools/windows/check-test-governance.ps1`, CI static job, and `docs.test_platform_contract`. |

## Audit Findings Fixed In TEST-PLATFORM-01

- `tests/fuzz/README.md` was placeholder-only; it now documents seed replay and libFuzzer
  targets.
- `tests/integration/README.md` said no integration tests existed; it now reflects current
  integration/source-input/package boundaries.
- `src/core/README.md` and `src/engine/v8/README.md` underreported implemented runtime and
  V8 foundations; they now describe current ownership without public alpha or production
  claims.
- PR governance lacked a mandatory evidence lane report; AGENTS, CONTRIBUTING, and the PR
  template now require one.
- Source-input and package evidence had no manually reviewable fixture metadata; fixture
  metadata now records requirements, expected pass/fail modes, and lane separation.

## Deferred Cleanup

The following items remain issue-backed or lane-backed cleanup, not hidden pass evidence:

- Full #652 cross-API end-to-end apps are partial until all involved JS bridge paths are
  executable in a stable V8-gated lane.
- Long #493 torture/crash-resistance runs remain opt-in until bounded and stable enough for
  CI.
- Live PostgreSQL/SQL Server/provider lanes remain optional and must report SKIPPED or
  UNAVAILABLE when prerequisites are absent.
- Sanitizer/memory-safety lanes are documented but not yet default-required for every PR.

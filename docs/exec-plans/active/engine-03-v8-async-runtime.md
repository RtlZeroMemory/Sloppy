# Execution Plan: ENGINE-03 V8 Async Runtime

## Goal

Make async handlers real in the V8 runtime for the bounded ENGINE-03 slice: returned
Promises must settle through the owner-thread microtask drain, rejected Promises must
produce deterministic diagnostics, cancellation/deadline/backpressure primitives must have
a native shape, and request-scope cleanup must run on success and failure paths that the
current runtime can measure.

## Source Docs

- `docs/project/slop-engine-final-shape.md`
- `docs/project/slop-engine-layered-roadmap.md`
- `docs/project/engine-framework-contract.md`
- `docs/execution-model.md`
- `docs/modules/engine-v8/README.md`
- `docs/concurrency.md`
- `docs/diagnostics.md`
- `docs/testing-strategy.md`
- `docs/quality-gates.md`
- Issues #260, #280, #281, #282, and #283

## Non-goals

- No Node event loop, timers, fetch, fs, process, Buffer, npm, or package compatibility.
- No compiler extraction expansion beyond existing handwritten/golden artifacts.
- No worker-thread scheduler or native provider async queue implementation.
- No HTTP body/parser expansion, SQLite bridge expansion, public alpha docs, or benchmark
  claims.

## Scope

- V8 Promise detection and settlement for handler calls.
- Explicit V8 microtask drain after app evaluation and handler calls on the isolate owner
  thread only.
- Async rejection and still-pending Promise diagnostics.
- Native cancellation/deadline/backpressure token snapshot shape and JS `ctx.signal`
  exposure.
- Request-scope cleanup coverage for async resolve, rejection, pre-cancel, and pending
  failure paths.
- V8-gated tests and docs that keep default non-V8 evidence separate.

## Steps

1. Add core cancellation status/diagnostic primitives and tests.
2. Extend V8 handler conversion to settle returned Promises through explicit microtask
   checkpoints.
3. Expose cancellation/deadline snapshots in handler context without raw native pointers.
4. Add V8-gated tests for resolve, reject, pending deadline, owner-thread policy, context
   signal shape, and request cleanup.
5. Update source docs, module docs, testing docs, quality/debt docs, and conformance
   evidence.
6. Run default gates, language gates, cargo gates, diff/status checks, and V8-gated build
   and tests when a local SDK is available.

## Acceptance Criteria

- No returned Promise is serialized as `[object Promise]` or reported as success before
  settlement.
- Promise fulfillment converts through the existing supported result conversion path.
- Promise rejection is observable, deterministic, and diagnostic-backed.
- A Promise that remains pending after the bounded microtask drain fails clearly.
- V8 microtasks drain only on the isolate owner thread.
- Wrong-thread calls fail before entering V8 where detectable.
- Cancellation/deadline/backpressure states have documented native primitives and tests.
- Cleanup runs for the covered async success and failure paths.
- Default non-V8 gates do not claim V8 runtime success.

## Validation Commands

```powershell
.\tools\windows\bootstrap.ps1
.\tools\windows\dev.ps1 configure
.\tools\windows\dev.ps1 build
.\tools\windows\dev.ps1 test
.\tools\windows\dev.ps1 format-check
.\tools\windows\dev.ps1 lint
.\tools\windows\check-js-ts-standards.ps1
.\tools\windows\check-rust-standards.ps1
cargo fmt --manifest-path compiler/Cargo.toml -- --check
cargo clippy --manifest-path compiler/Cargo.toml -- -D warnings
cargo test --manifest-path compiler/Cargo.toml
git diff --check
git status --short --ignored
```

V8-gated evidence, when `.sdeps/v8/windows-x64` is available:

```powershell
cmake --preset windows-dev -DSLOPPY_ENABLE_V8=ON -DSLOPPY_V8_ROOT=".sdeps/v8/windows-x64"
cmake --build --preset windows-dev
ctest --preset windows-dev --output-on-failure
```

## Decision Log

- Use V8's explicit microtask policy and owner-thread checkpoints instead of adding a JS
  event loop or native async scheduler in this slice.
- Treat still-pending Promises as deadline-style handler failures because this runtime does
  not yet implement timers, fetch, provider queues, or shutdown drain/cancel queues.
- Expose cancellation to JavaScript as a copied plain object with `aborted`, `reason`, and
  `throwIfAborted()` instead of exposing native pointers.
- Keep direct async compiler fixtures executable only where the returned Promise can settle
  during the V8 microtask drain; keep broader async body shapes rejected.

## Progress Log

- Added cancellation/deadline/backpressure token primitives and diagnostic codes.
- Added V8 Promise settlement, rejection, pending, cancellation, and owner-thread tests.
- Added V8-gated direct async handler conformance execution.
- Updated source docs to separate ENGINE-03's bounded async support from future native
  event-loop/provider work.

## Risks

- Local default gates do not prove V8 unless the SDK is configured and V8 CTest targets
  run.
- Pending Promise behavior is intentionally bounded; future timer/native-provider queues
  must extend the lifecycle without weakening cleanup guarantees.
- Runtime source-map remapping for async diagnostics remains deferred.

## Completion Notes

Default `windows-dev` evidence passed: bootstrap, configure, build, CTest, format-check,
lint, JS/TS standards, Rust standards, cargo fmt, cargo clippy, cargo test, and
`git diff --check`.

The requested Debug `windows-dev` V8 configure was attempted with the available SDK and
failed at CMake configure because the SDK is packaged with Chromium libc++ release
libraries and cannot link into the Debug CRT configuration. V8 evidence was therefore run
with the documented `windows-relwithdebinfo` preset and the resolved SDK at
`V:\Slop\.sdeps\v8\windows-x64`.

V8 `windows-relwithdebinfo` evidence passed: configure, build,
`.\tools\windows\dev.ps1 test -Preset windows-relwithdebinfo`, raw
`cmake --build --preset windows-relwithdebinfo`, and raw
`ctest --preset windows-relwithdebinfo --output-on-failure`. V8 CTest included
`engine.v8.smoke`, `engine.v8.owner_thread`, and `conformance.async_handler.run_once`.
Live PostgreSQL and SQL Server tests remained skipped because their live environment
variables were not configured.

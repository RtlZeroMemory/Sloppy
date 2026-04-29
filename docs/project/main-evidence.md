# MAIN Evidence

Status: MAIN-01 evidence contract for the executable artifact-path alpha verification.

MAIN-01 is an evidence PR. It proves, or explicitly separates, the smallest supported path:
`examples/compiler-hello/app.js` -> `sloppyc build` -> deterministic artifacts ->
V8-gated `sloppy run --artifacts --once GET /`.

## Default Non-V8 Gates

The default non-V8 gates are:

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
```

These commands prove the default runtime/compiler/test surface, compiler artifact
determinism, docs/static checks, unsupported compiler inputs, source-input run deferral,
missing artifact diagnostics, malformed plan diagnostics, and the non-V8
`sloppy run --artifacts` diagnostic.

This default gate set does not prove V8 execution. A green default test run does not mean
the MAIN hello handler executed through V8.

## Compiler Artifact Smoke

The MAIN artifact smoke command is:

```powershell
sloppyc build examples/compiler-hello/app.js --out .sloppy-main-smoke
```

The output directory must contain `app.plan.json`, `app.js`, and `app.js.map`. Repeated
builds must produce byte-identical artifact contents with stable handler IDs and without
absolute local paths, timestamps, random IDs, or checkout-specific paths.

This proves deterministic compiler emission for the canonical supported source shape. It
does not prove runtime execution.

## V8-Gated Commands

The V8-gated commands are optional/manual unless the local build has a valid V8 SDK.

The positive MAIN run command is:

```powershell
sloppy run --artifacts .sloppy-main-smoke --once GET /
```

Run it only from a V8-enabled build. The expected response body is:

```text
Hello from Sloppy
```

When V8 is not available, report the command as skipped or unavailable and run the default
non-V8 diagnostic test instead. Do not infer V8 success from default gates.

## Unsupported Paths

MAIN evidence covers these unsupported paths:

- `sloppy run <source.js>`: source-input handoff to `sloppyc` is deferred.
- dynamic route patterns in compiler input: rejected by compiler diagnostics.
- arbitrary bare imports such as `express`, `fs`, or `node:fs`: rejected by compiler
  diagnostics.
- missing artifact directory or missing `app.plan.json`: command failure with stderr.
- malformed `app.plan.json`: command failure with stderr.
- missing `app.js`: V8-gated startup failure with stderr.
- V8-disabled `sloppy run --artifacts`: command failure with the V8-required diagnostic.
- unsupported run-once methods: safe `405 Method Not Allowed` response when V8 execution is
  available.

## Out Of Scope Evidence

Package smoke is relevant only when packaging files or package behavior change. MAIN-01
does not require package smoke unless a validation run explicitly chooses to add it to the
report.

Live provider tests are not relevant to MAIN-01. MAIN does not exercise SQLite,
PostgreSQL, SQL Server, provider bridges, credentials, or live database services.

Benchmark claims are not relevant to MAIN-01. Benchmark list/smoke checks prove harness
startup only and must not be reported as runtime performance evidence.

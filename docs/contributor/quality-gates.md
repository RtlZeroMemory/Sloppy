# Quality Gates

Run the gates that match the files and behavior you changed. Keep optional
lanes separate in the report instead of folding them into default success.

## Standard PR Gate

```powershell
git diff --check
.\tools\windows\dev.ps1 configure
.\tools\windows\dev.ps1 build
.\tools\windows\dev.ps1 test
.\tools\windows\dev.ps1 format-check
.\tools\windows\dev.ps1 lint
```

`format-check` runs C/C++ `clang-format --dry-run --Werror` and Rust
`cargo fmt --check` when the tools are available. `lint` runs platform and
physical boundary checks, C/JS/Rust standards checks, docs freshness, core API
integration, test governance, release artifact checks, C complexity warnings,
`clang-tidy` where configured, Rust clippy, and staged-artifact hygiene.

## Evidence Statuses

Use only:

- `PASS`
- `FAIL`
- `SKIPPED`
- `UNAVAILABLE`
- `DEFERRED`
- `NOT RUN`

Skipped optional gates are not pass claims.

## Language-Specific Checks

Run the direct standards checks when touching those surfaces or debugging lint:

```powershell
.\tools\windows\check-c-standards.ps1
.\tools\windows\check-js-ts-standards.ps1
.\tools\windows\check-rust-standards.ps1
```

Rust compiler/tooling changes should also run:

```powershell
cargo fmt --manifest-path compiler\Cargo.toml -- --check
cargo test --manifest-path compiler\Cargo.toml
cargo clippy --manifest-path compiler\Cargo.toml -- -D warnings
```

## Scanner Self-Tests

Run scanner self-tests when changing scanners, documentation policy, release
artifact policy, or test-governance wording:

```powershell
.\tools\windows\check-docs-freshness.ps1 -SelfTest
.\tools\windows\check-test-governance.ps1 -SelfTest
.\tools\windows\check-release-artifacts.ps1 -SelfTest
```

Then run the scanner itself:

```powershell
.\tools\windows\check-docs-freshness.ps1
.\tools\windows\check-test-governance.ps1
.\tools\windows\check-release-artifacts.ps1
```

## V8 Gate

Run a separate V8 lane for runtime, app-host, compiler, bootstrap, provider,
configuration, and V8-adjacent behavior:

```powershell
.\tools\windows\resolve-v8-sdk.ps1
.\tools\windows\dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8
.\tools\windows\dev.ps1 build -Preset windows-relwithdebinfo
.\tools\windows\dev.ps1 test -Preset windows-relwithdebinfo
```

If the V8 resolver fails locally, report the resolver failure. Do not call the
lane skipped, optional, or passed.

## Provider Gates

SQLite native/provider coverage is part of the default and V8 lanes when those
tests are selected by CTest.

PostgreSQL live lane:

```powershell
.\tools\windows\test-live-postgres.ps1
```

SQL Server live lane:

```powershell
.\tools\windows\test-live-sqlserver.ps1
```

Combined live lane:

```powershell
.\tools\windows\test-live-providers.ps1
```

Report missing live services, missing Docker, missing ODBC drivers, and
true-async SQL Server unavailability as unavailable/skipped evidence with the
concrete reason.

## Package Gate

```powershell
.\tools\windows\dev.ps1 package
.\tools\windows\dev.ps1 test-package
```

The package gate is outside-checkout archive evidence. It does not prove public
release readiness, provider readiness, npm application dependency support, or
V8 execution unless those lanes also ran and passed.

## Required PR Report

Before opening a PR, include:

- expected behavior under test;
- source-of-truth docs or task contract;
- explicit non-goals;
- negative paths covered;
- commands run and exact result;
- commands skipped, unavailable, or not run;
- optional V8/live-provider/package/fuzz/stress/benchmark lanes as separate
  evidence categories;
- golden updates and why they are intended;
- secret/redaction checks;
- deferred coverage or cleanup.

Do not stage generated artifacts from `artifacts/`, `build/`, `compiler/target/`,
`target/`, `.sdeps/`, `.sloppy/`, or archive/binary outputs.

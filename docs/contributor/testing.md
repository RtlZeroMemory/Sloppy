# Testing

Sloppy uses named evidence lanes. A broad implementation report should say which
lanes were `PASS`, `FAIL`, `SKIPPED`, `UNAVAILABLE`, `DEFERRED`, or `NOT RUN`.

## Default Windows Test Lane

```powershell
.\tools\windows\dev.ps1 configure
.\tools\windows\dev.ps1 build
.\tools\windows\dev.ps1 test
```

`dev.ps1 test` runs CTest for the selected preset and `cargo test` for the Rust
compiler when Cargo is available.

## V8 Test Lane

```powershell
.\tools\windows\resolve-v8-sdk.ps1
.\tools\windows\dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8
.\tools\windows\dev.ps1 build -Preset windows-relwithdebinfo
.\tools\windows\dev.ps1 test -Preset windows-relwithdebinfo
```

Report this separately from the default non-V8 lane. A default pass does not
prove V8 execution.

## Live Provider Lanes

PostgreSQL:

```powershell
.\tools\windows\test-live-postgres.ps1
```

SQL Server:

```powershell
.\tools\windows\test-live-sqlserver.ps1
```

Both:

```powershell
.\tools\windows\test-live-providers.ps1
```

Use `-NoDocker` only when the service is already running and the required
environment variables are set.

## Package Smoke Lane

```powershell
.\tools\windows\dev.ps1 package
.\tools\windows\dev.ps1 test-package
```

The package smoke extracts the archive outside the checkout and validates the
archive layout, manifest, checksums, CLI smoke, stdlib assets, excluded paths,
and a prebuilt artifact fixture. It must not compile source outside the checkout
when the fixture metadata says `mustNotCompileSource=true`.

## Focused CTest Runs

Use CTest filters for narrow reruns:

```powershell
ctest --test-dir build\windows-dev --output-on-failure -R "data\.sqlite\.provider"
ctest --test-dir build\windows-relwithdebinfo --output-on-failure -R "conformance\.postgres\.(native_live|bridge_live)"
ctest --test-dir build\windows-relwithdebinfo --output-on-failure -R "conformance\.sqlserver\.(native_live|bridge_live)"
```

Focused reruns are useful for debugging, but they do not replace the broader
lane when the broader lane is the requested evidence.

## Evidence Buckets

Use these terms consistently:

- Default non-V8.
- Compiler/Plan.
- V8-gated.
- localhost transport.
- SQLite/capability.
- Source-input.
- package outside-checkout.
- Platform-specific.
- Dependency-backed.
- live-network/live-provider.
- live-provider optional.
- Advanced static analysis.
- Fuzz/property.
- Stress/torture.
- stress/smoke.
- Sanitizer/memory-safety.
- Benchmark.
- benchmark harness.

Representative CTest prefixes:

- `conformance.foundation.*`
- `conformance.v8.*`
- `conformance.http.*`
- `conformance.transport.*`
- `conformance.sqlite.*`
- `conformance.capability.*`
- `conformance.package.*`
- `smoke.*`
- `benchmark.*`

## Reporting Rules

- Skipped optional gates are not pass claims.
- Missing SDKs, services, or optional dependencies are reported as skipped/not configured,
  not as passing evidence.
- Benchmark evidence is not correctness evidence.
- Native provider tests are not app-level V8 bridge evidence.
- Live-provider skips are not default test passes.
- Missing Docker, a missing database service, or a missing ODBC driver is
  unavailable evidence, not a provider success.
- Report secrets only as redacted placeholders.

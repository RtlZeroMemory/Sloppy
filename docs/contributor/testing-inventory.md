# Testing inventory

Where tests live. Pair this with [testing.md](testing.md) for the
principles.

## Layout

```text
tests/
  unit/            per-module unit tests (C, run through CTest)
    core/
    engine/
    data/
    platform/
    plan/
    ...
  integration/     cross-module integration, still in-process
    http_dispatch/
    plan_run/
    ...
  conformance/     end-to-end via the real CLI
    foundation/
    http/
    transport/
    sqlite/
    capability/
    package/
    v8/
  fixtures/        inputs reused across tests
    source-input/  source-shaped Sloppy apps with metadata
    package/       package layout fixtures
    run/           run-mode metadata
  golden/          checked-in expected outputs (Plan, diagnostics, CLI)
    plan/
    diagnostics/
    cli/
  fuzz/            fuzz harnesses, JavaScript randomized targets, and corpora
    corpus/
    fuzz_*.c
    js_fuzz_targets.mjs
  live/            live-provider scripts (PostgreSQL, SQL Server)
  scripts/         test helpers
  cmake/           CMake helpers used by CTest fixtures
  bootstrap/       bootstrap stdlib smoke

compiler/
  tests/
    fixtures/      input.{js,ts} → expected plan/bundle/sourcemap/diagnostic
    sloppyc_tests.rs
```

## What runs in the default lane

```powershell
.\tools\windows\dev.ps1 test
```

Hits CTest for the default preset (`windows-dev`) plus the Cargo test
suite when Cargo is available. That covers:

- C unit tests under `tests/unit/`
- Integration tests under `tests/integration/`
- Conformance fixtures that don't require V8
- Compiler/Plan tests
- Default-safe fuzz seed replay
- JavaScript property tests for bootstrap stdlib and app-host surfaces
- Lint and standards scanners

## V8-gated lane

Built and run separately:

```powershell
.\tools\windows\resolve-v8-sdk.ps1 -Fetch
.\tools\windows\dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8
.\tools\windows\dev.ps1 build -Preset windows-relwithdebinfo
.\tools\windows\dev.ps1 test -Preset windows-relwithdebinfo
```

Adds:

- Engine bridge smoke (`tests/conformance/v8/`)
- Bridge unit tests (`tests/unit/engine/`)
- Source-map exception remapping
- Provider V8 bridge tests (SQLite, optionally Postgres/SQL Server)
- HTTP request-context end-to-end through V8

## Live-provider lanes (opt-in)

```powershell
# PostgreSQL
$env:SLOPPY_POSTGRES_TEST_URL = "postgres://user:pass@localhost/postgres"

# SQL Server (ODBC connection string)
$env:SLOPPY_SQLSERVER_TEST_CONNECTION_STRING = "Driver={...};..."
```

CI exposes `live-postgres`, `live-sqlserver`, `live-providers`, and
`full-ci` labels. Missing Docker, missing ODBC driver, or unavailable
async support is `UNAVAILABLE` — never folded into a default pass.

## Sanitizer lanes (mandatory in CI)

```powershell
# Windows ASan
.\tools\windows\dev.ps1 configure -Preset windows-asan
.\tools\windows\dev.ps1 build -Preset windows-asan
ctest --preset windows-asan --output-on-failure

# libFuzzer seed replay
.\tools\windows\dev.ps1 configure -Preset windows-libfuzzer
.\tools\windows\dev.ps1 build -Preset windows-libfuzzer
ctest --preset windows-libfuzzer -L fuzz --output-on-failure
```

Linux mirror:

```sh
cmake --preset linux-sanitizers
cmake --build --preset linux-sanitizers
ctest --preset linux-sanitizers --output-on-failure
```

## Test engine lanes

```powershell
.\tools\windows\test-engine.ps1 -Tier pr -Area all -Out artifacts\test-engine\pr.json
.\tools\windows\fuzz.ps1 -All -Iterations 1000 -Seed 12345
```

```sh
tools/unix/test-engine.sh --tier pr --area all --out artifacts/test-engine/pr.json
tools/unix/fuzz.sh --all --iterations 1000 --seed 12345
```

The engine records JSON evidence for the selected tier and area. The fuzz
wrappers cover native seed replay, selected libFuzzer mutation runs, and the
JavaScript randomized/property targets listed in [test-engine.md](test-engine.md).

## SIMD lanes

For SIMD backend changes:

```powershell
.\tools\windows\dev.ps1 configure -Preset windows-simd      # SSE2
.\tools\windows\dev.ps1 build -Preset windows-simd

.\tools\windows\dev.ps1 configure -Preset windows-avx2      # AVX2 (AVX2-capable CPU only)
.\tools\windows\dev.ps1 build -Preset windows-avx2
```

These lanes verify scalar/SIMD parity for byte and string primitives.
They don't measure performance — that's separate.

## Benchmark lane

```powershell
.\build\windows-relwithdebinfo\sloppy_bench.exe --smoke --format json
.\tools\windows\bench.ps1 -Suite http -Runtime sloppy,node,bun,deno -Out artifacts\bench\local-comparison.json
.\tools\windows\bench.ps1 -Compare @("artifacts\bench\before.json", "artifacts\bench\after.json")
```

`sloppy_bench --smoke` is harness coverage. The `-Suite` runner is the
manual local runtime comparison engine; it records host/runtime metadata
and validates responses before timing them. Missing comparator runtimes
are reported as `UNAVAILABLE` or `SKIPPED`. For real measurements,
report the full command, build configuration, hardware, workload,
runtime versions, and output. Benchmark output is never correctness
evidence.

## Compiler fixtures

```text
compiler/tests/fixtures/<name>/
  input.js | input.mjs | input.ts
  app.plan.json
  app.js
  app.js.map
  diagnostics.txt   (if the input is rejected)
```

The harness in `compiler/src/sloppyc_tests.rs` runs each input
through `sloppyc` and diffs against the expected outputs. Drift
fails CI.

Negative fixtures assert specific `SLOPPYC_E_*` codes for unsupported
inputs.

## Source-input fixtures

```text
tests/fixtures/source-input/<name>/
  metadata.json    declares lane, mode, V8 requirement, etc.
  app.{js,ts}      the source
  expected/        expected Plan / output / diagnostics
```

Run via `tests/cmake/check_source_input_run.cmake`. Each fixture maps
to one or more CTest cases.

## Package fixtures

```text
tests/fixtures/package/
  ...layout fixtures for outside-checkout smoke
```

`dev.ps1 package` produces the archive; `dev.ps1 test-package` smokes
it from outside the repository checkout.

## Goldens

```text
tests/golden/
  plan/            full Plan outputs for representative apps
  diagnostics/     rendered diagnostic strings (with redaction)
  cli/             CLI command output (routes, openapi, …)
```

Golden updates require an explicit reason in the PR body. See
[testing.md](testing.md#goldens).

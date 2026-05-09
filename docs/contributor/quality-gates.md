# Quality gates

The gates that have to be green before a PR merges. Some are local,
some run in CI; the local commands mirror what CI does.

## Local commands

```powershell
.\tools\windows\dev.ps1 configure
.\tools\windows\dev.ps1 build
.\tools\windows\dev.ps1 test
.\tools\windows\dev.ps1 format-check
.\tools\windows\dev.ps1 lint
git diff --check
```

`lint` runs platform/boundary scans, language standards
(`check-c-standards.ps1`, `check-rust-standards.ps1`,
`check-js-ts-standards.ps1`), docs hygiene checks, release-policy
checks, and complexity warnings.

For runtime/compiler/V8-adjacent work, also run the V8 lane:

```powershell
.\tools\windows\resolve-v8-sdk.ps1 -Fetch
.\tools\windows\dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8
.\tools\windows\dev.ps1 build -Preset windows-relwithdebinfo
.\tools\windows\dev.ps1 test -Preset windows-relwithdebinfo
```

Dogfood evidence is a separate wrapper over existing lanes:

```powershell
.\tools\windows\dev.ps1 dogfood
```

Without V8 it must report V8-required dogfood lanes as `UNAVAILABLE` after
verifying compile/diagnostic behavior. With `-EnableV8`, the same wrapper may
record `PASS` for V8-gated hello and pre-alpha control-plane execution.

## Mandatory CI lanes

Every PR runs:

- **Default non-V8** — the default `dev.ps1 test` lane plus lint and
  scanners.
- **Windows ASan**, **Windows libFuzzer seed replay**, **Linux
  ASan/UBSan** — sanitizer evidence.
- **Compiler/Plan** — `sloppyc` fixtures plus Plan parser tests.
- **Advanced static analysis** — clang-tidy + analyzer for non-doc
  changes; CodeQL for analysis-relevant changes.

A PR can't merge with any required lane skipped, stale, or red.

Use these statuses in PR reports: `PASS`, `FAIL`, `SKIPPED`, `UNAVAILABLE`,
`DEFERRED`, `NOT RUN`.

Use these evidence lane names exactly when they apply: default non-V8,
compiler/Plan, V8-gated, source-input, package outside-checkout,
platform-specific, dependency-backed, live-network/live-provider,
advanced static analysis, fuzz/property, stress/torture,
sanitizer/memory-safety, and benchmark.

## Optional lanes

These run on demand or when labels/inputs select them:

| Lane                 | Trigger                                                         |
| -------------------- | --------------------------------------------------------------- |
| V8-gated             | Required for runtime/compiler/V8-adjacent PRs                   |
| Source-input         | Triggered by source-input fixture changes                       |
| Dogfood              | App/example coverage catalog; wraps source-input, V8, and package evidence |
| Package outside-checkout | `package-smoke` / `full-ci` label, or `workflow_dispatch`   |
| Live PostgreSQL      | `live-postgres` / `live-providers` / `full-ci` label            |
| Live SQL Server      | `live-sqlserver` / `live-providers` / `full-ci` label           |
| SIMD backend (SSE2/AVX2) | SIMD-relevant changes                                       |
| libFuzzer mutation   | Manual workflow dispatch                                        |
| Stress / torture     | Manual workflow dispatch                                        |
| Benchmark            | Manual; output is measurement only, never correctness           |
| benchmark | Manual/local for reports; Cargo scale smoke runs with compiler tests |

When CI is green except for lanes that didn't apply, that's fine. When
a lane is skipped because the environment doesn't support it (no
Docker, no ODBC driver, etc.), it's reported as `UNAVAILABLE` — never
folded into a default pass.

## V8 SDK availability

If `resolve-v8-sdk.ps1` fails on your machine, that's an environment
blocker — report the resolver failure verbatim. Don't pretend the V8
lane was skipped or passed.

## Documentation gates

Docs touch runs the docs hygiene scanners as part of `lint`. The
scanners check for:

- broken cross-links inside `docs/`;
- repository workflow notes leaking into product docs;
- stale planning material;
- claims unsupported by the current implementation.

If you intentionally need to skip one, name it in the PR body.

## What "green CI" doesn't prove

- A passing benchmark smoke is harness coverage, not a performance
  number.
- A local runtime benchmark with missing Node/Bun/Deno is an
  `UNAVAILABLE` comparator lane, not a failed default gate and not a
  successful comparison.
- A skipped optional lane is not a passing optional lane.
- A passing default lane doesn't validate V8 behavior, package smoke,
  or live providers.

PRs that conflate any of these will be sent back.

## Where the buttons live

| Command                                | Effect                                  |
| -------------------------------------- | --------------------------------------- |
| `.\tools\windows\dev.ps1 doctor`       | Environment check                       |
| `.\tools\windows\dev.ps1 configure`    | Configure a CMake preset                |
| `.\tools\windows\dev.ps1 build`        | Build configured targets                |
| `.\tools\windows\dev.ps1 test`         | CTest for the preset + Cargo tests      |
| `.\tools\windows\dev.ps1 format-check` | clang-format / cargo fmt verification   |
| `.\tools\windows\dev.ps1 lint`         | Standards + boundary + docs scanners    |
| `.\tools\windows\dev.ps1 analyze`      | clang-tidy + analyzer (`sloppy_memory_analysis`) |
| `.\tools\windows\dev.ps1 package`      | Build a local archive                   |
| `.\tools\windows\dev.ps1 test-package` | Smoke the archive from outside the repo |

The Unix wrappers (`tools/unix/dev.sh`) implement the same shape where
ported.

## Branch protection

- Don't merge with skipped, stale, or failing required CI.
- Don't `[skip ci]` on a merge-ready commit.
- Don't bypass branch protection.
- Rebase or update from `main` when required checks depend on the
  merge base, then re-run.

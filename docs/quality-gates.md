# Quality Gates

Quality gates are the repository's executable evidence contract. A gate only
proves the lane it actually ran. Skipped, stale, unavailable, cancelled, or
failing required checks are not acceptable merge evidence.

## Baseline Local Workflow

Canonical Windows commands:

```powershell
.\tools\windows\bootstrap.ps1
.\tools\windows\dev.ps1 configure
.\tools\windows\dev.ps1 build
.\tools\windows\dev.ps1 test
.\tools\windows\dev.ps1 format-check
.\tools\windows\dev.ps1 lint
git diff --check
```

`tools/windows/dev.ps1 lint` runs language standards checks, docs freshness and
claim checks, core API integration checks, test-governance checks, C complexity
warnings, optional local `clang-tidy` where configured, and artifact hygiene.

Run narrower checks only when the task scope justifies them, and report that
scope honestly.

## Evidence Status

Use these statuses in PR reports. Canonical status labels are `PASS, FAIL, SKIPPED, UNAVAILABLE`.
`DEFERRED` and `NOT RUN` may be used when a lane was explicitly out of scope or applicable
but not executed.

| Status | Meaning |
| --- | --- |
| `PASS` | The command or lane ran successfully for the stated scope. |
| `FAIL` | The command or lane ran and failed. Include the failure. |
| `SKIPPED` | The lane was intentionally not run. Explain why. |
| `UNAVAILABLE` | Required local dependency or service was unavailable. Include the concrete blocker. |
| `DEFERRED` | The source doc or issue explicitly keeps the lane out of scope. |
| `NOT RUN` | The lane was applicable but was not run. |

Separate lane results must not be folded into the default lane. A default pass
does not imply V8, package, live-provider, advanced static analysis, stress,
torture, or benchmark evidence. Mandatory memory-safety CI provides separate
sanitizer and libFuzzer seed-replay evidence.

Use the PR lane names from `docs/testing-strategy.md`: default non-V8, compiler/Plan,
V8-gated, source-input, package outside-checkout, platform-specific, dependency-backed,
live-network/live-provider, advanced static analysis, fuzz/property, stress/torture,
sanitizer/memory-safety, and benchmark.

## Required CI Rules

- Do not merge with skipped, stale, cancelled, or failing required CI.
- Do not merge when the branch is behind `main` and required checks are stale.
- Do not use `[skip ci]` for a merge-ready commit.
- Do not bypass branch protection.
- Rebase or update from latest `main` when required checks depend on the merge
  base, then rerun applicable gates.

## V8 Lane

Runtime, app-host, compiler, bootstrap, provider, configuration, or V8-adjacent
changes require separate V8-enabled Windows evidence unless the V8 SDK resolver
itself fails:

```powershell
.\tools\windows\resolve-v8-sdk.ps1
.\tools\windows\dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8
.\tools\windows\dev.ps1 build -Preset windows-relwithdebinfo
.\tools\windows\dev.ps1 test -Preset windows-relwithdebinfo
```

If SDK resolution fails on this machine, report the resolver failure as an
environment blocker. Do not count the V8 lane as skipped, optional, or passed.

## Documentation and Static Checks

Documentation work must run `git diff --check` and the docs/static checks that
are wired into lint or available as standalone scripts. Claim checks should
guard public/current docs against construction-language drift, public alpha
claims, production-readiness claims, unsupported compatibility claims, benchmark
or performance overclaims, and obvious real secrets.

Archive docs, issue snapshots, contributor/agent instructions, and tests may
contain historical or fake marker text when that text is clearly scoped.

## Tooling-Specific Gates

- JavaScript/TypeScript examples, scripts, and stdlib changes:
  `tools/windows/check-js-ts-standards.ps1`.
- Rust/compiler changes: `tools/windows/check-rust-standards.ps1`, plus
  `cargo fmt`, `cargo clippy`, and `cargo test` where applicable.
- C/runtime changes: configure, build, test, format-check, lint, and applicable
  V8, advanced static analysis, or sanitizer lanes.
- Package or release tooling changes: package smoke outside the checkout, plus
  artifact hygiene.

## Mandatory Sanitizer And Fuzz Evidence

Windows ASan, Windows libFuzzer seed replay, and Linux ASan/UBSan are mandatory
memory-safety CI evidence. They do not replace default correctness gates, and they must be
reported with their exact preset, target, and result.

Stable local Windows lanes mirror mandatory CI:

```powershell
.\tools\windows\dev.ps1 configure -Preset windows-asan
.\tools\windows\dev.ps1 build -Preset windows-asan
ctest --preset windows-asan --output-on-failure

.\tools\windows\dev.ps1 configure -Preset windows-libfuzzer
.\tools\windows\dev.ps1 build -Preset windows-libfuzzer
ctest --preset windows-libfuzzer -L fuzz --output-on-failure
```

Mandatory CI also runs the `linux-sanitizers` preset with ASan and UBSan enabled. Local
Linux validation should mirror CI:

```bash
cmake --preset linux-sanitizers
cmake --build --preset linux-sanitizers
ctest --preset linux-sanitizers --output-on-failure
```

libFuzzer mutation runs are separate optional evidence. They must name the target, corpus,
toolchain, duration, and whether the run modified the corpus. Generated corpus files are
not committed unless a scoped task deliberately promotes them to reviewed seeds.

## SIMD Evidence

SIMD backends are selected build-time implementations of canonical scalar primitives. The
`memory-simd` CI matrix builds both `windows-simd` with default `SLOPPY_ENABLE_SIMD=AUTO`
selection and `windows-avx2` with `SLOPPY_SIMD_LEVEL=AVX2`, then runs byte/string parity
tests, memory primitive seed replay, and benchmark smoke. Those lanes prove backend
selection and scalar/SIMD parity for the covered primitives on supported runners; benchmark
smoke remains harness evidence, not performance evidence.

Local Windows lane:

```powershell
.\tools\windows\dev.ps1 configure -Preset windows-simd
.\tools\windows\dev.ps1 build -Preset windows-simd
ctest --preset windows-simd -R "core\.(bytes|str)|fuzz\.memory_primitives\.seed_replay|benchmarks\.sloppy_bench" --output-on-failure

.\tools\windows\dev.ps1 configure -Preset windows-avx2
.\tools\windows\dev.ps1 build -Preset windows-avx2
ctest --preset windows-avx2 -R "core\.(bytes|str)|fuzz\.memory_primitives\.seed_replay|benchmarks\.sloppy_bench" --output-on-failure
```

## Advanced Static Analysis

Fast script scanners remain part of default lint. The clang-tidy/analyzer lane is deeper
evidence and is intentionally controlled while the baseline stabilizes. The enforceable
lane runs the memory/core `sloppy_memory_analysis` target; the broader
`sloppy_clang_tidy` target remains exploratory until the full-repo analyzer baseline is
quiet enough to be governed as a gate.

Local Windows lane:

```powershell
.\tools\windows\dev.ps1 configure
.\tools\windows\dev.ps1 analyze
```

GitHub CI can run the optional `advanced static analysis` job through
`workflow_dispatch` with `advanced_analysis=true` or by applying the `memory-analysis`
label to a pull request. That lane is expected for memory-sensitive PRs when practical:
memory primitives, parsers, platform backends, V8/native bridge changes, provider
backends, HTTP parser/transport, resource lifetime work, and security/redaction changes.

Skipped advanced analysis is not pass evidence. If a finding is suppressed, the PR must
name the suppression, issue, reason, and removal condition.

## Final Review

Before opening or updating a PR, inspect `git status`, review the changed files
for accidental scope creep, verify docs/checks agree, and ensure no generated or
ignored artifacts are staged. The PR body should identify source docs, intended
behavior, non-goals, files touched, checks run, skipped or unavailable lanes,
and known deferred cleanup.

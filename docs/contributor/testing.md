# Testing Strategy

Tests are executable specifications. The repository makes that rule mechanical:
each changed behavior needs a source-of-truth contract, tests that fail when that
contract is violated, and docs/spec/test updates in the same PR when intent changes.
Tests must not snapshot current output merely because it exists.

## Principles

- Test documented intent, not implementation accidents.
- Move docs, specs, tests, and code together when behavior changes.
- Cover negative paths, malformed inputs, cleanup, and diagnostics for every
  contract that can fail.
- Keep optional lanes separate from default checks.
- Use goldens as semantic contracts, not as unreviewed output dumps.
- Report skipped or unavailable lanes honestly as not run.

## Evidence Lanes

Every evidence report must name each applicable lane and status: `PASS`, `FAIL`, `SKIPPED`, `UNAVAILABLE`, `DEFERRED`, or `NOT RUN`.
Use those exact status values in PR summaries.

Use these PR lane names when reporting evidence: default non-V8, compiler/Plan, V8-gated,
source-input, package outside-checkout, platform-specific, dependency-backed,
live-network/live-provider, advanced static analysis, fuzz/property, stress/torture,
sanitizer/memory-safety, and benchmark.

| Lane | What it validates |
| --- | --- |
| Default non-V8 | Native/Cargo/CTest/scanner behavior in the default preset. |
| Compiler/Plan | Rust compiler, emitted artifact, Plan parser, and Plan semantic contract behavior. |
| V8-gated | V8-enabled runtime behavior and engine bridge contracts. |
| Source-input | `sloppyc` source compilation and `sloppy run <source>` paths. |
| Package outside-checkout | Source/package layout and outside-checkout smoke behavior. |
| Platform-specific | Windows, Linux, macOS, or SDK-specific behavior. |
| Dependency-backed | Behavior requiring a local SDK, tool, driver, or service dependency. |
| Live-network/live-provider | Dependency-backed provider behavior against a real service. |
| Advanced static analysis | Clang-tidy, Clang Static Analyzer, custom AST/query checks, or similar static tooling. |
| Fuzz/property | Deterministic seed replay or mutation evidence for parser/protocol contracts. |
| Stress/torture | Long-running, high-volume, race, cancellation, or shutdown pressure. |
| Sanitizer/memory-safety | Address, undefined-behavior, thread, or memory-safety sanitizer evidence. |
| Benchmark | Measured Release benchmark output only; never correctness evidence. |

Default non-V8 checks cover the default preset. V8, sanitizer, libFuzzer
seed-replay, package, live-provider, and benchmark lanes are reported
separately. Package smoke validates package behavior only. Benchmark smoke
validates harness execution only.

## Conformance Matrix Terms

Use these lane names when summarizing the conformance matrix:

- Default non-V8
- V8-gated
- localhost transport
- SQLite/capability
- package outside-checkout
- live-provider optional
- stress/smoke
- benchmark harness

Use these test families when mapping a PR to CTest output:

- `conformance.foundation.*`
- `conformance.v8.*`
- `conformance.http.*`
- `conformance.transport.*`
- `conformance.sqlite.*`
- `conformance.capability.*`
- `conformance.package.*`
- `smoke.*`
- `benchmark.*`

Report skipped or unavailable lanes honestly as not run. In CTest output, optional lanes
may appear as skipped/not configured.

Database provider evidence uses the same lane vocabulary. SQLite embedded conformance may
run in the default lane; PostgreSQL and SQL Server service behavior belongs to
live-network/live-provider; JS bridge behavior belongs to V8-gated; provider lifecycle
pressure belongs to stress/torture; and provider benchmark output remains benchmark
output only. A skipped or unavailable Docker/driver/service lane cannot be
reported as provider validation.

## Golden Policy

Goldens are semantic contracts. Structured goldens should assert normalized
semantic fields. Text goldens are reserved for deliberate UX contracts such as
diagnostic rendering and CLI output.

Golden updates must:

- explain the intended behavior change;
- preserve redaction and secret checks;
- state the lane they validate;
- avoid regenerating source-input or package fixtures from the code path being
  tested;
- keep fixture descriptions current in local README files.

See `tests/golden/README.md` and `tests/fixtures/source-input/README.md` for
local fixture rules.

## Negative Paths

Negative-path tests are required when contracts reject input or clean up
resources. They should cover malformed plans, invalid descriptors, unsupported
configuration, missing files, partial reads/writes, cancellation, shutdown,
allocation failure where injectable, redaction, and capability denial where
applicable.

Tests must fail on the contract violation itself. Avoid tests that only assert a
function returned an error while ignoring the diagnostic, cleanup, or rollback
contract.

## Fuzz, Torture, Sanitizers, and Benchmarks

Default-safe fuzz/property seed replay may run in the default lane when it is
deterministic and bounded. Windows ASan, Windows libFuzzer seed replay, and Linux
ASan/UBSan are mandatory memory-safety CI lanes and must be reported separately.
The SSE2-targeted `windows-simd` and AVX2-targeted `windows-avx2` lanes are mandatory
for SIMD backend PRs and report scalar/SIMD parity plus seed replay, not performance.
`windows-avx2` must only run on AVX2-capable CPUs. libFuzzer mutation, long fuzzing,
stress/torture, live-provider checks, package checks, V8 checks, and benchmarks remain
separate opt-in lanes unless a scoped task promotes a bounded target.

Advanced static analysis is separate from default script lint. The repo-wide
clang-tidy/analyzer and CodeQL lanes are mandatory for non-doc analysis-relevant
PRs. Report skipped or unavailable advanced analysis with its own status.

Benchmark reports must include the command, build configuration,
hardware/context, workload, and output. Use them only for the measured workload.

## Documentation Coupling

When implementation intent changes, update the governing source doc before or
with the tests. When tests expose undocumented intended behavior, either update
the source doc or narrow the test. When a docs-only task intentionally does not
change tests, state that explicitly in the PR evidence.

## Review Expectations

Reject tests that:

- mirror the current implementation instead of documented behavior;
- hide an important failure path;
- fold optional lanes into default results;
- update goldens without explaining intent;
- use benchmark or smoke evidence as correctness evidence;
- weaken redaction, capability, or platform-boundary checks.

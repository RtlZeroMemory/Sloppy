# Testing

Tests are how Sloppy verifies that a behavior change matches the docs. The
contract is short:

1. Test documented intent, not implementation accidents.
2. Move docs, tests, and code together when behavior changes.
3. Cover the negative path — malformed input, cleanup, diagnostics —
   for any contract that can fail.
4. Goldens are semantic contracts, not "current output, frozen".

[testing-inventory.md](testing-inventory.md) is the operational map of
where tests live. This page is the why and how.

## Lanes

Different test lanes verify different parts of the system. Default CI runs the
default lane plus a few mandatory ones; the rest are opt-in.

| Lane                 | Verifies                                              |
| -------------------- | ----------------------------------------------------- |
| Default (non-V8)     | Native runtime, parser, plan, dispatch, route, scope   |
| Compiler / Plan      | `sloppyc` extraction, emitted Plan/bundle, diagnostics |
| V8-gated             | JS handler execution, bridge invariants                |
| Source-input         | `sloppy run <source>` end-to-end                       |
| Package outside-checkout | Built archive runs from a clean directory          |
| Sanitizer            | Linux ASan/UBSan in native PRs; Windows sanitizer lanes on main/schedule/manual/labels |
| libFuzzer seed replay| Deterministic seed replay; default-safe replay in normal tests, instrumented Windows replay on main/schedule/manual/labels |
| Advanced static analysis | CodeQL for analysis-relevant paths; clang-tidy/analyzer on main/schedule/manual/labels |
| Live providers       | PostgreSQL/SQL Server against real services            |
| TestServices         | Experimental Docker-backed PostgreSQL/SQL Server through first-party `TestServices` |
| Stress / torture     | Long-running pressure, races, drains                   |
| Benchmark            | Measurement only; never correctness                    |

Every lane is reported separately. A passing default lane doesn't
imply a passing V8 lane, and a benchmark smoke isn't a performance
claim.

Use only `PASS`, `FAIL`, `SKIPPED`, `UNAVAILABLE`, `DEFERRED`, or `NOT RUN`
when reporting test evidence. Use these lane names in PR evidence when
they apply: default non-V8, compiler/Plan, V8-gated, source-input,
package outside-checkout, platform-specific, dependency-backed,
live-network/live-provider, advanced static analysis, fuzz/property,
stress/torture, sanitizer/memory-safety, and benchmark.

## Where to put a test

```text
unit              behavior of one function/module
integration       behavior across modules, no transport
conformance       end-to-end via CTest fixtures (CLI in, output out)
golden            rendered output (diagnostics, JSON, CLI)
fuzz              corpus + harness for parser/protocol code
fixtures          source/data inputs reused across tests
```

When in doubt, pick the smallest unit that exercises the thing you
care about and don't accidentally test V8/HTTP/the kitchen sink.

For Sloppy app framework behavior, prefer the app test host when the contract
is route matching, request context materialization, middleware order, result
conversion, ProblemDetails, CORS, health checks, or request service cleanup.
Use `sloppy run --once` for compiled artifacts, native dispatch, V8 execution,
generated typed bindings, package layout, and provider bridge behavior.
For native endpoint dispatch metadata, pair compiler Plan assertions with
`sloppy routes --dispatch` / `sloppy doctor --dispatch` CLI coverage, and keep
counters such as route artifact gaps, native no-JS endpoints, and native URL
writers accurate.

The dogfood control-plane coverage deliberately uses both. The app-host test
imports `examples/prealpha-control-plane/src/routes/*.js` and checks bodies,
headers, query/path params, CORS, ProblemDetails, service-scope disposal, and
negative paths. The CTest source-input lanes compile the same project, inspect
the emitted plan/tooling outputs, assert non-V8 diagnostics, and run one
V8-gated synthetic request when V8 is enabled.

## Writing a good unit test

- Name the contract being tested in the test name. `test_route_pattern_int_param_strict_trailing_slash`
  beats `test_route_3`.
- Use the test arena pattern (`SlArena*`) — never leak between tests.
- Assert the actual contract: status, diagnostic code, source location,
  cleanup hook called once. Asserting "function returned an error" is
  almost never enough.
- Pair every "happy path" with at least one negative case — malformed
  input, missing field, wrong order.

## Goldens

Goldens are checked-in expected outputs (Plan JSON, diagnostic
rendering, CLI text). They are semantic contracts, so:

- A change to a golden needs an explicit reason. "Output drifted" is
  not a reason; "we now emit the source span before the hint" is.
- Goldens that include redacted fields must keep redaction. Drop the
  redaction and the test fails by design.
- The PR body says which lane each updated golden covers and what
  intent the change reflects.

The alpha proof suite under `tests/golden/alpha/` adds normalized goldens for
CLI help, compiler artifacts, templates, package manifests, diagnostics,
example coverage, and app-flow integration. See
[golden-tests.md](golden-tests.md) for the update and review workflow.

## Negative paths

Required wherever a contract can reject input or clean up resources:

- malformed Plan / artifacts / config
- invalid request bodies, headers, methods, content types
- partial reads, partial writes
- cancellation mid-flight
- shutdown with in-flight work
- allocation failure (where injectable)
- redaction (assert the secret never appears)
- capability denial (assert a missing capability fails closed)

A test that asserts "function returned `SL_STATUS_ERROR`" without
checking the diagnostic code or the cleanup behavior is incomplete.

## TestServices

`TestServices` is the experimental first-party Docker-backed dependency lane for
PostgreSQL and SQL Server integration tests. Default CI runs the Sloppy
artifact bootstrap contract without starting containers. Real containers
require an explicit gate such as:

```powershell
$env:SLOPPY_TESTSERVICES = "1"
```

Report this lane separately from the existing live-provider lane. A skipped
container lane must say the exact reason: Docker CLI missing, Docker daemon
unreachable, provider bridge unavailable, SQL Server driver unavailable, or
the gate not set. Do not turn those skips into a PostgreSQL or SQL Server
`PASS`.

Use `TestServices` when a test needs to prove dependency behavior through
Docker lifecycle, readiness, migrations, seed data, reset, diagnostics, and
cleanup. Use `TestData` or fake providers for pure app-host logic that does
not need a real database.

## Fuzz, sanitizers, benchmarks

- **Fuzz / property** — deterministic seed replay (default-safe) plus
  optional libFuzzer mutation runs. Seed replay is mandatory for
  parsers and binary formats. Use `tools/windows/fuzz.ps1` or
  `tools/unix/fuzz.sh` for seed replay, selected native mutation targets, and
  JavaScript randomized/property targets. Every randomized run must report the
  seed, target, iteration, and failure artifact or reproduction command.
- **Sanitizers** — Linux ASan/UBSan is the default native PR sanitizer.
  Windows ASan and instrumented libFuzzer seed replay run on `main`,
  schedules, manual dispatch, or explicit labels. Local equivalents:

  ```powershell
  .\tools\windows\dev.ps1 configure -Preset windows-asan
  .\tools\windows\dev.ps1 build -Preset windows-asan
  ctest --preset windows-asan --output-on-failure
  ```

- **Benchmarks** — `sloppy_bench` runs native microbenchmarks, and
  `tools/windows/bench.ps1 -Suite ...` runs the local runtime comparison
  engine, including process-level CPU/memory counters and the `concurrency`
  suite. Output is measurement, not pass/fail. Don't use benchmark smoke as
  correctness evidence; don't claim performance numbers from a smoke run.
  Missing Node, Bun, Deno, or V8-enabled Sloppy executables are reported as
  `UNAVAILABLE` or `SKIPPED`, separate from default gates.
- **Compiler scale** — `tools/windows/bench-compiler.ps1` and
  `tools/unix/bench-compiler.sh` generate deterministic source-input projects
  and measure `sloppyc` compile time, phase timings, memory, and artifact
  sizes. Use `-CompilerProfile release` / `--compiler-profile release` for
  release compiler evidence. Cargo tests also include route/module and
  full-framework scale smoke guards to catch obvious compiler regressions,
  framework-lowering regressions, helper duplication, and source-map/artifact
  growth. See
  [compiler-performance.md](compiler-performance.md).

Logging has focused native stress and benchmark coverage:

```powershell
ctest --test-dir build/windows-dev -R "core.logging.structured|stress.logging.structured" --output-on-failure
build/windows-dev/sloppy_bench.exe --smoke --format json --bench logging.enabled.five_fields
```

Use the stress test for queue pressure, flushing, and shutdown regressions. Use
the benchmark smoke only to check that the benchmark harness still runs.

The test engine wraps the common cross-lane combinations:

```powershell
.\tools\windows\test-engine.ps1 -Tier pr -Out artifacts\test-engine\pr.json
.\tools\windows\test-engine.ps1 -Tier extended -Area fuzz -Seed 12345
```

Use [test-engine.md](test-engine.md) for the full option reference. The
wrapper reports optional missing build presets or tools as unavailable lanes
instead of folding them into a pass.

Default PR-tier fuzz/property coverage includes native corpus replay plus
bounded JavaScript properties for codec, Results/ProblemDetails, time,
HttpClient option validation, workers, logging, and config. Extended and
torture tiers are where larger iteration counts, sanitizer builds, package
archive smoke, V8, providers, and long stress runs belong.

## Conformance and CTest

CTest fixtures under `tests/conformance/` drive the CLI end-to-end —
they invoke `sloppy build` / `sloppy run --once` and assert the
output. They're the closest thing to "what a user sees".

CTest naming convention groups them by area:

```
conformance.foundation.*
conformance.http.*
conformance.transport.*
conformance.transport.http2_*
conformance.sqlite.*
conformance.capability.*
conformance.package.*
smoke.*
benchmark.*
```

When a CI run skips a conformance lane, it's reported as `SKIPPED` /
`UNAVAILABLE`, never folded into the default count.

HTTP/2 changes should include the smallest native protocol lane that proves
the contract: frame/HPACK/session unit tests for parser behavior,
`conformance.transport.http2_h2c` for cleartext prior knowledge,
`conformance.transport.http2_h2c_upgrade` for HTTP/1.1 Upgrade, and
`conformance.transport.http2_tls_alpn` for TLS ALPN selection. The Linux clang
CI lane installs h2spec and runs full h2spec against a live h2c Sloppy
transport server. The same wrapper also reports curl, nghttp, and h2load
smoke lanes, but those are coverage only when the tool is present and the lane
prints `PASS`. Report missing tools, curl builds without HTTP/2, or missing
live `--url` / `-Url` targets as `SKIPPED` or `UNAVAILABLE`. Wrapper existence
by itself is not conformance evidence.

## When tests get rejected at review

Common reasons:

- mirrors the implementation instead of the contract;
- skips the negative path;
- updates a golden without explaining the intent;
- folds an optional lane into the default;
- weakens redaction or capability checks;
- treats benchmark output as correctness.

These aren't style nits — they undermine the regressions tests are
supposed to catch.

## When docs and tests disagree

If a test expects behavior the docs don't describe, either the test is
wrong or the docs are. Pick one, fix it, in the same PR. A test
without doc backing is a hidden contract; a doc without test backing
is wishful thinking.

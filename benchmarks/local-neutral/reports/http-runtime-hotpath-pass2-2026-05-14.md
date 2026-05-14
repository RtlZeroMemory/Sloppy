# HTTP Runtime Hot-Path Pass 2 - 2026-05-14

Local engineering measurements only. These are not public performance claims.

## Summary

This pass kept two reviewable changes:

- `SL_PLAN_REQUEST_BINDING_UNKNOWN` no longer qualifies for the trivial native
  request-validation fast path. Unknown/default bindings now fall back to the
  normal validation path.
- Plain `200 OK` text responses can use a narrow fixed-buffer serializer before
  the generic byte-builder path.

The broader response fast path, V8 context object-shape rewrite, and
`ctx.body.json()` cached body-kind experiment were not kept.

## Correctness Review

`sl_request_validation_bindings_are_trivially_satisfied` now returns `false`
for `SL_PLAN_REQUEST_BINDING_UNKNOWN` and the default case. This avoids silently
accepting a binding kind whose semantics are not known to the fast path.

Direct string route-param wrapper generation remains limited to unconstrained
string route params. Focused compiler tests cover:

- explicit `Route<string>` route binding emits direct `ctx.route["name"]`
- inferred `string` route binding emits direct `ctx.route["name"]`
- constrained numeric route binding keeps the generic coercion path

The runtime route table still owns missing-param handling, URL decoding, and
route-name fallback behavior. This pass did not change those route-table
semantics.

## Experiments

| Track | Experiment | Result | Decision |
| --- | --- | --- | --- |
| Native HTTP/write | Broad fixed-buffer `200 OK` serializer for text and JSON | `health` improved, but `route-param` fell to 44.3k RPS and p99 rose to 7.09 ms | Rejected |
| Native HTTP/write | Narrow text-only `200 OK` serializer | Modest health/static benefit; avoids JSON/V8 response rows | Kept |
| Native HTTP/write | Disabled fast path A/B | Retained as local comparison artifact | Evidence only |
| V8/context | Build route/body objects with `v8::Object::New(... names, values ...)` | Compiled, then V8 dispatch smoke crashed | Reverted |
| Post JSON/body | Cached V8 string compare for `ctx.body.json()` kind check | Passed tests, no clean measured win | Reverted |
| V8 Fast API | Native request-wrapper design and compile-only CFunction probe | Pinned SDK surface compiles; no runtime routeInt kept without safe wrapper/lifetime model | Kept as design/probe only |

## Why The Earlier Response Fast Path Regressed

The current transport already writes a coalesced header/body buffer through
`uv_try_write` before falling back to `uv_write`. The broad fast path therefore
did not reduce write count. It mostly replaced a mature generic fixed-buffer
builder with a second serializer and touched JSON responses that immediately
feed V8/result-conversion rows. The measured regression was consistent with
extra branch/cache disturbance rather than a socket-write win.

The kept serializer is intentionally narrower: status 200, no custom headers,
non-stream, non-suppressed body, and exact `text/plain; charset=utf-8`.

## Focused Benchmark Results

Same-machine k6, Sloppy only, 64 connections, 8s duration, 2s warmup, 2 repeats.
Raw artifacts are retained under `artifacts/benchmarks/`.

### Immediate A/B

`pass2-response-fastpath-disabled-20260514` compared with
`pass2-final-text-only-20260514`.

| Workload | Disabled RPS | Text-only RPS | Delta | Disabled p99 | Text-only p99 | RSS |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `health` @ 64 | 58,792 | 59,905 | +1.9% | 9.88 ms | 5.48 ms | 87.6 -> 87.5 MiB |
| `json-small` @ 64 | 58,053 | 60,958 | +5.0% | 4.73 ms | 4.09 ms | 87.6 -> 90.1 MiB |
| `route-param` @ 64 | 48,082 | 48,554 | +1.0% | 3.33 ms | 3.26 ms | 96.5 -> 96.3 MiB |
| `post-json-validated` @ 64 | 32,523 | 30,599 | -5.9% | 3.05 ms | 3.42 ms | 97.4 -> 100.0 MiB |
| `mixed-realistic` @ 64 | 48,119 | 48,480 | +0.8% | 5.12 ms | 3.06 ms | 100.4 -> 100.4 MiB |

The text fast path only applies to `health`; movement in JSON/V8 rows should be
treated as local-run noise unless reproduced in a clean longer run.

### Against First Kept Pass

| Workload | Reference RPS | Pass 2 RPS | Delta | Reference p99 | Pass 2 p99 |
| --- | ---: | ---: | ---: | ---: | ---: |
| `health` @ 64 | 57,627 | 59,905 | +4.0% | 6.26 ms | 5.48 ms |
| `json-small` @ 64 | 59,439 | 60,958 | +2.6% | 5.23 ms | 4.09 ms |
| `route-param` @ 64 | 48,706 | 48,554 | -0.3% | 2.59 ms | 3.26 ms |
| `post-json-validated` @ 64 | 31,399 | 30,599 | -2.5% | 2.74 ms | 3.42 ms |
| `mixed-realistic` @ 64 | 49,076 | 48,480 | -1.2% | 2.61 ms | 3.06 ms |

The pass raises the static text row modestly but does not materially advance
the route-param or validated POST ceilings. Those remain the next bottlenecks.

## Fast API Findings

See `benchmarks/local-neutral/reports/v8-fast-api-wrapper-design-2026-05-14.md`.

Short version:

- the pinned V8 SDK has `v8-fast-api-calls.h`, `v8::CFunction::Make`,
  `v8::CFunctionBuilder`, `FastApiCallbackOptions`,
  `FunctionTemplate::New(..., const CFunction*)`, and
  `NewWithCFunctionOverloads`
- this pinned SDK does not expose `options.fallback` or
  `CFunction::MakeWithFallbackSupport`
- the current HTTP context is a plain JS object, so a safe Fast API route-param
  accessor needs a native wrapper with internal fields and request lifetime
  checks before runtime wiring
- a compile-only CFunction probe was added to the V8 CMake lane

No runtime Fast API path was kept.

## Validation

| Command | Result |
| --- | --- |
| `tools/windows/dev.ps1 build -Preset windows-relwithdebinfo` | PASS |
| `ctest --test-dir build/windows-relwithdebinfo -C RelWithDebInfo -R "^(core\.request_validation|core\.http\.response|core\.http\.dispatch|conformance\.http\.default_dispatch|engine\.v8\.smoke|conformance\.v8\.runtime_bridge|http\.dispatch\.execution|conformance\.v8\.http_dispatch_execution|benchmarks\.local_neutral\.contract)$" --output-on-failure` | PASS |
| `ctest --test-dir build/windows-relwithdebinfo -C RelWithDebInfo -R "^alpha\.golden\.compiler\." --output-on-failure` | PASS |
| `tests/scripts/test_local_neutral_benchmark_contract.ps1 -RepoRoot .` | PASS |
| `node benchmarks/local-neutral/scripts/run.mjs --check-tools --json` | PASS |
| `node benchmarks/local-neutral/scripts/run.mjs --tool k6 --runtime sloppy --workload health,json-small,route-param,post-json-validated,mixed-realistic --connections 64 --duration 8s --warmup 2s --repeats 2 --claim-mode local --base-port 43400 --out artifacts/benchmarks/pass2-final-text-only-20260514 --json` | PASS |
| `git diff --check` | PASS |
| `node tests/contracts/runner/contract-runner.mjs --area http --tier pr` | UNAVAILABLE: runner path is absent in this checkout |
| `node tests/contracts/runner/contract-runner.mjs --area all --tier pr` | UNAVAILABLE: runner path is absent in this checkout |
| `tools/windows/test-engine.ps1 -Area v8 -Tier pr` | FAIL: V8 configure/build passed, broad suite then failed unrelated stale alpha/template/example/source-input lanes; focused V8 lanes above passed |
| `cargo test --manifest-path compiler/Cargo.toml framework_runtime --release --quiet` | FAIL: direct Cargo link cannot find `msvcrt.lib` in this shell; CMake compiler goldens passed |

## Remaining Bottlenecks

- Static HTTP: the transport is already coalescing response bytes, so the next
  native win likely needs parser/read scheduling, connection reuse/backpressure
  profiling, or lower-level write scheduling counters rather than another
  duplicate serializer.
- Route/context: `route-param` still needs a deeper route-param materialization
  change or a native wrapper that avoids building `ctx.route` for generated
  slot access.
- Validated POST: the likely work remains body facade creation, JSON parse,
  success-path validation allocation, and result conversion.
- Fast API: the next safe step is the native request wrapper plus counters for
  fast calls, slow calls, and unsupported/sentinel fallback paths.

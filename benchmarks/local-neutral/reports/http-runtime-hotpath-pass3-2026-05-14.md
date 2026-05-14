# HTTP Runtime Hot-Path Pass 3 - 2026-05-14

Local engineering measurements only. These are not public performance claims.

## Summary

Pass 3 kept one measured optimization: typed `RequestContext` handlers that use
`ctx.request.json(...)` now advertise a narrower internal binding,
`request.body.json`. Native dispatch still makes JSON body data available to the
request facade, but V8 no longer has to construct and publish the richer
`ctx.body` facade for this path.

The route-slot/native-wrapper prototype was measured and reverted.

## POST Fast-Path Confirmation

The pass-2 text-only response fast path did not reproduce a POST regression in a
longer five-repeat confirmation. The fast path was therefore kept.

Artifacts:

- `artifacts/benchmarks/pass3-post-confirm-fast-enabled-20260514`
- `artifacts/benchmarks/pass3-post-confirm-fast-disabled-20260514`

| Variant | Workload | Conn | Median RPS | p95 | p99 | Peak RSS | Peak private | Errors |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| fast path enabled | `post-json-validated` | 64 | 27,516 | 2.10 ms | 3.22 ms | 97.3 MiB | 113.9 MiB | 0 |
| fast path disabled | `post-json-validated` | 64 | 27,329 | 2.14 ms | 3.69 ms | 97.4 MiB | 114.1 MiB | 0 |

The enabled run was slightly faster and lower-p99 in this confirmation. The
earlier pass-2 POST dip is treated as local-run noise.

## Route/Context Wrapper Findings

The route-slot prototype tried to avoid full `ctx.route` materialization by
emitting generated slot access through an internal helper and storing compact
route slot metadata on the context. It compiled after adding the missing V8
external reference for snapshot startup and passed the focused V8/http lanes.

It was not kept. The measured route/mixed result was lower than the pass-2
reference and used much more memory:

| Artifact | Workload | Conn | Median RPS | p99 | Peak RSS | Peak private | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| `pass3-route-slots-rerun-20260514` | `route-param` | 64 | 47,409 | 3.00 ms | 213.7 MiB | 230.0 MiB | Reverted |
| `pass3-route-slots-rerun-20260514` | `mixed-realistic` | 64 | 47,323 | 3.08 ms | 210.7 MiB | 226.4 MiB | Reverted |

The failed first route-slot run is retained at
`artifacts/benchmarks/pass3-route-slots-20260514`. It exposed a missing
generated helper binding for one emit path and returned 500s before the rerun.

The useful architectural finding is that a route Fast API lane still needs a
true native request wrapper with internal fields and request lifetime
invalidation. A JS helper layered over ordinary objects did not remove enough
work and made memory worse.

## Body/Validation Findings

The kept POST optimization separates request body availability from public body
facade materialization:

- compiler metadata recognizes `ctx.request.json(...)` as `request.body.json`
- dispatch sets `needs_request` and `needs_body` without `needs_body_facade`
- V8 attaches body bytes/kind/cached JSON to `ctx.request` private slots
- V8 creates `ctx.body` only when the plan actually needs the body facade
- focused V8 smoke coverage verifies `ctx.request.json()` works while
  `bodyFacadeMaterialized` remains zero

This preserves public behavior for direct `ctx.body.json()` and typed
`Body<T>` wrappers, which still request the full body facade.

## Focused Benchmark Results

Final pass-3 focused run: same-machine k6, Sloppy only, 64 connections, 15s
duration, 3s warmup, 5 repeats. Raw artifacts:
`artifacts/benchmarks/pass3-final-focused-20260514`.

The pass-2 comparison row below uses `pass2-final-focused-20260514`, which was a
shorter 8s/2s/3-repeat focused run. Treat deltas as engineering evidence, not a
public comparison.

| Workload | Pass 2 median RPS | Pass 3 median RPS | Delta | Pass 2 p99 | Pass 3 p99 | Pass 3 RSS | Pass 3 private |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `route-param` @ 64 | 45,053 | 43,228 | -4.0% | 3.19 ms | 3.12 ms | 96.4 MiB | 113.8 MiB |
| `post-json-validated` @ 64 | 30,918 | 34,420 | +11.3% | 3.04 ms | 3.58 ms | 97.2 MiB | 113.9 MiB |
| `mixed-realistic` @ 64 | 44,013 | 45,228 | +2.8% | 3.14 ms | 4.07 ms | 97.6 MiB | 114.4 MiB |

An isolated body-path run immediately before the final run measured
`post-json-validated` at 35,990 median RPS and `mixed-realistic` at 45,948
median RPS in `artifacts/benchmarks/pass3-body-request-json-20260514`.

## Kept And Reverted

| Track | Experiment | Result | Decision |
| --- | --- | --- | --- |
| POST confirmation | Disable pass-2 text response path and compare five repeats | Regression did not repeat | Keep pass-2 fast path |
| Route/context | Generated route slot helper over JS context/private values | Correct after fixes, but RPS did not beat pass 2 and memory roughly doubled | Reverted |
| Body/validation | Request-body JSON binding without public `ctx.body` facade | `post-json-validated` improved more than 10% in focused runs; memory neutral-to-better | Kept |

## Validation

| Command | Result |
| --- | --- |
| `tools/windows/dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8` | PASS |
| `tools/windows/dev.ps1 build -Preset windows-relwithdebinfo` | PASS |
| `ctest --test-dir build/windows-relwithdebinfo -C RelWithDebInfo -R "^(core\.request_validation\|core\.http\.dispatch\|engine\.v8\.smoke\|conformance\.v8\.runtime_bridge\|http\.dispatch\.execution\|conformance\.v8\.http_dispatch_execution\|benchmarks\.local_neutral\.contract)$" --output-on-failure` | PASS |
| `ctest --test-dir build/windows-relwithdebinfo -C RelWithDebInfo -R "^alpha\.golden\.compiler\." --output-on-failure` | PASS |
| `tests/scripts/test_local_neutral_benchmark_contract.ps1 -RepoRoot .` | PASS |
| `node benchmarks/local-neutral/scripts/run.mjs --check-tools --json` | PASS |
| `node benchmarks/local-neutral/scripts/run.mjs --tool k6 --runtime sloppy --workload post-json-validated --connections 64 --duration 20s --warmup 3s --repeats 5 --claim-mode local --base-port 44000 --out artifacts/benchmarks/pass3-post-confirm-fast-enabled-20260514 --json` | PASS |
| `node benchmarks/local-neutral/scripts/run.mjs --tool k6 --runtime sloppy --workload post-json-validated --connections 64 --duration 20s --warmup 3s --repeats 5 --claim-mode local --base-port 44100 --out artifacts/benchmarks/pass3-post-confirm-fast-disabled-20260514 --json` | PASS |
| `node benchmarks/local-neutral/scripts/run.mjs --tool k6 --runtime sloppy --workload route-param,mixed-realistic --connections 64 --duration 15s --warmup 3s --repeats 5 --claim-mode local --base-port 44300 --out artifacts/benchmarks/pass3-route-slots-rerun-20260514 --json` | PASS, experiment reverted |
| `node benchmarks/local-neutral/scripts/run.mjs --tool k6 --runtime sloppy --workload post-json-validated,mixed-realistic --connections 64 --duration 15s --warmup 3s --repeats 5 --claim-mode local --base-port 44400 --out artifacts/benchmarks/pass3-body-request-json-20260514 --json` | PASS |
| `node benchmarks/local-neutral/scripts/run.mjs --tool k6 --runtime sloppy --workload route-param,post-json-validated,mixed-realistic --connections 64 --duration 15s --warmup 3s --repeats 5 --claim-mode local --base-port 44600 --out artifacts/benchmarks/pass3-final-focused-20260514 --json` | PASS |
| `git diff --check` | PASS |
| `node tests/contracts/runner/contract-runner.mjs --area http --tier pr` | UNAVAILABLE |

Note: the HTTP contract runner path was absent in this checkout.

During development, the first focused V8/native CTest run failed with a segfault
after the new `needs_body_facade` bit was added. The failure exposed stale
test-helper state where `needs_body` could be cleared while the new facade bit
remained set. V8 now requires both bits before creating or publishing `ctx.body`,
and the final focused lanes pass.

## Remaining Bottlenecks

- `route-param` still needs the real native wrapper/lifetime model before a
  Fast API route slot experiment is worth keeping.
- `mixed-realistic` is sensitive to local scheduling and still reflects both
  route and POST work; use it as a guardrail rather than the primary signal.
- The POST success path is now past the old 30k ceiling, but still pays for V8
  `JSON.parse` and response result conversion.

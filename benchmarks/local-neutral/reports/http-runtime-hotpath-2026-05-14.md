# HTTP Runtime Hot-Path Profiling Notes - 2026-05-14

These are local engineering measurements for the performance branch. They are not
public performance claims.

## Summary

Checkpoint baseline is `55e725da` (`bench: checkpoint current k6 harness`).
The kept changes reduce JS wrapper work for simple typed framework handlers and
skip native request-validation setup when all bindings are already trivially
satisfied string/context/injection bindings.

## Profiling Signal

Opt-in HTTP/JSON profiling showed three ceilings:

| Row | Main observed ceiling |
| --- | --- |
| `health` / `json-small` @ 64 | native HTTP/parser/write/socket path; no V8 handler work |
| `route-param` / `mixed-realistic` @ 64 | V8 handler entry, context construction, route param materialization, result conversion, validation |
| `post-json-validated` @ 64 | V8 handler execution, context/body facade work, JSON parse, schema validation, response conversion |

Representative route-param profile costs included socket write scheduling,
V8 handler execution, handler invocation, result conversion, context
construction, route param materialization, JSON stringify, request validation,
and HTTP parse. POST profiles were dominated by V8 handler execution,
context/body materialization, JSON body handling, validation, and response
conversion.

## Experiments

| Experiment | Result | Decision |
| --- | --- | --- |
| Direct wrapper args for `ctx`, `ctx.body.json()`, string route params | Improved route/mixed/post latency and avoided generic binding helper for simple cases | Kept |
| Compile typed handler once in generated wrapper closure | Helped mixed routes and reduced per-request function construction | Kept |
| Hoist generic helper descriptors only | No material win in focused rows | Reverted |
| Native trivial request-validation fast path | Improved route/mixed p99 and kept RPS neutral-positive | Kept |
| Narrow response writer 200 OK fast path | Large regression in health/json-small | Reverted |
| V8 Fast API route-param lane | SDK supports Fast API surface, but current HTTP ctx is a plain JS object without a native internal-field handle; no safe runtime fast callback was kept | Not kept |

## Clean Focused Comparison

The clean focused baseline and kept-experiment rows used k6, Sloppy only,
64 connections, 8s duration, 2s warmup, and 3 repeats. Raw focused artifact
directories were removed after a later local ENOSPC incident; the summarized
values below are retained as the review evidence. Re-run on idle hardware before
using these in PR evidence.

| Workload | Baseline RPS | Kept RPS | Delta | Baseline p95 | Kept p95 | Baseline p99 | Kept p99 | RSS |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `health` @ 64 | 57,627 | not retested clean | n/a | 1.51 ms | n/a | 6.26 ms | n/a | 87.5 MiB baseline |
| `json-small` @ 64 | 59,439 | not retested clean | n/a | 1.51 ms | n/a | 5.23 ms | n/a | 87.5 MiB baseline |
| `route-param` @ 64 | 47,327 | 48,706 | +2.9% | 1.56 ms | 1.54 ms | 3.61 ms | 2.59 ms | 96.7 -> 96.5 MiB |
| `post-json-validated` @ 64 | 31,108 | 31,399 | +0.9% | 2.06 ms | 2.02 ms | 3.15 ms | 2.74 ms | 97.2 -> 100.3 MiB |
| `mixed-realistic` @ 64 | 47,375 | 49,076 | +3.6% | 1.60 ms | 1.55 ms | 3.73 ms | 2.61 ms | 97.6 -> 100.4 MiB |

The kept changes are justified mostly by p99 reductions with neutral-positive
RPS on the affected V8/context/validation rows. They do not claim to raise the
native static HTTP ceiling.

## Fresh Smoke Rerun

After reattaching the branch and rebuilding with V8 enabled, a fresh 2-repeat
k6 smoke run passed with 0 errors and 0 non-2xx rows:

| Workload | Median RPS | p95 | p99 | RSS | Private |
| --- | ---: | ---: | ---: | ---: | ---: |
| `health` @ 64 | 48,179 | 1.93 ms | 18.38 ms | 87.6 MiB | 109.8 MiB |
| `json-small` @ 64 | 38,897 | 3.28 ms | 21.31 ms | 87.6 MiB | 108.8 MiB |
| `route-param` @ 64 | 34,271 | 2.67 ms | 16.43 ms | 96.5 MiB | 113.8 MiB |
| `post-json-validated` @ 64 | 23,278 | 3.00 ms | 8.17 ms | 97.4 MiB | 114.0 MiB |
| `mixed-realistic` @ 64 | 35,023 | 2.42 ms | 18.20 ms | 97.8 MiB | 114.2 MiB |

This rerun is smoke evidence only: the checkout was dirty and the machine still
had many unrelated shell processes plus one unrelated Sloppy process. It proves
the harness still runs and returns correct responses; it is not a clean
before/after comparison.

## V8 Fast API Findings

Pinned SDK: `V:/Slop/.sdeps/v8/windows-x64`, revision
`7221f49fdb6c89cce6be08005732ebcab3c45b38`, monolithic release x64 with pointer
compression and sandbox enabled.

Header investigation found:

| API surface | Finding |
| --- | --- |
| `v8-fast-api-calls.h` | Present |
| `v8::CFunction::Make` | Present |
| `v8::CFunctionBuilder` | Present |
| `v8::FastApiCallbackOptions` | Present, with `isolate` and `data` fields |
| `v8::FunctionTemplate::New` CFunction overload | Present |
| `v8::FunctionTemplate::NewWithCFunctionOverloads` | Present |
| `FastOneByteString` | Present |
| `MakeWithFallbackSupport` / `options.fallback` | Mentioned in comments, not exposed as fields/functions in this pinned header |

The first plausible target remains route integer param access, but Sloppy's
current HTTP context is built as plain `v8::Object::New(isolate)` in
`src/engine/v8/http_bridge.cc` and route params are materialized into a normal
JS `ctx.route` object. A safe Fast API `routeInt(ctx, slot)` needs a native
context wrapper/template with internal fields and a lifetime story for the
request context. Without fallback support in this SDK shape, the callback also
cannot use the newer explicit fallback flag pattern described in comments.

A throwaway compile probe under ignored artifacts reached the pinned headers
but standalone compilation needs the SDK's libc++/MSVC configuration from the
CMake lane. No Fast API runtime code was kept because it did not yet fire and
would have required unsafe context-wrapper work without measured benefit.

## Validation

| Command | Result |
| --- | --- |
| `tools/windows/dev.ps1 configure -Preset windows-relwithdebinfo` | PASS |
| `tools/windows/dev.ps1 build -Preset windows-relwithdebinfo` | PASS |
| `tools/windows/dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8` | PASS |
| `tools/windows/dev.ps1 build -Preset windows-relwithdebinfo` | PASS |
| `ctest --test-dir build/windows-relwithdebinfo -C RelWithDebInfo -R "^(core\\.request_validation\|core\\.http\\.dispatch\|http_dispatch_execution)$" --output-on-failure` | PASS |
| `ctest --test-dir build/windows-relwithdebinfo -C RelWithDebInfo -R "^alpha\\.golden\\.compiler\\." --output-on-failure` | PASS |
| `ctest --test-dir build/windows-relwithdebinfo -C RelWithDebInfo -R "^benchmarks\\.local_neutral\\.contract$" --output-on-failure` | PASS |
| `ctest --test-dir build/windows-relwithdebinfo -C RelWithDebInfo -R "^(engine\\.v8\\.smoke\|engine_v8_smoke\|http_dispatch_execution)$" --output-on-failure` | PASS |
| `tests/scripts/test_local_neutral_benchmark_contract.ps1 -RepoRoot .` | PASS |
| `node benchmarks/local-neutral/scripts/run.mjs --check-tools --json` | PASS |
| `node benchmarks/local-neutral/scripts/run.mjs --tool k6 --runtime sloppy --workload health,json-small,route-param,post-json-validated,mixed-realistic --connections 64 --duration 8s --warmup 2s --repeats 2 --claim-mode local --out artifacts/benchmarks/focused-kept-v8-rerun-20260514 --json` | PASS |
| `tools/windows/test-engine.ps1 -Area v8 -Tier pr` | UNAVAILABLE |
| `tools/windows/dev.ps1 test -Preset windows-relwithdebinfo` | FAIL |
| `cargo test --manifest-path compiler/Cargo.toml framework_runtime --release --quiet` | FAIL |
| `git diff --check` | PASS |

Notes: `tools/windows/test-engine.ps1 -Area v8 -Tier pr` was unavailable because
`SLOPPY_V8_ROOT` was not set. The broad `tools/windows/dev.ps1 test` lane failed
on alpha/template/example goldens outside this slice; focused compiler goldens
passed. Direct Cargo testing failed because this shell could not find
`msvcrt.lib`; CMake-backed lanes covered the compiler changes.

## Public Readiness

This is not ready for public claims. The harness now captures neutral k6 rows,
p95/p99, RSS/private memory, CPU, errors, and non-2xx counts, but the kept
optimization evidence is still local same-machine data. Before public numbers,
run a clean, idle, committed checkout with all selected comparators, fixed power
settings, enough repeats, and retained raw artifacts.

## Remaining Bottlenecks

The next likely wins are context construction, route param materialization,
V8 call/result conversion, and response write scheduling. Fast API should stay
as an experiment lane, but only after adding a safe native request-context
wrapper and counters for fast calls, slow calls, and unsupported/fallback paths.

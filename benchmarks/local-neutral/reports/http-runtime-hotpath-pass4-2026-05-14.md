# HTTP Runtime Hot-Path Pass 4 - 2026-05-14

Local engineering measurements only. These are not public performance claims.

## Route-Param A/B

The pass-3 `ctx.request.json(...)` body-facade optimization does not reproduce
the earlier `route-param` regression. The A/B below used the same settings for
both rows: k6, Sloppy only, 64 connections, 15s duration, 3s warmup, 5 repeats.

The first two disabled attempts hit stale port collisions and are retained as
failed artifacts:

- `artifacts/benchmarks/pass4-route-param-body-json-disabled-20260514`
- `artifacts/benchmarks/pass4-route-param-body-json-disabled-rerun-20260514`

The successful A/B artifacts are:

- `artifacts/benchmarks/pass4-route-param-body-json-disabled-rerun2-20260514`
- `artifacts/benchmarks/pass4-route-param-body-json-current-20260514`

| Variant | Median RPS | Mean RPS | p95 | p99 | Peak RSS | Peak private | Errors | Non-2xx |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| body-json optimization disabled | 41,512 | 41,511 | 1.88 ms | 4.01 ms | 96.4 MiB | 113.7 MiB | 0 | 0 |
| pass-3 current | 42,026 | 41,507 | 1.77 ms | 3.81 ms | 99.0 MiB | 116.3 MiB | 0 | 0 |

The current build was slightly better on median RPS and p95/p99 in this A/B.
The earlier route-param drop is treated as local-run noise, not a regression
from the body-json optimization.

The pass-3 POST JSON improvement repeated: `post-json-validated @ 64` measured
35,990 median RPS in `pass3-body-request-json-20260514` and 35,234 median RPS
in the clean pass-4 Sloppy-only matrix below, compared with the earlier pass-2
focused row at 30,918 median RPS.

## Clean Sloppy-Only Matrix

Artifact: `artifacts/benchmarks/pass4-clean-sloppy-focused-20260514`.

Settings: k6, Sloppy only, 64 connections, 15s duration, 3s warmup, 5 repeats.

| Workload | Median RPS | Mean RPS | p95 | p99 | Peak RSS | Peak private | Errors | Non-2xx |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `health` | 53,311 | 53,980 | 1.56 ms | 4.51 ms | 87.5 MiB | 108.7 MiB | 0 | 0 |
| `json-small` | 53,642 | 53,456 | 1.54 ms | 3.66 ms | 87.5 MiB | 108.7 MiB | 0 | 0 |
| `route-param` | 41,513 | 41,355 | 1.83 ms | 3.51 ms | 96.4 MiB | 113.7 MiB | 0 | 0 |
| `post-json-validated` | 35,234 | 34,925 | 2.02 ms | 3.59 ms | 99.7 MiB | 116.5 MiB | 0 | 0 |
| `mixed-realistic` | 45,005 | 44,629 | 1.62 ms | 4.55 ms | 97.6 MiB | 114.4 MiB | 0 | 0 |

## Local Runtime Comparison

Artifact: `artifacts/benchmarks/pass4-all-runtimes-focused-20260514`.

Settings: k6, Sloppy/Node/Bun/Deno, 64 connections, 15s duration, 3s warmup,
5 repeats. This is a same-machine local comparison. It should not be used as a
public performance claim.

| Workload | Sloppy RPS | Node RPS | Bun RPS | Deno RPS | Highest median RPS in this run |
| --- | ---: | ---: | ---: | ---: | --- |
| `health` | 56,558 | 45,567 | 60,145 | 54,743 | Bun |
| `json-small` | 54,445 | 45,603 | 57,808 | 54,279 | Bun |
| `route-param` | 42,794 | 45,365 | 47,919 | 50,798 | Deno |
| `post-json-validated` | 34,822 | 21,734 | 49,903 | 44,199 | Bun |
| `mixed-realistic` | 44,945 | 38,261 | 54,321 | 50,482 | Bun |

| Workload | Sloppy vs Node | Sloppy vs Bun | Sloppy vs Deno |
| --- | ---: | ---: | ---: |
| `health` | +24.1% | -6.0% | +3.3% |
| `json-small` | +19.4% | -5.8% | +0.3% |
| `route-param` | -5.7% | -10.7% | -15.8% |
| `post-json-validated` | +60.2% | -30.2% | -21.2% |
| `mixed-realistic` | +17.5% | -17.3% | -11.0% |

Sloppy is competitive on static rows and ahead of Node in this run for
health/json/post/mixed, but route-param remains behind all comparator runtimes
and validated POST remains behind Bun/Deno. The next measured work should focus
on route/context materialization, validated body success-path allocation, result
conversion, and a safe native request wrapper for future V8 Fast API work.

## Validation

| Lane | Command or artifact | Result |
| --- | --- | --- |
| Diff hygiene | `git diff --check` | PASS |
| V8 configure | `tools/windows/dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8` | PASS |
| V8 build | `tools/windows/dev.ps1 build -Preset windows-relwithdebinfo` | PASS |
| Focused native/V8/http CTest | focused CTest regex listed below | PASS |
| Compiler alpha goldens | `ctest --test-dir build/windows-relwithdebinfo -C RelWithDebInfo -R "^alpha\.golden\.compiler\." --output-on-failure` | PASS |
| Local-neutral contract | `tests/scripts/test_local_neutral_benchmark_contract.ps1 -RepoRoot .` | PASS |
| Tool smoke | `node benchmarks/local-neutral/scripts/run.mjs --check-tools --json` | PASS |
| K6 smoke | `artifacts/benchmarks/pass4-k6-smoke-20260514` | PASS |
| Route-param disabled A/B | `artifacts/benchmarks/pass4-route-param-body-json-disabled-rerun2-20260514` | PASS |
| Route-param current A/B | `artifacts/benchmarks/pass4-route-param-body-json-current-20260514` | PASS |
| Clean Sloppy-only focused matrix | `artifacts/benchmarks/pass4-clean-sloppy-focused-20260514` | PASS |
| Local all-runtime comparison | `artifacts/benchmarks/pass4-all-runtimes-focused-20260514` | PASS |
| HTTP contract runner | `node tests/contracts/runner/contract-runner.mjs --area http --tier pr` | UNAVAILABLE |

Note: this checkout lacks `tests/contracts/runner/contract-runner.mjs`.

Focused CTest command:

```powershell
ctest --test-dir build/windows-relwithdebinfo -C RelWithDebInfo -R "^(core\.request_validation|core\.http\.dispatch|engine\.v8\.smoke|conformance\.v8\.runtime_bridge|http\.dispatch\.execution|conformance\.v8\.http_dispatch_execution|benchmarks\.local_neutral\.contract)$" --output-on-failure
```

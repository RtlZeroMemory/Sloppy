# Local Neutral Runtime Benchmarks

This harness compares Sloppy, Node.js, Bun, and Deno on one local machine with
an external load generator. It is for engineering review: regression checks,
branch comparisons, and finding bottlenecks before alpha releases.

These are local engineering measurements, not public performance claims.
Single-machine benchmark results can be affected by CPU scheduling, power mode,
thermal behavior, loopback networking, tool choice, and background processes.

## Why This Exists

Sloppy also has internal benchmark runners that use Sloppy's own Program Mode
HTTP client. Those are useful for testing Sloppy internals, but they are not a
neutral client. This harness keeps the load generator outside Sloppy so that
runtime comparison rows are easier to review.

The harness does not publish results, rank runtimes, or make CI pass or fail on
throughput. It creates artifacts that a reviewer can inspect. Public-claim mode
is deliberately stricter: it marks the report `NOT_PUBLIC_READY` unless the run
has complete comparator rows, p95/p99 latency, process resource samples, a
stress-sized matrix, a clean checkout, and an explicit separate-machine load
topology.

## Install A Load Generator

Install at least one of these tools yourself. The harness detects them and uses
the first available tool in this order:

1. `oha`
2. `wrk`
3. `k6`
4. `vegeta`

You can select one explicitly:

```sh
node benchmarks/local-neutral/scripts/run.mjs --tool k6
```

The harness does not vendor, install, or download these tools.

`k6` is the preferred public-candidate tool because its summary export gives
stable p95/p99 latency fields and request-failure counters. `oha` is the
preferred stress tool when you want a simple high-pressure HTTP generator with
JSON output. `wrk` and `vegeta` remain supported for local engineering runs,
but their adapter output may be less complete for public-candidate reporting.

## Quick Run

Build Sloppy with V8 support first, or pass a V8-enabled binary with
`--sloppy-bin`.

```sh
node benchmarks/local-neutral/scripts/run.mjs --runtime sloppy,node --workload health --connections 1 --duration 3s --warmup 1s --repeats 1
```

Windows wrapper:

```powershell
.\tools\windows\bench-local-neutral.ps1 -Runtime sloppy,node -Workload health -Connections 1 -Duration 3s -Warmup 1s -Repeats 1
```

Default quick mode runs `health`, `json-small`, and `route-param` with
connections `1,16`, three repeats, a 15-second measured window, and a 5-second
warmup.

## Presets

```sh
node benchmarks/local-neutral/scripts/run.mjs --preset quick
node benchmarks/local-neutral/scripts/run.mjs --preset alpha
node benchmarks/local-neutral/scripts/run.mjs --preset full
node benchmarks/local-neutral/scripts/run.mjs --preset stress --tool oha
node benchmarks/local-neutral/scripts/run.mjs --preset public-candidate --tool k6 --runtime all --claim-mode public-candidate --load-host-kind separate-machine
```

- `quick`: short local sanity run for the core GET workloads.
- `alpha`: all required workloads with connections `1,16,64`.
- `full`: larger local matrix for branch-to-branch comparison.
- `stress`: high-concurrency local pressure run for soak/stability signals.
- `public-candidate`: long, repeated, resource-sampled matrix for reports that
  may later support public claims after topology and environment review.

Use `--runtime sloppy,node,bun,deno` or `--runtime all`. Missing optional
runtimes are reported as unavailable unless you requested only that runtime.

## Workloads

The required workloads are defined in `workloads/` and implemented by each
server under `servers/`:

- `health`: `GET /health`, plain text `ok`.
- `json-small`: `GET /json-small`, small stable JSON.
- `route-param`: `GET /users/123`, one route parameter.
- `post-json-validated`: `POST /users`, JSON body validation.
- `auth-api-key`: `GET /private`, `x-api-key` header check.
- `static-small`: `GET /public/hello.txt`, small static text.
- `mixed-realistic`: weighted mix of the required workloads.

Weighted mixes require adapter support. The `k6` adapter supports
`mixed-realistic`. Other adapters skip it and explain why instead of faking a
mix.

The Sloppy `auth-api-key` row is currently skipped by the harness because the
source-input fixture needs config-backed `Auth.apiKey` handoff that is not
reliable enough for an apples-to-apples benchmark row yet. The harness reports
that skip explicitly rather than serving an unprotected Sloppy endpoint.

## Outputs

Each run writes to `artifacts/benchmarks/local-neutral/<timestamp>/` unless you
pass `--out`:

- `environment.json`: host, runtime, tool, and git metadata.
- `matrix.json`: effective workload and timing matrix.
- `results.json`: raw normalized repeat rows.
- `summary.json`: aggregate rows and comparison metadata.
- `report.md`: human-readable report.
- `report.csv`: spreadsheet-friendly summary.
- `raw/`: server stdout and stderr logs.
- `raw/**/resources-repeat-*.json`: process resource samples for each measured
  repeat when resource sampling is enabled.

Reports include RPS, p50/p95/p99 latency when the selected tool provides those
values, errors, non-2xx counts when available, server CPU and memory samples,
and explicit skipped rows.

Resource sampling is best-effort process telemetry. On Windows it samples
`Get-Process`; on Unix-like systems it samples `ps`. It is not a profiler and
does not replace OS-level tools such as Windows Performance Recorder, `perf`, or
eBPF. Disable it with `--no-resources` only for smoke/debugging.

## How To Read Results

Compare rows only when the workload, connection count, tool, machine, and build
configuration match. Keep the JSON artifact with any discussion of numbers.

If Sloppy has the highest RPS in a row, say "Sloppy had higher RPS in this run."
Do not say "Sloppy is faster than Node/Bun/Deno." If Sloppy loses, report that
directly.

Same-machine loopback benchmarks are good for local regression work. Public
claims need more: neutral tools, repeated runs, full environment disclosure,
resource evidence, stress evidence, and a separate load-generator machine.
`--claim-mode public-candidate` makes these gaps visible in the report instead
of letting a local run look publication-ready by accident.

For public-candidate runs, keep the whole artifact directory. The Markdown
report is reviewable, but `results.json`, `summary.json`,
`environment.json`, and raw resource samples are the source evidence.

## Relationship To The Existing Realistic Runner

`benchmarks/realistic/` remains useful for Sloppy-internal feedback because it
can exercise Sloppy's own Program Mode client and capture Sloppy-specific
metadata. Use `benchmarks/local-neutral/` when the client must be outside
Sloppy.

The two suites answer different questions. Do not mix their results in one
table unless the report says exactly how each row was generated.

## Add A Workload

1. Add a workload JSON file under `workloads/`.
2. Add the same endpoint behavior to every runtime server under `servers/`.
3. Make sure status codes, response bodies, headers, and request bodies match.
4. If a load tool cannot support the workload shape, make the adapter skip it
   with a clear reason.
5. Run a tiny local smoke before discussing numbers.

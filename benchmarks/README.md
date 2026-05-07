# Sloppy Benchmarks

Sloppy benchmarks are manual performance-validation tools. They are not correctness tests,
release gates, marketing claims, or comparisons with other runtimes.

Run the smoke/list checks locally:

```powershell
.\tools\windows\bench.ps1 -List
.\tools\windows\bench.ps1 -Smoke -Json
```

Run a measured local benchmark from a Release build:

```powershell
.\tools\windows\bench.ps1 -Configuration Release
.\tools\windows\bench.ps1 -Configuration Release -Json > .\benchmarks-local.json
```

Debug numbers are not meaningful. Smoke mode only proves the harness starts and each default
benchmark path can execute a tiny iteration count.

## Methodology

The harness uses Sloppy's platform monotonic clock abstraction and reports elapsed
nanoseconds plus nanoseconds per operation. Each benchmark has warmup iterations and
measured iterations. The benchmark loops accumulate a checksum so the compiler cannot
remove the exercised code path.

Route matcher benchmarks parse patterns before timing except for `route.parse.multi_param`,
which intentionally measures parse cost. Match arenas are reset consistently inside the
loop.

Handler dispatch benchmarks are split by current runtime capability:

- `handler.plan.lookup` measures borrowed Sloppy Plan handler ID lookup only.
- `handler.runtime_contract.noop_unsupported` resolves the handler export and crosses the
  current noop engine boundary, which is expected to report unsupported.
- `http.dispatch.get.noop_unsupported` exercises synthetic parsed GET dispatch through the
  existing route matcher, manual dispatch table, plan lookup, and noop engine boundary.
- V8 handler-call benchmarking is deferred unless the build is configured with an approved
  V8 SDK and the benchmark is explicitly gated.

`http.request_head.parse` is a microbenchmark for the complete-buffer request-head parser.
It is not an HTTP server throughput benchmark.

Memory primitive benchmarks currently include scalar byte find-any scans, ASCII
case-insensitive string comparison, checked array-size arithmetic, and arena-backed builder
append/growth counters. These are internal microbenchmarks for future optimization
decisions. They are not allocation-rate, parser-throughput, or public performance claims.

## Output

Text output is human-readable. JSON output uses `sloppyBenchmarkVersion: 1` and is intended
for checked-in examples or local comparison artifacts. Local benchmark result files should
stay untracked unless a future task deliberately adds golden benchmark metadata.

See `benchmarks/fixtures/sample-output.json` for the output shape. The sample contains
zero values and is not performance data. Any measured report used for an optimization
decision must include the command, preset/configuration, compiler, platform, CPU/VM
context, V8 setting, relevant corpus/fixture, raw JSON/text output, and whether the run was
smoke or measured.

## Deferred

Full HTTP server throughput, JSON serialization benchmarks, database live benchmarks,
cross-runtime comparisons, dashboards, uploads, and CI performance gates are deferred until
the comparable runtime paths exist and can be measured honestly.

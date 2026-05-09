# Sloppy Benchmarks

Sloppy benchmarks are manual performance-validation tools. They are not correctness tests,
release gates, marketing statements, or comparisons with other runtimes.

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

Debug numbers are not meaningful. Smoke mode only validates the harness starts and each default
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
- V8 bridge benchmarks run only when the build is configured with an apvalidated V8 SDK and
  the benchmark is explicitly gated with `--include-v8`.

`http.request_head.parse` is a microbenchmark for the complete-buffer request-head parser.
It is not an HTTP server throughput benchmark.

V8 bridge benchmarks currently measure internal evidence for:

- V8 engine startup with minimal and app/http feature sets;
- JS source evaluation that registers native handlers;
- zero-argument handler-call proxy overhead through the public engine ABI;
- JS-to-native no-argument, primitive, string, and byte argument paths through current
  stdlib intrinsics;
- returned JavaScript Promise resolve/reject conversion through owner-thread microtask
  drain;
- native timer Promise settlement posted back to the V8 owner thread;
- HTTP result descriptor conversion and invalid option/header diagnostics;
- request-context base materialization, route/query access, header lookup, full header
  entries, body text/JSON, and body byte transfer;
- current in-process Sloppy HTTP flow: complete request parse, route match, Plan handler
  resolution, registered V8 handler entry with context, and JSON result conversion.

These benchmarks intentionally use the public engine ABI and existing intrinsics instead
of benchmark-only V8 hooks. They answer bridge-cost questions well enough to guide
internal optimization, but they are not pure CPU microbenchmarks for V8 itself. The
in-process HTTP flow benchmark still excludes sockets, TLS, kernel scheduling, and network
IO, so it must not be used as request-throughput or public performance statements.

Memory primitive benchmarks currently include canonical byte find-any scans, ASCII
case-insensitive string comparison, checked array-size arithmetic, and arena-backed builder
append/growth counters plus explicit small-builder append counters. The byte find-any
benchmark exercises the backend selected by the build preset, including automatic SIMD
selection on supported platforms. These are internal microbenchmarks for future
optimization decisions. Advanced SIMD presets such as `windows-avx2` must be named in
measured reports. These are not allocation-rate, parser-throughput, SIMD-performance, or
public performance statements.

## Output

Text output is human-readable. JSON output uses `sloppyBenchmarkVersion: 1` and is intended
for checked-in examples or local comparison artifacts. Local benchmark result files should
stay untracked unless a future task deliberately adds golden benchmark metadata.

See `benchmarks/fixtures/sample-output.json` for the output shape. The sample contains
zero values and is not performance data. Any measured report used for an optimization
decision must include the command, preset/configuration, compiler, platform, CPU/VM
context, V8 setting, relevant corpus/fixture, raw JSON/text output, and whether the run was
smoke or measured.

V8 bridge measured reports must also record the resolved V8 SDK/version, whether the
baseline was a benchmark-harness-only worktree or another commit, and which bridge paths
were intentionally only inspected because no safe current public-ABI benchmark exists.

## Deferred

Full HTTP server throughput, JSON serialization benchmarks, database live benchmarks,
cross-runtime comparisons, dashboards, uploads, and CI performance gates are deferred until
the comparable runtime paths exist and can be measured honestly.

# Benchmarks

Sloppy benchmarks are local engineering evidence. They are not release gates,
marketing copy, or official runtime rankings.

Use benchmark reports to answer specific engineering questions:

- where Sloppy is fast or slow;
- whether a branch changes V8 bridge overhead;
- whether route lookup scales as expected;
- where JSON/result serialization, HTTP parser/write paths, memory, or CPU
  behavior differ across runtimes;
- whether compiler-generated metadata helps a workload.

## HTTP phase profiling (experimental)

The HTTP server has an experimental, opt-in phase profiler for loopback
bottleneck work. It is disabled unless `SLOPPY_HTTP_PROFILE=1` is present. When
enabled with `SLOPPY_HTTP_PROFILE_OUT=<path>`, the runtime writes a JSON summary
containing per-phase count/total/average/min/max nanoseconds and counters such
as request count, keep-alive reuse, parser bytes, route/native JSON hits,
response bytes, libuv write calls, response buffer copies, arena resets,
diagnostics, and breadcrumbs. This env-driven surface is subject to change and
is not production telemetry.

Use the JSON competitor harness when a profile must line up with Sloppy
loopback rows:

```powershell
tools/windows/bench-json-competitors.ps1 -Iterations 1000 -Warmup 100 -Repeat 3 -HttpProfile
```

The harness writes one profile artifact per Sloppy runtime/scenario/repeat under
`artifacts/bench/http-profile-*.json` and a compact
`artifacts/bench/http-profile-summary.md` phase table. Treat profile numbers as
local engineering evidence and keep them with the benchmark JSON that produced
them.

For before/after work, keep timing and profiling as separate runs. Use the
non-profile JSON as the timing source of truth, then attach a smaller profile
run to the report:

```powershell
tools/windows/bench-json-competitors.ps1 `
  -Iterations 100 -Warmup 20 -Repeat 2 `
  -Out artifacts/bench/after/json-competitors.json `
  -Compare artifacts/bench/before/json-competitors.json `
  -ProfileInput artifacts/bench/profile/json-competitors.json `
  -Report
```

The wrapper writes `report.md` next to the timing JSON unless `-ReportOut` is
provided. The report renderer summarizes runtime status, scenario medians,
Sloppy before/after deltas, and HTTP profile counters without turning local
evidence into public claims.

The profiler is a process-global engineering probe for the current single
Sloppy HTTP server loop and V8 owner-thread path. Do not use one profiled
process to aggregate multiple concurrently updated servers or worker-thread
handler paths without first making the profile state thread-safe.

Optional external profiler commands for deeper investigation:

- Windows: capture with Windows Performance Recorder and inspect in WPA, or use
  Visual Studio Profiler / PerfView ETW when available.
- Linux: use `perf record` / `perf report`, and flamegraph tooling when
  installed.
- macOS: use Instruments Time Profiler or `sample` for short local captures.

## Realistic HTTP comparisons

The realistic suite lives in `benchmarks/realistic/` and is launched through:

```powershell
tools/windows/bench-realistic.ps1 -Quick -Runtime sloppy,node
tools/windows/bench-realistic.ps1 -Suite http -Runtime sloppy,node,bun,deno
```

Unix wrapper:

```sh
tools/unix/bench-realistic.sh --quick --runtime sloppy,node
tools/unix/bench-realistic.sh --suite http --runtime sloppy,node,bun,deno
```

The runner detects installed runtimes, records versions and paths, reports
missing runtimes as `UNAVAILABLE`, validates responses before timing, starts
servers on free loopback ports, samples process memory where practical, and
writes:

- `artifacts/bench/realistic/results.json`;
- `artifacts/bench/realistic/summary.md`;
- raw stdout, stderr, load-generator data, and process samples under
  `artifacts/bench/realistic/raw/`.

Node.js is required because the load generator is implemented with Node's HTTP
client. Bun and Deno are optional comparators unless the command marks them as
required.

## App shapes

Do not compare unlike rows as if they were the same benchmark.

- `baseline` is the minimal HTTP surface.
- `framework` adds equivalent route matching, JSON, params, query parsing, and
  request ID behavior.
- `feature-rich` adds quiet middleware, request ID, CORS metadata, and service
  style setup where supported.

Compare Sloppy to Node/Bun/Deno within the same category, workload,
connection count, duration, and iteration policy.

## Startup and build

Steady-state HTTP rows build Sloppy artifacts before timing request throughput.
Build duration, artifact sizes, route counts, Plan kind, and required features
are recorded separately. Do not mix Sloppy compilation time into request
throughput numbers.

Use the `startup` suite when the timing boundary is process start to first
`/health` response:

```powershell
tools/windows/bench-realistic.ps1 -Suite startup -Runtime sloppy,node
```

V8 script code caching is experimental and off by default. To measure it, set
`SLOPPY_V8_CODE_CACHE_DIR` to a writable directory before running a startup
suite. Cache files are keyed by generated source bytes, source label, cache
format version, and the linked V8 version. If V8 rejects a cache entry, Sloppy
falls back to normal compilation and rewrites the entry. Report whether the
cache directory was empty, warmed, or reused; do not mix cold-cache and
warm-cache rows.

V8 startup snapshots are also experimental and off by default. To measure them,
set `SLOPPY_V8_SNAPSHOT_DIR` to a writable directory before running a startup
suite. Snapshot blobs are keyed by linked V8 version, snapshot format version,
and Plan runtime feature mask. Snapshot creation and snapshot-backed isolate
creation use the same native callback external-reference table for Sloppy
provider, filesystem, time, crypto, codec, network, OS, HTTP client, and worker
intrinsics. Current snapshot support also requires engine creation to receive a
`SlRuntimeFeatureSet` through `SlEngineOptions.runtime_features`; an environment
variable alone only requests snapshot mode. If no runtime feature set is present,
snapshot build or read fails, or V8 rejects a blob, Sloppy falls back to normal
context creation. Report whether the snapshot directory was empty, warmed, or
reused, and label rows as requested snapshot mode unless the artifact shows a
blob was created or reused. When a startup snapshot is active, Sloppy does not
also consume or write the app-script code cache for that engine; keep snapshot
and code-cache measurements labeled as separate startup modes.

## Reporting rules

Every benchmark discussion should include:

- command line;
- git commit and dirty state if known;
- Sloppy build preset and whether V8 is enabled;
- runtime versions;
- machine CPU, core count, memory, OS, and power/VM context if relevant;
- output paths for `results.json` and `summary.md`;
- any unavailable runtimes or skipped categories.

Use neutral language: "relative to", "delta", and "local run". Do not write
"beats Node", "faster than Bun", "best TypeScript runtime", or similar public
claims.

## CI

Full benchmarks are manual. Default PR CI should not run long performance
matrices. Cheap validation may check script syntax, runner `--dry-run`, or a
tiny smoke if the PR explicitly needs benchmark harness evidence.

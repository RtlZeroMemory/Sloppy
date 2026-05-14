# Local JSON Competitor Benchmark

This is a local dev-machine loopback HTTP harness for JSON route comparisons. It
is not wired into default CI and must not be used for public performance claims.

```sh
node benchmarks/competitors/json-local.mjs --iterations 100 --warmup 10 --repeat 3 --out artifacts/bench/json-competitors.json
```

On Windows, the wrapper is:

```powershell
.\tools\windows\bench-json-competitors.ps1 -Iterations 100
```

Use `-Runtime` and `-Scenario` for targeted optimization loops. Values accept
comma-separated or whitespace-separated entries; `sloppy` runs both Sloppy
native/generic loopback modes.

```powershell
.\tools\windows\bench-json-competitors.ps1 `
  -Runtime sloppy `
  -Scenario dynamic-json,exception,large `
  -Iterations 300 -Warmup 30 -Repeat 5
```

Use `-Report` to emit a Markdown review report next to the JSON file:

```powershell
.\tools\windows\bench-json-competitors.ps1 `
  -Iterations 100 -Warmup 20 -Repeat 2 `
  -Out artifacts/bench/json-competitors.json `
  -Report
```

Use `-Compare` to attach a previous JSON run and `-ProfileInput` to attach a
separate profiled run. Keep profiled timing separate from the main timing
comparison because HTTP profile snapshots write JSON during the run.

```powershell
.\tools\windows\bench-json-competitors.ps1 `
  -Iterations 100 -Warmup 20 -Repeat 2 `
  -Out artifacts/bench/after/json-competitors.json `
  -Compare artifacts/bench/before/json-competitors.json `
  -ProfileInput artifacts/bench/profile/json-competitors.json `
  -Report
```

The harness records versions, OS/CPU details, SKIPPED entries for unavailable
runtimes or optional dependencies, response-correctness failures, and
machine-readable JSON results. The report renderer reads those JSON files and
summarizes runtime status, median per-scenario timings, Sloppy before/after
deltas, checked native/generic row labels, and HTTP profile counters without
making public performance claims.

Rows in this harness include client/server/socket/event-loop overhead and are
not directly comparable to in-process `sloppy_bench` JSON rows. Use the
`sloppy.loopback.native_json.*` and `sloppy.loopback.generic_json.*` rows from
this harness for local Sloppy-vs-runtime loopback comparisons.

Optional Express and Fastify rows are reported as `SKIPPED` until their
dependencies are installed locally:

```sh
npm install --no-save express fastify
```

The `route-table` scenario validates `/route/{id}` loopback routing for IDs in
a 1000-value cycle. Raw Node/Bun/Deno implementations may use one parameterized
route, so this row is loopback routing evidence, not a generated 1000-route
algorithm comparison.

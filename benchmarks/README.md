# Sloppy Benchmarks

Sloppy benchmarks are manual performance-validation tools. They are not correctness tests,
release gates, marketing statements, or public runtime rankings.

There are two benchmark layers:

- `sloppy_bench` is the native microbenchmark binary for route matching, memory primitives,
  handler dispatch, and V8 bridge internals.
- `tools/windows/bench.ps1 -Suite ...` is the BENCH-01 local runtime comparison engine for
  controlled Sloppy/Node/Bun/Deno workloads. It validates responses before timing them and
  writes structured JSON.
- `tools/windows/bench-realistic.ps1` and `tools/unix/bench-realistic.sh` run the
  realistic local HTTP comparison suite under `benchmarks/realistic/`. It keeps
  baseline, framework-equivalent, and feature-rich app shapes separate and writes
  JSON, Markdown, and raw artifacts under `artifacts/bench/realistic/`.
- `tools/windows/bench-compiler.ps1` is the compiler scalability harness for
  deterministic source-input projects. It measures `sloppyc` compile time,
  phase timings, process working set, and emitted artifact sizes.
- `tools/windows/bench-json-dispatch.ps1` runs the local Sloppy JSON dispatch
  microbenchmark rows across selected build configurations and writes a combined
  JSON report.
- `tools/windows/bench-json-competitors.ps1` runs the opt-in local JSON
  competitor harness under `benchmarks/competitors/`, marking unavailable
  runtimes or dependencies as `SKIPPED`.

Run native smoke/list checks locally:

```powershell
.\tools\windows\bench.ps1 -List
.\tools\windows\bench.ps1 -Smoke -Json
node benchmarks/cache/cache_bench.mjs
```

Run a measured local benchmark from a Release build:

```powershell
.\tools\windows\bench.ps1 -Configuration Release
.\tools\windows\bench.ps1 -Configuration Release -Json > .\benchmarks-local.json
.\tools\windows\bench-json-dispatch.ps1 -Smoke
.\tools\windows\bench-json-dispatch.ps1 -Configuration RelWithDebInfo -JsonMode native -Repeat 3
.\tools\windows\bench-json-competitors.ps1 -Iterations 100
.\tools\windows\bench-json-competitors.ps1 -Iterations 100 -Warmup 10
.\tools\windows\bench-json-competitors.ps1 -Iterations 100 -Warmup 10 -HttpProfile
```

Run the local runtime engine:

```powershell
.\tools\windows\bench.ps1 -Suite http
.\tools\windows\bench.ps1 -Suite http,route -Runtime sloppy,node,bun,deno
.\tools\windows\bench.ps1 -Suite route.generated-table -Runtime sloppy,node,bun,deno
.\tools\windows\bench.ps1 -Suite bridge -Runtime sloppy -Out artifacts\bench\sloppy-bridge.json
.\tools\windows\bench.ps1 -Compare @("artifacts\bench\before.json", "artifacts\bench\after.json")
```

Run the realistic local runtime suite:

```powershell
.\tools\windows\bench-realistic.ps1 -Quick -Runtime sloppy,node
.\tools\windows\bench-realistic.ps1 -Suite http -Runtime sloppy,node,bun,deno
.\tools\windows\bench-realistic.ps1 -Suite http -Workload health,json,route-param,large-routes
```

Run compiler scalability smoke and scale reports:

```powershell
.\tools\windows\bench-compiler.ps1 -Suite smoke -Out artifacts\bench\compiler-smoke.json
.\tools\windows\bench-compiler.ps1 -Suite scale -Size small,medium -Out artifacts\bench\compiler-scale-smoke.json
.\tools\windows\bench-compiler.ps1 -Compare artifacts\bench\compiler-before.json artifacts\bench\compiler-after.json
```

The Unix wrapper preserves the command shape and native `sloppy_bench` path. The BENCH-01
local runtime comparison engine is currently Windows-first; Unix reports that lane as
`UNAVAILABLE` until a matching process/HTTP runner is implemented.
The Unix compiler wrapper is available at `tools/unix/bench-compiler.sh` and
uses the same Node-backed generator and compiler harness.

Debug numbers are not meaningful. Smoke mode only validates the harness starts and each default
benchmark path can execute a tiny iteration count; smoke output is not a performance
conclusion. Use larger measured runs on the same machine for branch-to-branch comparisons.

`benchmarks/cache/cache_bench.mjs` is a JavaScript app-host cache smoke
benchmark. It measures memory hit/miss/set, cache-aside hit, same-process
coalesced misses, and output cache hits. It reports local elapsed nanoseconds
per operation only; it does not compare Sloppy to Redis, ASP.NET, Spring, or
any external cache service.

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
- Route dispatch benchmarks exercise the current native endpoint table: exact
  method/path hash lookup plus parameter-route segment-trie dispatch. They are
  local engineering measurements, not public performance claims.
- JSON dispatch benchmarks isolate schema-backed native request validation,
  validation rejection, materialize-once handoff counters, preencoded native
  JSON response writing, HEAD metadata writing, fallback counters, and a
  bounded native request-plus-response path. They do not include sockets or
  public throughput claims.
- V8 bridge benchmarks run only when the build is configured with a validated V8 SDK and
  the benchmark is explicitly gated with `--include-v8`.

Native route dispatch benchmark rows are intentionally scoped:

- `route.dispatch.generated_table.<count>.<mode>.<target>` compares identical
  static route tables under stored dispatch modes `compiled`, `classic`, and
  `validate`. Counts cover 10, 100, 1000, and 10000 routes. Targets cover
  first, middle, last, missing static path, and method mismatch. The route
  table is cached by the warmup call; the measured loop excludes artifact
  validation, route-table build, sockets, the HTTP parser, response writer, and
  V8.
- `route.dispatch.param_trie.<mode>.<case>` compares mixed parameter route
  tables with shared prefixes under the same three dispatch modes. Cases cover
  parameter hit, parameter miss, constrained miss falling through to a string
  parameter, parameter-first routes, static-vs-parameter precedence, and
  constrained-vs-string precedence. The mixed table is cached by the warmup
  call. Validate mode must agree with classic and compiled dispatch before the
  benchmark contributes timing. The compiled parameter-hit row also has
  no-capture and capture variants to separate trie lookup from route-param
  materialization.
- `route.dispatch.param_heavy.<count>.<mode>.last` compares compiled and classic
  dispatch for the last route in shared-prefix parameter tables with 100, 1000,
  and 10000 routes. The table is cached before timing; the row measures
  dispatch plus native static response construction.
- `route.dispatch.native_response.*` measures dispatch plus construction of a
  native no-JS static response for literal `Results.text`, `Results.json`, and
  `Results.ok` shapes. It excludes sockets and the response writer. Use
  `http.dispatch.get.noop_unsupported` as the current noop handler-boundary
  comparison row.
- `route.dispatch.table_build.*` measures Plan-backed route-table
  materialization before serving. It excludes route lookup, handler execution,
  sockets, the HTTP parser, response writer, V8, and routes.slrt artifact
  validation. `route.dispatch.table_build_param.*` repeats the build
  measurement for 100, 1000, and 10000 shared-prefix parameter routes so
  parameter bucket and trie construction scale independently from exact-static
  indexing.
- `route.dispatch.artifact_validate.1` measures one-route `routes.slrt`
  artifact validation against Plan metadata and reports the route count and
  artifact byte size in benchmark metadata. It excludes dispatch table
  materialization and request dispatch.

No 50000-route native microbenchmark is registered in this PR. The 10000-route
case keeps smoke/list validation reasonable while exercising a larger table;
50000-route timing is left to ad hoc local runs or a later benchmark-methodology
PR with pinned hardware and duration budgets.

`http.request_head.parse` is a microbenchmark for the complete-buffer request-head parser.
It is not an HTTP server throughput benchmark.
`http.body_reader.json_known_length` measures the bounded body reader for a declared
JSON content length and records builder grow/copy counters in the checksum. It exists to
evaluate request-body copy and allocation changes without involving sockets.

Native JSON benchmark rows are intentionally scoped:

- `json.request.generic_parse.small_login.payload_only` and
  `json.request.generic_parse.medium_body.payload_only` measure generic yyjson parsing
  without schema validation or dispatch.
- `json.request.native_schema.valid.payload_validate_only` parses and validates
  a small schema-backed JSON request body. It excludes route lookup, sockets,
  response writing, and JavaScript handler execution.
- `json.request.native_materialize_once.small_login.payload_validate_materialize` adds the
  native-validated materialize-once handoff model and reports the duplicate
  validation skip counter for that deterministic path.
- `json.request.generic_parse.malformed.payload_only` and
  `json.request.native_schema.reject.problem_details` split invalid request
  rejection into generic parser and native schema paths; the native path builds
  validation problem details without echoing request values.
- `json.response.generic.serialize.payload_only` and
  `json.response.native_schema.serialize.payload_only` both serialize the same
  supported JSON payload shape without HTTP status/header writing, sockets, or
  JavaScript handler execution.
- `json.response.generic.http_response_write`,
  `json.response.native_schema.http_response_write`, and
  `json.response.native_static.http_response_write` include HTTP response
  status/header/body framing. They still exclude sockets. The generic and
  native-schema rows use the same JSON payload shape; the static row starts from
  preencoded JSON bytes.
- The native schema response rows pre-emit fixed schema field-name fragments and
  fast-path strings that need no escaping. The payload row uses Sloppy's bounded
  JSON writer for checked direct writes, string escaping, and scalar formatting.
  The HTTP response row uses the same known write-plan length to write headers
  and body into one bounded output buffer, so it measures native JSON emission
  plus HTTP framing without an extra body copy.
- `json.response.native_static.head_http_response_write` writes response
  metadata with body suppression, modeling `HEAD` after normal dispatch.
- `json.response.large_list.http_response_write` covers a larger preencoded
  JSON list response through the HTTP response writer.
- `json.dispatch.full_inprocess.generic_json` and
  `json.dispatch.full_inprocess.native_json` route a POST request, apply JSON
  request handling, and return a native JSON response.
- `json.dispatch.routes_1k.table_build` and
  `json.dispatch.routes_10k.table_build` measure only Plan-backed route-table
  construction from a prebuilt in-memory Plan fixture. They do not include
  fixture generation, request dispatch, JSON parsing, schema validation,
  response writing, or sockets.
- `json.dispatch.routes_1k.native_json.dispatch_only`,
  `json.dispatch.routes_10k.native_json.dispatch_only`,
  `json.dispatch.routes_1k.generic_json.dispatch_only`, and
  `json.dispatch.routes_10k.generic_json.dispatch_only` build the route table
  once before timing, then route an in-memory POST request through the selected
  JSON dispatch mode. They exclude schema validation, handler execution,
  response writing, and sockets.
- `json.dispatch.routes_1k.native_json.full_inprocess`,
  `json.dispatch.routes_10k.native_json.full_inprocess`,
  `json.dispatch.routes_1k.generic_json.full_inprocess`, and
  `json.dispatch.routes_10k.generic_json.full_inprocess` build the route table
  once before timing, then include in-process routing, body policy, JSON
  request handling, request validation metadata, and native response
  materialization. They still exclude sockets and JavaScript handler execution.
- `json.dispatch.native_schema_static_response.full_inprocess` combines native
  request validation with native static JSON response writing and still excludes
  sockets and user handler execution.

Set `SLOPPY_JSON_PROFILE=1` to add a `jsonProfile` object to each JSON-family
`sloppy_bench --format json` result. The profiler is disabled by default so
normal benchmark rows keep their existing output shape and timing. Profile
output is machine-readable evidence for phase attribution and counters; it is
not a cross-runtime score. Request-side phases include body-size checks,
yyjson parsing, schema lookup, object field iteration, field lookup, required
field tracking, scalar/string/integer validation, unknown-field policy, path
construction, issue recording, problem-details construction, materialize-once
handoff, and generic fallback markers. Response-side phases include write-plan
lookup, output-size estimation, response field iteration, field value access,
string escaping, scalar formatting, literal writes, builder grow/copy events,
HTTP response header writing, native fallback, and capacity failure markers.

All JSON route-scale rows currently target the first generated static route in
the table. The target is intentionally fixed so route-count changes are not
mixed with first/middle/last/param/miss target changes. Use the route dispatch
benchmark family for those target-position comparisons.

`tools/windows/bench-json-competitors.ps1` is a separate local loopback HTTP
harness. It uses a Node `fetch` client, one awaited request at a time, with the
same warmup and measured iteration count for every runtime in that run. It
records response correctness before counting a request. Its Sloppy row names
are `sloppy.loopback.native_json.*` and `sloppy.loopback.generic_json.*`; Node,
Bun, Deno, Express, and Fastify rows are also named as loopback HTTP rows. These
rows are the only JSON rows in this family intended for local
Sloppy-vs-runtime loopback comparison. In-process `sloppy_bench` JSON rows must
not be compared directly against competitor loopback rows.

The Sloppy loopback matrix includes static no-JS `Results.json` and
`Results.text` rows, a dynamic V8 `Results.json` row, request-body JSON rows,
large JSON response rows, and route-table rows. When `-HttpProfile` is enabled,
the static no-JS rows should show `nativeResponseHits > 0`; a zero value means
the benchmark app did not exercise the native response path being investigated.

Pass `-HttpProfile` to run Sloppy loopback rows with `SLOPPY_HTTP_PROFILE=1`.
The runner starts a fresh Sloppy process per profiled scenario and writes
machine-readable phase summaries to `artifacts/bench/http-profile-*.json`, plus
`artifacts/bench/http-profile-summary.md`. Profiling is disabled by default and
the runtime writes no profile file unless `SLOPPY_HTTP_PROFILE_OUT` is set by
the harness or by an explicit local profiling command.

The competitor `route-table` scenario validates `/route/{id}` loopback routing
for IDs in a 1000-value cycle. Raw Node/Bun/Deno implementations may use a
parameter route rather than generated static route entries, so this scenario is
not a generated-route-table algorithm comparison. Generated route-table
algorithm comparisons belong in the native route dispatch or local-neutral
benchmark suites where comparator shape is explicit.

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

Operations benchmarks cover native metrics overhead:

- `ops.metrics.counter.inc` increments one counter with a stable route-pattern label.
- `ops.metrics.histogram.observe` records one histogram observation with fixed buckets.
- `ops.metrics.prometheus.render_64` renders Prometheus text for 64 counter series.

These rows measure the native registry and renderer only. They do not include sockets,
transport parsing, JavaScript handlers, provider I/O, or external Prometheus scraping.

The local runtime engine uses semantically equivalent HTTP workloads where practical:

- `http`: `/health`, small JSON, route parameter JSON, query decode, and small JSON
  POST acknowledgement.
- `route.generated-table`: route table sizes 10, 100, and 1000, with
  first/middle/last/missing targets. Sloppy, Node, Bun, and Deno all use explicit
  generated route entries for this shape.
- `route.compact-prefix`: route counts 10, 100, and 1000 with the same targets, but
  comparator runtimes may use a compact `/routes/:id`/integer-bounds handler shape.
  Sloppy is reported as `SKIPPED` for this shape until it can express an equivalent
  compact-prefix app. Do not compare compact-prefix entries against generated-table
  entries as apples-to-apples measurements.
- `bridge`: Sloppy-only V8/result/request-context workloads. Source-input header
  facade coverage exercises the live runtime path, while native V8 bridge microbenchmarks
  still cover lower-level header lookup.
- `concurrency`: fixed concurrent HTTP batches against `/health`, JSON, route parameter,
  and POST workloads. Latency is reported as concurrent batch wall time, with `wallMs`,
  `concurrency`, and measured batch count included per workload.
- `middleware`: Sloppy-only middleware, ProblemDetails, CORS, and health workloads.
- `sqlite`: reserved for the stable SQLite/provider bridge path; unavailable cases are
  reported instead of simulated.
- `startup`: Sloppy minimal build/artifact size where available, plus process startup to
  first `/health` response.

Sloppy steady-state workloads build artifacts first, then run `sloppy run --artifacts` on a
local listener. Startup/build workloads name their timing boundary separately. The harness
records runtime versions and reports missing Node, Bun, Deno, or Sloppy executables as
`UNAVAILABLE`. A missing comparator runtime is normal on developer machines and is not a
default test failure.

Response correctness is part of every measured HTTP workload: status, body, and relevant
content type are checked before a request contributes latency data. Broken responses are
reported as failed benchmark entries, not as measurements.

Route benchmark results are meaningful only within the same `comparatorShape`
(`generated-table` or `compact-prefix`). Route workload JSON includes `comparatorShape`,
`routeCount`, and `routePosition` so comparisons can reject mismatched shapes instead of
mixing different routing strategies.

HTTP workload entries also include process-level CPU and memory snapshots for the measured
window when the child process exposes them. CPU is reported as elapsed processor time, and
memory is reported from the operating-system process counters. Small runs can show coarse
CPU increments or zero deltas on Windows. `allocations` and `bytesCopied` remain
reserved/null until Sloppy exposes allocator-level counters; process memory is not an
allocation-rate measurement.

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

Local runtime JSON uses `schemaVersion: 1`. Each report includes:

- Git commit, branch, and dirty state.
- Host OS, architecture, CPU, and logical core count.
- Runtime path, version, availability, and unavailable reason.
- Suite/runtime selection, warmup count, request count, and timeout.
- Per-workload status, correctness details, p50/p95/p99 latency, throughput, startup time,
  error count, process CPU/memory counters where available, and reserved allocation/copying
  fields.

Compiler benchmark JSON also uses `schemaVersion: 1`. Each report includes
Git/host/compiler metadata plus one benchmark entry per size. Entries record
project shape, status, duration, sampled peak working set, artifact byte sizes,
compiler profile, compiler phase timings, top phases, and source counters. See
`docs/contributor/compiler-performance.md` for the schema and workflow.

Use compare mode to compare two reports from the same machine and similar load conditions.
Do not compare a laptop run against a CI VM or a dirty Debug build against a clean Release
build and treat the delta as meaningful.
Compare output reports duration, artifact, phase, and sampled working-set deltas when
both inputs contain the relevant fields.

## Deferred

Full public methodology, dashboards, uploads, CI performance gates, stable Unix runtime
comparison, live database comparisons, and published performance reports are deferred until
the comparable runtime paths exist and can be measured with a separate reviewed methodology.

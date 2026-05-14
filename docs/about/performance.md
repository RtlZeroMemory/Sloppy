# Performance

Sloppy is public alpha software. Performance is not a marketing
surface yet.
Benchmarks measure specific workloads when they're run as benchmarks;
benchmark *smoke* runs only verify the harness executes — they're
neither performance numbers nor correctness coverage.

This page is honest about what's been thought about, what's been
measured, and what hasn't.

## What's been designed for

The runtime kernel is C with arena allocation, intern tables for
repeated metadata, and explicit ownership rules. Hot paths (route
matching, plan validation, dispatch) avoid heap allocation per
request. The HTTP parser/dispatch path is bounded and copy-light on
the request side.

The V8 bridge is narrow — most calls cross it once per request, not
thousands of times.

Native layout work is one of the few performance optimizations that is allowed
before broad benchmarking, because it can reduce the amount of memory the
runtime has to touch in repeated structures. The rule is conservative: use
normal C layouts that are easy to inspect, such as field reordering, tagged
unions, flag masks, `_Alignof`, and layout tests. Do not use packed runtime
structs, NaN boxing, or tagged pointers as shortcuts. Those techniques need a
separate design with measurements and a clear portability/debugging story.

The same standard applies to branch shape. Hot dispatch over frame types, event
kinds, field kinds, or async operation kinds should use direct `switch`
dispatch when there are several cases. Predicate-heavy code, string matching,
and V8 type probes should stay as ordinary `if` checks unless measurements show
otherwise.

Generated static HTTP routes are indexed by method and path. Parameterized
routes use a method-specific native segment trie, with the older first-static
segment buckets retained as an internal fallback shape. Valid HTTP/1.1 error
responses, including 404 route misses, remain eligible for keep-alive instead
of forcing a reconnect per miss.

The compiler exposes this as `routeDispatch.mode: native-compiled` in Plan
metadata and emits a `routes.slrt` binary route artifact. `sloppy run` validates
the artifact before materializing the runtime table from `app.plan.json`.
Native no-JS static responses and native URL writers are reported by their Plan
counters; those are structure evidence, not benchmark results.

HTTP/V8 profiling is opt-in. `SLOPPY_HTTP_PROFILE=1` records HTTP dispatch,
native response, response serialization, and V8 bridge phase summaries when
`SLOPPY_HTTP_PROFILE_OUT` points at a local artifact path. `SLOPPY_V8_PROFILE=1`
also enables that profile output for V8-focused runs. Profile counters are
engineering attribution data: handler lookup/cache hits, no-JS/native response
hits, V8 handler calls, Promise vs sync returns, materialization counts, result
conversion counts, JSON stringify calls, and exception mapping counts. They are
not cross-runtime benchmark scores.

Current HTTP/V8 profile evidence separates static no-JS routes from dynamic
handler routes. Static `Results.json`, `Results.text`, empty-status, and
problem-details rows are expected to show native/no-JS hits with zero V8 handler
calls. Dynamic handlers still enter V8; their current cost is concentrated in
the V8 call, request/context materialization when the handler asks for context
facets, and result conversion/JSON stringify for object and large JSON
responses.

The V8 path has opt-in startup experiments through `SLOPPY_V8_CODE_CACHE_DIR`
and `SLOPPY_V8_SNAPSHOT_DIR`. They are engineering knobs for startup
measurement, not default runtime guarantees. Code-cache entries are invalidated
by generated source bytes, source label, cache format version, and linked V8
version; V8 cache rejection falls back to normal compilation. Startup snapshots
are keyed by V8 version and runtime feature mask, with Sloppy native intrinsic
callbacks listed in the snapshot external-reference table. When a startup
snapshot is active, app-script code caching is skipped for that engine so the two
V8 serialization paths are measured separately.

These are design intents, not measured outcomes. They suggest where to
look when measurements eventually run, not what numbers to expect.

## What's been measured

The repo has microbenchmark harnesses under `benchmarks/` that can be run on
demand. The smoke variant verifies the harness executes; it proves nothing
about throughput or latency.

The repo also has local runtime comparison runners for internal
engineering feedback:

```powershell
tools/windows/bench.ps1 -Suite http -Runtime sloppy,node,bun,deno
tools/windows/bench-realistic.ps1 -Suite http -Runtime sloppy,node,bun,deno
node benchmarks/local-neutral/scripts/run.mjs --runtime sloppy,node --workload health
```

The realistic suite under `benchmarks/realistic/` is the longer-lived
Sloppy/Node/Bun/Deno comparison harness. It records host metadata, runtime
versions, warmup/sample counts, process memory where practical, raw artifacts,
structured JSON, and a Markdown summary. It keeps baseline, framework-equivalent,
and feature-rich app shapes separate so unlike rows are not treated as
apples-to-apples measurements. Missing comparator runtimes are reported as
`UNAVAILABLE`. Use larger measured runs to compare branches on the same machine,
not to rank Sloppy publicly.

The local neutral suite under `benchmarks/local-neutral/` starts equivalent
Sloppy, Node, Bun, and Deno apps and drives them with an external tool such as
`oha`, `wrk`, `k6`, or `vegeta`. Use it when the benchmark client must be
outside Sloppy. It writes JSON, CSV, Markdown, raw logs, and environment
metadata under `artifacts/benchmarks/local-neutral/`. Reports include p95/p99
latency and best-effort server CPU/memory samples. `--preset realistic-short`
is the normal inspectable all-runtime K6 pass for branch work; it is sized to
finish in about 30 minutes instead of hours. `--preset stress` is the local
high-pressure lane, and `--preset public-candidate --claim-mode
public-candidate` adds explicit public-readiness checks. Long runs write
`progress.json`, `results.partial.json`, `summary.partial.json`, and
`report.partial.md` while they execute so interrupted benchmarks still leave
reviewable evidence. Same-machine runs are still local engineering evidence
until the report shows a complete comparator matrix, resource samples, clean
checkout, stress-sized repeats, and a separate load-generator topology. The
current Sloppy fixture records `auth-api-key` as `SKIPPED`; the report keeps
that gap visible instead of treating that row as equivalent.

A real benchmark run names the workload, the build configuration, the
hardware, the command, and the output. Anything described as "Sloppy
benchmark" without that context is informal, not a project claim.

## What hasn't been done yet

- No published competitive comparison against Node/Bun/Deno or
  framework-specific baselines. The realistic suite is local engineering
  evidence only.
- No published latency or throughput targets.
- No performance regression gates beyond "the harness still runs".
- No production deployment evidence.

## Why numbers are not published yet

Public alpha numbers are useful for local regression tracking,
but misleading as public claims. Optimizations land sporadically, the HTTP
server is not production-hardened, and the framework feature set is still
landing. Locking in a number now would tell future Sloppy something untrue
about its own floor.

Before benchmark numbers are published, Sloppy needs:

- a fixed methodology (workload, build flags, hardware class, version)
- explicit comparison runtimes
- p95/p99 latency and resource reporting
- stress/soak evidence, not just short loopback checks
- a way to reproduce locally and a separate-load-generator run for public claims

Until then, treat any informal measurement you do as an estimate, not a
benchmark.

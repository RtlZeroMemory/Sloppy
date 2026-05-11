# Performance

Sloppy is public alpha, pre-production software. Performance is not a marketing
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

Generated static HTTP routes are indexed by method and path. Parameterized
routes are bucketed by method and first static segment, with an indexed bucket
lookup so misses do not scale with the total number of parameter buckets before
candidate patterns are checked. Valid HTTP/1.1 error responses, including 404
route misses, remain eligible for keep-alive instead of forcing a reconnect per
miss.

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
```

The realistic suite under `benchmarks/realistic/` is the longer-lived
Sloppy/Node/Bun/Deno comparison harness. It records host metadata, runtime
versions, warmup/sample counts, process memory where practical, raw artifacts,
structured JSON, and a Markdown summary. It keeps baseline, framework-equivalent,
and feature-rich app shapes separate so unlike rows are not treated as
apples-to-apples measurements. Missing comparator runtimes are reported as
`UNAVAILABLE`. Use larger measured runs to compare branches on the same machine,
not to rank Sloppy publicly.

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

Public alpha, pre-production numbers are useful for local regression tracking,
but misleading as public claims. Optimizations land sporadically, the HTTP
server is not production-hardened, and the framework feature set is still
landing. Locking in a number now would tell future Sloppy something untrue
about its own floor.

Before benchmark numbers are published, Sloppy needs:

- a fixed methodology (workload, build flags, hardware class, version)
- explicit comparison runtimes
- a way to reproduce locally

Until then, treat any informal measurement you do as an estimate, not a
benchmark.

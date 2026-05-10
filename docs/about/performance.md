# Performance

Sloppy is pre-alpha. Performance is *not* a marketing surface yet.
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

Pre-alpha numbers are misleading. Optimizations land sporadically, the
HTTP server isn't production-hardened (so any test under load measures
non-final code), and the framework feature set is still landing. Locking
in a number now would tell future Sloppy something untrue about its own
floor.

Before benchmark numbers are published, Sloppy needs:

- a fixed methodology (workload, build flags, hardware class, version)
- explicit comparison runtimes
- a way to reproduce locally

Until then, treat any informal measurement you do as an estimate, not a
benchmark.

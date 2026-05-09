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

The repo has microbenchmark harnesses (`benchmarks/`) that can run as
opt-in lanes. The smoke variant verifies the harness executes; it
proves nothing about throughput or latency.

The repo also has a local runtime comparison runner for internal
engineering feedback:

```
tools/windows/bench.ps1 -Suite http -Runtime sloppy,node,bun,deno
```

It records host metadata, runtime versions, warmup/sample counts, and
structured JSON results. Missing comparator runtimes are reported as
unavailable. Use those reports to compare branches on the same machine,
not to rank Sloppy publicly.

A real benchmark run names the workload, the build configuration, the
hardware, the command, and the output. Anything described as "Sloppy
benchmark" without that context is informal, not a project claim.

## What hasn't been done yet

- No published competitive comparison against Node/Bun/Deno or
  framework-specific baselines.
- No published latency or throughput targets.
- No performance regression gates beyond "the harness still runs".
- No production deployment evidence.

## Why we're cagey

Pre-alpha numbers are misleading. Optimizations land sporadically, the
HTTP server isn't production-hardened (so any test under load measures
non-final code), and the framework feature set is still landing. Locking
in a number now would tell future Sloppy something untrue about its own
floor.

When Sloppy hits public alpha, expect a benchmarks page with:

- a fixed methodology (workload, build flags, hardware class, version)
- explicit comparison runtimes
- a way to reproduce locally

Until then, treat any informal measurement you do as an estimate, not a
benchmark.

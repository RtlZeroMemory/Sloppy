# Performance

Sloppy is pre-alpha. Performance is *not* a marketing surface yet, and
benchmarks in this repository measure correctness, not speed.

This page is honest about what's been thought about, what's been measured,
and what hasn't.

## What's been designed for

The runtime kernel is C with arena allocation, intern tables for repeated
metadata, and explicit ownership rules. Hot paths (route matching, plan
validation, dispatch) avoid heap allocation per request. The HTTP
parser/dispatch path is bounded and copy-light on the request side.

The V8 bridge is narrow — most calls cross it once per request, not
thousands of times.

## What's been measured

The repo has microbenchmark harnesses (`benchmarks/`) that smoke-run as
part of CI. They prove the harness works. They don't establish any
operational performance number.

If you see a number described as "Sloppy benchmark" anywhere outside a
specific benchmark run with a documented workload, hardware, build
config, and command line — it's not a claim Sloppy has signed off on.

## What hasn't been done yet

- No competitive comparisons against Node/Bun/Deno or framework-specific
  baselines.
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

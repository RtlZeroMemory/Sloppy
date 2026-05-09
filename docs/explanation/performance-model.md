# Performance

## Purpose

Performance is part of Sloppy's credibility. Developer ergonomics is the main
product wedge, but the app-host model should also remove avoidable framework
overhead from common backend paths.

Performance comparisons need measured benchmark reports. Until those reports
exist, docs should describe design intent and measured local results only.

## Scope

This document covers:

- performance principles;
- native app-host advantages;
- hot-path design choices;
- benchmark policy;
- planned benchmark suite;
- implementation tasks and acceptance criteria.

## Current Boundaries

The current pre-alpha docs focus on model and harness work:

- release-blocking performance regression gates are not defined yet;
- unsupported runtime paths are not optimization targets;
- diagnostics, resource safety, and portability stay ahead of early
  microbenchmarks.

## Current Phase

Performance strategy and benchmark harness scaffolding exist. Benchmark list or
smoke checks validate that the harness starts and can select benchmarks. A
measured Release benchmark report must name the command, build,
hardware/context, workload, and output.

Optional SIMD backends may change which implementation a benchmark exercises.
Measured reports still need to identify the preset, compiler, platform,
configured `SLOPPY_ENABLE_SIMD` mode, and configured `SLOPPY_SIMD_LEVEL`.

The V8 bridge has an internal benchmark group behind `sloppy_bench --include-v8`. It exists
to answer engineering questions about JS-to-native calls, native-to-JS Promise settlement,
HTTP result conversion, request-context materialization, header lookup/materialization, and
body byte/text transfer. The group also includes an in-process HTTP flow benchmark for
complete request parsing, route dispatch, registered V8 handler entry, and JSON result
conversion. HTTP server throughput needs a separate workload and report.

## Future Phase

Measured benchmark checks should become meaningful only when a scoped task
defines the path, methodology, and release-build data to collect.

## Product Positioning

Sloppy can win where a native app host removes framework overhead:

- route graph known before serving;
- handler IDs are numeric;
- middleware and filters are prevalidated;
- request objects are materialized lazily;
- common results can use native fast paths;
- permissions and services fail at startup instead of on the hot path.

This is a design direction, not a universal benchmark result.

## Hot-Path Design Choices

Planned choices:

- native route trie or equivalent static route table;
- numeric handler IDs in dispatch;
- app graph freeze before app run;
- lazy JavaScript request context materialization, especially headers and request body
  views that may never be accessed by a handler;
- request-scoped arenas for transient native state;
- native response fast paths for text, JSON, status-only, and no-content results;
- prevalidated service, permission, and schema metadata;
- no string handler lookups in hot paths;
- no dynamic graph mutation in static plan mode.

Current V8 bridge optimizations may cache fixed property keys, private keys, and helper
functions per isolate/context, and may use copied byte snapshots or V8-owned
`ArrayBuffer` storage where that avoids repeated JS materialization. They must not keep
borrowed request, arena, platform, resource, or V8 handles alive outside their documented
lifetime.

## Concurrency Performance Model

Sloppy concurrency is mostly I/O concurrency inside one JS worker. JS callbacks execute
sequentially on that worker, so CPU-heavy JS blocks the worker. Native route matching,
preflight validation, request scopes, and data-provider scheduling reduce unnecessary JS hot
path work.

Future benchmarks should measure many pending I/O-bound requests without assuming
thread-per-request behavior, plus worker/isolates scaling once multicore execution exists.
See `docs/internals/async-runtime.md`.

## Lifecycle Impact

Startup does more validation so requests do less:

1. load plan;
2. validate compatibility;
3. freeze graph;
4. build native route table;
5. verify handler IDs;
6. enter event loop.

Request execution should then be:

1. parse protocol;
2. route match;
3. create request scope;
4. call handler by ID;
5. convert result;
6. cleanup.

## Benchmark Policy

Performance comparisons require benchmark data.

Benchmark list/smoke output only validates that the harness builds, starts, and
exposes the expected benchmark names. It is not a runtime measurement.

Each benchmark report must record:

- Sloppy commit;
- build type and compiler;
- operating system;
- hardware;
- runtime configuration;
- workload;
- input corpus or fixture identity;
- compared baseline;
- warmup behavior;
- repetition count/statistical method;
- raw data location.

Benchmarks must be reproducible from source and documented inputs.

## Planned Benchmark Suite

Initial candidates:

- cold start measurement plan;
- many pending async requests later;
- worker scaling later;
- `app.plan.json` load and validation;
- handler dispatch C -> V8 -> C;
- static route match;
- dynamic route match later;
- `Results.text`;
- `Results.json`;
- middleware depth;
- service resolution;
- SQLite route later;
- idle memory;
- memory under load.

Current V8 bridge benchmark names use `v8.bridge.*` and `v8.startup.*`. The
benchmark group supports bridge optimization work; benchmark smoke in CI only
checks selection and tiny iteration execution.

Benchmark names should use `bench_<path_or_topic>`, for example
`bench_route_match_static`.

## Internal Architecture

Likely layout:

```text
tests/benchmarks/
  README.md
  bench_plan_load.*
  bench_route_match.*
  bench_handler_dispatch.*
docs/explanation/performance-model.md
```

The benchmark harness should remain separate from correctness tests. A benchmark smoke test
may run in CI, but trend/regression checks should be introduced only when workloads are
stable.

## Error And Diagnostic Behavior

Benchmark tooling should fail with clear diagnostics for:

- missing runtime binary;
- unsupported build configuration;
- missing V8 SDK when a V8 benchmark is requested;
- missing provider driver for provider benchmarks;
- noisy or incomplete benchmark metadata.

## Testing Requirements

Benchmarks themselves need smoke coverage:

- benchmark executable starts;
- metadata is recorded;
- one trivial benchmark produces output;
- CI can run a short non-authoritative smoke mode.

Correctness tests remain separate and must pass before benchmark results matter.

## Quality Gates

- performance comparisons link to benchmark data;
- benchmark code must not weaken normal correctness gates;
- benchmark output must include commit/build metadata;
- provider benchmarks must skip with a clear reason when environment is unavailable.

## Implementation Tasks

1. Add benchmark directory and README.
2. Choose benchmark harness for C paths.
3. Add smoke benchmark for a trivial function after core primitives exist.
4. Add plan-load benchmark after plan loader exists.
5. Add route-match benchmark after router exists.
6. Add handler-dispatch benchmark after V8 bridge milestone.
7. Add provider benchmarks only in provider phases.

## Acceptance Criteria For First Benchmark Harness

The first benchmark harness is accepted when:

- it builds with the normal developer workflow;
- it can run a trivial benchmark locally;
- output records commit/build/platform metadata;
- docs explain the measured scope and limits of the results;
- CI either runs a smoke mode or documents why it is deferred.

## Open Questions

- Exact benchmark framework.
- Whether benchmark trend checks run in GitHub Actions or dedicated machines.
- Which baseline frameworks are fair for public comparisons.
- How to publish raw benchmark data.

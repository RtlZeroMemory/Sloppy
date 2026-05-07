# Performance

## Purpose

Performance is Sloppy's credibility layer. Developer ergonomics is the main product wedge,
but the app-host model should also remove avoidable framework overhead from common backend
paths.

Sloppy must not claim to be faster than Bun, Node, Deno, or any framework universally
without measurements. Performance claims require benchmarks.

## Scope

This document covers:

- performance principles;
- native app-host advantages;
- hot-path design choices;
- benchmark policy;
- planned benchmark suite;
- implementation tasks and acceptance criteria.

## Non-Goals

The current pre-alpha docs do not:

- make public performance claims;
- define release-blocking performance regression gates;
- optimize unsupported runtime paths;
- trade away diagnostics, resource safety, or portability for early microbenchmarks.

## Current Phase

Performance strategy and benchmark harness scaffolding exist. Benchmark list or smoke
checks prove that the harness runs; they do not prove runtime performance or support public
performance claims. Measured Release benchmark evidence must name the command, build,
hardware/context, workload, and output.

## Future Phase

Measured benchmark lanes should become meaningful only when a scoped task defines the path,
methodology, and release-build evidence to collect.

## Product Positioning

Sloppy can win where a native app host removes framework overhead:

- route graph known before serving;
- handler IDs are numeric;
- middleware and filters are prevalidated;
- request objects are materialized lazily;
- common results can use native fast paths;
- permissions and services fail at startup instead of on the hot path.

This is not a promise that every benchmark is faster than every other runtime.

## Hot-Path Design Choices

Planned choices:

- native route trie or equivalent static route table;
- numeric handler IDs in dispatch;
- app graph freeze before app run;
- lazy JavaScript request context materialization;
- request-scoped arenas for transient native state;
- native response fast paths for text, JSON, status-only, and no-content results;
- prevalidated service, permission, and schema metadata;
- no string handler lookups in hot paths;
- no dynamic graph mutation in static plan mode.

## Concurrency Performance Model

Sloppy concurrency is mostly I/O concurrency inside one JS worker. JS callbacks execute
sequentially on that worker, so CPU-heavy JS blocks the worker. Native route matching,
preflight validation, request scopes, and data-provider scheduling reduce unnecessary JS hot
path work.

Future benchmarks should measure many pending I/O-bound requests without assuming
thread-per-request behavior, plus worker/isolates scaling once multicore execution exists.
See `docs/concurrency.md`.

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

No performance claim without a benchmark.

Benchmark list/smoke output only proves that the harness builds, starts, and exposes the
expected benchmark names. It is never performance evidence.

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

- cold start placeholder;
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

Benchmark names should use `bench_<path_or_claim>`, for example
`bench_route_match_static`.

## Internal Architecture

Likely layout:

```text
tests/benchmarks/
  README.md
  bench_plan_load.*
  bench_route_match.*
  bench_handler_dispatch.*
docs/performance.md
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

- no marketing/performance claim without linked benchmark data;
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
- docs explain that results are not product claims yet;
- CI either runs a smoke mode or documents why it is deferred.

## Open Questions

- Exact benchmark framework.
- Whether benchmark trend checks run in GitHub Actions or dedicated machines.
- Which baseline frameworks are fair for public comparisons.
- How to publish raw benchmark data.

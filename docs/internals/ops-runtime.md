# Operations Runtime

The operations runtime spans the bootstrap stdlib, app host, native dispatch,
provider executor, Plan metadata, CLI diagnostics, and benchmark harness.

## Health

`stdlib/sloppy/health.js` owns the health registry, result normalization,
bounded output, redaction, tag filtering, timeout handling, and cache handling.
`app.health()` wires that registry into GET routes.

`app.mapHealthChecks()` remains as the earlier compatibility API. It is kept
separate so older examples and compiler fixtures keep their previous response
shape.

## Metrics

`stdlib/sloppy/metrics.js` owns counters, gauges, histograms, timer helpers,
label normalization, cardinality guard behavior, JSON snapshots, and Prometheus
rendering.

The app test host records HTTP request metrics after route matching. Labels use
the route pattern from metadata, never the raw target path. The management
metrics endpoint refreshes uptime, memory, shutdown state, and route-table
gauges before rendering JSON or Prometheus output.

`include/sloppy/ops_metrics.h` and `src/core/ops_metrics.c` provide the native
registry used by C runtime surfaces. It supports counters, gauges, histograms,
bounded label cardinality, reset-for-test behavior, JSON snapshots, and
Prometheus text rendering. Metric names keep Sloppy's readable dots internally
and render Prometheus-compatible underscores at the exposition boundary.

`SlHttpDispatchTable.metrics` is an optional borrowed registry. When present,
native dispatch records request totals, active requests, route-pattern hits,
status totals, byte counts, duration histograms, routing-mode hits, and native
JSON request hits without using raw paths as labels.

`sl_provider_executor_record_metrics()` exports provider executor counters into
database/provider metric names. It reports queue depth, active and idle pool
slots, exhausted submissions, total submitted operations, failures, and
timeouts using provider kind and instance labels only.

## Management

`app.management()` is opt-in. It registers health, metrics, info, and runtime
routes under a configurable root path. The optional `protect` hook is evaluated
inside each management route handler.

Management output must not include raw config, environment values, request
headers, cookies, provider connection strings, SQL parameters, job payloads, or
stack traces.

The app host marks startup complete when the app is frozen for serving.
`Testing.createHost(app).close()` marks shutdown started before draining and
disposing app services, which lets readiness checks report the drain state.

## Validation Surface

Focused bootstrap coverage lives in `tests/bootstrap/test_ops_management.mjs`.
Seed-replay/property coverage for Prometheus escaping and redaction lives in
`tests/bootstrap/test_ops_properties.mjs`.
Native registry and dispatch coverage lives in `tests/unit/core/test_ops_metrics.c`,
`tests/unit/core/test_http_dispatch.c`, and
`tests/unit/core/test_provider_executor.c`.
The staged asset lists in CMake include `health.js` and `metrics.js` so package
and install lanes copy the operations layer with the rest of the bootstrap
stdlib.

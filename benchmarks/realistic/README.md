# Realistic Local Runtime Benchmarks

This suite compares Sloppy, Node.js, Bun, and Deno on local HTTP workloads for
engineering feedback. It is not a public performance claim.

The benchmark is useful when Sloppy loses. The goal is to identify where time
goes: V8 bridge materialization, route lookup, JSON/result serialization, HTTP
parser/write behavior, concurrency, memory, CPU, and generated metadata.

## Install runtimes

The runner itself uses Node.js as the load generator. Install Node before
running the suite. Bun and Deno are optional; missing runtimes are reported as
`UNAVAILABLE` unless you mark them required.

Sloppy must be a V8-enabled release-like build for fair runtime comparisons.
The runner looks for `build/windows-relwithdebinfo/sloppy.exe`,
`build/windows-release/sloppy.exe`, Unix equivalents, or `sloppy` on `PATH`.
Override with `--sloppy-exe` or `-SloppyExe`.

## Quick run

Windows:

```powershell
tools/windows/bench-realistic.ps1 -Quick -Runtime sloppy,node
```

Unix:

```sh
tools/unix/bench-realistic.sh --quick --runtime sloppy,node
```

Quick mode is a local sanity pass. It uses short timings and a small workload
subset so the harness can be checked without running a full benchmark matrix.

For startup experiments that intentionally exercise V8 app-script code caching,
set `SLOPPY_V8_CODE_CACHE_DIR` to a writable directory and record whether the
directory was empty or already warmed. Leave it unset for default cold-start
behavior.

## Normal run

Windows:

```powershell
tools/windows/bench-realistic.ps1 -Suite http -DurationSeconds 30 -WarmupSeconds 10 -Connections 64 -Iterations 5
tools/windows/bench-realistic.ps1 -Suite all -Runtime sloppy,node,bun,deno
tools/windows/bench-realistic.ps1 -Suite http -Workload health,json,route-param,large-routes
tools/windows/bench-realistic.ps1 -Out artifacts/bench/realistic
tools/windows/bench-realistic.ps1 -RequireRuntime sloppy,node,bun
```

Unix:

```sh
tools/unix/bench-realistic.sh --suite http --duration-seconds 30 --warmup-seconds 10 --connections 64 --iterations 5
tools/unix/bench-realistic.sh --suite all --runtime sloppy,node,bun,deno
tools/unix/bench-realistic.sh --out artifacts/bench/realistic
```

## App categories

The suite keeps app shapes separate:

- `baseline`: minimal native HTTP surface for Node, Bun, and Deno; minimal
  Sloppy app without benchmark-only middleware.
- `framework`: small equivalent route table, JSON responses, route params,
  query parsing, and request ID propagation.
- `feature-rich`: request ID, quiet middleware, CORS metadata, service/config
  style setup where supported. It remains labeled as feature-rich and should
  not be compared to baseline rows as if they were the same app.

Quick mode defaults to `framework`. Normal and full modes include all three
categories unless `--category` is supplied.

## Workloads

- `health`: `GET /health` returns `ok`.
- `json-small`: `GET /json` returns `{"message":"hello","ok":true,"count":42}`.
- `route-param`: `GET /users/123` returns `{"id":123,"name":"Ada Lovelace"}`.
- `query`: `GET /search?q=ada&page=2&limit=10` materializes query values.
- `post-json-small`: `POST /echo` parses a small JSON body and echoes fields.
- `middleware-request-id`: `GET /middleware` generates or propagates a request ID.
- `large-route-table-hit`: generated 100, 1000, and full-mode 5000 route tables,
  with first/middle/last hits.
- `large-route-table-miss`: generated route tables with a missing path.
- `static-ish-payload`: `GET /payload/64kb` returns a stable 64 KiB payload.
- `mixed-realistic`: weighted local mix of health/json, route params, query,
  POST JSON, and misses.

Use `large-routes` as a workload alias for both large-route hit and miss rows.

## Outputs

Reports are written under `artifacts/bench/realistic` by default:

- `results.json`: schema version 1 machine-readable report.
- `summary.md`: human-readable summary with environment, versions, caveats,
  workload definitions, result tables, and neutral relative deltas.
- `raw/`: stdout, stderr, load-generator JSON, and process samples per row.

Sloppy source-input apps are built once per app shape before measured request
throughput rows. Build duration and artifact sizes are recorded separately and
are not mixed into HTTP throughput timing.

## Fairness checklist

- Run on one machine without switching power profile mid-run.
- Use one load generator for every runtime.
- Keep the same concurrency, duration, warmup, iterations, payloads, endpoint
  behavior, keep-alive behavior, and logging settings.
- Compare rows only inside the same category and workload.
- Do not compare debug Sloppy builds to release-like Node, Bun, or Deno.
- Include the full JSON artifact when discussing numbers.
- Treat missing Bun, Deno, Node, or Sloppy as `UNAVAILABLE`, not as a failed
  runtime result, unless the command used `--require-runtime`.

## Adding a workload

Add the endpoint to each app category for each runtime, add the request and
validation entry in `runner/bench-realistic.mjs`, document the workload here,
and keep response status/body behavior equivalent. If a workload intentionally
uses a different app shape, give it a separate category or state the deviation
in the report.

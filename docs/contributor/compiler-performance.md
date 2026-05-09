# Compiler performance workflow

This page is for contributors measuring and improving `sloppyc`. Benchmark
output is local evidence for engineering decisions. It is not a public
performance claim, a release gate by itself, or correctness coverage.

## Commands

Run the smoke compiler benchmark:

```powershell
.\tools\windows\bench-compiler.ps1 -Suite smoke -Out artifacts\bench\compiler-smoke.json
```

Run scale benchmarks:

```powershell
.\tools\windows\bench-compiler.ps1 -Suite scale -Size tiny,small,medium,large -Out artifacts\bench\compiler-scale.json
```

Run the huge size manually when the machine has enough time and memory:

```powershell
.\tools\windows\bench-compiler.ps1 -Suite scale -Size huge -Out artifacts\bench\compiler-huge.json
```

Compare two reports from the same machine and similar load conditions:

```powershell
.\tools\windows\bench-compiler.ps1 -Compare artifacts\bench\compiler-before.json artifacts\bench\compiler-after.json
```

Unix keeps the same command shape:

```bash
./tools/unix/bench-compiler.sh --suite smoke --out artifacts/bench/compiler-smoke.json
./tools/unix/bench-compiler.sh --suite scale --size small,medium --out artifacts/bench/compiler-scale-smoke.json
./tools/unix/bench-compiler.sh --compare artifacts/bench/compiler-before.json artifacts/bench/compiler-after.json
```

Generated projects and benchmark reports live under `artifacts/` and must not be
committed.

## Scale project generator

The benchmark harness uses:

```powershell
node tools/compiler/generate-scale-project.mjs --size medium --out artifacts/compiler-scale/medium
```

You can generate a custom shape:

```powershell
node tools/compiler/generate-scale-project.mjs --files 100 --routes 1000 --schemas 200 --controllers 50 --services 100 --out artifacts/compiler-scale/custom
```

Project sizes are deterministic:

| Size | Files | Routes | Schemas | Services | Controllers |
| --- | ---: | ---: | ---: | ---: | ---: |
| tiny | 1 | 2 | 1 | 1 | 0 |
| small | 5 | 20 | 5 | 5 | 0 |
| medium | 25 | 200 | 50 | 25 | 10 |
| large | 100 | 1000 | 200 | 100 | 50 |
| huge | 500 | 5000 | 1000 | 500 | 250 |

Generated projects exercise the current compiler subset: relative modules,
route groups, static middleware, CORS, RequestId, RequestLogging, services,
config defaults, typed bindings, SQLite provider metadata, health checks, and
controller mappings. They do not use npm resolution, arbitrary TypeScript
checking, watch mode, or fake runtime APIs.

## Result schema

Compiler benchmark reports use `schemaVersion: 1`.

```json
{
  "schemaVersion": 1,
  "startedAt": "2026-05-09T00:00:00.000Z",
  "git": {
    "commit": "...",
    "branch": "compiler-perf-scalability",
    "dirty": true
  },
  "host": {
    "os": "Windows_NT ...",
    "arch": "x64",
    "cpu": "...",
    "logicalCores": 16
  },
  "compiler": {
    "version": "sloppyc 0.8.0",
    "path": "..."
  },
  "benchmarks": [
    {
      "id": "compiler.large.routes_1000.files_100",
      "status": "pass",
      "projectSize": "large",
      "files": 100,
      "routes": 1000,
      "schemas": 200,
      "services": 100,
      "controllers": 50,
      "durationMs": 1234,
      "peakWorkingSetBytes": 123456789,
      "artifacts": {
        "planBytes": 100000,
        "appJsBytes": 200000,
        "sourceMapBytes": 300000
      },
      "phases": {
        "readInputMs": 1,
        "parseEntryMs": 10,
        "parseModulesMs": 100,
        "resolveModuleGraphMs": 20,
        "extractMs": 200,
        "appGraphMs": 10,
        "planEmitMs": 50,
        "bundleEmitMs": 40,
        "sourceMapMs": 20,
        "writeMs": 5
      }
    }
  ]
}
```

`peakWorkingSetBytes` is sampled by the harness while `sloppyc` runs. Treat it
as process-level memory evidence, not allocator telemetry.

## Timing JSON

`sloppyc` can emit compiler timing details without changing normal output:

```powershell
cargo run --manifest-path compiler\Cargo.toml -- build examples\compiler-hello\app.js --out artifacts\compiler-hello --timings-json artifacts\bench\hello-timings.json
```

`--diagnostics-timing-json` is accepted as an alias for internal/dev tooling.
The timing JSON includes phase timings, source/artifact counters, and artifact
sizes. Timings are disabled unless a timing path is requested.

## Interpreting results

Use before/after comparisons from the same machine, compiler mode, and workload
shape. The useful evidence is the direction and likely cause of a change:
phase timing, artifact size, route count, source size, and memory movement.

Do not compare a laptop run to a CI VM, a Debug run to a Release run, or a smoke
run to a measured scale run and present the delta as meaningful.

## Regression gates

Default Cargo tests include a medium scale smoke test. It generates a 25-file,
200-route project, compiles it, parses the emitted Plan, asserts the route
count, and enforces a generous threshold through
`SLOPPYC_SCALE_SMOKE_THRESHOLD_MS` with a default of 30000 ms.

The default smoke threshold catches obvious exponential regressions. It is not a
microbenchmark and does not prove a performance improvement.

Recommended PR evidence:

- benchmark: smoke report from `bench-compiler.ps1 -Suite smoke`
- benchmark: small/medium scale report
- benchmark: large local report when the change claims a scale improvement
- benchmark: huge manual report only when practical
- compiler/Plan: `cargo test --manifest-path compiler\Cargo.toml`
- default non-V8: normal repository build/test/lint gates

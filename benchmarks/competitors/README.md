# Local JSON Competitor Benchmark

This is a local dev-machine harness for JSON route comparisons. It is not wired
into default CI and must not be used for public performance claims.

```sh
node benchmarks/competitors/json-local.mjs --iterations 100 --out artifacts/bench/json-competitors.json
```

On Windows, the wrapper is:

```powershell
.\tools\windows\bench-json-competitors.ps1 -Iterations 100
```

The harness records versions, OS/CPU details, SKIPPED entries for unavailable
runtimes or optional dependencies, and machine-readable JSON results.

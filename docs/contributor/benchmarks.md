# Benchmarks

Sloppy benchmarks are local engineering evidence. They are not release gates,
marketing copy, or official runtime rankings.

Use benchmark reports to answer specific engineering questions:

- where Sloppy is fast or slow;
- whether a branch changes V8 bridge overhead;
- whether route lookup scales as expected;
- where JSON/result serialization, HTTP parser/write paths, memory, or CPU
  behavior differ across runtimes;
- whether compiler-generated metadata helps a workload.

## Realistic HTTP comparisons

The realistic suite lives in `benchmarks/realistic/` and is launched through:

```powershell
tools/windows/bench-realistic.ps1 -Quick -Runtime sloppy,node
tools/windows/bench-realistic.ps1 -Suite http -Runtime sloppy,node,bun,deno
```

Unix wrapper:

```sh
tools/unix/bench-realistic.sh --quick --runtime sloppy,node
tools/unix/bench-realistic.sh --suite http --runtime sloppy,node,bun,deno
```

The runner detects installed runtimes, records versions and paths, reports
missing runtimes as `UNAVAILABLE`, validates responses before timing, starts
servers on free loopback ports, samples process memory where practical, and
writes:

- `artifacts/bench/realistic/results.json`;
- `artifacts/bench/realistic/summary.md`;
- raw stdout, stderr, load-generator data, and process samples under
  `artifacts/bench/realistic/raw/`.

Node.js is required because the load generator is implemented with Node's HTTP
client. Bun and Deno are optional comparators unless the command marks them as
required.

## App shapes

Do not compare unlike rows as if they were the same benchmark.

- `baseline` is the minimal HTTP surface.
- `framework` adds equivalent route matching, JSON, params, query parsing, and
  request ID behavior.
- `feature-rich` adds quiet middleware, request ID, CORS metadata, and service
  style setup where supported.

Compare Sloppy to Node/Bun/Deno within the same category, workload,
connection count, duration, and iteration policy.

## Startup and build

Steady-state HTTP rows build Sloppy artifacts before timing request throughput.
Build duration, artifact sizes, route counts, Plan kind, and required features
are recorded separately. Do not mix Sloppy compilation time into request
throughput numbers.

Use the `startup` suite when the timing boundary is process start to first
`/health` response:

```powershell
tools/windows/bench-realistic.ps1 -Suite startup -Runtime sloppy,node
```

## Reporting rules

Every benchmark discussion should include:

- command line;
- git commit and dirty state if known;
- Sloppy build preset and whether V8 is enabled;
- runtime versions;
- machine CPU, core count, memory, OS, and power/VM context if relevant;
- output paths for `results.json` and `summary.md`;
- any unavailable runtimes or skipped categories.

Use neutral language: "relative to", "delta", and "local run". Do not write
"beats Node", "faster than Bun", "best TypeScript runtime", or similar public
claims.

## CI

Full benchmarks are manual. Default PR CI should not run long performance
matrices. Cheap validation may check script syntax, runner `--dry-run`, or a
tiny smoke if the PR explicitly needs benchmark harness evidence.

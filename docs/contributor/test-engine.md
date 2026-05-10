# Test engine

The test engine is the repo-level wrapper for quality lanes that span more
than one tool. It does not replace CTest, Cargo, or the existing `dev.ps1` /
`dev.sh` commands. It records which lanes ran, which optional lanes were not
available, and the seed needed to replay randomized checks.

Use it when a PR needs combined evidence across static checks, native tests,
compiler fixtures, JavaScript property tests, fuzz seed replay, HTTP/2,
stress, packaging, sanitizers, V8, or provider lanes.

## Commands

Windows:

```powershell
.\tools\windows\test-engine.ps1 -Tier pr -Out artifacts\test-engine\pr.json
.\tools\windows\test-engine.ps1 -Tier extended -Area fuzz -Seed 12345
.\tools\windows\test-engine.ps1 -Tier torture -Area stress -StressSeconds 300
```

Unix:

```sh
tools/unix/test-engine.sh --tier pr --out artifacts/test-engine/pr.json
tools/unix/test-engine.sh --tier extended --area fuzz --seed 12345
tools/unix/test-engine.sh --tier torture --area stress --stress-seconds 300
```

The supported tiers are:

| Tier | Purpose |
| --- | --- |
| `pr` | Default PR evidence. Uses bounded property/fuzz iteration counts and skips package, sanitizer, provider, and V8 areas unless selected directly. |
| `extended` | Nightly or manual evidence. Runs longer fuzz/property counts and includes package, sanitizer, and provider areas when `all` is selected. |
| `torture` | Manual pressure evidence. Uses the largest default fuzz/property and stress budgets and also asks for the V8 area when `all` is selected. |

The supported areas are `all`, `static`, `native`, `compiler`, `js`, `fuzz`,
`http2`, `package`, `sanitizer`, `stress`, `v8`, `provider`, and `meta`.

`-Out` / `--out` writes a JSON report with:

- `schemaVersion`, `tier`, `area`, `seed`, timestamps, git metadata, and host
  metadata;
- one `lanes[]` entry per lane with `id`, `status`, `durationMs`, `command`,
  and `notes`;
- a `summary` count by status.

Report JSON uses lower-case status values: `pass`, `fail`, `skipped`, and
`unavailable`. PR evidence tables still use the repo evidence statuses:
`PASS`, `FAIL`, `SKIPPED`, `UNAVAILABLE`, `DEFERRED`, and `NOT RUN`.

## Fuzz runners

Windows:

```powershell
.\tools\windows\fuzz.ps1 -All -Iterations 1000 -Seed 12345
.\tools\windows\fuzz.ps1 -Target http-query -Iterations 10000 -Seed 12345
```

Unix:

```sh
tools/unix/fuzz.sh --all --iterations 1000 --seed 12345
tools/unix/fuzz.sh --target http-query --iterations 10000 --seed 12345
```

The fuzz runner has three layers:

- CTest seed replay for committed native corpora, reported as unavailable when
  the selected build preset has not been configured;
- native libFuzzer mutation for a selected native target when the libFuzzer
  preset has been built;
- JavaScript randomized/property targets with failure artifacts under
  `artifacts/fuzz/failures/`.

The current native targets are `plan`, `route-pattern`, `http-request`,
`http-query`, `http2-frame`, `http2-hpack`, `http2-session`,
`diagnostics-render`, and `memory-primitives`.

The current JavaScript targets are `config-json`, `openapi-plan`, `headers`,
`query-string`, `percent-decoding`, `logging-json`, `package-manifest`,
`route-table`, `required-features`, `http-client-options`, `results-headers`,
`worker-queue`, `h2-client-options`, and `stdlib-import-shapes`.

The bootstrap property runner covers `codec`, `results`, `time`,
`http-client`, `workers`, `logging`, and `config`.

Every randomized JavaScript lane accepts `--seed`, `--iterations`, and
`--failure-root`. Failures print the seed, target, iteration, and failure
artifact path. Use those fields as the reproduction handle in PR evidence.

## Current coverage payload

The default-safe PR fuzz/property payload now includes:

- codec roundtrips and validation for Base64, Base64Url, Hex, UTF-8, and
  binary reader/writer bounds;
- Results and ProblemDetails descriptor shape, body-forbidden status behavior,
  header validation, and location validation;
- Deadline, cancellation, timeout, and fake-clock ordering behavior;
- HttpClient option normalization and rejection paths that do not require a
  live network bridge;
- WorkQueue capacity, retry, drain, cancellation-before-start, and idempotent
  shutdown checks;
- logging redaction, escaping, level filtering, memory-sink capacity, and sink
  option validation;
- configuration object loading, binding, typed reads, and invalid key/type
  rejection;
- JavaScript fuzz targets for config JSON, Plan fragments, headers, query and
  percent decoding, logging JSON, package manifests, route tables, required
  features, HTTP/1 and HTTP/2 client options, result headers, worker queues,
  and stdlib import shapes;
- native seed replay corpora for Plan, route pattern, HTTP request, HTTP query,
  HTTP/2 frame/HPACK/session, diagnostics rendering, and memory primitives;
- focused CTest coverage for HTTP/2 multi-stream ordering, create/package
  failure paths, and test-engine report contracts.

## Meta coverage

The `meta` area verifies the wrapper contract itself: help output is available,
invalid arguments fail, and the JSON report shape uses the expected schema and
status values. The Windows CTest contract also parses the Unix wrapper report
when `bash` is available, verifies invalid areas, verifies unavailable lanes do
not fail the command, and checks that the extended and torture workflows remain
manual/scheduled according to their tier.

## Remaining gaps

The test engine does not make optional or heavyweight lanes part of normal PR
CI. ASan/libFuzzer mutation, V8 bridge execution, live provider services, and
long stress/torture runs remain extended or manual evidence unless a PR selects
that area directly. Missing optional tools or environment variables must be
reported as `UNAVAILABLE` or `SKIPPED`, never as a pass.

## Reporting

Use the test engine report as supporting evidence, not as a substitute for the
PR evidence table. Name the exact command, seed, tier, area, and output path.
If a lane reports `unavailable`, list the missing build preset, dependency,
service, or environment variable separately in the PR.

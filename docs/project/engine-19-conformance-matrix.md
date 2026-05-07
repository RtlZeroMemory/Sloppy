# ENGINE-19 Conformance Matrix

Status: current conformance evidence contract.

This matrix defines how Slop reports conformance and compatibility evidence. It is a
reporting and test-organization contract only. It does not add runtime behavior, package
release readiness, benchmark claims, public alpha docs, Node/npm compatibility, or
PostgreSQL/SQL Server JavaScript bridges.

## Evidence Rules

Evidence lanes must not be blended. A PR body, gate report, or status doc must name the
lane that actually ran and must list skipped optional lanes as skipped/not configured, not
as passed.

Required PR-body wording for applicable lanes:

- Default non-V8 evidence: list the default commands and CTest labels that ran.
- V8-gated evidence: say either "V8 SDK configured; V8-gated tests ran" or "V8 SDK not
  configured; V8-gated tests skipped/not configured."
- Localhost transport evidence: list the loopback transport tests that ran and state that
  they are not production-edge HTTP proof.
- SQLite/capability evidence: separate native provider/capability tests from V8-gated
  SQLite bridge or users API tests.
- Package outside-checkout evidence: say whether package smoke ran from an extracted
  package outside the repo checkout.
- Live-provider evidence: say whether PostgreSQL/SQL Server live provider gates were
  configured, skipped, or run.
- Stress/smoke evidence: say what safety or lifecycle behavior was exercised, with no
  throughput or latency claim.
- Benchmark harness evidence: say list/smoke or measured Release run; list/smoke is
  harness proof only.

## Evidence Lanes

| Lane | Purpose | What it proves | What it does not prove | Naming convention | Skip semantics | Reporting rules |
| --- | --- | --- | --- | --- | --- | --- |
| Default non-V8 | Protect the required local and CI path without requiring a V8 SDK. | C/C++ build, non-V8 CTest, compiler/Rust gates, static stdlib/example checks, native provider tests, and default conformance compile/reject behavior. | V8 execution, live providers, package smoke, benchmark performance, public alpha readiness, or production-edge HTTP. | `conformance.foundation.*` for broad default conformance; existing default names such as `conformance.hello.compile_artifacts` stay valid until renamed intentionally. | Required gates must pass; optional lanes not run by default are skipped/not configured. | PRs must call this "default non-V8 evidence" and must not use it as V8 or live-provider proof. |
| V8-gated | Prove behavior that requires a configured V8 SDK. | `sloppy run --artifacts`, handler execution, V8 bridge behavior, Promise/runtime behavior, and SQLite bridge behavior when registered as V8 tests. | Default gate success, package runtime layout, live PostgreSQL/SQL Server unless the live lane is also configured, or production HTTP readiness. | `conformance.v8.*`; existing labels include `conformance;v8`. | Missing V8 SDK means skipped/not configured, not passed. | Report the SDK availability and the exact V8 preset/CTest filter used. |
| Localhost transport | Prove bounded loopback TCP request/response behavior. | Raw localhost bytes, bounded request parsing, supported response serialization, cleanup, sequential keep-alive, scoped chunked handling, and opt-in HTTPS loopback where the transport lane ran. | Production TLS hardening, HTTP/2, HTTP/3, WebSockets, pipelining, public streaming APIs, reverse-proxy behavior, internet-edge production readiness, or benchmark performance. | `conformance.transport.*` for transport-specific cases; existing labels include `transport`. | If the host cannot bind localhost or the relevant transport target is absent, report skipped/not configured. | State whether the test used real loopback TCP/TLS or synthetic dispatch. |
| SQLite/capability | Prove SQLite provider/bridge and capability enforcement behavior. | Native SQLite provider mapping, native capability registry denial before work, and V8-gated SQLite bridge/users API behavior when those tests run. | PostgreSQL live bridge behavior, SQL Server JS bridge behavior, ORM/migrations, async/offloaded SQLite, live DBs, or public alpha readiness. | `conformance.sqlite.*` and `conformance.capability.*`; existing names such as `conformance.sqlite.bridge` remain valid. | Native default tests are required; V8 bridge/users API proof is skipped/not configured without V8. Live PostgreSQL/SQL Server absence is skipped/not configured, not passed. | Separate native provider/capability, V8 SQLite bridge, localhost users API, and optional live-provider results. |
| Package outside-checkout | Prove an experimental local package layout starts and basic commands work after extraction outside the checkout. | Package archive contents, stdlib/bootstrap assets, manifest/checksum layout, packaged `sloppy`/`sloppyc` startup, and scoped outside-checkout compile/run smoke when implemented. | Release readiness, installer/signing/notarization, package-manager distribution, V8 execution unless the V8 package lane ran, live providers, or public alpha readiness. | `conformance.package.*`; package scripts may also report package-smoke commands outside CTest. | If package smoke is unavailable or not run, report skipped/not configured. | PRs must say "package smoke is local package layout/execution evidence, not release readiness." |
| Live-provider optional | Prove real external PostgreSQL or SQL Server service behavior when explicitly configured. | Env-gated provider behavior for configured live services; PostgreSQL includes native libpq and V8 stdlib bridge evidence when that lane runs. | Default provider behavior, SQLite behavior, SQL Server JS bridge readiness, package readiness, or skipped-service success. | Labels `live-provider;postgres` and `live-provider;sqlserver`; future conformance names should use `conformance.live_provider.*` only for live-gated tests. | Missing env vars, drivers, or services must use CTest skip code 77 or equivalent skipped/not configured reporting. | Redact secrets and name only the configured provider lane. SQLite proof is not PostgreSQL/SQL Server proof. |
| Stress/smoke | Exercise bounded repeated operations and cleanup/lifecycle invariants. | Deterministic overflow, cleanup-once, cancellation/shutdown safety, lifecycle coherence, or harness startup. | Throughput, latency, scalability, external comparison, benchmark performance, or production readiness. | `smoke.*` for smoke-only tests; stress targets may keep module names but must document the lane. | Stress/smoke that is optional must be reported as skipped/not configured if not run. | Use "stress/smoke evidence" and avoid performance language. |
| Benchmark harness | Prove benchmark commands and benchmark-target selection work. | `--list`, tiny smoke execution, JSON output shape, and measured Release runs only when explicitly scoped. | Correctness coverage, public performance claims, throughput promises, or regression gates unless a later task defines them. | `benchmark.*` for benchmark CTests and `benchmarks.*` for current CTest names. | Benchmark list/smoke can pass as harness proof; unavailable measured Release runs are skipped/not configured. | State "benchmark list/smoke is harness proof only" unless a measured Release methodology is reported. |

## Test Naming Policy

New ENGINE-19 conformance tests should prefer these names:

- `conformance.foundation.*` for default non-V8 compiler/runtime compatibility checks;
- `conformance.v8.*` for V8-required runtime execution;
- `conformance.http.*` for HTTP parser, dispatch, body, header, and response behavior that
  is not transport-specific;
- `conformance.transport.*` for localhost TCP transport behavior;
- `conformance.sqlite.*` for SQLite provider or bridge behavior;
- `conformance.capability.*` for capability allow/deny behavior;
- `conformance.package.*` for outside-checkout package layout/execution smoke;
- `smoke.*` for smoke-only lifecycle or harness checks;
- `benchmark.*` for benchmark harness or measured benchmark checks.

Existing tests do not need churn-only renames. Existing CTest names such as
`conformance.hello.compile_artifacts`, `conformance.request_context.run_once`,
`conformance.sqlite.native_provider`, `conformance.sqlite.bridge`,
`conformance.capability.native_registry`,
`conformance.users_api_sqlite.localhost_transport`, `conformance.transport.keep_alive`,
`conformance.transport.keep_alive_idle_timeout`,
`conformance.transport.keep_alive_max_requests`, `conformance.transport.lifecycle_reset`,
`conformance.transport.chunked_request`, `conformance.transport.streaming_response`,
`conformance.transport.backpressure`, `conformance.transport.shutdown_cancel`,
`smoke.transport.keep_alive_streaming_bounded`, and
`benchmarks.sloppy_bench.smoke_json` remain acceptable while their labels and docs make the
lane clear.

CTest labels should match the reporting lane where practical:

- `conformance` for conformance cases;
- `v8` for V8-gated cases;
- `transport` for localhost transport cases;
- `sqlite` for SQLite cases;
- `capability` for capability cases;
- `package` for package smoke;
- `live-provider`, plus provider-specific labels such as `postgres` or `sqlserver`;
- `smoke` for smoke-only checks;
- `benchmark` for benchmark harness checks.

## Skip Semantics

Skipped optional gates are not pass claims.

- V8 SDK missing: V8-gated tests are skipped/not configured, not passed.
- PostgreSQL/SQL Server live service, driver, or secret missing: live provider tests are
  skipped/not configured, not passed.
- Package smoke unavailable or not run: package outside-checkout lane is skipped/not
  configured, not passed.
- Benchmark list/smoke: harness proof only, no performance claim.
- Stress/smoke: bounded safety or lifecycle proof only, no performance claim.
- Localhost transport: loopback MVP proof only, no production-edge HTTP claim.
- HTTP-25.F transport: bounded keep-alive/chunked/streaming conformance and stress smoke,
  no benchmark/performance claim and no TLS/HTTP2/HTTP3/WebSocket/SSE/multipart/
  compression/static-file/reverse-proxy or production-edge claim.
- SQLite users API proof: SQLite proof only, no PostgreSQL/SQL Server JS bridge claim.

## PR Body Checklist

Every ENGINE-19 PR body must include the relevant evidence lanes and these phrases when the
lane is in scope:

- "Default non-V8 evidence"
- "V8-gated evidence"
- "localhost transport evidence"
- "SQLite/capability evidence"
- "package outside-checkout evidence"
- "live-provider optional evidence"
- "stress/smoke evidence"
- "benchmark harness evidence"
- "Skipped optional gates are not pass claims"

If a lane is out of scope, say so in Non-goals or Evidence Boundaries rather than omitting
it in a way that could be mistaken for success.

# Capability Conformance

Status: native registry/provider-admission policy covered; SQLite bridge enforcement
V8-gated.

Source fixtures: `tests/unit/core/test_capability.c`,
`tests/unit/core/test_provider_executor.c`, `tests/unit/engine/test_v8_smoke.c`,
`tests/integration/execution/sqlite_bridge/`, and
`tests/integration/execution/sqlite_denied_capability/`.

Default evidence:

```powershell
ctest -R "conformance.capability.(native_registry|provider_executor)" --output-on-failure
```

Expected behavior:

- allowed database read/write/readwrite capabilities permit matching operations;
- missing database capability fails before the fake provider operation is called;
- insufficient read/write access fails before provider work;
- provider mismatch fails before provider work;
- provider executor admission runs capability checks before queue slot reservation,
  ownership transfer, or worker execution;
- denied provider executor admission leaves no resource or queued operation to clean up;
- filesystem/network capability entries remain metadata/check-only skeletons;
- denied diagnostics are redacted.
- V8 SQLite bridge `open`, `exec`, `query`, and `queryOne` check declared capabilities
  before SQLite provider work when the bridge is enabled.

V8-gated evidence:

```powershell
ctest -R "conformance.sqlite.(bridge|denied_capability)|conformance.users_api_sqlite.localhost_transport" --output-on-failure
```

The V8-gated suite covers allowed SQLite bridge use, read-only/write denial, write-only/read
denial, missing capability registry failure, missing provider failure, provider-kind
mismatch, stale handle failure, and denied users API transport behavior.

Gated/deferred requirements: SQLite bridge enforcement executes only in V8-enabled builds.
PostgreSQL and SQL Server JavaScript provider bridges remain deferred and must not be
reported as capability-enforced from JavaScript. Filesystem/network checks are metadata
checks only until their APIs exist.

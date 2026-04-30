# Capability Conformance

Status: native policy covered; SQLite bridge enforcement V8-gated.

Source fixtures: `tests/unit/core/test_capability.c`,
`tests/unit/engine/test_v8_smoke.c`, and `tests/integration/execution/sqlite_bridge/`.

Default evidence:

```powershell
ctest -R core.capability.registry --output-on-failure
```

Expected behavior:

- missing database capability fails before the fake provider operation is called;
- insufficient read/write access fails before provider work;
- provider mismatch fails before provider work;
- filesystem/network capability entries remain metadata/check-only skeletons;
- denied diagnostics are redacted.
- V8 SQLite bridge `open`, `exec`, `query`, and `queryOne` check declared capabilities
  before SQLite provider work when the bridge is enabled.

Gated evidence: SQLite bridge enforcement executes only in V8-enabled builds. PostgreSQL
and SQL Server JavaScript provider bridges remain deferred and must not be reported as
capability-enforced from JavaScript.

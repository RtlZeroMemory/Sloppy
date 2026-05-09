# PostgreSQL Basic Example

This is an API-shape example for the PostgreSQL provider.

It shows the intended module/capability/service registration shape, PostgreSQL `$1`
query-template lowering, a simple bounded pool option, and transaction usage.

Current limitations:

- requires PostgreSQL and a connection string such as `SLOPPY_POSTGRES_TEST_URL`;
- not part of default CI live database execution;
- the V8 bridge uses nonblocking libpq socket readiness and a bounded connection pool;
- parameterized exec/query/queryOne and callback transactions are supported through the
  stdlib bridge when the Plan enables `provider.postgres`;
- no migrations;
- no ORM;
- TLS option hardening and advanced operational pooling policy remain separate provider
  hardening work.

Native live provider tests are opt-in:

```powershell
$env:SLOPPY_POSTGRES_TEST_URL="<redacted PostgreSQL connection string>"
.\tools\windows\test-live-postgres.ps1
```

Do not paste credentials into PR bodies or diagnostics. Connection strings must be redacted
before reporting.

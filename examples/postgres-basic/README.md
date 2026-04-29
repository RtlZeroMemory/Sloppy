# PostgreSQL Basic Example

This is a static API-shape example for the EPIC-17 PostgreSQL provider.

It shows the intended module/capability/service registration shape, PostgreSQL `$1`
query-template lowering, a simple bounded pool option, and transaction usage.

Current limitations:

- requires PostgreSQL and a connection string such as `SLOPPY_POSTGRES_TEST_URL`;
- not part of default CI live database execution;
- no migrations;
- no ORM;
- pool behavior is a small bounded skeleton, not production pooling;
- async libpq socket integration, worker-pool offload, cancellation, deadlines, TLS option
  hardening, arrays, JSON, and blobs are deferred;
- JavaScript-to-native data intrinsics are not wired yet, so this example is not runnable
  through `sloppy run` today.

Native live provider tests are opt-in:

```powershell
$env:SLOPPY_POSTGRES_TEST_URL="postgres://postgres:postgres@localhost:5432/sloppy_test"
.\tools\windows\dev.ps1 test
```

Do not paste credentials into PR bodies or diagnostics. Connection strings must be redacted
before reporting.

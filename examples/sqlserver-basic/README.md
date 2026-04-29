# SQL Server Basic Example

This is a static API-shape example for the EPIC-18 SQL Server provider.

It shows the intended module/capability/service registration shape, ODBC `?`
query-template lowering, a simple bounded pool option, transaction usage, and the
SQL Server doctor helper shape.

Current limitations:

- requires SQL Server and Microsoft ODBC Driver 18 for SQL Server;
- uses ODBC through the native provider;
- set `SLOPPY_SQLSERVER_TEST_CONNECTION_STRING` or equivalent config for live use;
- not part of default CI live database execution;
- no migrations;
- no ORM;
- pool behavior is a small bounded skeleton, not production pooling;
- async ODBC, worker-pool offload, cancellation, deadlines, TLS/auth hardening,
  table-valued parameters, bulk copy, blobs, and date/time mapping are deferred;
- JavaScript-to-native data intrinsics are not wired yet, so this example is not runnable
  through `sloppy run` today.

Native live provider tests are opt-in:

```powershell
$env:SLOPPY_SQLSERVER_TEST_CONNECTION_STRING="Driver={ODBC Driver 18 for SQL Server};Server=localhost;Database=sloppy_test;UID=sa;PWD=<secret>;TrustServerCertificate=yes;"
.\tools\windows\dev.ps1 test
```

Do not paste credentials into PR bodies or diagnostics. Connection strings must be redacted
before reporting. Use `TrustServerCertificate=yes` only for local development when that is
appropriate for the server you are connecting to.

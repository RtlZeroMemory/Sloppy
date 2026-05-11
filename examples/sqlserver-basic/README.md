# SQL Server Basic Example

This example demonstrates SQL Server provider wiring: module/capability setup,
ODBC `?` query-template lowering, a bounded pool option, transaction usage, and the
SQL Server doctor helper shape.

Current limitations:

- requires SQL Server and Microsoft ODBC Driver 18 for SQL Server, or
  Microsoft ODBC Driver 17 for SQL Server;
- this example requires SQL Server, Microsoft ODBC Driver 17 or 18, and a live
  connection string;
- normal Sloppy apps, the Quickstart, SQLite, templates, and package support do
  not require SQL Server or ODBC;
- uses true-async ODBC connection/statement mode through the V8 provider bridge when the
  configured driver supports async completion;
- set `SLOPPY_SQLSERVER_TEST_CONNECTION_STRING` or equivalent config for live use;
- not part of default CI live database execution;
- no migrations;
- no ORM;
- pooling is bounded and provider-owned, not an ORM/session abstraction;
- unsupported drivers report SQL Server async-driver unavailability instead of falling
  back to a blocking pool and calling it true async;
- TLS/auth hardening, table-valued parameters, bulk copy, richer date/time mapping, and
  advanced pooling policies remain separate provider-hardening work.

Native live provider tests are opt-in:

```powershell
$env:SLOPPY_SQLSERVER_TEST_CONNECTION_STRING="<redacted SQL Server connection string>"
.\tools\windows\test-live-sqlserver.ps1
```

Do not paste credentials into PR bodies or diagnostics. Connection strings must be redacted
before reporting. Use `TrustServerCertificate=yes` only for local development when that is
appropriate for the server you are connecting to.

# Framework v2 SQL Server CRUD Example

Status: opt-in live-lane Framework v2 SQL Server example with honest unavailable
diagnostics when the local ODBC lane is not configured.

This example documents the Plan-visible Framework v2 shape for SQL Server CRUD-style
handlers and passes request deadlines into provider calls where the live provider/runtime
lane supports them.

It is not part of default CI. Run it only with a local SQL Server test database,
Microsoft ODBC Driver 18 for SQL Server, and a redacted connection string managed outside
source control. Missing driver/live configuration must be reported as unavailable diagnostics, not pass evidence.

```powershell
$env:SLOPPY_SQLSERVER_TEST_CONNECTION_STRING="Driver={ODBC Driver 18 for SQL Server};Server=localhost;Database=sloppy_test;UID=sa;PWD=<secret>;TrustServerCertificate=yes;"
.\tools\windows\test-live-sqlserver.ps1
```

This is not an ORM, migration system, production database policy, public alpha claim,
benchmark, package-manager behavior, or Node/Bun/Deno compatibility proof.

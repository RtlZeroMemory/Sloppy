# How To Run Live SQL Server Checks

Run the opt-in SQL Server live-provider test lane on Windows.

This lane is for live SQL Server provider execution. It is separate from
compiler metadata checks, default native tests, and non-live framework examples.

## Prerequisites

- Docker CLI available (unless using `-NoDocker`).
- Microsoft ODBC Driver 18 or 17 for SQL Server installed.
- A build preset (default `windows-relwithdebinfo`).

## Steps

1. Run the live lane with Docker-managed SQL Server.

```powershell
.\tools\windows\test-live-sqlserver.ps1
```

2. Or run against an existing SQL Server service.

```powershell
$env:SLOPPY_SQLSERVER_TEST_CONNECTION_STRING = "Driver={ODBC Driver 18 for SQL Server};Server=tcp:127.0.0.1,1433;Database=sloppy_test;UID=<user>;PWD=<password>;Encrypt=yes;TrustServerCertificate=yes;"
$env:Sloppy__Providers__sqlserver__main__connectionString = $env:SLOPPY_SQLSERVER_TEST_CONNECTION_STRING
.\tools\windows\test-live-sqlserver.ps1 -NoDocker
```

The script runs:

```powershell
ctest --test-dir build\windows-relwithdebinfo --output-on-failure -R "data\.sqlserver\.live_provider|conformance\.sqlserver\.(native_live|bridge_live)"
```

## Expected Result

- CTest reports passing results for the matched SQL Server live-provider tests.
- Matched tests include native live checks and V8 bridge live checks when the
  V8-enabled preset and ODBC async behavior are available.
- Docker mode starts SQL Server, applies `tests/live/sqlserver/init.sql`, and tears the container down.

## Common Failures

- `UNAVAILABLE: Microsoft ODBC Driver 18 or 17 for SQL Server is required for the SQL Server live-provider lane.`
- `UNAVAILABLE: Docker CLI is required for the SQL Server live-provider lane.`
- `driver async support is unavailable`: true-async bridge evidence is skipped in this environment.

# How To Run Live SQL Server Checks

Run the SQL Server integration checks on Windows.

These checks use a real SQL Server database, so they need the Microsoft ODBC
Driver and either Docker or an existing SQL Server instance.

## Prerequisites

- Docker CLI available (unless using `-NoDocker`).
- Microsoft ODBC Driver 18 or 17 for SQL Server installed.
- A build preset (default `windows-relwithdebinfo`).

## Steps

1. Run the checks with Docker-managed SQL Server.

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

- CTest reports passing results for the matched SQL Server integration tests.
- Matched tests include native checks and V8 bridge checks when the
  V8-enabled preset and ODBC async behavior are available.
- Docker mode starts SQL Server, applies `tests/live/sqlserver/init.sql`, and tears the container down.

## Common Failures

- `UNAVAILABLE: Microsoft ODBC Driver 18 or 17 for SQL Server is required for the SQL Server integration checks.`
- `UNAVAILABLE: Docker CLI is required for the SQL Server integration checks.`
- `driver async support is unavailable`: the installed driver cannot run the
  true-async SQL Server bridge check in this environment.

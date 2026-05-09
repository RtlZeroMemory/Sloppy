# How To Run Live PostgreSQL Checks

Run the PostgreSQL integration checks on Windows.

These checks use a real PostgreSQL service, so they need Docker or an existing
database and connection credentials.

## Prerequisites

- Docker CLI available (unless using `-NoDocker`).
- A build preset (default `windows-relwithdebinfo`).
- Disposable PostgreSQL database credentials.

## Steps

1. Run the checks with Docker-managed PostgreSQL.

```powershell
.\tools\windows\test-live-postgres.ps1
```

2. Or run against an existing PostgreSQL service.

```powershell
$env:SLOPPY_POSTGRES_TEST_URL = "postgres://<user>:<password>@127.0.0.1:5432/sloppy_test"
$env:Sloppy__Providers__postgres__main__connectionString = $env:SLOPPY_POSTGRES_TEST_URL
.\tools\windows\test-live-postgres.ps1 -NoDocker
```

The script runs:

```powershell
ctest --test-dir build\windows-relwithdebinfo --output-on-failure -R "data\.postgres\.live_provider|conformance\.postgres\.(native_live|bridge_live)"
```

## Expected Result

- CTest reports passing results for the matched PostgreSQL integration tests.
- Matched tests include native checks and V8 bridge checks when the
  V8-enabled preset is available.
- Docker mode starts and tears down `tests/live/postgres/compose.yml`.

## Common Failures

- `UNAVAILABLE: Docker CLI is required for the PostgreSQL integration checks.`
- Connection/auth failures against your external PostgreSQL service when using `-NoDocker`.
- Default non-live checks do not connect to PostgreSQL; use this page when you
  need to verify a real PostgreSQL service.

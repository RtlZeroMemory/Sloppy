# How To Run Live PostgreSQL Checks

Run the opt-in PostgreSQL live-provider test lane on Windows.

## Prerequisites

- Docker CLI available (unless using `-NoDocker`).
- A build preset (default `windows-relwithdebinfo`).
- Disposable PostgreSQL database credentials.

## Steps

1. Run the live lane with Docker-managed PostgreSQL.

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

- CTest reports passing results for the matched PostgreSQL live-provider tests.
- Docker mode starts and tears down `tests/live/postgres/compose.yml`.

## Common Failures

- `UNAVAILABLE: Docker CLI is required for the PostgreSQL live-provider lane.`
- Connection/auth failures against your external PostgreSQL service when using `-NoDocker`.
- Treating default non-live test results as PostgreSQL live evidence.

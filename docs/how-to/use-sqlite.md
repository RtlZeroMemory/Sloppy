# How To Use SQLite

Run the SQLite users API example and verify real query output.

## Prerequisites

- Repository checkout with `examples/users-api-sqlite`.
- V8-enabled runtime build:

```powershell
.\tools\windows\resolve-v8-sdk.ps1
.\tools\windows\dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8
.\tools\windows\dev.ps1 build -Preset windows-relwithdebinfo
```

## Steps

1. Run one request against the example from repo root.

```powershell
.\build\windows-relwithdebinfo\sloppy.exe run examples/users-api-sqlite/app.js --once GET /users
```

2. Or run from the example directory using `sloppy.json`.

```powershell
cd examples/users-api-sqlite
..\..\build\windows-relwithdebinfo\sloppy.exe run --once GET /users
```

## Expected Result

- The response is `HTTP/1.1 200 OK`.
- The JSON body includes seeded users (`Ada Lovelace`, `Grace Hopper`).
- The SQLite file path comes from `Sloppy:Providers:sqlite:main:database` in appsettings.

## Common Failures

- `sloppy run: sloppy run requires V8-enabled build`.
- Missing provider database config for `sqlite:main`: add `Sloppy:Providers:sqlite:main:database` in appsettings.
- Running non-SQLite API-shape examples: API-shape examples alone are not live SQLite execution evidence.

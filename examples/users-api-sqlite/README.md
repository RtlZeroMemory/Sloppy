# SQLite Users API Example

## What This Demonstrates

This example shows a small SQLite-backed users API using the supported classic
artifact path:

- `GET /health`;
- `GET /users`;
- `GET /users/{id}`;
- `POST /users`;
- explicit `sqlite("main")` provider registration;
- a relative function module under `modules/users.js`;
- `sloppy.json` and `appsettings.json` project configuration.

## Status

This is a V8-gated framework example used by the localhost transport conformance
lane and source-input fixture checks.

## Requirements

- A V8-enabled `sloppy` runtime.
- SQLite bridge support in that runtime.

## Run Command

From this example directory:

```powershell
..\..\build\windows-relwithdebinfo\sloppy.exe run --once GET /users
```

From the repository root:

```powershell
.\build\windows-relwithdebinfo\sloppy.exe run examples/users-api-sqlite/app.js --once GET /users
```

## Expected Result

The response body is JSON with the seeded users. The exact rows can include
users inserted by earlier requests when the configured database file is reused.

```json
[{"id":1,"name":"Ada Lovelace","email":"ada@example.test"},{"id":2,"name":"Grace Hopper","email":"grace@example.test"}]
```

## What To Inspect

- `app.js`: app creation, SQLite descriptor registration, and route wiring.
- `modules/users.js`: `app.provider("sqlite:main")` and users route handlers.
- `sloppy.json`: source-input project configuration.
- `appsettings.json`: SQLite provider database setting.
- Generated `.sloppy/app.plan.json`: route, module, provider, capability, and
  source-location metadata.

After building artifacts, these commands inspect the Plan:

```powershell
..\..\build\windows-dev\sloppy.exe routes --plan .sloppy\app.plan.json
..\..\build\windows-dev\sloppy.exe capabilities --plan .sloppy\app.plan.json
..\..\build\windows-dev\sloppy.exe doctor --plan .sloppy\app.plan.json
..\..\build\windows-dev\sloppy.exe audit --plan .sloppy\app.plan.json --format json
..\..\build\windows-dev\sloppy.exe openapi --plan .sloppy\app.plan.json
```

## Limitations

This example focuses on SQLite provider integration and localhost transport flow. PostgreSQL
and SQL Server are covered by separate examples, and streaming APIs are not included in
this slice.

## Related Docs

- `docs/tutorials/sqlite-api.md`
- `docs/reference/providers.md`
- `docs/reference/framework.md`
- `docs/how-to/use-sqlite.md`

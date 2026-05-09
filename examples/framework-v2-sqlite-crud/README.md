# Framework v2 SQLite CRUD Example

## What This Demonstrates

This example shows a small Framework v2 users API backed by SQLite:

- typed `Body<T>` and `Route<T>` handler parameters;
- `Sqlite<"main">` typed provider injection;
- provider config from `appsettings.json`;
- request `signal` and `deadline` passed to database calls;
- `Results.ok`, `Results.notFound`, and `Results.created`.

## Requirements

- A V8-enabled `sloppy` runtime.
- The SQLite provider bridge available in that runtime.

## Run

From the repository root:

```powershell
.\build\windows-relwithdebinfo\sloppy.exe run examples/framework-v2-sqlite-crud/app.ts --once GET /users
```

## Expected Result

The response body is a JSON array with the seeded users:

```json
[{"id":1,"name":"Ada Lovelace","email":"ada@example.test"},{"id":2,"name":"Grace Hopper","email":"grace@example.test"}]
```

## What To Inspect

- `app.ts`: typed handlers, SQLite injection, seed data, and CRUD routes.
- `appsettings.json`: provider configuration for `sqlite/main`.
- Generated `.sloppy/app.plan.json`: inferred provider/capability metadata and
  route bindings after `sloppy build`.

## Current Limits

This example is not an ORM or migration layer. It covers the SQLite Framework v2
path for the current runtime. PostgreSQL and SQL Server examples need external
database setup.

## Related Docs

- `docs/reference/providers.md`
- `docs/reference/framework.md`
- `docs/explanation/provider-runtime-model.md`
- `docs/how-to/use-sqlite.md`

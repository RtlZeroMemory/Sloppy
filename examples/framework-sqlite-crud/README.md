# Framework SQLite CRUD Example

## What this shows

A small users API backed by SQLite:

- typed `Body<T>` and `Route<T>` handler parameters;
- `Sqlite<"main">` typed provider injection;
- provider config from `appsettings.json`;
- request `signal` and `deadline` passed through to database calls;
- `Results.ok`, `Results.notFound`, and `Results.created`.

## Requirements

- A V8-enabled `sloppy` runtime.
- The SQLite provider bridge available in that runtime.

## Run

From the repository root:

```powershell
.\build\windows-relwithdebinfo\sloppy.exe run examples/framework-sqlite-crud/app.ts --once GET /users
```

## Expected response

A JSON array of seeded users:

```json
[{"id":1,"name":"Ada Lovelace","email":"ada@example.test"},{"id":2,"name":"Grace Hopper","email":"grace@example.test"}]
```

## Files to look at

- `app.ts` — typed handlers, SQLite injection, seed data, and CRUD routes.
- `appsettings.json` — provider configuration for `sqlite/main`.
- Generated `.sloppy/app.plan.json` — inferred provider/capability metadata
  and route bindings after `sloppy build`.

## Scope

This is not an ORM or migration layer. PostgreSQL and SQL Server versions of the
same shape live in separate examples and need their own database setup.

## Related docs

- `docs/api/data.md`
- `docs/guide/sqlite.md`
- `docs/reference/providers.md`
- `docs/reference/framework.md`
- `docs/internals/provider-runtime.md`

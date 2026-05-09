# Framework v2 PostgreSQL CRUD Example

## What This Demonstrates

This example shows the Framework v2 shape for a users API backed by PostgreSQL:

- typed `Body<T>` and `Route<T>` handler parameters;
- `Postgres<"main">` typed provider injection;
- PostgreSQL placeholders (`$1`, `$2`);
- request `signal` and `deadline` passed to database calls;
- `Results.ok`, `Results.notFound`, and `Results.created`.

## Status

This is an opt-in live-provider example. It needs a PostgreSQL service and
provider connection string at runtime.

## Requirements

- A V8-enabled `sloppy` runtime.
- A PostgreSQL test database with a `users` table.
- `Sloppy__Providers__postgres__main__connectionString` set in the environment.

Example table shape:

```sql
create table users (
  id serial primary key,
  name text not null,
  email text not null unique
);
```

## Run Command

Use the live PostgreSQL lane:

```powershell
$env:Sloppy__Providers__postgres__main__connectionString = "postgres://<user>:<password>@<host>:<port>/<db>"
.\tools\windows\test-live-postgres.ps1
```

## Expected Result

The live lane runs the PostgreSQL native and bridge tests selected by:

```powershell
ctest --test-dir build\windows-relwithdebinfo --output-on-failure -R "data\.postgres\.live_provider|conformance\.postgres\.(native_live|bridge_live)"
```

With the table and connection string configured, `GET /users` returns JSON rows
from the PostgreSQL database.

## What To Inspect

- `app.ts`: `Postgres<"main">` injection and PostgreSQL SQL statements.
- Generated `.sloppy/app.plan.json`: inferred `postgres/main` provider metadata.
- `docs/how-to/run-live-postgres-checks.md`: live-lane setup.

## Limitations

Schema setup is manual for this example. It depends on a working live PostgreSQL
service and valid credentials.

## Related Docs

- `docs/reference/providers.md`
- `docs/reference/framework.md`
- `docs/how-to/run-live-postgres-checks.md`
- `docs/explanation/provider-runtime-model.md`

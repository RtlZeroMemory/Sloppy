# Framework v2 PostgreSQL CRUD Example

## What This Demonstrates

This example shows the Framework v2 shape for a users API backed by PostgreSQL:

- typed `Body<T>` and `Route<T>` handler parameters;
- `Postgres<"main">` typed provider injection;
- PostgreSQL placeholders (`$1`, `$2`);
- request `signal` and `deadline` passed to database calls;
- `Results.ok`, `Results.notFound`, and `Results.created`.

## Requirements

- A V8-enabled `sloppy` runtime.
- A PostgreSQL service with a test database and `users` table.
- `Sloppy__Providers__postgres__main__connectionString` set in the environment.

Example table shape:

```sql
create table users (
  id serial primary key,
  name text not null,
  email text not null unique
);
```

## Run

Set the connection string and run the PostgreSQL integration checks:

```powershell
$env:Sloppy__Providers__postgres__main__connectionString = "postgres://<user>:<password>@<host>:<port>/<db>"
.\tools\windows\test-live-postgres.ps1
```

## Expected Result

The script runs the PostgreSQL native and bridge tests selected by:

```powershell
ctest --test-dir build\windows-relwithdebinfo --output-on-failure -R "data\.postgres\.live_provider|conformance\.postgres\.(native_live|bridge_live)"
```

With the table and connection string configured, `GET /users` returns JSON rows
from the PostgreSQL database.

## What To Inspect

- `app.ts`: `Postgres<"main">` injection and PostgreSQL SQL statements.
- Generated `.sloppy/app.plan.json`: inferred `postgres/main` provider metadata.
- `docs/api/data.md`: PostgreSQL setup and connection string convention.

## Current Limits

Schema setup is manual for this example. Migrations, ORM-style modeling,
deployment guidance, and package dependency support are future work.

## Related Docs

- `docs/api/data.md`
- `docs/reference/providers.md`
- `docs/reference/framework.md`
- `docs/internals/provider-runtime.md`

# Framework PostgreSQL CRUD Example

## What this shows

A users API backed by PostgreSQL:

- typed `Body<T>` and `Route<T>` handler parameters;
- `Postgres<"main">` typed provider injection;
- PostgreSQL placeholders (`$1`, `$2`);
- request `signal` and `deadline` passed to database calls;
- `Results.ok`, `Results.notFound`, and `Results.created`.

## Requirements

- A V8-enabled `sloppy` runtime.
- A PostgreSQL service with a test database and `users` table.
- PostgreSQL client support. Current alpha packages use system or
  build-provided libpq; package-local PostgreSQL provider packages are not
  shipped yet.
- `Sloppy__Providers__postgres__main__connectionString` set in the environment.

You do not need PostgreSQL or libpq for normal Sloppy apps, the Quickstart,
Program Mode, SQLite, templates, or package support.

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

## Expected result

The script runs the PostgreSQL native and bridge tests selected by:

```powershell
ctest --test-dir build\windows-relwithdebinfo --output-on-failure -R "data\.postgres\.live_provider|conformance\.postgres\.(native_live|bridge_live)"
```

With the table and connection string configured, `GET /users` returns JSON rows
from the PostgreSQL database.

## Files to look at

- `app.ts` — `Postgres<"main">` injection and PostgreSQL SQL statements.
- Generated `.sloppy/app.plan.json` — inferred `postgres/main` provider metadata.
- `docs/api/data.md` — PostgreSQL setup and connection string convention.

## Scope

Schema setup is manual for this example. Migrations, ORM-style modeling,
deployment guidance, and future PostgreSQL provider-package distribution are
outside this example. Package dependency behavior is covered by the
package/dependency examples.

## Related docs

- `docs/api/data.md`
- `docs/reference/providers.md`
- `docs/reference/framework.md`
- `docs/internals/provider-runtime.md`

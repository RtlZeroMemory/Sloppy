# Framework SQL Server CRUD Example

## What this shows

A users API backed by SQL Server:

- typed `Body<T>` and `Route<T>` handler parameters;
- `SqlServer<"main">` typed provider injection;
- SQL Server parameter placeholders (`?`);
- `output inserted...` for create responses;
- request `signal` and `deadline` passed to database calls.

## Requirements

- A V8-enabled `sloppy` runtime.
- Microsoft ODBC Driver 18 or 17 for SQL Server.
- A SQL Server test database with a `users` table.
- `Sloppy__Providers__sqlserver__main__connectionString` set in the environment.

You do not need SQL Server or ODBC for normal Sloppy apps, the Quickstart,
Program Mode, SQLite, templates, or package support. Sloppy does not bundle
Microsoft's ODBC driver in the core alpha package; install it from Microsoft's
platform packages or through your organization's managed deployment.

Example table shape:

```sql
create table users (
  id int identity primary key,
  name nvarchar(200) not null,
  email nvarchar(320) not null unique
);
```

## Run

Set the connection string and run the SQL Server integration checks:

```powershell
$env:Sloppy__Providers__sqlserver__main__connectionString = "Driver={ODBC Driver 18 for SQL Server};Server=tcp:127.0.0.1,1433;Database=sloppy_test;UID=<user>;PWD=<password>;Encrypt=yes;TrustServerCertificate=yes;"
.\tools\windows\test-live-sqlserver.ps1
```

## Expected result

The script runs the SQL Server native and bridge tests selected by:

```powershell
ctest --test-dir build\windows-relwithdebinfo --output-on-failure -R "data\.sqlserver\.live_provider|conformance\.sqlserver\.(native_live|bridge_live)"
```

With driver, database, and connection string configured, `GET /users` returns
JSON rows from SQL Server.

## Files to look at

- `app.ts` — `SqlServer<"main">` injection and SQL Server statements.
- Generated `.sloppy/app.plan.json` — inferred `sqlserver/main` provider metadata.
- `docs/api/data.md` — SQL Server setup and unavailable driver cases.

## Scope

Schema setup is manual for this example. It also depends on Microsoft ODBC
Driver 17 or 18 and available async SQL Server support. Migrations, ORM-style
modeling, deployment guidance is outside this example, and package dependency
behavior is covered by the package/dependency examples.

## Related docs

- `docs/api/data.md`
- `docs/reference/providers.md`
- `docs/reference/framework.md`
- `docs/internals/provider-runtime.md`

# Framework v2 SQL Server CRUD Example

## What This Demonstrates

This example shows the Framework v2 shape for a users API backed by SQL Server:

- typed `Body<T>` and `Route<T>` handler parameters;
- `SqlServer<"main">` typed provider injection;
- SQL Server parameter placeholders (`?`);
- `output inserted...` for create responses;
- request `signal` and `deadline` passed to database calls.

## Status

This is an opt-in live-provider example. It needs SQL Server, the Microsoft ODBC
driver, and a connection string at runtime.

## Requirements

- A V8-enabled `sloppy` runtime.
- Microsoft ODBC Driver 18 or 17 for SQL Server.
- A SQL Server test database with a `users` table.
- `Sloppy__Providers__sqlserver__main__connectionString` set in the environment.

Example table shape:

```sql
create table users (
  id int identity primary key,
  name nvarchar(200) not null,
  email nvarchar(320) not null unique
);
```

## Run Command

Use the live SQL Server lane:

```powershell
$env:Sloppy__Providers__sqlserver__main__connectionString = "Driver={ODBC Driver 18 for SQL Server};Server=tcp:127.0.0.1,1433;Database=sloppy_test;UID=<user>;PWD=<password>;Encrypt=yes;TrustServerCertificate=yes;"
.\tools\windows\test-live-sqlserver.ps1
```

## Expected Result

The live lane runs the SQL Server native and bridge tests selected by:

```powershell
ctest --test-dir build\windows-relwithdebinfo --output-on-failure -R "data\.sqlserver\.live_provider|conformance\.sqlserver\.(native_live|bridge_live)"
```

With driver, database, and connection string configured, `GET /users` returns
JSON rows from SQL Server.

## What To Inspect

- `app.ts`: `SqlServer<"main">` injection and SQL Server statements.
- Generated `.sloppy/app.plan.json`: inferred `sqlserver/main` provider metadata.
- `docs/how-to/run-live-sqlserver-checks.md`: live-lane setup and unavailable
  driver cases.

## Limitations

Schema setup is manual for this example. It also depends on an installed ODBC
driver and available async SQL Server support in the live lane.

## Related Docs

- `docs/reference/providers.md`
- `docs/reference/framework.md`
- `docs/how-to/run-live-sqlserver-checks.md`
- `docs/explanation/provider-runtime-model.md`

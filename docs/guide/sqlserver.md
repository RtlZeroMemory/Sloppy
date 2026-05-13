# SQL Server Guide

SQL Server support is public alpha, optional, and live-service
gated.

This walkthrough uses SQL Server, so it needs a SQL Server connection string
and Microsoft ODBC Driver 17 or 18.

You do not need SQL Server or ODBC for normal Sloppy apps, the Quickstart,
Program Mode, SQLite, templates, or package support.

Sloppy does not bundle Microsoft's ODBC driver in the core alpha package.
Enterprises often install and manage it centrally. `sloppy doctor` detects
whether a suitable driver is visible and gives provider-specific guidance when
it is missing.

## Configure

Use the runtime data API with an explicit ODBC connection string:

```ts
import { data } from "sloppy";
import { Environment } from "sloppy/os";

const db = data.sqlserver.open({
    connectionString: Environment.get("SLOPPY_SQLSERVER_TEST_CONNECTION_STRING"),
    maxConnections: 2,
});
```

Generated typed provider injection records SQL Server metadata, but execution
depends on the active provider bridge, Microsoft ODBC Driver 17 or 18, and a
live service.

## Health

Use a small query for readiness:

```ts
await db.queryOne("select 1 as ok", []);
```

Live SQL Server checks are optional and gated by environment variables. Default
tests do not start or require SQL Server or ODBC for apps that do not use SQL
Server.

## Migrations

`sloppy.json` can record SQL Server migrations:

```json
{
  "migrations": {
    "main": {
      "provider": "sqlserver",
      "path": "migrations/*.sql"
    }
  }
}
```

`sloppy db status` and `sloppy db migrate` execute those migrations against a
live SQL Server connection. For generated provider metadata, set:

```sh
Sloppy__Providers__sqlserver__main__connectionString=Driver={ODBC Driver 18 for SQL Server};...
```

Each migration runs in its own SQL Server transaction and is recorded in
`dbo._sloppy_migrations`.

# SQL Server Guide

SQL Server support is public alpha, pre-production, and live-service gated. Use
it when you have a V8-enabled runtime, an ODBC driver with async support, and a
configured SQL Server service. The normal Quickstart and SQLite template do not
require an ODBC driver.

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
depends on the active provider bridge, ODBC configuration, and a live service.

## Health

Use a small query for readiness:

```ts
await db.queryOne("select 1 as ok", []);
```

Live SQL Server checks are optional and gated by environment variables. Default
tests do not start or require SQL Server.

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

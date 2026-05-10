# SQL Server Guide

SQL Server support is pre-alpha and live-service gated. Use it when you have a
V8-enabled runtime, an ODBC driver with async support, and a configured SQL
Server service.

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

`sloppy.json` may record SQL Server migration metadata for package parity, but
`sloppy db migrate` does not execute SQL Server migrations yet. Do not report a
SQL Server migration as applied unless a live provider-specific migration path
actually ran.

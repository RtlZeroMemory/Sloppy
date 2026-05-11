# PostgreSQL Guide

PostgreSQL support is public alpha, pre-production, and live-service gated. Use
it when you have a V8-enabled runtime, `libpq`, and a configured PostgreSQL
service. The normal Quickstart and SQLite template do not require `libpq`.

## Configure

Use the runtime data API with an explicit connection string:

```ts
import { data } from "sloppy";
import { Environment } from "sloppy/os";

const db = data.postgres.open({
    connectionString: Environment.get("SLOPPY_POSTGRES_TEST_URL"),
    maxConnections: 2,
});
```

Generated typed provider injection records PostgreSQL metadata, but execution
still depends on the active provider bridge and live database configuration.

## Health

Use a small query for readiness:

```ts
await db.queryOne("select 1 as ok", []);
```

Live PostgreSQL checks are optional and gated by environment variables. Default
tests do not start or require a PostgreSQL service.

## Migrations

`sloppy.json` can record PostgreSQL migrations:

```json
{
  "migrations": {
    "main": {
      "provider": "postgres",
      "path": "migrations/*.sql"
    }
  }
}
```

`sloppy db status` and `sloppy db migrate` execute those migrations against a
live PostgreSQL connection. For generated provider metadata, set:

```sh
Sloppy__Providers__postgres__main__connectionString=postgres://...
```

Each migration runs in its own PostgreSQL transaction and is recorded in
`_sloppy_migrations`.

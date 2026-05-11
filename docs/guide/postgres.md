# PostgreSQL Guide

PostgreSQL support is public alpha, pre-production, optional, and live-service
gated.

This walkthrough uses PostgreSQL, so it needs a PostgreSQL connection string
and PostgreSQL client support.

You do not need PostgreSQL, libpq, or the PostgreSQL provider package for
normal Sloppy apps, the Quickstart, Program Mode, SQLite, templates, or package
support.

Current alpha packages use system or build-provided libpq. Package-local
PostgreSQL provider packages are the intended product path when the binaries,
licenses, and package contents are verified.

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
tests do not start or require a PostgreSQL service or libpq setup for apps that
do not use PostgreSQL.

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

# PostgreSQL Guide

PostgreSQL support is pre-alpha and live-service gated. Use it when you have a
V8-enabled runtime, `libpq`, and a configured PostgreSQL service.

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

`sloppy.json` may record PostgreSQL migration metadata for package parity, but
`sloppy db migrate` does not execute PostgreSQL migrations yet. Do not report a
PostgreSQL migration as applied unless a live provider-specific migration path
actually ran.

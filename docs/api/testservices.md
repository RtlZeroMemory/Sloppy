# TestServices

`TestServices` provides opt-in live database handles for app-host and artifact
tests. Import it from `sloppy`:

```ts
import { TestHost, TestServices } from "sloppy";
```

## PostgreSQL

```ts
const pg = await TestServices.postgres();

if (pg.available) {
  const host = await TestHost.create(app, {
    providers: {
      main: pg.provider(),
    },
  });
}
```

`TestServices.postgres()` reads `SLOPPY_POSTGRES_TEST_URL` by default. Pass
`connectionString` or `envName` to override it.

When the environment variable or PostgreSQL native bridge is unavailable, the
returned service has:

```ts
{
  kind: "postgres",
  available: false,
  status: "SKIPPED",
  reason: "..."
}
```

Calling `provider()` or `open()` on a skipped service throws. This keeps live
provider lanes honest: tests can report `SKIPPED` with the exact reason instead
of pretending a live database ran.

## SQL Server

```ts
const sqlserver = await TestServices.sqlserver();
```

`TestServices.sqlserver()` reads
`SLOPPY_SQLSERVER_TEST_CONNECTION_STRING` by default. It uses the same skipped
service shape when the environment variable or SQL Server native bridge is
unavailable.

## Migrations

Pass `migrations` to apply existing SQL migrations before returning a live
service:

```ts
const pg = await TestServices.postgres({
  migrations: "migrations/postgres/*.sql",
});
```

Migration application uses `Migrations.apply` from `sloppy/data` and the
provider-specific connection.

# ORM TestServices

`TestServices.postgres()` and `TestServices.sqlServer()` are opt-in live
provider helpers for app-host tests. They report `SKIPPED` when the matching
environment variable or native bridge is unavailable.

```ts
import { Sloppy, TestHost, TestServices } from "sloppy";

const app = Sloppy.create();
// Register routes that use ctx.db.

const pg = await TestServices.postgres({
  migrations: "migrations/postgres/*.sql",
});

if (!pg.available) {
  console.log(pg.status, pg.reason);
} else {
  const host = await TestHost.create(app, {
    providers: {
      main: pg.provider(),
    },
  });
  try {
    // Run requests and assertions.
  } finally {
    await host.close();
    await pg.close();
  }
}
```

The default environment variables are:

- `SLOPPY_POSTGRES_TEST_URL`
- `SLOPPY_SQLSERVER_TEST_CONNECTION_STRING`

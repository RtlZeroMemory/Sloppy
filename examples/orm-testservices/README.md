# ORM TestServices

`TestServices.postgres()` and `TestServices.sqlserver()` are opt-in live
provider helpers for app-host tests. They report `SKIPPED` when the matching
environment variable or native bridge is unavailable.

```ts
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
}
```

The default environment variables are:

- `SLOPPY_POSTGRES_TEST_URL`
- `SLOPPY_SQLSERVER_TEST_CONNECTION_STRING`

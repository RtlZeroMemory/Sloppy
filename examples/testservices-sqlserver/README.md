# TestServices SQL Server

This experimental example shows the recommended shape for a SQL Server-backed
integration test with `TestServices` and `TestHost`.

Requirements:

- Docker CLI on `PATH`
- reachable Docker daemon
- V8/native SQL Server provider bridge
- Microsoft ODBC Driver 17 or 18
- opt-in test gate, for example `SLOPPY_TESTSERVICES=1`

The test harness or CI job should perform skip/exit behavior when the opt-in
gate or Docker probe is unavailable. Keep the application snippet runtime
neutral.

```ts
import { Results, Sloppy, TestHost, TestServices, sql } from "sloppy";

const docker = await TestServices.docker.available();
if (!docker.ok) {
    throw new Error(`Docker unavailable for TestServices: ${docker.reason}`);
}

await using sqlServer = await TestServices.sqlServer({
    database: "app_test",
    driver: "ODBC Driver 17 for SQL Server",
    username: "sa",
    password: "Strong_test_password_123!",
});

await sqlServer.migrate("migrations/sqlserver/*.sql");
await sqlServer.seed((db) =>
    db.exec("insert into dbo.Users (Email) values (?)", ["ada@example.com"]));

const app = Sloppy.create();
app.post("/users", async (ctx) => {
    const db = ctx.services.get("data.main");
    const input = await ctx.request.json();
    await db.exec(sql`insert into dbo.Users (Email) values (${input.email})`);
    return Results.created("/users/1", { email: input.email });
});

await using host = await TestHost.create(app, {
    providers: {
        main: sqlServer.provider(),
    },
    config: {
        SQLSERVER_CONNECTION_STRING: sqlServer.connectionString,
    },
});

await host.post("/users")
    .json({ email: "grace@example.com" })
    .expectStatus(201);

await sqlServer.reset({ migrate: true });
```

For artifact/package mode, pass environment instead of an app-host provider:

```ts
await using host = await TestHost.fromPackage("./dist/app", {
    mode: "loopback",
    env: sqlServer.env(),
});

await host.get("/health/ready").expectStatus(200);
```

SQL Server image startup is heavier than PostgreSQL startup. Increase
`startupTimeoutMs` on cold machines instead of adding sleeps to tests.
This API currently supports only the built-in `sa` login; custom SQL Server
logins are not provisioned by TestServices.

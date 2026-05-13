# TestServices SQL Server

This example shows the recommended shape for a SQL Server-backed integration
test with `TestServices` and `TestHost`.

Requirements:

- Docker CLI on `PATH`
- reachable Docker daemon
- V8/native SQL Server provider bridge
- Microsoft ODBC Driver 17 or 18
- opt-in test gate, for example `SLOPPY_TESTSERVICES=1`

```ts
import { Results, Sloppy, TestHost, TestServices, sql } from "sloppy";

if (process.env.SLOPPY_TESTSERVICES !== "1") {
    console.log("SKIPPED: set SLOPPY_TESTSERVICES=1 to run SQL Server containers");
    process.exit(0);
}

const docker = await TestServices.docker.available();
if (!docker.ok) {
    console.log(`SKIPPED: Docker unavailable: ${docker.reason}`);
    process.exit(0);
}

await using sqlServer = await TestServices.sqlServer({
    database: "app_test",
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

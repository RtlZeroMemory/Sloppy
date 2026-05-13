# TestServices PostgreSQL

This example shows the recommended shape for a PostgreSQL-backed integration
test with `TestServices` and `TestHost`.

Requirements:

- Docker CLI on `PATH`
- reachable Docker daemon
- V8/native PostgreSQL provider bridge
- opt-in test gate, for example `SLOPPY_TESTSERVICES=1`

```ts
import { Results, Sloppy, TestHost, TestServices, sql } from "sloppy";

if (process.env.SLOPPY_TESTSERVICES !== "1") {
    console.log("SKIPPED: set SLOPPY_TESTSERVICES=1 to run PostgreSQL containers");
    process.exit(0);
}

const docker = await TestServices.docker.available();
if (!docker.ok) {
    console.log(`SKIPPED: Docker unavailable: ${docker.reason}`);
    process.exit(0);
}

await using pg = await TestServices.postgres({
    database: "app_test",
});

await pg.migrate("migrations/postgres/*.sql");
await pg.seed((db) =>
    db.exec("insert into users (email) values ($1)", ["ada@example.com"]));

const app = Sloppy.create();
app.post("/users", async (ctx) => {
    const db = ctx.services.get("data.main");
    const input = await ctx.request.json();
    await db.exec(sql`insert into users (email) values (${input.email})`);
    return Results.created("/users/1", { email: input.email });
});

await using host = await TestHost.create(app, {
    providers: {
        main: pg.provider(),
    },
    config: {
        DATABASE_URL: pg.connectionString,
    },
});

await host.post("/users")
    .json({ email: "grace@example.com" })
    .expectStatus(201);

await pg.reset({ migrate: true });
```

For artifact/package mode, pass environment instead of an app-host provider:

```ts
await using host = await TestHost.fromArtifacts(".sloppy", {
    env: pg.env(),
});

await host.get("/health/ready").expectStatus(200);
```

Cleanup is automatic with `await using`; without it, call `host.dispose()` and
then `pg.dispose()`.

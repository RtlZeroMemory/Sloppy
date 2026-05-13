# TestHost DB

```js
import { Sloppy, TestData, TestHost, data, Results } from "sloppy";

const db = TestData.sqliteMemory({
    migrations: "migrations/*.sql",
    seed: async (connection) => {
        await connection.exec("insert into users (email) values (?)", ["ada@example.com"]);
    },
});

const app = Sloppy.create();

app.get("/users", async () => {
    const connection = await db.open();
    try {
        return Results.json(await connection.query("select email from users", []));
    } finally {
        await connection.close?.();
    }
});

await using host = await TestHost.create(app);

await host.get("/users").expectStatus(200);
```

SQLite native bridge support depends on the active Sloppy runtime lane. For
compiled app tests, prefer `TestHost.fromArtifacts(".sloppy")` with the same
provider configuration used by `sloppy run`.

# SQLite walkthrough

A small CRUD API backed by SQLite. The whole thing fits in one file. By the
end you'll have a server that creates, reads, updates, and deletes users
through HTTP.

Prerequisites: you've done the [Quickstart](../quickstart.md), `sloppy` is
on your `PATH`, and your build executes handlers (V8 enabled).

## 1. Project layout

```sh
mkdir users-api && cd users-api
mkdir src
```

`sloppy.json`:

```json
{
  "entry": "src/main.ts",
  "outDir": ".sloppy",
  "environment": "Development"
}
```

## 2. Wire up SQLite

`src/main.ts`:

```ts
import { Sloppy, Results, sql, data } from "sloppy";

const builder = Sloppy.createBuilder();

builder.capabilities.addDatabase("data.main", {
    provider: "sqlite",
    access: "readwrite",
});

builder.services.addSingleton("data.main", () =>
    data.sqlite.open({
        capability: "data.main",
        database: "users.db",
        access: "readwrite",
    })
);

builder.services.addSingleton("setup", async (s) => {
    const db = s.get("data.main");
    await db.exec(sql`
        CREATE TABLE IF NOT EXISTS users (
            id    INTEGER PRIMARY KEY,
            name  TEXT NOT NULL,
            email TEXT NOT NULL UNIQUE
        )
    `);
});

const app = builder.build();

// touch the setup service once at startup
await app.services.get("setup");
```

What's happening:

- `builder.capabilities.addDatabase` declares that the app needs a
  read/write SQLite database under the token `data.main`.
- `builder.services.addSingleton` registers the SQLite handle as a service.
  `data.sqlite.open(...)` returns a database client.
- The `setup` service runs the `CREATE TABLE` once at startup.

## 3. List and create

Add the routes after `builder.build()`:

```ts
app.get("/users", async (ctx) => {
    const db = ctx.services.get("data.main");
    const rows = await db.query(sql`SELECT id, name, email FROM users`);
    return Results.ok(rows);
});

app.post("/users", async (ctx) => {
    const body = ctx.request.json() as { name: string; email: string };

    if (!body?.name || !body?.email) {
        return Results.badRequest({ error: "name and email required" });
    }

    const db = ctx.services.get("data.main");
    const result = await db.exec(sql`
        INSERT INTO users (name, email)
        VALUES (${body.name}, ${body.email})
    `);

    const created = await db.queryOne(sql`
        SELECT id, name, email FROM users WHERE id = ${result.lastInsertId}
    `);

    return Results.created(`/users/${created.id}`, created);
});

export default app;
```

## 4. Read, update, delete

```ts
app.get("/users/{id:int}", async (ctx) => {
    const id = Number(ctx.route.id);
    const db = ctx.services.get("data.main");
    const user = await db.queryOne(sql`
        SELECT id, name, email FROM users WHERE id = ${id}
    `);
    return user ? Results.ok(user) : Results.notFound();
});

app.put("/users/{id:int}", async (ctx) => {
    const id = Number(ctx.route.id);
    const body = ctx.request.json() as { name?: string; email?: string };
    const db = ctx.services.get("data.main");

    const existing = await db.queryOne(sql`SELECT id FROM users WHERE id = ${id}`);
    if (!existing) return Results.notFound();

    const name  = body.name  ?? null;
    const email = body.email ?? null;
    await db.exec(sql`
        UPDATE users
           SET name  = COALESCE(${name},  name),
               email = COALESCE(${email}, email)
         WHERE id    = ${id}
    `);

    const updated = await db.queryOne(sql`
        SELECT id, name, email FROM users WHERE id = ${id}
    `);
    return Results.ok(updated);
});

app.delete("/users/{id:int}", async (ctx) => {
    const id = Number(ctx.route.id);
    const db = ctx.services.get("data.main");
    const result = await db.exec(sql`DELETE FROM users WHERE id = ${id}`);
    return result.affectedRows > 0 ? Results.noContent() : Results.notFound();
});
```

## 5. Run it

```
sloppy run
```

In another shell:

```sh
curl -s http://127.0.0.1:5173/users

curl -s -X POST http://127.0.0.1:5173/users \
     -H 'content-type: application/json' \
     -d '{"name":"Ada","email":"ada@example.com"}'

curl -s http://127.0.0.1:5173/users/1
```

## What you got

- A real HTTP server with five routes.
- A typed-template SQL layer that's safe by construction — every `${…}` is
  a parameter, never concatenated into the query.
- Capability and provider declarations the runtime validates at startup.
- A handler that can be smoked offline with `sloppy run --once GET /users`.

## Where to go next

- [API: data](../api/data.md) — `query`, `queryOne`, `exec`, `transaction`,
  cancellation, deadlines.
- [API: services](../api/services.md) — singleton/scoped/transient,
  disposal, scopes.
- [Examples](examples.md) — typed-handler version of this same app under
  `examples/framework-v2-sqlite-crud/`.

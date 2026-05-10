# PostgreSQL walkthrough

A small CRUD API backed by PostgreSQL. Same shape as the
[SQLite walkthrough](sqlite.md), with a live PostgreSQL service in front of
your app.

This walkthrough uses PostgreSQL, so it needs a PostgreSQL client library
(`libpq`) and a database connection string. PostgreSQL support is
experimental.

You do **not** need PostgreSQL or `libpq` for normal Sloppy apps, the
Quickstart, Program Mode, SQLite, templates, or package support. Use SQLite
if you just want to try the quickstart.

## Prerequisites

For this page only:

- `sloppy` is installed and on your `PATH` (see [Install](../install.md)).
- You have completed the [Quickstart](../quickstart.md) smoke test.
- PostgreSQL is reachable from your machine.
- `libpq` is available to the Sloppy runtime. See
  [Native dependencies](../reference/dependencies.md).
- `SLOPPY_POSTGRES_TEST_URL` contains a PostgreSQL connection string.

The published alpha runtime packages are V8-enabled. If you are using a
source build, make sure it was configured with V8 before running this
walkthrough.

## 1. Project layout

```sh
mkdir users-postgres && cd users-postgres
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

## 2. Wire up PostgreSQL

The compiler auto-infers the database capability from a typed `Postgres<"main">`
handler parameter, so the shortest shape is:

```ts
import { Sloppy, sql, data } from "sloppy";
import { Postgres } from "sloppy/providers/postgres";
import { Environment } from "sloppy/os";

const app = Sloppy.create();
app.use(data.postgres.descriptor("main", {
    connectionString: Environment.get("SLOPPY_POSTGRES_TEST_URL"),
}));

app.get("/users", (db: Postgres<"main">) =>
    db.query(sql`SELECT id, name, email FROM users`)
);

export default app;
```

The rest of this walkthrough uses the explicit module shape so it can also
show startup migrations and explicit capability control. Both shapes produce
a Plan with the same `data.main` capability.

`src/main.ts`:

```ts
import { Sloppy, Results, sql, data } from "sloppy";
import { Environment } from "sloppy/os";

function requireEnv(name: string): string {
    const v = Environment.get(name);
    if (!v) throw new Error(`Missing required environment value: ${name}`);
    return v;
}

const PostgresModule = Sloppy.module("data.postgres")
    .capabilities((caps) => {
        caps.addDatabase("data.main", {
            provider: "postgres",
            access: "readwrite",
        });
    })
    .services((services) => {
        services.addSingleton("data.main", () =>
            data.postgres.open({
                connectionString: requireEnv("SLOPPY_POSTGRES_TEST_URL"),
                maxConnections: 2,
            })
        );
    });

const builder = Sloppy.createBuilder().addModule(PostgresModule);

builder.services.addSingleton("setup", async (s) => {
    const db = s.get("data.main");
    await db.exec(sql`
        CREATE TABLE IF NOT EXISTS users (
            id    SERIAL PRIMARY KEY,
            name  TEXT NOT NULL,
            email TEXT NOT NULL UNIQUE
        )
    `);
});

const app = builder.build();

await app.services.get("setup");
```

What's happening:

- `Sloppy.module(...)` declares a module that owns the PostgreSQL capability
  and the `data.main` service.
- `data.postgres.open({ connectionString, maxConnections })` opens a pooled
  client. The connection string never lands in the Plan or generated bundle.
- `setup` runs the `CREATE TABLE` once at startup using the same
  `sql\`...\`` tagged template that SQLite uses — the provider swaps `?`
  for `$1`-style parameters automatically.

## 3. List and create

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
    const created = await db.queryOne(sql`
        INSERT INTO users (name, email)
        VALUES (${body.name}, ${body.email})
        RETURNING id, name, email
    `);

    return Results.created(`/users/${created.id}`, created);
});

export default app;
```

PostgreSQL supports `RETURNING`, so the insert plus identity read fit in one
statement.

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

    const updated = await db.queryOne(sql`
        UPDATE users
           SET name  = COALESCE(${body.name  ?? null}, name),
               email = COALESCE(${body.email ?? null}, email)
         WHERE id    = ${id}
        RETURNING id, name, email
    `);

    return updated ? Results.ok(updated) : Results.notFound();
});

app.delete("/users/{id:int}", async (ctx) => {
    const id = Number(ctx.route.id);
    const db = ctx.services.get("data.main");
    const result = await db.exec(sql`DELETE FROM users WHERE id = ${id}`);
    return result.affectedRows > 0 ? Results.noContent() : Results.notFound();
});
```

## 5. Run it

Export the connection string and run the server:

```sh
export SLOPPY_POSTGRES_TEST_URL="postgresql://user:pass@localhost:5432/users"
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

## Current limits

- PostgreSQL support is experimental and needs a V8-enabled runtime,
  `libpq`, and a live database.
- Live provider checks are opt-in; missing services are reported as skipped
  or unavailable rather than as silent passes.
- Connection strings are read at request time and are never persisted in
  `app.js` or `app.plan.json`.

## Where to go next

- [API: data](../api/data.md) — `query`, `queryOne`, `exec`, `transaction`,
  cancellation, deadlines, value wrappers.
- [Reference: providers](../reference/providers.md) — full provider contract
  for the descriptor, static handle, typed injection, and runtime open
  paths.
- [Reference: native dependencies](../reference/dependencies.md) — which
  libraries each provider needs.
- [Examples](examples.md) — typed-handler version of this same shape under
  `examples/framework-postgres-crud/`.

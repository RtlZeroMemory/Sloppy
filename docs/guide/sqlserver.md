# SQL Server walkthrough

A small CRUD API backed by SQL Server. Same shape as the
[SQLite walkthrough](sqlite.md), with a live SQL Server instance reached
through ODBC.

This walkthrough uses SQL Server, so it needs an ODBC driver that supports
async statement work (Microsoft ODBC Driver 17 or 18) and a database
connection string. SQL Server support is experimental.

You do **not** need SQL Server or ODBC for normal Sloppy apps, the
Quickstart, Program Mode, SQLite, templates, or package support. Use SQLite
if you just want to try the quickstart.

## Prerequisites

For this page only:

- `sloppy` is installed and on your `PATH` (see [Install](../install.md)).
- You have completed the [Quickstart](../quickstart.md) smoke test.
- A supported ODBC driver is installed (see
  [Native dependencies](../reference/dependencies.md)).
- `SLOPPY_SQLSERVER_TEST_CONNECTION_STRING` contains a SQL Server
  connection string.

The published alpha runtime packages are V8-enabled. If you are using a
source build, make sure it was configured with V8 before running this
walkthrough.

## 1. Project layout

```sh
mkdir users-sqlserver && cd users-sqlserver
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

## 2. Wire up SQL Server

The compiler auto-infers the database capability from a typed
`SqlServer<"main">` handler parameter, so the shortest shape is:

```ts
import { Sloppy, sql, data } from "sloppy";
import { SqlServer } from "sloppy/providers/sqlserver";
import { Environment } from "sloppy/os";

const app = Sloppy.create();
app.use(data.sqlserver.descriptor("main", {
    connectionString: Environment.get(
        "SLOPPY_SQLSERVER_TEST_CONNECTION_STRING",
    ),
}));

app.get("/users", (db: SqlServer<"main">) =>
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

const SqlServerModule = Sloppy.module("data.sqlserver")
    .capabilities((caps) => {
        caps.addDatabase("data.main", {
            provider: "sqlserver",
            access: "readwrite",
        });
    })
    .services((services) => {
        services.addSingleton("data.main", () =>
            data.sqlserver.open({
                connectionString: requireEnv(
                    "SLOPPY_SQLSERVER_TEST_CONNECTION_STRING",
                ),
                maxConnections: 2,
            })
        );
    });

const builder = Sloppy.createBuilder().addModule(SqlServerModule);

builder.services.addSingleton("setup", async (s) => {
    const db = s.get("data.main");
    await db.exec(sql`
        IF NOT EXISTS (
            SELECT 1 FROM sys.tables WHERE name = 'users'
        )
        CREATE TABLE users (
            id    INT IDENTITY(1,1) PRIMARY KEY,
            name  NVARCHAR(200) NOT NULL,
            email NVARCHAR(320) NOT NULL UNIQUE
        )
    `);
});

const app = builder.build();

await app.services.get("setup");
```

What's happening:

- `data.sqlserver.open({ connectionString, maxConnections })` opens a
  pooled ODBC client. The connection string never lands in the Plan or
  generated bundle.
- The same `sql\`...\`` tagged template works here — the provider picks
  the SQL Server parameter style (`?` by default, or `@p1` named style
  for some scenarios).
- `setup` runs the `CREATE TABLE` once at startup.

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
        OUTPUT INSERTED.id, INSERTED.name, INSERTED.email
        VALUES (${body.name}, ${body.email})
    `);

    return Results.created(`/users/${created.id}`, created);
});

export default app;
```

SQL Server's `OUTPUT INSERTED.*` returns the inserted row in the same
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
        OUTPUT INSERTED.id, INSERTED.name, INSERTED.email
         WHERE id    = ${id}
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
export SLOPPY_SQLSERVER_TEST_CONNECTION_STRING="Driver={ODBC Driver 18 for SQL Server};Server=localhost,1433;Database=users;UID=sa;PWD=...;Encrypt=yes;TrustServerCertificate=yes"
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

- SQL Server support is experimental and needs a V8-enabled runtime, an
  ODBC driver capable of async statement work, and a live database.
- Live provider checks are opt-in; missing drivers or services are reported
  as skipped or unavailable rather than as silent passes.
- Connection strings are read at request time and are never persisted in
  `app.js` or `app.plan.json`. Diagnostic output redacts password-shaped
  fields.

## Where to go next

- [API: data](../api/data.md) — `query`, `queryOne`, `exec`, `transaction`,
  cancellation, deadlines, value wrappers.
- [Reference: providers](../reference/providers.md) — full provider contract
  for the descriptor, static handle, typed injection, and runtime open
  paths.
- [Reference: native dependencies](../reference/dependencies.md) — which
  libraries each provider needs.
- [Examples](examples.md) — typed-handler version of this same shape under
  `examples/framework-sqlserver-crud/`.

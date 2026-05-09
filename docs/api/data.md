# Data

Sloppy ships first-party providers for SQLite, PostgreSQL, and SQL Server.
Queries use a tagged template literal that's safe by construction — every
interpolation becomes a parameter, never a string concatenation.

> Stable: SQLite. Experimental: PostgreSQL and SQL Server need their live
> dependencies (libpq, ODBC) and a V8-enabled runtime.

## Tagged template

```ts
import { sql } from "sloppy";

const id = 42;
const query = sql`SELECT id, name FROM users WHERE id = ${id}`;
```

`sql\`...\`` returns a *lowered query* — a frozen descriptor that the
provider lowers further into the database's parameter style:

```ts
query.text;            // "SELECT id, name FROM users WHERE id = ?"
query.parameters;      // [42]
query.parameterCount;  // 1
```

Different providers use different placeholder styles:

| Style       | Used by             | Example         |
| ----------- | ------------------- | --------------- |
| `question`  | SQLite, SQL Server  | `?`             |
| `postgres`  | PostgreSQL          | `$1`            |
| `named`     | SQL Server (named)  | `@p1`           |

You don't pick the style — the provider does.

## Value wrappers

For values that don't have a clean JS representation, wrap them with a
`sql.*` helper before interpolation:

```ts
sql`INSERT INTO orders (id, total, ts)
    VALUES (${sql.uuid(orderId)},
            ${sql.decimal(total)},
            ${sql.instant(timestamp)})`;
```

Available wrappers:

| Helper                        | Stored as                              |
| ----------------------------- | -------------------------------------- |
| `sql.decimal(string)`         | exact decimal string                   |
| `sql.uuid(string)`            | validated UUID                         |
| `sql.date("YYYY-MM-DD")`      | calendar date                          |
| `sql.time("HH:MM:SS[.nanos]")`| time of day                            |
| `sql.timestamp(...)`          | local date-time                        |
| `sql.instant("...Z")`         | UTC instant                            |
| `sql.offsetDateTime(...)`     | date-time with explicit `±HH:MM`       |
| `sql.json(value)`             | JSON-serializable value                |
| `sql.rawJson(string)`         | already-serialized JSON text           |
| `sql.bytes(uint8 / buffer)`   | raw bytes                              |

The wrappers validate up front; bad input throws before the query runs.

## SQLite

The simplest provider. No external server. Best for the local dev loop and
single-node services.

```ts
const builder = Sloppy.createBuilder();

builder.capabilities.addDatabase("data.main", {
    provider: "sqlite",
    access: "readwrite",
});

builder.services.addSingleton("data.main", () =>
    data.sqlite.open({
        capability: "data.main",
        database: "app.db",
        access: "readwrite",
    })
);

const app = builder.build();
```

Opening with `:memory:` gets you an in-memory database — convenient for
tests:

```ts
data.sqlite.open({
    capability: "data.main",
    database: ":memory:",
    access: "readwrite",
});
```

### Querying

```ts
const db = ctx.services.get("data.main");

await db.exec(sql`
    CREATE TABLE IF NOT EXISTS users (
        id    INTEGER PRIMARY KEY,
        name  TEXT NOT NULL
    )
`);

await db.exec(sql`INSERT INTO users (name) VALUES (${"Ada"})`);

const rows = await db.query(sql`SELECT id, name FROM users`);
const ada  = await db.queryOne(sql`SELECT id FROM users WHERE name = ${"Ada"}`);
```

| Method              | Returns                          |
| ------------------- | -------------------------------- |
| `db.query(sql)`     | array of rows                    |
| `db.queryOne(sql)`  | single row, or `null`            |
| `db.exec(sql)`      | `{ affectedRows, lastInsertId? }`|
| `db.transaction(fn)`| runs `fn(tx)` in a transaction   |

A row is a plain object keyed by column name.

### Transactions

```ts
await db.transaction(async (tx) => {
    await tx.exec(sql`UPDATE users SET name = ${name} WHERE id = ${id}`);
    await tx.exec(sql`INSERT INTO audit (user_id) VALUES (${id})`);
});
```

The callback receives a transaction handle that mirrors the database's API
but participates in the transaction. Returning normally commits; throwing
rolls back.

### Cancellation and deadlines

Pass a deadline or signal in the operation options:

```ts
await db.query(sql`SELECT * FROM big_table`, {
    timeoutMs: 1500,
});
```

| Option       | Effect                                                       |
| ------------ | ------------------------------------------------------------ |
| `timeoutMs`  | Abort after this many milliseconds                           |
| `deadline`   | Abort at this absolute deadline                              |
| `signal`     | Abort when the supplied cancellation signal fires            |

## PostgreSQL

> Experimental. Requires a V8-enabled runtime and `libpq` available at
> runtime. Live evidence is opt-in.

```ts
builder.capabilities.addDatabase("data.main", {
    provider: "postgres",
    access: "readwrite",
});

builder.services.addSingleton("data.main", () =>
    data.postgres.open({
        capability: "data.main",
        // by convention the connection string is read from
        // SLOPPY:PROVIDERS:POSTGRES:MAIN:CONNECTIONSTRING in the environment
    })
);
```

The query API is identical (`query`, `queryOne`, `exec`, `transaction`),
including the `sql\`...\`` template — the provider switches placeholder
style under the hood.

PostgreSQL-specific value wrappers worth knowing:

- `sql.json(...)` becomes `jsonb`-friendly text on the wire.
- `sql.bytes(...)` becomes `bytea`.

## SQL Server

> Experimental. Requires a V8-enabled runtime and an ODBC driver capable of
> async connection/statement work.

```ts
builder.capabilities.addDatabase("data.main", {
    provider: "sqlserver",
    access: "readwrite",
});

builder.services.addSingleton("data.main", () =>
    data.sqlserver.open({
        capability: "data.main",
        // SLOPPY:PROVIDERS:SQLSERVER:MAIN:CONNECTIONSTRING in env
    })
);
```

Same API. Connection strings, decimal handling, and async ODBC support
matter; check `data.sqlserver.open(...)` diagnostics if startup fails.

## Compiler-inferred providers

Framework v2 typed handlers can declare provider parameters directly:

```ts
import { Sloppy, Results, sql } from "sloppy";
import { Sqlite } from "sloppy/providers/sqlite";

const app = Sloppy.create();

app.get("/users", (db: Sqlite<"main">) =>
    db.query(sql`SELECT id, name FROM users`)
);
```

The compiler infers a `data.main` capability and a SQLite provider binding
from the `Sqlite<"main">` type. The runtime materializes the provider when
the request scope opens. `Postgres<"name">` and `SqlServer<"name">` work
the same way.

> Experimental: typed-handler injection works for SQLite end-to-end and for
> PostgreSQL/SQL Server when their dependencies are available. The full
> compiler-inferred surface is still landing.

## Errors

Provider errors are normalized into structured diagnostics. Inside a handler,
catch them like any error and convert to a result:

```ts
try {
    return Results.ok(await db.queryOne(sql`...`));
} catch (err) {
    ctx.log.error("query failed", { error: String(err) });
    return Results.problem({ status: 500, title: "Database failure" });
}
```

The runtime never includes credential strings or query text in the
default-redacted error path — see [about/security.md](../about/security.md).

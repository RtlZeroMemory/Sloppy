# Data

Sloppy ships first-party providers for SQLite, PostgreSQL, and SQL Server.
Queries use a tagged template literal that's safe by construction — every
interpolation becomes a parameter, never a string concatenation.

> SQLite is the most complete provider path today. PostgreSQL and SQL
> Server are pre-alpha and require their live dependencies (libpq,
> ODBC) plus a V8-enabled runtime.

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

`data.postgres.open(...)` requires an explicit `connectionString`. The
recommended pattern is to read it from an environment variable using
`Environment` from `sloppy/os` and pass it to `open`:

```ts
import { Sloppy, data } from "sloppy";
import { Environment } from "sloppy/os";

function requireEnv(name) {
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

const app = Sloppy.createBuilder().addModule(PostgresModule).build();
```

The query API is identical (`query`, `queryOne`, `exec`, `transaction`),
including the `sql\`...\`` template - the provider switches parameter
style under the hood.

PostgreSQL-specific value wrappers worth knowing:

- `sql.json(...)` becomes `jsonb`-friendly text on the wire.
- `sql.bytes(...)` becomes `bytea`.

## SQL Server

> Experimental. Requires a V8-enabled runtime and an ODBC driver capable
> of async connection/statement work.

Same shape — `data.sqlserver.open({ connectionString })` requires an
explicit ODBC connection string:

```ts
import { Sloppy, data } from "sloppy";
import { Environment } from "sloppy/os";

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
                connectionString: Environment.get(
                    "SLOPPY_SQLSERVER_TEST_CONNECTION_STRING",
                ),
            })
        );
    });
```

Connection strings, decimal handling, and async ODBC support matter;
check `data.sqlserver.open(...)` diagnostics if startup fails.

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

The compiler emits Plan metadata for `Sqlite<"name">`,
`Postgres<"name">`, and `SqlServer<"name">` typed handler parameters.
Generated typed provider wrappers open the matching provider from runtime
configuration:

| Marker | Generated provider metadata | Runtime requirements |
| --- | --- | --- |
| `Sqlite<"main">` | `sqlite/main` provider plus `data.main` capability | V8-enabled runtime and SQLite bridge/config |
| `Postgres<"main">` | `postgres/main` provider plus `data.main` capability | `Sloppy__Providers__postgres__main__connectionString`, active PostgreSQL bridge, and live service setup |
| `SqlServer<"main">` | `sqlserver/main` provider plus `data.main` capability | `Sloppy__Providers__sqlserver__main__connectionString`, active SQL Server bridge, and ODBC driver support |

The `SLOPPYC_E_UNSUPPORTED_PROVIDER_BRIDGE` diagnostic applies to
compiler-generated **static** non-SQLite provider handles such as
`app.provider("postgres:main")`, not to typed `Postgres<...>` or
`SqlServer<...>` handler parameters. Use the explicit module shape shown above
when you want to manage provider services manually.

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

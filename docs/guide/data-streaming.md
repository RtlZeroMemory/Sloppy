# Data Streaming

Sloppy data providers expose cursor APIs for incremental database reads. Use
them when a query can return more rows than you want to materialize at once.

```ts
const cursor = await db.queryCursor(sql`
    select id, email, created_at
    from users
    order by id
`, { batchSize: 512, maxRows: 100000 });

try {
    for await (const row of cursor) {
        await writeUser(row);
    }
} finally {
    await cursor.close();
}
```

## Public Cursor APIs

| API | Rows |
| --- | --- |
| `db.queryCursor(sql, options?)` | object rows keyed by column name |
| `db.queryRawCursor(sql, options?)` | positional raw rows |
| `db.stream(sql, options?)` | alias for `queryCursor(...)` |

Cursor options:

| Option | Meaning |
| --- | --- |
| `batchSize` | Provider fetch batch size, `1..4096` |
| `maxRows` | Optional hard cap for the cursor stream |
| `timeoutMs` | Abort the cursor open/fetch path after this many milliseconds |

A cursor is an async iterable. It owns the active statement/result and pins the
provider connection until it reaches end-of-stream or closes. Early `for await`
exit, explicit `close()`, transaction teardown, and runtime shutdown release or
invalidate the native cursor deterministically.

SQLite cursors step a prepared statement incrementally. PostgreSQL cursors use
libpq single-row mode. SQL Server cursors keep an ODBC statement active and
fetch rows incrementally.

## ORM Cursor Export

The ORM exposes cursor reads for query-builder output:

```ts
const cursor = await orm
    .from(Users)
    .select({ id: Users.id, email: Users.email })
    .cursor(ctx.db, { batchSize: 512, maxRows: 100000 });

const stream = orm.stream.ndjson(cursor, row => ({
    id: row.id,
    email: row.email,
}));
```

`orm.stream.ndjson(cursor, mapper?)` returns an async iterable of encoded NDJSON
chunks and keeps selected-column metadata available for the native streaming
path. See `examples/orm-cursor-export`.

## HTTP Responses

Database cursors and HTTP response streaming are separate APIs. A cursor is the
database-side incremental read surface. `Results.stream(...)` is the current
bounded response descriptor surface.

When you connect the two, do not collect the whole cursor into an array first.
Close the cursor on normal completion, client cancellation, and stream errors.

```ts
return Results.stream(async (writer) => {
    const cursor = await ctx.db.queryCursor(sql`select id, email from users`);
    try {
        for await (const row of cursor) {
            writer.writeText(`${JSON.stringify(row)}\n`);
        }
    } finally {
        await cursor.close();
    }
}, { contentType: "application/x-ndjson" });
```

Native provider handles stay native-only; JavaScript sees Sloppy cursor objects,
not driver pointers or native stream handles.

# ORM Cursor Export

This documentation example shows the ORM cursor-to-NDJSON adapter:

```ts
const cursor = await orm.from(Users).cursor(ctx.db, {
  batchSize: 512,
  maxRows: 100000,
});

const stream = orm.stream.ndjson(cursor);
```

The cursor is incremental and closes on early exit or adapter completion.
`Results.stream` remains the current bounded result descriptor; the ORM adapter
keeps selected-column metadata available for the native streaming path.

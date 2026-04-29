# Data

Status: Bootstrap data/capabilities foundation implemented.

Purpose: document future data provider modules, query templates, transactions, and
provider-specific limitations.

Implemented bootstrap API example:

```ts
import { data, sql } from "sloppy";

const fakeDb = data.createFakeProvider({
  query(lowered) {
    return [{ id: lowered.parameters[0], loweredText: lowered.text }];
  },
  exec() {
    return { affectedRows: 1 };
  },
});

const user = await fakeDb.queryOne`
  select id, name
  from users
  where id = ${route.id}
`;

const lowered = sql`select id from users where id = ${route.id}`;
```

Implemented behavior:

- `sql` lowers tagged templates to frozen query descriptors.
- `data.lowerQueryTemplate(strings, values, options)` supports `question`, `postgres`,
  and `named` placeholder styles.
- lowered query descriptors keep SQL text and values separate:
  `{ __sloppyQuery, text, parameters, parameterCount, placeholderStyle, placeholders }`.
- `data.createFakeProvider(...)` creates a JS-only fake provider for tests/examples with
  `query`, `queryOne`, `exec`, and `transaction`.
- provider methods accept tagged templates or already-lowered query objects.
- `queryOne` uses a supplied handler or falls back to the first row returned by `query`.
- `transaction(callback)` passes a transaction object with `query`, `queryOne`, and `exec`,
  commits when the callback resolves, and rolls back when it throws or rejects.

Not implemented yet:

- no SQLite, PostgreSQL, or SQL Server providers;
- no database connections;
- no SQL execution;
- no SQL parser, ORM, migrations, pooling, cancellation, isolation levels, or native SQL
  execution;
- no compiler extraction of JavaScript template literals.

Related internal docs: `docs/data-providers.md`, `docs/concurrency.md`.

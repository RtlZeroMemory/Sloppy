# Data

Status: Bootstrap data/capabilities foundation plus native SQLite and PostgreSQL providers
implemented.

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

SQLite bootstrap service shape:

```ts
import { Sloppy, data } from "sloppy";

const SqliteModule = Sloppy.module("data.sqlite")
  .capabilities(caps => {
    caps.addDatabase("data.main", {
      provider: "sqlite",
      path: ":memory:",
      access: "readwrite",
    });
  })
  .services(services => {
    services.addSingleton("data.main", () => data.sqlite.open({
      path: ":memory:",
      access: "readwrite",
    }));
  });
```

PostgreSQL bootstrap service shape:

```ts
import { Sloppy, data } from "sloppy";

const PostgresModule = Sloppy.module("data.postgres")
  .capabilities(caps => {
    caps.addDatabase("data.main", {
      provider: "postgres",
      configKey: "SLOPPY_POSTGRES_TEST_URL",
      access: "readwrite",
    });
  })
  .services(services => {
    services.addSingleton("data.main", () => data.postgres.open({
      connectionString: "postgres://localhost/sloppy_test",
      maxConnections: 2,
    }));
  });
```

Implemented behavior:

- `sql` lowers tagged templates to frozen query descriptors.
- `data.lowerQueryTemplate(strings, values, options)` supports `question`, `postgres`,
  and `named` placeholder styles.
- lowered query descriptors keep SQL text and values separate:
  `{ __sloppyQuery, text, parameters, parameterCount, placeholderStyle, placeholders }`.
- `data.createFakeProvider(...)` creates a JS-only fake provider for tests/examples with
  `query`, `queryOne`, `exec`, and `transaction`.
- `data.sqlite` exposes SQLite provider metadata plus `open(options)` as the future stdlib
  entry point.
- `data.postgres` exposes PostgreSQL provider metadata, `$1` placeholder style, connection
  string redaction, and `open(options)` as the future stdlib entry point.
- native C SQLite tests execute real SQLite against `:memory:` databases.
- native C PostgreSQL tests execute live libpq coverage only when `SLOPPY_POSTGRES_TEST_URL`
  is set.
- native SQLite supports `exec`, `query`, `queryOne`, primitive parameter binding, and
  transactions.
- native PostgreSQL supports connection-string open/close, `exec`, `query`, `queryOne`,
  primitive parameter binding, a tiny bounded pool skeleton, diagnostics, and transactions.
- provider methods accept tagged templates or already-lowered query objects.
- `queryOne` uses a supplied handler or falls back to the first row returned by `query`.
- `transaction(callback)` passes a transaction object with `query`, `queryOne`, and `exec`,
  commits when the callback resolves, and rolls back when it throws or rejects.

Not implemented yet:

- no JavaScript-to-native SQLite intrinsic bridge yet, so `data.sqlite.open(...)` validates
  options and fails with an honest bridge-unavailable error in the stdlib;
- no JavaScript-to-native PostgreSQL intrinsic bridge yet, so `data.postgres.open(...)`
  validates/redacts options and fails with an honest bridge-unavailable error in the stdlib;
- no SQL Server provider;
- no SQL parser, ORM, migrations, production pooling, cancellation, isolation levels, or
  native SQL execution from JavaScript;
- no public file database policy beyond the native provider accepting SQLite paths;
- no compiler extraction of JavaScript template literals.

Related internal docs: `docs/data-providers.md`, `docs/concurrency.md`.

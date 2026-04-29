# Data Foundation Example

Status: Bootstrap data/capabilities foundation example.

This example shows the current JavaScript-only shape for database capability metadata,
query template lowering, a fake data provider service, and transaction callback semantics.

What works today:

- `Sloppy.module("data").capabilities(...)` declares a database capability token.
- `caps.addDatabase("data.main", { provider: "sqlite", access: "readwrite" })` stores
  metadata only.
- `data.createFakeProvider(...)` creates a test/example provider with `query`, `queryOne`,
  `exec`, and `transaction`.
- tagged query templates keep SQL text and parameter values separate.
- `sql` and provider methods lower placeholders to `?` by default.
- fake transactions record begin/commit/rollback behavior for tests.

What does not work yet:

- `sloppy run` does not exist yet;
- `sloppyc` does not compile this example;
- this example does not emit `app.plan.json`;
- there is no real HTTP server;
- the real SQLite provider is covered by native C tests, but this example still uses a fake
  JavaScript provider;
- PostgreSQL and SQL Server have separate provider examples, but this example deliberately
  stays on the fake JavaScript provider;
- no database connection is opened;
- no SQL is executed;
- filesystem and network capabilities are not enforced;
- migrations, pooling, cancellation, isolation levels, and native SQL execution are future
  work;
- the future bare `"sloppy"` import is planned only.

See `examples/sqlite-basic/`, `examples/postgres-basic/`, and `examples/sqlserver-basic/`
for provider registration shapes.

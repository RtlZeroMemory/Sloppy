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
- there is no real SQLite, PostgreSQL, or SQL Server provider yet;
- no database connection is opened;
- no SQL is executed;
- filesystem and network capabilities are not enforced;
- migrations, pooling, cancellation, isolation levels, and native SQL execution are future
  work;
- the future bare `"sloppy"` import is planned only.

SQLite provider work comes next. PostgreSQL and SQL Server providers come later.

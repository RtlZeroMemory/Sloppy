# Data Foundation Example

Bootstrap data/capabilities foundation example.
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

Current product state:

- This source-stdlib example is a checked-in API-shape fixture.
- `sloppy run --artifacts` currently runs emitted artifacts such as
  `examples/compiler-hello`.
- `sloppyc` compilation and `app.plan.json` emission for this data shape are future
  source-extraction work.
- The bounded `sloppy run` path currently loads generated artifacts, not this
  source-stdlib data example.
- The real SQLite provider is covered by native C tests. This example uses a fake
  JavaScript provider so the metadata and callback shape stay inspectable.
- PostgreSQL and SQL Server have separate provider examples. This example stays on the
  fake JavaScript provider.
- The fake provider records calls only; it opens no database connection and executes no SQL.
- Filesystem and network capability enforcement belong to their provider/runtime lanes.
- Migrations and ORM behavior are separate application-framework features.
- Native SQL execution, pooling, and true-async external providers are covered by
  provider-specific SQLite/PostgreSQL/SQL Server examples and tests.
- Bare `"sloppy"` imports are the current source shape for this example.

See `examples/sqlite-basic/`, `examples/postgres-basic/`, and `examples/sqlserver-basic/`
for provider registration shapes.

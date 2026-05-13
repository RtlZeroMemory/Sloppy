# SQLite Basic Example

This is a SQLite provider API-shape example with native provider coverage in C tests and a
separate SQLite runtime fixture.

This example shows the intended bootstrap shape for registering SQLite as `data.main`,
using in-memory configuration, and writing query-template-based route code.

What works today:

- the native C SQLite provider opens `:memory:` databases in tests;
- native C tests cover `exec`, `query`, `queryOne`, parameter binding, transactions, and
  diagnostics;
- V8-enabled runtime tests cover the real SQLite JS-to-native bridge through
  `data.sqlite("main")`, provider metadata, the native capability hook, and safe resource
  IDs;
- `Sloppy.module("data.sqlite").capabilities(...)` declares SQLite database metadata;
- `data.sqlite.open({ database: ":memory:", capability: "data.main" })` opens a native
  SQLite connection only when the V8 runtime installs the SQLite bridge intrinsics and has
  Plan-backed capability metadata;
- `data.sqlite.open({ database: ":memory:", capability: "data.main", access: "readwrite" })`
  is the canonical explicit shape. `path` remains only a transitional alias for
  `database`, and unsupported option fields fail before provider work;
- access is enforced twice: against Plan capability metadata before open/use, and against
  the opened resource mode before every operation. `read` permits `query`/`queryOne`,
  `write` permits `exec`/transaction writes, and `readwrite` permits both;
- result mapping is small and stable: `NULL` becomes `null`, integers/floats become
  JavaScript numbers, text becomes strings, blobs become `Uint8Array`, `queryOne` returns a
  row or `null`, and duplicate column names should be avoided with SQL aliases;
- `db.transaction(callback)` commits when the callback succeeds, rolls back when it throws
  or rejects, rejects nested transactions, and rejects transaction-object use after
  commit/rollback on the current synchronous SQLite bridge;
- query templates lower to `?` placeholders without interpolating values.
- public prepared statement handles are intentionally absent; SQLite prepare/bind/step/
  finalize remains internal to `exec`, `query`, `queryOne`, and transaction operations.

Current product state:

- This source-stdlib example is a checked-in API-shape fixture.
- `sloppy run --artifacts` currently runs emitted artifacts such as
  `examples/compiler-hello`.
- `sloppyc` compilation and `app.plan.json` emission for this SQLite source
  shape are planned separately.
- The bounded `sloppy run` path currently loads generated artifacts, not this
  source-stdlib SQLite example directly.
- Executable SQLite runtime coverage currently lives in the handler-execution
  artifact fixture.
- PostgreSQL and SQL Server providers are covered by their own examples and tests.
- PostgreSQL has its own true-async bridge and live PostgreSQL checks.
- ORM, migrations, expanded pooling, public prepared statement handles, and mid-operation
  interruption are separate SQLite/provider features.
- Bare `"sloppy"` imports are the current source shape for this example.

# SQLite Basic Example

Status: SQLite provider API-shape example with native provider coverage in C tests.

This example shows the intended bootstrap shape for registering SQLite as `data.main`,
using in-memory configuration, and writing query-template-based route code.

What works today:

- the native C SQLite provider opens `:memory:` databases in tests;
- native C tests cover `exec`, `query`, `queryOne`, parameter binding, transactions, and
  diagnostics;
- `Sloppy.module("data.sqlite").capabilities(...)` declares SQLite database metadata;
- `data.sqlite.open({ path: ":memory:" })` exists as the stdlib provider entry point and
  fails honestly until native stdlib intrinsics are wired;
- query templates lower to `?` placeholders without interpolating values.

What does not work yet:

- `sloppy run` does not exist yet;
- `sloppyc` does not compile this example;
- this example does not emit `app.plan.json`;
- there is no real HTTP server yet;
- the stdlib cannot call the native SQLite provider until runtime intrinsics are added;
- no PostgreSQL or SQL Server provider is implemented;
- no ORM, migrations, connection pooling, async worker offload, cancellation, deadline, or
  blob support is implemented;
- the future bare `"sloppy"` import is planned only.

# SQLite Basic Example

Status: SQLite provider API-shape example with native provider coverage in C tests and a
separate V8-gated SQLite bridge fixture.

This example shows the intended bootstrap shape for registering SQLite as `data.main`,
using in-memory configuration, and writing query-template-based route code.

What works today:

- the native C SQLite provider opens `:memory:` databases in tests;
- native C tests cover `exec`, `query`, `queryOne`, parameter binding, transactions, and
  diagnostics;
- V8-enabled runtime tests cover the real SQLite JS-to-native bridge through
  `__sloppy.data.sqlite` and safe resource IDs;
- `Sloppy.module("data.sqlite").capabilities(...)` declares SQLite database metadata;
- `data.sqlite.open({ path: ":memory:" })` opens a native SQLite connection only when the
  V8 runtime installs the SQLite bridge intrinsics;
- query templates lower to `?` placeholders without interpolating values.

What does not work yet:

- this source-stdlib example is not a `sloppy run --artifacts` app;
- `sloppyc` does not compile this example;
- this example does not emit `app.plan.json`;
- the current `sloppy run` MVP does not load this source-stdlib SQLite example;
- the executable SQLite proof is the internal V8-gated fixture, not this public
  source-stdlib example yet;
- PostgreSQL and SQL Server providers are covered by their own examples and native C tests;
- PostgreSQL and SQL Server do not have JS-to-native provider bridges yet;
- no ORM, migrations, connection pooling, async worker offload, cancellation, deadline, or
  blob support is implemented;
- the future bare `"sloppy"` import is planned only.

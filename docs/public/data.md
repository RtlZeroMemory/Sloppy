# Data

Status: Bootstrap data/capabilities foundation, native SQLite/PostgreSQL/SQL Server
providers, and a Plan/capability-wired V8-gated SQLite JavaScript bridge implemented.

Purpose: document future data provider modules, query templates, transactions, and
provider-specific limitations.

ENGINE-01 target contract:

- SQLite is the core foundation database provider.
- Canonical public SQLite open options use `database`, `capability`, and `access`.
  `path` remains a transitional alias for `database`; new docs and examples should use
  `database`.
- `data.sqlite.open`, `exec`, `query`, `queryOne`, `transaction`, and `close` are the
  foundation operations.
- `:memory:` examples are core conformance.
- file databases require a capability/path policy before public examples bless them.
- cancellation must be plumbed through request-context SQLite calls; sync-backed calls
  check cancellation before work and before result conversion until real interruption
  exists.
- prepared statement handles, ORM, migrations, query builders, PostgreSQL JavaScript
  bridge, and SQL Server JavaScript bridge remain deferred.

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
      database: ":memory:",
      access: "readwrite",
    });
  })
  .services(services => {
    services.addSingleton("data.main", () => data.sqlite.open({
      database: ":memory:",
      capability: "data.main",
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

SQL Server bootstrap service shape:

```ts
import { Sloppy, data } from "sloppy";

const SqlServerModule = Sloppy.module("data.sqlserver")
  .capabilities(caps => {
    caps.addDatabase("data.main", {
      provider: "sqlserver",
      configKey: "SLOPPY_SQLSERVER_TEST_CONNECTION_STRING",
      access: "readwrite",
    });
  })
  .services(services => {
    services.addSingleton("data.main", () => data.sqlserver.open({
      connectionString:
        "Driver={ODBC Driver 18 for SQL Server};Server=localhost;Database=sloppy_test;Trusted_Connection=yes;TrustServerCertificate=yes;",
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
- `data.sqlite` exposes SQLite provider metadata, callable provider shorthand, and
  `open(options)`. In a V8-enabled Sloppy runtime that installs SQLite intrinsics and has
  Plan/capability metadata, `data.sqlite("main")` resolves provider token `data.main` and
  returns a safe SQLite connection wrapper. In bootstrap-only or non-V8 contexts, it fails
  with a bridge-unavailable error.
- `data.postgres` exposes PostgreSQL provider metadata, `$1` placeholder style, connection
  string redaction, and `open(options)` as the future stdlib entry point.
- `data.sqlserver` exposes SQL Server provider metadata, ODBC `?` placeholder style,
  connection string redaction, a doctor helper shape, and `open(options)` as the future
  stdlib entry point.
- Plan v1 alpha can carry metadata-only `dataProviders` entries with a token, provider
  kind (`sqlite`, `postgres`, or `sqlserver`), optional service token, and optional
  capability token reference.
- ENGINE-02 compiler output can emit minimal SQLite `dataProviders` and database
  `capabilities` metadata from `builder.capabilities.addDatabase(...)`; this is metadata
  for later runtime/provider work and does not open a native provider by itself.
- MAIN1-10 adds a native capability registry and provider policy check hook for plan
  capability metadata. ENGINE-05 SQLite bridge calls that hook before open/read/write
  provider work and fails closed if the hook inputs are unavailable.
- native C SQLite tests execute real SQLite against `:memory:` databases.
- V8-gated conformance executes checked-in SQLite bridge artifact fixtures against
  in-memory databases. The success fixture creates/inserts/selects rows, returns JSON from
  a handler, and closes the resource; the denied fixture verifies capability denial returns
  a 500 response without claiming a broader security sandbox.
- native C PostgreSQL tests execute live libpq coverage only when `SLOPPY_POSTGRES_TEST_URL`
  is set; otherwise the separate live CTest is reported as skipped.
- native C SQL Server tests execute live ODBC coverage only when
  `SLOPPY_SQLSERVER_TEST_CONNECTION_STRING` is set; otherwise the separate live CTest is
  reported as skipped.
- native SQLite supports `exec`, `query`, `queryOne`, primitive parameter binding, and
  transactions.
- native PostgreSQL supports connection-string open/close, `exec`, `query`, `queryOne`,
  primitive parameter binding, a tiny bounded pool skeleton, diagnostics, and transactions.
- native SQL Server supports ODBC connection-string open/close, `exec`, `query`,
  `queryOne`, primitive parameter binding through ODBC, a tiny bounded pool skeleton,
  missing-driver diagnostics, redaction, and transactions.
- fake provider methods accept tagged templates or already-lowered query objects.
- SQLite bridge methods accept raw SQL strings plus optional positional parameter arrays;
  the ESM stdlib wrapper also accepts existing lowered query objects.
- `queryOne` uses a supplied handler or falls back to the first row returned by `query`.
- `transaction(callback)` passes a transaction object with `query`, `queryOne`, and `exec`,
  commits when the callback resolves, and rolls back when it throws or rejects.

SQLite JS bridge support:

```ts
const db = data.sqlite("main");

db.exec("create table users (id integer primary key, name text)");
db.exec("insert into users (name) values (?)", ["Ada"]);

const row = db.queryOne("select name from users where id = ?", [1]);

db.close();
```

The bridge is intentionally small. It supports `open`, `close`, `exec`, `query`, and
`queryOne` for SQLite only. It returns arrays/plain objects, maps SQLite `NULL` to
JavaScript `null`, and supports primitive positional parameters: `null`, string, number,
and boolean. The wrapper stores only an opaque resource ID object; stale, closed, invalid,
wrong-kind, missing-provider, and denied-capability handles fail before provider code runs.
Double close is idempotent at the wrapper level.

Layering matters for future providers: the public stdlib wrapper is JavaScript, the native
resource table is engine-owned, and provider-specific V8 conversion code lives in
`src/engine/v8/intrinsics_<provider>.cc`. SQLite uses `intrinsics_sqlite.cc`; later
PostgreSQL or SQL Server bridges should not add provider logic directly to `engine_v8.cc`.

Not implemented yet:

- no JavaScript-to-native PostgreSQL intrinsic bridge yet, so `data.postgres.open(...)`
  validates/redacts options and fails with an honest bridge-unavailable error in the stdlib;
- no JavaScript-to-native SQL Server intrinsic bridge yet, so `data.sqlserver.open(...)`
  validates/redacts options and fails with an honest bridge-unavailable error in the stdlib;
- no SQL parser, ORM, migrations, production pooling, cancellation, isolation levels,
  transactions through the SQLite JS wrapper, or PostgreSQL/SQL Server native SQL execution
  from JavaScript;
- no public file database policy beyond the native provider accepting SQLite paths;
- no compiler extraction of JavaScript template literals.

CLI status:

- `sloppy doctor` can report deterministic provider readiness metadata supplied through
  `doctorChecks` and reports whether provider/capability metadata is present in a
  native-validated plan;
- live PostgreSQL and SQL Server checks are not run by default and remain opt-in future CLI
  work;
- default CI and package smoke do not prove live database availability, SQL Server driver
  installation, or V8-gated SQLite JS-to-native provider execution;
- doctor output redacts connection-string-like secrets before printing;
- `sloppy audit` can flag incomplete provider metadata, missing capability references,
  statically-known insufficient access, and filesystem/network skeleton notes. It remains
  metadata-oriented and does not execute providers or validate live reachability.

Related internal docs: `docs/data-providers.md`, `docs/concurrency.md`.

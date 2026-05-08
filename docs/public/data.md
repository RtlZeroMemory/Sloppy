# Data

Status: Bootstrap data/capabilities foundation, native SQLite/PostgreSQL/SQL Server
providers, Plan/capability-wired V8-gated SQLite, PostgreSQL, and SQL Server JavaScript
bridges implemented. V8-gated SQLite fixtures run through the configured V8 lane;
PostgreSQL and SQL Server bridge evidence additionally require their live provider
connection settings and dependencies.

Purpose: document future data provider modules, query templates, transactions, and
provider-specific limitations.

Current target contract:

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
- public prepared statement handles, ORM, migrations, and query builders remain deferred.
- the users API proof is not a public alpha, production HTTP edge, benchmark, ORM,
  migration, or SQL Server bridge claim.

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
import { Environment } from "sloppy/os";

function requireEnvironment(name) {
  const value = Environment.get(name);
  if (value === undefined || value === "") {
    throw new Error(`Missing required environment value: ${name}`);
  }
  return value;
}

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
      connectionString: requireEnvironment("SLOPPY_POSTGRES_TEST_URL"),
      maxConnections: 2,
    }));
  });
```

SQL Server bootstrap service shape:

```ts
import { Sloppy, data } from "sloppy";
import { Environment } from "sloppy/os";

function requireEnvironment(name) {
  const value = Environment.get(name);
  if (value === undefined || value === "") {
    throw new Error(`Missing required environment value: ${name}`);
  }
  return value;
}

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
      connectionString: requireEnvironment("SLOPPY_SQLSERVER_TEST_CONNECTION_STRING"),
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
- SQL operation methods accept an optional operation-options object with `signal`,
  `deadline`, and `timeoutMs`. The stdlib checks already-aborted signals and expired
  deadlines before provider dispatch and forwards the normalized option state to JS fake
  providers. Current native SQL bridges treat this as Slop-side terminal admission
  behavior; provider-specific active interruption, such as SQLite interruption, libpq
  cancel, or ODBC statement cancellation while a native call is already running, remains
  separately tested provider work before it can be claimed.
- `data.sqlite` exposes SQLite provider metadata, callable provider shorthand, and
  `open(options)`. In a V8-enabled Sloppy runtime that installs SQLite intrinsics and has
  Plan/capability metadata, `data.sqlite("main")` resolves provider token `data.main` and
  returns a safe SQLite connection wrapper with the normal `readwrite` default. Explicit
  open uses
  `data.sqlite.open({ database, capability, access })`; `database` is canonical, `path` is
  a transitional alias, `capability` is required, `access` defaults to `readwrite`, and
  unsupported option fields fail clearly. Use explicit `access: "read"` for read-only
  capabilities. In bootstrap-only or non-V8 contexts, it fails with a bridge-unavailable
  error.
- `data.postgres` exposes PostgreSQL provider metadata, `$1` placeholder style, connection
  string redaction, and `open(options)`. In a V8-enabled runtime that installs PostgreSQL
  intrinsics and has Plan/capability metadata, it returns a safe wrapper around a
  nonblocking libpq-backed connection pool. In bootstrap-only or non-V8 contexts, it fails
  with a bridge-unavailable error.
- `data.sqlserver` exposes SQL Server provider metadata, ODBC `?` placeholder style,
  connection string redaction, a doctor helper shape, and `open(options)`. In a
  V8-enabled runtime that installs SQL Server intrinsics, has Plan/capability metadata,
  and can enable required ODBC async behavior, it returns a safe wrapper around a bounded
  ODBC connection pool. In bootstrap-only, non-V8, missing-driver, or async-unavailable
  contexts, it fails with a bridge/driver-unavailable error.
- Plan v1 alpha can carry metadata-only `dataProviders` entries with a token, provider
  kind (`sqlite`, `postgres`, or `sqlserver`), optional service token, and optional
  capability token reference.
- Compiler output can emit minimal SQLite `dataProviders` and database
  `capabilities` metadata from `builder.capabilities.addDatabase(...)`; this is metadata
  for later runtime/provider work and does not open a native provider by itself.
- The native runtime includes a capability registry and provider policy check hook for plan
  capability metadata. The SQLite bridge calls that hook before open/read/write
  provider work and fails closed if the hook inputs are unavailable.
- native C SQLite tests execute real SQLite against `:memory:` databases.
- The conformance matrix exposes the native provider path as
  `conformance.sqlite.native_provider` and the native capability/admission paths as
  `conformance.capability.native_registry` and
  `conformance.capability.provider_executor`.
- V8-gated conformance executes checked-in SQLite bridge artifact fixtures against
  in-memory databases. The success fixture creates/inserts/selects rows, returns JSON from
  a handler, and closes the resource; the denied fixture verifies capability denial returns
  a 500 response without claiming a broader security sandbox.
- V8-gated PostgreSQL live conformance executes a checked-in artifact fixture against a
  configured live PostgreSQL database through `data.postgres.open`, parameterized exec,
  query, callback transactions, bounded pooling, and owner-thread Promise settlement. It
  is skipped unless `SLOPPY_POSTGRES_TEST_URL` is configured.
- native C PostgreSQL tests execute live libpq coverage only when `SLOPPY_POSTGRES_TEST_URL`
  is set; otherwise the separate live CTest is reported as skipped.
- native C SQL Server tests execute live ODBC coverage only when
  `SLOPPY_SQLSERVER_TEST_CONNECTION_STRING` is set; otherwise the separate live CTest is
  reported as skipped.
- native SQLite supports `exec`, `query`, `queryOne`, primitive parameter binding, and
  transactions.
- native PostgreSQL supports connection-string open/close, `exec`, `query`, `queryOne`,
  primitive parameter binding, bounded pool lifecycle behavior, diagnostics, and
  transactions.
- native SQL Server supports ODBC connection-string open/close, `exec`, `query`,
  `queryOne`, primitive parameter binding through ODBC, a tiny bounded pool skeleton,
  missing-driver diagnostics, redaction, and transactions.
- fake provider methods accept tagged templates or already-lowered query objects.
- SQLite bridge methods accept raw SQL strings plus optional positional parameter arrays;
  the ESM stdlib wrapper also accepts existing lowered query objects.
- `queryOne` uses a supplied handler or falls back to the first row returned by `query`.
- `transaction(callback)` passes a transaction object with `query`, `queryOne`, and `exec`,
  commits when the callback resolves, and rolls back when it throws or rejects. Nested
  transactions are rejected, and transaction objects fail after commit/rollback.

SQLite JS bridge support:

```ts
const db = data.sqlite("main");

db.exec("create table users (id integer primary key, name text)");
db.exec("insert into users (name) values (?)", ["Ada"]);

const row = db.queryOne("select name from users where id = ?", [1]);

await db.transaction(async tx => {
  tx.exec("insert into users (name) values (?)", ["Grace"]);
});

db.close();
```

The bridge is intentionally small. It supports `open`, `close`, `exec`, `query`,
`queryOne`, and callback transactions for SQLite only. It returns arrays/plain objects,
maps SQLite `NULL` to JavaScript `null`, and supports primitive positional parameters:
`null`, string, number, and boolean. Boolean parameters bind as SQLite integers `0` or
`1`; blob results map to `Uint8Array`; duplicate column names use normal object assignment,
so the last duplicate column wins unless SQL aliases make names unique. The wrapper stores
only an opaque resource ID object; stale, closed, invalid, wrong-kind, transaction
use-after-close, missing-provider, and denied-capability handles fail before provider code
runs. A `read` handle can query/queryOne only, a `write` handle can exec/write only, and
`readwrite` permits both. Double close is idempotent at the wrapper level. Public prepared
statement handles are absent by design; internal
prepare/bind/step/finalize remains per operation until a later resource-lifetime task.

PostgreSQL JS bridge support:

```ts
const db = data.postgres.open({
  connectionString: Environment.get("SLOPPY_POSTGRES_TEST_URL"),
  capability: "data.main",
  maxConnections: 2,
});

await db.exec("insert into users (name) values ($1)", ["Ada"]);

const rows = await db.query("select id, name from users where name = $1", ["Ada"]);

await db.transaction(async tx => {
  await tx.exec("insert into users (name) values ($1)", ["Grace"]);
});

db.close();
```

The PostgreSQL bridge uses libpq nonblocking connect/query state machines and
Slop-owned socket readiness watches. It does not occupy a blocking worker while waiting for
database I/O. JavaScript sees only a stdlib connection wrapper and opaque Sloppy resource
ID. Supported parameters include `null`, booleans, numbers, int64 `bigint`, strings,
`Uint8Array` bytea values, and scalar arrays encoded for PostgreSQL. Result mapping returns
booleans and numeric primitives as JavaScript primitives, bytea as `Uint8Array`, nulls as
`null`, and PostgreSQL textual/domain values such as numeric, uuid, JSON, date/time, and
timestamps as strings unless a later typed-value policy promotes them. Callback
transactions pin one pooled connection until commit or rollback; nested transactions are
rejected.

SQL Server JS bridge support follows the same stdlib wrapper shape as PostgreSQL, using
ODBC `?` placeholders and live-lane configuration through
`SLOPPY_SQLSERVER_TEST_CONNECTION_STRING`. It is live-provider evidence only when the SQL
Server V8 lane runs with an ODBC driver and reachable service; default tests do not prove
live SQL Server behavior.

Layering matters for future providers: the public stdlib wrapper is JavaScript, the native
resource table is engine-owned, and provider-specific V8 conversion code lives in
`src/engine/v8/intrinsics_<provider>.cc`. SQLite uses `intrinsics_sqlite.cc`, PostgreSQL
uses `intrinsics_postgres.cc`, and SQL Server uses `intrinsics_sqlserver.cc`; future
providers should follow that provider-owned bridge shape rather than adding provider logic
directly to `engine_v8.cc`.

Not implemented yet:

- no SQL parser, ORM, migrations, production operational pooling policy, public prepared
  statement handles, hosted live-provider service CI, or default live SQL Server execution;
- no public file database policy beyond the native provider accepting SQLite paths;
- no compiler extraction of JavaScript template literals.

CLI status:

- `sloppy doctor` can report deterministic provider readiness metadata supplied through
  `doctorChecks` and reports whether provider/capability metadata is present in a
  native-validated plan;
- live PostgreSQL and SQL Server checks are not run by default; live bridge evidence is
  available through the opt-in CTest/tooling lanes when the relevant environment variable
  and provider dependencies are configured;
- default CI and package smoke do not prove live database availability, SQL Server driver
  installation, or V8-gated provider execution;
- doctor output redacts connection-string-like secrets before printing;
- `sloppy audit` can flag incomplete provider metadata, missing capability references,
  statically-known insufficient access, and filesystem/network skeleton notes. It remains
  metadata-oriented and does not execute providers or validate live reachability.

Related internal docs: `docs/data-providers.md`, `docs/concurrency.md`.

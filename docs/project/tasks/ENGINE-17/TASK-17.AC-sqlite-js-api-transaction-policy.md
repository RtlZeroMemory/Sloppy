# TASK ENGINE-17.A/C: SQLite JS API and Transaction Policy

Status: implemented in this PR.

Issues:

- #340 TASK ENGINE-17.A: SQLite Public JS API Finalization
- #342 TASK ENGINE-17.C: SQLite Transactions and Prepared Statement Decision
- #315 EPIC ENGINE-17: SQLite Runtime and Data Access Completion

Scope:

- finalize the public SQLite JS wrapper shape around `data.sqlite("main")`,
  `data.sqlite.open({ database, capability, access })`, `close`, `exec`, `query`,
  `queryOne`, and `transaction(callback)`;
- keep `database` canonical and `path` as a documented transitional alias only;
- require capability metadata for explicit provider-backed opens;
- fail unsupported explicit-open option fields clearly;
- implement callback transaction semantics on the current synchronous SQLite bridge:
  success commits, throw/reject rolls back, nested transactions reject, and transaction
  objects fail after commit/rollback;
- keep public prepared statement handles deferred until statement resources, capability
  checks, cleanup, stale handles, and tests can be implemented together.

Non-goals:

- no HTTP backend changes;
- no ENGINE-17.E users API proof;
- no ORM, migrations, pooling expansion, public prepared statement handles, PostgreSQL
  bridge, SQL Server bridge, package-manager behavior, public alpha docs, or benchmark
  claims;
- no SQLite conversion to the ENGINE-23 provider executor in this slice.

Evidence:

- Native SQLite provider transaction and resource behavior remains covered by
  `data.sqlite.provider`.
- Bootstrap stdlib tests cover explicit-open option validation, wrapper methods,
  transaction commit/rollback/nested/use-after-close behavior, stale/closed handles, and
  absent public prepared handles with a mocked native bridge.
- V8-gated SQLite bridge tests cover the native transaction intrinsics when the V8 SDK lane
  is available.

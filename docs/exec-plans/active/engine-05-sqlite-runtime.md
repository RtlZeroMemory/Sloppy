# ENGINE-05 SQLite End-to-End Runtime

Status: implemented in `feature/engine-05-sqlite-runtime`; keep this plan with the PR until
merge.

## Scope

Implement SQLite as the first real Sloppy handler data runtime:

- public JS API: `data.sqlite("main")` plus explicit
  `data.sqlite.open({ database, capability, access })`;
- native V8 bridge: open, close, exec, query, queryOne;
- resource table handles only; no raw native pointers in JavaScript;
- Plan provider resolution and database capability hook checks before provider work;
- deterministic cleanup through explicit close and engine resource-table disposal;
- V8-gated handler fixture returning JSON from SQLite rows.

## Non-Goals

- no PostgreSQL or SQL Server JavaScript bridge;
- no ORM, migrations, pooling framework, prepared statement handles, or JS transaction API;
- no async database engine rewrite or worker-thread offload;
- no Node compatibility or package-manager behavior;
- no ENGINE-06 policy-engine work beyond calling the existing capability hook and failing
  closed when hook inputs are unavailable.

## Implementation Notes

- `SlEngineOptions` borrows optional `SlPlan` and `SlCapabilityRegistry` pointers. The app
  host owns their lifetime and passes them into V8 creation.
- `sloppy run` initializes the capability registry immediately after parsing the plan.
- `intrinsics_sqlite.cc` resolves provider tokens from `dataProviders`, validates provider
  kind/database metadata, stores capability/provider tokens beside each SQLite resource, and
  calls `sl_capability_check_database(...)` for open/read/write operations.
- Missing provider metadata, missing hook inputs, closed/stale/wrong-kind handles, invalid
  SQL, invalid params, and denied capabilities all fail before unchecked provider work.

## Evidence Targets

- default tests: stdlib API shape, native SQLite provider lifecycle/query tests, capability
  registry tests, resource cleanup/stale-handle tests;
- V8 tests: SQLite bridge success, stale handle, invalid params, missing provider, missing
  capability registry, denied read capability;
- V8 conformance: checked-in `/sqlite` artifact returns JSON rows; checked-in
  `/sqlite-denied` artifact returns a 500 for denied read access.

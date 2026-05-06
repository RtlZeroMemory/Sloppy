# SQLite Conformance

Status: native provider conformance plus V8-gated executable bridge and users API
transport fixtures.

Source fixtures:

- `tests/unit/data/test_sqlite.c`
- `tests/integration/execution/sqlite_bridge/`
- `examples/users-api-sqlite/`
- `tests/scripts/test_users_api_sqlite_transport.ps1`

Default native command:

```powershell
ctest -R conformance.sqlite.native_provider --output-on-failure
```

Expected native behavior:

- open and close `:memory:` databases and reject use after close;
- execute DDL/DML, insert rows, query rows, and return `queryOne` found/not-found states;
- copy text and blob values into arena-owned result storage, preserving embedded bytes;
- map null, integer, float, text, boolean, blob, and empty blob parameters/results;
- reject unsupported parameters, trailing SQL, arity mismatch, invalid open options, and
  invalid SQL with stable provider diagnostics;
- redact SQL parameter values from provider diagnostics;
- commit, rollback, and resynchronize transaction state; and
- clean up SQLite resources exactly once.

Run command when V8 is enabled:

```powershell
sloppy run --artifacts tests/integration/execution/sqlite_bridge --once GET /sqlite
```

Expected behavior:

- open an in-memory SQLite database through the JavaScript wrapper with a declared
  `data.main` capability;
- create and populate a table;
- insert one row through `transaction(callback)` and commit on callback success;
- select one row with a positional parameter;
- return a JSON result containing `Ada`;
- close the resource in a `finally` block.
- deny missing/insufficient/mismatched capabilities before SQLite provider work in the
  V8-gated smoke tests.

Users API transport command when V8 is enabled:

```powershell
ctest -R conformance.users_api_sqlite.localhost_transport --output-on-failure
```

Expected users API behavior:

- build `examples/users-api-sqlite/app.js` with `sloppyc`;
- start `sloppy run --artifacts` on loopback TCP;
- verify `GET /users`, `GET /users/{id}`, missing-user `404`, `POST /users`, follow-up
  `GET /users`, invalid JSON `400`, and denied SQLite capability behavior.

Default evidence: native C SQLite provider tests cover `:memory:` open, exec, query,
`queryOne`, primitive parameters, transactions, and diagnostics. The default suite does
not prove the V8 JavaScript bridge or localhost transport executed.

Gated/deferred requirements: PostgreSQL and SQL Server JavaScript bridges are not part of
this conformance suite. The users API fixture is localhost transport evidence, not
production-edge HTTP evidence. Filesystem and network capability checks remain Sloppy
policy/metadata evidence and do not prove OS-level containment.

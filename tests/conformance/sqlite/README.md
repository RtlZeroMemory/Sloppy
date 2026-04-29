# SQLite Conformance

Status: V8-gated executable bridge fixture.

Source fixture: `tests/integration/execution/sqlite_bridge/`.

Run command when V8 is enabled:

```powershell
sloppy run --artifacts tests/integration/execution/sqlite_bridge --once GET /sqlite
```

Expected behavior:

- open an in-memory SQLite database through the JavaScript wrapper;
- create and populate a table;
- select one row with a positional parameter;
- return a JSON result containing `Ada`;
- close the resource in a `finally` block.

Default evidence: native C SQLite provider tests cover `:memory:` open, exec, query,
`queryOne`, primitive parameters, transactions, and diagnostics. The default suite does
not prove the V8 JavaScript bridge executed.

Gated/deferred requirements: PostgreSQL and SQL Server JavaScript bridges are not part of
this conformance suite. SQLite capability-policy enforcement in the JavaScript bridge is
deferred until the bridge calls the native capability hook.

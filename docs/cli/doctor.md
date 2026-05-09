# `sloppy doctor`

Check that the local environment and (optionally) a Plan are healthy.
Useful as the first command to run when something isn't working.

```
sloppy doctor [--plan <path>] [--format text|json]
```

## What it checks

- The CLI version and build flags (V8 enabled, debug/release).
- The bootstrap stdlib is present and at a compatible version.
- If `--plan` is given: the Plan parses, schema version is supported,
  artifact hashes match, required features are activatable.
- Provider dependencies: SQLite is built-in; PostgreSQL needs `libpq`;
  SQL Server needs an ODBC driver. `doctor` reports each provider as
  `available`, `unavailable`, or `unknown`.

## Output

**Text** (default):

```
$ sloppy doctor --plan .sloppy/app.plan.json
sloppy 0.x.y  (V8 enabled, RelWithDebInfo)
stdlib       : available  bootstrap.manifest.json @ 0.1.0
plan         : ok         schema=plan/v1-alpha
sqlite       : available
postgres     : unavailable  libpq not found
sqlserver    : unknown      ODBC driver scan deferred
```

**JSON**: same data, machine-readable.

## Exit codes

- `0` when every check passes.
- `1` when one or more checks fail (e.g. unsupported Plan, missing required
  feature).

`unavailable` and `unknown` provider statuses do *not* fail the command —
they're informational. They become failures only if a Plan declares a
capability the local runtime can't satisfy.

## Use cases

- New machine setup: confirm `sloppy` and dependencies are wired up.
- Troubleshooting: get a one-shot view of the runtime's view of the world.
- CI pre-flight: gate the build on `sloppy doctor` returning 0.

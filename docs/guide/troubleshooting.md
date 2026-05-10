# Troubleshooting

Common errors and how to read them. The first stop for any unexpected
failure is `sloppy doctor` — it answers most "is my environment OK"
questions on its own.

## Build errors

### Unsupported syntax

```
error: cannot extract route metadata from this expression
  --> src/main.ts:12:3
12 |   for (const route of routes) {
   |   ^^^
hint: route registrations must be top-level static calls
```

The compiler couldn't statically analyze a route registration. See
[guide/typescript.md](typescript.md) for the supported subset. Common
fixes:

- Replace `for`-loop registrations with explicit calls or a module per route.
- Hoist computed strings into top-level `const` literals.
- Move conditional logic *into* the handler instead of around the
  registration.

### Import not allowed

```
SLOPPYC_E_PACKAGE_NOT_FOUND
Package "lodash" was not found from src/main.ts.
hint: Install it with your package manager, for example: npm install lodash
```

Sloppy resolves installed packages from `node_modules`; it does not install
them. Install the dependency with your package manager, or remove/vendor the
code.

If the package is installed but still fails, check for a more specific
diagnostic:

- `SLOPPYC_E_PACKAGE_EXPORT_UNSUPPORTED`: the package's `exports` shape is not
  in Sloppy's supported subset yet.
- `SLOPPYC_E_NATIVE_ADDON_UNSUPPORTED`: the package needs a native Node addon.
- `SLOPPYC_E_UNSUPPORTED_NODE_BUILTIN`: the package imports a Node builtin that
  Sloppy's compatibility registry does not support.

## Runtime startup errors

### V8 unavailable

```
sloppy run: handler execution requires a V8-enabled build
```

Your CLI was built without V8. `sloppy build` still works; `sloppy run`
needs V8. On a source build, run `tools\windows\resolve-v8-sdk.ps1 -Fetch`
and reconfigure with `-EnableV8`. With a published archive, use one that
includes V8.

### Plan schema mismatch

```
sloppy run: app.plan.json schema 'plan/v1-alpha' is unsupported
hint: this Plan was produced by an older or newer compiler version
```

Recompile with the same `sloppy` version. Plans are pinned to a schema
that the runtime validates strictly.

### Artifact hash mismatch

```
sloppy run: app.js hash mismatch
```

The Plan recorded a hash that doesn't match the bundle next to it on disk.
Almost always means the artifacts were edited or partially overwritten.
Run `sloppy build` to regenerate.

### Required feature missing

```
sloppy run: required feature 'postgres' is not available
```

The Plan declares a capability the runtime can't satisfy. Check
`sloppy doctor` to see which providers are available. For PostgreSQL/SQL
Server, install `libpq` / an ODBC driver and re-run.

## HTTP-level errors

| Status | Cause                                           | Fix                                          |
| ------ | ----------------------------------------------- | -------------------------------------------- |
| 400    | Malformed JSON body                             | Send valid JSON, with the right `Content-Type` |
| 404    | No route matched                                | Run `sloppy routes` to confirm registrations |
| 405    | Method not allowed for this path                | Add the verb, or hit the right path          |
| 413    | Body exceeded the configured request limit      | Raise the configured server max-body-bytes   |
| 415    | Unsupported `Content-Type`                      | Use `application/json` or `text/plain` today |
| 500    | Handler threw or returned an unsupported result | Check the diagnostic on stderr               |
| 501    | Transfer encoding not accepted                  | Avoid `Transfer-Encoding: chunked` for now   |

## Provider errors

### "database file is locked" (SQLite)

A different process or a previous run still holds the file. SQLite is
single-writer. Either close the other connection or use `:memory:` for
tests.

### Postgres connection refused

The connection string passed to `data.postgres.open({ connectionString })`
didn't resolve to a reachable server. Verify with `psql` first, then
re-run `sloppy run`. Provider examples read the value from a single
environment variable (e.g. `SLOPPY_POSTGRES_TEST_URL`) using
`Environment.get(...)` from `"sloppy/os"`.

### SQL Server "driver not found"

ODBC isn't seeing the driver. On Linux, install `unixodbc` plus a vendor
driver. On Windows, install MSODBCSQL. `sloppy doctor` reports driver
status under `sqlserver`.

## "Why is my route not being called?"

Run, in order:

```
sloppy build
sloppy routes --plan .sloppy
```

If the route isn't listed there, the compiler didn't extract it. Check the
build output for a diagnostic. If it *is* listed but a request still 404s,
make sure the URL has the right method and trailing-slash shape — Sloppy
treats `/users` and `/users/` as distinct routes.

## Getting better diagnostics

- `sloppy doctor --plan .sloppy` runs every available check.
- `sloppy build --environment Development` keeps full source mapping in
  diagnostics.
- Set `logging:minimumLevel` to `debug` (in `appsettings.Development.json`)
  to surface verbose runtime logs.

If a diagnostic doesn't make sense, the source code path it points at is
the authoritative answer. Open it.

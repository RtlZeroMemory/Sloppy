# `sloppy doctor`

Check the local runtime, packaged assets, optional provider dependencies, and
optionally a compiled Plan.

```sh
sloppy doctor [artifacts-dir|plan.json|--plan <path>|--artifacts <dir>] [--format text|json] [--dispatch]
```

## What it checks

- bootstrap runtime assets;
- V8 bridge status for the current build/package;
- live provider configuration;
- native Plan parsing;
- route, provider, and capability metadata, including Program Plans with no
  route metadata by design and web Plans with partial/dynamic route metadata;
- route dispatch metadata when `--dispatch` is passed;
- dependency graph metadata and compatibility findings;
- native FFI metadata, dynamic library load checks, and symbol resolution;
- SQLite metadata and the fact that SQLite needs no external DB driver;
- PostgreSQL provider metadata with optional client-library guidance when the
  app uses PostgreSQL;
- SQL Server provider metadata with optional Microsoft ODBC Driver 17/18
  guidance when the app uses SQL Server.

## Text output

Current text output uses status-prefixed lines:

```text
Sloppy Doctor

[ok] bootstrap.assets: bootstrap runtime asset found
[warn] engine.v8: V8 bridge disabled in this build; V8 runtime tests not run
[warn] providers.live: live provider checks are not configured by default and no live DB was contacted
[warn] package.runtime: package release status requires package smoke
[ok] app.plan.parse: app plan parsed by native Plan v1 parser
[warn] app.plan.routes: no route metadata present
[warn] app.plan.providers: provider metadata not present
[warn] app.plan.capabilities: capability metadata not present
[ok] dependency.graph: dependency graph available: 1 package(s), 4 module(s), 1 Node binding(s)
[ok] ffi.native: native FFI metadata available: 1 library, 4 function(s)
[ok] sqlite.provider: SQLite is embedded; SQLite providers do not require PostgreSQL, SQL Server, libpq, or ODBC
[warn] postgres.provider: PostgreSQL is optional; apps that use it need a connection string and libpq from a bundled provider package when available or from the system
[warn] sqlserver.provider: SQL Server is optional; apps that use it need Microsoft ODBC Driver 17 or 18 visible to the driver manager
```

The exact rows depend on the current platform, build, package, and provider
environment.

With `--dispatch`, doctor also reports the Plan route dispatch mode, artifact
status/hash, classic fallback availability, segment-trie metadata, native
no-JS endpoint count, and native URL writer count. In this alpha,
`native-compiled` means the compiler emitted `routes.slrt` and the runtime
validated it before building the native dispatch table.

## Exit codes

| Code | Meaning |
| --- | --- |
| `0` | No errors |
| `1` | One or more checks reported `error` |

Warnings are informational unless the checked Plan requires the unavailable
feature. Missing PostgreSQL or SQL Server support is reported as an optional
provider dependency, not as a broken Sloppy install for SQLite-only apps.

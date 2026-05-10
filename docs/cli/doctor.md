# `sloppy doctor`

Check the local runtime, packaged assets, optional provider dependencies, and
optionally a compiled Plan.

```sh
sloppy doctor [artifacts-dir|plan.json|--plan <path>|--artifacts <dir>] [--format text|json]
```

## What it checks

- bootstrap runtime assets;
- V8 bridge status for the current build/package;
- live provider configuration;
- native Plan parsing;
- route, provider, and capability metadata, including Program Plans with no
  route metadata by design and web Plans with partial/dynamic route metadata;
- native FFI metadata, dynamic library load checks, and symbol resolution;
- SQLite metadata;
- PostgreSQL live-test environment;
- SQL Server ODBC driver availability.

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
[ok] sqlite.provider: sqlite provider metadata available
[warn] postgres.live: live PostgreSQL check skipped because SLOPPY_POSTGRES_TEST_URL is not set
[error] sqlserver.driver: connectionString=<redacted>; Microsoft ODBC Driver 18 for SQL Server not found
```

The exact rows depend on the current platform, build, package, and provider
environment.

## Exit codes

| Code | Meaning |
| --- | --- |
| `0` | No errors |
| `1` | One or more checks reported `error` |

Warnings are informational unless the checked Plan requires the unavailable
feature.

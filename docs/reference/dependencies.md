# Native Dependencies

Most users do not need extra native libraries to try Sloppy.

The npm alpha package includes the runtime pieces needed for the supported
platform. The default `api` template uses SQLite and does not require
PostgreSQL, SQL Server, libpq, ODBC, or FFI setup.

This page explains optional native dependencies used by specific features.

## Quick map

| Feature | Library | When you need it |
| --- | --- | --- |
| Handler execution (`sloppy run`) | V8 | Any time the runtime evaluates JavaScript handlers. CLI commands that only read artifacts (`build`, `routes`, `capabilities`, `audit`, `openapi`, `doctor`, `package`) do not need V8. |
| Native FFI (`sloppy/ffi`) | libffi plus the declared native libraries | Apps that import `sloppy/ffi` and execute typed C ABI calls. Build-time metadata commands can inspect the Plan without loading user native libraries, but runtime execution and `doctor` load checks need the libraries available. |
| SQLite provider | `sqlite3` | Always available; statically embedded in the runtime. |
| PostgreSQL provider | PostgreSQL client support (`libpq`) | Apps that use `Postgres<"name">`, `data.postgres.*`, or PostgreSQL migrations. |
| SQL Server provider | Microsoft ODBC Driver 17 or 18 | Apps that use `SqlServer<"name">`, `data.sqlserver.*`, or SQL Server migrations. |
| TLS (inbound and `HttpClient` outbound) | OpenSSL | Any time TLS is configured for the development server, or when `HttpClient` opens an `https://` URL. |
| Plan parsing | `yyjson` | Always — bundled into the native runtime, no user setup. |

`sloppy doctor` reports optional provider dependencies as provider-specific
guidance. Missing PostgreSQL or SQL Server support is not a broken Sloppy
install unless the app uses that provider.

## PostgreSQL

PostgreSQL is optional.

You only need PostgreSQL client support when your app uses the PostgreSQL
provider or runs PostgreSQL migrations.

Sloppy's PostgreSQL provider-package design uses this lookup order:

1. explicit configured libpq path, if provided;
2. bundled/provider package libpq, when installed and available;
3. system libpq.

Current public alpha packages do not yet ship package-local libpq binaries.
They use the system or build-provided libpq path today, and the package-local
provider package path is a follow-up. If PostgreSQL support is unavailable,
`sloppy doctor` and `sloppy db` report a provider-specific fix. The Quickstart
and SQLite apps are unaffected.

## SQL Server

SQL Server is optional.

You only need Microsoft's ODBC driver when your app uses the SQL Server
provider or runs SQL Server migrations.

Sloppy does not bundle Microsoft's ODBC driver in the core alpha package. Use
Microsoft's official platform package or your organization's managed driver
deployment. `sloppy doctor` reports whether a suitable driver is visible.

## Where each library is used

- `yyjson` parses `app.plan.json` strictly; it is part of the runtime build.
- `sqlite3` backs `data.sqlite` and `Sqlite<"name">` injection.
- PostgreSQL client support and ODBC back `data.postgres` / `data.sqlserver`
  and the matching typed providers when those optional providers are used.
- V8 lives behind a narrow C++ bridge under `src/engine/v8/` and is only
  pulled in when the build is configured with V8 enabled.
- libffi backs `sloppy/ffi` typed native calls. User-declared native libraries
  are loaded through the platform dynamic loader or through package manifest
  path overrides for copied local libraries.
- OpenSSL is invoked through the inbound transport and the private outbound
  TLS bridge that `HttpClient` uses for HTTPS.

## Package Dependencies

Installed package graph support is experimental. Sloppy can read already
installed `node_modules` packages at build time when a package is pure
JavaScript and fits Sloppy's supported package resolver, module loader, and
runtime API boundary. The compiler bundles compatible modules into the
generated artifacts and records them in the dependency graph.

The runtime does not bundle a JavaScript package manager, an ORM, a migration
tool, a registry installer, or native Node addon support. See
[Using installed packages](../guide/using-packages.md) and
[Dependency graph](dependency-graph.md).

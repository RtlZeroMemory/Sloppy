# Native Dependencies

Most users do not need extra native libraries to try Sloppy.

The npm alpha package includes the runtime pieces it needs for the supported
platform. The default `api` template uses SQLite, runs entirely with what npm
installs, and does not require PostgreSQL, SQL Server, ODBC, `libpq`, or
FFI setup. Program Mode and the `package-api` template are the same — no
extra native setup.

This page is a map of optional native dependencies pulled in by specific
features: PostgreSQL, SQL Server, TLS/HTTPS, FFI, and source builds.

## Quick map

| Feature | Native dependency | When you need it |
| --- | --- | --- |
| Handler execution (`sloppy run`) | V8 | Included in the published V8-enabled alpha runtime package. You do not install V8 manually for npm alpha installs. Source builds must enable/provide V8. |
| SQLite provider | `sqlite3` | Bundled with the runtime. No user setup. Used by the default `api` template. |
| PostgreSQL provider | `libpq` | Only when your app uses `data.postgres.*` APIs or a `Postgres<"name">` injection against a real database. |
| SQL Server provider | Microsoft ODBC Driver 17 or 18 | Only when your app uses `data.sqlserver.*` APIs or a `SqlServer<"name">` injection against a real database. |
| TLS / HTTPS | OpenSSL | Needed only for inbound TLS or `HttpClient` HTTPS, depending on your platform package or build configuration. |
| Native FFI (`sloppy/ffi`) | libffi plus your declared native libraries | Only when your app imports `sloppy/ffi` and executes native calls. Build-time metadata inspection can run without loading user native libraries. |
| Plan parsing | `yyjson` | Bundled into the runtime. No user setup. |

`sloppy doctor .sloppy` reports which of these are present in the active
install and which optional features are reachable from your local environment.

## Where each library is used

- `yyjson` parses `app.plan.json` strictly; it is part of the runtime build.
- `sqlite3` backs `data.sqlite` and `Sqlite<"name">` injection and is
  embedded with the runtime, so no install step is needed for the default
  `api` template.
- V8 lives behind a narrow C++ bridge under `src/engine/v8/`. The published
  alpha packages bundle V8-enabled runtimes; source builds opt in through
  the build flags.
- `libpq` and ODBC back `data.postgres` / `data.sqlserver` and the matching
  typed providers. They are only loaded when the running app actually opens
  a PostgreSQL or SQL Server connection.
- libffi backs `sloppy/ffi` typed native calls. User-declared native
  libraries are loaded through the platform dynamic loader or through
  package manifest path overrides for copied local libraries.
- OpenSSL is invoked through the inbound TLS transport and the private
  outbound TLS bridge that `HttpClient` uses for HTTPS.

## Package dependencies

Installed package graph support is experimental. Sloppy can read packages
already installed in `node_modules` at build time when a package is pure
JavaScript and fits Sloppy's supported package resolver, module loader, and
runtime API boundary. The compiler bundles compatible modules into the
generated artifacts and records them in the dependency graph.

The runtime does not bundle a JavaScript package manager, an ORM, a
migration tool, a registry installer, or native Node addon support. See
[Using installed packages](../guide/using-packages.md) and
[Dependency graph](dependency-graph.md).

# Native Dependencies

Sloppy uses a small, named set of native libraries. This page tells you which
ones you need installed for which features, and where they show up.

## Quick map

| Feature | Library | When you need it |
| --- | --- | --- |
| Handler execution (`sloppy run`) | V8 | Any time the runtime evaluates JavaScript handlers. CLI commands that only read artifacts (`build`, `routes`, `capabilities`, `audit`, `openapi`, `doctor`, `package`) do not need V8. |
| Native FFI (`sloppy/ffi`) | libffi plus the declared native libraries | Apps that import `sloppy/ffi` and execute typed C ABI calls. Build-time metadata commands can inspect the Plan without loading user native libraries, but runtime execution and `doctor` load checks need the libraries available. |
| SQLite provider | `sqlite3` | Always available; statically embedded in the runtime. |
| PostgreSQL provider | `libpq` | Compiling and running apps that use `Postgres<"name">` injection or `data.postgres.*` against a real database. |
| SQL Server provider | Microsoft ODBC Driver 17 or 18 | Compiling and running apps that use `SqlServer<"name">` injection or `data.sqlserver.*` against a real database. |
| TLS (inbound and `HttpClient` outbound) | OpenSSL | Any time TLS is configured for the development server, or when `HttpClient` opens an `https://` URL. |
| Plan parsing | `yyjson` | Always — bundled into the native runtime, no user setup. |

`sloppy doctor` prints which of these are present in the active install.

## Where each library is used

- `yyjson` parses `app.plan.json` strictly; it is part of the runtime build.
- `sqlite3` backs `data.sqlite` and `Sqlite<"name">` injection.
- `libpq` and ODBC back `data.postgres` / `data.sqlserver` and the matching
  typed providers.
- V8 lives behind a narrow C++ bridge under `src/engine/v8/` and is only
  pulled in when the build is configured with V8 enabled.
- libffi backs `sloppy/ffi` typed native calls. User-declared native libraries
  are loaded through the platform dynamic loader or through package manifest
  path overrides for copied local libraries.
- OpenSSL is invoked through the inbound transport and the private outbound
  TLS bridge that `HttpClient` uses for HTTPS.

## What Sloppy does not bring in

Sloppy does not load `node_modules`, and the runtime does not bundle a
JavaScript package manager, an ORM, or a migration tool. See
[Why no `node_modules`?](../about/why-no-node-modules.md) for the reasoning
and the alternatives Sloppy ships in the stdlib.

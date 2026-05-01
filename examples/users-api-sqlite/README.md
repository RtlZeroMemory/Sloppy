# SQLite Users API Example

Status: V8-gated framework example registered as
`conformance.users_api_sqlite.localhost_transport`. Source-input compile/run tests also
verify that the example emits artifacts and Plan metadata through the supported dev loop.

This example is a deliberately small users API backed by SQLite:

- `GET /health` returns `ok`.
- `GET /users` returns seeded and inserted users as JSON.
- `GET /users/{id}` returns one user as JSON or `404`.
- `POST /users` accepts a JSON object with `name` and `email`, inserts the user, and returns `201`.
- `POST /users` returns a safe problem response for a structurally invalid user payload.

The example uses the explicit provider import
`import { sqlite } from "sloppy/providers/sqlite"`, `sloppy.json`, `appsettings.json`, and
a relative function module. The compiler rewrites that supported source graph into the
classic artifact runtime path and emits Plan-visible module routes, `data.main`
provider/capability metadata, generated route effects, config-driven SQLite metadata, and
source locations. The database file is configured through
`Sloppy:Providers:sqlite:main:database`. Tests remove `users-api-sqlite-runtime.db` before
and after the localhost transport proof.

With a V8-enabled build, the direct source-input shortcut compiles and then runs the same
artifact path:

```powershell
.\build\windows-relwithdebinfo\sloppy.exe run examples/users-api-sqlite/app.js --once GET /users
```

From this directory, `sloppy run` reads `sloppy.json`:

```powershell
..\..\build\windows-relwithdebinfo\sloppy.exe run --once GET /users
```

Generated positional-source artifacts go under `.sloppy/cache/dev/source-input`. The
explicit `sloppyc build ... --out <dir>` plus `sloppy run --artifacts <dir>` path remains
the debuggable artifact workflow.

After building artifacts, these commands expose the Plan-driven developer surfaces:

```powershell
..\..\build\windows-dev\sloppy.exe routes --plan .sloppy\app.plan.json
..\..\build\windows-dev\sloppy.exe capabilities --plan .sloppy\app.plan.json
..\..\build\windows-dev\sloppy.exe doctor --plan .sloppy\app.plan.json
..\..\build\windows-dev\sloppy.exe audit --plan .sloppy\app.plan.json --format json
..\..\build\windows-dev\sloppy.exe openapi --plan .sloppy\app.plan.json
```

Current expected metadata includes `/health`, `/users`, `/users/{id:int}`, generated
SQLite read/write capabilities, the `usersModule` module attribution, and explicit partial
response metadata for module handlers whose runtime branches are not fully inferred.

This is not an ORM, migration framework, production HTTP edge, benchmark, public alpha
claim, HTTP-25.F keep-alive/chunked/streaming stress claim, PostgreSQL bridge claim, or
SQL Server bridge claim. The localhost transport is keep-alive-capable after
HTTP-25.A/B/C, but this users API fixture remains V8-gated workflow evidence rather than
streaming, chunked, pipelining, or production-edge HTTP evidence. SQLite bridge calls are
still synchronous in the current V8 bridge; provider executor/offload conversion remains a
separate provider-runtime task.

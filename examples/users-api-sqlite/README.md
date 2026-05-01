# SQLite Users API Example

Status: V8-gated ENGINE-17.E runtime proof fixture registered as
`conformance.users_api_sqlite.localhost_transport`. ENGINE-02.E also registers a
V8-gated source-input run-once proof for this source.

This example is a deliberately small users API backed by SQLite:

- `GET /users` returns seeded and inserted users as JSON.
- `GET /users/{id}` returns one user as JSON or `404`.
- `POST /users` accepts a JSON object with `name` and `email`, inserts the user, and returns `201`.

The example uses the explicit provider import
`import { sqlite } from "sloppy/providers/sqlite"` and a relative function module. The
compiler rewrites that supported source graph into the classic artifact runtime path and
emits Plan-visible `data.main` provider/capability metadata. The database file is
configured through `appsettings.json` at `Sloppy:Providers:sqlite:main:database`. Tests
remove `users-api-sqlite-runtime.db` before and after the localhost transport proof. Each
handler creates the table if needed and seeds Ada Lovelace and Grace Hopper only when
their deterministic IDs are absent.

With a V8-enabled build, the direct source-input shortcut compiles and then runs the same
artifact path:

```powershell
.\build\windows-relwithdebinfo\sloppy.exe run examples/users-api-sqlite/app.js --once GET /users
```

Generated positional-source artifacts go under `.sloppy/cache/dev/source-input`. The
explicit `sloppyc build ... --out <dir>` plus `sloppy run --artifacts <dir>` path remains
the debuggable artifact workflow.

This is not an ORM, migration framework, production HTTP edge, benchmark, public alpha
claim, HTTP-25.F keep-alive/chunked/streaming stress claim, PostgreSQL bridge claim, or
SQL Server bridge claim.
The localhost transport is keep-alive-capable after HTTP-25.A/B/C, but this users API
fixture remains V8-gated workflow evidence rather than streaming, chunked, pipelining, or
production-edge HTTP evidence. SQLite bridge calls are still synchronous in the current V8
bridge; provider executor/offload conversion remains a separate provider-runtime task.

# SQLite Users API Example

Status: V8-gated ENGINE-17.E runtime proof fixture.

This example is a deliberately small users API backed by SQLite:

- `GET /users` returns seeded and inserted users as JSON.
- `GET /users/{id}` returns one user as JSON or `404`.
- `POST /users` accepts a JSON object with `name` and `email`, inserts the user, and returns `201`.

The example uses the public `data.sqlite("main")` JavaScript API and Plan-emitted
`data.main` database capability metadata. The database file is
`users-api-sqlite-runtime.db`; tests remove it before and after the localhost transport
proof. Each handler creates the table if needed and seeds Ada Lovelace and Grace Hopper
only when their deterministic IDs are absent.

This is not an ORM, migration framework, production HTTP edge, benchmark, public alpha
claim, PostgreSQL bridge claim, or SQL Server bridge claim. SQLite bridge calls are still
synchronous in the current V8 bridge; provider executor/offload conversion remains a
separate provider-runtime task.

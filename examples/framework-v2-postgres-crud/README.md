# Framework v2 PostgreSQL CRUD Example

Status: opt-in live-lane Framework v2 PostgreSQL example.

This example documents the Plan-visible Framework v2 shape for PostgreSQL CRUD-style
handlers: typed `Body<T>` and `Route<T>` bindings, provider injection through
`Postgres<"main">`, semantic request types, and deadline propagation into provider calls.

It is not part of default CI. Run it only with a local PostgreSQL test database and a
redacted connection string managed outside source control. Missing PostgreSQL/libpq/live
configuration must be reported as unavailable diagnostics, not pass evidence.

```powershell
$env:SLOPPY_POSTGRES_TEST_URL="postgres://postgres:postgres@localhost:5432/sloppy_test"
.\tools\windows\test-live-postgres.ps1
```

This is not an ORM, migration system, production database policy, public alpha claim,
benchmark, package-manager behavior, or Node/Bun/Deno compatibility proof.

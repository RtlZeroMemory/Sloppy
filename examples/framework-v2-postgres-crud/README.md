# Framework v2 PostgreSQL CRUD Example

Status: opt-in live-lane Framework v2 PostgreSQL example.

This example documents the Plan-visible Framework v2 shape for PostgreSQL CRUD-style
handlers: typed `Body<T>` and `Route<T>` bindings, compiler-inferred `postgres/main`
provider metadata from `Postgres<"main">`, and semantic request types. Provider operation
deadline propagation remains separate provider/runtime work and is not claimed by this
example.

It is not part of default CI. Run it only with a local PostgreSQL test database and a
redacted connection string managed outside source control. Missing PostgreSQL/libpq/live
configuration must be reported as unavailable diagnostics, not pass evidence.

```powershell
$env:Sloppy__Providers__postgres__main__connectionString="postgres://<USER>:<PASSWORD>@<HOST>:<PORT>/<DB>"
.\tools\windows\test-live-postgres.ps1
```

Provide the real value through the environment on the machine running the live lane. Do
not commit live credentials or local DSNs to source control.

This is not an ORM, migration system, production database policy, public alpha claim,
benchmark, package-manager behavior, or Node/Bun/Deno compatibility proof.

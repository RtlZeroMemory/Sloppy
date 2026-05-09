# Framework v2 SQL Server CRUD Example

This is an opt-in live-lane Framework v2 SQL Server example with honest unavailable
diagnostics when the local ODBC lane is not configured.

This example documents the Plan-visible Framework v2 shape for SQL Server CRUD-style
handlers: typed `Body<T>` and `Route<T>` bindings, compiler-inferred `sqlserver/main`
provider metadata from `SqlServer<"main">`, semantic request types, and SQL operation
options that pass `ctx.signal`/`ctx.deadline` for Slop-side pre-dispatch cancellation and
deadline checks. Provider-specific ODBC statement cancellation for already-running queries
remains separate provider/runtime work and is not claimed by this example.

It is not part of default CI. Run it only with a local SQL Server test database,
Microsoft ODBC Driver 18 for SQL Server, and a redacted connection string managed outside
source control. Missing driver/live configuration must be reported as unavailable diagnostics, not pass evidence.

```powershell
$env:Sloppy__Providers__sqlserver__main__connectionString="<redacted SQL Server ODBC connection string>"
.\tools\windows\test-live-sqlserver.ps1
```

This is not an ORM, migration system, production database policy, public release claim,
benchmark, package-manager behavior, or Node/Bun/Deno compatibility proof.

# Provider Runtime Model

Sloppy separates provider concerns into four layers because each layer has a
different owner and runtime path:

1. Plan metadata (`dataProviders` and `capabilities`) in
   `src/core/plan_parse.c` and `src/core/app_host.c`.
2. Native provider implementations in `src/data/*.c`.
3. V8 bridge modules in `src/engine/v8/intrinsics_*.cc`.
4. Live external service availability (environment- and driver-dependent).

`src/data` implements real provider work (open/query/exec/transactions/pools)
with redaction-aware diagnostics and explicit resource lifetime rules.

The runtime still validates provider and capability metadata at startup. Database
capabilities and provider tokens must cross-reference correctly, and malformed
metadata fails closed.

SQL Server is deliberately explicit about build/runtime gating: when ODBC support
is not enabled, provider APIs return `unsupported` diagnostics rather than fake
success.

This model explains why provider docs name the exact surface: a provider may be
visible to the compiler before every runtime path is available on every
machine.

## Surface Separation

```mermaid
flowchart TB
    Descriptor[app.use(sqlite(...))] --> Plan[Plan metadata]
    Static[app.provider("sqlite:main")] --> Generated[Generated static provider bridge]
    Typed[Sqlite/Postgres/SqlServer typed parameters] --> Wrapper[Generated handler wrapper]
    Runtime[data.sqlite/postgres/sqlserver] --> Bridge[V8/native provider bridge]
    Bridge --> Native[src/data providers]
    Native --> Live[Optional live service or driver]
```

The descriptor path is not the typed-injection path, and neither is the same as
the runtime data API. SQLite has the most complete local path because it can run
embedded. PostgreSQL and SQL Server require provider configuration and live or
driver availability for service execution.

## Runtime Surfaces

| Surface | Where It Is Exercised |
| --- | --- |
| SQLite descriptor registration | stdlib/bootstrap descriptor tests or compiler fixture using `app.use(sqlite(...))` |
| Static SQLite provider handle | compiler fixture or example using `app.provider("sqlite:main")` |
| Typed provider metadata | compiler Framework v2 metadata fixtures |
| Runtime data API options | stdlib/provider tests and native provider tests |
| PostgreSQL service execution | PostgreSQL integration checks with service configuration |
| SQL Server service execution | SQL Server integration checks with ODBC driver and service configuration |
| V8 bridge provider execution | V8-enabled provider bridge checks |

SQL Server currently has runtime API and typed-injection surfaces. Live SQL
Server execution also needs SQL Server setup, an ODBC driver, connection
configuration, and async-driver support for the true-async bridge path.

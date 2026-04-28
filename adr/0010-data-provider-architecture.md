# 0010: Data Provider Architecture

## Status

Accepted.

## Context

Sloppy needs SQLite, PostgreSQL, and SQL Server support. Database providers must be modular
and extensible, but the runtime should not pretend all SQL dialects or driver deployment
models are identical.

Windows-first runtime support affects the SQL Server choice.

## Decision

Sloppy will provide a common data API for routine query, transaction, lifecycle, and health
check operations. Database providers are modules.

SQLite is first and may be built in or statically linked. PostgreSQL is planned through
libpq. SQL Server is planned through Microsoft ODBC Driver and the ODBC API on Windows.

Provider-specific advanced APIs are allowed under provider namespaces. Query template
literals parameterize by default. Provider registrations appear in the Sloppy Plan.

Dynamic provider ABI is future work. Static first-party providers come first.

## Consequences

This is more architecture than hardcoded SQLite, but it gives Sloppy a path to multiple
databases without making false portability promises.

Distribution and `sloppy doctor` tooling must handle native driver availability, packaged
DLLs, missing ODBC drivers, and configuration validation.

## Alternatives Considered

- Hardcode SQLite only: rejected because PostgreSQL and SQL Server are explicit goals.
- Use ODBC for all databases from the start: rejected because it would flatten
  provider-specific capabilities and complicate SQLite/PostgreSQL unnecessarily.
- Use JavaScript npm database packages: rejected because Sloppy does not target Node
  compatibility and native resource tracking belongs to the host.
- Build custom wire protocol clients immediately: rejected because libpq and ODBC reduce
  protocol risk for early provider support.

## Follow-up Tasks

- Add common data API fixtures before provider implementation.
- Add SQLite first with resource lifecycle tests.
- Add PostgreSQL packaging/driver diagnostics before release support.
- Add SQL Server ODBC driver detection diagnostics before SQL Server happy path.

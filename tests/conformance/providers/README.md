# Provider Conformance

This directory records the provider matrix and evidence lane contract.
Provider conformance is one shared Db contract with provider-specific deltas called out
explicitly. A default pass is not live-provider evidence, a V8 pass is not native live
evidence, and skipped or unavailable lanes must stay visible as skipped or unavailable.
Benchmark output is never correctness evidence.

## Common Contract

All providers expose `query`, `queryOne`, `exec`, and callback `transaction` behavior.
The common tests cover statement text plus bound parameters, parameter ordering,
`queryOne` found/not-found behavior, execute result shape, result ownership, invalid
state cleanup, unsupported value diagnostics, and redacted provider diagnostics.

SQLite is the default embedded conformance provider. PostgreSQL and SQL Server reuse the
same shape in their Docker-backed live lanes because they require external services and
driver dependencies.

## Registered Lanes

| Lane | Command | Evidence |
| --- | --- | --- |
| Common native contract | `ctest -R conformance.data.common_contract --output-on-failure` | DbValue, statement, row-set, execute-result, and redaction contract. |
| SQLite default native | `ctest -R conformance.sqlite.native_provider --output-on-failure` | Embedded `:memory:` and temp-file provider behavior without Docker. |
| SQLite V8 bridge | `ctest -R conformance.sqlite.bridge --output-on-failure` | JS bridge, Promise settlement, capability admission, transaction callback, and `Uint8Array` blob parameters when V8 is enabled. |
| PostgreSQL default native | `ctest -R conformance.postgres.native_provider --output-on-failure` | Native diagnostics, redaction, pooling lifecycle, and non-live failure paths. |
| PostgreSQL live native/V8 | `tools/windows/test-live-postgres.ps1` or `tools/unix/test-live-postgres.sh` | Docker-backed PostgreSQL provider and V8 bridge behavior when the live URL and V8 lane are configured. |
| SQL Server default native | `ctest -R conformance.sqlserver.native_provider --output-on-failure` | Native diagnostics, redaction, ODBC driver detection, pooling lifecycle, and unavailable-driver behavior. |
| SQL Server live native/V8 | `tools/windows/test-live-sqlserver.ps1` or `tools/unix/test-live-sqlserver.sh` | Docker-backed SQL Server provider and V8 bridge behavior when Docker, ODBC, and async driver support are available. |
| Provider stress/torture smoke | `ctest -R stress.provider_executor --output-on-failure` | Bounded provider executor pressure for queue overflow, shutdown, cancellation, pool workers, late completion, and cleanup-once behavior. |

`tools/windows/test-live-providers.ps1` and `tools/unix/test-live-providers.sh` run
`postgres`, `sqlserver`, or `all` provider live lanes intentionally. Missing Docker,
missing ODBC drivers, missing connection-string environment, or driver async
unavailability must be reported as `UNAVAILABLE` or CTest `SKIPPED`; they are not pass
evidence.

## Provider-Specific Deltas

- SQLite uses `SERIALIZED_BLOCKING`, `?` placeholders, no connection pool, and explicit
  text/blob encodings for JSON/date/time/timestamp/instant-like values.
- PostgreSQL uses `TRUE_ASYNC`, `$1` placeholders, nonblocking libpq, bounded pooling,
  scalar arrays where supported, and Docker-backed live evidence for external behavior.
- SQL Server uses `TRUE_ASYNC` only when ODBC async connection/statement mode completes,
  `?` placeholders, bounded pooling, and an honest unavailable result for drivers that do
  not advance async ODBC operations.

No provider conformance lane proves ORM behavior, migrations, production readiness,
Node/Bun/Deno compatibility, package readiness, benchmark claims, or public release
readiness.

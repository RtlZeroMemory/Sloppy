# Data Module

## Status

Bootstrap data/capabilities foundation implemented. Real providers are still planned / not
implemented yet.

## Purpose

Provide common data APIs and provider integrations for SQLite, PostgreSQL, and SQL Server.

## Scope

Common data API, query template lowering, transactions, provider resources, capabilities,
and diagnostics.

## Non-goals

No database dependencies before the relevant provider phase.

## Public/Internal API

Implemented bootstrap API:

- `sql` tagged template lowering helper.
- `data.lowerQueryTemplate(strings, values, options)` for direct lowering tests.
- `data.createFakeProvider(definition)` for tests/examples.
- fake provider methods: `query`, `queryOne`, `exec`, and `transaction`.
- `builder.capabilities.addDatabase(token, options)` and module `.capabilities(...)`
  metadata declarations.

Future provider modules and native provider interfaces remain behind resource IDs.

## Ownership/Lifetime Rules

Current fake providers own only JavaScript test/example callbacks and debug event arrays.
Fake transactions close their transaction object after commit/rollback and reject use after
close. Future real connections, statements, pools, and transactions are resource-table-owned
and scoped explicitly.

## Invariants

Template query APIs parameterize by default. Lowered query descriptors preserve text and
parameters separately. Provider-specific APIs stay namespaced.

## Diagnostics

Implemented JavaScript errors cover invalid query template usage, fake provider missing
methods, duplicate/missing capability tokens, invalid database capability metadata,
transaction callback misuse, nested transactions, and use after closed transaction scope.
Missing config, missing driver, parameter binding, and redacted provider diagnostics remain
future provider work.

## Tests

`bootstrap.stdlib.data_foundation` executes the ESM stdlib with Node when available and
covers capability metadata, query lowering, fake provider method dispatch, transaction
commit/rollback, rejected async callbacks, nested transaction rejection, use after close,
and module/service integration.

Future provider tests add resource cleanup, driver-unavailable diagnostics, and env-gated
integrations.

## Source Docs

- `docs/data-providers.md`;
- `docs/concurrency.md`;
- `docs/modularity.md`;
- `docs/testing-strategy.md`;
- ADR 0010.

## Open Questions

- Exact row/result JS shape.

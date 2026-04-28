# Data Module

## Status

Planned / not implemented yet.

## Purpose

Provide common data APIs and provider integrations for SQLite, PostgreSQL, and SQL Server.

## Scope

Common data API, query template lowering, transactions, provider resources, capabilities,
and diagnostics.

## Non-goals

No database dependencies before the relevant provider phase.

## Public/Internal API

Future provider modules and native provider interfaces behind resource IDs.

## Ownership/Lifetime Rules

Connections, statements, pools, and transactions are resource-table-owned and scoped
explicitly.

## Invariants

Template query APIs parameterize by default. Provider-specific APIs stay namespaced.

## Diagnostics

Missing config, missing driver, parameter binding, transaction misuse, and redacted
provider diagnostics.

## Tests

Placeholder lowering, transaction commit/rollback, resource cleanup, driver-unavailable
diagnostics, and env-gated integrations.

## Source Docs

- `docs/data-providers.md`;
- `docs/concurrency.md`;
- `docs/modularity.md`;
- `docs/testing-strategy.md`;
- ADR 0010.

## Open Questions

- Exact row/result JS shape.

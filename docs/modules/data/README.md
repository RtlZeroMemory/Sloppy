# Data Module

## Purpose

The data module connects provider declarations, capabilities, native provider boundaries,
and JavaScript bridge behavior.

## Current Status

The repository has provider metadata, runtime capability checks, native provider
foundations, provider executor contracts, a provider-neutral Db value/statement/result
contract, SQLite `SERIALIZED_BLOCKING` executor configuration, native SQLite behavior, and
a V8-gated SQLite bridge whose exec/query/queryOne/transaction operations settle
JavaScript Promises from provider-executor completions on the V8 owner thread.
PostgreSQL and SQL Server foundations are not broad JavaScript provider bridges and
require their own evidence lanes.

## Invariants

- Provider work must validate capability metadata before native execution.
- Native provider resources must not be exposed as raw JS pointers.
- Text/blob data crossing provider boundaries must have explicit ownership.
- SQL text and parameters must remain separate until the provider-specific binding layer,
  and diagnostics must redact parameter values.
- SQLite JSON/date/time/timestamp/instant behavior is explicit text/blob encoding policy,
  not a fake native SQLite type system.
- Live-provider evidence is separate from default non-V8 evidence.
- Provider diagnostics must redact secrets and connection strings.

## Deferred Work

Deferred work includes broader PostgreSQL and SQL Server provider bridges, live-provider CI
lanes, richer audit behavior, migrations/schema tooling, and production hardening.

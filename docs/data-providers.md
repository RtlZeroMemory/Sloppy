# Data Providers

## Purpose

Data providers connect Sloppy's Plan-visible capability model to native database/provider
boundaries. Provider behavior must be explicit, capability-checked, and honest about which
lanes are executable.

## Current Status

Implemented foundations include:

- provider metadata in compiler and Plan artifacts;
- capability metadata and runtime capability checks;
- native provider boundaries for SQLite, PostgreSQL, and SQL Server foundations;
- provider executor infrastructure and completion ownership rules;
- synchronous native SQLite query/exec behavior;
- a narrow V8-gated SQLite bridge;
- doctor/audit metadata for providers and capabilities;
- tests and examples that distinguish metadata, native provider behavior, V8 bridge
  behavior, and live-provider evidence.

The current SQLite bridge is synchronous in the V8 path. Provider executor/offload adoption
for that bridge remains separate work. PostgreSQL and SQL Server JavaScript bridge behavior
and live-provider lanes must not be implied unless those lanes run.

## Capability Rules

Provider work must validate:

- provider kind;
- token/name;
- requested access mode;
- configured capability metadata;
- active runtime feature set;
- operation ownership and cleanup.

Failure must produce stable diagnostics and must not leak secrets or connection strings.

## JavaScript Boundary

JavaScript receives Sloppy-owned descriptors and bridge functions, not raw native pointers.
Resource handles use generation-counted IDs or bridge-owned objects. Result text/blob data
must be copied to the documented owner before it outlives the native provider call.

## Provider Executor

The provider executor owns operation admission, bounded queueing, completion dispatch,
cleanup-once behavior, and explicit scope retention. Failed admission does not transfer
ownership. Late completions after cancellation, timeout, shutdown, or discard are
cleanup-only work.

## Evidence Lanes

- Default non-V8 tests can prove native provider metadata and native provider contracts.
- V8-gated tests can prove bridge behavior.
- Live-provider tests can prove external service integration.
- Benchmark tests can prove only the measured workload they report.

These lanes are separate. A default pass is not live-provider or V8 evidence.

## Deferred Work

Deferred provider work includes executor-backed SQLite bridge adoption, broader
JavaScript-to-native provider bridges, live-provider CI lanes, richer provider audit
policy, connection pooling policy, migrations/schema tooling, and production hardening.

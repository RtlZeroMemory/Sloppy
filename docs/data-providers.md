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
- canonical provider execution modes: `SERIALIZED_BLOCKING`, `BLOCKING_POOL`,
  `TRUE_ASYNC`, and fail-closed `UNAVAILABLE`;
- provider-neutral C Db contract types for `DbValue`, SQL statements, parameters,
  columns, row sets, execute results, transaction options, and redacted statement
  diagnostics;
- SQLite provider configuration that maps to `SERIALIZED_BLOCKING` executor policy with
  one active operation per provider instance;
- native SQLite open/close, file and in-memory database, query/exec,
  transaction, binding, result-copy, and diagnostic behavior;
- a V8-gated SQLite bridge that routes SQLite exec/query/queryOne/transaction work through
  the serialized provider executor and settles Promises on the V8 owner thread;
- a V8-gated PostgreSQL bridge that uses libpq's nonblocking state machine, Slop-owned
  socket-readiness watches, bounded connection pooling, parameterized exec/query/queryOne,
  callback transactions, and owner-thread Promise settlement;
- doctor/audit metadata for providers and capabilities;
- tests and examples that distinguish metadata, native provider behavior, V8 bridge
  behavior, and live-provider evidence.

The SQLite bridge is async at the JavaScript boundary through the `SERIALIZED_BLOCKING`
executor. SQLite work still runs on one serialized blocking worker per provider instance;
it is not labeled `TRUE_ASYNC`. PostgreSQL JavaScript provider work is labeled
`TRUE_ASYNC` only for the V8 bridge path that is driven by nonblocking libpq and
`SlAsyncIoWatch`, not for blocking-pool fallback. SQL Server JavaScript bridge behavior and
live-provider lanes must not be implied unless those lanes run.

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

`SERIALIZED_BLOCKING` and `BLOCKING_POOL` are the only worker-backed modes. `TRUE_ASYNC`
is reserved for provider-owned nonblocking state machines that do not occupy blocking
workers while waiting on database I/O. `UNAVAILABLE` keeps a configured provider instance
fail-closed: capability checks still run before admission, but no work is enqueued.

## Common Db Contract

The shared native Db contract lives in `include/sloppy/data.h` and `src/data/common.c`.
It defines provider-neutral value kinds for null, boolean, integers, float64, decimal,
text, bytes, uuid, date/time/timestamp/instant, json, and arrays. The contract keeps SQL
text and parameters separate, carries provider placeholder style metadata, and provides a
redacted statement formatter that never prints parameter values.

Provider-specific implementations still own driver conversion rules, lifecycle, and live
I/O. The common contract is not an ORM, migration layer, SQL parser, or package-manager
surface.

SQLite stores only SQLite-native null, integer, real, text, and blob values. Sloppy maps
JSON, date, time, timestamp, and instant values through explicit text/blob encodings for
SQLite instead of claiming native SQLite value types that do not exist.

## Evidence Lanes

- Default non-V8 tests can prove native provider metadata and native provider contracts.
- V8-gated tests can prove bridge behavior.
- Live-provider tests can prove external service integration.
- Benchmark tests can prove only the measured workload they report.

These lanes are separate. A default pass is not live-provider or V8 evidence.

## Deferred Work

Deferred provider work includes SQL Server JavaScript bridge completion until the ODBC
driver async lane proves real async behavior, broader live-provider CI scheduling, richer
provider audit policy, migrations/schema tooling, and production hardening.

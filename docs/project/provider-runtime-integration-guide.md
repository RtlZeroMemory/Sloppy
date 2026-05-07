# Provider Runtime Integration Guide

Status: DATA-RUNTIME-01 evidence and integration guide for SQLite, PostgreSQL, and SQL
Server runtime work.

This guide explains how database providers must consume the ENGINE-23 provider executor.
It is not a database bridge implementation. It does not convert SQLite to async offload,
add PostgreSQL or SQL Server JavaScript bridges, create an ORM, add migrations, change V8,
or make benchmark claims.

## What ENGINE-23 Proves

ENGINE-23 now provides a Slop-owned native provider/offload executor with:

- operation descriptors that copy or retain data needed after submission;
- per-provider-instance bounded admission and deterministic overload rejection;
- `SERIALIZED_BLOCKING` execution with one active operation per provider instance;
- `BLOCKING_POOL` execution capped by configured worker count and queue capacity;
- `TRUE_ASYNC` execution-mode metadata for provider-owned nonblocking state machines;
- fail-closed `UNAVAILABLE` mode that checks capabilities and rejects before enqueue;
- capability-gated dispatch before enqueue and before provider work starts;
- cancellation, timeout, shutdown, and late-completion terminal states;
- cleanup-once behavior across success, failure, rejection, shutdown, and late completion;
- stable provider executor diagnostics/counters covered by bounded native stress smoke.

The evidence is native executor evidence. It is not live SQLite/PostgreSQL/SQL Server
throughput evidence, not V8 provider bridge evidence, and not public performance proof.

## Generic Provider Rules

Every provider bridge that can block or outlive the caller stack must enter native work
through the provider executor.

Rules:

- no provider worker may enter V8 or touch JS handles;
- no provider may bypass capability checks;
- no provider may bypass bounded admission;
- no provider operation may use borrowed request memory after submission;
- no provider may create thread-per-request behavior;
- no provider may use libuv's global threadpool as a policy substitute;
- completion must post through the Slop async runtime;
- operation descriptors own SQL text, parameters, blobs, diagnostic context, and cleanup
  payloads needed after submission;
- late completion after cancellation, timeout, or shutdown is cleanup-only and must not
  double-settle;
- provider-specific cancellation/interruption is explicit provider work, not implied by
  Slop terminal state.

## SQLite Integration

Default mode for a single SQLite connection is `SERIALIZED_BLOCKING`.

Future SQLite runtime completion should route `query`, `execute`, and `queryOne`
through the executor:

1. Resolve the JS-facing opaque resource ID to the native SQLite connection resource on the
   owner thread.
2. Check the requested database capability before enqueue.
3. Build an operation descriptor whose payload owns SQL text, parameter strings, parameter
   blobs, operation kind, capability token, and cleanup state.
4. Submit to the provider executor for the configured provider instance, such as
   `sqlite:main`.
5. Run `sqlite3_step` and result materialization on the serialized provider worker.
6. Post completion through `SlAsyncLoop`.
7. Convert results and settle JS only on the V8 owner thread.
8. Run operation cleanup exactly once.

Cancellation and timeout terminal state means Slop no longer uses the result. It does not
automatically interrupt `sqlite3_step`. A future SQLite-specific interruption hook may use
`sqlite3_interrupt` only when the provider can prove it is safe for the active connection
and operation. Until then, active SQLite cancellation/timeout is terminal from Slop's
perspective and late worker completion is cleanup-only.

Future read/write pooling or WAL-mode parallelism is separate SQLite provider policy. It
must be scoped, documented, and tested independently from the single-connection serialized
default.

## PostgreSQL Integration

PostgreSQL final provider work must use `TRUE_ASYNC` through nonblocking libpq-style state
machines. A blocking-pool fallback can be used only as explicit temporary evidence or
local scaffolding and must never be reported as true async provider completion.

The true-async strategy must still use Slop admission, cancellation, timeout, completion,
and cleanup semantics:

- capability checks happen before enqueue;
- connection strings and credentials stay redacted;
- operation descriptors own query text, parameter data, and result-conversion state;
- nonblocking mode owns provider async state without occupying a blocking worker while
  waiting;
- provider cancellation, such as libpq cancellation, must be explicit and tested before it
  is documented as implemented.

PostgreSQL must not bypass the capability/provider executor model through a direct bridge
call into native provider code.

## SQL Server Integration

SQL Server final provider work must use true async driver behavior when available and must
report `UNAVAILABLE` or `UNSUPPORTED` honestly when the configured ODBC/driver lane cannot
provide async execution. Blocking-pool fallback must not be labeled true async.

Rules:

- operation descriptors own SQL text, ODBC parameter buffers, blobs, diagnostic context,
  and cleanup state;
- worker code never blocks the V8 owner thread;
- ODBC handles remain native resources and are never exposed to JavaScript;
- connection strings, passwords, `PWD`, and token fields stay redacted;
- provider-specific cancellation, such as ODBC statement cancellation, must be explicit,
  documented, and tested before docs claim mid-call interruption.

SQL Server bridge work remains deferred. The current native provider boundary and non-live
tests do not prove JS-to-native SQL Server execution.

## Diagnostics And Counters

Provider executor diagnostics and counters distinguish:

- queue full / overload;
- invalid operation or invalid executor mode/backend;
- executor shutdown;
- worker failure;
- operation failure;
- cancellation;
- timeout;
- late completion;
- capability denial;
- cleanup-once behavior under stress.

Diagnostics must be stable enough for tests, avoid raw native pointers, avoid V8/libuv
implementation details, and avoid SQL parameters, connection strings, passwords, tokens,
or other secret-bearing values. Safe context may include provider instance id, provider
kind, operation name, capability token, and redacted diagnostic context.

## Evidence Boundaries

The bounded stress smoke tests exercise local executor behavior with provider-like native
callbacks. They are correctness smoke, not benchmarks.

Do not report:

- throughput;
- latency;
- live database scalability;
- SQLite async/offload completion;
- PostgreSQL or SQL Server bridge readiness;
- public alpha provider readiness.

The remaining provider bridge work belongs to ENGINE-17 for SQLite runtime completion and
future PostgreSQL/SQL Server bridge tasks.

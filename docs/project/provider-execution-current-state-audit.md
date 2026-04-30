# Provider Execution Current State Audit

Status: ENGINE-23 planning audit, updated through ENGINE-23.C.

This document records the provider/offload state after ENGINE-12 and the ENGINE-23.A/B/C
foundation. It separates generic async runtime infrastructure from provider execution
policy, from the implemented serialized worker model, and from the remaining provider
runtime gaps. It is not a public alpha document and does not claim SQLite runtime
completion is implemented.

## Summary

ENGINE-12 now provides the shared async substrate, and ENGINE-23.A/B/C provides the first
provider/offload runtime slice:

- a bounded `SlAsyncLoop` completion backend with deterministic test and libuv-backed
  implementations;
- owner-thread V8 native continuation posting for V8-enabled builds;
- cancellation reason primitives for cancelled, deadline, backpressure, and shutdown;
- async completion ownership, cleanup-once, and scope retain/release hooks;
- provider execution mode names and a bounded per-provider-instance executor model;
- a `SERIALIZED_BLOCKING` worker lifecycle with one long-lived worker and one active
  operation per provider instance;
- native tests proving copied operation inputs, serialized activation, queue overflow,
  serialized worker FIFO order, worker failure, cancellation, timeout, shutdown, late
  completion, and cleanup-once behavior.

That is still not full provider runtime completion. SQLite is not routed through the
provider executor, blocking provider calls can still occur in the V8 SQLite bridge,
`BLOCKING_POOL` is not implemented, provider-specific cancellation/interruption remains
deferred, and capability checks are bridge hooks rather than executor admission hooks.

ENGINE-23 must build the provider execution subsystem before deeper HTTP/backend/provider
work depends on scalable provider behavior.

## ENGINE-12 Provides

Generic async runtime infrastructure:

- `include/sloppy/async_backend.h` defines `SlAsyncLoop`, `SlAsyncCompletion`, completion
  operation kinds, fixed queue capacity, cleanup callbacks, and optional scope
  retain/release hooks.
- `src/core/async_backend.c` implements the common bounded queue and deterministic test
  backend.
- `src/platform/libuv/async_backend_libuv.c` implements internal libuv wakeups and
  cross-thread posting while keeping libuv types out of public headers.
- `src/engine/v8/async_scheduler.cc` and V8 async scheduler tests prove native
  continuations settle on the V8 owner thread when the V8 gate is enabled.
- `include/sloppy/cancellation.h` and `src/core/cancellation.c` define cancellation
  reasons and status-code mapping for cancelled, deadline exceeded, backpressure, and
  shutdown.

Provider policy only:

- `include/sloppy/provider_executor.h` defines `SlProviderExecutionMode`,
  `SlProviderOperationDescriptor`, `SlProviderOperation`, and
  `SlProviderInstanceExecutor`.
- `src/core/provider_executor.c` models admission, serialized activation, serialized
  worker dispatch, completion posting, shutdown rejection, cancellation, timeout, late
  completion, and cleanup.
- `tests/unit/core/test_provider_executor.c` verifies the policy using deterministic
  native operations, not real database work.

Missing provider executor infrastructure after ENGINE-23.C:

- no bounded blocking pool executor;
- no nonblocking provider adapter contract beyond the mode name;
- no provider instance registry integration;
- no capability check in the executor admission path;
- no request/app scope retention on provider operations beyond the generic async scope
  hook;
- no provider operation result payload contract for SQLite/PostgreSQL/SQL Server;
- no provider runtime diagnostics surface beyond simple counters and terminal diagnostics;
- no stress evidence for real worker wakeup, shutdown, or late completion cleanup.

## Findings

| Finding | File path | Current behavior | Risk | Required ENGINE-23 task |
| --- | --- | --- | --- | --- |
| Async completion queue exists and is bounded. | `include/sloppy/async_backend.h`, `src/core/async_backend.c` | `SlAsyncLoop` owns completions only after successful post, rejects overflow with `SL_STATUS_CAPACITY_EXCEEDED`, and runs cleanup/release on dispatch or discard. | Provider runtime can reuse the right completion contract, but provider tasks may overclaim if they treat this as provider execution. | ENGINE-23.B and ENGINE-23.C/D must post provider completions through this loop without bypassing cleanup or owner-thread dispatch. |
| Libuv is internal backend plumbing. | `src/platform/libuv/async_backend_libuv.c` | Libuv provides wakeups, mutex-protected queue access, and owner-thread drain checks. Public headers expose no libuv types. | Future provider work could accidentally use libuv's global threadpool as an implicit provider runtime. | ENGINE-23.C/D must define Slop-owned executors and forbid libuv global threadpool as provider policy. |
| V8 owner-thread continuation scheduler exists. | `src/engine/v8/async_scheduler.cc`, `tests/unit/engine/test_v8_async_scheduler.cc` | Native continuations are posted to `SlAsyncLoop` and settle Promises only when drained on the owner thread. | Provider worker threads must not settle Promises or enter V8 directly. | ENGINE-23.C/D/E must post native completions back to the owner loop and rely on the scheduler for JS settlement. |
| Cancellation token is small and terminal-state oriented. | `include/sloppy/cancellation.h`, `src/core/cancellation.c` | Tokens carry cancelled, deadline, backpressure, and shutdown reasons with status-code mapping. They do not start timers or interrupt provider calls. | Blocking native calls may continue after cancellation unless provider-specific interruption exists. | ENGINE-23.E must distinguish Slop terminal state from optional provider interruption and define late completion cleanup-only behavior. |
| Provider execution modes are named but not all implemented as runtime modes. | `include/sloppy/provider_executor.h`, `src/core/provider_executor.c` | `INLINE_FAST`, `SERIALIZED_BLOCKING`, `BLOCKING_POOL`, `NONBLOCKING_IO`, and `EXTERNAL_MANAGED` parse and report as supported. `SERIALIZED_BLOCKING` has a real worker lifecycle; blocking-pool and nonblocking adapters remain unimplemented. | Mode names may be mistaken for complete runtime support. | ENGINE-23.D/H must keep unimplemented modes clearly bounded and document integration guidance. |
| Per-provider queue isolation is deterministic but test-only. | `src/core/provider_executor.c`, `tests/unit/core/test_provider_executor.c` | Executors have per-instance slots, capacity, count, in-flight count, and counters. Tests prove one instance can overflow while another accepts work. | Good policy foundation, but no provider registry or app-scope binding exists. | ENGINE-23.B must bind executors to provider instances, app scope, queue capacity, worker count, and shutdown state. |
| Provider operation input ownership is partially modeled. | `include/sloppy/provider_executor.h`, `src/core/provider_executor.c`, `tests/unit/core/test_provider_executor.c` | Descriptors copy instance id, kind, operation name, capability token, and input bytes into an arena before queued work outlives the caller. | Real providers need typed owned SQL, params, blobs, config-derived values, result payloads, diagnostic context, and cleanup callbacks. | ENGINE-23.A must define the full operation descriptor and owned-payload contract. |
| Cleanup-once and late completion are tested in the skeleton. | `src/core/provider_executor.c`, `tests/unit/core/test_provider_executor.c` | Cancellation, timeout, shutdown, and late completion paths run cleanup once and reject double terminal settlement. | Production workers can race cancellation/shutdown unless the runtime preserves the same terminal-state discipline. | ENGINE-23.A/E/G must preserve cleanup-once under real worker dispatch, late completion, and stress tests. |
| Serialized provider worker lifecycle exists, but only for provider-like operations. | `include/sloppy/provider_executor.h`, `src/core/provider_executor.c`, `src/platform/libuv/thread.c` | `SERIALIZED_BLOCKING` starts one long-lived worker per provider instance, executes one active run callback at a time, and posts completions through the libuv async backend. | SQLite-class runtime calls are not converted yet, and provider-specific cancellation is still generic terminal-state only. | ENGINE-17 routes SQLite through this executor; ENGINE-23.E adds richer cancellation/late-completion semantics. |
| Existing `SlWorkerPool` is inline-only and not the provider runtime. | `include/sloppy/worker_pool.h`, `src/core/worker_pool.c`, `tests/unit/core/test_worker_pool.c` | Work callbacks run inline on the caller thread and post to `SlLoop`, not `SlAsyncLoop`. | Reusing it as-is would keep blocking work on the caller/V8 owner thread. | ENGINE-23.C/D must introduce or adapt real OS-thread-backed provider executors instead of relying on the inline skeleton. |
| SQLite native provider is synchronous and single-thread-owned. | `include/sloppy/data_sqlite.h`, `src/data/sqlite.c` | `SlSqliteConnection` wraps a native handle and the module documents no async worker execution, pooling, migrations, or generic provider registry. Calls execute synchronously and materialize results into caller arenas. | V8 bridge calls can block the V8 owner thread during SQLite work. | ENGINE-23.C and ENGINE-17 must route SQLite operations through `SERIALIZED_BLOCKING` before claiming scalable provider execution. |
| SQLite already has useful ownership adapters. | `include/sloppy/data_sqlite.h`, `src/data/sqlite.c`, `tests/unit/data/test_sqlite.c` | Result text/blob and future async/offload text/blob parameters are copied into arena-owned storage. | The helper shape is useful but not a complete operation descriptor for queued work. | ENGINE-23.A and ENGINE-23.H must preserve these ownership rules in queued SQLite operations. |
| SQLite V8 bridge enforces capability before direct provider calls. | `src/engine/v8/intrinsics_sqlite.cc` | Open/exec/query/queryOne parse provider metadata, resolve capability tokens, call capability checks, then invoke synchronous native SQLite functions. | Capability enforcement is bridge-side only; provider executor admission does not enforce it yet. | ENGINE-23.F must make capability-gated dispatch mandatory before executor admission. |
| Capability registry is immutable and bridge-ready. | `include/sloppy/capability.h`, `src/core/capability.c`, `tests/unit/core/test_capability.c` | Plan-backed provider/capability metadata is borrowed, validated, and checked for read/write/provider mismatch; denied diagnostics redact secret-like values and tests prove deny-before-provider-work when the hook is called. | A future provider path could bypass the hook if capability checks are not part of provider dispatch. | ENGINE-23.F must require denial before enqueue and tests proving no operation is admitted on denial. |
| Resource IDs exist for JS-visible native handles. | `include/sloppy/resource.h`, `src/core/resource.c`, SQLite V8 bridge files | Resource table validates kind, generation, liveness, and cleanup. SQLite bridge uses opaque handles. | Provider executor operations must retain or validate resources safely across async boundaries. | ENGINE-23.A/B/H must define resource/scope references carried by provider operations. |
| PostgreSQL and SQL Server native providers remain synchronous provider boundaries. | `include/sloppy/data_postgres.h`, `src/data/postgres.c`, `include/sloppy/data_sqlserver.h`, `src/data/sqlserver.c` | Native providers expose open/query/exec/queryOne/transactions and tiny pool skeletons with live tests opt-in. JS bridges are deferred. | Future bridges need provider runtime guidance without implementing them in ENGINE-23. | ENGINE-23.H documents integration modes and keeps PostgreSQL/SQL Server bridge implementation out of scope. |
| Diagnostics distinguish many provider failures but executor lifecycle evidence is still not stress-level. | `docs/diagnostics.md`, `src/data/*`, `tests/unit/core/test_provider_executor.c` | Provider diagnostics cover provider errors, unsupported values, pool exhaustion, and permission denial; executor tests count overflow/cancel/timeout/shutdown, worker failure, post failure, and late completion. | Queue pressure, worker lifecycle, and shutdown need broader stress evidence before production claims. | ENGINE-23.G must add diagnostics and stress smoke without benchmark claims. |
| Public docs already warn about default-gate limits. | `docs/quality-score.md`, `docs/data-providers.md`, `docs/concurrency.md` | Default gates are separated from V8/live-provider/package/benchmark evidence; provider executor is documented as policy, not live DB proof. | Roadmap docs can drift now that ENGINE-12.C/D landed. | This PR updates docs to put ENGINE-23 between ENGINE-12 and ENGINE-13/17. |

## Audit Conclusion

ENGINE-12 established the correct substrate and ENGINE-23.A/B/C now provides descriptors,
admission, and the serialized blocking worker. Provider execution is still not complete
enough for HTTP, SQLite, PostgreSQL, SQL Server, or public alpha claims until the remaining
tasks land. ENGINE-23 remains the implementation bridge between generic async completion
and real data provider runtime work:

1. own provider operation descriptors and memory;
2. bind executors to provider instances and app/request scopes;
3. run SQLite-class blocking work off the V8 owner thread in serialized order;
4. support bounded blocking pools where providers can safely parallelize;
5. keep nonblocking provider mode compatible with Slop completion/cancellation semantics;
6. enforce capabilities before enqueue;
7. preserve deterministic overload, cancellation, timeout, shutdown, late completion, and
   cleanup behavior;
8. produce diagnostics and stress evidence without benchmark claims.

# Provider Execution Runtime Architecture

Status: DATA-RUNTIME-01 strategic architecture with descriptor/admission,
serialized blocking execution, blocking pool execution, true-async mode metadata,
fail-closed unavailable mode, terminal-state plus capability-gated dispatch, and
diagnostics/stress evidence plus provider integration guidance implemented.

ENGINE-23 creates Slop's provider execution and blocking offload runtime. It sits after
ENGINE-12's generic async backend and before deeper provider, SQLite, HTTP, and public
alpha work can rely on scalable provider behavior.

This document is implementation-grade architecture for tasks under
`DATA-RUNTIME-01: Provider Execution Runtime, Async Modes, Admission, Cancellation, and
Diagnostics`.

It does not implement runtime code by itself. It also does not create Node/npm
compatibility, package-manager behavior, green threads/fibers/virtual threads,
PostgreSQL/SQL Server JavaScript bridges, an ORM, migrations, HTTP backend behavior,
public alpha docs, or benchmark claims.

## 1. Provider Runtime Purpose

The provider execution runtime owns the Slop-native path from an admitted provider request
to an owner-thread completion:

- provider instance registry integration;
- execution mode selection;
- provider operation descriptor construction;
- admission and deterministic backpressure;
- blocking offload execution;
- provider queue isolation;
- cancellation and deadline propagation;
- shutdown and late completion cleanup;
- provider diagnostics and counters;
- completion posting to the Slop async runtime.

Provider execution is a core subsystem, not a provider-specific side effect. SQLite,
PostgreSQL, SQL Server, and future providers must enter native work through this contract
when work may outlive the caller stack, block, wait on I/O, or settle JavaScript later.

## 2. Execution Modes

Provider execution modes are core Slop semantics.

### SERIALIZED_BLOCKING

`SERIALIZED_BLOCKING` is one operation at a time for one provider instance.

Rules:

- default execution mode for a single SQLite connection provider instance;
- blocking work runs away from the V8 owner thread;
- queue capacity is bounded per provider instance;
- at most one operation is active for the provider instance;
- late completion after cancellation, timeout, or shutdown is cleanup-only;
- worker code never touches V8 or JS handles.

### BLOCKING_POOL

`BLOCKING_POOL` is a bounded worker pool for one provider instance.

Rules:

- only for providers/drivers that can safely parallelize blocking work;
- worker count is bounded by provider instance configuration;
- queue capacity is bounded by provider instance configuration;
- no thread-per-request model;
- no global uncontrolled provider pool;
- per-instance in-flight limits remain authoritative.

### TRUE_ASYNC

`TRUE_ASYNC` is for providers/clients with true async I/O support.

Rules:

- no worker is occupied while waiting on provider I/O;
- socket readiness, provider async APIs, timers, and wakeups remain internal runtime
  details;
- completions still use Slop cancellation, deadline, backpressure, cleanup, and
  owner-thread settlement semantics;
- provider-specific async state must be owned by the operation or retained scope.

### UNAVAILABLE

`UNAVAILABLE` is an explicit fail-closed provider execution mode.

Rules:

- capability checks still run before admission;
- provider work is rejected before enqueue;
- diagnostics report runtime-feature/provider unavailability without leaking secrets;
- callers must not treat this as skipped, optional, or successful provider evidence.

## 3. Provider Instance Model

A provider instance is a configured logical execution target bound to an app scope. It is
not merely a provider kind.

Required fields:

- provider instance id/name, such as `sqlite:main`;
- provider kind, such as `sqlite`, `postgres`, or `sqlserver`;
- execution mode;
- queue capacity;
- worker count;
- max in-flight operations;
- shutdown state;
- diagnostics counters when scoped and cheap;
- capability requirements;
- config binding, with secret values redacted or referenced indirectly;
- relation to app scope and resource lifecycle.

Example provider instances:

- `sqlite:main`;
- `sqlite:audit`;
- future `postgres:main`;
- future `sqlserver:reporting`.

Instance rules:

- provider instances own admission state independently;
- one overloaded provider instance must not starve or corrupt another provider instance;
- shutdown rejects new admission deterministically;
- app shutdown must transition provider executors before app resources are released;
- provider instance IDs are stable diagnostic context, not raw native pointers.

## 4. Provider Operation Descriptor

ENGINE-23.A/B introduced the implementation-grade `SlProviderOperation` concept for native
tests and future provider bridges. ENGINE-23.C adds the first execution callback path for
`SERIALIZED_BLOCKING` provider-like operations so blocking provider work can run away from
the V8 owner thread while still completing through `SlAsyncLoop`.

Required descriptor fields:

- provider instance id;
- provider kind;
- operation kind, such as `open`, `exec`, `query`, `queryOne`, `close`, or
  provider-specific internal work;
- execution mode;
- required capability;
- request/app scope reference;
- cancellation token;
- deadline;
- owned operation input payload;
- completion target;
- diagnostic context;
- cleanup callback.

Ownership rules:

- provider operation owns memory needed after submission;
- no borrowed request arena views may escape into queued work unless the owning scope is
  explicitly retained and released by the operation;
- SQL strings, params, blobs, config-derived values, and result-conversion context needed
  during work are copied or otherwise owned;
- cleanup runs exactly once;
- late completion after cancellation, timeout, or shutdown is cleanup-only;
- provider worker never touches V8.

Operation state rules:

- admission can fail before ownership transfer;
- after successful admission the executor owns cleanup until terminal dispatch or discard;
- failed admission leaves caller ownership intact and does not run the operation cleanup
  callback;
- terminal states include success, provider failure, cancellation, timeout, overflow, and
  shutdown;
- only one terminal result may be posted;
- double completion returns deterministic invalid-state behavior or becomes cleanup-only
  when the operation is already terminal.

Implemented in ENGINE-23.A/B:

- `include/sloppy/provider_executor.h` defines execution modes, operation kinds,
  capability requirement fields, request/app scope placeholders, cancellation/deadline
  references, diagnostic context, owned input bytes, completion target, and cleanup
  callback.
- `src/core/provider_executor.c` copies provider instance id, provider kind, operation
  name, capability token, diagnostic context, and input bytes into the operation arena
  before successful admission.
- Helpers initialize descriptors, attach owned-input views for later copying, attach
  capability/cancellation/scope/completion cleanup metadata, and preserve existing fields
  on invalid helper inputs.
- No SQL-specific payload conversion, SQLite bridge conversion, PostgreSQL/SQL Server
  bridge, HTTP backend, green threads, Node/npm behavior, public alpha docs, or benchmark
  claims are introduced by this foundation.

Implemented in ENGINE-23.C/D:

- descriptors may attach a native provider run callback used only by the serialized
  blocking and blocking pool executor paths;
- accepted serialized or pool work transfers ownership to the executor before a worker can
  run;
- rejected run callbacks on non-worker modes or unsafe (non-thread-safe) completion
  backends return a deterministic unsupported status without ownership transfer;
- worker callbacks receive only `SlProviderOperation` and caller-owned provider payload
  context; they must not touch V8 or JS handles.

Implemented in ENGINE-23.E/F:

- provider executors require a provider-supplied capability check hook at initialization;
- provider submissions check capability token, required access, provider token, and
  provider kind before slot reservation or worker execution; current database providers
  supply a database capability checker, while future native providers/addons supply their
  own pre-admission policy hook without changing the worker executor;
- denied admission leaves operation ownership with the caller and may write a redacted
  admission diagnostic to caller-provided diagnostic storage;
- read operations require `read` or `readwrite`, write operations require `write` or
  `readwrite`, and readwrite operations require `readwrite`;
- pre-cancelled and deadline-cancelled operations reject before enqueue;
- queued cancellation prevents worker execution when the worker has not claimed the
  operation;
- active cancellation, timeout, and shutdown post terminal completion without claiming that
  the blocking native call was interrupted;
- active-operation cleanup is deferred until the worker callback returns, so late
  completion is cleanup-only and cannot double-settle or free payload still in use.

## 5. Worker And Executor Model

Provider executors own worker lifecycle, queue admission, dispatch, completion posting,
failure handling, shutdown handling, backpressure, and deterministic overflow.
They are generic native-provider/offload infrastructure, not a SQL-only execution path:
database providers are the first users, but the worker model is meant for any Sloppy-owned
native provider/addon that may need bounded blocking work away from the engine owner
thread. Blocking work must use these Slop-owned provider workers rather than libuv's
shared threadpool.

Common executor lifecycle:

1. initialize from provider instance configuration and app scope;
2. start workers or async provider state needed by the selected mode;
3. admit operations only after capability and cancellation checks;
4. queue or dispatch within bounded capacity and in-flight limits;
5. execute provider work without entering V8;
6. post terminal completion to `SlAsyncLoop`;
7. dispatch completion on the owner thread through the existing async/V8 continuation path;
8. run cleanup exactly once;
9. stop admission on shutdown;
10. drain, cancel, or discard according to the configured shutdown policy.

Implemented in ENGINE-23.A/B:

- provider instance id, provider kind, execution mode, queue capacity, pending count,
  in-flight count, max in-flight, future worker count, shutdown state, counters,
  app-owner placeholder, and config-binding placeholder;
- bounded in-memory pending slots with no global uncontrolled provider queue;
- deterministic rejection for invalid descriptors, missing provider instance id,
  provider-kind/instance mismatch, invalid execution mode, queue overflow, pre-cancelled
  tokens, and shutdown;
- explicit ownership transfer only on successful admission;
- dispose and immediate shutdown clean admitted pending/active operations exactly once;
- `SERIALIZED_BLOCKING` activates one operation at a time, while other modes only model
  in-flight metadata and do not run execution engines yet.

Implemented in ENGINE-23.C:

- `SERIALIZED_BLOCKING` starts one long-lived worker per provider instance during executor
  initialization;
- the worker waits on provider-instance state, claims at most one active operation at a
  time, runs provider-like blocking work away from the owner thread, and posts terminal
  completion through the libuv-backed `SlAsyncLoop`;
- FIFO activation is preserved by sequence order and `max_in_flight = 1`;
- queue capacity is enforced before ownership transfer, and overflow leaves the caller
  owning any submission-side payload;
- dispose requests worker stop, joins the worker, then discards any remaining admitted
  pending/active operations exactly once;
- the worker is per provider instance, not per operation.

Implemented in ENGINE-23.D:

- `BLOCKING_POOL` starts a bounded number of long-lived workers per provider instance;
- queue capacity remains fixed and admission rejects overflow before ownership transfer;
- `max_in_flight` defaults to worker count and cannot exceed worker count for pool
  executors;
- workers consume active operations without entering V8 or touching JS handles;
- accepted operation cleanup remains executor-owned and runs exactly once through terminal
  completion cleanup or shutdown/dispose discard;
- pending work is cancelled on shutdown/dispose, while already-running worker callbacks
  complete safely and post through the same async completion path;
- native provider-like tests cover worker caps, queue caps, deterministic overflow,
  cleanup-once success/failure/shutdown behavior, and libuv completion posting without
  requiring V8 or live databases.

Implemented in ENGINE-23.E/F:

- admission is fail-closed when the executor is not initialized with a capability check
  hook;
- capability denial happens before queue slot reservation, before ownership transfer, and
  before provider workers can observe the operation;
- pending cancellation, expired deadline state, shutdown, insufficient capability access,
  wrong capability kind, missing capability, provider-token mismatch, and provider-kind
  mismatch are covered by native tests;
- immediate shutdown posts shutdown terminal completions for queued and active operations;
  active blocking callbacks finish later and are counted as late completions if they try to
  complete after the shutdown terminal state;
- cleanup remains exactly once across success, provider failure, cancellation, timeout,
  shutdown, overflow, denied admission, completion-post failure, dispose, and late worker
  completion.

`SERIALIZED_BLOCKING` behavior:

- single long-lived worker per provider instance;
- one active operation per provider instance;
- bounded queue enforced before ownership transfer;
- shutdown uses the current immediate-cancel policy for admitted work, and late worker
  completion after cancellation/shutdown is cleanup-only;
- completion posting requires a thread-safe async backend; current worker tests use the
  libuv backend, while the deterministic test backend remains single-threaded;
- SQLite single-connection providers use this mode by default, but real SQLite bridge
  conversion remains ENGINE-17.

`BLOCKING_POOL` behavior:

- bounded worker count;
- bounded queue;
- bounded max in-flight operations no larger than worker count;
- no thread-per-request behavior;
- overflow is deterministic and diagnostic-backed;
- shutdown rejects new work, cancels pending work through terminal completions, lets
  already-running worker callbacks finish without native interruption, treats their later
  worker result as late completion, and preserves cleanup-once behavior;
- provider-like tests prove parallel active execution is capped by configured workers and
  queued work is promoted without creating one thread per operation.

`TRUE_ASYNC` requirements:

- provider async wait state is owned by operation/executor;
- waiting does not occupy a blocking worker;
- provider callbacks/wakeups post back through Slop completion queues;
- provider-specific cancellation is optional but must not bypass terminal-state cleanup.

## 6. Cancellation And Deadline Model

Slop cancellation/deadline state is generic and terminal. Provider-specific interruption is
optional and provider-specific.

Rules:

- cancellation before admission rejects without enqueuing work;
- deadline reached before admission rejects without enqueuing work;
- cancellation after admission marks the operation terminal from Slop's perspective;
- timeout after admission marks the operation terminal with deadline semantics;
- shutdown after admission marks pending/active operations according to shutdown policy;
- cancellation does not guarantee a blocking native provider call stops immediately;
- provider-specific interruption hooks can be added where drivers support them;
- late completion after cancel, timeout, or shutdown is ignored or cleanup-only;
- diagnostics distinguish cancel, timeout, overflow, shutdown, provider failure, and
  worker/executor failure.

Provider-specific examples:

- SQLite may later use `sqlite3_interrupt` for a connection if the owning executor can
  prove it is safe for the active operation.
- PostgreSQL may use libpq cancellation or a nonblocking libpq state machine when that
  bridge is scoped.
- SQL Server may use ODBC cancellation if driver behavior is documented and tested.

DATA-RUNTIME-01 preserves generic terminal-state correctness for pre-cancelled admission,
queued/active cancellation, timeout marking, immediate shutdown, and late worker result
handling. Provider-specific interruption paths remain deferred provider-specific work; no
SQLite, PostgreSQL, or SQL Server interruption hook is implemented here.

## 7. Capability And Security Model

Capability checks are an admission precondition.

Rules:

- capability check must occur before provider work starts;
- denied access must not enqueue a provider operation;
- provider mismatch is denied before execution;
- no provider may bypass capability checks, admission, backpressure, cancellation, or
  shutdown policy;
- diagnostics must redact secrets and avoid raw connection strings;
- denied diagnostics identify token, operation, provider context, and access mismatch
  without exposing secret config values.

The current database runtime capability registry is immutable and Plan-backed. DATA-RUNTIME-01
wires provider-supplied capability hooks into dispatch so future provider bridges cannot
accidentally call native provider code directly after parsing JS arguments. Executor
initialization requires a capability hook and provider token; descriptor admission then
checks the requested capability before enqueue. Missing capability, insufficient access,
wrong capability kind, provider-token mismatch, and provider-kind mismatch reject before
ownership transfer.

## 8. Libuv Relationship

Libuv is internal backend plumbing for eventing, timers, wakeups, and completions. It is not
Slop's public runtime model.

Rules:

- libuv global threadpool is not the Slop provider runtime;
- provider execution uses Slop-owned provider executors;
- provider worker code posts completions to `SlAsyncLoop`;
- no libuv types appear in public provider APIs;
- libuv handles remain under `src/platform/libuv/*`;
- core provider runtime APIs remain Slop-owned and testable through deterministic native
  backends where practical.

## 9. Green Thread And Fiber Decision

Slop will not implement green threads, fibers, or virtual threads for provider execution
now.

Decision:

- lightweight operation descriptors are the chosen model;
- blocking C calls still require OS-thread-backed execution or true async provider APIs;
- a green-thread abstraction would not make blocking native database drivers nonblocking;
- adding fibers before the provider runtime exists would obscure ownership, cancellation,
  and cleanup paths.

Revisit only after core runtime evidence exists: production provider executors, real
SQLite offload, nonblocking provider proof if needed, deterministic shutdown, and stress
evidence without benchmark claims.

## 10. Test And Evidence Model

ENGINE-23 implementation PRs must add tests proportional to risk and must avoid benchmark
marketing.

Required test classes:

- native unit tests for descriptor ownership, cleanup-once, invalid descriptors, and
  capability-denied admission;
- executor tests for per-instance queue capacity, serialized activation, blocking pool
  bounds, deterministic overflow, and shutdown;
- libuv-backed tests for worker completion posting and owner-thread drain;
- provider-like test operations that sleep/block without touching real DBs where that is
  enough to test scheduling;
- V8-gated Promise completion tests proving provider completions settle only on the owner
  thread;
- overload/backpressure tests for full queues and max in-flight limits;
- shutdown and late completion tests for pending and active operations;
- stress smoke that exercises many bounded operations and reports it as local/manual
  stress evidence, not benchmark performance.
- admission and terminal diagnostics/counters for overload, invalid operation, shutdown,
  worker failure, operation failure, cancellation, timeout, late completion, capability
  denial, and cleanup-once behavior. These diagnostics are redacted and must not expose
  raw native pointers, V8/libuv internals, SQL parameters, connection strings, or secret
  values.

Evidence reporting:

- default gates prove non-V8 native executor behavior only;
- V8-gated tests prove owner-thread JS settlement only when SDK evidence is available;
- live provider tests remain separate from default executor tests;
- stress smoke is not a production benchmark;
- benchmark claims remain out of scope.

## 11. DATA-RUNTIME-01 Task Map

Implemented scope:

- provider operation descriptor and ownership contract;
- per-provider-instance executor model;
- serialized blocking executor for SQLite-class providers;
- blocking pool executor and admission policy;
- true-async and unavailable execution mode contracts;
- cancellation, timeout, and late completion semantics;
- capability-gated provider dispatch;
- provider executor diagnostics and stress evidence;
- provider runtime integration guide for SQLite/PostgreSQL/SQL Server.

Dependencies:

- ENGINE-12.A/B/C/D completion, cancellation, deadline, shutdown, and backpressure policy;
- existing capability registry hooks;
- resource table and app/request scope ownership primitives;
- V8 owner-thread continuation scheduler for V8-gated settlement tests;
- data provider native boundaries for SQLite/PostgreSQL/SQL Server.

Work after ENGINE-23:

- ENGINE-13 proper HTTP backend may consume shared pressure/cancellation semantics but does
  not implement provider runtime;
- SQLite runtime completion should route SQLite operations through the provider
  executor before claiming scalable provider behavior;
- PostgreSQL and SQL Server JS bridges remain deferred until the provider runtime and
  SQLite path are solid.

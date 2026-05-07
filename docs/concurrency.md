# Concurrency And Async Model

Sloppy concurrency is owner-thread based. Native worker threads and platform callbacks may
complete work, but JavaScript execution enters a V8 isolate only through the owning engine
thread and documented bridge boundaries.

## Core Rules

- V8 isolate access is owner-thread only.
- Platform callbacks never expose OS or libuv handles outside platform/runtime boundaries.
- Native async completions post back into Sloppy-owned completion paths.
- Cancellation, timeout, shutdown, and late completion are explicit terminal states.
- Cleanup is once-only and tied to request, app, resource, or provider lifetime.
- Optional async/provider/stress evidence lanes are separate from default evidence.

## Request And App Lifetimes

The app owns startup resources until shutdown. A request owns request-scoped arena storage,
cleanup registrations, cancellation/deadline state, and resource references for the
duration of handler dispatch. Request cleanup runs after success, failure, cancellation, or
timeout. Independently closable resources still belong in resource-table entries and must
be closed through registered cleanup when request-scoped.

## V8 Async Boundary

Direct async handlers are supported only when the returned Promise settles during the
bounded V8 owner-thread microtask drain. This is not a Node event loop, timer/fetch/process
compatibility layer, or arbitrary pending-native-async runtime. If a Promise cannot settle
within the scoped owner-thread drain contract, the runtime must fail clearly rather than
report partial/default validation as success.

## Provider Work

Provider work is separated from generic async completion. Provider descriptors, admission,
capability checks, executor mode, bounded queues, cancellation, deadline behavior, and late
completion must remain provider-owned runtime contracts. SQLite-class blocking work may use
serialized/offloaded provider execution where the scoped lane supports it. PostgreSQL and
SQL Server JavaScript bridges remain deferred unless their own source docs and evidence
lanes prove the bridge.

## HTTP Transport

The HTTP transport lives behind platform/runtime abstractions. It owns bounded connection
and request admission, read/header/body/request/write timeouts, disconnect handling,
shutdown terminal paths, bounded sequential keep-alive, and scoped chunked handling.

Transport callbacks must not enter V8 directly. Dispatch crosses into the engine through
the runtime-owned handler boundary after request parsing, capability checks, and lifecycle
setup. Pipelining, public request/response streaming APIs, HTTP/2, HTTP/3, WebSockets,
production graceful drain, and scalable async HTTP claims remain out of scope until scoped
source docs and evidence lanes define them.

## Time And Deadlines

Time APIs provide Sloppy-owned delay, deadline, cancellation, interval, scheduled job, and
fake-clock semantics where the active runtime bridge is available. Deadline-aware APIs must
observe pre-cancelled and expired inputs before work submission. Native work that has
already been submitted may still complete later; late completion must be cleanup-only and
must not double-settle caller-visible state.

## Evidence Requirements

Concurrency and async PRs should include:

- source docs and invariants under review;
- owner-thread and native-thread boundaries;
- cancellation/deadline/shutdown outcomes;
- late-completion behavior;
- cleanup-once checks;
- negative paths for overflow, timeout, cancellation, invalid lifecycle, and unsupported
  features;
- separate reporting for default, V8, provider, stress/torture, sanitizer, and benchmark
  lanes.

Skipped optional lanes are not pass evidence. Benchmark or stress smoke is not production
or performance evidence.

# 0014: Concurrency and Async Model

## Status

Accepted.

## Context

Sloppy must handle many concurrent backend requests. ASP.NET Core async resumes
continuations on ThreadPool workers. JavaScript runtimes like Node, Bun, and Deno generally
execute JS callbacks sequentially per worker/isolate.

V8 isolate access must be controlled and must not be entered by arbitrary native worker
threads. Sloppy needs a safe model for promises, request scopes, native async operations, DB
providers, and future workers.

## Decision

Sloppy uses one JS event-loop thread per JS worker/isolate. One V8 isolate is owned by one
JS thread. Native I/O and worker-pool work may happen elsewhere. Completions are posted
back to the owning JS event-loop thread.

Sloppy does not use thread-per-request. Sloppy does not run JS continuations on arbitrary
thread-pool threads. Blocking native operations use bounded worker-pool/provider executors.
CPU-bound JS scales through future workers, isolates, or processes, not parallel execution
inside one isolate.

## Consequences

Sloppy has a JS concurrency model similar to Node, Bun, and Deno. This gives safer V8
integration and supports high I/O concurrency. CPU-heavy JS can block one worker, so
multiple workers/isolates are needed later for multicore JS scaling.

Request scopes must survive pending promises. The runtime must implement cancellation and
backpressure carefully.

## Alternatives Considered

- ASP.NET-style arbitrary thread-pool continuation for JS: rejected because it violates the
  owner-thread model for one V8 isolate.
- Thread-per-request: rejected because it does not fit JS isolate execution and scales
  poorly for many I/O-bound requests.
- Calling into V8 from worker-pool threads: rejected because it makes engine ownership
  unsafe and hard to reason about.
- Fully synchronous request handling: rejected because Sloppy must support many in-flight
  I/O-bound requests.
- Multiple isolates from day one: rejected because the single-worker model should be proven
  before multicore scaling is added.

## Follow-up Tasks

- `docs/concurrency.md`.
- Event loop abstraction.
- V8 owner-thread enforcement.
- Promise settlement tests.
- Request scope lifetime.
- Worker pool abstraction.
- DB provider async strategy.
- Cancellation/deadlines/backpressure.
- Multi-worker design later.

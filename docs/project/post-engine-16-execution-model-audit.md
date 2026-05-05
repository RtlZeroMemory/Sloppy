# Post-ENGINE-16 Execution Model Audit

Status: 2026-05-05 planning/consolidation audit, updated by ENGINE-26.A/B. This document
inspects current code and tests after ENGINE-15 and ENGINE-16 and records the execution
domain source-of-truth added for Roadmap-2.

## Source Inputs

- Docs: `docs/architecture.md`, `docs/execution-model.md`, `docs/concurrency.md`,
  `docs/memory.md`, `docs/diagnostics.md`, `docs/testing-strategy.md`,
  `docs/modules/engine-v8/README.md`, `docs/modules/http/README.md`,
  `docs/modules/data/README.md`, and `docs/modules/app-host/README.md`.
- Code: `include/sloppy/*`, `src/core/*`, `src/platform/libuv/*`,
  `src/engine/v8/*`, `src/data/*`, `compiler/src/*`, `stdlib/sloppy/*`,
  and current unit/integration/conformance/golden tests.

## Current Execution Domains

| Domain | What runs there | Must never run there | Owned data | Boundary crossing |
| --- | --- | --- | --- | --- |
| V8 owner thread | V8 isolate/context lifecycle, app evaluation, handler calls, Promise microtask drains, result conversion, SQLite bridge callbacks, and native async Promise settlement in `async_scheduler.cc`. | Provider worker callbacks, raw libuv socket callbacks from non-owner threads, direct OS APIs, raw native pointer exposure to JS. | `SlV8Engine`, V8 handles, registered handler table, engine resource table, borrowed Plan/capability pointers. | Native code may call V8 only after `sl_v8_check_owner_thread`; async completions must dispatch through `SlAsyncLoop` and `async_scheduler.cc`. |
| Libuv async backend | Thread-safe completion posting, owner-loop wakeup, owner-thread dispatch, pending completion cleanup on dispose. | V8 entry from libuv worker/non-owner threads; feature-specific provider logic. | `SlAsyncLoop`, fixed completion queue, retained scope refs, libuv async handle/mutex. | `sl_async_loop_post` copies completion records and retains scopes only after successful post. |
| HTTP libuv transport | TCP bind/listen/accept/read/write callbacks, request accumulation, keep-alive reset, timeout callbacks, shutdown close paths. | Direct V8 entry from transport callbacks unless through the dispatch/runtime contract path; libuv types in public headers. | `SlHttpTransportServer`, per-connection arenas/builders/timers, backend request lifecycle. | Dispatch callback returns `SlHttpResponse`; write callbacks complete or close request lifecycle; late close/write callbacks are cleanup-only. |
| Core HTTP backend | Backend/listener/connection/request state transitions, body-reader ownership, cancellation/shutdown terminal states. | Socket-specific behavior and V8-specific behavior. | Backend counters, request lifecycle, cancellation token, request arena. | Transport owns platform events and calls backend state helpers. |
| Provider executor workers | Serialized/blocking-pool provider `run` callbacks and terminal completion posting. | V8 entry, JS handle inspection, borrowed request scratch views without scope retention. | Arena-owned `SlProviderOperation` metadata/input, worker slots, executor counters. | Operation descriptors copy provider IDs, operation names, capability token, diagnostic context, and input bytes before worker visibility. |
| App/request lifecycle | App start/stop/drain/force shutdown; request-scope terminal outcome and cleanup-once behavior. | Production monitoring claims; hidden runtime-global state. | `SlAppLifecycle`, `SlAppRequestScope`, `SlScope`, resource cleanup payloads. | Request scopes can tie cleanup to app lifecycle; late completions are rejected as stale lifecycle work. |
| Compiler/process tooling | `sloppyc` parser/extractor/goldens, process CLI commands, source-input compilation. | Runtime scheduling, V8 isolate ownership, native provider execution. | Rust-owned artifacts and diagnostics until file/artifact boundary. | Artifacts cross into C runtime as Plan, bundle, and source map bytes. |

The canonical machine-readable domain table is `include/sloppy/execution_domain.h`, covered
by `core.execution_domain`. It defines stable names, the single V8-entry domain, which
domains require copied/owned cross-thread data, which domains may run classified blocking
work, and which domains must route JavaScript continuation through the owner-thread
scheduler.

## Findings

| Area | Status | Evidence | Gap / risk |
| --- | --- | --- | --- |
| V8 owner-thread isolation | Complete for current V8 entrypoints | `engine_v8.cc` records owner thread and rejects eval/call/info on wrong thread; `tests/unit/engine/test_v8_owner_thread.cc` covers initialized owner identity, wrong-thread calls, and destroy deferral. | Real provider/HTTP/timer continuations still need adoption in later Roadmap-2 work. |
| Native async completion queue | Partial | `SlAsyncLoop` has libuv and test backends; libuv post is thread-safe and owner-loop dispatch checks owner thread. | Policy is split across headers/docs; shutdown/deadline/backpressure behavior is implemented for provider operations but not a universal runtime contract. |
| V8 native continuation scheduler | Partial | `src/engine/v8/async_scheduler.cc` is the only code path that settles V8 Promise state from native completions; `include/sloppy/execution_domain.h` marks all non-owner domains as requiring owner-thread dispatch before JS continuation. | It is proven with test continuations, not with real SQLite/provider/HTTP/timer completions. |
| Provider workers and V8 separation | Partial / good boundary | `provider_executor.c` worker loop invokes provider `run` callbacks and posts completion; comments, API, and `core.execution_domain` classify provider workers as non-V8. | SQLite bridge still calls provider functions synchronously on the V8 callback path, so the intended worker boundary is not adopted by the real JS SQLite path. |
| HTTP transport terminal checks | Complete for current transport | `http_transport_libuv.c` checks terminal connection state before timeout/write/idle callbacks; timeout/write paths close and treat late write as cleanup-only. | Route-level deadline policy, request IDs, access events, and production drain policy are missing. |
| App/request lifecycle cleanup | Complete for native helper layer | `app_host.c` records terminal outcomes, rejects late completions, closes cleanup once, and exposes leak snapshots. | Timer/callback/provider-operation counters are reserved placeholders until the owners integrate. |
| Cancellation/deadline propagation | Partial | `SlCancellationToken`, V8 pre/post checks, HTTP timeout transitions, and provider executor terminal mapping exist. | Deadlines are not a coherent cross-domain object yet; provider-specific native interruption remains future work. |
| Blocking offload policy | Partial | Provider executor supports serialized and bounded blocking-pool modes. | No runtime feature routes SQLite bridge through it; unclear sync paths remain in V8 SQLite bridge. |
| Race-oriented tests | Partial | Unit tests cover async backend, provider executor, app lifecycle, resource table, HTTP transport, and V8 owner thread. | No torture matrix for late completion, forced shutdown, provider worker races, or many pending native continuations. |

## Async/Threading Search Notes

The audit searched for cross-thread callbacks, completion queues, cancellation checks,
terminal-state checks, V8 entrypoints, provider worker callbacks, libuv callbacks, and
TODO/FIXME/temporary/deferred/later language around async/threading. The highest-signal
code reality is:

- `include/sloppy/async_backend.h` still describes ENGINE-12.C as future policy, while
  provider-executor policy has since landed. The canonical docs should now point to
  Roadmap-2 for the remaining universal runtime policy.
- `include/sloppy/http_backend.h` still has comments from the close-after-response era.
  The backend layer is still lower-level than HTTP-25, but canonical docs must avoid
  implying keep-alive is entirely absent.
- `src/engine/v8/intrinsics_sqlite.cc` calls `sl_sqlite_open`, `sl_sqlite_exec`,
  `sl_sqlite_query`, `sl_sqlite_query_one`, and transaction helpers directly inside V8
  callbacks. That is the clearest V8-blocking sync work path.

## Recommended Roadmap-2 Work

- ENGINE-26.A: Execution Domain Source-of-Truth.
- ENGINE-26.B: V8 Owner-Thread Scheduler Invariants.
- ENGINE-26.C: Cross-Thread Completion Queue and Terminal-State Checks.
- ENGINE-26.D: Cancellation and Deadline Propagation Across Domains.
- ENGINE-26.E: Blocking Work and Offload Policy.
- ENGINE-26.F: Race-Oriented Concurrency Tests.

ENGINE-26 should land before provider bridge expansion, metrics, or torture work.

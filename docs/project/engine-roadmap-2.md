# Engine Roadmap-2

Status: 2026-05-05 owner-review planning source. Roadmap-2 focuses on runtime
architecture maturation before the next framework expansion. It does not implement new
runtime/compiler/framework/provider features in this PR.

## Current Runtime State

| Area | State |
| --- | --- |
| Compiler and Plan metadata | Strong supported-subset compiler and Plan metadata exist, including routes, capabilities, provider effects, source maps, completeness, and Strong Plan consumer output. Broader TypeScript checking, imported helper inference, and typed graph/versioning follow-ups remain open. |
| V8 execution | Optional V8 lane supports registered handlers, bounded microtask Promise settlement, request/result conversion, source-map exception primary-span remapping, and SQLite bridge calls. Timers/fetch/native async sources and real provider-completion integration remain missing. |
| HTTP runtime | Bounded localhost HTTP/1.1 transport supports sequential keep-alive, idle timeout, max requests, lifecycle reset, chunked request decoding, internal chunked response writer, and stress/conformance smoke. Route-level policy, request IDs, access events, trusted proxy policy, and production drain remain missing. |
| Provider runtime | Native provider executor exists, with copied descriptors, bounded admission, worker modes, cancellation/shutdown terminal posting, late-completion cleanup, and diagnostics/counters. SQLite JS bridge is still synchronous and not executor-backed. PostgreSQL/SQL Server JS bridges remain deferred. |
| App/resource lifecycle | ENGINE-16 provides app/request lifecycle states, terminal outcomes, cleanup-once invariants, typed resource cleanup, leak snapshots, and lifecycle diagnostics for the native helper layer. Timer/callback/provider-operation counter integration remains missing. |
| Diagnostics | ENGINE-15 provides stable codes, deterministic text/JSON/source-frame rendering, source-map exception remapping for current V8 claims, redaction coverage, and diagnostic goldens. Runtime events/counters and access observability remain missing. |
| Public alpha | Still blocked. Roadmap-2 is not public alpha docs and makes no benchmark/performance claims. |

## Non-Goals

- No runtime feature implementation in this consolidation PR.
- No public alpha docs.
- No benchmark/performance claims.
- No Node/npm/package-manager compatibility.
- No PostgreSQL or SQL Server JavaScript bridge implementation.
- No broad code rewrite.
- No generated/build artifacts.

## EPIC ENGINE-26: Execution Model Hardening

Goal: Lock Slop's final-ish execution/threading/offload model before more runtime
expansion.

Tasks:

- ENGINE-26.A: Execution Domain Source-of-Truth.
- ENGINE-26.B: V8 Owner-Thread Scheduler Invariants.
- ENGINE-26.C: Cross-Thread Completion Queue and Terminal-State Checks.
- ENGINE-26.D: Cancellation and Deadline Propagation Across Domains.
- ENGINE-26.E: Blocking Work and Offload Policy.
- ENGINE-26.F: Race-Oriented Concurrency Tests.

## EPIC ENGINE-27: Runtime Feature Modularity

Goal: Make runtime features Plan/import/use-driven instead of always-on bloat.

Tasks:

- ENGINE-27.A: Runtime Feature Registry.
- ENGINE-27.B: Plan-Driven Feature Activation.
- ENGINE-27.C: Provider/Transport/Stdlib Feature Descriptors.
- ENGINE-27.D: V8 Intrinsic Registration by Feature.
- ENGINE-27.E: Missing Feature Diagnostics.
- ENGINE-27.F: Package Include-Only-Used Feature Policy.

## EPIC ENGINE-28: Provider Runtime Maturation

Goal: Finalize provider operation/offload semantics and move SQLite bridge through the
provider executor.

Tasks:

- ENGINE-28.A: Provider Operation Descriptor Contract.
- ENGINE-28.B: SQLite JS Bridge Through Serialized Provider Executor.
- ENGINE-28.C: Provider Cancellation/Deadline/Backpressure.
- ENGINE-28.D: Provider Result Ownership and V8 Resumption.
- ENGINE-28.E: Provider Diagnostics and Redaction.
- ENGINE-28.F: Provider Runtime Conformance.

## EPIC HTTP-26: Route-Level HTTP Policy and Observability

Goal: Mature HTTP as an application server layer beyond protocol features.

Tasks:

- HTTP-26.A: Route-Level Limits and Timeout Policy From Plan/Config.
- HTTP-26.B: Request ID and Correlation Context.
- HTTP-26.C: Access Log/Event Model.
- HTTP-26.D: Trusted Proxy and Forwarded Headers Policy.
- HTTP-26.E: HTTP Error Response Consistency.
- HTTP-26.F: HTTP Policy Conformance.

## EPIC ENGINE-29: Runtime Events and Metrics

Goal: Add internal runtime counters/events before benchmark and torture work.

Tasks:

- ENGINE-29.A: Runtime Event Model.
- ENGINE-29.B: Counter Registry.
- ENGINE-29.C: HTTP Transport Counters.
- ENGINE-29.D: Provider Executor Counters.
- ENGINE-29.E: Scope/Resource/Lifecycle Counters.
- ENGINE-29.F: CLI/Test Reporting for Runtime Stats.

## EPIC ENGINE-30: Runtime Torture and Crash-Resistance Harness

Goal: Stress timing/lifecycle edges after execution/modularity/provider/http/metrics are
mature.

Tasks:

- ENGINE-30.A: Lifecycle Torture Harness.
- ENGINE-30.B: HTTP Disconnect/Shutdown Timing Matrix.
- ENGINE-30.C: V8 Async/Cancel Torture Matrix.
- ENGINE-30.D: Provider Late Completion Torture Matrix.
- ENGINE-30.E: Resource Leak and Stale Handle Torture.
- ENGINE-30.F: Sanitizer/Valgrind/Cross-Platform Strategy.

## Recommended Implementation Order

1. ENGINE-26 Execution Model Hardening.
2. ENGINE-27 Runtime Feature Modularity.
3. ENGINE-28 Provider Runtime Maturation.
4. HTTP-26 Route-Level HTTP Policy and Observability.
5. ENGINE-29 Runtime Events and Metrics.
6. ENGINE-30 Runtime Torture Harness.

Parallelism guidance:

- ENGINE-26 first, no parallel.
- ENGINE-27 can start after ENGINE-26.A/B if boundaries are stable.
- ENGINE-28 should wait for ENGINE-26 and key ENGINE-27 feature registry decisions.
- HTTP-26 can start after ENGINE-26 and can run in parallel with ENGINE-28 if file
  conflicts are manageable.
- ENGINE-29 should follow ENGINE-26 and consume HTTP/provider counters.
- ENGINE-30 should run after ENGINE-26, ENGINE-28, and ENGINE-29.

## Quality Rules For Roadmap-2 Issues

Every implementation issue that touches C runtime code must include:

- C17.
- Existing Slop memory/string/buffer/diagnostics primitives.
- Explicit ownership/lifetime expectations.
- No raw native pointers exposed to JS.
- V8 types only under `src/engine/v8/*`.
- libuv types internal/private.
- Provider workers must not enter V8.
- Cross-thread data must be copied/owned.
- Cleanup exactly once.
- Terminal-state/late-completion checks.
- Deterministic diagnostics and tests.
- Evidence lanes reported separately.

Every Rust/compiler issue must include:

- rustfmt.
- clippy `-D warnings`.
- deterministic diagnostics.
- no panics for user input.
- no `unwrap`/`expect` in production paths except documented impossible invariants.
- focused tests/goldens.

# Engine Roadmap-2 Issue Index

Status: 2026-05-05 planning index. GitHub issue numbers were created by the
post-ENGINE-16 consolidation PR.

## EPIC ENGINE-26: Execution Model Hardening

Goal: Lock Slop's final-ish execution/threading/offload model before more runtime
expansion.

| Key | Issue | Title |
| --- | --- | --- |
| ENGINE-26 | #488 | EPIC ENGINE-26: Execution Model Hardening |
| ENGINE-26.A | #494 | Execution Domain Source-of-Truth |
| ENGINE-26.B | #495 | V8 Owner-Thread Scheduler Invariants |
| ENGINE-26.C | #496 | Cross-Thread Completion Queue and Terminal-State Checks |
| ENGINE-26.D | #497 | Cancellation and Deadline Propagation Across Domains |
| ENGINE-26.E | #498 | Blocking Work and Offload Policy |
| ENGINE-26.F | #499 | Race-Oriented Concurrency Tests |

## EPIC ENGINE-27: Runtime Feature Modularity

Goal: Make runtime features Plan/import/use-driven instead of always-on bloat.

| Key | Issue | Title |
| --- | --- | --- |
| ENGINE-27 | #489 | EPIC ENGINE-27: Runtime Feature Modularity |
| ENGINE-27.A | #500 | Runtime Feature Registry |
| ENGINE-27.B | #501 | Plan-Driven Feature Activation |
| ENGINE-27.C | #502 | Provider/Transport/Stdlib Feature Descriptors |
| ENGINE-27.D | #503 | V8 Intrinsic Registration by Feature |
| ENGINE-27.E | #504 | Missing Feature Diagnostics |
| ENGINE-27.F | #505 | Package Include-Only-Used Feature Policy |

## EPIC ENGINE-28: Provider Runtime Maturation

Goal: Finalize provider operation/offload semantics and move SQLite bridge through provider
executor.

| Key | Issue | Title |
| --- | --- | --- |
| ENGINE-28 | #490 | EPIC ENGINE-28: Provider Runtime Maturation |
| ENGINE-28.A | #506 | Provider Operation Descriptor Contract |
| ENGINE-28.B | #507 | SQLite JS Bridge Through Serialized Provider Executor |
| ENGINE-28.C | #508 | Provider Cancellation/Deadline/Backpressure |
| ENGINE-28.D | #509 | Provider Result Ownership and V8 Resumption |
| ENGINE-28.E | #510 | Provider Diagnostics and Redaction |
| ENGINE-28.F | #511 | Provider Runtime Conformance |

## EPIC HTTP-26: Route-Level HTTP Policy and Observability

Goal: Mature HTTP as an application server layer beyond protocol features.

| Key | Issue | Title |
| --- | --- | --- |
| HTTP-26 | #491 | EPIC HTTP-26: Route-Level HTTP Policy and Observability |
| HTTP-26.A | #512 | Route-Level Limits and Timeout Policy From Plan/Config |
| HTTP-26.B | #513 | Request ID and Correlation Context |
| HTTP-26.C | #514 | Access Log/Event Model |
| HTTP-26.D | #515 | Trusted Proxy and Forwarded Headers Policy |
| HTTP-26.E | #516 | HTTP Error Response Consistency |
| HTTP-26.F | #517 | HTTP Policy Conformance |

## EPIC ENGINE-29: Runtime Events and Metrics

Goal: Add internal runtime counters/events before benchmark and torture work.

| Key | Issue | Title |
| --- | --- | --- |
| ENGINE-29 | #492 | EPIC ENGINE-29: Runtime Events and Metrics |
| ENGINE-29.A | #518 | Runtime Event Model |
| ENGINE-29.B | #519 | Counter Registry |
| ENGINE-29.C | #520 | HTTP Transport Counters |
| ENGINE-29.D | #521 | Provider Executor Counters |
| ENGINE-29.E | #522 | Scope/Resource/Lifecycle Counters |
| ENGINE-29.F | #523 | CLI/Test Reporting for Runtime Stats |

## EPIC ENGINE-30: Runtime Torture and Crash-Resistance Harness

Goal: Stress timing/lifecycle edges after execution/modularity/provider/http/metrics are
mature.

| Key | Issue | Title |
| --- | --- | --- |
| ENGINE-30 | #493 | EPIC ENGINE-30: Runtime Torture and Crash-Resistance Harness |
| ENGINE-30.A | #524 | Lifecycle Torture Harness |
| ENGINE-30.B | #525 | HTTP Disconnect/Shutdown Timing Matrix |
| ENGINE-30.C | #526 | V8 Async/Cancel Torture Matrix |
| ENGINE-30.D | #527 | Provider Late Completion Torture Matrix |
| ENGINE-30.E | #528 | Resource Leak and Stale Handle Torture |
| ENGINE-30.F | #529 | Sanitizer/Valgrind/Cross-Platform Strategy |

## Implementation Order

1. ENGINE-26 Execution Model Hardening.
2. ENGINE-27 Runtime Feature Modularity.
3. ENGINE-28 Provider Runtime Maturation.
4. HTTP-26 Route-Level HTTP Policy and Observability.
5. ENGINE-29 Runtime Events and Metrics.
6. ENGINE-30 Runtime Torture Harness.

ENGINE-26 is the prerequisite boundary-setting wave. ENGINE-30 should wait until
execution, provider, and metrics foundations are mature enough for torture evidence to be
interpretable.

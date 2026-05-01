# ENGINE-23 Provider Execution Issue Index

Status: created GitHub issue index.

Source architecture:

- `docs/project/archive/post-core-mvp/provider-execution-current-state-audit.md`;
- `docs/project/provider-execution-runtime-architecture.md`.

## Issues

| Issue | Title | Purpose |
| --- | --- | --- |
| #390 | ENGINE-23: Provider Execution and Blocking Offload Runtime | Parent EPIC for provider execution/offload runtime. |
| #391 | TASK ENGINE-23.A: Provider Operation Descriptor and Ownership Contract | Own queued provider inputs, scope references, capability requirement, deadline/cancellation, completion target, and cleanup contract. |
| #392 | TASK ENGINE-23.B: Per-Provider-Instance Executor Model | Define provider instance executor state, execution mode, queue capacity, in-flight limits, shutdown state, and counters. |
| #393 | TASK ENGINE-23.C: Serialized Blocking Executor for SQLite-Class Providers | Implement one-active-operation serialized blocking execution for single-connection providers such as SQLite. |
| #394 | TASK ENGINE-23.D: Blocking Pool Executor and Admission Policy | Implement bounded provider-instance worker pools for providers that can safely parallelize blocking work. |
| #395 | TASK ENGINE-23.E: Provider Cancellation, Timeout, and Late Completion Semantics | Apply Slop cancellation/deadline/shutdown terminal states to provider operations and late completion cleanup. |
| #396 | TASK ENGINE-23.F: Capability-Gated Provider Dispatch | Deny provider work before enqueue/execution when capability checks fail or provider kind mismatches. |
| #397 | TASK ENGINE-23.G: Provider Executor Diagnostics and Stress Evidence | Add queue/worker/cancel/timeout/shutdown/late-completion diagnostics and bounded stress smoke without benchmark claims. |
| #398 | TASK ENGINE-23.H: Provider Runtime Integration Guide for SQLite/PostgreSQL/SQL Server | Document how future provider bridges integrate with the executor model without implementing them. |

## Recommended Implementation Order

1. ENGINE-23.A.
2. ENGINE-23.B.
3. ENGINE-23.C.
4. ENGINE-23.D.
5. ENGINE-23.E.
6. ENGINE-23.F.
7. ENGINE-23.G.
8. ENGINE-23.H if useful.

## Dependencies

- ENGINE-23 depends on ENGINE-12.A/B/C/D for `SlAsyncLoop`, libuv-backed completion
  posting, owner-thread V8 continuation scheduling, cancellation/deadline/shutdown
  reasons, deterministic overflow, and cleanup-once policy.
- ENGINE-23.A must precede executor implementation because queued provider work needs a
  stable owned-input and cleanup contract.
- ENGINE-23.B must precede production executors because provider instance admission state
  defines queue capacity, in-flight limits, worker count, shutdown state, and diagnostics
  counters.
- ENGINE-23.C and ENGINE-23.D both depend on A/B and share worker lifecycle concerns.
- ENGINE-23.E depends on at least one executor path so late completion, cancellation,
  timeout, and shutdown can be tested against real provider-runtime behavior.
- ENGINE-23.F depends on A/B so denial can occur before operation ownership transfers into
  an executor.
- ENGINE-23.G depends on executor and terminal-state paths from C/D/E/F.
- ENGINE-23.H can start as docs after A/B shape is stable, but final guidance should match
  the implemented C/D/E/F behavior.

## Parallel Work

Can run in parallel:

- Diagnostics wording and fixture planning for ENGINE-23.G can start while A/B are
  implemented.
- ENGINE-23.H draft guidance can start once the A/B descriptor/admission vocabulary is
  stable enough.
- Test fixture scaffolding for serialized blocking and blocking pool executors can be
  prepared in parallel if write sets are kept separate.

Must not run in parallel without an agreed owner:

- ENGINE-23.C and ENGINE-23.D should not both rewrite shared worker lifecycle, queue, or
  shutdown internals at the same time.
- ENGINE-23.E should not independently change terminal-state ownership while C/D are still
  shaping active-operation state.
- ENGINE-23.F should not wire bridge dispatch around an unstable operation descriptor.
- ENGINE-17 SQLite runtime completion should not claim scalable/off-owner-thread SQLite
  execution until ENGINE-23.C/E/F behavior is available.
- PostgreSQL and SQL Server JavaScript bridge tasks should not begin from ENGINE-23 unless
  a later issue explicitly scopes them.

## Suggested PR Grouping

- PR 1: ENGINE-23.A plus ENGINE-23.B.
- PR 2: ENGINE-23.C.
- PR 3: ENGINE-23.D.
- PR 4: ENGINE-23.E plus ENGINE-23.F.
- PR 5: ENGINE-23.G plus docs/evidence.
- PR 6: ENGINE-23.H if docs-only.

## Label Note

The requested `area:provider` label does not currently exist in the repository. The created
issues use available labels such as `area:data`, `area:concurrency`, `area:resource`, and
`area:diagnostics` where relevant, plus `priority:p1`, `risk:high`, and
`status:needs-review`.

## Ordering With Adjacent EPICs

Recommended foundation order:

1. ENGINE-12 generic async runtime.
2. ENGINE-23 provider execution/offload runtime.
3. ENGINE-13 proper HTTP runtime backend.
4. ENGINE-17 SQLite runtime/data completion.
5. ENGINE-19 conformance.
6. ENGINE-20 strategic Plan layer.
7. ENGINE-11 public alpha gate.

ENGINE-13 proper HTTP backend consumes ENGINE-12 and may later use provider
pressure/cancellation semantics, but it does not implement provider execution.
ENGINE-17 SQLite runtime completion depends on ENGINE-23 before Slop can claim scalable
SQLite provider execution. Public alpha docs remain blocked until the foundation evidence
is honest and complete or explicitly scoped down.

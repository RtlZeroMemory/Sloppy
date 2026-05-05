# Post-ENGINE-16 Lifecycle and Memory Safety Audit

Status: 2026-05-05 planning/consolidation audit. This records app/resource lifecycle and
memory/string risk after ENGINE-16.

## Source Inputs

- Code: `include/sloppy/app_host.h`, `include/sloppy/resource.h`,
  `include/sloppy/scope.h`, `src/core/app_host.c`, `src/core/resource.c`,
  `src/core/scope.c`, `src/core/async_backend.c`, `src/core/provider_executor.c`,
  provider code, V8 bridge code, tests, and examples.
- Searches included raw allocation/free, C-string formatting/copying, local builders,
  cleanup functions, scope handoff, and TODO/FIXME/deferred/later language.

## Lifecycle Risk Table

| Area | Classification | Evidence | Follow-up |
| --- | --- | --- | --- |
| App lifecycle start/shutdown | Compliant for native helper layer | `SlAppLifecycle` has explicit states, app IDs, startup failure cleanup, drain/force paths, idempotent shutdown. | Production service/resource ownership remains future framework work. |
| Request-scope terminalization | Compliant for native helper layer | ENGINE-16.C terminal outcomes are recorded before cleanup; bare close before terminal is rejected except forced shutdown. | Real async/provider/timer owners still need to retain/release request scopes. |
| Late completions | Compliant for app/request helper layer | Late completion after terminal returns stale lifecycle status and increments counters. | Provider/HTTP/timer adoption should report late completions through common counters/events. |
| Resource table | Compliant | `SlResourceId` has slot/generation only; lookup validates live state and kind; close/typed close cleanup exactly once. | Provider bridge expansion must keep using the engine-owned resource table. |
| Scope ownership | Compliant with documented limits | `SlScope` owns cleanup registrations, not payloads; LIFO close is deterministic; not thread-safe. | Cross-thread scope retention must use `SlAsyncScopeRef` or a future explicit lifecycle hook. |
| App/request snapshot hooks | Partial | Lifecycle/resource snapshots and no-leak assertions exist for tests. Timer/callback/provider counters are reserved zeros. | ENGINE-29 should integrate counters after owners have stable semantics. |
| Provider executor cleanup | Partial / good foundation | Accepted operations clean exactly once; failed admission does not transfer ownership; worker completion posts through async loop. | Caller-serialized contract and shutdown counter updates need tighter source-of-truth before torture tests. |
| SQLite V8 bridge resources | Partial | Safe resource IDs, kind/generation validation, capability metadata beside resource. | Bridge still synchronous and not executor-backed; app/request cleanup ownership policy for handles is incomplete. |
| Raw native pointer exposure | Compliant in audited JS bridge path | JS receives opaque handles; no provider/native pointer is exposed. | Preserve for future provider bridges. |

## Memory/String Findings

| Pattern | Classification | Notes |
| --- | --- | --- |
| V8 C++ `new`/`delete`, `std::string`, `std::vector` | Allowed boundary use | Confined to `src/engine/v8/*`, where C++/V8 ownership is the intended bridge boundary. |
| SQLite `sqlite3_free` and provider handles | Allowed provider boundary use | Provider-specific native library ownership remains local to `src/data/sqlite.c`. |
| PostgreSQL local C-string/copy helpers and numeric `snprintf` | Future tech debt | Already tracked in `docs/tech-debt-tracker.md`; keep libpq boundary adapters local until shared helpers are scoped. |
| SQL Server ODBC copy/redaction/streamed text helpers | Future tech debt | Already tracked; ODBC handle and streaming boundaries can stay local until a scoped helper adoption task. |
| Test-only `strcpy`/C-string helpers | Human review / low priority | Test fixtures should either use shared helpers or document the local boundary. |
| Manual offset/builders in current hot paths | Mostly compliant | HTTP, Plan, diagnostics, V8, and SQLite have adopted shared builders/helpers for current paths. |
| Hidden heap allocation in core C | Not observed as a new broad pattern | C runtime remains mostly arena/caller-storage/resource-table oriented. |

## Torture Harness Prerequisites

ENGINE-30 should not begin until these are stable:

- ENGINE-26 defines execution domains, owner-thread scheduling, terminal-state checks, and
  cancellation/deadline semantics.
- ENGINE-27 defines runtime feature activation so torture lanes can state exactly which
  features are active.
- ENGINE-28 routes SQLite bridge work through the provider executor and defines provider
  result ownership.
- ENGINE-29 adds runtime events/counters so torture failures have observable state without
  benchmark claims.

## Tech-Debt Updates

The tech-debt tracker should continue to carry these as real deferred work:

- SQLite JS bridge through serialized provider executor.
- Provider operation/app-request counter integration.
- PostgreSQL and SQL Server primitive cleanup adoption.
- Test-only C-string boundary helper cleanup.
- Runtime torture after execution/modularity/provider/metrics mature.

This audit does not reopen broad memory/string rewrites.

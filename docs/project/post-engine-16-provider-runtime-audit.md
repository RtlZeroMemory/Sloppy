# Post-ENGINE-16 Provider Runtime Audit

Status: 2026-05-05 planning/consolidation audit. This records current provider/offload
reality after ENGINE-16 and before Engine Roadmap-2.

## Source Inputs

- Code: `include/sloppy/provider_executor.h`, `src/core/provider_executor.c`,
  `src/data/sqlite.c`, `src/data/postgres.c`, `src/data/sqlserver.c`,
  `src/engine/v8/intrinsics_sqlite.cc`, `stdlib/sloppy/data.js`,
  `compiler/src/*`, and provider tests.
- Docs: `docs/modules/data/README.md`, `docs/concurrency.md`,
  `docs/memory.md`, `docs/diagnostics.md`,
  `docs/project/provider-execution-runtime-architecture.md`, and
  `docs/project/provider-runtime-integration-guide.md`.

## Current Provider Model

| Provider/runtime part | Current state | Notes |
| --- | --- | --- |
| Provider executor | Implemented native infrastructure. Supports `SERIALIZED_BLOCKING`, `BLOCKING_POOL`, bounded admission, copied operation metadata/input bytes, capability checks before enqueue, cancellation/timeout/shutdown terminal posting, late-completion counts, and cleanup exactly once. | Worker code never enters V8. Worker-backed submissions require the libuv async backend for thread-safe completion posting. |
| SQLite native provider | Implemented synchronous C provider with caller-owned connection/transaction wrappers, parameter binding, result copying, transactions, and diagnostics. | It has text/blob copy helpers intended for future operation-owned async/offload inputs. |
| SQLite JS bridge | Implemented V8-gated bridge through safe `SlResourceId` handles and capability checks. | The bridge is synchronous: open/exec/query/queryOne/transaction calls run on the V8 callback path and can block the owner thread. |
| PostgreSQL native provider | Implemented synchronous native libpq provider plus redaction, doctor, pool skeleton, and live optional tests. | No JS bridge; no executor adoption; no nonblocking client integration. |
| SQL Server native provider | Implemented synchronous ODBC provider plus redaction, doctor, pool skeleton, and optional live tests. | No JS bridge; no executor adoption; platform availability remains explicit. |
| Stdlib providers | `data.sqlite` can use the installed native bridge; `data.postgres` and `data.sqlserver` validate metadata/options and fail honestly until bridges exist. | This is correct; do not create fake provider bridges. |
| Compiler/Plan metadata | Compiler can emit provider/capability/effect metadata, including provider-kind-aware database effects and honest rejection for generated non-SQLite bridge use. | This is planning/metadata evidence, not native async execution evidence. |

## Exact Adoption Gaps

| Gap | Code reality | Roadmap-2 need |
| --- | --- | --- |
| SQLite blocks V8 today | `intrinsics_sqlite.cc` calls `sl_sqlite_open`, `sl_sqlite_exec`, `sl_sqlite_query`, `sl_sqlite_query_one`, and transaction helpers directly inside V8 callbacks. | ENGINE-28.B should route SQLite bridge operations through a serialized provider executor and resume on the V8 owner thread. |
| Operation descriptor contract not yet provider-specific enough | Generic descriptor copies raw input bytes and metadata, but no SQLite bridge operation envelope is defined. | ENGINE-28.A should define operation payload schemas, ownership, and redaction expectations. |
| Capability checks exist in two places | SQLite bridge checks before direct calls; executor has capability-gated admission. | ENGINE-28.B/C should unify bridge operations around executor admission so capability denial occurs before queueing and worker execution. |
| Cancellation/deadline terminal posting does not interrupt native calls | Executor can terminalize operation state, but provider-specific SQLite interruption, libpq cancellation, and ODBC cancellation are future work. | ENGINE-28.C should define what cancellation means before, during, and after blocking provider calls. |
| Late completion is generic, not integrated with app/request counters | Executor records late-completion counts; app-host provider-operation counters are zero placeholders. | ENGINE-29.D/E should add counters/events after ENGINE-28 semantics settle. |
| Result ownership and V8 resumption are not implemented for real provider results | V8 scheduler exists for test/native continuations; SQLite results are materialized synchronously. | ENGINE-28.D should copy result payloads before worker handoff/resumption and settle only on owner thread. |
| Redaction is local per provider | SQLite avoids params, Postgres/SQL Server redact connection strings, executor redacts hint values. | ENGINE-28.E and ENGINE-29 should centralize provider diagnostic/redaction registry expectations. |
| Provider expansion would be premature | Postgres/SQL Server native code exists but bridge semantics would need the executor, feature registry, capability checks, and result ownership. | Postgres/SQL Server JS bridges should wait until ENGINE-26/27/28 decisions are done. |

## Answered Questions

- SQLite is still synchronous in the JS bridge path.
- SQLite open, exec, query, queryOne, transaction begin/commit/rollback can block the V8
  owner thread today.
- Provider executor operations copy metadata and input bytes before handoff, but the real
  SQLite bridge has not adopted that path.
- Provider capabilities are checked for current SQLite bridge calls and provider executor
  admissions. Route/request/provider-operation policy is not yet one coherent runtime
  feature model.
- SQL text may appear in provider diagnostics, but parameter values and connection
  secrets are not supposed to be printed. Postgres/SQL Server have provider-specific
  redaction helpers; executor admission hints redact unsafe token/context values.

## Recommended Roadmap-2 Work

- ENGINE-28.A: Provider Operation Descriptor Contract.
- ENGINE-28.B: SQLite JS Bridge Through Serialized Provider Executor.
- ENGINE-28.C: Provider Cancellation/Deadline/Backpressure.
- ENGINE-28.D: Provider Result Ownership and V8 Resumption.
- ENGINE-28.E: Provider Diagnostics and Redaction.
- ENGINE-28.F: Provider Runtime Conformance.

Provider Roadmap-2 must complete SQLite executor integration before PostgreSQL or SQL
Server JavaScript bridges are implemented.

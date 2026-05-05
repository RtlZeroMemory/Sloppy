# Roadmap

Status: 2026-05-05 post-ENGINE-16 source-of-truth reset plus Engine Roadmap-2
planning. This is an audit and planning document, not a release promise.

## Current Reality

Core MVP / Slop Engine proof has landed for the scoped development path:

- `sloppyc build` emits deterministic Plan, bundle, and source-map artifacts for the
  supported source subset.
- `sloppy run --artifacts` executes selected V8-gated artifacts.
- V8 runtime execution covers registered handlers, bounded direct Promise/microtask
  settlement, request context, result conversion, and SQLite bridge calls.
- HTTP backend semantics and libuv localhost transport exist for bounded sequential
  HTTP/1.1 keep-alive over localhost, including idle timeout, max requests, lifecycle
  reset, bounded chunked request decoding, and an internal chunked response writer.
  Pipelining, public streaming APIs, SSE/WebSockets/file streaming, and production-edge
  HTTP remain out of scope.
- SQLite users API proof runs through compiler -> artifacts -> localhost TCP -> V8 handler
  -> capability-gated SQLite bridge -> JSON response.
- Provider executor/offload infrastructure exists, but the current SQLite bridge is not
  yet routed through it.
- ENGINE-19 evidence lanes and package outside-checkout smoke exist; default, V8-gated,
  package, live-provider, stress, and benchmark evidence must remain separate.
- ENGINE-15 completes the current source-map/diagnostic renderer wave, and ENGINE-16
  completes the current native app/request/resource lifecycle wave. Roadmap-2 now focuses
  on execution model hardening, runtime feature modularity, provider executor adoption,
  route-level HTTP policy/observability, runtime events/metrics, and later torture tests.

The compact source records for this reset are:

- `docs/project/post-engine-16-execution-model-audit.md`;
- `docs/project/post-engine-16-runtime-modularity-audit.md`;
- `docs/project/post-engine-16-provider-runtime-audit.md`;
- `docs/project/post-engine-16-http-runtime-audit.md`;
- `docs/project/post-engine-16-lifecycle-memory-audit.md`;
- `docs/project/post-engine-16-diagnostics-observability-audit.md`;
- `docs/project/post-engine-16-docs-issue-reconciliation.md`;
- `docs/project/engine-roadmap-2.md`;
- `docs/project/engine-roadmap-2-issue-index.md`;
- historical post-Core records under `docs/project/post-core-mvp-*.md` and
  `docs/project/post-core-next-wave-issue-map.md`;
- `docs/project/framework-app-layer-roadmap.md`;
- `docs/project/framework-api-shape.md`;
- `docs/project/source-input-run-dev-loop-plan.md`;
- `docs/project/strong-plan-strategic-layer-plan.md`;
- `docs/project/compiler-inference-engine-architecture.md`;
- `docs/project/compiler-inference-issue-index.md`;
- `docs/project/http-post-mvp-transport-plan.md`;
- `docs/project/post-core-immediate-hardening-plan.md`.

Durable architecture sources remain:

- `docs/architecture.md`;
- `docs/execution-model.md`;
- `docs/concurrency.md`;
- `docs/memory.md`;
- `docs/diagnostics.md`;
- `docs/data-providers.md`;
- `docs/security-permissions.md`;
- `docs/testing-strategy.md`;
- `docs/quality-gates.md`;
- `docs/project/engine-framework-contract.md`;
- `docs/project/slop-engine-final-shape.md`;
- `docs/project/slop-engine-layered-roadmap.md`;
- `docs/project/engine-19-conformance-matrix.md`.

## Status Key

- Complete/proven: implemented and covered by checked-in tests or explicit optional gates.
- Partial: useful implementation exists, but important behavior or evidence remains scoped
  out.
- Missing: not implemented.
- Blocked: depends on a missing decision or prerequisite.
- Deferred: intentionally later and not part of the current claim.

## Core MVP Status

| Area | Status | Current boundary |
| --- | --- | --- |
| Compiler -> Plan/artifacts | Complete/proven | Supported subset only; COMPILER-30.A adds module/library/test-harness foundation, not full TypeScript checking, package resolution, or broad module/service/schema/effect/capability inference. |
| V8 runtime execution | Complete/proven for scoped path | Optional V8 SDK lane; default gates do not prove V8. |
| Async semantics | Partial | Direct returned Promises that settle during bounded owner-thread microtask drain are supported; timers/fetch/arbitrary native async sources are missing. |
| HTTP backend | Complete/proven for MVP | Sequential keep-alive, bounded chunked request decoding, and internal chunked response writer only; no pipelining, public streaming APIs, SSE/WebSockets/file streaming, production HTTP, TLS, HTTP/2/3, middleware, or benchmark claims. |
| Libuv localhost transport | Complete/proven for MVP | Bounded localhost transport with sequential HTTP/1.1 keep-alive, idle timeout, max requests, chunked request decoding, internal chunked response writer, and close policy. |
| SQLite users API | Complete/proven for proof fixture | Current bridge is synchronous and not provider-executor-backed. |
| Capability enforcement | Complete/proven for integrated paths | SQLite/provider-executor paths enforce before provider work; CORE-FS-01.A/B defines filesystem feature/capability policy and import-driven Plan metadata, but filesystem runtime enforcement lands in later CORE-FS slices; network remains metadata/check skeleton. |
| Provider executor | Partial | Native executor exists; provider bridge adoption remains future work. |
| Package smoke | Partial | Local experimental package evidence, not release readiness or package-manager compatibility. |
| Source-input run | Partial/proven for compiler-owned module subset | `sloppy run <source.js>` and `sloppy run` via `sloppy.json` compile through `sloppyc`, validate artifacts, and reuse `--artifacts`; supported relative function modules and Sloppy provider imports are compiler-owned. Cache reuse, watch/hot reload, Node/npm, and full TypeScript remain deferred. |
| Framework configuration | Partial/proven for first slice | Built-in defaults, appsettings overlays, environment selection, canonical env vars, selected CLI overrides, typed access, `bind`, config-driven SQLite provider metadata, redacted Plan metadata, and first Plan-driven doctor/audit consumption exist. Reload, user secrets, custom/remote providers, broad CLI config, and OpenAPI config consumption remain deferred. |
| Public alpha | Blocked | Needs canonical docs, executable examples, broader ergonomics, package/platform story, and no fake claims. |

## Active Issue Map

Closed during this reset or immediately before it:

- ENGINE-13 HTTP backend parent;
- ENGINE-14 module/bootstrap runtime parent;
- ENGINE-15 diagnostics/source-map parent;
- ENGINE-16 app/resource lifetime parent;
- ENGINE-17 SQLite runtime parent;
- ENGINE-19 conformance/package evidence tasks;
- ENGINE-24 HTTP transport parent;
- HTTP-25 keep-alive/chunked/internal streaming parent;
- HARDEN-01 post-Core foundation hardening parent;
- COMPILER-30 compiler inference parent/tasks;
- legacy ENGINE-08 and ENGINE-09 diagnostic/example parents now covered by completed
  ENGINE-15/16 and framework/example evidence.

Kept open intentionally:

- #259 compiler/source-input parent remains open; #302/#346 are closed for the current
  rebuild-always JavaScript shortcut, leaving TypeScript/module/cache reuse follow-ups;
- #316 CLI/dev loop parent, with #345 and #349 still open for artifact inspection,
  watch/dev-loop, and command-diagnostic cleanup;
- #318/#355-#359 Strong Plan strategic layer;
- #268/#300/#301 public alpha readiness and non-claims review.

Previously created for the owner-approved post-Core next wave, now completed or kept as
historical evidence where closed:

- #432 FRAMEWORK-01 framework/app-layer parent, with #437-#439 still open for request
  binding, validation/error responses, and response model completion;
- #433/#441-#446 HTTP-25 HTTP/1.1 keep-alive, chunked request decoding, internal
  streaming response writing, and bounded stress/conformance evidence;
- #434/#447/#448 HARDEN-01 post-Core foundation hardening, with #431 and #26 reused for
  SQLite preflight and platform scanner proof.

Created for Engine Roadmap-2:

- ENGINE-26 Execution Model Hardening;
- ENGINE-27 Runtime Feature Modularity;
- ENGINE-28 Provider Runtime Maturation;
- HTTP-26 Route-Level HTTP Policy and Observability;
- ENGINE-29 Runtime Events and Metrics;
- ENGINE-30 Runtime Torture and Crash-Resistance Harness.

The exact issue-number map lives in `docs/project/engine-roadmap-2-issue-index.md`.

## Next Recommended Tracks

The next engine wave is `docs/project/engine-roadmap-2.md`. In order:

1. ENGINE-26 execution model hardening.
2. ENGINE-27 runtime feature modularity.
3. ENGINE-28 provider runtime maturation, including SQLite executor-backed bridge work.
4. HTTP-26 route-level HTTP policy and observability.
5. ENGINE-29 runtime events and metrics.
6. ENGINE-30 runtime torture and crash-resistance harness.

Provider expansion remains after provider runtime maturation. Torture tests come after
execution/modularity/provider/metrics foundations are mature. Public alpha and benchmark
claims remain blocked.

## Deferred By Design

- Node/npm/package-manager compatibility.
- Public alpha docs and tutorials.
- Public performance or benchmark comparison claims.
- Production-edge HTTP claims.
- PostgreSQL/SQL Server JavaScript bridges.
- ORM/migrations.
- TLS, HTTP/2, HTTP/3, WebSockets, pipelining, public streaming APIs, SSE/file streaming,
  reverse-proxy behavior, static files, compression, and production hardening unless a
  future scoped issue says so.

## Cleanup Policy

Temporary planning docs are allowed during fast build phases, but each phase should end
with compaction: merge durable facts into canonical docs, archive useful history, delete
obsolete "current state" docs, reconcile GitHub issues with evidence, and keep roadmap
claims tied to code/tests/examples.

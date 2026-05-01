# Roadmap

Status: 2026-05-01 post-core source-of-truth reset plus owner-approved next-wave
planning. This is an audit and planning document, not a release promise.

## Current Reality

Core MVP / Slop Engine proof has landed for the scoped development path:

- `sloppyc build` emits deterministic Plan, bundle, and source-map artifacts for the
  supported source subset.
- `sloppy run --artifacts` executes selected V8-gated artifacts.
- V8 runtime execution covers registered handlers, bounded direct Promise/microtask
  settlement, request context, result conversion, and SQLite bridge calls.
- HTTP backend semantics and libuv localhost transport exist for the one-request-per-
  connection, close-after-response MVP.
- SQLite users API proof runs through compiler -> artifacts -> localhost TCP -> V8 handler
  -> capability-gated SQLite bridge -> JSON response.
- Provider executor/offload infrastructure exists, but the current SQLite bridge is not
  yet routed through it.
- ENGINE-19 evidence lanes and package outside-checkout smoke exist; default, V8-gated,
  package, live-provider, stress, and benchmark evidence must remain separate.

The compact source records for this reset are:

- `docs/project/post-core-mvp-code-reality-audit.md`;
- `docs/project/post-core-mvp-docs-inventory.md`;
- `docs/project/post-core-mvp-issue-reconciliation.md`;
- `docs/project/post-core-mvp-memory-string-audit.md`;
- `docs/project/post-core-mvp-boundary-audit.md`;
- `docs/project/post-core-mvp-next-roadmap.md`;
- `docs/project/post-core-next-wave-issue-map.md`;
- `docs/project/framework-app-layer-roadmap.md`;
- `docs/project/framework-api-shape.md`;
- `docs/project/source-input-run-dev-loop-plan.md`;
- `docs/project/strong-plan-strategic-layer-plan.md`;
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
| Compiler -> Plan/artifacts | Complete/proven | Supported subset only; no full TypeScript checking, package resolution, or broad module/service/schema extraction. |
| V8 runtime execution | Complete/proven for scoped path | Optional V8 SDK lane; default gates do not prove V8. |
| Async semantics | Partial | Direct returned Promises that settle during bounded owner-thread microtask drain are supported; timers/fetch/arbitrary native async sources are missing. |
| HTTP backend | Complete/proven for MVP | No production HTTP, keep-alive, streaming, TLS, HTTP/2/3, WebSockets, middleware, or benchmark claims. |
| Libuv localhost transport | Complete/proven for MVP | One request per connection, close-after-response, bounded localhost transport only. |
| SQLite users API | Complete/proven for proof fixture | Current bridge is synchronous and not provider-executor-backed. |
| Capability enforcement | Complete/proven for integrated paths | SQLite/provider-executor paths enforce before provider work; filesystem/network remain metadata/check skeletons. |
| Provider executor | Partial | Native executor exists; provider bridge adoption remains future work. |
| Package smoke | Partial | Local experimental package evidence, not release readiness or package-manager compatibility. |
| Source-input run | Partial/proven for compiler-owned module subset | `sloppy run <source.js>` and `sloppy run` via `sloppy.json` compile through `sloppyc`, validate artifacts, and reuse `--artifacts`; supported relative function modules and Sloppy provider imports are compiler-owned. Cache reuse, watch/hot reload, Node/npm, and full TypeScript remain deferred. |
| Framework configuration | Partial/proven for first slice | Built-in defaults, appsettings overlays, environment selection, canonical env vars, selected CLI overrides, typed access, `bind`, config-driven SQLite provider metadata, and redacted Plan metadata exist. Reload, user secrets, custom/remote providers, broad CLI config, and doctor/OpenAPI consumption remain deferred. |
| Public alpha | Blocked | Needs canonical docs, executable examples, broader ergonomics, package/platform story, and no fake claims. |

## Active Issue Map

Closed during this reset or immediately before it:

- ENGINE-13 HTTP backend parent;
- ENGINE-17 SQLite runtime parent;
- ENGINE-19 conformance/package evidence tasks;
- ENGINE-24 HTTP transport parent.

Kept open intentionally:

- #259/#302 compiler/source-input handoff, with ENGINE-02.E covering the current
  rebuild-always JavaScript shortcut and leaving TypeScript/module/cache reuse follow-ups;
- #312/#325-#329 module/bootstrap runtime completion;
- #313/#330-#334 source maps and diagnostics;
- #314/#335-#339 app/resource lifetime runtime;
- #316/#345-#349 CLI/dev loop runtime;
- #318/#355-#359 Strong Plan strategic layer;
- #265/#295 diagnostics/async diagnostic work;
- #266/#296/#297 examples and docs reality work;
- #268/#300/#301 public alpha readiness and non-claims review;
- #26 platform scanner fixture/self-test proof.

Created for the owner-approved post-Core next wave:

- #432/#435-#440 FRAMEWORK-01 framework/app-layer source-of-truth and ergonomics;
- #433/#441-#446 HTTP-25 HTTP/1.1 keep-alive and streaming;
- #434/#447/#448 HARDEN-01 post-Core foundation hardening, with #431 and #26 reused for
  SQLite preflight and platform scanner proof.

## Next Recommended Tracks

The owner-approved next-wave map is `docs/project/post-core-next-wave-issue-map.md`. In
short:

1. HARDEN-01 small boundary/safety cleanup if selected.
2. Source-input follow-ups through reused #259/#302 and #316/#345-#349: TypeScript/module
   completion, cache reuse, and watch/dev-loop decisions.
3. Strong Plan typed graph through reused #318/#355-#359.
4. Framework config, binding, validation, Results, and examples through #432/#435-#440.
5. Plan-driven doctor/OpenAPI after Plan metadata is real.
6. HTTP keep-alive/streaming through #433/#441-#446 after MVP transport boundary cleanup.
7. Provider expansion later after SQLite/provider-executor integration is proven.

## Deferred By Design

- Node/npm/package-manager compatibility.
- Public alpha docs and tutorials.
- Public performance or benchmark comparison claims.
- Production-edge HTTP claims.
- PostgreSQL/SQL Server JavaScript bridges.
- ORM/migrations.
- TLS, HTTP/2, HTTP/3, WebSockets, keep-alive, streaming, reverse-proxy behavior, static
  files, compression, and production hardening unless a future scoped issue says so.

## Cleanup Policy

Temporary planning docs are allowed during fast build phases, but each phase should end
with compaction: merge durable facts into canonical docs, archive useful history, delete
obsolete "current state" docs, reconcile GitHub issues with evidence, and keep roadmap
claims tied to code/tests/examples.

# Post-Core MVP Code Reality Audit

Status: 2026-05-01 consolidation audit. This report is compact evidence, not a new
architecture source.

## Now Real / Proven By Tests

| Area | Current reality | Evidence |
| --- | --- | --- |
| Compiler to artifacts | `sloppyc build` emits deterministic `app.plan.json`, `app.js`, and source maps for the supported one-file app subset. | `compiler/src/sloppyc.rs`, compiler fixtures, conformance compile/reject checks. |
| V8 runtime execution | V8-enabled builds execute generated/classic bootstrap code, registered handlers, direct async handlers that settle during bounded microtask drain, and SQLite bridge calls. | `src/engine/v8/*`, `tests/unit/engine/test_v8_smoke.c`, V8-gated conformance entries. |
| HTTP backend semantics | Native route dispatch, response conversion, body/header policy, shutdown/cancellation terminal paths, bounded stress smoke, and diagnostics exist. | `src/core/http*.c`, `tests/unit/core/test_http*.c`, `tests/integration/http_dispatch/*`. |
| Localhost HTTP transport | Libuv-backed bind/listen/accept/read/dispatch/write/close exists for the one-request-per-connection MVP. | `src/platform/libuv/http_transport_libuv.c`, `tests/unit/core/test_http_transport.cc`, transport conformance. |
| SQLite users API proof | Source-built SQLite users API runs through `sloppyc build` -> `sloppy run --artifacts` -> localhost TCP -> V8 handler -> capability-gated SQLite bridge -> JSON response. | `examples/users-api-sqlite/*`, `tests/scripts/test_users_api_sqlite_transport.ps1`. |
| Capability enforcement | Native capability registry, provider-executor admission, and V8 SQLite bridge checks deny before provider work where integrated. | `src/core/capability.c`, `src/core/provider_executor.c`, `src/engine/v8/intrinsics_sqlite.cc`. |
| Provider executor | Per-provider-instance descriptors, bounded admission, serialized workers, blocking pools, cancellation/timeout/shutdown terminal paths, and diagnostics exist as native infrastructure. | `include/sloppy/provider_executor.h`, `src/core/provider_executor.c`, `tests/unit/core/test_provider_executor.c`. |
| Package smoke | Local experimental packages verify extracted layout, CLI startup, stdlib assets, packaged compiler smoke, and honest non-V8 execution skips. | `tools/windows/test-package.ps1`, package conformance docs. |

## Still Synthetic / Partial

| Area | Boundary |
| --- | --- |
| Source-input run | `sloppy run app.js` is still deferred; the supported path is explicit `sloppyc build` plus `sloppy run --artifacts`. |
| HTTP/1.1 depth | Keep-alive, pipelining, chunked request decoding, streaming response writing, TLS, HTTP/2/3, WebSockets, middleware, and production-edge behavior are not implemented. |
| SQLite provider execution | The current V8 SQLite bridge is synchronous and single-thread-owned; it is not yet routed through the ENGINE-23 provider executor. |
| Async JavaScript | Direct returned Promises are bounded by owner-thread microtask drain; timers, fetch, arbitrary native async sources, and broad async source syntax are deferred. |
| PostgreSQL/SQL Server JS bridges | Native providers exist; JavaScript-to-native bridges remain deferred and should not be implied by SQLite proof. |
| Public alpha | Conformance and package smoke are evidence lanes, not public release readiness. |

## Stale Comments / Docs Found And Fixed

- README wording changed from `sloppyc` placeholder to supported-subset compiler.
- README SQLite capability status now says V8 bridge open/use checks exist, while async/offload remains deferred.
- `src/engine/v8/engine_v8.cc` microtask-limit hint now names remaining deferred async sources instead of old ENGINE-12 queue wording.
- `include/sloppy/http_transport.h` stopped calling accepted connections placeholders.
- `tests/README.md` now reflects current unit/integration/conformance/package/compiler coverage.

## Stale Comments / Docs Requiring Follow-Up

- Public docs still deserve a targeted post-core executable-example pass before any public alpha work.
- `docs/project/archive/post-core-mvp/*` is historical; do not treat archived "current state" language as current source of truth.
- Some CTest names still carry MVP wording; keep them until behavior changes, but avoid turning names into product claims.

## Risky Implementation Areas

- HTTP transport lifecycle around uv close callbacks, timers, write callbacks, stop/shutdown, and cleanup-once terminal paths.
- V8 owner-thread enforcement and bounded Promise drain diagnostics.
- Provider executor terminal states: post failure, late completion, shutdown while worker-owned, and cleanup-once behavior.
- SQLite bridge capability ordering and synchronous owner-thread work.
- Package smoke wording: local experimental packaging is not release readiness.

## Human-Review Items

- Decide when SQLite bridge conversion through provider executors blocks stronger alpha claims.
- Decide whether `src/main.c` direct libuv dev-run code remains acceptable as a temporary CLI boundary or should move behind the reusable transport wrapper in a narrow follow-up.
- Decide when source-input run becomes the next owner-approved EPIC.

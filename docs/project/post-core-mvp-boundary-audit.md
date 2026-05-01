# Post-Core MVP Boundary Audit

Status: 2026-05-01 consolidation audit.

| Boundary | Status | Evidence / Notes |
| --- | --- | --- |
| No V8 types outside `src/engine/v8/*` | Pass | Search found no `v8::` or V8 header leakage outside the V8 island. Public engine ABI remains C/opaque. |
| No libuv types in public API | Mixed | New HTTP transport public API hides libuv, but legacy `sl_http_libuv_smoke` remains public and `src/main.c` still uses `uv_loop_t`/`uv_tcp_t` in the dev-only run path. Track as debt; no broad rewrite in this PR. |
| Libuv transport details internal/private | Mostly pass | `src/platform/libuv/http_transport_libuv.c` owns bind/listen/read/write and public transport headers expose Slop-owned structs. |
| No raw native pointers exposed to JS | Pass | SQLite bridge exposes resource IDs/slot-generation validation, not raw pointers. |
| No provider work bypassing capability checks | Pass for integrated paths | V8 SQLite open/use checks and provider-executor admission checks happen before provider work. Direct low-level native provider APIs remain primitives without capability context by design. |
| No provider worker enters V8 | Pass | Provider executor contracts and implementation keep worker callbacks native-only and complete through Slop async paths. |
| No hidden unbounded HTTP/provider queues | Mostly pass | HTTP transport/backend and provider executors use bounded capacities. SQLite V8 parameter-array reserve needs a preflight cap before allocation. |
| No public alpha/performance/production HTTP claims | Pass | Current docs keep public alpha blocked, benchmark claims deferred, and HTTP described as dev/MVP evidence. |
| PostgreSQL/SQL Server JS bridges deferred | Pass | Docs and stdlib fail honestly for JS bridges; native providers and live optional tests are separate evidence. |

## Fixes Made

- Stale comments/docs were narrowed for V8 async hints, HTTP transport accepted
  connections, README status, and tests overview.

## Follow-Up Tech Debt

- Hide or retire `sl_http_libuv_smoke` behind platform/internal test boundaries.
- Move remaining dev-only libuv socket loop in `src/main.c` behind the reusable transport
  boundary or an internal CLI adapter.
- Add SQLite V8 parameter-count allocation preflight.

## Human-Review Items

- Decide whether the libuv smoke helper is still needed after ENGINE-24 transport tests.
- Decide whether the source-input run/dev loop should be the slice that removes direct CLI
  libuv usage.

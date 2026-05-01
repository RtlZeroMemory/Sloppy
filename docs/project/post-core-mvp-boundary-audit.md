# Post-Core MVP Boundary Audit

Status: 2026-05-01 consolidation audit.

| Boundary | Status | Evidence / Notes |
| --- | --- | --- |
| No V8 types outside `src/engine/v8/*` | Pass | Search found no `v8::` or V8 header leakage outside the V8 island. Public engine ABI remains C/opaque. |
| No libuv types in public API | Pass | HTTP public APIs expose Slop-owned structs and opaque platform pointers. The legacy public `sl_http_libuv_smoke` helper was retired, and `src/main.c` now enters server mode through `SlHttpTransportServer` instead of direct `uv_loop_t`/`uv_tcp_t` usage. |
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
- HARDEN-01.A retired public `sl_http_libuv_smoke` and moved the dev-only CLI server path
  behind the reusable HTTP transport boundary without changing HTTP behavior.

## Follow-Up Tech Debt

- Add SQLite V8 parameter-count allocation preflight; tracked by #431.

## Human-Review Items

- Decide whether the source-input run/dev loop is the next owner-approved EPIC; it remains
  separate from HTTP transport boundary cleanup.

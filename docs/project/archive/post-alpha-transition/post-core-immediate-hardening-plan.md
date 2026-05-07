# Post-Core Immediate Hardening Plan

Status: planning source of truth for #434 and nearby reused hardening issues.

These are small foundation hardening items that can happen before or alongside the
framework wave. They should stay bounded and must not become feature implementation PRs.

## Hardening Items

| Item | Issue | Scope |
| --- | --- | --- |
| HTTP transport boundary cleanup | #447 | HARDEN-01.A retires public `sl_http_libuv_smoke` and routes `src/main.c` server mode through the reusable HTTP transport boundary; keep future work out of this cleanup slice. |
| SQLite V8 parameter preflight | #431 | Preflight JS array length before native vector reserve in the SQLite V8 bridge; reject arrays above the bridge cap deterministically. |
| Provider primitive cleanup plan | #448 | Inventory PostgreSQL/SQL Server provider string/buffer cleanup candidates, record precise helper moves, and leave implementation to scoped follow-up PRs. |
| Platform scanner fixture/self-test proof | #26 | Completed by scanner self-tests that create temporary positive and allowed-boundary fixtures before the repository scan. |

## Boundaries

- No runtime feature implementation beyond the named cleanup item.
- No HTTP keep-alive, chunked decoding, streaming, TLS, HTTP/2, HTTP/3, or WebSockets.
- No provider expansion or PostgreSQL/SQL Server JS bridge work.
- No public alpha docs.
- No benchmark/performance claims.

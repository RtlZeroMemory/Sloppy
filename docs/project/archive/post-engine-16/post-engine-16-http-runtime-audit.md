# Post-ENGINE-16 HTTP Runtime Audit

Status: 2026-05-05 planning/consolidation audit. This records HTTP runtime maturity after
HTTP-25 and before Engine Roadmap-2.

## Source Inputs

- Code: `include/sloppy/http*.h`, `src/core/http*.c`,
  `src/platform/libuv/http_transport_libuv.c`, `src/engine/v8/http_bridge.cc`,
  current HTTP tests and transport conformance registrations.
- Docs: `docs/modules/http/README.md`, `docs/execution-model.md`,
  `docs/concurrency.md`, `docs/diagnostics.md`,
  `docs/project/archive/http/http-post-mvp-transport-plan.md`, and `docs/testing-strategy.md`.

## Current Maturity Table

| Area | Status | Evidence | Missing / risky |
| --- | --- | --- | --- |
| Keep-alive | Complete for bounded sequential HTTP/1.1 localhost transport | HTTP-25.A/B/C added sequential reuse, idle timeout, max requests, close policy, lifecycle reset, and pipelining rejection. | No concurrent requests on one connection, production keep-alive tuning, or reverse-proxy behavior. |
| Chunked requests | Complete for bounded full-body request decoding | HTTP-25.D/E decodes current no-extension chunked subset into existing bounded body storage and rejects trailers. | No public request streaming API and no streaming parser surface. |
| Streaming responses | Partial / internal only | Internal/native chunked response writer writes bounded chunks and final zero chunk. | No public `Results.stream`, SSE, file streaming, WebSockets, or framework response-stream API. |
| Lifecycle reset | Complete for current keep-alive path | Transport resets request arena/body/response state before next sequential request. | Needs more race/torture coverage around late callbacks and shutdown timing. |
| Timeouts | Partial | Header/body/request/write/idle timers exist and terminalize or close; 408 response attempted when safe. | Route-level timeout policy from Plan/config is missing. |
| Backpressure | Partial | Connection/request caps, body limits, pending write cap, and deterministic pipelining rejection exist. | Route-level limits, queue policy, and observability for rejections are missing. |
| Shutdown/drain | Partial | Stop rejects new work and immediate-cancel/drain-lite closes active connections. | Production graceful drain remains explicitly out of scope. |
| Error responses | Partial | Safe 400/404/405/408/413/415/500 style paths exist in current runtime. | Error response registry/consistency across route-level policy and framework validation is not complete. |
| Request binding | Partial | V8 request context exposes route/query/header/text/json for current supported path. | Typed route/query/header/body binding and validation problem responses remain framework follow-ups. |
| Route-level policy | Missing | Plan and config metadata exist but do not drive HTTP route limits/timeouts. | HTTP-26 should define policy from Plan/config. |
| Observability | Partial | Stable diagnostics and selected transport counters exist. | No request ID/correlation context, access log/event model, or runtime counter registry. |
| Proxy/TLS/H2/WebSocket boundaries | Missing / deferred | Docs explicitly defer these production-edge areas. | Trusted proxy/forwarded headers policy should be specified before proxy deployment claims. |

## Key Questions Answered

- Application-server essentials remaining: route-level limits/timeouts, request IDs,
  access events/logs, trusted proxy policy, consistent error envelope policy, graceful
  drain policy, production hardening, and public streaming APIs.
- Request ID/correlation context is not a general runtime feature today.
- Route-level limits/timeouts from Plan/config are missing.
- Forwarded headers/trusted proxy policy is missing.
- HTTP error responses exist in multiple paths but are not governed by one consistency
  policy.
- Access logs/runtime events are not available as a stable model.
- Transport state is no longer MVP close-after-response, but production drain,
  observability, proxy, and public streaming remain intentionally immature.

## Recommended Roadmap-2 Work

- HTTP-26.A: Route-Level Limits and Timeout Policy From Plan/Config.
- HTTP-26.B: Request ID and Correlation Context.
- HTTP-26.C: Access Log/Event Model.
- HTTP-26.D: Trusted Proxy and Forwarded Headers Policy.
- HTTP-26.E: HTTP Error Response Consistency.
- HTTP-26.F: HTTP Policy Conformance.

HTTP-26 can start after ENGINE-26 execution-domain decisions. It can run in parallel with
ENGINE-28 if file ownership is kept separate.

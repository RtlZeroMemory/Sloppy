# HTTP Client API Issue Index

Parent epic: #583 CORE-HTTPCLIENT-01.

| Issue | Slice | Status |
| --- | --- | --- |
| #601 | API contract, security policy, and non-goals | Implemented by the contract, feature id, compiler activation, fail-closed stdlib facade, and architecture doc. |
| #602 | TLS backend and certificate validation policy | Documented no-custom-TLS/no-custom-validation policy; TLS diagnostics reserved. |
| #603 | Feature, Plan, capability, and diagnostics model | Implemented `stdlib.httpclient`, compiler import activation, and stable diagnostic names. |
| #604 | HTTP/1.1 client request/response core | Implemented for cleartext `http://` request/response execution over CORE-NET TCP; TLS, pooling, redirects, streaming, and full deadline/cancellation behavior remain deferred to sibling issues. |
| #605 | Convenience APIs and JSON/text/bytes helpers | Implemented for `get`, `post`, `request`, `text`, `json`, `bytes`, `getJson`, `postJson`, response `text()`, `json()`, `bytes()`, request `json` bodies, and base URL/path joining. |
| #606 | Streaming bodies, backpressure, deadlines, and cancellation | Implemented for bounded async-iterable request body consumption, response `stream()` iteration over buffered bodies, operation-wide `timeoutMs`/`deadline`/`signal`, timeout/cancellation distinction, and cleanup-only late completion. True socket-level streaming remains a future transport evolution. |
| #607 | Pooling, redirects, DNS, and header redaction | Implemented for per-origin HTTP/1.1 pooling, deterministic pool exhaustion, stale-idle reconnect, bounded redirects, redirect-loop/max diagnostics, DNS failure mapping, strict-network denial, and cross-origin sensitive-header strip/deny defaults. |
| #608 | V8/stdlib integration and JS surface | Partially implemented by the stdlib surface over the existing CORE-NET V8 bridge; dedicated HTTP-native bridge behavior remains deferred. |
| #609 | Doctor/audit, conformance, examples, docs, and goldens | Implemented for HTTP client doctor/audit metadata goldens, source-shape example checks, conformance indexing, diagnostic fixture indexing, and docs reality updates. |

The parent epic remains open until every child issue is fully implemented and tested.

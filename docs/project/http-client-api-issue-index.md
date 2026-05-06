# HTTP Client API Issue Index

Parent epic: #583 CORE-HTTPCLIENT-01.

| Issue | Slice | Status |
| --- | --- | --- |
| #601 | API contract, security policy, and non-goals | Implemented by the contract, feature id, compiler activation, fail-closed stdlib facade, and architecture doc. |
| #602 | TLS backend and certificate validation policy | Documented no-custom-TLS/no-custom-validation policy; TLS diagnostics reserved. |
| #603 | Feature, Plan, capability, and diagnostics model | Implemented `stdlib.httpclient`, compiler import activation, and stable diagnostic names. |
| #604 | HTTP/1.1 client request/response core | Partially implemented for cleartext `http://` request/response execution over CORE-NET TCP; TLS, pooling, redirects, streaming, and full deadline/cancellation behavior remain deferred. |
| #605 | Convenience APIs and JSON/text/bytes helpers | Partially implemented for `get`, `post`, `request`, response `text()`, `json()`, and `bytes()`; request `json` body helpers and `getJson`/`postJson` remain deferred. |
| #606 | Streaming bodies, backpressure, deadlines, and cancellation | Deferred. |
| #607 | Pooling, redirects, DNS, and header redaction | Deferred. |
| #608 | V8/stdlib integration and JS surface | Partially implemented by the stdlib surface over the existing CORE-NET V8 bridge; dedicated HTTP-native bridge behavior remains deferred. |
| #609 | Doctor/audit, conformance, examples, docs, and goldens | Deferred except for contract documentation and the first local stdlib loopback conformance test. |

The parent epic remains open until every child issue is fully implemented and tested.

# HTTP Client API Issue Index

Parent epic: #583 CORE-HTTPCLIENT-01.

| Issue | Slice | Status |
| --- | --- | --- |
| #601 | API contract, security policy, and non-goals | This PR defines the contract, feature id, compiler activation, fail-closed stdlib facade, and architecture doc. |
| #602 | TLS backend and certificate validation policy | This PR documents the no-custom-TLS/no-custom-validation policy and reserves TLS diagnostics. |
| #603 | Feature, Plan, capability, and diagnostics model | This PR adds `stdlib.httpclient`, compiler import activation, and stable diagnostic names. |
| #604 | HTTP/1.1 client request/response core | Deferred. |
| #605 | Convenience APIs and JSON/text/bytes helpers | Deferred beyond the fail-closed facade. |
| #606 | Streaming bodies, backpressure, deadlines, and cancellation | Deferred. |
| #607 | Pooling, redirects, DNS, and header redaction | Deferred. |
| #608 | V8/stdlib integration and JS surface | Partially started with a fail-closed bootstrap surface; native bridge is deferred. |
| #609 | Doctor/audit, conformance, examples, docs, and goldens | Deferred except for this contract documentation. |

The parent epic remains open until every child issue is fully implemented and tested.

# Cross-API Conformance Foundation

This directory indexes cross-API conformance scenarios. The goal is to validate shared
runtime primitives and denial/error semantics across implemented APIs, not to implement
missing APIs merely to make a matrix green.

| Scenario | Current evidence | Status |
| --- | --- | --- |
| FS + Time + Codec | Native filesystem, Time, and Codec contract tests plus doctor/audit goldens | DEFERRED for end-to-end source app until common JS execution path is V8-ready |
| Network + Time + Codec | Localhost TCP client/listener, Time, and Codec bootstrap tests | PARTIAL |
| HTTP client + Time + Codec + Crypto/redaction | HTTP client bootstrap, timeout/cancel checks, Codec/Crypto redaction docs and goldens | PARTIAL |
| Process + Time + Codec | OS process facade tests and Time/Codec bootstrap checks | PARTIAL |
| Workers + Time | Worker API metadata and Time owner-thread tests | PARTIAL |
| Workers + Crypto/Codec | Worker plus Crypto/Codec bootstrap tests | DEFERRED until worker bridge coverage expands |
| Config + internal FS + OS env + Crypto secrets + Time duration | Config compiler/source-input tests, OS/env docs, crypto secret redaction helpers | PARTIAL |
| Strict policy shared denial style | Capability/provider/filesystem/network/HTTP diagnostics and goldens | PARTIAL |
| Runtime bootstrap artifact loading independent from app FS policy | `runtime-artifact;filesystem-boundary` tests and package outside-checkout smoke | IMPLEMENTED FOUNDATION |
| Timeout/cancel distinction across APIs | Async/backend/HTTP/Codec/Time cancellation tests | PARTIAL |
| Embedded NUL/binary correctness across stream APIs | Network, Codec Binary, HTTP parser, and fuzz seed replay evidence | PARTIAL |

Deferred rows must remain issue-backed. Missing or incomplete APIs should be reported as
DEFERRED or UNSUPPORTED with a reason, not implemented inside a test-platform PR unless the
change is a narrow test-only hook.

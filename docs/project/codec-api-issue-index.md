# CORE-CODEC-01 Issue Index

Parent: #621 CORE-CODEC-01.

| Issue | Slice | Status |
| --- | --- | --- |
| #622 | API Contract and Backend Policy | PR 1 locks `sloppy/codec` API, backend/dependency policy, and non-goals. |
| #623 | Feature, Plan, Diagnostics, and Safety Model | PR 1 adds `stdlib.codec`, Plan/compiler activation, diagnostic code reservations, and safety policy. |
| #624 | Base64, Base64Url, and Hex APIs | PR 2 implements encoding primitives and vectors. |
| #625 | Text Encoding, Decoding, and Streaming Decoder | PR 2 implements UTF-8 encode/decode and streaming decoder semantics. |
| #626 | Binary Reader and Writer APIs | PR 3 implements bounds-checked endian-explicit binary APIs. |
| #627 | Compression and Decompression APIs | PR 4 implements vetted compression helpers and limits. |
| #628 | Streaming Compression, Backpressure, Deadline, and Cancellation | PR 4 implements streaming transform semantics and cleanup rules. |
| #629 | Checksums and Non-Security Utilities | PR 5 implements CRC32 and non-security docs/warnings. |
| #630 | V8/Stdlib Integration and JS Surface | PR 2 starts the JS/V8 surface and later PRs extend it where needed. |
| #631 | Conformance, Test Vectors, Examples, Docs, and Goldens | PR 5 completes conformance matrices, examples, and final evidence. |

## Cross-API Boundary

`sloppy/codec` is independent from CORE-CRYPTO-01 and CORE-NET-01. This EPIC does not
replace Crypto helper formatting, integrate into Network/Filesystem/Process/HTTP/Workers,
or close unrelated issues. Cross-API adoption belongs to CORE-INTEGRATION-01.

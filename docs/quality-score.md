# Quality Score

Status: pre-alpha quality snapshot.

## Status Key

- Green: implemented and covered by default or clearly named evidence.
- Yellow: implemented or specified with meaningful gaps.
- Red: not ready for public alpha use.

Default gates prove the non-V8 path unless a V8-enabled lane is explicitly run. Optional
V8, package, live-provider, advanced static analysis, fuzz/property, stress, torture,
sanitizer, and benchmark evidence must be reported separately.

| Area | Status | Current Evidence | Remaining Gap |
| --- | --- | --- | --- |
| Native C safety | Yellow | Core primitives, arenas, builders, diagnostics, Plan parser, HTTP/backend/transport paths, provider boundaries, lifecycle cleanup, and standards scanners. | Broader sanitizer/fuzz coverage and continued provider/runtime cleanup adoption. |
| Memory and ownership | Yellow | Borrowed views, arenas, builders, interned metadata, resource IDs, request/app scopes, async completion ownership, V8/native copies, and SQLite text/blob helpers. | Broader hot-path adoption, allocator/sanitizer lanes, and executor-backed provider ownership. |
| Diagnostics | Yellow | Stable diagnostic codes, text/JSON/source-frame renderers, redaction helpers, compiler/runtime/provider/HTTP diagnostics, V8-gated source-map remapping, and diagnostic goldens. | CLI-wide diagnostic format plumbing, richer structured fixes/categories, localization, and broader optional-lane coverage. |
| Platform boundaries | Yellow | Platform directories, boundary scanners, Windows/POSIX-specific implementations, and hosted default non-V8 CI. | More OS API categories and broader platform-specific evidence. |
| V8 integration | Yellow | Optional V8 execution, owner-thread checks, registered handlers, bounded Promise settlement, request/result conversion, selected intrinsics, SQLite bridge, and V8-gated conformance. | SDK packaging/hosted lane maturity, ESM module loading, scalable native async sources, async stack remapping, and executor-backed SQLite bridge. |
| HTTP foundation | Yellow | Bounded parser, route dispatch, response writer, backend lifecycle, libuv localhost transport, sequential keep-alive, chunked behavior, opt-in inbound OpenSSL TLS wrapping, timeout/shutdown diagnostics, and conformance/stress smoke for current lanes. | Production TLS hardening, production graceful drain, HTTP/2/3, WebSockets, SSE, static files, public streaming APIs, middleware, and production-edge hardening. |
| Compiler extraction | Yellow | Supported source subset emits deterministic Plan/bundle/source-map artifacts, rejects unsupported syntax, records route/provider/capability/result/config metadata, and has fixture/golden coverage. | Broader TypeScript checking, decorators/controllers, repository/object/class inference, non-database provider adapters, cache reuse, and final Framework v2 extraction. |
| App/framework ergonomics | Yellow | Bootstrap app/results/config/logging/services/modules/schema/data helpers, source-input execution, Plan-driven feature validation, runtime artifacts, and examples for current supported shapes. | Framework v2, service lifetimes beyond current feature validation, reload/secrets/custom config providers, final public docs, and packaging story. |
| Data providers | Yellow | Native provider foundations, capability checks, provider executor contracts, SQLite serialized bridge, V8-gated true-async PostgreSQL and SQL Server bridges, bounded provider pooling, and provider metadata/audit surfaces. | Broader live-provider CI lanes, richer audit policy, migrations/schema tooling, and production hardening. |
| Security/capabilities | Yellow | Plan-visible capability/provider metadata, runtime registry checks, filesystem path policy, network metadata, provider admission checks, secret redaction, and audit/doctor surfaces. | OS sandboxing, permission prompts/enforcement beyond current metadata, broader audit enforcement, and production least-privilege tooling. |
| Network APIs | Green | Scoped TCP, local IPC, HTTP client, feature-gated V8 bridge behavior, resource IDs, examples, doctor/audit goldens, and conformance evidence for current contracts. | External live-network, stress/torture, package, benchmark, TLS, pooled/redirecting HTTP client behavior, UDP, WebSocket, and compatibility claims remain out of scope. |
| Time/deadline/cancellation | Green | Public time API contract, V8-gated native delay backend, deadlines, cancellation controller/signal, intervals, scheduled jobs, fake clock, examples, and diagnostic goldens. | Broader shutdown/cancellation consumers remain future work. |
| Crypto APIs | Green | Random bytes/UUID/token helpers, SHA-2/HMAC, constant-time helpers, secrets, password hashing/verify/needs-rehash, non-crypto hash, examples, and conformance evidence. | Broader algorithms, larger/offloaded streaming hash work, package evidence, and production hardening. |
| Codec APIs | Green | Base64/Base64Url/Hex, UTF-8 encode/decode, binary reader/writer, gzip/gunzip, async-iterable compression transforms, checksums, examples, and conformance evidence. | Broader compression algorithms, native incremental streaming, package evidence, and public alpha docs. |
| Packaging/build/distribution | Yellow | Local package layouts and outside-checkout package fixtures exist as experimental evidence. | Release packaging, installers, hosted distribution, signing, upgrade story, and public alpha package readiness. |
| Public docs | Red | Public docs are a pre-alpha skeleton with explicit limits. | Final public user docs, tutorials, release notes, packaging story, and public alpha readiness verification. |

## Interpretation

Passing default CI does not prove V8 execution, live database access, package release
readiness, public alpha readiness, production HTTP behavior, or benchmark claims. Those
claims require their named implementation and evidence lanes.

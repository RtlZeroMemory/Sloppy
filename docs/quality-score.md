# Quality Score

Status: 2026-05-01 post-core reset.

Status key:

- Green: implemented and enforced by default checks.
- Yellow: implemented or specified with meaningful gaps.
- Red: not ready for public alpha use.

Default gates prove the non-V8 path unless a V8-enabled lane is explicitly run. Optional
V8, package, live-provider, stress, and benchmark evidence must be reported separately.

| Area | Status | Implemented / Proven | Remaining Gap |
| --- | --- | --- | --- |
| Native C safety | Yellow | Core primitives, arenas, builders, diagnostics, plan parser, HTTP/backend/transport paths, provider boundaries, and standards scanners. | Sanitizer/fuzz gates, scanner fixtures, provider primitive cleanup, request-scope leak checks. |
| Memory/string foundations | Yellow | `SlStr`/`SlBytes`, arena copies, builders, intern tables, HTTP/Plan/V8/SQLite adoption for current paths. | PostgreSQL/SQL Server provider cleanup, SQLite V8 allocation preflight, remaining CLI/conformance cleanup. |
| Diagnostics | Yellow | Stable codes, text/JSON/source-frame rendering, redaction helpers, provider/runtime diagnostics, source-map artifacts. | Runtime consumption of compiler source maps, richer CLI format plumbing, structured fixes. |
| Platform boundaries | Yellow | Platform dirs, Windows/POSIX scanners, platform time, hosted non-V8 CI. | Scanner self-tests and richer OS API categories. |
| V8 integration | Yellow | Optional V8 execution, handlers, bounded direct Promise settlement, SQLite bridge, owner-thread checks. | SDK packaging/CI, ESM module loading, timers/fetch/native async sources, runtime source-map remapping. |
| HTTP foundation | Yellow | Bounded parser/body/response/dispatch, backend lifecycle, libuv localhost transport, sequential HTTP/1.1 keep-alive, idle timeout, max requests, lifecycle reset, chunked request decoding, internal chunked response writer, and HTTP-25.F bounded keep-alive/chunked/streaming conformance plus stress smoke. | Public request/response streaming APIs, SSE/file streaming, production graceful drain, TLS/HTTP2/3/WebSockets, middleware, production-edge stress. |
| Compiler extraction | Yellow | Supported source subset emits deterministic artifacts and diagnostics; source-input run compiles through `sloppyc` and validates generated artifacts through the rebuild-always artifact validation pipeline. COMPILER-30 now defines the source-of-truth inference roadmap, COMPILER-30.A added the module/library/test-harness foundation, COMPILER-30.B/C adds parser/import/symbol/DSL/static-eval module APIs, COMPILER-30.D covers Minimal API route methods, nested route groups, function-module route contributions, duplicate route validation, and module source locations, COMPILER-30.E emits provider/config/schema/request/result metadata, COMPILER-30.F/G starts provider-kind-aware route effect/capability inference for database provider handles plus same-file helpers, COMPILER-30.H/I emits strong Plan completeness/source/module metadata with missing-provider validation, and COMPILER-30.J adds broad fixture/golden coverage for realistic supported apps, partial completeness, provider-kind database metadata, and invalid provider/effect shapes. | Non-database provider adapters, imported helper effects, repository/object/class inference, broad TS checking, package/service extraction, cache reuse, broader async source shapes. |
| App/framework ergonomics | Yellow | Bootstrap builder/app/config/logging/services/modules/schema/data/result helpers, typed config access, `bind`, config-driven SQLite provider metadata, and current compiler/runtime path. | Request binding/validation completion, broader generated capability policy, service lifetimes, executable public examples, reload/secrets/custom config providers. |
| Data providers | Yellow | Native SQLite/PostgreSQL/SQL Server providers, V8 SQLite bridge, capability checks, provider executor infrastructure. | SQLite bridge not yet executor-backed; PostgreSQL/SQL Server JS bridges, pooling/cancellation/live conformance deferred. |
| Security/capabilities | Yellow | Plan/provider/capability metadata, registry checks, provider-executor admission, SQLite bridge enforcement, and Plan-driven `sloppy capabilities`/audit visibility for compiler-generated provider effects. | No OS sandbox; filesystem/network capabilities are skeletons; audit remains static metadata review. |
| Conformance/package evidence | Yellow | ENGINE-19 evidence matrix, V8/HTTP/async/SQLite/capability/package lanes, HTTP-25.F bounded transport stress/smoke, local package smoke. | Public alpha readiness, hosted V8/package/live-provider gates, release hardening. |
| Docs freshness | Yellow | Canonical docs plus post-core compact audits. | Automated link checker and semantic stale-doc checks. |
| Public alpha | Red | Core proof and current source-input shortcut exist. | Executable examples beyond the current compiler subset, canonical public docs, package/platform story, no fake production/perf claims. |

## Interpretation

Passing default Windows/Linux/macOS CI does not prove V8 execution, live database access,
package execution, public alpha readiness, production HTTP behavior, or benchmark claims.
Those claims require their named gates and PR evidence.

The next owner-approved planning wave is mapped in
`docs/project/post-core-next-wave-issue-map.md`. It keeps source-input run, Strong Plan,
framework ergonomics, hardening, and HTTP keep-alive/streaming as separate evidence lanes;
it does not change public-alpha or production HTTP status.

Framework/API ergonomics are now locked in `docs/project/framework-api-shape.md` as a
design target: Minimal API first, function modules first, inferred capabilities by
default, layered Plan-visible config, explicit request binding, explicit `Results.*`, and
Plan-first DSL extraction. This design lock is not implementation evidence.

COMPILER-30 (#460) is the compiler inference roadmap that turns that design target into
Plan-visible metadata. COMPILER-30.A is foundation evidence for module architecture,
library API, diagnostics shape, and fixture/golden harness. COMPILER-30.B/C and D add
parser/import/symbol/DSL/static-eval and route/group/function-module extraction evidence;
COMPILER-30.E adds provider/config/schema/request/result metadata evidence. COMPILER-30.F/G
adds the first provider-kind-aware route effect and inferred capability evidence for the
database adapter family, including an honest rejection path for generated non-SQLite
provider-backed handlers until those JS bridges exist. COMPILER-30.H/I adds completeness,
static validation, strong Plan metadata, and compatibility goldens. COMPILER-30.J adds the
large fixture/golden pass for realistic supported apps, partial completeness, multi-provider
database metadata, and invalid provider/effect shapes. Non-database provider adapters,
imported/repository/service inference, and broad TypeScript checking remain open.

ENGINE-20.C/D add the first post-COMPILER-30 Strong Plan consumer evidence for developers:
routes, capabilities, doctor, audit, and OpenAPI consume compiler-emitted Plan metadata.
OpenAPI remains a supported subset with explicit partial markers, and optimization
candidate hooks are reports only.

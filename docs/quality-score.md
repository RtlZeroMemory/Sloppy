# Quality Score

Status key:

- Green: implemented and enforced by default checks.
- Yellow: implemented or specified with meaningful gaps.
- Red: not ready for public alpha use.

This score separates "implemented" from "validated". Default gates are not V8-enabled
gates, and default provider tests are not live database tests. Cross-platform default CI
proves the non-V8 path across hosted Windows, Linux, and macOS runners; it still does not
prove optional SDK/service paths.

MAIN evidence is tracked separately in `docs/project/main-evidence.md`. Default gates prove
default build/test health, static/lint gates, Rust compiler-tool gates, and non-V8
diagnostic behavior. Only a V8-enabled build plus the documented run-once smoke proves the
executable hello path through V8.

2026-04-29 strategic update: Slop Engine foundation is the next phase. Public alpha
readiness remains red until the engine layers in
`docs/project/slop-engine-layered-roadmap.md` are completed or explicitly deferred.
ENGINE-01 locks the implementation-facing contract in
`docs/project/engine-framework-contract.md`. The locked contract covers compiler support
for realistic apps, V8 Promise/microtask handling, framework HTTP API
runtime, SQLite capability-wired end-to-end execution, app/request lifecycle cleanup,
diagnostics/source maps, cancellation/backpressure/resource-limit infrastructure,
conformance examples, and packaged runtime evidence. PostgreSQL and SQL Server JS bridges
and benchmark claims are not immediate readiness blockers.

| Area | Status | Implemented | Validated by default gates | Gated / not validated by default | To move to Green |
| --- | --- | --- | --- | --- | --- |
| Native C safety | Yellow | Core primitives, checked math, arena, resource table, diagnostics, plan parser, HTTP parser, providers, boundary-oriented tests, and local sanitizer options. | CTest, warnings, format, lint, C standards scanner, platform scanner. | Sanitizers are not required in CI, fuzz targets are not implemented, and allocator misuse checks, request-scope leak checks, and deeper cleanup integration are incomplete. | Add stable sanitizer/fuzz gates, allocator checks, request-scope resource leak checks, and scanner fixtures. |
| Diagnostics | Yellow | Stable C diagnostic codes, text rendering, JSON diagnostic rendering, single-line source frames, compiler source-frame fixture output, ENGINE-02 handler-line source-map artifacts, V8 generated-source exception spans, and representative redaction helpers/tests. | `core.diagnostics.foundation`, diagnostics golden snapshots, compiler diagnostic/source-map fixtures, provider redaction tests, and V8-gated exception/source checks when SDK is available. | CLI-wide diagnostic-format selection, richer related compiler spans, localization, structured fixes, and TypeScript source-map remapping remain deferred because runtime diagnostics do not consume compiler maps yet. | Add CLI format plumbing when command error paths share the renderer, then add source-map parsing/remapping in the V8 diagnostic path. |
| Platform boundaries | Yellow | `src/platform/*` structure, platform docs, Windows/POSIX scanners, platform time abstraction, and default Windows/Linux/macOS CI. | Lint checks common forbidden OS headers outside platform directories; POSIX CI runs shell scanners. | Scanner self-tests, sanitizer/fuzz platform jobs, and richer platform API categories are missing. | Add scanner fixtures, sanitizer/fuzz jobs, and documented platform API categories. |
| Docs freshness | Yellow | Source docs, public docs, module docs, ADRs, quality score, and tech-debt tracker exist. | Lightweight docs freshness structure check. | Semantic stale-doc detection and link checking are not implemented. | Add link checker and targeted semantic checks for examples/API claims. |
| Language standards | Green | C/C++, JS/TS, and Rust standards docs exist with operational skill summaries and review checklists. | `dev.ps1 lint` runs C standards, JS/TS standards, Rust standards, platform, docs freshness, and artifact hygiene checks. | The JS/TS scanner is structural rather than parser-based; deeper Rust lint config is intentionally minimal. | Add parser-based JS linting and stronger Rust lint configuration only after the compiler/tooling scope justifies them. |
| Tests as intent | Yellow | Testing strategy requires docs-as-intent; many modules have tests tied to docs. | CTest/cargo gates plus golden fixtures. | Some structural/example checks are static because runtime execution is not available. | Keep tests tied to source docs and replace static checks with runtime checks when features exist. |
| V8 integration | Yellow | SDK detection, isolated ABI, classic-script smoke, exception mapping, bootstrap runtime asset loading, handler registration intrinsic, provider-intrinsic aggregation outside `engine_v8.cc`, registered handler table validation, handwritten/compiler artifact execution paths, owner-thread checks, lifecycle diagnostics, and ENGINE-03 microtask-only Promise settlement/rejection/pending diagnostics. | Default gates validate only non-V8/noop engine paths and static bootstrap assets. | V8 tests require `SLOPPY_ENABLE_V8=ON` and a valid SDK; true ESM loading, SDK distribution, timers/fetch/native async queues, broad async event-loop behavior, scalable async runtime evidence, and runtime source-map remapping are not solved. ENGINE-12 (#306-#310) tracks the full native completion, owner-thread continuation, deadline/shutdown, backpressure, provider/offload, and stress-evidence layer. | Add packaged SDK strategy, V8-enabled CI or release gate, true ESM/module cache, ENGINE-12 native async completion/event-loop integration, source-map remapping, and keep future provider bridges in `intrinsics_<provider>.cc`. |
| HTTP foundation | Yellow | Route parser/matcher, complete-buffer HTTP/1 request-head parser, libuv init smoke, native route table with literal-before-parameter precedence, synthetic GET dispatch, dev-only `sloppy run` socket/`--once`, native response writer, query parser, request-target/query bounds, unsupported-body rejection, and route/query/request context. | Default C tests, route table/dispatch tests, response/query/request-target tests, default `sloppy run` failure-mode tests, and static example checks. | V8-backed run success, request-context execution, invalid descriptor execution, and socket smoke require a V8-enabled build; no body parser, streaming parser, middleware, or production hardening. | Broaden server lifecycle tests and keep production behavior separate. |
| Public API ergonomics | Yellow | Bootstrap ESM stdlib supports builder/app, `Results.*`, route groups, schema, modules, services, config, logging, data facade. The compiler accepts the supported bare `"sloppy"` import source shape, rewrites it to the classic bootstrap runtime handoff, emits ENGINE-02 method/async/provider/source-map metadata, and produces artifacts that `sloppy run --artifacts` can execute for GET route/query/request context plus direct async handlers when V8 is enabled. | Static checks, optional Node ESM tests, compiler golden tests, response/context tests, default non-V8 run failure tests, and V8-gated runtime tests when SDK is available. | True V8 ESM stdlib loading, `app.run`, source-input run handoff, `await`/multi-statement async compiler shapes, non-GET dispatch, and broad module/service compiler input shapes are deferred. | Add source-input run handoff and executable public examples through the real ESM bootstrap module shape once V8 module loading grows past the classic runtime asset. |
| Compiler extraction | Yellow | Oxc-backed one-file extractor for `Sloppy.create`, builder build, literal GET/POST/PUT/PATCH/DELETE route metadata, simple route groups, route names, async handler metadata, minimal SQLite provider/capability metadata, handler ID assignment, `"sloppy"` import rewrite, supported syntax matrix, specific unsupported dynamic/computed/import/handler/secret diagnostics, deterministic `app.plan.json`, `app.js`, real handler-line source map, deterministic artifact hashes, and native-validated route metadata consumed by `sloppy run`. | Cargo tests, compiler golden fixtures, full rendered diagnostics fixtures, rejected-build no-artifact coverage, conformance compile/reject tests, Rust standards scanner, clippy, and V8-gated run smoke when SDK is available. | No full TypeScript checking, Node/npm package resolution, bundling, modules/services/schemas, non-GET dispatch, provider bridge execution, broader async source shapes, or source-input `sloppy run` handoff. | Add source-input handoff, module/service/schema extraction, runtime async/HTTP/provider consumption, and official type checking. |
| App host | Yellow | JavaScript-only builder/freeze/config/logging/services skeleton, plus native startup validation for the parsed Plan-backed artifact path and a minimal native request-scope cleanup boundary. | Bootstrap tests validate in-memory behavior; `core.app_host.hardening` covers startup validation and request cleanup without V8. | No native module graph, DI container, service activation, provider bridge, async request-scope retention, disposal hooks, or public `run/listen` lifecycle. | Emit real module/service metadata, add service lifetimes/disposal, wire provider handles through request scope, and keep V8 validation reported separately. |
| Modules | Yellow | JavaScript-only `Sloppy.module`, dependency ordering, phases, attribution, debug metadata. | Bootstrap module tests. | No compiler extraction, package loading, native module graph, middleware/filter phases, or real plan emission. | Emit/validate module plan metadata and load it through runtime startup. |
| Data providers | Yellow | Native SQLite, PostgreSQL, and SQL Server provider boundaries plus bootstrap data API/fake provider, core JS-safe resource IDs, and the V8-gated SQLite JS-to-native bridge. | SQLite native in-memory tests; SQLite JS wrapper mocked-bridge tests; V8-gated SQLite bridge tests when SDK is available; PostgreSQL/SQL Server default non-live tests; redaction, option, and tiny pool/resource lifecycle tests. | PostgreSQL and SQL Server live tests are separate opt-in CTest gates that skip when env vars are absent; PostgreSQL/SQL Server JS-to-native bridges, request-scope ownership, production pooling, async offload, and capability enforcement are missing. | Add optional live service infrastructure, production pool policy, async strategy, capability policy, and future provider bridges through `intrinsics_<provider>.cc`. |
| CLI | Yellow | Metadata-only `routes`, `doctor`, `audit`, and route-skeleton `openapi` commands over validated Plan artifacts where supported. Doctor reports V8/live-provider/package caveats; audit reports bounded alpha metadata findings. | Golden process tests over fixtures and compiler-generated artifact plans, plus missing/invalid metadata failure tests. | Commands do not compile, run handlers, start HTTP, enter V8, run live provider checks, or generate real OpenAPI schemas/security schemes. | Add opt-in live diagnostics and real schema/security output only after compiler/runtime metadata exists. |
| Benchmarks | Yellow | `sloppy_bench`, route/parser/handler/synthetic dispatch scenarios, JSON/text output, wrapper. | List/smoke CTest checks. | No performance regression gate; no real HTTP/V8/JSON/live DB/external comparisons. | Add release benchmark methodology, trend tracking, real paths, and only then external comparisons. |
| Cross-platform readiness | Yellow | Cross-platform layout, platform boundary policy, Linux clang/gcc CI, macOS clang CI, Windows clang-cl CI, POSIX standards scanners, and local Unix package smoke tooling exist. | Required default CI proves non-V8 configure/build/test, Cargo gates, artifact hygiene, and boundary scans on Windows, Linux, and macOS. | V8 CI is manual/gated, live provider services are opt-in, package smoke is not required on Linux/macOS hosted runners, and sanitizer/fuzz matrices are absent. | Add V8 SDK caching/prebuilt setup, optional live provider service jobs, hosted Linux/macOS package smoke, and stable sanitizer/fuzz gates before stronger public-alpha claims. |
| Distribution readiness | Yellow | Experimental local package layout, Windows ZIP tooling, Unix TAR script, manifest, checksums, stdlib inclusion, V8 SDK exclusion policy, Windows outside-checkout ZIP smoke, Unix outside-checkout TAR smoke, and optional V8 runtime-file validation exist. | Windows package creation and package smoke can validate `sloppy --version`, `sloppy --help`, `sloppyc --version`, `sloppyc --help`, stdlib assets, manifest fields, excluded directories, V8 SDK exclusion, and SHA256SUMS locally. Unix smoke can validate the same package-layout contract locally when run on Linux/macOS. | Package smoke is not release readiness and does not prove live provider availability, SQL Server driver installation, V8 execution, or JS-to-native provider bridges. Linux/macOS packaging is not CI-validated yet; V8 package execution smoke, dynamic V8 runtime file lists, libpq/runtime DLL strategy, signing/notarization, installers, package-manager distribution, reproducibility hardening, and public release automation remain deferred. | Add hosted package smoke, V8 package execution validation, provider runtime dependency strategy, signing/notarization, and package-manager work only when scoped. |
| Security / capability model | Yellow | Capability metadata, native Plan capability/provider validation, immutable runtime registry, database policy check hooks, denied diagnostics, and filesystem/network skeleton checks exist. | Plan parser fixtures, `core.capability.registry`, and expanded metadata/audit fixture checks. | No OS sandbox, no filesystem/network API, no permission prompts, and no JS provider bridge has wired the native hook yet. | Wire the SQLite JS-native bridge to capability checks after MAIN1-08 exposes the stable bridge boundary; keep OS sandboxing as later research. |
| End-to-end conformance | Yellow | MAIN1-13 conformance layout ties public compiler examples and selected checked-in artifact fixtures to the real compile/run boundary, and ENGINE-02 adds compile/reject conformance for method, async, provider/capability, source-map, and unsupported metadata cases. | Default CTest conformance compiles hello/request-context/ENGINE-02 fixtures twice for deterministic artifact output and rejects unsupported dynamic routes/imports/async bodies/secret metadata without emitting artifacts. V8-gated conformance runs hello, request context, async handler, invalid result descriptor, and SQLite bridge fixtures when SDK evidence is available. | Default gates do not prove V8 execution; SQLite capability enforcement through the JS bridge is still deferred; unsupported request bodies are covered by HTTP unit/dispatch tests rather than a socket-mode conformance fixture. | Add conformance for future source-input handoff, body/header context, broader async handler source shapes, non-GET dispatch, and JS bridge capability enforcement only when those features are implemented. |

## Current Summary

The repo has a surprisingly broad foundation now, and it has the first dev-only executable
artifact path plus default hosted CI across Windows, Linux, and macOS. Public-alpha
readiness is still red because the path is intentionally narrow: V8 validation is gated,
source-input `sloppy run` is deferred, response/request handling is still dev-only,
capability metadata now has native Plan/provider validation, an immutable runtime registry,
and database policy check hooks, but full scalable async runtime work (#306-#310),
JS-native bridge wiring, OS sandboxing, optional SDK/service validation, and release
packaging remain separate or experimental.

ROADMAP MAIN and ROADMAP MAIN.1 are now historical input to the strategic ENGINE roadmap.
Public alpha docs should not move ahead of the ENGINE roadmap unless a document explicitly
says the relevant workflow is still deferred and not part of the alpha claim.

## Gate Interpretation

Passing the default Windows gates means:

- portable C foundations, non-V8 tests, static/bootstrap checks, CLI fixture tests, and
  provider default tests passed;
- MAIN1-13 default conformance compiled supported public examples and rejected selected
  unsupported compiler inputs without requiring V8;
- `sloppy run` startup/failure-mode tests passed without proving V8 execution;
- V8-enabled tests did not necessarily run;
- PostgreSQL and SQL Server live tests did not necessarily run;
- package smoke did not necessarily run and is not public release readiness or V8 execution
  evidence;
- benchmark smoke ran only as harness correctness, not as performance evidence.

Passing the default cross-platform CI additionally means:

- non-V8 CMake configure/build/CTest passed on Windows clang-cl, Linux clang, Linux gcc,
  and macOS clang;
- Cargo build/fmt/clippy/test passed on those hosted runners;
- POSIX C/platform standards scans passed on Linux/macOS;
- provider live tests were either explicitly enabled by environment or reported as
  skipped. It does not mean live PostgreSQL, live SQL Server, package smoke, V8 package
  smoke, sanitizer gates, fuzz gates, or V8 validation ran.

Do not convert default-gate success into claims about V8, live databases, package release
readiness, HTTP throughput, public performance, public package usability, or production
readiness.

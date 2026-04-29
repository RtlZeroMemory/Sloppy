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

| Area | Status | Implemented | Validated by default gates | Gated / not validated by default | To move to Green |
| --- | --- | --- | --- | --- | --- |
| Native C safety | Yellow | Core primitives, checked math, arena, resource table, diagnostics, plan parser, HTTP parser, providers, boundary-oriented tests, and local sanitizer options. | CTest, warnings, format, lint, C standards scanner, platform scanner. | Sanitizers are not required in CI, fuzz targets are not implemented, and allocator misuse checks, request-scope leak checks, and deeper cleanup integration are incomplete. | Add stable sanitizer/fuzz gates, allocator checks, request-scope resource leak checks, and scanner fixtures. |
| Diagnostics | Yellow | Stable C diagnostic codes, text rendering, JSON diagnostic rendering, single-line source frames, compiler source-frame fixture output, and representative redaction helpers/tests. | `core.diagnostics.foundation`, diagnostics golden snapshots, compiler diagnostic fixtures, and provider redaction tests. | CLI-wide diagnostic-format selection, richer related compiler spans, localization, structured fixes, and V8 source-map exception remapping remain deferred. | Add CLI format plumbing when command error paths share the renderer, then complete MAIN1-05 source-map exception mapping and richer compiler spans. |
| Platform boundaries | Yellow | `src/platform/*` structure, platform docs, Windows/POSIX scanners, platform time abstraction, and default Windows/Linux/macOS CI. | Lint checks common forbidden OS headers outside platform directories; POSIX CI runs shell scanners. | Scanner self-tests, sanitizer/fuzz platform jobs, and richer platform API categories are missing. | Add scanner fixtures, sanitizer/fuzz jobs, and documented platform API categories. |
| Docs freshness | Yellow | Source docs, public docs, module docs, ADRs, quality score, and tech-debt tracker exist. | Lightweight docs freshness structure check. | Semantic stale-doc detection and link checking are not implemented. | Add link checker and targeted semantic checks for examples/API claims. |
| Language standards | Green | C/C++, JS/TS, and Rust standards docs exist with operational skill summaries and review checklists. | `dev.ps1 lint` runs C standards, JS/TS standards, Rust standards, platform, docs freshness, and artifact hygiene checks. | The JS/TS scanner is structural rather than parser-based; deeper Rust lint config is intentionally minimal. | Add parser-based JS linting and stronger Rust lint configuration only after the compiler/tooling scope justifies them. |
| Tests as intent | Yellow | Testing strategy requires docs-as-intent; many modules have tests tied to docs. | CTest/cargo gates plus golden fixtures. | Some structural/example checks are static because runtime execution is not available. | Keep tests tied to source docs and replace static checks with runtime checks when features exist. |
| V8 integration | Yellow | SDK detection, isolated ABI, classic-script smoke, exception mapping, bootstrap runtime asset loading, handler registration intrinsic, registered handler table validation, and handwritten/compiler artifact execution paths. | Default gates validate only non-V8/noop engine paths and static bootstrap assets. | V8 tests require `SLOPPY_ENABLE_V8=ON` and a valid SDK; true ESM loading, SDK distribution, promises, and owner-thread checks are not solved. | Add packaged SDK strategy, V8-enabled CI or release gate, true ESM/module cache, promises, richer source maps, and owner-thread checks. |
| HTTP foundation | Yellow | Route parser/matcher, complete-buffer HTTP/1 request-head parser, libuv init smoke, synthetic GET dispatch, dev-only `sloppy run` socket/`--once`, native response writer, query parser, and route/query/request context. | Default C tests, synthetic dispatch tests, response/query tests, default `sloppy run` failure-mode tests, and static example checks. | V8-backed run success, request-context execution, invalid descriptor execution, and socket smoke require a V8-enabled build; no body parser, streaming parser, middleware, or production hardening. | Broaden server lifecycle tests and keep production behavior separate. |
| Public API ergonomics | Yellow | Bootstrap ESM stdlib supports builder/app, `Results.*`, route groups, schema, modules, services, config, logging, data facade. Compiler MVP accepts a tiny bare `"sloppy"` import source shape, rewrites it to the classic bootstrap runtime handoff, and emits artifacts that `sloppy run --artifacts` can execute with route/query/request context when V8 is enabled. | Static checks, optional Node ESM tests, compiler golden tests, response/context tests, default non-V8 run failure tests, and V8-gated runtime tests when SDK is available. | True V8 ESM stdlib loading, `app.run`, source-input run handoff, and broad compiler input shapes are deferred. | Add source-input run handoff and executable public examples through the real ESM bootstrap module shape once V8 module loading grows past the classic runtime asset. |
| Compiler extraction | Yellow | Oxc-backed one-file extractor for `Sloppy.create`, builder build, literal `mapGet`, simple route groups, route names, handler ID assignment, `"sloppy"` import rewrite, supported syntax matrix, specific unsupported dynamic/computed/import/handler diagnostics, deterministic `app.plan.json`, `app.js`, placeholder source map, deterministic artifact hashes, and native-validated route metadata consumed by `sloppy run`. | Cargo tests, compiler golden fixtures, full rendered diagnostics fixtures, rejected-build no-artifact coverage, Rust standards scanner, clippy, and V8-gated run smoke when SDK is available. | No full TypeScript checking, Node/npm package resolution, bundling, modules/services/data providers, source-map fidelity, provider/capability extraction, or source-input `sloppy run` handoff. | Add broader extraction, source maps, and official type checking. |
| App host | Yellow | JavaScript-only builder/freeze/config/logging/services skeleton. | Bootstrap tests validate in-memory behavior. | No native app graph, startup validation, request scopes, disposal, or run/listen behavior. | Implement native app graph and runtime startup validation after compiler extraction. |
| Modules | Yellow | JavaScript-only `Sloppy.module`, dependency ordering, phases, attribution, debug metadata. | Bootstrap module tests. | No compiler extraction, package loading, native module graph, middleware/filter phases, or real plan emission. | Emit/validate module plan metadata and load it through runtime startup. |
| Data providers | Yellow | Native SQLite, PostgreSQL, and SQL Server provider boundaries plus bootstrap data API/fake provider and core JS-safe resource IDs. | SQLite live-in-memory tests; PostgreSQL/SQL Server default non-live tests; redaction, option, and tiny pool/resource lifecycle tests. | PostgreSQL and SQL Server live tests are separate opt-in CTest gates that skip when env vars are absent; JS-to-native bridge, request-scope ownership, production pooling, async offload, and capability enforcement are missing. | Add SQLite JS-native bridge, optional live service infrastructure, production pool policy, async strategy, and capability policy. |
| CLI | Yellow | Metadata-only `routes`, `doctor`, `audit`, and `openapi` commands. | Golden process tests over fixtures. | Commands do not compile, run handlers, start HTTP, enter V8, or run live provider checks. | Wire to compiler/app-host metadata and add opt-in live diagnostics. |
| Benchmarks | Yellow | `sloppy_bench`, route/parser/handler/synthetic dispatch scenarios, JSON/text output, wrapper. | List/smoke CTest checks. | No performance regression gate; no real HTTP/V8/JSON/live DB/external comparisons. | Add release benchmark methodology, trend tracking, real paths, and only then external comparisons. |
| Cross-platform readiness | Yellow | Cross-platform layout, platform boundary policy, Linux clang/gcc CI, macOS clang CI, Windows clang-cl CI, POSIX standards scanners, and local Unix package smoke tooling exist. | Required default CI proves non-V8 configure/build/test, Cargo gates, artifact hygiene, and boundary scans on Windows, Linux, and macOS. | V8 CI is manual/gated, live provider services are opt-in, package smoke is not required on Linux/macOS hosted runners, and sanitizer/fuzz matrices are absent. | Add V8 SDK caching/prebuilt setup, optional live provider service jobs, hosted Linux/macOS package smoke, and stable sanitizer/fuzz gates before stronger public-alpha claims. |
| Distribution readiness | Yellow | Experimental local package layout, Windows ZIP tooling, Unix TAR script, manifest, checksums, stdlib inclusion, V8 SDK exclusion policy, Windows outside-checkout ZIP smoke, Unix outside-checkout TAR smoke, and optional V8 runtime-file validation exist. | Windows package creation and package smoke can validate `sloppy --version`, `sloppy --help`, `sloppyc --version`, `sloppyc --help`, stdlib assets, manifest fields, excluded directories, V8 SDK exclusion, and SHA256SUMS locally. Unix smoke can validate the same package-layout contract locally when run on Linux/macOS. | Package smoke is not release readiness and does not prove live provider availability, SQL Server driver installation, V8 execution, or JS-to-native provider bridges. Linux/macOS packaging is not CI-validated yet; V8 package execution smoke, dynamic V8 runtime file lists, libpq/runtime DLL strategy, signing/notarization, installers, package-manager distribution, reproducibility hardening, and public release automation remain deferred. | Add hosted package smoke, V8 package execution validation, provider runtime dependency strategy, signing/notarization, and package-manager work only when scoped. |
| Security / capability model | Red | Capability metadata, native Plan capability/provider metadata validation, and audit concepts exist. | Plan parser fixtures plus metadata/audit fixture checks. | No runtime enforcement, filesystem/network policy, provider access policy, or permission gate. | Implement EPIC-27 enforcement and diagnostics before public alpha. |

## Current Summary

The repo has a surprisingly broad foundation now, and it has the first dev-only executable
artifact path plus default hosted CI across Windows, Linux, and macOS. Public-alpha
readiness is still red because the path is intentionally narrow: V8 validation is gated,
source-input `sloppy run` is deferred, response/request handling is still dev-only,
capability enforcement is missing, optional SDK/service validation remains separate, and
release packaging is experimental.

ROADMAP MAIN is the minimal verification path for the existing compiler-to-runtime work.
ROADMAP MAIN.1 is the hardening path for alpha-production quality. Public alpha docs should
not move ahead of MAIN.1 unless a document explicitly says the relevant workflow is still
deferred.

## Gate Interpretation

Passing the default Windows gates means:

- portable C foundations, non-V8 tests, static/bootstrap checks, CLI fixture tests, and
  provider default tests passed;
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

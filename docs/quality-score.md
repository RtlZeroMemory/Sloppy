# Quality Score

Status key:

- Green: implemented and enforced by default checks.
- Yellow: implemented or specified with meaningful gaps.
- Red: not ready for public alpha use.

This score separates "implemented" from "validated". Default gates are not V8-enabled
gates, and default provider tests are not live database tests.

| Area | Status | Implemented | Validated by default gates | Gated / not validated by default | To move to Green |
| --- | --- | --- | --- | --- | --- |
| Native C safety | Yellow | Core primitives, checked math, arena, diagnostics, plan parser, HTTP parser, providers, and boundary-oriented tests. | CTest, warnings, format, lint, C standards scanner, platform scanner. | Sanitizers, fuzzing, allocator/resource misuse checks, and deeper cleanup/leak checks are incomplete. | Add sanitizer/fuzz gates, resource table tests, allocator checks, and scanner fixtures. |
| Platform boundaries | Yellow | `src/platform/*` structure, platform docs, scanner, and platform time abstraction. | Lint checks common forbidden OS headers outside platform directories. | Linux/macOS CI and scanner self-tests are missing. | Add scanner fixtures, Unix CI jobs, and documented platform API categories. |
| Docs freshness | Yellow | Source docs, public docs, module docs, ADRs, quality score, and tech-debt tracker exist. | Lightweight docs freshness structure check. | Semantic stale-doc detection and link checking are not implemented. | Add link checker and targeted semantic checks for examples/API claims. |
| Language standards | Green | C/C++, JS/TS, and Rust standards docs exist with operational skill summaries and review checklists. | `dev.ps1 lint` runs C standards, JS/TS standards, Rust standards, platform, docs freshness, and artifact hygiene checks. | The JS/TS scanner is structural rather than parser-based; deeper Rust lint config is intentionally minimal. | Add parser-based JS linting and stronger Rust lint configuration only after the compiler/tooling scope justifies them. |
| Tests as intent | Yellow | Testing strategy requires docs-as-intent; many modules have tests tied to docs. | CTest/cargo gates plus golden fixtures. | Some structural/example checks are static because runtime execution is not available. | Keep tests tied to source docs and replace static checks with runtime checks when features exist. |
| V8 integration | Yellow | SDK detection, isolated ABI, classic-script smoke, call-function smoke, exception mapping, and handwritten execution path. | Default gates validate only non-V8/noop engine paths. | V8 tests require `SLOPPY_ENABLE_V8=ON` and a valid SDK; SDK distribution is not solved. | Add packaged SDK strategy, V8-enabled CI or release gate, ESM/module loading, intrinsics, promises, and owner-thread checks. |
| HTTP foundation | Yellow | Route parser/matcher, complete-buffer HTTP/1 request-head parser, libuv init smoke, synthetic GET dispatch, dev-only `sloppy run` socket/`--once`, native response writer, query parser, and route/query/request context. | Default C tests, synthetic dispatch tests, response/query tests, default `sloppy run` failure-mode tests, and static example checks. | V8-backed run success, request-context execution, invalid descriptor execution, and socket smoke require a V8-enabled build; no body parser, streaming parser, middleware, or production hardening. | Broaden server lifecycle tests and keep production behavior separate. |
| Public API ergonomics | Yellow | Bootstrap ESM stdlib supports builder/app, `Results.*`, route groups, schema, modules, services, config, logging, data facade. Compiler MVP accepts a tiny bare `"sloppy"` import source shape and emits artifacts that `sloppy run --artifacts` can execute with route/query/request context when V8 is enabled. | Static checks, optional Node ESM tests, compiler golden tests, response/context tests, and non-V8 run failure tests. | No V8-backed stdlib loading, no `app.run`, source-input run handoff is deferred, and only a narrow compiler input shape runs. | Add V8 module loading, source-input run handoff, and executable public examples through the real bootstrap module shape. |
| Compiler extraction | Yellow | Oxc-backed one-file extractor for `Sloppy.create`, builder build, literal `mapGet`, simple route groups, route names, handler ID assignment, deterministic `app.plan.json`, `app.js`, placeholder source map, and route metadata consumed by `sloppy run`. | Cargo tests, compiler golden fixtures, diagnostics fixtures, Rust standards scanner, clippy, and V8-gated run smoke when SDK is available. | No full TypeScript checking, package resolution, bundling, modules/services/data providers, source-map fidelity, or source-input `sloppy run` handoff. | Add EPIC-24 bootstrap module loading, broader extraction, source maps, and official type checking. |
| App host | Yellow | JavaScript-only builder/freeze/config/logging/services skeleton. | Bootstrap tests validate in-memory behavior. | No native app graph, startup validation, request scopes, disposal, or run/listen behavior. | Implement native app graph and runtime startup validation after compiler extraction. |
| Modules | Yellow | JavaScript-only `Sloppy.module`, dependency ordering, phases, attribution, debug metadata. | Bootstrap module tests. | No compiler extraction, package loading, native module graph, middleware/filter phases, or real plan emission. | Emit/validate module plan metadata and load it through runtime startup. |
| Data providers | Yellow | Native SQLite, PostgreSQL, and SQL Server provider boundaries plus bootstrap data API/fake provider. | SQLite live-in-memory tests; PostgreSQL/SQL Server default non-live tests; redaction and option tests. | PostgreSQL and SQL Server live tests require env vars; JS-to-native bridge, resource IDs, pooling, async offload, and capability enforcement are missing. | Add JS-native resource bridge, live test infrastructure, pooling hardening, async strategy, and capability policy. |
| CLI | Yellow | Metadata-only `routes`, `doctor`, `audit`, and `openapi` commands. | Golden process tests over fixtures. | Commands do not compile, run handlers, start HTTP, enter V8, or run live provider checks. | Wire to compiler/app-host metadata and add opt-in live diagnostics. |
| Benchmarks | Yellow | `sloppy_bench`, route/parser/handler/synthetic dispatch scenarios, JSON/text output, wrapper. | List/smoke CTest checks. | No performance regression gate; no real HTTP/V8/JSON/live DB/external comparisons. | Add release benchmark methodology, trend tracking, real paths, and only then external comparisons. |
| Cross-platform readiness | Red | Cross-platform layout and platform boundary policy exist. | Windows-first gates only. | Linux/macOS CI, Unix tool wrappers, provider matrices, and package smoke tests are absent. | Add EPIC-26 CI expansion before public alpha claims. |
| Distribution readiness | Yellow | Experimental local package layout, Windows ZIP tooling, Unix TAR script, manifest, checksums, stdlib inclusion, V8 SDK exclusion policy, and outside-checkout ZIP smoke exist. | Windows package creation and package smoke can validate `sloppy --version`, `sloppyc --version`, stdlib assets, manifest fields, excluded directories, and SHA256SUMS locally. | Linux/macOS packaging is not CI-validated yet; dynamic V8 runtime bundling, libpq/runtime DLL strategy, signing/notarization, installers, package-manager distribution, reproducibility hardening, and public release automation remain deferred. | Add EPIC-26 cross-platform CI package validation, V8 runtime bundling hardening, provider runtime dependency strategy, signing/notarization, and package-manager work only when scoped. |
| Security / capability model | Red | Capability metadata and audit concepts exist. | Some metadata/audit fixture checks. | No runtime enforcement, filesystem/network policy, provider access policy, or permission gate. | Implement EPIC-27 enforcement and diagnostics before public alpha. |

## Current Summary

The repo has a surprisingly broad foundation now, and it has the first dev-only executable
artifact path. Public-alpha readiness is still red because the path is intentionally narrow:
V8 validation is gated, source-input `sloppy run` is deferred, response/request handling is
limited to the EPIC-23 dev-only artifact path, and capability enforcement, cross-platform
CI, and release packaging remain open.

## Gate Interpretation

Passing the default Windows gates means:

- portable C foundations, non-V8 tests, static/bootstrap checks, CLI fixture tests, and
  provider default tests passed;
- `sloppy run` startup/failure-mode tests passed without proving V8 execution;
- V8-enabled tests did not necessarily run;
- PostgreSQL and SQL Server live tests did not necessarily run;
- benchmark smoke ran only as harness correctness, not as performance evidence.

Do not convert default-gate success into claims about V8, live databases, HTTP throughput,
public package usability, or production readiness.

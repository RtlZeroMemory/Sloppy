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
| HTTP foundation | Yellow | Route parser/matcher, complete-buffer HTTP/1 request-head parser, libuv init smoke, synthetic GET dispatch. | Default C tests and synthetic dispatch tests. | No sockets, response writer, body parser, request context, streaming parser, or real server. | Build response writer/request context and `sloppy run` server path with tests. |
| Public API ergonomics | Yellow | Bootstrap ESM stdlib supports builder/app, `Results.*`, route groups, schema, modules, services, config, logging, data facade. | Static checks and optional Node ESM tests. | No bare `"sloppy"` import, no compiler extraction, no V8-backed stdlib loading, no `app.run`. | Add compiler extraction, app-plan emission, V8 module loading, and executable Sloppy examples. |
| App host | Yellow | JavaScript-only builder/freeze/config/logging/services skeleton. | Bootstrap tests validate in-memory behavior. | No native app graph, startup validation, request scopes, disposal, or run/listen behavior. | Implement native app graph and runtime startup validation after compiler extraction. |
| Modules | Yellow | JavaScript-only `Sloppy.module`, dependency ordering, phases, attribution, debug metadata. | Bootstrap module tests. | No compiler extraction, package loading, native module graph, middleware/filter phases, or real plan emission. | Emit/validate module plan metadata and load it through runtime startup. |
| Data providers | Yellow | Native SQLite, PostgreSQL, and SQL Server provider boundaries plus bootstrap data API/fake provider. | SQLite live-in-memory tests; PostgreSQL/SQL Server default non-live tests; redaction and option tests. | PostgreSQL and SQL Server live tests require env vars; JS-to-native bridge, resource IDs, pooling, async offload, and capability enforcement are missing. | Add JS-native resource bridge, live test infrastructure, pooling hardening, async strategy, and capability policy. |
| CLI | Yellow | Metadata-only `routes`, `doctor`, `audit`, and `openapi` commands. | Golden process tests over fixtures. | Commands do not compile, run handlers, start HTTP, enter V8, or run live provider checks. | Wire to compiler/app-host metadata and add opt-in live diagnostics. |
| Benchmarks | Yellow | `sloppy_bench`, route/parser/handler/synthetic dispatch scenarios, JSON/text output, wrapper. | List/smoke CTest checks. | No performance regression gate; no real HTTP/V8/JSON/live DB/external comparisons. | Add release benchmark methodology, trend tracking, real paths, and only then external comparisons. |
| Cross-platform readiness | Red | Cross-platform layout and platform boundary policy exist. | Windows-first gates only. | Linux/macOS CI, Unix tool wrappers, provider matrices, and package smoke tests are absent. | Add EPIC-26 CI expansion before public alpha claims. |
| Distribution readiness | Red | Build/package docs and V8 packaging helper exist. | Default local build/package pieces are partially covered. | No release packaging matrix, checksums, install scripts, SDK hosting, or external install validation. | Add EPIC-25 release package layout, artifacts, checksum, and smoke validation. |
| Security / capability model | Red | Capability metadata and audit concepts exist. | Some metadata/audit fixture checks. | No runtime enforcement, filesystem/network policy, provider access policy, or permission gate. | Implement EPIC-27 enforcement and diagnostics before public alpha. |

## Current Summary

The repo has a surprisingly broad foundation now, but public-alpha readiness is still red
because the pieces do not yet form an executable app path. The highest-confidence areas are
portable core primitives and default Windows gates. The riskiest gaps are V8 validation,
compiler extraction, `sloppy run`, response/request handling, capability enforcement,
cross-platform CI, and release packaging.

## Gate Interpretation

Passing the default Windows gates means:

- portable C foundations, non-V8 tests, static/bootstrap checks, CLI fixture tests, and
  provider default tests passed;
- V8-enabled tests did not necessarily run;
- PostgreSQL and SQL Server live tests did not necessarily run;
- benchmark smoke ran only as harness correctness, not as performance evidence.

Do not convert default-gate success into claims about V8, live databases, HTTP throughput,
public package usability, or production readiness.

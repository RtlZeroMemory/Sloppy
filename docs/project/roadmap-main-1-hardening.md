# ROADMAP MAIN.1: Alpha-Production Hardening

Status: issue-ready hardening roadmap.

MAIN.1 goal: harden existing supported and semi-supported systems from MVP/skeleton to
alpha-production quality after MAIN works.

Alpha-production means the supported path works end to end, functionality is real, docs are
honest, diagnostics are usable, tests cover intended behavior, unsupported behavior fails
clearly, and skeletons in the supported path are hardened enough to trust.

## MAIN.1 Non-Goals

- No Node compatibility.
- No package-manager behavior.
- No public marketing launch docs before the hardening exit criteria pass.
- No production feature breadth unless an EPIC explicitly says it is part of alpha.
- No fake success paths.

## EPIC MAIN1-01: Compiler Hardening and Supported Syntax Matrix

Goal: define and harden the compiler's alpha-supported source shapes.

Why it exists: EPIC-21 proved one narrow extraction MVP; MAIN.1 needs a documented matrix
of what is supported and what fails.

Prerequisites: MAIN executable artifact path.

Task breakdown:

- MAIN1-01.A: supported syntax matrix and negative-fixture plan.
- MAIN1-01.B: source span diagnostics for unsupported syntax.
- MAIN1-01.C: deterministic handler/route ordering and duplicate detection review.
- MAIN1-01.D: source-input `sloppy run` handoff design. MAIN1-01 chooses the explicit
  two-step artifact workflow for alpha and defers direct source input implementation.

Non-goals: full TypeScript checker, npm resolution, broad bundling.

Files likely touched: `compiler/src/sloppyc.rs`, `compiler/tests/fixtures/`,
`docs/compiler.md`, `docs/public/getting-started.md`.

Tests required: Cargo golden tests, diagnostics fixtures, Rust standards, CTest compiler
smoke.

Acceptance criteria: every supported syntax has a fixture; every rejected syntax fails
clearly; no partial extraction silently succeeds.

Risk: high.

Suggested PR grouping: split matrix/docs, diagnostics, and source-input handoff.

Advanced features included: source spans and deterministic diagnostics.

Deferred items: full TS checking, npm/package resolution.

Existing issue references: EPIC-21 is done; do not duplicate PR #124.

## EPIC MAIN1-02: Plan Schema and Compatibility Hardening

Goal: make the Sloppy Plan a trustworthy alpha contract.

Why it exists: the native parser owns minimal Plan v1 while route metadata is interim.

Prerequisites: compiler artifact path and runtime route consumption.

Task breakdown:

- MAIN1-02.A: route section becomes native plan-owned.
- MAIN1-02.B: module/provider/capability sections get compatibility fixtures.
- MAIN1-02.C: bundle/source-map/hash validation.
- MAIN1-02.D: plan version/runtime compatibility diagnostics.

Non-goals: full app graph for all future features.

Files likely touched: `include/sloppy/plan.h`, `src/core/plan_parse.c`, `tests/golden/plan/`,
`docs/app-plan.md`, `docs/modules/plan/README.md`.

Tests required: golden valid/invalid fixtures, parser rollback tests, CLI fixture updates.

Acceptance criteria: alpha plan sections are parsed, validated, documented, and covered.

Risk: high.

Suggested PR grouping: route section first, then provider/capability, then hashes/maps.

Advanced features included: compatibility and artifact integrity checks.

Deferred items: dynamic runtime discovery.

Existing issue references: #7 should close for prior scope; create new hardening issues.

## EPIC MAIN1-03: Runtime App Host Hardening

Goal: move app-host concepts from bootstrap-only state toward runtime startup validation.

Why it exists: config/logging/services/modules are useful JS skeletons but not native host
contracts yet.

Prerequisites: Plan sections for app metadata.

Task breakdown:

- MAIN1-03.A: native app graph startup validation.
- MAIN1-03.B: module ordering and service token diagnostics from plan metadata.
- MAIN1-03.C: request-scope lifecycle design for handlers.
- MAIN1-03.D: disposal/cleanup hooks tied to `SlScope` or resource table.

Non-goals: full dependency injection framework, dynamic module packages.

Files likely touched: `stdlib/sloppy/app.js`, `docs/modules/app-host/README.md`,
`src/core/`, `tests/bootstrap/`, `tests/integration/`.

Tests required: bootstrap behavior tests, plan/app-host startup diagnostics, lifecycle tests.

Acceptance criteria: supported app-host metadata is validated before serving.

Risk: high.

Suggested PR grouping: validation first, lifecycle second.

Advanced features included: request-scope ownership boundaries.

Deferred items: rich DI/config providers.

Existing issue references: EPIC-12 and EPIC-14 are completed bootstrap scope; avoid
duplicating them.

## EPIC MAIN1-04: HTTP Runtime Hardening

Goal: harden the alpha HTTP request/response path.

Why it exists: EPIC-23 provides a narrow response/context MVP, not a production HTTP model.

Prerequisites: MAIN hello path.

Task breakdown:

- MAIN1-04.A: route table/precedence and ambiguity diagnostics.
- MAIN1-04.B: request context limits, headers, and unsupported body diagnostics.
- MAIN1-04.C: result serialization policy and malformed descriptor diagnostics.
- MAIN1-04.D: safe server lifecycle and shutdown behavior for the dev server.

Non-goals: TLS, streaming/files, middleware, production server breadth.

Files likely touched: `include/sloppy/http*.h`, `src/core/http*.c`, `src/main.c`,
`tests/unit/core/test_http*.c`.

Tests required: unit tests, integration dispatch tests, malformed input fixtures.

Acceptance criteria: supported HTTP behavior is clear and unsupported behavior fails
deterministically.

Risk: high.

Suggested PR grouping: route table, context, response, lifecycle.

Advanced features included: route precedence and context limits.

Deferred items: production HTTP feature breadth.

Existing issue references: EPIC-23 tasks #132-#136 are closed; do not duplicate them.

## EPIC MAIN1-05: V8 Runtime Hardening

Goal: harden the engine bridge and bootstrap runtime for alpha execution.

Why it exists: V8 works behind a gate, but default builds do not prove it and true module
loading remains deferred.

Prerequisites: V8 SDK availability for validation.

Task breakdown:

- MAIN1-05.A: owner-thread and shutdown policy.
- MAIN1-05.B: promise/microtask and async result policy.
- MAIN1-05.C: source map handoff for exceptions.
- MAIN1-05.D: decide whether alpha requires true ESM bootstrap loading or documented
  classic runtime artifacts.

Non-goals: multiple engine backends, Node APIs, package resolution.

Files likely touched: `include/sloppy/engine.h`, `src/engine/v8/engine_v8.cc`,
`stdlib/sloppy/internal/runtime-classic.js`, `docs/modules/engine-v8/README.md`.

Tests required: V8-gated integration tests and default skip/failure diagnostics.

Acceptance criteria: V8 success is reported only when V8-gated tests run; engine ownership
rules are documented and tested.

Risk: high.

Suggested PR grouping: ownership/shutdown, async policy, source maps, module decision.

Advanced features included: promise/microtask policy if alpha needs async handlers.

Deferred items: snapshots and multi-worker scaling.

Existing issue references: EPIC-24 tasks #137-#141 are closed.

## EPIC MAIN1-06: Diagnostics Completion

Goal: make diagnostics usable for alpha developers and tools.

Why it exists: text diagnostics exist, but JSON/source frames/source maps are unfinished.
Current MAIN1-06 issue scope covers JSON diagnostics, source-frame rendering, and redaction
audit. V8 exception source-map remapping remains owned by MAIN1-05.

Prerequisites: compiler/runtime error paths identified.

Task breakdown:

- MAIN1-06.A: JSON diagnostic renderer.
- MAIN1-06.B: source-frame renderer and snapshot fixtures.
- MAIN1-06.C: redaction policy coverage across CLI/providers/runtime.

Non-goals: localization or broad fix-it framework.

Files likely touched: `include/sloppy/diagnostics.h`, `src/core/diagnostics.c`,
`tests/golden/diagnostics/`, `docs/diagnostics.md`.

Tests required: golden snapshots, redaction tests, JSON renderer tests, and compiler
diagnostic fixture tests.

Acceptance criteria: alpha errors can be read by humans and tools without leaking secrets.

Risk: medium.

Suggested PR grouping: JSON, source frames, redaction.

Advanced features included: machine-readable JSON diagnostics and source-frame rendering.

Deferred items: localization, CLI-wide diagnostic format flag, richer compiler spans, and
MAIN1-05 V8 exception source-map remapping.

Existing issue references: #34 remains open.

## EPIC MAIN1-07: Resource Lifecycle and JS-Native Handles

Goal: implement safe native resource IDs for JS-visible resources.

Why it exists: native providers exist but JS cannot safely hold raw pointers.

Prerequisites: `SlScope` and memory ownership docs.

Task breakdown:

- MAIN1-07.A: `SlResourceId` layout and resource table. Implemented by the MAIN1-07
  resource lifecycle PR.
- MAIN1-07.B: kind/generation/stale-handle validation. Implemented by the MAIN1-07
  resource lifecycle PR.
- MAIN1-07.C: cleanup/leak diagnostics. Cleanup callbacks and deterministic
  stale/wrong-kind diagnostics are implemented; app/request leak reporting remains a
  follow-up.
- MAIN1-07.D: JS bridge handle conventions. Implemented as bridge policy docs; provider
  bridge consumption remains MAIN1-08.

Non-goals: native plugin ABI.

Files likely touched: `include/sloppy/`, `src/core/`, `docs/memory.md`,
`docs/modules/resource/README.md`, `tests/unit/core/`.

Tests required for MAIN1-07: stale ID, wrong kind, double close, cleanup, and exhaustion.
Request-scope lifetime and leak reporting are handled under app-host lifecycle work, not
MAIN1-07.

Acceptance criteria: JS-visible native resources use IDs and generation checks. MAIN1-07
establishes the core table and policy; MAIN1-08 consumes it for SQLite JS-native handles.

Risk: high.

Suggested PR grouping: core table, diagnostics, JS conventions.

Advanced features included: leak reports.

Deferred items: cross-process resource isolation.

Existing issue references: #35 remains open.

## EPIC MAIN1-08: SQLite JS-to-Native Data Bridge

Goal: make one database story executable through real Sloppy, starting with SQLite.

Why it exists: native SQLite is real but `data.sqlite.open` intentionally fails in JS.

Prerequisites: resource table and capability policy.

Task breakdown:

- MAIN1-08.A: SQLite open/query/exec intrinsics.
- MAIN1-08.B: JS resource wrapper with safe IDs.
- MAIN1-08.C: transaction lifecycle and cleanup tests.
- MAIN1-08.D: executable SQLite example or explicit deferral if blocked.

Non-goals: ORM, migrations, PostgreSQL/SQL Server JS bridge in this EPIC.

Files likely touched: `stdlib/sloppy/data.js`, `src/data/sqlite.c`, `src/engine/v8/`,
`examples/sqlite-basic/`, `docs/public/data.md`.

Tests required: unit provider tests, V8-gated JS bridge tests, example conformance.

Acceptance criteria: alpha SQLite behavior is real or public docs explicitly defer it.

Risk: high.

Suggested PR grouping: intrinsics, JS wrapper, executable example.

Advanced features included: transaction lifecycle through JS.

Deferred items: migrations and advanced types.

Existing issue references: do not duplicate EPIC-16 native provider tasks #71-#74.

## EPIC MAIN1-09: Provider Hardening

Goal: harden provider boundaries and live-test reporting for alpha.

Why it exists: native providers are real, but live PostgreSQL/SQL Server confidence is
opt-in and packaging/runtime dependency policy remains incomplete.

Prerequisites: provider MVPs.

Task breakdown:

- MAIN1-09.A: live provider test service strategy and explicit skip reporting.
- MAIN1-09.B: pooling health/close/drain diagnostics.
- MAIN1-09.C: runtime dependency packaging notes for libpq/ODBC/SQLite.
- MAIN1-09.D: provider redaction coverage audit.

Non-goals: full provider feature parity.

Files likely touched: `src/data/`, `tests/unit/data/`, `.github/workflows/ci.yml`,
`docs/data-providers.md`, `docs/dependencies.md`.

Tests required: default non-live tests, opt-in live tests, redaction tests.

Acceptance criteria: provider claims distinguish native/default/live/JS-bridge evidence.

Risk: high.

Suggested PR grouping: CI/live reporting, pooling, packaging.

Advanced features included: provider health diagnostics.

Deferred items: advanced SQL features.

Existing issue references: EPIC-17 and EPIC-18 native scopes are closed.

## EPIC MAIN1-10: Capability and Security Hardening

Goal: enforce declared capabilities for supported alpha resource access.

Why it exists: metadata exists but enforcement does not.

Prerequisites: plan capability section and resource table.

Task breakdown:

- MAIN1-10.A: capability plan section and parser validation.
- MAIN1-10.B: runtime capability registry.
- MAIN1-10.C: provider access policy.
- MAIN1-10.D: filesystem/network capability skeletons.
- MAIN1-10.E: denied-access diagnostics and audit fixtures.

Non-goals: OS sandboxing or strong isolation claims.

Files likely touched: `docs/security-permissions.md`, `docs/public/permissions.md`,
`src/core/`, `src/data/`, `tests/golden/cli/`.

Tests required: denied access, missing capability, audit fixtures, redaction.

Acceptance criteria: supported provider/filesystem/network access is gated or clearly
unsupported.

Risk: high.

Suggested PR grouping: follow existing #152-#156 task split.

Advanced features included: denied diagnostics and audit fixtures.

Deferred items: OS sandboxing.

Existing issue references: #130 and #152-#156 remain open.

## EPIC MAIN1-11: CLI Introspection Hardening

Goal: make CLI introspection reflect real compiler/app-host metadata.

Why it exists: current CLI commands are metadata-fixture MVPs.

Prerequisites: hardened Plan metadata.

Task breakdown:

- MAIN1-11.A: consume compiler-emitted route/module/provider/capability metadata.
- MAIN1-11.B: richer audit rules for alpha safety.
- MAIN1-11.C: OpenAPI schema policy for supported validation shapes.
- MAIN1-11.D: live provider checks behind explicit flags.

Non-goals: full OpenAPI generator or live checks by default.

Files likely touched: `src/main.c`, `tests/fixtures/cli/`, `tests/golden/cli/`,
`docs/public/cli.md`.

Tests required: golden text/JSON tests, fixture tests, redaction tests.

Acceptance criteria: CLI output is deterministic, honest, and tied to real metadata.

Risk: medium.

Suggested PR grouping: metadata ingestion, audit, OpenAPI, live checks.

Advanced features included: alpha audit rules.

Deferred items: complete OpenAPI/security schema.

Existing issue references: EPIC-19 is completed for metadata MVP.

## EPIC MAIN1-12: Packaging and Cross-Platform Hardening

Goal: make package and CI evidence reliable enough for alpha.

Why it exists: local package MVP and default hosted CI exist, but optional paths remain
manual/gated.

Prerequisites: MAIN evidence model.

Task breakdown:

- MAIN1-12.A: Linux/macOS package smoke in CI or explicit deferral.
- MAIN1-12.B: V8 runtime packaging strategy and validation.
- MAIN1-12.C: sanitizer/fuzz gate plan for parsers/resource code.

Non-goals: installers, signing, package-manager distribution.

Files likely touched: `.github/workflows/ci.yml`, `tools/windows/package.ps1`,
`tools/unix/package.sh`, `docs/build-and-distribution.md`.

Tests required: package smoke, default CI, optional gate reports.

Acceptance criteria: package and CI docs say exactly what each gate proves.

Risk: high.

Suggested PR grouping: package smoke, V8 packaging, live services, sanitizers.

Advanced features included: sanitizer/fuzz planning.

Deferred items: signing/notarization.

Existing issue references: EPIC-25/26 closed; create hardening only.

## EPIC MAIN1-13: End-to-End Conformance Suite

Goal: cover supported alpha workflows end to end.

Why it exists: unit/golden tests are strong, but alpha needs workflow-level conformance.

Prerequisites: MAIN path and selected MAIN.1 features.

Task breakdown:

- MAIN1-13.A: executable hello conformance.
- MAIN1-13.B: request context conformance.
- MAIN1-13.C: SQLite bridge conformance if included.
- MAIN1-13.D: unsupported behavior conformance.

Non-goals: broad framework scenario matrix.

Files likely touched: `tests/integration/`, `examples/`, `CMakeLists.txt`,
`docs/testing.md`.

Tests required: CTest integration tests, V8-gated tests, default unsupported-path tests.

Acceptance criteria: every public alpha example is covered or explicitly marked static.

Risk: high due to V8 gating.

Suggested PR grouping: hello, HTTP, data, unsupported paths.

Advanced features included: conformance fixtures.

Deferred items: load/performance conformance.

Existing issue references: new MAIN.1 coverage, not duplicate of EPIC-23/24 unit tests.

## EPIC MAIN1-14: Benchmarks and Performance Methodology

Goal: make performance measurement honest without public comparison claims.

Why it exists: benchmark harness exists, but smoke tests are not performance evidence.

Prerequisites: stable supported runtime path.

Task breakdown:

- MAIN1-14.A: release-only local run methodology.
- MAIN1-14.B: benchmark environment metadata.
- MAIN1-14.C: real HTTP/V8 path benchmark only after those paths are stable.
- MAIN1-14.D: docs that prohibit external comparison claims before comparable paths exist.

Non-goals: dashboard, CI regression thresholds, public comparisons.

Files likely touched: `benchmarks/`, `tools/windows/bench.ps1`, `docs/quality-score.md`,
`docs/tech-debt-tracker.md`.

Tests required: benchmark smoke/list checks, wrapper JSON test.

Acceptance criteria: benchmark docs and outputs cannot be mistaken for public claims.

Risk: medium.

Suggested PR grouping: methodology first, real scenarios later.

Advanced features included: benchmark metadata.

Deferred items: dashboards and external comparisons.

Existing issue references: EPIC-20 closed for harness.

## EPIC MAIN1-15: Public Alpha Documentation and Examples

Goal: publish public alpha docs only for verified, executable workflows.

Why it exists: EPIC-28 is conceptually premature unless it follows MAIN.1 hardening gates.

Prerequisites: MAIN and selected MAIN.1 hardening exit criteria.

Task breakdown:

- MAIN1-15.A: executable hello tutorial.
- MAIN1-15.B: executable SQLite demo or explicit deferral.
- MAIN1-15.C: README and public docs refresh.
- MAIN1-15.D: troubleshooting and V8 SDK caveats.
- MAIN1-15.E: public alpha checklist.

Non-goals: marketing launch, package-manager docs, Node compatibility docs.

Files likely touched: `README.md`, `docs/public/`, `examples/compiler-hello/`,
`examples/sqlite-basic/`, `docs/quality-score.md`.

Tests required: example conformance, docs/static checks, V8-gated execution where claimed.

Acceptance criteria: every public example either runs through the real Sloppy toolchain or
is clearly labeled as API-shape/static/deferred.

Risk: medium.

Suggested PR grouping: hello first, SQLite decision second, README/public docs last.

Advanced features included: public alpha checklist.

Deferred items: marketing launch docs.

Existing issue references: #131 and #157-#161 should be re-scoped behind MAIN.1.

## MAIN.1 Exit

MAIN.1 exits when:

- supported workflows are executable and covered;
- unsupported workflows fail clearly;
- diagnostics are usable;
- resource/capability/data bridge behavior is real for supported resources;
- public docs are honest and not marketing-forward;
- issue tracker state no longer invites duplicate completed work.

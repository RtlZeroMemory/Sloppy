# Next Roadmap: EPIC-23 Through EPIC-28

## Purpose

This document defines the remaining coherent roadmap batch after the EPIC-00 through
EPIC-22 foundation, compiler extraction, dev-run, provider, CLI, and benchmark work.

The batch is deliberately integration-heavy. The repo now has the smallest compiler ->
plan -> runtime -> HTTP path; the next step is to harden the response/request boundary,
V8 bootstrap loading, packaging, CI, capabilities, and public-alpha documentation without
pretending Sloppy is production ready.

## Completed Before This Batch

- EPIC-21 Compiler Extraction MVP is complete and should not be recreated as a new GitHub
  issue in this ceremony pass.
- EPIC-22 Sloppy Run MVP is complete and should not be recreated as a new GitHub issue in
  this ceremony pass.

## Non-Goals For The Batch

- No production HTTP server before the dev-only run path exists.
- No package manager behavior.
- No Node compatibility goal.
- No broad provider expansion.
- No fake performance claims.
- No native plugin ABI.
- No dynamic module system.
- No full TypeScript type system unless the selected compiler slice explicitly introduces
  it with tests and docs.

## Completed Reference: EPIC-21 Compiler Extraction MVP

Status: complete before this remaining-roadmap issue ceremony pass. This section is kept
as prerequisite context for EPIC-23 onward and should not be recreated as GitHub issue work
from `tools/github/next-roadmap-issues.json`.

Summary: turn a tiny Sloppy source file into deterministic runtime artifacts.

Goal: `sloppyc` can parse/import a small public API source shape, discover app/routes/
handlers, and emit `app.plan.json` plus an `app.js` fixture/bundle that the existing native
runtime can load in later epics.

Non-goals:

- full TypeScript type checking;
- broad bundling;
- dynamic route/module extraction;
- services/data/provider extraction beyond minimal metadata needed for acceptance;
- watch mode or build cache.

Suggested large-coherent task grouping:

- compiler project/file discovery and input contract;
- minimal parser/extractor for `Sloppy.createBuilder`, `Sloppy.create`, `app.mapGet`, and
  `app.mapGroup`;
- handler discovery and stable numeric handler ID assignment;
- deterministic plan writer for current minimal plus route metadata;
- tiny generated `app.js` fixture/bundle and optional source map placeholder;
- compiler diagnostics and golden tests.

Prerequisites:

- current bootstrap public API docs;
- Plan v1 fixture contract;
- route pattern subset docs;
- decision on Oxc introduction for the MVP.

Tests:

- cargo unit tests for CLI/input validation;
- golden `app.plan.json` and `app.js` outputs;
- diagnostics for nonliteral route, missing app, duplicate handler/route name where in
  scope;
- default cargo gates.

Acceptance criteria:

- one tiny source file with `Sloppy.create()` and one `mapGet` emits deterministic
  artifacts;
- grouped GET route emits combined path metadata;
- unsupported dynamic input fails honestly;
- output does not claim full TypeScript checking;
- no runtime server behavior is added in this epic.

## Completed Reference: EPIC-22 Sloppy Run MVP

Status: complete before this remaining-roadmap issue ceremony pass. This section is kept
as prerequisite context for EPIC-23 onward and should not be recreated as GitHub issue work
from `tools/github/next-roadmap-issues.json`.

Summary: make the first dev-only executable app path.

Goal: `sloppy run` builds or loads artifacts, starts a local single-process HTTP server,
routes GET requests, calls a V8 handler, and returns text/json responses.

Non-goals:

- production server hardening;
- HTTPS;
- streaming bodies;
- middleware;
- hot reload;
- package manager integration;
- multi-worker scaling.

Suggested large-coherent task grouping:

- CLI command and artifact/source selection;
- build/load handoff to `sloppyc` or prebuilt `.sloppy` artifacts;
- dev-only single-process HTTP accept/read loop;
- route GET requests through the existing route/dispatch foundation;
- call V8 handler through the runtime-contract path;
- return basic response text/json through EPIC-23 or a tightly bounded temporary writer if
  EPIC-23 is split differently.

Prerequisites:

- EPIC-21 artifacts;
- V8 SDK available for validation;
- HTTP parser and route matching foundations.

Tests:

- process-level smoke for generated hello route;
- missing artifact diagnostics;
- unsupported method diagnostic/response;
- V8-gated integration tests;
- default non-V8 tests must skip or report clearly.

Acceptance criteria:

- a tiny checked-in example can be built and served locally;
- GET `/` returns the expected text or JSON;
- command output is honest about dev-only scope;
- V8-enabled validation is reported separately from default gates.

## EPIC-23: HTTP Response Writer and Request Context

Summary: define the native response/request boundary Sloppy handlers actually use.

Goal: convert result descriptors into HTTP responses and provide a minimal request context
with route params and query params.

Non-goals:

- full body parsing;
- streaming;
- file results;
- cookies;
- content negotiation;
- middleware/filter pipeline;
- production router optimization.

Suggested large-coherent task grouping:

- response descriptor C model;
- status/header/content-type validation;
- text/json response writing;
- route params materialization;
- query parsing and percent-decoding policy;
- minimal request context object for V8 handler calls;
- diagnostics for invalid result/request shapes.

Prerequisites:

- EPIC-10 route/HTTP parser foundation;
- EPIC-13 `Results.*` descriptor docs;
- EPIC-22 run path or integration fixture harness.

Tests:

- response writer unit tests;
- route param and query parsing tests;
- V8-gated handler context integration;
- malformed result diagnostics;
- no response headers/body overlap or double-write path.

Acceptance criteria:

- text and JSON result descriptors produce valid HTTP responses;
- route params from the native matcher are available to handler context;
- query params are available with documented decoding behavior;
- unsupported body/stream/file results fail honestly.

## EPIC-24: V8 Module Loading and Bootstrap Runtime

Summary: move from classic-script smoke toward the real bootstrap/app module model.

Goal: load the source-controlled bootstrap stdlib and app module entrypoint reliably inside
V8, bind runtime intrinsics, and preserve a clear boundary between public JS API and native
host behavior.

Non-goals:

- Node resolution;
- npm package loading;
- dynamic import graph policies beyond the MVP;
- inspector/debugger support;
- snapshots.

Suggested large-coherent task grouping:

- classic-script bootstrap runtime decision and implementation notes, with true ESM
  loading deferred;
- bootstrap stdlib loading from installed/staged assets;
- bare `"sloppy"` compiler rewrite story;
- generated app entrypoint loading;
- handler registration intrinsic;
- source location fallback diagnostics without full source-map fidelity;
- V8-gated bootstrap/runtime integration tests.

Prerequisites:

- V8 SDK validation path;
- bootstrap stdlib layout;
- compiler artifact shape from EPIC-21.

Tests:

- V8-gated classic bootstrap runtime smoke;
- missing stdlib asset diagnostic;
- missing handler registration diagnostic;
- source location fallback diagnostic;
- no `v8::*` leakage outside `src/engine/v8`.

Acceptance criteria:

- V8 loads the classic bootstrap runtime asset and app entrypoint without Node;
- handlers register through documented intrinsics;
- missing or malformed modules produce deterministic diagnostics;
- default non-V8 gates remain honest about not validating this path.

## EPIC-25: Release Packaging and Distribution

Summary: define and test installable artifacts.

Goal: produce honest platform packages with the runtime, compiler, stdlib assets, V8
strategy, provider dependency notes, checksums, and smoke validation from outside the repo.

Non-goals:

- package manager integration;
- auto-update;
- installer UI;
- signed releases unless separately scoped;
- public alpha announcement.

Suggested large-coherent task grouping:

- package layout docs;
- Windows zip package;
- Linux tar package;
- macOS tar package;
- V8 linked/bundled strategy;
- checksum generation;
- outside-checkout smoke tests;
- install scripts later if needed.

Prerequisites:

- stable build output layout;
- V8 SDK/package decision;
- stdlib staging/install rules.

Tests:

- package contents tests;
- no generated/local cache artifacts;
- outside-checkout `sloppy --version`, `sloppyc --version`, and stdlib asset smoke;
- checksum verification.

Acceptance criteria:

- package artifacts are reproducible enough for review;
- no V8 SDK binaries are committed;
- package docs clearly say what is included and what is still optional/gated.

## EPIC-26: Cross-platform CI Expansion

Summary: prove the cross-platform-by-design claim with real gates.

Status: implemented for required non-V8 hosted CI. Windows clang-cl, Linux clang/gcc, and
macOS clang gates exist. Optional V8 and live provider validation are reported separately
and remain gated by explicit SDK/service configuration.

Goal: add Linux and macOS CI jobs alongside Windows and make optional V8/provider gates
explicit.

Non-goals:

- full platform feature parity in one PR;
- hiding optional dependency failures;
- making live DB tests mandatory by default.

Suggested large-coherent task grouping:

- Linux clang/gcc configure/build/test;
- macOS clang configure/build/test;
- Windows clang-cl maintenance;
- V8 optional/gated jobs;
- provider gated tests with clear skip reasons;
- Unix tool wrappers where needed.

Prerequisites:

- platform boundary docs;
- current Windows gate stability;
- decisions on optional provider availability.

Tests:

- CI workflow matrix;
- default non-live provider tests;
- platform scanner on every OS;
- cargo gates on every OS.

Acceptance criteria:

- default CI proves core non-V8 functionality on Windows, Linux, and macOS;
- optional V8/provider gates are labeled and reported honestly;
- platform-specific failures are not hidden as success.

## EPIC-27: Runtime Security / Capabilities Enforcement

Summary: make declared authority mean something.

Goal: enforce declared capabilities for provider access and define filesystem/network
capability skeletons with diagnostics.

Non-goals:

- OS sandboxing;
- broad filesystem API;
- network client/server feature expansion beyond declared policy checks;
- third-party provider trust model.

Suggested large-coherent task grouping:

- capability section in plan;
- runtime capability registry tied to app graph;
- provider access policy;
- filesystem capability skeleton;
- network capability skeleton;
- diagnostics and audit output alignment;
- tests proving denied access fails before work starts.

Prerequisites:

- plan provider/capability emission from compiler/app host;
- resource table or equivalent handle model;
- security docs update.

Tests:

- allowed/denied provider access;
- missing capability diagnostic;
- secret redaction;
- `sloppy audit` fixture updates;
- no raw native pointer exposure.

Acceptance criteria:

- declared capabilities are enforced for at least one real access path;
- denied access fails with deterministic diagnostics;
- docs clearly distinguish capability checks from OS sandboxing.

## EPIC-28: Public Alpha Docs and Examples

Summary: make the public story executable and honest.

Goal: refresh public docs and examples around what a developer can actually run after
EPIC-21 through EPIC-27.

Non-goals:

- marketing benchmarks;
- production support claims;
- provider demos that require credentials by default;
- broad tutorial site.

Suggested large-coherent task grouping:

- executable hello tutorial;
- executable SQLite demo if JS-to-native bridge is ready;
- CLI workflow docs;
- architecture cleanup for implemented-vs-deferred behavior;
- README refresh;
- troubleshooting/V8 SDK caveats;
- public alpha checklist.

Prerequisites:

- `sloppy run` MVP;
- response/request context;
- V8 bootstrap/module loading;
- packaging story.

Tests:

- public examples executed in CI where dependencies are available;
- static checks for docs claims;
- no `sloppy run` claims without executable verification.

Acceptance criteria:

- README and public docs say experimental/not production ready;
- hello example runs through Sloppy, not Node;
- SQLite demo is either executable or explicitly deferred;
- no public docs overclaim package manager, Node compatibility, live DB, security, or
  performance readiness.

## Issue Generation Notes

`tools/github/next-roadmap-issues.json` contains reviewed issue data for the remaining
roadmap work. The GitHub issue scripts support an explicit `-Input` override, so EPIC-23
onward issues can be validated, summarized, dry-run, and applied without copying entries
into `tools/github/issues.json`.

Dry-run before applying:

```powershell
.\tools\github\validate-issue-data.ps1 -Input tools/github/next-roadmap-issues.json
.\tools\github\dry-run-summary.ps1 -Input tools/github/next-roadmap-issues.json
.\tools\github\create-issues.ps1 -Input tools/github/next-roadmap-issues.json -DryRun
```

Mutation remains explicit through `-Apply`, and issue creation skips exact-title matches
instead of duplicating existing issues. EPIC-21 and EPIC-22 are already complete and are
intentionally excluded from the next-roadmap issue input.

# ROADMAP MAIN: Minimal End-to-End Alpha Path

Status: issue-ready roadmap for the minimal alpha path.

MAIN goal: a developer can write a tiny supported Sloppy app, compile it, run it locally,
receive HTTP responses, and understand what works and what does not.

MAIN is intentionally short because EPIC-21 through EPIC-26 already landed for their MVP
scopes. MAIN must not recreate those tasks. It should verify the path, fix stale status
docs, and prepare the issue tracker for MAIN.1.

## Completed Baseline: Do Not Duplicate

- EPIC-21 Compiler Extraction MVP: complete via PR #124.
- EPIC-22 Sloppy Run MVP: complete via PR #125.
- EPIC-23 HTTP Response Writer and Request Context: complete via PR #163.
- EPIC-24 V8 Module Loading and Bootstrap Runtime: complete via PR #165 for the classic
  bootstrap runtime, not true ESM module graph loading.
- EPIC-25 Release Packaging and Distribution: complete via PR #162 for experimental local
  packages.
- EPIC-26 Cross-platform CI Expansion: complete via PR #164 and follow-up PR #166 for
  default non-V8 hosted CI and optional/gated reporting.

## MAIN Non-Goals

- No runtime/compiler/provider feature expansion beyond final verification.
- No broad compiler syntax support.
- No source-input run handoff unless MAIN is later amended.
- No production HTTP server.
- No JS-to-native database bridge.
- No capability enforcement.
- No public marketing docs.
- No GitHub issue mutations from this PR.

## EPIC MAIN-01: Executable Artifact-Path Alpha Verification

Goal: prove the supported two-step alpha path with a tiny app:
`sloppyc build` then `sloppy run --artifacts`.

Why it exists: EPIC-21 through EPIC-24 landed the parts, but MAIN needs one locked,
documented, end-to-end verification target that does not overclaim unsupported workflows.

Prerequisites:

- EPIC-21 through EPIC-24 merged.
- V8-enabled local build available for positive runtime execution.
- Default non-V8 build available for unsupported-path diagnostics.

Task breakdown:

- MAIN-01.A: define the exact supported hello source shape and artifact directory contract.
- MAIN-01.B: verify `sloppyc build` emits deterministic `app.plan.json`, `app.js`, and
  `app.js.map` for the hello source.
- MAIN-01.C: verify V8-enabled `sloppy run --artifacts --once GET /` returns the expected
  HTTP response.
- MAIN-01.D: verify non-V8 builds fail with clear diagnostics rather than fake success.

Non-goals:

- no source-input `sloppy run <source.js>`;
- no true ESM bootstrap module graph;
- no additional compiler syntax.

Files likely touched:

- `examples/compiler-hello/`;
- `docs/public/getting-started.md`;
- `docs/public/cli.md`;
- `tests/integration/execution/`;
- V8-gated CMake tests only if a missing verification hook is found.

Tests required:

- focused compiler golden test if the hello fixture changes;
- V8-gated run verification where the SDK is available;
- default non-V8 diagnostic test if behavior changes;
- `git diff --check`.

Acceptance criteria:

- the documented MAIN hello path is executable in a V8-enabled build;
- default non-V8 behavior is clear and separately reported;
- no unsupported public example is promoted as executable.

### MAIN-01 Supported Hello Contract

MAIN supports exactly the existing executable artifact workflow, using
`examples/compiler-hello/app.js` as the canonical source fixture:

```js
import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.mapGet("/", () => Results.text("Hello from Sloppy"));

export default app;
```

The supported command sequence is:

```powershell
sloppyc build examples/compiler-hello/app.js --out .sloppy-main-smoke
sloppy run --artifacts .sloppy-main-smoke --once GET /
```

The first command emits deterministic artifacts under the requested output directory:

```text
.sloppy-main-smoke/
  app.plan.json
  app.js
  app.js.map
```

`app.plan.json` targets V8, declares bundle path `app.js`, source map path `app.js.map`,
assigns stable handler ID `1`, and records one `GET /` route bound to handler ID `1`.
`app.js` is the generated classic-script artifact. It reads `Results` from the
runtime-loaded classic bootstrap asset and registers handler ID `1` through
`__sloppy_register_handler`. `app.js.map` remains a deterministic placeholder.

The expected `--once GET /` response body is:

```text
Hello from Sloppy
```

Runtime execution requires a V8-enabled build and a configured bootstrap stdlib root.
Default non-V8 builds must fail clearly with the V8-required diagnostic; they prove
unsupported-path behavior, not positive V8 execution. Source-input `sloppy run <source.js>`
is deferred; MAIN runs artifact directories only. Dynamic route patterns, arbitrary bare
imports such as `express`, `fs`, or `node:fs`, package resolution, npm/package-manager
behavior, and Node compatibility are unsupported and must fail or remain documented as
deferred. Node/npm/package-manager behavior is not part of MAIN.

Risk: high because V8 validation is environment-gated.

Suggested PR grouping: one bounded verification/docs PR.

Existing issues/PRs to avoid duplicating:

- PR #124, PR #125, PR #163, PR #165;
- do not recreate EPIC-21, EPIC-22, EPIC-23, or EPIC-24 implementation tasks.

## EPIC MAIN-02: MAIN Evidence and Gate Report

Goal: lock the evidence model for MAIN so default, V8, package, and provider validations
are reported honestly.

Why it exists: default CI proves non-V8 behavior, while MAIN's positive runtime path needs
V8. The docs must keep those claims separate.

Prerequisites:

- EPIC MAIN-01 verification target.
- Current Windows and hosted CI gate docs.

Task breakdown:

- MAIN-02.A: document which commands prove default non-V8 behavior.
- MAIN-02.B: document which commands prove V8-enabled hello execution.
- MAIN-02.C: document what package smoke proves and what it does not.
- MAIN-02.D: keep live provider checks explicitly out of MAIN unless enabled and reported.

Non-goals:

- no new CI service matrix;
- no V8 SDK distribution;
- no public release automation.

Files likely touched:

- `docs/quality-score.md`;
- `docs/quality-gates.md`;
- `README.md`;
- `docs/project/roadmap-main.md`.

Tests required:

- docs-only validation and standard docs/lint gates.

Acceptance criteria:

- every gate statement says whether it proves default, V8, live provider, package, or
  benchmark behavior;
- no default gate is described as proving V8 or live database success.

Risk: medium; the main risk is overclaiming.

Suggested PR grouping: can land with MAIN-01 if it stays docs-only.

Existing issues/PRs to avoid duplicating:

- PR #162, PR #164, PR #166.

## EPIC MAIN-03: Issue Cleanup Application Readiness

Goal: prepare the tracker for MAIN.1 by closing or relabeling stale items after review.

Why it exists: stale parent EPICs make already-completed work look open and encourage
duplicate tasks.

Prerequisites:

- `docs/project/archive/post-alpha-transition/post-core-mvp-issue-reconciliation.md`;
- human approval to mutate issues.

Task breakdown:

- MAIN-03.A: review and approve parent EPIC closure list.
- MAIN-03.B: review old open task recommendations #22-#29, #32, #34, #35.
- MAIN-03.C: review EPIC-28 public-doc deferral/re-scope.
- MAIN-03.D: apply approved close/relabel commands manually or through a future scoped
  issue-cleanup task.

Non-goals:

- no issue mutation in this PR;
- no automatic issue creation;
- no closing issues without human approval.

Files likely touched:

- `docs/project/archive/post-alpha-transition/post-core-mvp-issue-reconciliation.md`;
- `tools/github/roadmap-main-issues.json`.

Tests required:

- `tools/github/validate-issue-data.ps1`;
- `tools/github/dry-run-summary.ps1` for staged issue data.

Acceptance criteria:

- cleanup actions are explicit and dry-run style;
- staged MAIN issue data avoids completed EPIC-21 through EPIC-26 work.

Risk: medium because GitHub mutations are irreversible enough to require human review.

Suggested PR grouping: docs-only cleanup plan PR first, mutation PR/task later.

Existing issues/PRs to avoid duplicating:

- all closed child tasks listed in
  `docs/project/archive/post-alpha-transition/post-core-mvp-issue-reconciliation.md`.

## MAIN Exit

MAIN exits when:

- EPIC MAIN-01 proves the minimal executable artifact path;
- EPIC MAIN-02 locks honest evidence language;
- EPIC MAIN-03 cleanup recommendations are approved or explicitly deferred;
- MAIN.1 issue data has been reviewed and can be created without duplicate completed work.

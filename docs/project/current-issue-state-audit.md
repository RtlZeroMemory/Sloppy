# Current Issue State Audit

Status: read-only GitHub audit for MAIN and MAIN.1 planning.

Date: 2026-04-29.

Repository: `RtlZeroMemory/Slop`.

Commands used for the live snapshot:

```powershell
gh issue list --repo RtlZeroMemory/Slop --state open --limit 300
gh issue list --repo RtlZeroMemory/Slop --state closed --limit 300
gh issue list --repo RtlZeroMemory/Slop --state all --limit 300
gh pr list --repo RtlZeroMemory/Slop --state merged --limit 80
```

This document does not mutate GitHub. It is a recommended cleanup plan only.

## Snapshot

The live issue tracker is noisy:

- 48 open issues were visible in the read-only snapshot.
- 78 closed issues were visible in the read-only snapshot.
- 27 parent EPIC issues remained open.
- 21 task issues remained open.
- all closed issues in the snapshot still carried at least one `status:*` label.
- no exact duplicate issue titles were found by the read-only issue list.

The biggest cleanup issue is not duplicate titles. It is stale parent EPICs and stale staged
issue data after implementation PRs already landed.

## Open Parent EPICs With All Child Tasks Closed

| Issue | Title | State | Recommended action | Reason | Evidence |
| ---: | --- | --- | --- | --- | --- |
| #3 | EPIC-02: Core Native Foundation | Open | Close as completed | The only child task, #30, is closed and PR #91 merged the scoped core primitive foundation. | `include/sloppy/status.h`, `include/sloppy/string.h`, `tests/unit/core/test_status.c`, `tests/unit/core/test_string.c`; PR #91. |
| #7 | EPIC-06: Plan Schema and Loader | Open | Close as completed for scoped foundation | Child tasks #37, #38, and #39 are closed. | `include/sloppy/plan.h`, `src/core/plan_parse.c`, `tests/golden/plan/`, PRs #96-#98. |
| #8 | EPIC-07: V8 Bridge Smoke | Open | Close as completed with V8-gated caveat | Child tasks #40-#43 are closed. The bridge remains optional and V8-gated, but the scoped smoke EPIC landed. | `include/sloppy/engine.h`, `src/engine/v8/engine_v8.cc`, `tests/unit/engine/test_v8_smoke.c`, PRs #99-#102. |
| #9 | EPIC-08: Handwritten Artifact Execution | Open | Close as completed with V8-gated caveat | Child task #44 is closed. | `tests/integration/execution/handwritten_smoke/`, `src/core/runtime_contract.c`, PR #103. |
| #10 | EPIC-09: Event Loop / Concurrency Foundation | Open | Close as completed for skeleton scope | Child tasks #45-#47 are closed. | `include/sloppy/loop.h`, `include/sloppy/async.h`, `include/sloppy/worker_pool.h`, `tests/unit/core/test_loop.c`, PRs #104-#106. |
| #11 | EPIC-10: HTTP Router Foundation | Open | Close as completed for foundation scope | Child tasks #48-#50 are closed. | `include/sloppy/route.h`, `include/sloppy/http.h`, `include/sloppy/http_dispatch.h`, PRs #107-#109. |
| #12 | EPIC-11: Public TypeScript API Bootstrap | Open | Close as completed for bootstrap scope | Child tasks #51-#54 are closed. | `stdlib/sloppy/`, `examples/hello/`, PRs #110-#112. |
| #13 | EPIC-12: App Host Foundation | Open | Close as completed for bootstrap scope | Child tasks #55-#58 are closed. | `stdlib/sloppy/app.js`, `tests/bootstrap/test_app_host_foundation.mjs`, PR #113. |
| #14 | EPIC-13: Developer Ergonomics Layer | Open | Close as completed for bootstrap scope | Child tasks #59-#62 are closed. | `stdlib/sloppy/results.js`, `stdlib/sloppy/schema.js`, `examples/ergonomics/`, PR #114. |
| #15 | EPIC-14: Modularity / App Modules | Open | Close as completed for bootstrap scope | Child tasks #63-#66 are closed. | `stdlib/sloppy/app.js`, `tests/bootstrap/test_modules.mjs`, PR #115. |
| #16 | EPIC-15: Data / Capabilities Foundation | Open | Close as completed for metadata/fake-provider scope | Child tasks #67-#70 are closed. Runtime enforcement remains separate. | `stdlib/sloppy/data.js`, `tests/bootstrap/test_data_foundation.mjs`, PR #116. |
| #17 | EPIC-16: SQLite Provider | Open | Close as completed for native provider scope | Child tasks #71-#74 are closed. JS-to-native bridge remains a later hardening item. | `src/data/sqlite.c`, `tests/unit/data/test_sqlite.c`, PR #117. |
| #18 | EPIC-17: PostgreSQL Provider | Open | Close as completed for native provider scope | Child tasks #75-#78 are closed. Live tests remain opt-in. | `src/data/postgres.c`, `tests/unit/data/test_postgres.c`, PR #118. |
| #19 | EPIC-18: SQL Server Provider | Open | Close as completed for native provider scope | Child tasks #79-#82 are closed. Live tests remain opt-in. | `src/data/sqlserver.c`, `tests/unit/data/test_sqlserver.c`, PR #119. |
| #20 | EPIC-19: CLI Introspection Tooling | Open | Close as completed for metadata MVP scope | Child tasks #83-#86 are closed. | `src/main.c`, `tests/golden/cli/`, PR #121. |
| #21 | EPIC-20: Benchmarks / Performance Validation | Open | Close as completed for harness scope | Child tasks #87-#90 are closed. | `benchmarks/`, `tools/windows/bench.ps1`, PR #120. |
| #126 | EPIC-23: HTTP Response Writer and Request Context | Open | Close as completed | Child tasks #132-#136 are closed. | `include/sloppy/http_response.h`, `include/sloppy/http_context.h`, `tests/unit/core/test_http_response.c`, PR #163. |
| #127 | EPIC-24: V8 Module Loading and Bootstrap Runtime | Open | Close as completed for classic bootstrap runtime scope | Child tasks #137-#141 are closed. True ESM module loading remains deferred. | `stdlib/sloppy/internal/runtime-classic.js`, `src/engine/v8/engine_v8.cc`, PR #165. |
| #128 | EPIC-25: Release Packaging and Distribution | Open | Close as completed for experimental package scope | Child tasks #142-#146 are closed. Public release hardening remains later. | `tools/windows/package.ps1`, `tools/unix/package.sh`, PR #162. |
| #129 | EPIC-26: Cross-platform CI Expansion | Open | Close as completed after noting follow-up #166 | Child tasks #147-#151 are closed, and PR #166 landed a CI fix. | `.github/workflows/ci.yml`, PRs #164 and #166. |

## Open Parent EPICs With Child Tasks Still Open

| Issue | Title | State | Recommended action | Reason | Evidence |
| ---: | --- | --- | --- | --- | --- |
| #1 | EPIC-00: Foundation / Harness / Tooling | Open | Review, then close completed tasks or retitle remaining cleanup | Children #22-#25 are still open, but much of the work exists in docs and tooling. | `AGENTS.md`, `docs/project/issue-workflow.md`, `tools/github/`, `tools/windows/create-review-zip.ps1`. |
| #2 | EPIC-01: Platform Abstraction | Open | Keep open, narrow to remaining platform scanner/docs/CI cleanup | Children #26-#29 are open. | `docs/platform-abstraction.md`, `tools/windows/check-platform-boundaries.ps1`, `tools/unix/check-platform-boundaries.sh`. |
| #4 | EPIC-03: Memory Foundation | Open | Keep open | #31 is closed, but #32 string builder / buffer foundation remains open. | `include/sloppy/arena.h`, `docs/memory.md`. |
| #5 | EPIC-04: Diagnostics Foundation | Open | Keep open | #33 is closed, but #34 source frame / JSON diagnostics remains open. | `include/sloppy/diagnostics.h`, `tests/golden/diagnostics/`. |
| #6 | EPIC-05: Resource Lifecycle Foundation | Open | Keep open | #36 is closed, but #35 resource table remains open. | `include/sloppy/scope.h`, `docs/memory.md`. |
| #130 | EPIC-27: Runtime Security / Capabilities Enforcement | Open | Keep open and move into MAIN.1 | Children #152-#156 are all open and remain important before public alpha. | `docs/security-permissions.md`, `docs/public/permissions.md`. |
| #131 | EPIC-28: Public Alpha Docs and Examples | Open | Keep open, but re-scope behind hardening | Children #157-#161 are open, but public alpha docs should not imply readiness before MAIN.1 criteria are met. | `README.md`, `docs/public/`, `examples/compiler-hello/`. |

## Closed Tasks Whose Parent EPIC Is Still Open

| Parent | Closed child tasks | Recommended action |
| --- | --- | --- |
| #3 | #30 | Close parent #3 as completed. |
| #7 | #37-#39 | Close parent #7 as completed. |
| #8 | #40-#43 | Close parent #8 as completed with V8-gated caveat. |
| #9 | #44 | Close parent #9 as completed with V8-gated caveat. |
| #10 | #45-#47 | Close parent #10 as completed for skeleton scope. |
| #11 | #48-#50 | Close parent #11 as completed for foundation scope. |
| #12 | #51-#54 | Close parent #12 as completed for bootstrap scope. |
| #13 | #55-#58 | Close parent #13 as completed for bootstrap scope. |
| #14 | #59-#62 | Close parent #14 as completed for bootstrap scope. |
| #15 | #63-#66 | Close parent #15 as completed for bootstrap scope. |
| #16 | #67-#70 | Close parent #16 as completed for metadata/fake-provider scope. |
| #17 | #71-#74 | Close parent #17 as completed for native SQLite scope. |
| #18 | #75-#78 | Close parent #18 as completed for native PostgreSQL scope. |
| #19 | #79-#82 | Close parent #19 as completed for native SQL Server scope. |
| #20 | #83-#86 | Close parent #20 as completed for metadata CLI scope. |
| #21 | #87-#90 | Close parent #21 as completed for benchmark harness scope. |
| #126 | #132-#136 | Close parent #126 as completed. |
| #127 | #137-#141 | Close parent #127 as completed for classic bootstrap runtime scope. |
| #128 | #142-#146 | Close parent #128 as completed for experimental package scope. |
| #129 | #147-#151 | Close parent #129 as completed for hosted non-V8 CI scope. |

## Stale Old Issues From EPIC-00 Through EPIC-20

| Issue | Title | State | Recommended action | Reason | Evidence |
| ---: | --- | --- | --- | --- | --- |
| #22 | TASK 00.A: Final Docs Audit | Open | Close as superseded or retitle as narrow docs sweep | The repo now has roadmap, post-0.7 audit, review, workflow, and standards docs. | `docs/roadmap.md`, `docs/project/post-0.7-issue-audit.md`, `docs/review-playbook.md`. |
| #23 | TASK 00.B: GitHub Ceremony Pass | Open | Close as completed or retitle as MAIN issue cleanup application | Issue tooling exists; this PR produces the next cleanup plan. | `tools/github/create-issues.ps1`, `tools/github/dry-run-summary.ps1`, this audit. |
| #24 | TASK 00.C: Review Zip Hygiene | Open | Verify, then close as completed | Review zip/source archive tooling exists. | `tools/windows/create-review-zip.ps1`, `docs/project/pr-workflow.md`. |
| #25 | TASK 00.D: Remaining Harness Cleanup | Open | Retitle to a specific cleanup or close as superseded | The title is broad and no longer useful as an execution task. | `AGENTS.md`, `docs/project/issue-workflow.md`. |
| #26 | TASK 01.A: Platform Scanner and Fixtures | Open | Keep open | Scanner exists, but fixture/self-test hardening remains plausible. | `tools/windows/check-platform-boundaries.ps1`, `tools/unix/check-platform-boundaries.sh`. |
| #27 | TASK 01.B: Initial Platform Header Boundaries | Open | Review and likely close | Header boundary docs and scanners now exist. | `include/sloppy/platform.h`, `docs/platform-abstraction.md`. |
| #28 | TASK 01.C: OS API Category Documentation | Open | Keep open if the taxonomy still needs detail | Some categories are documented, but this can remain as a narrow docs cleanup. | `docs/platform-abstraction.md`. |
| #29 | TASK 01.D: CI Boundary Integration | Open | Keep open only if it means scanner fixture/CI hardening beyond current CI | CI now runs cross-platform default gates. | `.github/workflows/ci.yml`, PR #164, PR #166. |
| #32 | TASK 03.B: String Builder / Buffer Foundation | Open | Keep open and move into MAIN.1 | `SlBuf` and string builder remain future memory primitives. | `docs/memory.md`. |
| #34 | TASK 04.B: Source Frame / JSON Diagnostics | Open | Keep open and move into MAIN.1 | JSON/source-frame diagnostics remain unfinished. | `docs/diagnostics.md`. |
| #35 | TASK 05.A: Resource Table | Open | Keep open and move into MAIN.1 | JS-native resource IDs and generation table are not implemented. | `docs/memory.md`, `docs/security-permissions.md`. |

## EPIC-21 Through EPIC-28 Completion and Duplicate Risk

| Area | GitHub state | Recommendation | Evidence |
| --- | --- | --- | --- |
| EPIC-21 Compiler Extraction MVP | No generated EPIC issue; PR #124 merged | Do not create duplicate issues for compiler extraction MVP. Only create hardening tasks for gaps. | `compiler/src/sloppyc.rs`, `compiler/tests/fixtures/`, `docs/project/next-roadmap.md`. |
| EPIC-22 Sloppy Run MVP | No generated EPIC issue; PR #125 merged | Do not create duplicate issues for artifact-load run MVP. Source-input handoff is the real remaining task. | `src/main.c`, `docs/public/cli.md`, `docs/project/next-roadmap.md`. |
| EPIC-23 HTTP Response/Context | Parent #126 open, child tasks #132-#136 closed | Close #126 as completed; do not recreate EPIC-23 tasks. | PR #163; `include/sloppy/http_response.h`, `include/sloppy/http_context.h`. |
| EPIC-24 V8 Bootstrap Runtime | Parent #127 open, child tasks #137-#141 closed | Close #127 as completed for classic bootstrap runtime; do not recreate tasks. | PR #165; `stdlib/sloppy/internal/runtime-classic.js`. |
| EPIC-25 Packaging | Parent #128 open, child tasks #142-#146 closed | Close #128 as completed for experimental packaging; move release hardening to MAIN.1. | PR #162; `tools/windows/package.ps1`. |
| EPIC-26 Cross-platform CI | Parent #129 open, child tasks #147-#151 closed | Close #129 as completed after noting PR #166; move optional V8/live/sanitizer/package jobs to MAIN.1. | PRs #164, #166; `.github/workflows/ci.yml`. |
| EPIC-27 Capabilities | Parent #130 open, child tasks #152-#156 open | Keep open and fold into MAIN.1 rather than MAIN unless minimal hello requires it. | `docs/security-permissions.md`, `docs/public/permissions.md`. |
| EPIC-28 Public Docs | Parent #131 open, child tasks #157-#161 open | Re-scope/defer public alpha docs behind MAIN.1 hardening. Keep executable hello docs internal until criteria pass. | `README.md`, `docs/public/`, `examples/compiler-hello/README.md`. |

## Issues That Duplicate Already-Done Work

No exact-title duplicate issues were found. The duplicate risk is semantic:

- `tools/github/next-roadmap-issues.json` still stages EPIC-23 through EPIC-26, while the
  live child tasks are already closed.
- any new issue titled around compiler extraction MVP, artifact emission, `sloppy run
  --artifacts`, HTTP response descriptor MVP, classic bootstrap runtime load, local package
  layout, or hosted default CI should be checked against merged PRs #124, #125, #163, #165,
  #162, #164, and #166 before creation.
- EPIC-28 public docs tasks duplicate the desire for public alpha readiness unless they are
  explicitly scoped as honesty/docs gates after hardening rather than marketing launch work.

## Issues Recommended To Close As Completed

Recommended completed closures after human review:

- parent EPICs #3, #7, #8, #9, #10, #11, #12, #13, #14, #15, #16, #17, #18, #19, #20, #21;
- parent EPICs #126, #127, #128, #129;
- likely old tasks #22, #23, #24, #27 if the owner agrees the current docs/tools satisfy
  their original scope.

## Issues Recommended To Close As Not Planned Or Superseded

Recommended superseded closures after human review:

- #25 if no specific remaining harness cleanup can be stated.
- any future recreation of EPIC-21 through EPIC-26 tasks that simply repeats already-merged
  scope.
- EPIC-28 tasks should not be closed as not planned wholesale, but #158 should explicitly
  allow deferral of executable SQLite docs until the JS-to-native SQLite bridge exists.

## Issues Recommended To Relabel Or Re-scope

| Issue | Recommendation |
| ---: | --- |
| #1 | Re-scope to the remaining open #22-#25 cleanup, or close after individual children are resolved. |
| #2 | Re-scope to scanner fixtures, OS API taxonomy, and CI scanner hardening only. |
| #29 | Re-scope away from generic CI integration if current CI already covers default boundary scanning. |
| #130 | Re-scope as MAIN.1 capability hardening, not MAIN minimal hello. |
| #131 | Re-scope as MAIN.1 final public alpha docs, not immediate public launch docs. |
| #157-#161 | Mark as blocked by MAIN/M1 evidence as appropriate; avoid public marketing claims. |
| Closed tasks #30-#90 and #132-#151 | Optionally remove stale `status:ready`, `status:deferred`, or `status:blocked` labels from closed issues. |

## Issues Recommended To Remain Open

Keep these open unless a more detailed verification pass proves completion:

- #2, #4, #5, #6;
- #26, #28, #29, #32, #34, #35;
- #130 and #152-#156;
- #131 and #157-#161, with public-alpha docs deferred behind hardening.

## Missing Issues To Create Only After MAIN/MAIN.1 Approval

Do not create these in this PR. The staged data in `tools/github/roadmap-main-issues.json`
and `tools/github/roadmap-main1-issues.json` prepares issue-ready candidates for review:

- MAIN final end-to-end executable hello verification through `sloppyc build` plus
  `sloppy run --artifacts`.
- MAIN V8-enabled local verification and package/CI evidence report.
- MAIN issue cleanup application task after the recommendation is approved.
- MAIN.1 source-input handoff and artifact cache.
- MAIN.1 plan schema compatibility and route/module/data/capability section hardening.
- MAIN.1 resource table / JS-native handle system.
- MAIN.1 JS-to-native SQLite bridge.
- MAIN.1 provider hardening, capability enforcement, diagnostics, CLI introspection,
  conformance, packaging, benchmarks, and public alpha docs.

# Post-0.7 GitHub Issue Audit

Status: historical audit. The newer MAIN/MAIN.1 audit supersedes this as the active issue
cleanup source.

See `docs/project/current-issue-state-audit.md` and
`docs/project/main-main1-issue-cleanup-plan.md` for current recommendations.

Generated from `gh issue list --repo RtlZeroMemory/Slop --state open --limit 200` and
`gh issue list --repo RtlZeroMemory/Slop --state closed --limit 200` during the 0.8
planning/consolidation pass.

No GitHub issues were mutated by this audit.

## Summary

- Open issues: 32.
- Closed issues returned by the audit: 58.
- Milestones remain open from `0.0 Foundation / Harness` through
  `0.7 Tooling and Performance`.
- Child tasks for EPIC-06 through EPIC-20 are mostly or fully closed, but parent EPIC
  issues remain open.
- Several old task issues from EPIC-00, EPIC-01, EPIC-03, EPIC-04, and EPIC-05 remain open
  and should be reconciled before creating EPIC-21 onward issues.

## Recommended Open-Issue Cleanup

| Issue | Title | Recommended action | Reason |
| --- | --- | --- | --- |
| #1 | EPIC-00: Foundation / Harness / Tooling | Review then close or relabel as follow-up | Repo-side harness/docs/tooling mostly exists; open child tasks appear stale or cleanup-only. |
| #2 | EPIC-01: Platform Abstraction | Keep open, narrow remaining scope | Platform boundary exists, but scanner fixtures/self-tests, CI boundary integration, and richer OS API docs remain open. |
| #3 | EPIC-02: Core Native Foundation | Close as implemented | TASK 02.A is closed and core primitives/tests exist. |
| #4 | EPIC-03: Memory Foundation | Keep open | `SlArena` is complete, but TASK 03.B string builder/buffer foundation is still open. |
| #5 | EPIC-04: Diagnostics Foundation | Keep open | Diagnostic core is complete, but source-frame/JSON diagnostics remain open. |
| #6 | EPIC-05: Resource Lifecycle Foundation | Keep open | `SlScope` exists, but generation-counted resource table remains open. |
| #7 | EPIC-06: Plan Schema and Loader | Close as implemented for scoped minimal loader | TASK 06.A through 06.C are closed; remaining plan work belongs to EPIC-21 onward follow-ups. |
| #8 | EPIC-07: V8 Bridge Smoke | Close as implemented with V8-gated caveat | TASK 07.A through 07.D are closed; default gates still do not validate V8, but that is documented as a gate caveat. |
| #9 | EPIC-08: Handwritten Artifact Execution | Close as implemented with V8-gated caveat | TASK 08.A is closed; compiler/HTTP/module follow-ups belong to next roadmap. |
| #10 | EPIC-09: Event Loop / Concurrency Foundation | Close as implemented for skeleton scope | TASK 09.A through 09.C are closed; real threads/libuv/promises are future EPIC work. |
| #11 | EPIC-10: HTTP Router Foundation | Close as implemented for foundation scope | TASK 10.A through 10.C are closed; response/server/context work is EPIC-22/23 territory. |
| #12 | EPIC-11: Public TypeScript API Bootstrap | Close as implemented for bootstrap scope | TASK 11.A through 11.D are closed; compiler/runtime integration is EPIC-21 onward. |
| #13 | EPIC-12: App Host Foundation | Close as implemented for JS bootstrap scope | TASK 12.A through 12.D are closed; native app host is future work. |
| #14 | EPIC-13: Developer Ergonomics Layer | Close as implemented for JS bootstrap scope | TASK 13.A through 13.D are closed; automatic validation/OpenAPI/runtime integration remain future work. |
| #15 | EPIC-14: Modularity / App Modules | Close as implemented for JS bootstrap scope | TASK 14.A through 14.D are closed; compiler/native module emission is future work. |
| #16 | EPIC-15: Data / Capabilities Foundation | Close as implemented for bootstrap scope | TASK 15.A through 15.D are closed; enforcement and JS-native resources are future work. |
| #17 | EPIC-16: SQLite Provider | Close as implemented for native provider scope | TASK 16.A through 16.D are closed; JS bridge/pooling/capabilities remain future work. |
| #18 | EPIC-17: PostgreSQL Provider | Close as implemented for native provider scope | TASK 17.A through 17.D are closed; live tests are env-gated and production hardening remains future work. |
| #19 | EPIC-18: SQL Server Provider | Close as implemented for native provider scope | TASK 18.A through 18.D are closed; live tests are env-gated and production hardening remains future work. |
| #20 | EPIC-19: CLI Introspection Tooling | Close as implemented for metadata-only scope | TASK 19.A through 19.D are closed; real compiler/app metadata and live checks are future work. |
| #21 | EPIC-20: Benchmarks / Performance Validation | Close as implemented for initial benchmark scope | TASK 20.A through 20.D are closed; real HTTP/V8/DB/external comparisons remain future work. |
| #22 | TASK 00.A: Final Docs Audit | Close if superseded by this PR | This consolidation PR performs the post-roadmap docs audit. |
| #23 | TASK 00.B: GitHub Ceremony Pass | Close or relabel tooling follow-up | GitHub ceremony data/tooling exists; remaining need is next-roadmap issue creation. |
| #24 | TASK 00.C: Review Zip Hygiene | Close if no missing acceptance remains | Review zip helper exists and is documented. |
| #25 | TASK 00.D: Remaining Harness Cleanup | Close or retitle | Remaining cleanup should be explicit, not a broad old task. |
| #26 | TASK 01.A: Platform Scanner and Fixtures | Keep open | Scanner exists, but fixture/self-test hardening still appears useful. |
| #27 | TASK 01.B: Initial Platform Header Boundaries | Review and likely close | Header boundaries and scanners exist; verify whether any original acceptance remains. |
| #28 | TASK 01.C: OS API Category Documentation | Keep or retitle | Platform docs exist, but richer OS API categories may still be useful. |
| #29 | TASK 01.D: CI Boundary Integration | Keep open | Cross-platform and stronger CI integration remain deferred. |
| #32 | TASK 03.B: String Builder / Buffer Foundation | Keep open | Not implemented. |
| #34 | TASK 04.B: Source Frame / JSON Diagnostics | Keep open | Not implemented. |
| #35 | TASK 05.A: Resource Table | Keep open | Not implemented. |

## Closed Issues Reviewed

The following closed child tasks line up with repo-side implementation and should usually
stay closed:

- #30 TASK 02.A Core Basics Foundation.
- #31 TASK 03.A SlArena Foundation.
- #33 TASK 04.A Diagnostic Core.
- #36 TASK 05.B Scope / Lifetime Skeleton.
- #37 through #39 Plan schema/parser/fixtures.
- #40 through #43 V8 SDK / ABI / smoke / exception mapping.
- #44 Handwritten app.js + app.plan execution.
- #45 through #47 Event loop / async / worker-pool skeletons.
- #48 through #50 Route parser, llhttp/libuv skeleton, and synthetic GET dispatch.
- #51 through #54 Public TypeScript API bootstrap and hello example.
- #55 through #58 App-host foundation.
- #59 through #62 Developer ergonomics layer.
- #63 through #66 Module skeleton.
- #67 through #70 Data/capabilities foundation.
- #71 through #74 SQLite provider tasks.
- #75 through #78 PostgreSQL provider tasks.
- #79 through #82 SQL Server provider tasks.
- #83 through #86 CLI introspection tasks.
- #87 through #90 Benchmark tasks.

## Milestone Cleanup

| Milestone | Open | Closed | Recommended action |
| --- | ---: | ---: | --- |
| 0.0 Foundation / Harness | 5 | 0 | Review stale harness tasks after this PR; close or retitle before new roadmap issue generation. |
| 0.1 Native Core | 12 | 4 | Keep open for true remaining tasks: platform hardening, buffer/string builder, JSON diagnostics, resource table. |
| 0.2 Runtime Contract | 3 | 8 | Close completed parent EPICs where scoped work is done; move remaining runtime-contract gaps to EPIC-21 onward. |
| 0.3 Async Runtime / HTTP | 2 | 6 | Close completed parent EPICs; response/server/request context moves to EPIC-22/23. |
| 0.4 TypeScript App Host | 4 | 16 | Close completed parent EPICs; compiler/native app-host integration moves to EPIC-21 onward. |
| 0.5 Data and Capabilities | 2 | 8 | Close completed parent EPICs; enforcement/JS-native resource bridge moves to EPIC-27 and data follow-ups. |
| 0.6 External Providers | 2 | 8 | Close completed parent EPICs; live infra/production pooling remains follow-up. |
| 0.7 Tooling and Performance | 2 | 8 | Close completed parent EPICs; real runtime metadata and benchmark hardening remain follow-up. |

## Missing Follow-Up Issues

Recommended new issues should be generated from the reviewed MAIN and MAIN.1 staged issue
data after current issue cleanup is approved:

- `tools/github/roadmap-main-issues.json`
- `tools/github/roadmap-main1-issues.json`

EPIC-21 through EPIC-26 are now treated as completed baseline work and should not be
recreated. EPIC-27 remains open as hardening work. EPIC-28 public alpha docs should be
deferred or re-scoped behind MAIN.1 hardening unless the roadmap review decides otherwise.

Also consider small cleanup issues after approval:

- add docs link checker;
- add platform scanner fixtures/self-tests;
- reconcile stale milestones and labels after issue closure.

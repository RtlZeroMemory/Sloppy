# GitHub Issue Cleanup Result

Status: cleanup applied.

Date: 2026-04-30.

Source audit: `docs/project/github-issue-hygiene-audit.md`.

This cleanup pass reconciled live GitHub issue state after the ENGINE-23 roadmap creation.
It closed only issues with clear merged-PR or parent/child evidence and left uncertain or
active roadmap work open.

## Issues Closed

### Completed

- #273 TASK ENGINE-01.B: Final Async and Microtask Policy.
- #274 TASK ENGINE-01.C: Final HTTP and SQLite Support Matrix.
- #275 TASK ENGINE-01.D: Foundation Cancellation, Limits, and Backpressure Contract.
- #258 EPIC ENGINE-01: Final Framework Contract.
- #260 EPIC ENGINE-03: V8 Async Runtime.
- #261 EPIC ENGINE-04: HTTP API Runtime.
- #262 EPIC ENGINE-05: SQLite End-to-End.
- #291 TASK ENGINE-06.B: Capability Audit and Doctor Evidence.
- #263 EPIC ENGINE-06: Capability Enforcement Completion.
- #264 EPIC ENGINE-07: App Host Lifecycle Completion.
- #306 EPIC ENGINE-12: Scalable Async Runtime.
- #365 TASK ENGINE-21.B: String and Byte View Primitives.
- #366 TASK ENGINE-21.C: String Builder, Byte Builder, and Formatting Utilities.
- #368 TASK ENGINE-21.E: Memory Safety and Stress Tests.
- #369 TASK ENGINE-21.F: String Interning and Symbol Table Foundation.
- #362 ENGINE-21: Memory and String Runtime Foundations.
- #363 ENGINE-22: Memory/String Adoption and Hot-Path Refactor.
- #32 TASK 03.B: String Builder / Buffer Foundation.

### Superseded

- None.

### Not Planned

- None.

## Issues Kept Open

Important still-open issues:

- #259 and #302 remain open for the compiler/source-input handoff.
- #265 and #295 remain open for diagnostics/source mapping and broader async diagnostic
  JSON work.
- #266, #267, and #268 remain open for examples, conformance/package evidence, and the
  blocked public-alpha gate.
- #300 and #301 remain blocked public-alpha/non-claims tasks.
- #311 through #359 remain open for the active ENGINE-13 through ENGINE-20 roadmap.
- #390 through #398 remain open for the active ENGINE-23 provider execution/offload
  roadmap.
- #26 remains open because scanner fixture/self-test completion was not proven.

## Needs Human Review

- #26 TASK 01.A: Platform Scanner and Fixtures. Scanners and CI wiring exist, but fixture
  or self-test hardening is not clearly complete.
- #295 TASK ENGINE-08.B: Async Diagnostic JSON Surfaces. PR #361 added narrow coverage,
  but the open issue body is broader and should be reviewed or narrowed before closure.

## Deferred Or Blocked

- Public alpha docs remain blocked by #268, #300, and #301.
- Benchmark marketing claims remain blocked by policy; no open benchmark issue was found
  in this pass.
- PostgreSQL and SQL Server JS bridges remain deferred until SQLite/core engine evidence is
  stronger.
- ENGINE-23 provider execution remains the active provider/offload prerequisite before
  scalable SQLite/provider claims.

## Duplicate Issues Found

- #32 duplicated the now-completed ENGINE-21.C builder/byte-builder scope and was closed as
  completed with PR #380 evidence.

No remaining open exact-title duplicates were found.

## Replacement Mapping

| Old issue | Current issue or evidence |
| --- | --- |
| #32 TASK 03.B: String Builder / Buffer Foundation | #366 / PR #380 ENGINE-21.C builder implementation |
| #258 ENGINE-01 parent | PR #303; follow-on implementation in #259, #260, #261, #262 |
| #260 ENGINE-03 parent | PR #305; scalable async follow-on in #306 was completed and provider runtime continues in #390 |
| #261 ENGINE-04 parent | PR #377; proper HTTP backend continues in #311 |
| #262 ENGINE-05 parent | PR #379; SQLite completion continues in #315 and provider execution in #390 |
| #263 ENGINE-06 parent | PR #378; provider/runtime capability follow-through continues in #390 and #315 |
| #264 ENGINE-07 parent | PR #361; diagnostics/source mapping continues in #265 |
| #306 ENGINE-12 parent | PRs #388 and #389; provider execution continues in #390 |
| #362 ENGINE-21 parent | PRs #380 and #382; later backend/provider adoption continues in active ENGINE issues |
| #363 ENGINE-22 parent | PRs #381, #383, #384, #385, #386, #387; remaining backend/provider work continues in #311, #315, #317, and #390 |

## Issues That Should Be Closed Manually Later

- #26, only if scanner fixture/self-test hardening is proven or implemented.
- #295, only after the owner confirms PR #361 covers the remaining scope or the issue is
  narrowed to the still-missing async diagnostic JSON work.

## Non-Goals Confirmed

- No runtime implementation.
- No compiler implementation.
- No provider implementation.
- No ENGINE-23 task closure.
- No public alpha documentation launch.
- No benchmark or performance claim.
- No Node/npm or package-manager behavior.

# MAIN and MAIN.1 Issue Cleanup Plan

Status: dry-run/manual plan only.

This document intentionally does not mutate GitHub. Run commands only after human review.
Commands are shown as comments or examples.

## Parent EPICs Recommended To Close As Completed

After approval, close these parent EPICs as completed:

```powershell
# gh issue close 3 --repo RtlZeroMemory/Slop --reason completed --comment "Completed for the scoped core native foundation via child task #30 and PR #91. Follow-up hardening remains tracked separately."
# gh issue close 7 --repo RtlZeroMemory/Slop --reason completed --comment "Completed for the scoped Plan schema/loader foundation via #37-#39 and PRs #96-#98."
# gh issue close 8 --repo RtlZeroMemory/Slop --reason completed --comment "Completed for the scoped V8 bridge smoke foundation via #40-#43 and PRs #99-#102. V8 remains optional/gated."
# gh issue close 9 --repo RtlZeroMemory/Slop --reason completed --comment "Completed for the scoped handwritten artifact execution milestone via #44 and PR #103. V8 remains optional/gated."
# gh issue close 10 --repo RtlZeroMemory/Slop --reason completed --comment "Completed for the scoped event loop/concurrency skeleton via #45-#47 and PRs #104-#106."
# gh issue close 11 --repo RtlZeroMemory/Slop --reason completed --comment "Completed for the scoped HTTP/router foundation via #48-#50 and PRs #107-#109."
# gh issue close 12 --repo RtlZeroMemory/Slop --reason completed --comment "Completed for the scoped bootstrap TypeScript API foundation via #51-#54 and PRs #110-#112."
# gh issue close 13 --repo RtlZeroMemory/Slop --reason completed --comment "Completed for the scoped bootstrap app-host foundation via #55-#58 and PR #113."
# gh issue close 14 --repo RtlZeroMemory/Slop --reason completed --comment "Completed for the scoped developer ergonomics layer via #59-#62 and PR #114."
# gh issue close 15 --repo RtlZeroMemory/Slop --reason completed --comment "Completed for the scoped bootstrap modules foundation via #63-#66 and PR #115."
# gh issue close 16 --repo RtlZeroMemory/Slop --reason completed --comment "Completed for the scoped data/capabilities metadata and fake-provider foundation via #67-#70 and PR #116. Runtime enforcement remains tracked separately."
# gh issue close 17 --repo RtlZeroMemory/Slop --reason completed --comment "Completed for the scoped native SQLite provider via #71-#74 and PR #117. JS-to-native bridge remains tracked separately."
# gh issue close 18 --repo RtlZeroMemory/Slop --reason completed --comment "Completed for the scoped native PostgreSQL provider via #75-#78 and PR #118. Live tests remain opt-in."
# gh issue close 19 --repo RtlZeroMemory/Slop --reason completed --comment "Completed for the scoped native SQL Server provider via #79-#82 and PR #119. Live tests remain opt-in."
# gh issue close 20 --repo RtlZeroMemory/Slop --reason completed --comment "Completed for the scoped metadata CLI introspection MVP via #83-#86 and PR #121."
# gh issue close 21 --repo RtlZeroMemory/Slop --reason completed --comment "Completed for the scoped benchmark harness via #87-#90 and PR #120. Performance claims remain deferred."
# gh issue close 126 --repo RtlZeroMemory/Slop --reason completed --comment "Completed via child tasks #132-#136 and PR #163."
# gh issue close 127 --repo RtlZeroMemory/Slop --reason completed --comment "Completed for the classic bootstrap runtime scope via #137-#141 and PR #165. True ESM module graph loading remains deferred."
# gh issue close 128 --repo RtlZeroMemory/Slop --reason completed --comment "Completed for the experimental local package scope via #142-#146 and PR #162."
# gh issue close 129 --repo RtlZeroMemory/Slop --reason completed --comment "Completed for default hosted non-V8 CI and explicit optional gate reporting via #147-#151, PR #164, and follow-up PR #166."
```

## Task Issues Recommended To Close As Completed Or Superseded

Review first, then close if accepted:

```powershell
# gh issue close 22 --repo RtlZeroMemory/Slop --reason completed --comment "Superseded by current roadmap/audit/docs state and MAIN/MAIN.1 planning docs."
# gh issue close 23 --repo RtlZeroMemory/Slop --reason completed --comment "GitHub ceremony tooling exists; MAIN/MAIN.1 cleanup is tracked by the new issue cleanup plan."
# gh issue close 24 --repo RtlZeroMemory/Slop --reason completed --comment "Review ZIP/source archive hygiene tooling exists under tools/windows and is documented."
# gh issue close 27 --repo RtlZeroMemory/Slop --reason completed --comment "Initial platform header boundaries are now documented and scanner-backed."
```

Close as superseded only if no specific remaining work is identified:

```powershell
# gh issue close 25 --repo RtlZeroMemory/Slop --reason not-planned --comment "Broad remaining harness cleanup is superseded by narrower tracked cleanup tasks and MAIN/MAIN.1 plans."
```

## Issues Recommended To Keep

Keep open unless a focused verification pass proves completion:

- #2, #26, #28, #29 for remaining platform scanner/docs/CI hardening.
- #4 and #32 for string builder / buffer foundation.
- #5 and #34 for source-frame / JSON diagnostics.
- #6 and #35 for resource table / lifecycle hardening.
- #130 and #152-#156 for capability enforcement.
- #131 and #157-#161, re-scoped as public alpha docs after MAIN.1 hardening.

## Issues Recommended To Defer

- #131/#157-#161 public alpha docs should remain blocked by MAIN and MAIN.1 evidence.
- #158 executable SQLite demo should explicitly allow deferral until JS-to-native SQLite
  bridge and resource/capability foundations exist.
- Any production HTTP, package-manager, Node compatibility, OS sandboxing, broad TS
  checking, or performance comparison issues should be post-alpha unless separately
  approved.

## Issues Recommended To Relabel

After review, consider:

```powershell
# gh issue edit 130 --repo RtlZeroMemory/Slop --add-label status:ready --remove-label status:deferred
# gh issue edit 131 --repo RtlZeroMemory/Slop --add-label status:blocked --remove-label status:ready
```

Optional closed issue label cleanup:

```powershell
# For closed issues only, consider removing stale status labels such as status:ready,
# status:deferred, or status:blocked if the team wants label hygiene.
```

## Duplicate Issues To Avoid

Do not create new issues for:

- EPIC-21 compiler extraction MVP;
- EPIC-22 `sloppy run --artifacts` MVP;
- EPIC-23 response/context MVP;
- EPIC-24 classic bootstrap runtime MVP;
- EPIC-25 experimental local package MVP;
- EPIC-26 default non-V8 cross-platform CI MVP.

Create new issues only for gaps named in `roadmap-main.md` and
`roadmap-main-1-hardening.md`, and only after reviewing the staged data.

## Fresh Issue Data Review

Validate and dry-run only:

```powershell
.\tools\github\validate-issue-data.ps1 -Input .\tools\github\roadmap-main-issues.json
.\tools\github\dry-run-summary.ps1 -Input .\tools\github\roadmap-main-issues.json
.\tools\github\create-issues.ps1 -Input .\tools\github\roadmap-main-issues.json -DryRun

.\tools\github\validate-issue-data.ps1 -Input .\tools\github\roadmap-main1-issues.json
.\tools\github\dry-run-summary.ps1 -Input .\tools\github\roadmap-main1-issues.json
.\tools\github\create-issues.ps1 -Input .\tools\github\roadmap-main1-issues.json -DryRun
```

Do not run `-Apply` until the roadmap and cleanup recommendations are approved.

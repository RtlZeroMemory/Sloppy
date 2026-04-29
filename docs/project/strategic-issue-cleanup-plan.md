# Strategic Issue Cleanup Plan

Status: executed during PR #256 follow-up after reviewer approval.

This plan follows the 2026-04-29 live audit in
`docs/project/strategic-current-state-audit.md`. The original version listed commands as
commented recommendations; after approval, the cleanup below was applied with matching
comments in GitHub issues.

## Ground Rules

- Re-run live `gh issue list` and `gh pr list` before future cleanup.
- Do not create ENGINE issues until this PR and the dry-run issue data are reviewed.
- Close completed parent issues with comments that name the merged PR evidence.
- Close superseded issues with comments that point to the ENGINE roadmap issue replacing
  them.
- Keep public alpha docs blocked until Layers 1-9 of the engine roadmap pass.

## Completed MAIN Parents

```powershell
gh issue close 167 --repo RtlZeroMemory/Slop --reason completed --comment "Completed by PR #241 for the scoped MAIN-01 executable artifact-path verification. Future engine-foundation work is tracked by ENGINE roadmap issues."
gh issue close 168 --repo RtlZeroMemory/Slop --reason completed --comment "Completed by PR #243 for the scoped MAIN-02 evidence/gate report. Future evidence work is tracked by ENGINE-10."
```

## Completed MAIN1 Parents

```powershell
gh issue close 180 --repo RtlZeroMemory/Slop --reason completed --comment "Completed by PR #244 for the scoped MAIN1-01 compiler hardening/syntax matrix. Remaining compiler foundation is tracked by ENGINE-02."
gh issue close 181 --repo RtlZeroMemory/Slop --reason completed --comment "Completed by PR #248 for the scoped MAIN1-02 Plan schema/compatibility hardening. Remaining Plan foundation is tracked by ENGINE-02 and ENGINE-08."
gh issue close 182 --repo RtlZeroMemory/Slop --reason completed --comment "Completed by PR #250 for the scoped MAIN1-03 runtime app-host hardening. Remaining lifecycle foundation is tracked by ENGINE-07."
gh issue close 183 --repo RtlZeroMemory/Slop --reason completed --comment "Completed by PR #251 for the scoped MAIN1-04 HTTP runtime hardening. Full framework HTTP runtime completion is tracked by ENGINE-04."
gh issue close 184 --repo RtlZeroMemory/Slop --reason completed --comment "Completed by PR #249 for the scoped MAIN1-05 V8 runtime hardening. Real async/Promise runtime completion is tracked by ENGINE-03."
gh issue close 185 --repo RtlZeroMemory/Slop --reason completed --comment "Completed by PR #247 for the scoped MAIN1-06 diagnostics completion. Remaining source-map/async diagnostic completion is tracked by ENGINE-08."
gh issue close 186 --repo RtlZeroMemory/Slop --reason completed --comment "Completed by PR #242 for the scoped MAIN1-07 resource lifecycle and JS-native handles. Remaining app/request lifecycle completion is tracked by ENGINE-07."
gh issue close 187 --repo RtlZeroMemory/Slop --reason completed --comment "Completed by PR #253 for the scoped MAIN1-08 SQLite JS-to-native bridge. SQLite end-to-end foundation and capability wiring are tracked by ENGINE-05 and ENGINE-06."
gh issue close 188 --repo RtlZeroMemory/Slop --reason completed --comment "Completed by PR #245 for the scoped MAIN1-09 provider hardening. PostgreSQL and SQL Server JS bridges are deferred until after SQLite engine foundation."
gh issue close 189 --repo RtlZeroMemory/Slop --reason completed --comment "Completed by PR #252 for the scoped MAIN1-10 capability/security hardening. Runtime bridge enforcement completion is tracked by ENGINE-06."
gh issue close 190 --repo RtlZeroMemory/Slop --reason completed --comment "Completed by PR #254 for the scoped MAIN1-11 CLI introspection hardening. Future evidence/introspection work is tracked by ENGINE-08 and ENGINE-10."
gh issue close 191 --repo RtlZeroMemory/Slop --reason completed --comment "Completed by PR #246 for the scoped MAIN1-12 packaging/cross-platform hardening. Packaged runtime evidence is tracked by ENGINE-10."
gh issue close 192 --repo RtlZeroMemory/Slop --reason completed --comment "Completed by PR #255 for the scoped MAIN1-13 end-to-end conformance suite. Expanded realistic API conformance is tracked by ENGINE-09."
```

## Completed Child Issues

Most MAIN/MAIN1 child task issues are already closed (#170-#179 and #195-#233). Confirm no
MAIN1 child remains open before closing parents. If a child was reopened, do not close the
parent until the reopened child has a replacement ENGINE issue.

Open old child issue likely completed:

```powershell
gh issue close 35 --repo RtlZeroMemory/Slop --reason completed --comment "The generation-checked resource table and JS-native handle foundation landed through MAIN1-07 / PR #242. Broader lifecycle work is tracked by ENGINE-07."
```

Potentially completed but needs reviewer confirmation:

```powershell
gh issue close 34 --repo RtlZeroMemory/Slop --reason completed --comment "JSON/source-frame diagnostics have landed through MAIN1-06 / PR #247. Remaining source-map/runtime diagnostic work is tracked by ENGINE-08."
```

## Stale Parent EPICs

Applied after review:

```powershell
gh issue close 2 --repo RtlZeroMemory/Slop --reason completed --comment "Closing as completed for the EPIC-01 platform abstraction foundation scope: platform directories/docs, header boundary rules, Windows/Unix scanners, lint integration, and CI boundary coverage now exist. The remaining scanner fixture/self-test hardening stays open as #26."
gh issue close 4 --repo RtlZeroMemory/Slop --reason completed --comment "Closing as completed for the EPIC-03 memory foundation parent scope: SlArena landed and is tested/documented. The optional string builder/buffer follow-up remains open as the specific task #32 rather than keeping this old parent EPIC open."
gh issue close 5 --repo RtlZeroMemory/Slop --reason completed --comment "Closing as completed for the EPIC-04 diagnostics foundation scope: diagnostic core (#33) and source-frame/JSON diagnostics (#34) are closed, and docs/diagnostics.md plus core diagnostics tests cover the foundation. Future source-map/runtime diagnostic completion is tracked by ENGINE-08 / PR #256."
gh issue close 6 --repo RtlZeroMemory/Slop --reason completed --comment "Resource table/handle foundation has landed. Remaining app/request lifecycle and cleanup work is tracked by ENGINE-07."
```

## Superseded Public Alpha Docs Issues

Public alpha docs remain blocked and should be represented by ENGINE-11 only after Layers
1-9 pass.

```powershell
gh issue close 194 --repo RtlZeroMemory/Slop --reason not-planned --comment "Superseded by the strategic Slop Engine roadmap. Public alpha docs remain blocked until ENGINE Layers 1-9 pass; readiness gate is tracked by ENGINE-11."
gh issue close 237 --repo RtlZeroMemory/Slop --reason not-planned --comment "Superseded by ENGINE-11. Executable hello tutorial should wait for the full engine-foundation evidence gate."
gh issue close 238 --repo RtlZeroMemory/Slop --reason not-planned --comment "Superseded by ENGINE-11. Public SQLite demo remains blocked until SQLite end-to-end and capability conformance pass."
gh issue close 239 --repo RtlZeroMemory/Slop --reason not-planned --comment "Superseded by ENGINE-11. Public alpha checklist remains blocked until Layers 1-9 pass."
```

## Deferred Benchmark Issues

Benchmarks are not next strategic blockers. Keep no-claims policy, but defer methodology
work until real HTTP/V8/SQLite paths are coherent.

```powershell
gh issue close 193 --repo RtlZeroMemory/Slop --reason not-planned --comment "Superseded by the strategic Slop Engine roadmap. Benchmark methodology remains deferred until real engine/framework paths pass evidence gates."
gh issue close 234 --repo RtlZeroMemory/Slop --reason not-planned --comment "Deferred until engine foundation examples execute through real HTTP/V8/SQLite paths."
gh issue close 235 --repo RtlZeroMemory/Slop --reason not-planned --comment "Deferred until real path evidence exists; no benchmark claims before methodology is real."
gh issue close 236 --repo RtlZeroMemory/Slop --reason not-planned --comment "No public comparison claims remain policy; public benchmark claim checks belong after ENGINE-10 evidence."
```

## Old Issue Leftovers

Keep:

- #26 TASK 01.A Platform Scanner and Fixtures.
- #32 TASK 03.B String Builder / Buffer Foundation.

Closed completed:

- #2 EPIC-01 Platform Abstraction.
- #4 EPIC-03 Memory Foundation.
- #5 EPIC-04 Diagnostics Foundation.
- #6 EPIC-05 Resource Lifecycle Foundation.
- #28 TASK 01.C OS API Category Documentation.
- #29 TASK 01.D CI Boundary Integration.
- #34 TASK 04.B Source Frame / JSON Diagnostics.
- #35 TASK 05.A Resource Table.

## Dry-Run Replacement Issue Data

Validate and dry-run only:

```powershell
.\tools\github\validate-issue-data.ps1 -Input .\tools\github\slop-engine-roadmap-issues.json
.\tools\github\dry-run-summary.ps1 -Input .\tools\github\slop-engine-roadmap-issues.json
.\tools\github\create-issues.ps1 -Input .\tools\github\slop-engine-roadmap-issues.json -DryRun
```

Apply only after review:

```powershell
# .\tools\github\create-all.ps1 -Input .\tools\github\slop-engine-roadmap-issues.json -Apply
```

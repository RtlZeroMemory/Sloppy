# GitHub Issue Hygiene Audit

Status: applied cleanup pass.

Audit date: 2026-04-30.

Repository: `RtlZeroMemory/Slop`.

This audit records the tracker state after the ENGINE-23 Provider Execution and Blocking
Offload Runtime roadmap was created. It is intentionally an issue-hygiene document, not an
implementation plan. No runtime, compiler, provider, benchmark, or public-alpha feature
work was implemented in this pass.

## Commands Used

```powershell
gh pr list --repo RtlZeroMemory/Slop --state open --limit 100
gh pr list --repo RtlZeroMemory/Slop --state merged --limit 200
gh issue list --repo RtlZeroMemory/Slop --state open --limit 400
gh issue list --repo RtlZeroMemory/Slop --state closed --limit 400
```

Additional spot checks used `gh issue view` and `gh pr view` for issues and merged PRs
whose closure evidence needed to be verified before mutation.

## Snapshot

The pre-cleanup live issue tracker still had several open ENGINE parents whose child task
issues were already closed, several ENGINE-21 primitive tasks that PR #380 explicitly
closed in its body, and the old `TASK 03.B` memory/string task that ENGINE-21.C absorbed.

The cleanup closed only issues with direct merged-PR evidence or parent issues whose child
tasks were all closed. Active ENGINE-13+ work was left open unless the issue was the
already-completed ENGINE-21/22 parent set. ENGINE-23 provider execution issues were left
open.

## 1. Open Parent EPICs Whose Child Tasks Are All Closed Or Implemented

| Issue | Title | Current state before cleanup | Parent EPIC | Related merged PRs | Evidence files/docs/tests | Recommendation | Reason |
| ---: | --- | --- | --- | --- | --- | --- | --- |
| #258 | EPIC ENGINE-01: Final Framework Contract | Open | n/a | #303 | `docs/project/engine-framework-contract.md`, `docs/project/slop-engine-layered-roadmap.md`, `docs/roadmap.md` | Close completed | PR #303 closed #272-#275 and locked the contract source of truth. |
| #260 | EPIC ENGINE-03: V8 Async Runtime | Open | n/a | #305 | `docs/exec-plans/active/engine-03-v8-async-runtime.md`, `docs/concurrency.md`, `tests/unit/engine/test_v8_smoke.c` | Close completed | PR #305 closed #280-#283 for the bounded V8 async runtime slice. |
| #261 | EPIC ENGINE-04: HTTP API Runtime | Open | n/a | #377 | `docs/exec-plans/active/engine-04-http-api-runtime.md`, `docs/public/routing.md`, HTTP/V8 unit coverage | Close completed | PR #377 closed #284-#286 for the bounded local HTTP API runtime. |
| #262 | EPIC ENGINE-05: SQLite End-to-End | Open | n/a | #379 | `docs/exec-plans/active/engine-05-sqlite-runtime.md`, `docs/data-providers.md`, SQLite bridge fixtures | Close completed | PR #379 closed #287-#289 for the bounded SQLite bridge slice. |
| #263 | EPIC ENGINE-06: Capability Enforcement Completion | Open | n/a | #378 | `docs/security-permissions.md`, `docs/public/permissions.md`, `tests/unit/core/test_capability.c` | Close completed | PR #378 closed #290 and #291; #291 was still open only due tracker drift. |
| #264 | EPIC ENGINE-07: App Host Lifecycle Completion | Open | n/a | #361 | `docs/modules/app-host/README.md`, `tests/unit/core/test_app_host.c` | Close completed | PR #361 closed #292 and #293; ENGINE-08 remains separate. |
| #306 | EPIC ENGINE-12: Scalable Async Runtime | Open | n/a | #388, #389 | `docs/concurrency.md`, `include/sloppy/async_backend.h`, `include/sloppy/provider_executor.h`, `tests/unit/core/test_provider_executor.c` | Close completed | PRs #388 and #389 closed #307-#310; provider execution moved to ENGINE-23. |
| #362 | ENGINE-21: Memory and String Runtime Foundations | Open | n/a | #380, #382 | `docs/project/engine-21-22-issue-index.md`, `include/sloppy/builder.h`, `include/sloppy/intern.h` | Close completed | All ENGINE-21 child tasks were closed or explicitly implemented by PR #380/#382. |
| #363 | ENGINE-22: Memory/String Adoption and Hot-Path Refactor | Open | n/a | #381, #383, #384, #385, #386, #387 | `docs/project/memory-string-adoption-map.md`, `docs/memory.md`, `docs/quality-score.md` | Close completed | All ENGINE-22 child tasks #370-#375 were closed by merged adoption PRs. |

## 2. Open Child Tasks Already Implemented By Merged PRs

| Issue | Title | Current state before cleanup | Parent EPIC | Related merged PRs | Evidence files/docs/tests | Recommendation | Reason |
| ---: | --- | --- | --- | --- | --- | --- | --- |
| #273 | TASK ENGINE-01.B: Final Async and Microtask Policy | Open | #258 | #303 | `docs/project/engine-framework-contract.md` | Close completed | PR #303 body says `Closes #273`. |
| #274 | TASK ENGINE-01.C: Final HTTP and SQLite Support Matrix | Open | #258 | #303 | `docs/project/engine-framework-contract.md`, public HTTP/data docs | Close completed | PR #303 body says `Closes #274`. |
| #275 | TASK ENGINE-01.D: Foundation Cancellation, Limits, and Backpressure Contract | Open | #258 | #303 | `docs/project/engine-framework-contract.md`, `docs/concurrency.md` | Close completed | PR #303 body says `Closes #275`. |
| #291 | TASK ENGINE-06.B: Capability Audit and Doctor Evidence | Open | #263 | #378 | `docs/security-permissions.md`, `tests/unit/core/test_capability.c` | Close completed | PR #378 body says `Closes #291`. |
| #365 | TASK ENGINE-21.B: String and Byte View Primitives | Open | #362 | #380 | `include/sloppy/string.h`, `include/sloppy/bytes.h`, string/bytes tests | Close completed | PR #380 body says `Closes #365`. |
| #366 | TASK ENGINE-21.C: String Builder, Byte Builder, and Formatting Utilities | Open | #362 | #380 | `include/sloppy/builder.h`, `src/core/builder.c`, `tests/unit/core/test_builder.c` | Close completed | PR #380 body says `Closes #366`. |
| #368 | TASK ENGINE-21.E: Memory Safety and Stress Tests | Open | #362 | #380 | `tests/unit/core/test_arena.c`, `test_builder.c`, `test_intern.c` | Close completed | PR #380 body says `Closes #368`. |
| #369 | TASK ENGINE-21.F: String Interning and Symbol Table Foundation | Open | #362 | #380 | `include/sloppy/intern.h`, `src/core/intern.c`, `tests/unit/core/test_intern.c` | Close completed | PR #380 body says `Closes #369`. |
| #32 | TASK 03.B: String Builder / Buffer Foundation | Open | old EPIC-03 | #380 | `include/sloppy/builder.h`, `src/core/builder.c`, `tests/unit/core/test_builder.c`, `docs/project/engine-21-22-issue-index.md` | Close completed | The old narrow string/buffer task was absorbed by ENGINE-21.C and implemented by PR #380. |

## 3. Open Issues Superseded By ENGINE Roadmap

No issue was closed as `superseded` in this pass. The only old duplicate/superseded-shaped
open issue found was #32, but its exact builder/buffer scope is now completed by ENGINE-21.C
rather than merely replaced by future planning.

Open ENGINE-17 and ENGINE-23 issues were not treated as duplicates of earlier SQLite,
capability, provider, async, or offload work. They carry broader current roadmap scope:
provider execution, scalable offload semantics, SQLite completion, conformance, and
integration evidence.

## 4. Open Benchmark/Public-Alpha/Docs Issues That Should Remain Blocked Or Deferred

| Issue | Title | Current state | Parent EPIC | Related merged PRs | Evidence files/docs/tests | Recommendation | Reason |
| ---: | --- | --- | --- | --- | --- | --- | --- |
| #268 | EPIC ENGINE-11: Public Alpha Readiness Gate | Open, blocked | n/a | #256, #303, #399 | `docs/project/slop-engine-layered-roadmap.md`, `docs/roadmap.md`, `docs/quality-score.md` | Defer/blocked | Public alpha docs remain blocked until engine foundation evidence is complete or explicitly scoped down. |
| #300 | TASK ENGINE-11.A: Public Alpha Readiness Checklist | Open, blocked | #268 | #256, #303 | `docs/roadmap.md`, `docs/quality-score.md` | Defer/blocked | The checklist is valid as a future gate, not an immediate docs launch task. |
| #301 | TASK ENGINE-11.B: Public Non-Claims Review | Open, blocked | #268 | #256, #303 | `docs/quality-score.md`, `docs/tech-debt-tracker.md` | Defer/blocked | Public non-claims review should remain blocked until the evidence gate is ready. |

No open benchmark methodology issue remained after the earlier MAIN1 cleanup. Benchmark
marketing remains blocked by policy in `docs/roadmap.md`, `docs/quality-score.md`, and
`docs/tech-debt-tracker.md`; no new benchmark issue was created.

## 5. Open Issues That Are Still Valid And Should Remain Open

| Issue | Title | Current state | Parent EPIC | Related merged PRs | Evidence files/docs/tests | Recommendation | Reason |
| ---: | --- | --- | --- | --- | --- | --- | --- |
| #259 | EPIC ENGINE-02: Compiler Full Supported App Pipeline | Open | n/a | #304 | `docs/compiler-supported-syntax.md`, `compiler/tests/fixtures`, #302 | Keep open | #302 remains open for direct source-input run handoff. |
| #302 | TASK ENGINE-02.E: Direct Source-Input Run Handoff | Open | #259 | #303 | `docs/project/engine-framework-contract.md`, `docs/public/cli.md` | Keep open | Direct `sloppy run app.js` remains unsupported and explicitly tracked. |
| #265 | EPIC ENGINE-08: Diagnostics and Source Mapping Completion | Open | n/a | #361 | `docs/diagnostics.md`, #295 | Keep open | #295 remains open; PR #361 only contributed narrow async JSON coverage. |
| #266 | EPIC ENGINE-09: End-to-End Example Apps | Open | n/a | #255, #379 | `tests/conformance/README.md`, examples docs | Keep open | Current roadmap still needs foundation example set and docs reality pass. |
| #296 | TASK ENGINE-09.A: Foundation Example Set | Open | #266 | #255, #379 | `tests/conformance/README.md`, examples docs | Keep open | Still valid until full foundation examples execute through the claimed paths. |
| #297 | TASK ENGINE-09.B: Example Documentation Reality Pass | Open | #266 | #255, #379 | `docs/public/`, examples docs | Keep open | Public/example docs need another reality pass after foundation evidence matures. |
| #267 | EPIC ENGINE-10: Engine Conformance and Packaged Runtime Evidence | Open | n/a | #246, #255 | `docs/project/main-evidence.md`, package docs | Keep open | V8/package evidence remains separate and still active. |
| #298 | TASK ENGINE-10.A: V8-Gated Foundation Conformance | Open | #267 | #305, #379 | V8-gated conformance fixtures | Keep open | Optional V8 evidence must remain explicit. |
| #299 | TASK ENGINE-10.B: Packaged Runtime Outside-Checkout Smoke | Open | #267 | #246 | package tooling and evidence docs | Keep open | Package smoke remains a valid evidence task, not public release readiness. |
| #311 | EPIC ENGINE-13: Proper HTTP Runtime Backend | Open | n/a | #360, #377 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE roadmap issue. |
| #319 | TASK ENGINE-13.A: HTTP Backend Architecture and Platform Boundary | Open | #311 | #360 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE-13 task. |
| #320 | TASK ENGINE-13.B: Connection and Request Lifecycle | Open | #311 | #360 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE-13 task. |
| #321 | TASK ENGINE-13.C: Parser Limits, Timeouts, and Backpressure | Open | #311 | #360 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE-13 task. |
| #322 | TASK ENGINE-13.D: Body Reader and Cancellation Integration | Open | #311 | #360 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE-13 task. |
| #323 | TASK ENGINE-13.E: Graceful Shutdown and Server Diagnostics | Open | #311 | #360 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE-13 task. |
| #324 | TASK ENGINE-13.F: HTTP Backend Stress and Conformance Smoke | Open | #311 | #360 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE-13 task. |
| #312 | EPIC ENGINE-14: Module Loading and Runtime Bootstrap Completion | Open | n/a | #360 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE roadmap issue. |
| #325 | TASK ENGINE-14.A: Bootstrap Asset Loading Contract | Open | #312 | #360 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE-14 task. |
| #326 | TASK ENGINE-14.B: App Module Loading and Cache Policy | Open | #312 | #360 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE-14 task. |
| #327 | TASK ENGINE-14.C: ESM vs Classic Runtime Decision | Open | #312 | #360 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE-14 task. |
| #328 | TASK ENGINE-14.D: Intrinsic Boundary and Import Rewrite Contract | Open | #312 | #360 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE-14 task. |
| #329 | TASK ENGINE-14.E: Module Loading Diagnostics and Tests | Open | #312 | #360 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE-14 task. |
| #313 | EPIC ENGINE-15: Source Maps and Diagnostics Completion | Open | n/a | #360 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE roadmap issue. |
| #330 | TASK ENGINE-15.A: Compiler Source Map Completion | Open | #313 | #360 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE-15 task. |
| #331 | TASK ENGINE-15.B: V8 Exception Source Remapping | Open | #313 | #360 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE-15 task. |
| #332 | TASK ENGINE-15.C: Async Diagnostic JSON and Source Frames | Open | #313 | #360 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE-15 task. |
| #333 | TASK ENGINE-15.D: Redaction and Stable Diagnostic Codes | Open | #313 | #360 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE-15 task. |
| #334 | TASK ENGINE-15.E: Diagnostic Golden Suite | Open | #313 | #360 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE-15 task. |
| #314 | EPIC ENGINE-16: App Host and Resource Lifetime Runtime | Open | n/a | #360, #361 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE roadmap issue for broader lifecycle runtime. |
| #335 | TASK ENGINE-16.A: App Startup and Shutdown Lifecycle | Open | #314 | #360, #361 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Broader ENGINE-16 scope remains beyond ENGINE-07. |
| #336 | TASK ENGINE-16.B: Request Scope and App Scope Ownership | Open | #314 | #360, #361 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Broader ENGINE-16 scope remains beyond ENGINE-07. |
| #337 | TASK ENGINE-16.C: Resource Cleanup on Success/Error/Cancel | Open | #314 | #360, #361 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Broader ENGINE-16 scope remains beyond ENGINE-07. |
| #338 | TASK ENGINE-16.D: Leak-Oriented Test Hooks | Open | #314 | #360 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE-16 task. |
| #339 | TASK ENGINE-16.E: Lifecycle Diagnostics | Open | #314 | #360, #361 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Broader ENGINE-16 diagnostics scope remains. |
| #315 | EPIC ENGINE-17: SQLite Runtime and Data Access Completion | Open | n/a | #360, #379, #399 | `docs/project/engine-13-plus-issue-index.md`, `docs/project/engine-23-provider-execution-issue-index.md` | Keep open | Active ENGINE roadmap issue; depends on provider execution for scalable claims. |
| #340 | TASK ENGINE-17.A: SQLite Public JS API Finalization | Open | #315 | #360, #379 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE-17 task. |
| #341 | TASK ENGINE-17.B: SQLite Capability-Wired Open/Use | Open | #315 | #360, #378, #379 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Broader SQLite runtime completion remains beyond the completed ENGINE-05/06 slices. |
| #342 | TASK ENGINE-17.C: SQLite Transactions and Prepared Statement Decision | Open | #315 | #360, #379 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Transactions/prepared statement policy remains active. |
| #343 | TASK ENGINE-17.D: SQLite Result Mapping and Error Policy | Open | #315 | #360, #386 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE-17 runtime/error-policy task. |
| #344 | TASK ENGINE-17.E: SQLite Users API Runtime Proof | Open | #315 | #360, #379 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE-17 proof task. |
| #316 | EPIC ENGINE-18: CLI and Dev Loop Runtime | Open | n/a | #360 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE roadmap issue. |
| #345 | TASK ENGINE-18.A: sloppyc Build UX and Artifact Inspection | Open | #316 | #360 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE-18 task. |
| #346 | TASK ENGINE-18.B: sloppy Run UX and Source-Input Run Decision | Open | #316 | #360 | `docs/project/engine-13-plus-issue-index.md`, #302 | Keep open | Active ENGINE-18 task; related to but not duplicate of #302. |
| #347 | TASK ENGINE-18.C: Doctor and Audit Real Artifact Checks | Open | #316 | #360 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE-18 task. |
| #348 | TASK ENGINE-18.D: OpenAPI Route Skeleton Policy | Open | #316 | #360 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE-18 task. |
| #349 | TASK ENGINE-18.E: Dev Loop / Watch Decision | Open | #316 | #360 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE-18 task. |
| #317 | EPIC ENGINE-19: Conformance Harness and Runtime Compatibility Suite | Open | n/a | #360 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE roadmap issue. |
| #350 | TASK ENGINE-19.A: Foundation Conformance Matrix | Open | #317 | #360 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE-19 task. |
| #351 | TASK ENGINE-19.B: V8-Gated Runtime Conformance | Open | #317 | #360 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE-19 task. |
| #352 | TASK ENGINE-19.C: HTTP and Async Conformance | Open | #317 | #360, #377, #389 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE-19 task. |
| #353 | TASK ENGINE-19.D: SQLite and Capability Conformance | Open | #317 | #360, #378, #379 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE-19 task. |
| #354 | TASK ENGINE-19.E: Package Outside-Checkout Smoke | Open | #317 | #360 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE-19 task. |
| #318 | EPIC ENGINE-20: Strong Plan Strategic Layer | Open | n/a | #360 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE roadmap issue. |
| #355 | TASK ENGINE-20.A: Typed Plan Graph Model | Open | #318 | #360 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE-20 task. |
| #356 | TASK ENGINE-20.B: Static Validation and Compatibility Strategy | Open | #318 | #360 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE-20 task. |
| #357 | TASK ENGINE-20.C: Plan-Driven Audit and Doctor Strategy | Open | #318 | #360 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE-20 task. |
| #358 | TASK ENGINE-20.D: Plan-Driven OpenAPI/Optimization Future Hooks | Open | #318 | #360 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE-20 task. |
| #359 | TASK ENGINE-20.E: Plan Versioning and Evolution Policy | Open | #318 | #360 | `docs/project/engine-13-plus-issue-index.md` | Keep open | Active ENGINE-20 task. |
| #390 | ENGINE-23: Provider Execution and Blocking Offload Runtime | Open | n/a | #399 | `docs/project/engine-23-provider-execution-issue-index.md` | Keep open | Active ENGINE-23 roadmap issue created by the prior run. |
| #391 | TASK ENGINE-23.A: Provider Operation Descriptor and Ownership Contract | Open | #390 | #399 | `docs/project/engine-23-provider-execution-issue-index.md` | Keep open | Active ENGINE-23 task. |
| #392 | TASK ENGINE-23.B: Per-Provider-Instance Executor Model | Open | #390 | #399 | `docs/project/engine-23-provider-execution-issue-index.md` | Keep open | Active ENGINE-23 task. |
| #393 | TASK ENGINE-23.C: Serialized Blocking Executor for SQLite-Class Providers | Open | #390 | #399 | `docs/project/engine-23-provider-execution-issue-index.md` | Keep open | Active ENGINE-23 task. |
| #394 | TASK ENGINE-23.D: Blocking Pool Executor and Admission Policy | Open | #390 | #399 | `docs/project/engine-23-provider-execution-issue-index.md` | Keep open | Active ENGINE-23 task. |
| #395 | TASK ENGINE-23.E: Provider Cancellation, Timeout, and Late Completion Semantics | Open | #390 | #399 | `docs/project/engine-23-provider-execution-issue-index.md` | Keep open | Active ENGINE-23 task. |
| #396 | TASK ENGINE-23.F: Capability-Gated Provider Dispatch | Open | #390 | #399 | `docs/project/engine-23-provider-execution-issue-index.md` | Keep open | Active ENGINE-23 task. |
| #397 | TASK ENGINE-23.G: Provider Executor Diagnostics and Stress Evidence | Open | #390 | #399 | `docs/project/engine-23-provider-execution-issue-index.md` | Keep open | Active ENGINE-23 task. |
| #398 | TASK ENGINE-23.H: Provider Runtime Integration Guide for SQLite/PostgreSQL/SQL Server | Open | #390 | #399 | `docs/project/engine-23-provider-execution-issue-index.md` | Keep open | Active ENGINE-23 task. |

## 6. Issues Requiring Human Review Because Evidence Is Unclear

| Issue | Title | Current state | Parent EPIC | Related merged PRs | Evidence files/docs/tests | Recommendation | Reason |
| ---: | --- | --- | --- | --- | --- | --- | --- |
| #26 | TASK 01.A: Platform Scanner and Fixtures | Open | old EPIC-01 | #123, #164, #166 | `tools/windows/check-platform-boundaries.ps1`, `tools/unix/check-platform-boundaries.sh`, `.github/workflows/ci.yml` | Needs human review | Platform scanners and CI integration exist, but scanner fixture/self-test hardening is not clearly proven complete. |
| #295 | TASK ENGINE-08.B: Async Diagnostic JSON Surfaces | Open | #265 | #361 | `docs/diagnostics.md`, V8-gated async diagnostic JSON coverage | Needs human review | PR #361 contributes narrowly, but the issue body is broader; do not close without owner confirmation or a narrowed scope. |

## 7. Newly Discovered Duplicates

| Issue | Title | Current state before cleanup | Duplicate/replacement | Recommendation | Reason |
| ---: | --- | --- | --- | --- | --- |
| #32 | TASK 03.B: String Builder / Buffer Foundation | Open | #366 / PR #380 | Close completed | The old issue duplicated the now-implemented ENGINE-21.C builder/byte-builder scope. |

No remaining open exact-title duplicates were found in the post-cleanup issue list.

## Applied Closure Set

Closed as completed:

- #273, #274, #275, #258.
- #260, #261, #262, #291, #263, #264, #306.
- #365, #366, #368, #369, #362, #363.
- #32.

Closed as superseded:

- None in this pass.

Closed as not planned:

- None in this pass.

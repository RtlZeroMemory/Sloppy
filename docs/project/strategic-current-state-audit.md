# Strategic Current State Audit

Status: strategic planning audit plus approved cleanup record.

Audit date: 2026-04-29.

This audit was built from live GitHub state and checked-in source/docs. The first pass did
not mutate GitHub state. After review approval during PR #256, completed/superseded stale
issues were closed with comments that name the replacement evidence or ENGINE roadmap
destination.

Commands used for live issue and PR inventory:

```powershell
gh pr list --repo RtlZeroMemory/Slop --state open --limit 100
gh pr list --repo RtlZeroMemory/Slop --state merged --limit 150
gh issue list --repo RtlZeroMemory/Slop --state open --limit 300
gh issue list --repo RtlZeroMemory/Slop --state closed --limit 300
```

## Strategic Verdict

Slop has moved past the old "minimum alpha path" planning shape. PRs #240-#255 closed the
MAIN and MAIN.1 implementation/evidence slices for their scoped tasks, but the resulting
system is still not ready for public alpha docs. The next strategic phase should be Slop
Engine foundation completion: real compiler pipeline, real V8 runtime integration, real
async/Promise policy, full framework HTTP runtime for APIs, SQLite end-to-end with
capability enforcement, lifecycle/resource cleanup, conformance, and packaged evidence.

PostgreSQL and SQL Server are useful provider foundations, but their JS bridges are not core
foundation blockers. Benchmarks and public alpha docs should remain deferred until the
engine/framework foundation has executable examples and evidence.

## Open PRs

Initial live inventory returned no open pull requests. PR #256 is now the open strategic
planning PR created from this branch.

Recommended action: do not merge PR #256 until the strategic docs, cleanup actions, and
new issue dry-run are reviewed.

## Merged PR Matrix

| PR | Merged | Title | Evidence meaning |
| --- | --- | --- | --- |
| #240 | 2026-04-29 | Define MAIN and MAIN.1 Alpha Roadmaps and Issue Cleanup Plan | Created the narrow MAIN/MAIN.1 planning frame that this audit now supersedes for full engine-foundation planning. |
| #241 | 2026-04-29 | MAIN-01: Executable Artifact-Path Alpha Verification | Proved the two-step compiler artifact path for the narrow supported hello workflow. |
| #242 | 2026-04-29 | MAIN1-07: Resource Lifecycle and JS-Native Handles | Added the generation-checked resource table/handle foundation. |
| #243 | 2026-04-29 | MAIN-02: Evidence and Gate Report | Separated default, V8-gated, package, provider, and benchmark evidence. |
| #244 | 2026-04-29 | MAIN1-01: Compiler Hardening and Supported Syntax Matrix | Documented and tested the current narrow compiler source shape and rejected shapes. |
| #245 | 2026-04-29 | MAIN1-09: Provider Hardening | Hardened native provider reporting and clarified PostgreSQL/SQL Server limits. |
| #246 | 2026-04-29 | MAIN1-12: Packaging and Cross-Platform Hardening | Added experimental package evidence and cross-platform package smoke planning. |
| #247 | 2026-04-29 | MAIN1-06: Diagnostics Completion | Added JSON/source-frame/redaction improvements while source-map remapping remains future work. |
| #248 | 2026-04-29 | MAIN1-02: Plan Schema and Compatibility Hardening | Strengthened Plan sections, compatibility, and artifact hash validation. |
| #249 | 2026-04-29 | MAIN1-05: V8 Runtime Hardening | Hardened V8 owner-thread/shutdown/source diagnostics and explicit Promise rejection. |
| #250 | 2026-04-29 | MAIN1-03: Runtime App Host Hardening | Added native startup validation and minimal request cleanup. |
| #251 | 2026-04-29 | MAIN1-04: HTTP Runtime Hardening | Added native route table/precedence, query/request context, unsupported body diagnostics, and result conversion. |
| #252 | 2026-04-29 | MAIN1-10: Capability and Security Hardening | Added Plan/native capability registry and provider-policy hooks, but not SQLite bridge enforcement. |
| #253 | 2026-04-29 | MAIN1-08: SQLite JS-to-Native Data Bridge | Added V8-gated SQLite bridge through resource IDs. |
| #254 | 2026-04-29 | MAIN1-11: CLI Introspection Hardening | Hardened metadata-only routes/doctor/audit/OpenAPI fixture behavior. |
| #255 | 2026-04-29 | MAIN1-13: End-to-End Conformance Suite | Added default compile/reject conformance and V8-gated run conformance. |

Older merged evidence remains relevant:

- #124/#125: EPIC-21 compiler MVP and EPIC-22 artifact run MVP.
- #162-#166: EPIC-23 through EPIC-26 and a POSIX CI follow-up.
- #117-#119: native SQLite, PostgreSQL, and SQL Server provider foundations.
- #120/#121: benchmark harness and CLI introspection MVP.

## Open Issues After Approved Cleanup

After the approved cleanup pass, the remaining pre-ENGINE open issues are only focused
follow-ups that were not proven complete:

| Issue | Current label state | Recommended action | Reason |
| --- | --- | --- | --- |
| #26 TASK 01.A Platform Scanner and Fixtures | `status:ready` | Keep | Scanner and CI integration exist, but positive/negative scanner fixture or self-test hardening was not proven complete. |
| #32 TASK 03.B String Builder / Buffer Foundation | `status:ready` | Keep | Still a specific missing core primitive. |

## Stale Issues

Closed as completed during PR #256 follow-up:

- #2, #4, #5, #6.
- #28, #29, #34, #35.
- #167, #168, #180-#192.

Closed as not planned / superseded during PR #256 follow-up:

- #193, #194.
- #234-#239.

Still open because they are not proven complete:

- #26 scanner fixture/self-test hardening.
- #32 string builder / buffer foundation.

## Completed But Open Issues

Closed completed after reviewer approval:

- #2: platform abstraction parent completed for foundation scope; #26 remains as a
  specific scanner fixture/self-test follow-up.
- #4: memory foundation parent completed for `SlArena`; #32 remains as a specific
  string/buffer follow-up.
- #5: diagnostics foundation completed by #33 and #34; runtime/source-map diagnostics move
  to ENGINE-08.
- #6: resource lifecycle foundation completed for `SlScope` and resource table/handle
  foundations; broader lifecycle moves to ENGINE-07.
- #28: OS API category documentation completed in `docs/platform-abstraction.md`.
- #29: CI boundary integration completed through lint and CI gates.
- #34: source-frame/JSON diagnostics completed through MAIN1-06 / PR #247.
- #35: resource table foundation completed by PR #242.
- #167: MAIN-01 completed by PR #241.
- #168: MAIN-02 completed by PR #243.
- #180: MAIN1-01 completed by PR #244.
- #181: MAIN1-02 completed by PR #248.
- #182: MAIN1-03 completed by PR #250.
- #183: MAIN1-04 completed by PR #251.
- #184: MAIN1-05 completed by PR #249.
- #185: MAIN1-06 completed by PR #247.
- #186: MAIN1-07 completed by PR #242.
- #187: MAIN1-08 completed by PR #253.
- #188: MAIN1-09 completed by PR #245.
- #189: MAIN1-10 completed by PR #252.
- #190: MAIN1-11 completed by PR #254.
- #191: MAIN1-12 completed by PR #246.
- #192: MAIN1-13 completed by PR #255.

## Superseded Issues

Closed superseded after reviewer approval:

- #194 and #237-#239: replace public alpha docs work with ENGINE-11 Public Alpha Readiness Gate.
- #193 and #234-#236: replace immediate benchmark work with a deferred benchmark gate after engine foundation evidence.
- Any old MAIN/MAIN.1 parent issue that is now only a historical planning container.

## Active Blockers

These are current product blockers before public alpha docs:

- Compiler/runtime: source-input handoff, named/hoisted handlers, module/service/schema
  extraction, runtime Promise settlement, non-GET dispatch, and provider/capability
  enforcement beyond ENGINE-02 metadata.
- V8/runtime: returned Promise support, microtask policy, cancellation-token propagation, bounded completion queues, async error diagnostics, lifecycle cleanup across pending async work.
- HTTP: framework-level API runtime for methods beyond GET, headers/body policy, JSON body parsing, body/header limits, cancellation signal, timeout hooks, backpressure policy, response serialization, error contract.
- SQLite: capability enforcement through the JS/native bridge, cancellation-aware operation boundaries, request/app-scope ownership, transaction/prepared-statement policy, executable source example.
- Security: no OS sandbox claim; enforce capabilities at real bridge points before documenting permissioned data access.
- Conformance: examples must prove realistic API apps through the real compiler/runtime path, not just static bootstrap API shapes.
- Packaging/evidence: packaged runtime smoke must prove the same supported path outside the checkout before public docs.

## Recommended Issue Actions

Keep:

- #26 for platform scanner fixture/self-test hardening.
- #32 for string builder / buffer foundation.

Close completed:

- Completed during PR #256 follow-up: #2, #4, #5, #6, #28, #29, #34, #35, #167, #168,
  #180-#192.

Close superseded:

- Superseded during PR #256 follow-up: #193/#234-#236 and #194/#237-#239.

Close not planned:

- Any issue that asks for public benchmark comparisons, public alpha launch docs, Node/npm compatibility, package-manager behavior, production-grade HTTP server breadth, or PostgreSQL/SQL Server JS bridge work before SQLite and the core engine are solid.

Defer:

- PostgreSQL JS bridge.
- SQL Server JS bridge.
- Benchmark methodology beyond harness smoke.
- Public alpha docs.
- OpenAPI full schemas/security.
- ORM/migrations/query builder.

Replace with new strategic roadmap issue:

- Replace MAIN/MAIN1 follow-up planning with ENGINE-00 through ENGINE-11, staged in
  `tools/github/slop-engine-roadmap-issues.json`.

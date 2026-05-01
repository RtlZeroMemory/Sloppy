# Post-Core Next-Wave Issue Map

Status: 2026-05-01 planning/issues map after PR #430. Framework/API shape lock added in
the follow-up design pass.

This map prevents duplicate EPIC creation after the post-Core MVP consolidation. Existing
issues remain authoritative where they already cover the scope; new issues fill gaps for
framework/app-layer, HTTP post-MVP transport, and immediate hardening.

The framework/API shape locked for this issue wave is
`docs/project/framework-api-shape.md`.

Issue bodies updated with this lock: #302, #318, #355-#359, #432, and #435-#440.

## Existing Issues Reused

| Issue | Title | Decision | New relationship | Reason |
| --- | --- | --- | --- | --- |
| #259 | Compiler Full Supported App Pipeline | Keep/reuse | Parent compiler context for source-input work | Still covers broader compiler pipeline completion. |
| #302 | Direct Source-Input Run Handoff | Keep/reuse | DEVLOOP source-input task | Directly covers `sloppy run <source>` handoff/cache policy; no duplicate DEVLOOP source-input task created. |
| #312 | EPIC-24 Module Loading and Runtime Bootstrap Completion | Keep/reuse | Framework dependency | Bootstrap/module completion feeds framework runtime shape. |
| #325-#329 | ENGINE-14 module/bootstrap tasks | Keep/reuse | Framework dependency tasks | Still cover stdlib asset loading, module loading, cache/reload decisions, import rewrite, and startup diagnostics. |
| #313 | EPIC-25 Source Maps and Diagnostics Completion | Keep/reuse | DEVLOOP/diagnostics dependency | Source-input run and framework diagnostics need these diagnostics lanes. |
| #330-#334 | ENGINE-15 source-map/diagnostic tasks | Keep/reuse | DEVLOOP/diagnostics dependency tasks | Still cover compiler maps, V8 remapping, async diagnostics, redaction, and CLI diagnostic goldens. |
| #314 | EPIC-26 App Host and Resource Lifetime Runtime | Keep/reuse | Framework dependency | Framework request/app scope and resource lifetime depend on this runtime layer. |
| #335-#339 | ENGINE-16 app/resource lifetime tasks | Keep/reuse | Framework dependency tasks | Still cover startup/shutdown, request/app scopes, cancellation, leak hooks, and lifecycle diagnostics. |
| #316 | EPIC-28 CLI and Dev Loop Runtime | Keep/reuse | DEVLOOP parent context | Broad CLI/dev-loop parent already exists. |
| #345-#349 | ENGINE-18 CLI/dev-loop tasks | Keep/reuse | DEVLOOP sibling tasks | Artifact inspect, doctor/audit, OpenAPI skeleton, watch/dev decision, and CLI diagnostics remain valid. |
| #318 | EPIC-30 Strong Plan Strategic Layer | Keep/reuse | PLAN parent | Already owns typed Plan strategic layer; no duplicate PLAN EPIC created. |
| #355-#359 | ENGINE-20 Strong Plan tasks | Keep/reuse | PLAN task set | Already cover graph model, metadata, validation, doctor/audit, OpenAPI hooks, and fast-path registry. |
| #265 | Diagnostics and Source Mapping Completion | Keep/reuse | Diagnostics umbrella | Still useful as a higher-level diagnostics roadmap reference. |
| #295 | Async Diagnostic JSON Surfaces | Keep/reuse | Human-review diagnostics item | Still needs owner review/narrowing before closure. |
| #266 | End-to-End Example Apps | Keep/reuse | Framework examples dependency | Still valid once source-input and framework ergonomics are real. |
| #296 | Foundation Example Set | Keep/reuse | Framework examples dependency | Still valid for executable examples hardening. |
| #297 | Example Documentation Reality Pass | Keep/reuse | Framework examples dependency | Still valid for honest example docs. |
| #268 | Public Alpha Readiness Gate | Keep/reuse | Public-alpha blocker | Public alpha remains blocked. |
| #300 | Public Alpha Readiness Checklist | Keep/reuse | Public-alpha blocker | Still valid; this PR does not claim public alpha. |
| #301 | Public Non-Claims Review | Keep/reuse | Public-alpha blocker | Still valid for no production/performance/package overclaims. |
| #26 | Platform Scanner and Fixtures | Keep/reuse | HARDEN-01.D equivalent | Existing issue already covers platform scanner fixture/self-test proof. |
| #431 | Track SQLite V8 parameter preflight allocation cap | Keep/reuse | HARDEN-01.B equivalent | Existing issue already covers SQLite V8 parameter-count preflight. |

## New Issues Created

| Issue | Title | Relationship | Reason |
| --- | --- | --- | --- |
| #432 | EPIC FRAMEWORK-01: Framework/App Layer Source of Truth and Developer Ergonomics | New framework EPIC | No current kept-open issue owned the full post-Core framework/app-layer wave. |
| #435 | TASK FRAMEWORK-01.A: Framework Layer Architecture and Public Surface Contract | Child of #432 | Locks public surface before implementation. |
| #436 | TASK FRAMEWORK-01.B: Configuration Model and Environment/CLI Binding | Child of #432 | Implemented first config model: appsettings/env/CLI binding, typed access, `bind`, config-driven SQLite provider metadata, and redacted Plan metadata. |
| #437 | TASK FRAMEWORK-01.C: Request Binding for Route, Query, Header, and Body | Child of #432 | Needed before honest app-layer request ergonomics. |
| #438 | TASK FRAMEWORK-01.D: Validation and Safe Error Response Policy | Child of #432 | Needed for safe framework diagnostics and error responses. |
| #439 | TASK FRAMEWORK-01.E: Results/Response Model Completion | Child of #432 | Needed before framework response API claims. |
| #440 | TASK FRAMEWORK-01.F: Framework Examples Hardening | Child of #432 | Coordinates executable, honest examples after source-input/framework work. |
| #433 | EPIC HTTP-25: HTTP/1.1 Keep-Alive and Streaming | New HTTP EPIC | Existing HTTP MVP issues intentionally stopped at close-after-response transport. |
| #441 | TASK HTTP-25.A: Keep-Alive State Machine and Connection Loop | Child of #433 | First sequential keep-alive slice. |
| #442 | TASK HTTP-25.B: Idle Timeout and Max Requests Per Connection | Child of #433 | Required connection budget policy. |
| #443 | TASK HTTP-25.C: Sequential Request Lifecycle Reset | Child of #433 | Required per-request cleanup/reset behavior. |
| #444 | TASK HTTP-25.D: Chunked Request Decoding | Child of #433 | Post-MVP request body transport behavior. |
| #445 | TASK HTTP-25.E: Streaming Response Writer | Child of #433 | Post-MVP response writing behavior. |
| #446 | TASK HTTP-25.F: Keep-Alive/Streaming Stress and Conformance | Child of #433 | Evidence lane for the HTTP-25 behavior. |
| #434 | EPIC HARDEN-01: Post-Core Foundation Hardening | New hardening EPIC | Groups small source-of-truth cleanup before/alongside implementation. |
| #447 | TASK HARDEN-01.A: HTTP Transport Boundary Cleanup | Child of #434 | Retire/hide public smoke and direct dev-run libuv boundary debt. |
| #448 | TASK HARDEN-01.C: Provider Primitive Cleanup Plan | Child of #434 | Narrows provider primitive adoption follow-up without provider expansion. |

## Not Created

| Proposed item | Decision | Reason |
| --- | --- | --- |
| DEVLOOP-01 EPIC and source-input subtasks | Not created | #259/#302 and #316/#345-#349 already cover the source-input and CLI/dev-loop scopes. |
| PLAN-01 EPIC and Strong Plan subtasks | Not created | #318/#355-#359 already cover the Strong Plan strategic layer. |
| HARDEN-01.B SQLite V8 Parameter Preflight | Not created | #431 already covers the exact safety issue. |
| HARDEN-01.D Platform Scanner Fixture/Self-Test Proof | Not created | #26 already covers the exact scanner fixture/self-test proof. |
| PostgreSQL/SQL Server JS bridge implementation tasks | Not created | Provider expansion remains later; only primitive cleanup planning was created. |

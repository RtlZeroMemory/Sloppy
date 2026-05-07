# Post-ENGINE-16 Documentation and Issue Reconciliation

Status: 2026-05-05 consolidation record. This document records the source-of-truth reset
after ENGINE-15/16, COMPILER-30, Strong Plan consumers, HTTP-25, and framework example
hardening. It is a planning record, not runtime implementation evidence.

## Active PR State

The audit ran:

- `gh pr list --repo RtlZeroMemory/Slop --state open --limit 100`
- `gh pr list --repo RtlZeroMemory/Slop --state merged --limit 250`

At audit time there were no open PRs. Recent merged evidence includes ENGINE-15,
ENGINE-16, COMPILER-30, Strong Plan consumer work, framework example hardening, and
HTTP-25 slices. Active PRs were not assumed merged.

## Docs Created In This PR

- `docs/project/archive/post-engine-16/post-engine-16-execution-model-audit.md`
- `docs/project/archive/post-engine-16/post-engine-16-runtime-modularity-audit.md`
- `docs/project/archive/post-engine-16/post-engine-16-provider-runtime-audit.md`
- `docs/project/archive/post-engine-16/post-engine-16-http-runtime-audit.md`
- `docs/project/archive/post-engine-16/post-engine-16-lifecycle-memory-audit.md`
- `docs/project/archive/post-engine-16/post-engine-16-diagnostics-observability-audit.md`
- `docs/project/archive/post-engine-16/post-engine-16-docs-issue-reconciliation.md`
- `docs/project/engine-roadmap-2.md`
- `docs/project/engine-roadmap-2-issue-index.md`

## Canonical Docs Updated

This PR updates the current source-of-truth docs to point at Roadmap-2 and to preserve
implemented-vs-deferred boundaries:

- `docs/roadmap.md`
- `docs/architecture.md`
- `docs/execution-model.md`
- `docs/concurrency.md`
- `docs/memory.md`
- `docs/diagnostics.md`
- `docs/testing-strategy.md`
- `docs/quality-gates.md`
- `docs/quality-score.md`
- `docs/tech-debt-tracker.md`
- `docs/project/README.md`
- `docs/modules/http/README.md`
- `docs/modules/data/README.md`
- `docs/modules/engine-v8/README.md`
- `docs/modules/app-host/README.md`
- `docs/modules/plan/README.md`

## Archived Or Superseded Project Docs

The post-Core MVP compact audit documents remain useful as historical records, but they
are no longer the forward roadmap. They are superseded by the post-ENGINE-16 audit set and
Roadmap-2 issue index for new runtime maturation work.

Historical source records retained for evidence:

- `docs/project/archive/post-alpha-transition/post-core-mvp-code-reality-audit.md`
- `docs/project/archive/post-alpha-transition/post-core-mvp-boundary-audit.md`
- `docs/project/archive/post-alpha-transition/post-core-mvp-memory-string-audit.md`
- `docs/project/archive/post-alpha-transition/post-core-mvp-next-roadmap.md`
- `docs/project/archive/post-alpha-transition/post-core-next-wave-issue-map.md`
- `docs/project/framework-api-shape.md`
- `docs/project/strong-plan-strategic-layer-plan.md`
- `docs/project/compiler-inference-engine-architecture.md`
- `docs/project/compiler-inference-issue-index.md`
- `docs/project/archive/http/http-post-mvp-transport-plan.md`
- `docs/project/engine-framework-contract.md`

No public alpha docs are created. No benchmark/performance claims are added.

## Issues Closed As Completed

These closures are high-confidence parent/task cleanup after merged evidence is present:

- `#433` EPIC HTTP-25: HTTP/1.1 Keep-Alive and Streaming.
- `#434` EPIC HARDEN-01: Post-Core Foundation Hardening.
- `#312` EPIC ENGINE-14: Module Loading and Runtime Bootstrap Completion.
- `#346` TASK ENGINE-18.B: sloppy Run UX and Source-Input Run Decision.
- `#347` TASK ENGINE-18.C: Doctor and Audit Real Artifact Checks.
- `#348` TASK ENGINE-18.D: OpenAPI Route Skeleton Policy.
- `#435` TASK FRAMEWORK-01.A: Framework Layer Architecture and Public Surface Contract.
- `#265` EPIC ENGINE-08: Diagnostics and Source Mapping Completion.
- `#295` TASK ENGINE-08.B: Async Diagnostic JSON Surfaces.
- `#266` EPIC ENGINE-09: End-to-End Example Apps.
- `#296` TASK ENGINE-09.A: Foundation Example Set.
- `#297` TASK ENGINE-09.B: Example Documentation Reality Pass.

Closure comments use the required completed evidence format in GitHub.

## Issues Kept Open

These remain valid future work and are not treated as completed by this audit:

- `#259` EPIC ENGINE-02: Compiler Full Supported App Pipeline.
- `#316` EPIC ENGINE-18: CLI and Dev Loop Runtime.
- `#318` EPIC ENGINE-20: Strong Plan Strategic Layer.
- `#355`, `#356`, `#359`: remaining Strong Plan typed graph/static/versioning tasks.
- `#345` and `#349`: remaining CLI/dev-loop artifact inspection and watch/dev-loop tasks.
- `#432` and remaining FRAMEWORK-01 tasks `#437`, `#438`, `#439`.
- `#268`, `#300`, `#301`: public alpha readiness and non-claims remain blocked.

## Issues Needing Human Review

- `#259` is still broad and old. Keep it open until the owner decides whether it should be
  narrowed, superseded by compiler/Roadmap-2 issues, or closed after remaining source-input
  compiler/dev-loop work is mapped elsewhere.
- `#318` remains open because Strong Plan still has typed graph/static/versioning work
  beyond the completed route/capabilities/doctor/openapi consumer slices.

## Roadmap-2 Issue Work

This PR creates or reuses Engine Roadmap-2 issues for:

- `#488` ENGINE-26: Execution Model Hardening, with tasks `#494` through `#499`.
- `#489` ENGINE-27: Runtime Feature Modularity, with tasks `#500` through `#505`.
- `#490` ENGINE-28: Provider Runtime Maturation, with tasks `#506` through `#511`.
- `#491` HTTP-26: Route-Level HTTP Policy and Observability, with tasks `#512` through
  `#517`.
- `#492` ENGINE-29: Runtime Events and Metrics, with tasks `#518` through `#523`.
- `#493` ENGINE-30: Runtime Torture and Crash-Resistance Harness, with tasks `#524`
  through `#529`.

The authoritative issue-number map lives in `docs/project/engine-roadmap-2-issue-index.md`.

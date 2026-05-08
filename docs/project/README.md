# Project Documentation Map

This directory contains project contracts, planning snapshots, historical
records, and issue-creation inputs. Repository docs and ADRs describe durable
design contracts. GitHub issues and pull requests own live task state. Local
issue indexes are creation or navigation snapshots, not authoritative status.

## Current Contracts

Use these documents when implementing or reviewing current behavior:

- `docs/architecture.md`
- `docs/execution-model.md`
- `docs/concurrency.md`
- `docs/memory.md`
- `docs/diagnostics.md`
- `docs/security-permissions.md`
- `docs/developer-ergonomics.md`
- `docs/app-plan.md`
- `docs/compiler.md`
- `docs/compiler-supported-syntax.md`
- `docs/testing-strategy.md`
- `docs/quality-gates.md`
- `docs/documentation-policy.md`
- `docs/project/core-api-platform-map.md`
- `docs/project/config-api-architecture.md`
- `docs/project/codec-api-architecture.md`
- `docs/project/crypto-api-architecture.md`
- `docs/project/filesystem-api-architecture.md`
- `docs/project/time-api-architecture.md`
- `docs/project/network-api-architecture.md`
- `docs/project/os-api-architecture.md`
- `docs/project/local-ipc-api-architecture.md`
- `docs/project/http-client-api-architecture.md`
- `docs/project/compiler-inference-engine-architecture.md`
- `docs/project/provider-execution-runtime-architecture.md`
- `docs/project/http-transport-runtime-architecture.md`
- `docs/project/framework-api-shape.md`
- `docs/project/framework-v2-current-state.md`
- `docs/project/physical-modularity-current-state.md`
- `docs/project/engine-19-conformance-matrix.md`
- `docs/project/main-evidence.md`
- `docs/project/alpha-infra-readiness.md`
- `docs/project/test-platform-inventory.md`

`docs/project/framework-api-shape.md` remains the Framework-01 baseline.
`docs/project/framework-v2-current-state.md` records the current metadata-only
Framework v2 compiler state and the runtime/provider deferrals until a fuller
Framework v2 contract replaces it.

## Current Planning

- `docs/roadmap.md` is the current repository roadmap.
- `docs/project/roadmap-main.md` and
  `docs/project/roadmap-main-1-hardening.md` remain active while the main-lane
  contract checks and active execution plans reference them.
- `docs/project/issue-workflow.md` and `docs/project/pr-workflow.md` describe
  issue and PR mechanics.
- `docs/project/prompts/` contains reusable internal prompts for scoped work.

GitHub is authoritative for issue state, blocking relationships, review status,
and merge readiness.

## Issue Snapshots

The files under `docs/project/tasks/`, `docs/project/epics/`, and the
`*-issue-index.md` files are snapshots or issue-creation inputs. They can help
recover intent, but they must not be treated as current status if GitHub differs.

## Historical Records

Historical audits and construction-phase planning records live under:

- `docs/project/archive/post-alpha-transition/`
- `docs/project/archive/post-engine-16/`
- `docs/project/archive/http/`
- `docs/project/archive/post-core-mvp/`

Archived documents may retain old issue names, task handles, and paths because
they are evidence records. Current docs should link to archives only when the
historical context is useful.

## Documentation Rule

Before changing behavior, identify the governing source doc. If the source doc
is stale, update it with the implementation and tests. If a task is docs-only,
state why code and tests did not change.

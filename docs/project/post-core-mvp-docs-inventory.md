# Post-Core MVP Docs Inventory

Status: 2026-05-01 compaction record. Active `docs/project` root was reduced; historical
records that still had context value moved under `docs/project/archive/post-core-mvp/`.

## Active Canonical Docs

| Path | Classification | Action | Reason |
| --- | --- | --- | --- |
| `docs/project/README.md` | canonical / keep | Updated | Project model entry point. |
| `docs/project/issue-workflow.md` | canonical / keep | Kept | GitHub issue workflow. |
| `docs/project/pr-workflow.md` | canonical / keep | Kept | PR workflow. |
| `docs/project/engine-framework-contract.md` | canonical / keep | Kept | Framework/engine contract source. |
| `docs/project/slop-engine-final-shape.md` | canonical / keep | Kept | Compact final shape source. |
| `docs/project/slop-engine-layered-roadmap.md` | canonical / keep | Kept, link-updated | Historical layer map still useful, but no longer the sole current roadmap. |
| `docs/project/engine-19-conformance-matrix.md` | canonical / keep | Kept | Evidence-lane source of truth. |
| `docs/project/http-transport-runtime-architecture.md` | canonical / keep | Kept | Current transport architecture source. |
| `docs/project/provider-execution-runtime-architecture.md` | canonical / keep | Kept | Provider executor architecture source. |
| `docs/project/provider-runtime-integration-guide.md` | canonical / keep | Kept | Provider integration guide. |
| `docs/project/main-evidence.md` | historical evidence / keep | Kept | Still referenced by MAIN contract checks. |
| `docs/project/roadmap-main.md` | historical evidence / keep | Kept | Still referenced by MAIN contract checks. |
| `docs/project/roadmap-main-1-hardening.md` | historical evidence / keep | Kept | Historical MAIN.1 input retained for now. |
| `docs/project/tasks/**` | canonical task archive | Kept | Task records remain useful for issue/task mapping. |
| `docs/project/epics/**` | canonical epic archive | Kept | Original epic records remain useful. |
| `docs/project/prompts/**` | canonical workflow templates | Kept | Agent workflow templates. |

## New Compact Reports

| Path | Classification | Action | Reason |
| --- | --- | --- | --- |
| `docs/project/post-core-mvp-code-reality-audit.md` | compact audit | Added | Replaces scattered current-state/code reality docs. |
| `docs/project/post-core-mvp-docs-inventory.md` | compact inventory | Added | Records compaction decisions. |
| `docs/project/post-core-mvp-issue-reconciliation.md` | compact issue audit | Added | Replaces old issue cleanup/audit docs. |
| `docs/project/post-core-mvp-memory-string-audit.md` | compact adoption audit | Added | Replaces old memory/string audit/adoption maps for current reporting. |
| `docs/project/post-core-mvp-boundary-audit.md` | compact boundary audit | Added | Captures architecture-rule findings. |
| `docs/project/post-core-mvp-next-roadmap.md` | compact roadmap proposal | Added | Replaces old next-roadmap draft. |

## Archived

| Path | Classification | Action | Replacement |
| --- | --- | --- | --- |
| `docs/project/engine-13-plus-architecture.md` | historical architecture | Archived | `docs/project/slop-engine-final-shape.md`, `docs/project/post-core-mvp-next-roadmap.md` |
| `docs/project/engine-13-plus-issue-index.md` | historical issue index | Archived | `docs/project/post-core-mvp-issue-reconciliation.md` |
| `docs/project/engine-21-22-issue-index.md` | historical issue index | Archived | `docs/project/post-core-mvp-memory-string-audit.md` |
| `docs/project/engine-23-provider-execution-issue-index.md` | historical issue index | Archived | `docs/project/provider-execution-runtime-architecture.md` |
| `docs/project/memory-string-current-state-audit.md` | historical audit | Archived | `docs/project/post-core-mvp-memory-string-audit.md` |
| `docs/project/memory-string-foundation-architecture.md` | historical architecture | Archived | `docs/memory.md` |
| `docs/project/memory-string-adoption-map.md` | historical adoption map | Archived | `docs/project/post-core-mvp-memory-string-audit.md` |
| `docs/project/archive/post-core-mvp/provider-execution-current-state-audit.md` | historical audit | Archived | `docs/project/provider-execution-runtime-architecture.md` |
| `docs/project/strategic-current-state-audit.md` | historical audit | Archived | `docs/roadmap.md`, `docs/project/post-core-mvp-next-roadmap.md` |
| `docs/project/strategic-system-audit.md` | historical audit | Archived | `docs/project/post-core-mvp-code-reality-audit.md` |

## Deleted

| Path | Classification | Action | Reason |
| --- | --- | --- | --- |
| `docs/project/barebones-and-advanced-feature-inventory.md` | obsolete audit | Deleted | Superseded by code reality and tech-debt reports. |
| `docs/project/current-issue-state-audit.md` | obsolete issue audit | Deleted | Superseded by post-core issue reconciliation. |
| `docs/project/engine-24-http-transport-issue-index.md` | obsolete issue index | Deleted | ENGINE-24 children are closed; retained evidence lives in issue reconciliation and transport architecture. |
| `docs/project/github-issue-cleanup-result.md` | obsolete cleanup result | Deleted | Superseded by post-core issue reconciliation. |
| `docs/project/github-issue-hygiene-audit.md` | obsolete cleanup audit | Deleted | Superseded by post-core issue reconciliation. |
| `docs/project/http-transport-current-state-audit.md` | obsolete audit | Deleted | Superseded by code reality and boundary reports. |
| `docs/project/main-main1-issue-cleanup-plan.md` | obsolete cleanup plan | Deleted | Applied historical plan; current tracker is live issue reconciliation. |
| `docs/project/main-main1-scope.md` | obsolete roadmap scope | Deleted | Superseded by post-core roadmap proposal. |
| `docs/project/main-main1-system-audit.md` | obsolete audit | Deleted | Superseded by code reality and boundary reports. |
| `docs/project/next-roadmap.md` | obsolete roadmap draft | Deleted | Superseded by post-core next roadmap. |
| `docs/project/post-0.7-issue-audit.md` | obsolete issue audit | Deleted | Superseded by post-core issue reconciliation. |
| `docs/project/strategic-issue-cleanup-plan.md` | obsolete cleanup plan | Deleted | Applied/superseded. |
| `docs/project/strategic-pushback.md` | obsolete checklist | Deleted | Useful cautions folded into roadmap/non-goals. |
| `docs/project/labels.md` | stale generated reference | Deleted | Rendered stale/incorrect label names; GitHub remains source. |
| `docs/project/milestones.md` | stale generated reference | Deleted | Older milestone summary; GitHub/tooling metadata remains source. |

## Human Review

- Decide whether `roadmap-main.md` and `roadmap-main-1-hardening.md` should also move to
  archive after `tests/cmake/check_main_contract_docs.cmake` no longer references them.
- Decide whether future issue-generation JSON should keep historical source-doc paths or
  be regenerated from the compact roadmap after owner approval.

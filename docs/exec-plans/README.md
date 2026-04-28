# Execution Plans

Execution plans are used for complex multi-step work. Active plans are checked into
`docs/exec-plans/active`; completed plans move to `docs/exec-plans/completed`. Plans
contain progress and decision logs, and agents update them as they work. Execution plans
prevent chat-only decisions.

Small changes may use lightweight in-prompt plans instead of a checked-in plan.

## Template

```markdown
# Execution Plan: <title>

## Goal

## Source Docs

## Non-goals

## Scope

## Steps

## Acceptance Criteria

## Validation Commands

## Decision Log

## Progress Log

## Risks

## Completion Notes
```

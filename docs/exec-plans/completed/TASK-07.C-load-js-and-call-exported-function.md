# Execution Plan: TASK 07.C Load JS and Call Exported Function

## Goal

Implement the minimal V8 bridge smoke path behind `include/sloppy/engine.h`.

## Source Docs

AGENTS.md, CONTRIBUTING.md, architecture, execution model, concurrency, app plan,
compiler, dependencies, build/distribution, C standards/style, platform abstraction,
diagnostics, memory, testing, quality gates, engine-v8 module docs, plan module docs,
review playbook, C/build/development skills, roadmap, Issue #42, and EPIC-07.

## Non-goals

No Sloppy Plan execution, handler table integration, HTTP, routing, event loop, async,
TypeScript compilation, module resolver, public JS API, Node compatibility, package
manager behavior, workers, inspector, or snapshots.

## Scope

- Add source evaluation and zero-argument global function call API.
- Implement V8 create/eval/call/destroy only under `src/engine/v8/`.
- Keep default non-V8 builds working.
- Add V8-gated smoke coverage and update docs/debt notes.

## Steps

1. Refresh from `origin/main` and create the feature branch.
2. Read source docs and issue/epic context.
3. Extend the engine ABI with classic script eval and global function call.
4. Add V8 bridge implementation and CMake gating.
5. Add noop/default and V8-gated tests.
6. Update docs and tech debt.
7. Run required gates and open a normal PR.

## Acceptance Criteria

- V8 code lives under `src/engine/v8/`.
- No V8 headers/types leak into public/core headers.
- Default non-V8 build remains SDK-free.
- V8-enabled build can evaluate a tiny source and call `sloppy_smoke`.
- String results are copied into caller-provided arena memory.
- Missing/throwing functions fail without crashing.

## Validation Commands

See PR body for exact command outcomes.

## Decision Log

- Reused existing `SlEngineResult` text kind instead of adding a parallel string result
  model.
- Added `sl_engine_eval_source` and `sl_engine_call_function0` as smoke APIs rather than
  forcing handler IDs before TASK 08.
- Used classic script/global function semantics only; ESM and module loading are deferred.
- Kept process-wide V8 platform state alive for process lifetime; per-engine destroy
  releases isolate/context resources only.

## Progress Log

- Branch created from fresh `origin/main`.
- ABI, bridge implementation, V8-gated test, docs, and debt notes updated.
- Review fixes added for process-lifetime V8 platform state and failed-create arena
  behavior.

## Risks

- V8 SDK availability is machine-local until the verified SDK fetch workflow lands.
- Exception diagnostics are intentionally basic until TASK 07.D.

## Completion Notes

Completed in this PR as a bounded EPIC-07 smoke slice.

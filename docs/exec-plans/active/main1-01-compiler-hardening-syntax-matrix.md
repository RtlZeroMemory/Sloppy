# Execution Plan: MAIN1-01 Compiler Hardening and Syntax Matrix

## Goal

Define and harden the alpha compiler source subset so supported shapes have golden fixtures,
unsupported shapes fail clearly, and source-input `sloppy run <source.js>` has an explicit
alpha policy.

## Source Docs

- `AGENTS.md`
- `CONTRIBUTING.md`
- `docs/project/post-core-mvp-next-roadmap.md`
- `docs/project/roadmap-main-1-hardening.md`
- `docs/project/post-core-mvp-code-reality-audit.md`
- `docs/compiler.md`
- `docs/app-plan.md`
- `docs/execution-model.md`
- `docs/developer-ergonomics.md`
- `docs/public/app-model.md`
- `docs/public/routing.md`
- `docs/public/cli.md`
- `docs/js-ts-standards.md`
- `docs/rust-standards.md`
- `docs/testing.md`
- `docs/testing-strategy.md`
- `docs/quality-gates.md`
- `docs/tech-debt-tracker.md`
- `docs/review-playbook.md`
- GitHub issues #180, #195, #196, and #197

## Non-goals

- No full TypeScript compiler.
- No bundler or arbitrary module graph.
- No package-manager behavior.
- No Node compatibility.
- No runtime/V8 changes.
- No middleware, services, modules, data provider, decorator, or dynamic route extraction.

## Scope

- Add `docs/compiler-supported-syntax.md` and link it from compiler/public docs.
- Expand supported compiler golden fixtures for documented supported result/handler shapes.
- Expand rejected diagnostic fixtures for dynamic/computed/loop/conditional/import/handler
  shapes.
- Keep rejected builds from producing success artifacts.
- Document that alpha keeps the two-step artifact workflow.

## Steps

1. Verify MAIN-01 prerequisite and branch from fresh `origin/main`.
2. Read source docs, issues, compiler code, and existing fixtures.
3. Patch compiler diagnostics and fixture harness.
4. Add supported/rejected fixtures and syntax matrix docs.
5. Run required gates, review scope, commit, push, and open a normal PR.

## Acceptance Criteria

- Supported compiler syntax matrix exists and is linked.
- Supported fixtures cover documented supported shapes.
- Rejected fixtures cover dynamic/unsupported shapes with stable diagnostics.
- Unsupported input fails clearly and does not silently partially extract.
- Source-input run handoff policy is documented.
- No package-manager/Node compatibility scope creep.

## Validation Commands

- `.\tools\windows\bootstrap.ps1`
- `.\tools\windows\dev.ps1 configure`
- `.\tools\windows\dev.ps1 build`
- `.\tools\windows\dev.ps1 test`
- `.\tools\windows\dev.ps1 format-check`
- `.\tools\windows\dev.ps1 lint`
- `.\tools\windows\check-js-ts-standards.ps1`
- `.\tools\windows\check-rust-standards.ps1`
- `cargo fmt --manifest-path compiler/Cargo.toml -- --check`
- `cargo clippy --manifest-path compiler/Cargo.toml -- -D warnings`
- `cargo test --manifest-path compiler/Cargo.toml`
- `git diff --check`
- `git status --short --ignored`

## Decision Log

- MAIN1-01 keeps `sloppyc build ... --out .sloppy` followed by
  `sloppy run --artifacts .sloppy` as the alpha workflow.
- `sloppy run <source.js>` remains deferred until compiler handoff, cache keys, stale
  artifact checks, source diagnostics, and rebuild policy are designed.
- Named handler identifiers remain rejected because the current compiler copies inline
  handler source slices and has no honest closure/source-copy policy for declarations.

## Progress Log

- Verified prerequisite PR #241 is merged to `main`.
- Created `feature/main1-01-compiler-hardening-syntax-matrix` from fresh `origin/main`.
- Added compiler diagnostics for computed route methods, non-GET route methods, loop route
  registration, conditional route registration, unsupported imports with spans, and
  rejected-build no-artifact behavior.
- Added supported `results-json` and `function-handler` fixtures.
- Added rejected computed, loop, conditional, Node import, named handler, and non-GET
  method fixtures.
- Added and linked the syntax matrix and source-input run policy docs.

## Risks

- Full JSON/source-frame diagnostics remain deferred to MAIN1-06.
- Source maps remain deterministic placeholders.
- Default gates still do not prove V8 runtime success.

## Completion Notes

Fill in final gate results in the PR body.

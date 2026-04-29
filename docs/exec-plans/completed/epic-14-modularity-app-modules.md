# Execution Plan: EPIC-14 Modularity App Modules

## Goal

Implement the bootstrap app module skeleton as one coherent EPIC-14 PR covering issues
#63, #64, #65, and #66.

## Source Docs

- `AGENTS.md`
- `CONTRIBUTING.md`
- `docs/architecture.md`
- `docs/developer-ergonomics.md`
- `docs/modularity.md`
- `docs/execution-model.md`
- `docs/compiler.md`
- `docs/app-plan.md`
- `docs/data-providers.md`
- `docs/public/modules.md`
- `docs/public/app-model.md`
- `docs/public/routing.md`
- `docs/public/services.md`
- `docs/public/diagnostics.md`
- `docs/modules/app-host/README.md`
- `docs/modules/http/README.md`
- `docs/modules/data/README.md`
- `docs/modules/plan/README.md`
- `docs/testing-strategy.md`
- `docs/testing.md`
- `docs/quality-gates.md`
- `docs/documentation-policy.md`
- `docs/review-playbook.md`
- `docs/skills/development-skill.md`
- `docs/roadmap.md`
- GitHub issues #15, #63, #64, #65, and #66

## Non-goals

No compiler extraction, real `app.plan.json` emission, native module/plugin loading,
package distribution, data providers, middleware, route filters, HTTP server behavior,
`app.run`/`app.listen`, package-manager work, or Node compatibility claims.

## Scope

- Add `Sloppy.module(name)` bootstrap API.
- Add `builder.addModule(module)` and deterministic module build phases.
- Add dependency graph validation, missing dependency errors, cycle errors, duplicate name
  errors, invalid module errors, and phase callback context errors.
- Attribute module-created services and routes where the current bootstrap model allows it.
- Expose plan-like debug metadata through app introspection.
- Add tests, examples, and docs matching the implemented skeleton.

## Steps

1. Implement module API and builder integration in `stdlib/sloppy/app.js`.
2. Add executable bootstrap tests and static API/example checks.
3. Add `examples/modules-basic/`.
4. Update public docs, module docs, roadmap/status docs, and debt tracker.
5. Run requested quality gates.
6. Commit, push, and open a normal PR.

## Acceptance Criteria

- `Sloppy.module` and `builder.addModule` exist.
- Module dependency declarations work in deterministic dependency order.
- Duplicate module names, invalid module objects, missing dependencies, cycles, and phase
  failures produce useful errors.
- Module services and routes can be contributed to the built app.
- Module debug metadata includes module order, dependencies, service tokens, route
  contributions, and custom metadata.
- Docs and examples are honest about bootstrap-only scope and future plan/compiler work.

## Validation Commands

- `.\tools\windows\bootstrap.ps1`
- `.\tools\windows\dev.ps1 configure`
- `.\tools\windows\dev.ps1 build`
- `.\tools\windows\dev.ps1 test`
- `.\tools\windows\dev.ps1 format-check`
- `.\tools\windows\dev.ps1 lint`
- `cargo fmt --manifest-path compiler/Cargo.toml -- --check`
- `cargo clippy --manifest-path compiler/Cargo.toml -- -D warnings`
- `cargo test --manifest-path compiler/Cargo.toml`
- `git status --short --ignored`

## Decision Log

- 2026-04-29: Keep EPIC-14 in the bootstrap stdlib only. Real plan emission and compiler
  extraction remain future work.
- 2026-04-29: Use lowercase module identifiers starting with a lowercase letter and then
  lowercase letters, digits, dots, or hyphens.
- 2026-04-29: Execute services for all modules before routes for all modules, each phase
  in topological order, so route handlers can resolve services from dependencies.

## Progress Log

- 2026-04-29: Refreshed from `origin/main` after EPIC-13 merged and recreated
  `feature/14-modularity-app-modules` from fresh `origin/main`.
- 2026-04-29: Read source docs and current EPIC-13 app-host implementation.
- 2026-04-29: Implemented bootstrap module API, builder integration, dependency graph,
  phases, attribution, debug metadata, tests, examples, and docs.
- 2026-04-29: Ran requested non-V8 quality gates successfully. `SLOPPY_V8_ROOT` was not
  set, so V8-enabled checks were not available.

## Risks

- Accidentally implying final `app.plan.json` emission from bootstrap debug metadata.
- Overbuilding a plugin/package system instead of a simple module graph skeleton.
- Letting module route/service attribution require a heavy rewrite of current bootstrap
  internals.

## Completion Notes

Completed implementation and validation for the EPIC-14 bounded bootstrap module skeleton.
PR publication is tracked in the final response and PR body.

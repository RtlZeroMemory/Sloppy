# Execution Plan: EPIC-12 App Host Foundation

## Goal

Implement the bootstrap stdlib app-host foundation as one coherent bounded context:
builder/app freeze model, config skeleton, logging skeleton, services skeleton, tests, and
honest docs.

## Source Docs

- AGENTS.md
- CONTRIBUTING.md
- docs/architecture.md
- docs/developer-ergonomics.md
- docs/execution-model.md
- docs/compiler.md
- docs/app-plan.md
- docs/modularity.md
- docs/data-providers.md
- docs/public/app-model.md
- docs/public/routing.md
- docs/public/results.md
- docs/public/config.md
- docs/public/logging.md
- docs/public/services.md
- docs/modules/app-host/README.md
- docs/modules/http/README.md
- docs/modules/engine-v8/README.md
- docs/testing-strategy.md
- docs/testing.md
- docs/quality-gates.md
- docs/documentation-policy.md
- docs/review-playbook.md
- docs/skills/development-skill.md
- docs/roadmap.md
- GitHub issues #13, #55, #56, #57, and #58

## Non-goals

- No `app.run` or `app.listen`.
- No real HTTP server or native HTTP app-host integration.
- No compiler extraction or `app.plan.json` emission.
- No modules, route groups, middleware, filters, validation, or data providers.
- No package-manager behavior or Node compatibility claims.
- No config file/env/CLI providers.
- No console/file/native logging sinks.
- No scoped request lifetime, disposal hooks, async factories, or typed service tokens.

## Scope

- Extend `stdlib/sloppy/app.js` without replacing the existing tiny app facade.
- Keep `Sloppy.create()` working and align it with builder-built apps.
- Add minimal object-backed config, memory logging, and string-token services.
- Add structural app freeze behavior and route introspection for bootstrap tests/debugging.
- Update examples and docs to describe implemented behavior and deferred work.

## Steps

1. Implement the app-host skeleton in the bootstrap stdlib.
2. Add executable JS smoke coverage when a local JS host is available, with static CTest
   checks remaining as the default structural coverage.
3. Update the hello example to demonstrate the builder path while preserving the current
   relative stdlib import.
4. Update public docs, module docs, testing docs, quality score, and tech debt tracker.
5. Run available checks and report any unavailable gates honestly.
6. Commit, push, and open a normal PR closing #55, #56, #57, and #58.

## Acceptance Criteria

- `Sloppy.createBuilder()` and `builder.build()` exist.
- `builder.config`, `builder.logging`, and `builder.services` exist.
- Config `addObject`, `get`, `has`, and `require` behavior is implemented and covered.
- Logging memory sink and minimum-level filtering are implemented and covered.
- Services singleton/transient resolution is implemented and covered.
- App freeze prevents route mutation and is covered.
- Existing `Sloppy.create`, `app.mapGet`, and `Results` behavior remains supported.
- Docs distinguish implemented skeleton behavior from planned future features.

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

- Use a structural freeze only: `builder.build()` freezes builder mutation, and
  `app.freeze()` freezes route/endpoint mutation. It does not run validation, emit a plan,
  or start runtime execution.
- Use later `builder.config.addObject(...)` keys overriding earlier object providers.
- Use deterministic memory logging with no timestamps.
- Use string tokens only for services. Duplicate tokens fail.

## Progress Log

- Branch refreshed from `origin/main` and recreated as `feature/12-app-host-foundation`.
- Source docs and GitHub issues #13, #55, #56, #57, and #58 read before edits.
- Bootstrap stdlib app-host skeleton implemented.
- Hello example updated to use the builder/config/logging/services skeleton.
- Static CMake API-shape checks and optional executable ESM test added.
- Public docs, app-host module docs, testing docs, quality score, and tech debt tracker
  updated.
- Validation commands completed successfully:
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

## Risks

- Static bootstrap checks can miss behavior that executable ESM tests would catch.
- The current V8 smoke path does not load ESM bootstrap modules, so full V8 stdlib
  execution remains deferred.

## Completion Notes

Completed for PR review. V8-backed ESM stdlib tests remain deferred because the current V8
bridge smoke path does not load ESM modules.

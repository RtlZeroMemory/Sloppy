# Execution Plan: ENGINE-21 Memory/String Primitives

## Goal

Implement the ENGINE-21 primitive foundation for memory and string handling without
starting ENGINE-22 subsystem adoption.

## Source Docs

- AGENTS.md
- CONTRIBUTING.md
- docs/memory.md
- docs/project/memory-string-current-state-audit.md
- docs/project/memory-string-foundation-architecture.md
- docs/project/memory-string-adoption-map.md
- docs/project/slop-engine-layered-roadmap.md
- docs/project/engine-13-plus-architecture.md
- docs/quality-gates.md
- docs/testing-strategy.md
- docs/c-standards.md
- docs/c-style.md
- GitHub issues #362, #364, #365, #366, #368, #369

## Non-goals

- No ENGINE-22 adoption.
- No HTTP rewrite.
- No V8 bridge adoption.
- No SQLite adoption.
- No compiler feature work.
- No async runtime work.
- No Node/npm compatibility or package manager behavior.
- No benchmark or public-alpha claims.

## Scope

- Lifetime/allocation documentation and arena dispose behavior.
- String and byte helpers for arena-owned copies, equality, suffix, and hashing.
- Fixed and arena-backed bounded byte/string builders with small formatting helpers.
- Bounded app/static-lifetime intern table and symbols for stable metadata only.
- Unit and bounded stress-style coverage for success, failure, overflow, capacity, and
  unchanged-output behavior.
- Source docs updated to distinguish implemented primitives from #367 and ENGINE-22
  follow-ups.

## Steps

1. Refresh from `origin/main` and create the requested feature branch.
2. Read governing docs and GitHub issues.
3. Map existing core primitive APIs, tests, and CMake wiring.
4. Add primitive APIs and implementations.
5. Add focused C unit tests.
6. Update docs and this execution plan.
7. Run required gates.
8. Review diff, commit, push, and open a normal PR.

## Acceptance Criteria

- Public headers document ownership and failed-call output behavior.
- Views remain pointer-plus-length with no implicit NUL assumptions.
- All growth and copy sizing uses checked arithmetic or bounded capacity checks.
- Builders remain valid after failed append/reserve and expose only the preserved prefix.
- Interning is table-owned, bounded, collision-safe by byte equality, and app/static
  metadata-only by policy.
- Tests cover invalid input, empty views, binary spans, non-NUL strings, overflow/capacity,
  failure-output preservation, and bounded stress cases.
- #367 and ENGINE-22 remain explicit follow-ups.

## Validation Commands

- .\tools\windows\bootstrap.ps1
- .\tools\windows\dev.ps1 configure
- .\tools\windows\dev.ps1 build
- .\tools\windows\dev.ps1 test
- .\tools\windows\dev.ps1 format-check
- .\tools\windows\dev.ps1 lint
- .\tools\windows\check-js-ts-standards.ps1
- .\tools\windows\check-rust-standards.ps1
- cargo fmt --manifest-path compiler/Cargo.toml -- --check
- cargo clippy --manifest-path compiler/Cargo.toml -- -D warnings
- cargo test --manifest-path compiler/Cargo.toml
- git diff --check
- git status --short --ignored

## Decision Log

- Use arena-owned copy helpers instead of adding a heap allocator or standalone `SlBuf`
  before a real operation-owned buffer contract exists.
- Implement builder growth over caller-provided arenas with explicit max capacity so there
  is no hidden global allocation path.
- Preserve builder prefixes after failed append/reserve and allow normal views afterward.
- Implement interning as a caller-owned table with arena-owned metadata and bytes, not a
  process-global pool.
- Leave V8/SQLite conversion helper policy to #367 because this PR does not adopt bridge
  or provider paths.

## Progress Log

- Created `feature/engine-21-memory-string-primitives` from `origin/main`.
- Read source docs and issues #362, #364, #365, #366, #368, and #369.
- Added string/byte arena-copy, hash, suffix, builder, arena dispose, and intern table APIs.
- Added focused tests for view helpers, builder growth/failure behavior, and intern table
  collision/capacity/stale-symbol behavior.
- Updated memory, project, quality, and tech-debt docs to mark primitive work implemented
  while keeping #367 and ENGINE-22 deferred.

## Risks

- Builder adoption in diagnostics/HTTP can create golden or response drift; keep it in
  ENGINE-22.
- Interned symbols are useful for metadata identity, but byte equality must remain the
  correctness rule during future adoption.
- Arena-backed builder growth intentionally abandons old buffers until arena reset; callers
  must use scoped lifetimes rather than long-lived unbounded builders.

## Completion Notes

- `.\tools\windows\bootstrap.ps1` passed.
- `.\tools\windows\dev.ps1 configure` passed.
- `.\tools\windows\dev.ps1 build` passed.
- `.\tools\windows\dev.ps1 test` passed. The live PostgreSQL and SQL Server provider
  tests remained skipped by their existing opt-in gates.
- `.\tools\windows\dev.ps1 format-check` passed.
- `.\tools\windows\dev.ps1 lint` passed with the existing non-fatal complexity warning for
  `src/core/diagnostics.c::sl_diag_code_name`.
- `.\tools\windows\check-js-ts-standards.ps1` passed.
- `.\tools\windows\check-rust-standards.ps1` passed.
- `cargo fmt --manifest-path compiler/Cargo.toml -- --check` passed.
- `cargo clippy --manifest-path compiler/Cargo.toml -- -D warnings` passed.
- `cargo test --manifest-path compiler/Cargo.toml` passed.
- `git diff --check` passed.
- `git status --short --ignored` showed only intended source/docs/test changes plus ignored
  `build/` and `compiler/target/`.

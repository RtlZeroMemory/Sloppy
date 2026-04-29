# Execution Plan: EPIC-26 Cross-Platform CI Expansion

## Goal

Add required non-V8 CI gates for Windows, Linux, and macOS while keeping V8 and live
provider validation optional, gated, and clearly reported.

## Source Docs

- `AGENTS.md`
- `CONTRIBUTING.md`
- `docs/project/next-roadmap.md`
- `docs/roadmap.md`
- `docs/architecture.md`
- `docs/platform-abstraction.md`
- `docs/build-and-distribution.md`
- `docs/dependencies.md`
- `docs/testing.md`
- `docs/testing-strategy.md`
- `docs/quality-gates.md`
- `docs/c-standards.md`
- `docs/c-style.md`
- `docs/js-ts-standards.md`
- `docs/rust-standards.md`
- `docs/modules/engine-v8/README.md`
- `docs/modules/data/README.md`
- `docs/quality-score.md`
- `docs/tech-debt-tracker.md`
- `docs/review-playbook.md`
- GitHub issues `#129`, `#147`, `#148`, `#149`, `#150`, and `#151`

## Non-goals

- No runtime feature implementation.
- No mandatory V8 in default CI.
- No mandatory live PostgreSQL or SQL Server services in default CI.
- No package-manager behavior.
- No V8 SDK, provider credentials, generated artifacts, or `.sdeps` committed.

## Scope

- Expand `.github/workflows/ci.yml` to Windows clang-cl, Linux clang/gcc, and macOS clang
  required non-V8 gates.
- Add Linux/macOS CMake presets.
- Add simple POSIX standards scanners for CI parity.
- Add optional/manual V8 workflow behavior that reports skipped/not configured when no SDK
  is supplied.
- Add provider gate reporting for default and live provider tests.
- Update source docs to describe matrix decisions and limitations.

## Steps

1. Refresh from `origin/main` and branch from fresh `origin/main`.
2. Read required docs and GitHub issues.
3. Update CI workflow and CMake presets.
4. Add POSIX scanner scripts.
5. Update docs.
6. Run local static checks and Windows gates.
7. Open a normal PR and inspect GitHub Actions.

## Acceptance Criteria

- Linux clang, Linux gcc, macOS clang, and Windows clang-cl jobs exist.
- Required default CI does not require V8 or live databases.
- Optional V8 behavior is manual/gated and honest about skipped SDK validation.
- Provider live test reporting is explicit.
- Docs explain default versus optional validation.

## Validation Commands

- `python -m json.tool CMakePresets.json`
- PyYAML parse of `.github/workflows/ci.yml`
- `bash tools/unix/check-platform-boundaries.sh`
- `bash tools/unix/check-c-standards.sh`
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

- Use direct CMake/Cargo commands on Linux/macOS instead of introducing a Unix dev wrapper
  framework.
- Keep SQL Server ODBC enabled on Windows only by default; Linux/macOS CI verifies the
  disabled/stub path.
- Keep V8 validation manual and runner-local because there is no hosted SDK/cache contract
  yet.
- Leave Linux/macOS package smoke out of required CI until packaging validation is scoped.

## Progress Log

- Fresh branch created from `origin/main`.
- Required docs and issues read.
- CI matrix, presets, POSIX scanners, and docs updated.
- Local Windows and static checks passed.

## Risks

- Hosted Linux/macOS vcpkg dependency restore may expose portability issues only visible in
  GitHub Actions.
- Optional V8 validation remains dependent on a future SDK hosting/cache story.

## Completion Notes

This plan is completed for the PR slice. Remaining work belongs to follow-ups, not this PR:
V8 SDK cache/prebuilt setup, optional live provider service jobs, Linux/macOS package smoke,
and sanitizer/fuzz expansion.

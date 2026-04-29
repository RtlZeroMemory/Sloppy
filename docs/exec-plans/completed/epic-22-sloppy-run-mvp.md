# Execution Plan: EPIC-22 Sloppy Run MVP

## Goal

Implement the first dev-only executable Sloppy application path from EPIC-21 artifacts:
load artifacts, route GET request paths, call V8 handlers through the existing
runtime-contract path, and return tiny text/JSON-compatible HTTP responses.

## Source Docs

Primary source of truth: `docs/project/next-roadmap.md`. No EPIC-22 GitHub issues were
found before implementation.

Additional source docs read: `AGENTS.md`, `CONTRIBUTING.md`, `docs/roadmap.md`,
`docs/architecture.md`, `docs/compiler.md`, `docs/app-plan.md`,
`docs/execution-model.md`, `docs/developer-ergonomics.md`, `docs/concurrency.md`,
`docs/platform-abstraction.md`, `docs/public/cli.md`,
`docs/public/getting-started.md`, `docs/public/app-model.md`, `docs/public/routing.md`,
`docs/public/results.md`, `docs/modules/http/README.md`,
`docs/modules/plan/README.md`, `docs/modules/engine-v8/README.md`,
`docs/modules/app-host/README.md`, `docs/js-ts-standards.md`, `docs/rust-standards.md`,
`docs/c-standards.md`, `docs/c-style.md`, `docs/testing-strategy.md`, `docs/testing.md`,
`docs/quality-gates.md`, `docs/documentation-policy.md`, `docs/quality-score.md`,
`docs/tech-debt-tracker.md`, and `docs/review-playbook.md`.

## Non-goals

No production server hardening, HTTPS, HTTP/2, request body parsing, streaming, middleware,
endpoint filters, hot reload, package manager behavior, Node compatibility, broad
TypeScript/bundling behavior, production deployment, full response writer, or app-host
request context.

## Scope

- Add `sloppy run --artifacts <dir>` and positional artifact-directory input.
- Defer source input handoff with a clear diagnostic.
- Add `--host`, `--port`, and deterministic `--once METHOD TARGET`.
- Load `app.plan.json`, route metadata, and `app.js` from EPIC-21 artifacts.
- Require V8 and fail clearly in default non-V8 builds.
- Dispatch GET routes through existing native route matching and runtime-contract handler
  calls.
- Add a tiny libuv dev server that writes one response and closes the connection.
- Update tests, docs, and the compiler hello example.

## Acceptance Criteria

- `sloppy run` command exists with help text.
- Artifact loading from EPIC-21 output works in V8-enabled builds.
- Non-V8 builds report that `sloppy run` requires V8.
- `--once GET /` can return the compiler hello body when V8 is available.
- Route miss and unsupported method responses are covered by V8-gated tests.
- Default tests pass without V8.
- Docs clearly mark dev-only limitations and deferred source-input behavior.

## Validation Commands

Required full gate list remains in the PR body. During implementation, the default
configure/build/test gate was run before final full validation.

## Decision Log

- EPIC-22 GitHub issues were not found, so implementation follows `docs/project/next-roadmap.md`.
- Source input handoff is deferred because the native `sloppy` CLI does not yet have a
  clean compiler library/subprocess contract.
- `--once` is implemented to keep CI deterministic and avoid socket timing flakiness.
- The response writer stays local and tiny until EPIC-23 owns response/request context.
- JSON support is compatibility-level only: compiler-generated `Results.json` currently
  returns a JSON string through the string result boundary.

## Progress Log

- Refreshed from `origin/main` after EPIC-21 merged.
- Created `feature/22-sloppy-run-mvp` from fresh `origin/main`.
- Implemented CLI options, artifact loading, V8 gate, route metadata loading, `--once`,
  and the dev server.
- Added default non-V8 process tests and V8-gated run tests.
- Updated user docs, module docs, quality docs, tech debt, and examples.

## Risks

- The real socket server has a narrow manual smoke surface; deterministic CI uses `--once`.
- Full result descriptor conversion is deferred to EPIC-23.
- Source-input `sloppy run <app.js>` is intentionally deferred.

## Completion Notes

This plan documents the implemented-vs-deferred boundary for the EPIC-22 PR.

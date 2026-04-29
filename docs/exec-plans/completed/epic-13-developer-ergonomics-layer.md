# Execution Plan: EPIC-13 Developer Ergonomics Layer

## Goal

Implement the coherent bootstrap developer ergonomics layer for TASK 13.A through
TASK 13.D: route groups, bounded `Results.*` helpers, a validation/schema skeleton, and
honest examples/fixtures.

## Source Docs

- `AGENTS.md`
- `CONTRIBUTING.md`
- `docs/architecture.md`
- `docs/developer-ergonomics.md`
- `docs/execution-model.md`
- `docs/compiler.md`
- `docs/app-plan.md`
- `docs/modularity.md`
- `docs/data-providers.md`
- `docs/public/app-model.md`
- `docs/public/routing.md`
- `docs/public/results.md`
- `docs/public/modules.md`
- `docs/public/services.md`
- `docs/public/diagnostics.md`
- `docs/modules/app-host/README.md`
- `docs/modules/http/README.md`
- `docs/modules/engine-v8/README.md`
- `docs/testing-strategy.md`
- `docs/testing.md`
- `docs/quality-gates.md`
- `docs/documentation-policy.md`
- `docs/review-playbook.md`
- `docs/skills/development-skill.md`
- `docs/roadmap.md`
- GitHub issues `#14`, `#59`, `#60`, `#61`, and `#62`

## Non-goals

- No compiler extraction.
- No `app.plan.json` emission.
- No real HTTP server, response writer, `app.run`, or `app.listen`.
- No middleware, filters, modules, data providers, OpenAPI, package-manager behavior, or
  full validation engine.

## Scope

- Extend the existing JavaScript bootstrap stdlib only.
- Keep descriptors, schemas, and route metadata as plain in-memory objects.
- Add tests/checks that verify documented bootstrap behavior.
- Update public, module, architecture, testing, and debt docs to match implemented limits.

## Steps

1. Refresh from `origin/main` and create `feature/13-developer-ergonomics-layer`.
2. Read source docs and GitHub issue bodies.
3. Implement `app.mapGroup`, grouped GET registration, prefix composition, and group
   metadata inheritance.
4. Expand `Results` helpers while keeping descriptor behavior plain and frozen.
5. Add `schema` with standalone string/number/boolean/object validation and metadata.
6. Add ergonomics example and static example check.
7. Update executable bootstrap tests and static API-shape checks.
8. Update docs and technical debt tracker.
9. Run available quality gates and open a normal PR.

## Acceptance Criteria

- `app.mapGroup` registers grouped GET routes with normalized patterns.
- Group tags/name/prefix appear in route snapshot metadata.
- `Results` includes the bounded helper set with stable descriptors.
- `schema` export exists with minimal validation and inspectable metadata.
- Examples and docs clearly say bootstrap examples are not runnable HTTP apps.
- Existing app-host foundation behavior remains covered.

## Validation Commands

- `node tests\bootstrap\test_app_host_foundation.mjs`
- `cmake -DSLOPPY_BOOTSTRAP_SOURCE_DIR=V:/Slop/stdlib/sloppy -P tests/cmake/check_bootstrap_api.cmake`
- `cmake -DPROJECT_SOURCE_DIR=V:/Slop -P tests/cmake/check_ergonomics_example.cmake`
- Full Windows and cargo gates before PR.

## Decision Log

- Route groups are implemented as small plain objects over the existing route array.
- Child patterns may be relative or start with `/`; prefixes normalize trailing slashes.
- Nested groups are deferred to keep this slice bounded.
- Validation is a schema skeleton with standalone `validate(...)`; request binding and
  automatic responses are deferred.
- `app.mapGet(pattern, metadata, handler)` stores metadata only.
- Results descriptors accept shallow plain-object headers but do not normalize or write
  headers.

## Progress Log

- Branch created from fresh `origin/main`.
- Source docs and GitHub issues read.
- Bootstrap stdlib, tests, examples, and docs updated.
- Targeted Node/CMake checks passed during implementation.

## Risks

- Future compiler extraction may choose a more restrictive static subset.
- Result descriptor shape may evolve when native response conversion lands.
- Schema metadata is intentionally small and should not be mistaken for OpenAPI support.

## Completion Notes

This plan is completed in the same PR that implements EPIC-13 as one coherent bootstrap
developer ergonomics layer.

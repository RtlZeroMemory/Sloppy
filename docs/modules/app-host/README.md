# App Host Module

## Status

Bootstrap stdlib layout exists; app-host behavior is planned / not implemented yet.

## Purpose

Provide the developer-facing app host model: builder, app freeze, config, logging,
services, routes, validation, and modules.

## Scope

App builder, frozen graph, services, config, logging, route metadata, validation shape, and
ergonomic public API.

## Non-goals

No Node compatibility by default and no raw primitive-first public app model.

## Public/Internal API

`stdlib/sloppy/index.js` now re-exports placeholder `Sloppy` and `Results` modules for the
future public `"sloppy"` facade. The exports are empty frozen objects. Builders, app graph
freeze, `Sloppy.create`, `app.mapGet`, route registration, and result helpers remain future
work.

## Ownership/Lifetime Rules

Service lifetimes and request scopes must be explicit and plan-visible.

## Invariants

The app graph freezes before run in static plan mode.

## Diagnostics

Missing service, duplicate route, invalid lifetime, missing config, and module graph errors.

## Tests

CTest registers `bootstrap.stdlib.assets` to verify the source bootstrap files and copied
build-tree assets exist. Public API examples, plan fixtures, diagnostics snapshots, and
integration smoke come later once behavior exists.

## Source Docs

- `docs/developer-ergonomics.md`;
- `docs/modularity.md`;
- `docs/app-plan.md`;
- `docs/testing-strategy.md`;
- `docs/build-and-distribution.md`;
- `stdlib/sloppy/README.md`.

## Open Questions

- Exact package layout for the public TypeScript API.

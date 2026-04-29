# App Host Module

## Status

Bootstrap stdlib layout and tiny in-memory app facade exist; full app-host behavior is
planned / not implemented yet.

## Purpose

Provide the developer-facing app host model: builder, app freeze, config, logging,
services, routes, validation, and modules.

## Scope

App builder, frozen graph, services, config, logging, route metadata, validation shape, and
ergonomic public API.

## Non-goals

No Node compatibility by default and no raw primitive-first public app model.

## Public/Internal API

`stdlib/sloppy/index.js` now re-exports frozen `Sloppy` and `Results` modules for the
future public `"sloppy"` facade. Implemented bootstrap behavior is intentionally tiny:

- `Results.text(...)` and `Results.json(...)` create frozen plain descriptors;
- `Sloppy.create()` creates a frozen JavaScript app facade;
- `app.mapGet(pattern, handler)` stores an in-memory GET route registration;
- `.withName(name)` stores a route name;
- `app.__getRoutes()` returns frozen route snapshots for bootstrap tests/debugging.

Builders, native app graph freeze, startup validation, `app.run`, `app.listen`,
`app.build`, `app.freeze`, compiler extraction, automatic `app.plan.json` emission, HTTP
server behavior, route groups, modules, services, middleware, validation, config, and
logging remain future work.

## Ownership/Lifetime Rules

Service lifetimes and request scopes must be explicit and plan-visible.

## Invariants

The app graph freezes before run in static plan mode.

## Diagnostics

Missing service, duplicate route, invalid lifetime, missing config, and module graph errors.

## Tests

CTest registers `bootstrap.stdlib.assets` to verify the source bootstrap files and copied
build-tree assets exist. CTest also registers `bootstrap.stdlib.api_shape` to statically
check the implemented bootstrap API names, descriptor fields, route registration shape, and
absence of future app-host APIs. Executable ESM/V8 stdlib tests, plan fixtures, diagnostics
snapshots, and full integration smoke remain future work once module loading exists.

## Source Docs

- `docs/developer-ergonomics.md`;
- `docs/modularity.md`;
- `docs/app-plan.md`;
- `docs/testing-strategy.md`;
- `docs/build-and-distribution.md`;
- `stdlib/sloppy/README.md`.

## Open Questions

- Exact package layout for the public TypeScript API.

# 0004: Sloppy Plan

## Status

Accepted.

## Context

Sloppy wants native route dispatch and strong startup validation. A runtime that discovers
the application graph only through arbitrary JavaScript execution gives up too much
precomputation and too many early diagnostics.

## Decision

`sloppyc` will emit a compiled Sloppy Plan as a runtime contract. The plan describes routes,
handlers, middleware, services, permissions, schemas, and artifact links. The route and app
graph freeze before the app runs in static plan mode.

Native route dispatch uses numeric handler IDs.

## Consequences

The compiler and runtime need a versioned contract. Static plan mode can be optimized and
validated strongly. Dynamic mode may exist later, but it must be explicit and less
optimized.

## Alternatives Considered

- Discover everything at runtime: rejected because it weakens validation and native
  dispatch.
- Generate C route code: deferred because it adds build complexity before the plan contract
  is proven.

## Follow-up Tasks

- Add plan v1 fixtures.
- Implement minimal plan loader before HTTP/router work.
- Add validation diagnostics for handler IDs, module ordering, and compatibility.
- Keep dynamic mode explicit and visibly less optimized.

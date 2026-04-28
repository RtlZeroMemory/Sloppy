# 0012: Developer Ergonomics as Product Wedge

## Status

Accepted.

## Context

Competing with Bun purely on raw performance is not the best wedge. Existing JavaScript
runtime examples often expose low-level primitives. Sloppy wants a clean app-host model
inspired by ASP.NET Minimal APIs. The name is silly, but the developer experience should be
sharp.

## Decision

Sloppy optimizes for ergonomic TypeScript backend apps. Minimal API-style route mapping is
the default. Handlers return `Results`. Config, logging, services, and modules are built-in
concepts. Database modules use safe template query ergonomics.

Diagnostics are considered part of the product UX. Performance supports the ergonomics story
but does not replace it.

## Consequences

Public API design matters as much as runtime internals. Sloppy should reject ugly
primitive-first examples as the main user path.

The runtime carries more built-in host concepts than Bun, Node, or Deno, but it also carries
less compatibility baggage.

## Alternatives Considered

- Runtime primitive only: rejected because it does not provide the app-host product wedge.
- Express-like API: rejected because Sloppy should own a typed, plan-aware host model.
- Node compatibility first: rejected because compatibility would dominate architecture.
- Performance-only positioning: rejected because ergonomics is the sharper product story.

## Follow-up Tasks

- Keep README examples app-host first.
- Add diagnostics acceptance criteria to user-facing features.
- Validate API shape with tiny app, grouped routes, and module-based app stories.
- Ensure performance work supports, rather than replaces, ergonomics.

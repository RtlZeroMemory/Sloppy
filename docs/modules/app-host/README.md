# App Host Module

## Status

Planned / not implemented yet.

## Purpose

Provide the developer-facing app host model: builder, app freeze, config, logging,
services, routes, validation, and modules.

## Scope

App builder, frozen graph, services, config, logging, route metadata, validation shape, and
ergonomic public API.

## Non-goals

No Node compatibility by default and no raw primitive-first public app model.

## Public/Internal API

Future TypeScript `Sloppy`, `Results`, builder, app, and module APIs.

## Ownership/Lifetime Rules

Service lifetimes and request scopes must be explicit and plan-visible.

## Invariants

The app graph freezes before run in static plan mode.

## Diagnostics

Missing service, duplicate route, invalid lifetime, missing config, and module graph errors.

## Tests

Public API examples, plan fixtures, diagnostics snapshots, and integration smoke once
features exist.

## Source Docs

- `docs/developer-ergonomics.md`;
- `docs/modularity.md`;
- `docs/app-plan.md`;
- `docs/testing-strategy.md`.

## Open Questions

- Exact package layout for the public TypeScript API.

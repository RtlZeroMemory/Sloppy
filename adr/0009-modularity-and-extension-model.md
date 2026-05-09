# 0009: Modularity and Extension Model

## Status

Accepted.

## Context

Sloppy needs extensibility without becoming plugin chaos. App modules, native providers,
compiler plugins, engine backends, and runtime backends are different concerns with
different safety and stability rules.

The runtime also needs a frozen graph for diagnostics, performance, permission audit, and
native route dispatch.

## Decision

App modules are first-class and declarative. The module graph is phased, dependency-ordered,
compiler-readable, and frozen after `builder.build()`.

The Sloppy Plan is the extension boundary between compiler and runtime. Modules contribute
routes, services, middleware, config sources, permissions/capabilities, schemas, jobs,
health checks, and metadata to that plan.

Service tokens are namespaced strings first. Capabilities are preferred over global power.

Native plugin ABI is future-only. Native plugins must not expose V8 directly; they target a
versioned, Sloppy-owned, engine-independent ABI.

## Consequences

Sloppy requires more upfront design before feature code. In return, the runtime gets better
diagnostics, performance, auditability, and deterministic behavior.

Dynamic behavior must be explicit and less optimized. Import order must not silently decide
module behavior.

## Alternatives Considered

- Node-style side-effect imports: rejected because import order would become hidden app
  graph behavior.
- Direct V8 native addons: rejected because native extensions would couple to one engine.
- Single global plugin API for everything: rejected because compiler, runtime, provider,
  and engine extension points have different contracts.
- Hardcoded first-party features only: rejected because Sloppy needs modular applications
  and provider growth.

## Follow-up Tasks

- Add module plan fixtures before module extraction.
- Implement deterministic module ordering and cycle diagnostics.
- Keep native plugin ABI future-only until static first-party providers validate the boundary.
- Keep compiler plugins restricted and metadata-focused when introduced.

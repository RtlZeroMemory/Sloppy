# 0001: Project Identity

## Status

Accepted.

## Context

The project name is Sloppy and the repository is Slop. The branding deliberately jokes about
AI slop, but the implementation goal is the opposite: small scope, serious engineering, and
no intentionally throwaway foundation layers.

Sloppy is a TypeScript application runtime, not a compatibility clone of an existing
JavaScript runtime.

## Decision

Sloppy will use the public identity "AI-slop branding, zero-slop architecture." The runtime
will be an app-host runtime with custom Sloppy APIs inspired by ASP.NET Core Minimal API
semantics.

Sloppy will not define itself as a clone of an existing JavaScript runtime or web framework.
Compatibility layers may be explored later only as explicit layers.

## Consequences

The product can be playful without letting the engineering become casual. API design,
diagnostics, permissions, routing, and lifecycle behavior belong to Sloppy rather than being
inherited by default from Node-compatible platforms.

## Alternatives Considered

- Build a Node-compatible runtime: rejected because compatibility would dominate the
  architecture.
- Build a thin framework over an existing runtime: rejected because the project goal is a
  native app host.

## Follow-up Tasks

- Keep README and product docs aligned with "AI-slop branding, zero-slop architecture."
- Reject feature proposals that turn Sloppy into a general JavaScript runtime compatibility
  clone by default.
- Ensure public API examples emphasize app-host ergonomics.

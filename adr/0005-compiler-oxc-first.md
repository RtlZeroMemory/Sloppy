# 0005: Compiler Oxc First

## Status

Accepted.

## Context

Sloppy needs TypeScript parsing, transformation, source locations, and application graph
extraction. The compiler must eventually emit JavaScript, source maps, and a Sloppy Plan.

## Decision

Oxc is the primary parser, transform, and app-plan extraction substrate. Official TypeScript
checking through `tsgo` or `tsc` will be integrated later. esbuild is not the core compiler
foundation. SWC may be considered later as a fallback backend.

## Consequences

The compiler can focus on deep extraction and diagnostics rather than only bundling speed.
TypeScript type compatibility remains delegated to official checker integration when that
phase arrives.

## Alternatives Considered

- esbuild as the core: rejected because Sloppy needs deeper graph extraction and diagnostic
  control.
- SWC first: deferred as a possible fallback.
- Handwritten parser: rejected as unnecessary risk.

## Follow-up Tasks

- Keep compiler dependencies empty until the Oxc extraction story begins.
- Add fake emitter and golden tests before adding Oxc.
- Add official TypeScript checker integration as a separate phase.
- Document any SWC fallback decision in a future ADR.

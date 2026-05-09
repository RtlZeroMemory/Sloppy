# Modularity And Extension Model

## Purpose

Sloppy extensibility starts with compiler-readable application modules, not native plugins.
Modules should be declarative, deterministic, auditable, and Plan-visible.

## Current Status

The bootstrap JavaScript stdlib implements an early module shape. Module definitions can
declare dependencies and contribute capabilities, services, and routes to debug metadata.
The builder validates missing dependencies and cycles, sorts modules deterministically, and
attributes module-created contributions.

This is not final module compiler extraction, native runtime module loading, package
distribution, dynamic plugin behavior, or Framework v2.

## Extension Layers

| Layer | Purpose | Status |
| --- | --- | --- |
| App modules | User-facing application composition | early bootstrap shape |
| First-party feature modules | Official framework/data/files/etc. packages | future public API |
| Built-in compiler extractors | Plan metadata extraction | internal |
| Compiler plugins | Future metadata extensions | deferred |
| Native providers | Native capability/provider code | internal or future ABI |
| Engine backends | V8 and future engines | internal |
| Runtime backends | event loop/platform/protocol implementations | internal |

Native plugins are not the starting point. They should wait until app modules,
provider contracts, feature activation, and package boundaries are settled.

## Module Graph Rules

- Module names must be stable identifiers.
- Dependencies must be explicit.
- Missing dependencies fail during build/freeze.
- Cycles fail deterministically.
- Contribution ordering must be deterministic.
- Capability/service/route contributions must be attributable to the module that produced
  them.
- Dynamic behavior must not bypass Plan-visible metadata.

## Compiler And Plan Direction

Future compiler work should extract module metadata into Plan artifacts. Native runtime
loading should consume validated Plan metadata rather than re-running arbitrary module
registration logic at request time.

The public API should remain small until Framework v2 establishes the final app/module
shape.

## Deferred Work

Deferred modularity work includes compiler extraction from the public module API, final Plan
emission for module contributions, native runtime module loading, package distribution,
dynamic behavior policy, native/compiler plugin boundaries, and broader module diagnostics.

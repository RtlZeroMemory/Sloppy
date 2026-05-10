# Framework Explicit Binding Example

A compile-time example for explicit handler-parameter binding.

The handler uses explicit `Route`, `Query`, `Header`, `Body`, and request-context
binding wrappers. This produces deterministic binding metadata for the current
supported wrapper subset.

## Scope

The example is limited to the explicit binding wrappers shown in `app.ts`.
Decorators, service scanning, and broad coercion behavior are not covered here.

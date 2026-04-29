# Services

Status: Bootstrap string-token services skeleton implemented.

Purpose: document the current minimal service registry/provider API and the future path to
request-scoped lifetimes, disposal, modules, and plan-visible service graphs.

Implemented API example:

```ts
const builder = Sloppy.createBuilder();

builder.services.addSingleton("message", () => "Hello");
builder.services.addTransient("clock", () => ({ now: () => 123 }));

const app = builder.build();
const services = app.services.createScope();

services.get("message");
services.get("clock");
```

Implemented behavior:

- Service tokens must be non-empty strings.
- `addSingleton(token, factoryOrValue)` registers a singleton.
- Singleton factories are called lazily on first resolution and then cached.
- Singleton non-function values are returned as supplied.
- `addTransient(token, factory)` registers a transient factory.
- Transient factories are called on every `get`.
- Duplicate service tokens fail during registration.
- Missing service tokens fail during resolution with a helpful error.
- `builder.build()` freezes further service registration.
- `app.services.get(token)` resolves through a short-lived scope.
- `app.services.createScope()` returns a scope with `scope.get(token)`.

The current scope object is a tiny resolution context only. It is not a real request
lifetime and it does not own disposal.

Not implemented yet: request-scoped lifetimes, disposal hooks, async factories, dependency
graph validation, cycle diagnostics, typed tokens, decorators, module-owned services,
capabilities, native service graph validation, and plan emission.

Related internal docs: `docs/developer-ergonomics.md`, `docs/modularity.md`,
`docs/diagnostics.md`.

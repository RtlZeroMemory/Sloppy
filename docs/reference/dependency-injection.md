# Dependency Injection Reference

Dependency injection is implemented by `stdlib/sloppy/internal/services.js` and the framework typed-binding compiler path.

## Registration APIs

Builder registration methods:

- `addSingleton(token, factoryOrValue)`
- `addScoped(token, factory)`
- `addTransient(token, factory)`

Validation:

- `token` must be a non-empty string
- scoped/transient factories must be functions
- duplicate tokens are rejected

## Resolution APIs

From built app:

- `app.services.get(token)` resolves singleton services only
- `app.services.createScope()` creates a scope that can resolve singleton/scoped/transient

Scope APIs:

- `scope.get(token)`
- `scope.dispose()` (idempotent)

Provider API:

- `app.services.dispose()` disposes singleton-owned resources

## Lifetime Rules

- singleton instances are created once and cached
- scoped instances are cached per scope
- transient instances are created on every resolve

Disposal behavior:

- scoped/transient owned values are disposed on scope dispose (reverse order)
- singleton-owned values are disposed on provider dispose
- disposal supports `Symbol.dispose`, `dispose()`, or `close()`

## Enforced Error Cases

- resolving after scope/provider disposal
- requesting unknown token
- circular dependency chain
- singleton depending on scoped service
- root resolution of non-singleton service

## Compiler Typed Injection Surface

Current typed wrappers recognized in handler signatures:

- `Service<T>` -> service injection
- `Config<"...">` -> configuration/env binding
- `Sqlite<"...">`, `Postgres<"...">`, `SqlServer<"...">` -> provider injection
- `WorkQueue<"...">` -> queue injection

Generated framework runtime uses a scoped provider per request and disposes that scope after handler completion.

## Limits

- No auto-registration by reflection/decorators.
- Service token naming and mapping is explicit and string-based.

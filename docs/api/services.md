# Services

Sloppy ships a small dependency injection container. Register services on the
builder, resolve them from a handler's request scope or from another service.

```ts
const builder = Sloppy.createBuilder();

builder.services.addSingleton("clock", () => ({ now: () => Date.now() }));
builder.services.addScoped("repo", (s) => new UsersRepo(s.get("clock")));
builder.services.addTransient("uuid", () => crypto.randomUUID());

const app = builder.build();
```

## Lifetimes

| Lifetime    | Constructed       | Disposed                                     |
| ----------- | ----------------- | -------------------------------------------- |
| Singleton   | once at first use | when the app shuts down                      |
| Scoped      | once per scope    | when the scope ends (e.g. end of a request)  |
| Transient   | every `get(...)`  | when the surrounding scope ends              |

```ts
builder.services.addSingleton(token, factoryOrValue);
builder.services.addScoped(token, factory);
builder.services.addTransient(token, factory);
```

For singletons you can pass a value directly:

```ts
builder.services.addSingleton("config:greeting", "hello");
```

For scoped and transient services, the second argument must be a factory.

## Resolving

A handler's `ctx.services` is a per-request scope. Inside a handler:

```ts
app.get("/", (ctx) => {
    const repo = ctx.services.get("repo");
    return Results.json(repo.list());
});
```

A factory receives the *same* scope:

```ts
builder.services.addScoped("repo", (scope) => {
    const clock = scope.get("clock");
    return new UsersRepo(clock);
});
```

`scope.get(token)` resolves any registered service. `scope.tryGet(token)`
returns `undefined` when the token isn't registered.

`app.services` is the **root scope**. It only resolves singletons —
asking for a scoped or transient service throws with a message telling
you to create a scope. Make a child scope for ad-hoc work:

```ts
const scope = app.services.createScope();
const repo = scope.get("repo");
// ... use repo ...
await scope.dispose();
```

## Disposal

When a scope ends, every service constructed inside it is disposed,
latest-constructed first. Sloppy looks for, in order:

1. `service[Symbol.dispose]()`
2. `service.dispose()`
3. `service.close()`

The first method that exists wins; the others are not called. If the
returned value is a Promise, the runtime awaits it.

You don't have to call disposal yourself — the runtime ends the request
scope after the handler completes (or throws).

## Resolution rules

- **Circular dependencies fail at resolve time.** If `A` depends on `B`
  which depends on `A`, `scope.get("A")` throws with the dependency
  chain in the diagnostic.
- **Singletons can't depend on scoped services.** Singletons outlive
  any scope; capturing a scoped service in one would break that
  contract. The resolver detects the violation and throws.

Other lifetime combinations (singleton → transient, scoped → transient,
transient → anything) are allowed today. The factory just needs to be
prepared for the lifetime it's wiring up.

## Token style

Tokens are arbitrary strings. Two conventions used in Sloppy itself:

- Dotted, lowercase namespaces: `"users.repo"`, `"data.main"`, `"clock"`
- Reserved prefixes for runtime-managed services: `"data.<name>"` is used
  for SQLite providers wired up via `app.use(...)` or capability declarations.

You can use anything you like, but stick to one style per app.

## Inspecting registered services

`app.__debug()` returns a frozen description of registered modules and
worker resources, useful in tests:

```ts
const info = app.__debug();
console.log(info.modules);
```

There is no public API to list every service token at runtime — that's
intentional, since it would freeze the resolution surface.

# Modules

Status: Planned / not implemented yet.

Purpose: document future Sloppy app modules, dependency ordering, phases, and module
diagnostics.

Planned API example:

```ts
const UsersModule = Sloppy.module("users")
  .dependsOn("data")
  .routes(app => {
    app.mapGet("/users/{id:int}", getUser);
  });
```

This example is aspirational and not currently implemented.

Related internal docs: `docs/modularity.md`, `docs/app-plan.md`.

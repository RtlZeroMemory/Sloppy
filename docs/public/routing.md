# Routing

Status: Planned / not implemented yet.

Bootstrap status: the stdlib layout exists, but no public route API exists yet.
`app.mapGet`, route groups, handler registration, and compiler extraction remain future
work.

Purpose: document future `app.mapGet`, `app.mapPost`, route groups, route parameters,
metadata, and route diagnostics.

Planned API example:

```ts
const users = app.mapGroup("/users").withTags("Users");

users.mapGet("/{id:int}", getUser).withName("Users.Get");
users.mapPost("/", createUser).withName("Users.Create");
```

This example is aspirational and not currently implemented.

Related internal docs: `docs/developer-ergonomics.md`, `docs/app-plan.md`,
`docs/execution-model.md`.

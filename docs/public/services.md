# Services

Status: Planned / not implemented yet.

Purpose: document future service registration, lifetimes, service tokens, and missing
service diagnostics.

Planned API example:

```ts
builder.services.addScoped("users.repo", scope => {
  return new UsersRepository(scope.get("data.main"));
});
```

This example is aspirational and not currently implemented.

Related internal docs: `docs/developer-ergonomics.md`, `docs/modularity.md`,
`docs/diagnostics.md`.

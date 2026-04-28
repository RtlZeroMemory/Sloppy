# Data

Status: Planned / not implemented yet.

Purpose: document future data provider modules, query templates, transactions, and
provider-specific limitations.

Planned API example:

```ts
const user = await db.queryOne`
  select id, name
  from users
  where id = ${route.id}
`;
```

This example is aspirational and not currently implemented.

Related internal docs: `docs/data-providers.md`, `docs/concurrency.md`.

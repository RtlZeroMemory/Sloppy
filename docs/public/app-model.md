# App Model

Status: Planned / not implemented yet.

Purpose: explain the future builder/app model, graph freeze, startup validation, and how
Sloppy differs from raw runtime callbacks.

Planned API example:

```ts
const builder = Sloppy.createBuilder();
const app = builder.build();

app.mapGet("/", () => Results.text("ok"));

await app.run();
```

This example is aspirational and not currently implemented.

Related internal docs: `docs/developer-ergonomics.md`, `docs/modularity.md`,
`docs/app-plan.md`.

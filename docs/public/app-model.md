# App Model

Status: Tiny bootstrap facade implemented.

Bootstrap status: `stdlib/sloppy/app.js` exports a frozen `Sloppy` object with
`Sloppy.create()`. The returned app is an in-memory conceptual object with
`app.mapGet(...)` and `app.__getRoutes()` for bootstrap tests/debugging.

Purpose: explain the future builder/app model, graph freeze, startup validation, and how
Sloppy differs from raw runtime callbacks.

Implemented bootstrap API example:

```ts
const app = Sloppy.create();

app.mapGet("/", () => Results.text("ok"));
```

`Sloppy.create()` does not build or freeze a native app graph yet. It returns a frozen
JavaScript app facade backed by an internal route array.

Current route registration shape:

```js
{
  method: "GET",
  pattern: "/hello",
  handler,
  name: null,
  metadata: {}
}
```

`app.__getRoutes()` returns frozen snapshots of the registered routes. It exists only for
bootstrap tests/debugging until compiler extraction and runtime host integration exist.

Not implemented yet: `Sloppy.createBuilder`, `app.run`, `app.listen`, `app.build`,
`app.freeze`, app host graph freeze, native startup validation, compiler extraction,
automatic `app.plan.json` emission, HTTP serving, modules, services, middleware,
validation, config, and logging.

Related internal docs: `docs/developer-ergonomics.md`, `docs/modularity.md`,
`docs/app-plan.md`.

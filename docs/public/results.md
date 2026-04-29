# Results

Status: Planned / not implemented yet.

Bootstrap status: `stdlib/sloppy/results.js` now exports a placeholder frozen `Results`
object for layout purposes only. `Results.text`, `Results.json`, and the other result
helpers are not implemented yet.

Purpose: document future `Results.*` helpers and how handler return values become native
response descriptors.

Planned API example:

```ts
app.mapGet("/health", () => Results.ok({ status: "ok" }));
app.mapGet("/hello", () => Results.text("hello"));
```

This example is aspirational and not currently implemented.

Related internal docs: `docs/developer-ergonomics.md`, `docs/execution-model.md`.

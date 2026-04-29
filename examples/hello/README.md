# Hello Example

Status: Bootstrap app-host skeleton example.

This example demonstrates Sloppy's first public app shape using the current source
bootstrap stdlib:

```js
import { Sloppy, Results } from "../../stdlib/sloppy/index.js";

const builder = Sloppy.createBuilder();

builder.config.addObject({
    "app.name": "hello",
});

builder.logging.addMemorySink();
builder.services.addSingleton("message", () => "Hello from Sloppy");

const app = builder.build();

app.mapGet("/", ({ services }) => Results.text(services.get("message")))
    .withName("Hello.Index");

export default app;
```

What works today:

- `Sloppy.createBuilder()` creates a bootstrap builder with config, logging, and services.
- `builder.config.addObject(...)` stores object-backed config values.
- `builder.logging.addMemorySink()` installs a deterministic in-memory log sink.
- `builder.services.addSingleton(...)` registers a string-token singleton service.
- `builder.build()` creates an in-memory app facade and freezes builder mutation.
- `app.mapGet("/", handler)` records a conceptual GET route.
- `.withName("Hello.Index")` stores the route name for the bootstrap shape.
- Route handlers can receive a minimal context with `services`, `config`, and `log`.
- `Results.text("Hello from Sloppy")` creates a frozen text result descriptor.
- CTest statically verifies this example imports the current stdlib path and uses the
  expected public API shape.

What does not work yet:

- `sloppy run` does not exist yet.
- `sloppyc` does not compile this example or extract routes from it.
- This example does not emit `app.plan.json`.
- There is no real HTTP server or response writer for this app.
- There is no `app.run` or `app.listen` yet.
- The bare import `import { Sloppy, Results } from "sloppy";` is future compiler/runtime
  module-resolution behavior, not something this repository supports today.

Future intended workflow:

```powershell
sloppy run examples/hello/app.ts
```

That command is planned only. Until compiler extraction, ESM bootstrap loading, app-plan
emission, and HTTP serving land, this directory is a checked-in API-shape and documentation
example rather than a runnable app host.

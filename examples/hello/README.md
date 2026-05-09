# Hello Example

Bootstrap app-host example.
This example shows the minimal app-host builder flow: config, logging, services, route
mapping, and a text result descriptor. Runtime lane notes follow after the code.

```js
import { Sloppy, Results } from "sloppy";

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

What to inspect:

- `Sloppy.createBuilder()` creates a bootstrap builder with config, logging, and services.
- `builder.config.addObject(...)` stores object-backed config values.
- `builder.logging.addMemorySink()` installs a deterministic in-memory log sink.
- `builder.services.addSingleton(...)` registers a string-token singleton service.
- `builder.build()` creates an in-memory app facade and freezes builder mutation.
- `app.mapGet("/", handler)` records a conceptual GET route.
- `.withName("Hello.Index")` stores the route name for the bootstrap shape.
- Route handlers can receive a minimal context with `services`, `config`, and `log`.
- `Results.text("Hello from Sloppy")` creates a frozen text result descriptor.
- CTest statically verifies this example imports the public Sloppy facade and uses the
  expected public API shape.

Current limitations:

- `sloppyc` does not compile this example or extract routes from it yet.
- `app.plan.json` is not emitted for this example.
- This source-stdlib example is not on the `sloppy run --artifacts` lane.
- `app.run` and `app.listen` are not available yet.
- Direct `../../stdlib` imports are reserved for internal bootstrap tests; public examples
  use the Sloppy facade import shape that `sloppyc` recognizes.

Runtime lane:

```powershell
sloppy run examples/hello/app.js
```

That command is not the current execution lane for this example. Until compiler extraction,
ESM bootstrap loading, app-plan emission for this broader source shape, and V8 bootstrap
loading land, this directory remains an API-shape/documentation example. Use
`examples/compiler-hello/` for the current `sloppy run --artifacts` path.

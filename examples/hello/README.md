# Hello Example

Bootstrap app-host example.
This example shows the minimal app-host builder flow: config, logging, services, route
mapping, and a text result descriptor. Runtime notes follow after the code.

```js
import { Sloppy, Results } from "sloppy";

const builder = Sloppy.createBuilder();

builder.config.addObject({
    "app.name": "hello",
});

builder.logging.addMemorySink();
builder.services.addSingleton("message", () => "Hello from Sloppy");

const app = builder.build();

app.log.info("hello example configured", { example: "hello" });

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
- `app.log.info(...)` writes a structured application event into the configured sink.
- `app.mapGet("/", handler)` records a conceptual GET route.
- `.withName("Hello.Index")` stores the route name for the bootstrap shape.
- Route handlers can receive a minimal context with `services`, `config`, and `log`.
- `Results.text("Hello from Sloppy")` creates a frozen text result descriptor.
- CTest statically verifies this example imports the public Sloppy facade and uses the
  expected public API shape.

Current product state:

- This source-stdlib example is a checked-in API-shape fixture.
- `sloppy run --artifacts` currently runs emitted artifacts such as
  `examples/compiler-hello`.
- `sloppyc` route extraction and `app.plan.json` emission for this broader builder shape
  are future source-extraction work.
- `app.run` and `app.listen` belong to later app-host runtime work.
- Direct `../../stdlib` imports are reserved for internal bootstrap tests; public examples
  use the Sloppy facade import shape that `sloppyc` recognizes.

Runtime Command:

```powershell
sloppy run examples/hello/app.js
```

That command is future user-facing shape for this example. The current emitted-artifact
runtime path is `examples/compiler-hello/`.

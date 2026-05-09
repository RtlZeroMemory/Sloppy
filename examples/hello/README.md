# Hello Example

Bootstrap app-host example.
This is a static API-shape example (not runnable in the current runtime lane).
This example demonstrates Sloppy's first public app shape using the current source
bootstrap stdlib:

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
- CTest statically verifies this example imports the public Sloppy facade and uses the
  expected public API shape.

What does not work yet:

- `sloppyc` does not compile this example or extract routes from it.
- This example does not emit `app.plan.json`.
- This source-stdlib example is not a `sloppy run --artifacts` app.
- There is no `app.run` or `app.listen` yet.
- Direct `../../stdlib` imports are reserved for internal bootstrap tests; public examples
  use the Sloppy facade import shape that `sloppyc` recognizes.

Current runtime boundary:

```powershell
sloppy run examples/hello/app.js
```

That command is not the current execution lane for this example. Until compiler extraction, ESM bootstrap loading, app-plan
emission for this broader source shape, and V8 bootstrap loading land, this directory is a
checked-in API-shape and documentation example rather than a runnable app host. Use
`examples/compiler-hello/` for the current dev-only `sloppy run --artifacts` path.

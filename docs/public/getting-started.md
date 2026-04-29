# Getting Started

Status: Bootstrap app-host, developer ergonomics, and module skeleton implemented; runtime
execution planned.

Purpose: introduce the future Sloppy developer workflow from app source to build artifacts
to runtime execution.

Current bootstrap API shape:

```ts
import { Sloppy, Results, schema } from "sloppy";

const builder = Sloppy.createBuilder();

builder.services.addSingleton("message", () => "Sloppy is alive");
builder.addModule(Sloppy.module("hello")
  .services(services => {
    services.addSingleton("hello.module", () => "Hello from a module");
  }));

const app = builder.build();

const Query = schema.object({
  q: schema.string().min(1),
});

app.mapGroup("/search")
  .withTags("Search")
  .mapGet("/", { query: Query }, ({ services }) => Results.ok({
    message: services.get("message"),
  }));
```

`Sloppy.createBuilder()`, `builder.build()`, `Sloppy.create()`, `app.mapGet(...)`,
`app.mapGroup(...)`, `app.freeze()`, `Sloppy.module(...)`, `builder.addModule(...)`, the
current `Results.*` helper set, object-backed config, memory logging, string-token
singleton/transient services, and the `schema` skeleton now exist in the bootstrap stdlib.
`app.run()` and `app.listen()` do not. The example remains aspirational as a runnable
application until compiler extraction, `app.plan.json` emission, native app-host
validation, and HTTP server behavior land.

The first checked-in example lives at `examples/hello/`. It uses the current source stdlib
path because compiler/runtime support for the bare `"sloppy"` import is not implemented:

```js
import { Sloppy, Results } from "../../stdlib/sloppy/index.js";
```

`examples/hello/app.js`, `examples/ergonomics/app.js`, and `examples/modules-basic/app.js`
are bootstrap API-shape examples and are statically checked by CTest. They are not compiled
by `sloppyc`, do not emit `app.plan.json`, and do not run through a real HTTP server.

Related internal docs: `docs/architecture.md`, `docs/execution-model.md`,
`docs/developer-ergonomics.md`.

# Getting Started

Status: Bootstrap app-host skeleton implemented; runtime execution planned.

Purpose: introduce the future Sloppy developer workflow from app source to build artifacts
to runtime execution.

Current bootstrap API shape:

```ts
import { Sloppy, Results } from "sloppy";

const builder = Sloppy.createBuilder();

builder.services.addSingleton("message", () => "Sloppy is alive");

const app = builder.build();

app.mapGet("/", ({ services }) => Results.text(services.get("message")));
```

`Sloppy.createBuilder()`, `builder.build()`, `Sloppy.create()`, `app.mapGet(...)`,
`app.freeze()`, `Results.text(...)`, object-backed config, memory logging, and
string-token singleton/transient services now exist in the bootstrap stdlib. `app.run()`
and `app.listen()` do not. The example remains aspirational as a runnable application until
compiler extraction, `app.plan.json` emission, native app-host validation, and HTTP server
behavior land.

The first checked-in example lives at `examples/hello/`. It uses the current source stdlib
path because compiler/runtime support for the bare `"sloppy"` import is not implemented:

```js
import { Sloppy, Results } from "../../stdlib/sloppy/index.js";
```

`examples/hello/app.js` is a bootstrap API-shape example and is statically checked by
CTest. It is not compiled by `sloppyc`, does not emit `app.plan.json`, and does not run
through a real HTTP server.

Related internal docs: `docs/architecture.md`, `docs/execution-model.md`,
`docs/developer-ergonomics.md`.

# Getting Started

Status: Partially implemented bootstrap API; hello example exists; runtime execution
planned.

Purpose: introduce the future Sloppy developer workflow from app source to build artifacts
to runtime execution.

Current bootstrap API shape:

```ts
import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.mapGet("/", () => Results.text("Sloppy is alive"));
```

`Sloppy.create()`, `app.mapGet(...)`, and `Results.text(...)` now exist in the bootstrap
stdlib, but `app.run()` does not. The example remains aspirational as a runnable
application until compiler extraction, `app.plan.json` emission, app host run/build/freeze,
and HTTP server behavior land.

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

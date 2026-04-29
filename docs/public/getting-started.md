# Getting Started

Status: Partially implemented bootstrap API; runtime execution planned.

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

Related internal docs: `docs/architecture.md`, `docs/execution-model.md`,
`docs/developer-ergonomics.md`.

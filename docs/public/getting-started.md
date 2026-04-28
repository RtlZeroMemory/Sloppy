# Getting Started

Status: Planned / not implemented yet.

Purpose: introduce the future Sloppy developer workflow from app source to build artifacts
to runtime execution.

Planned API example:

```ts
import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.mapGet("/", () => Results.text("Sloppy is alive"));

await app.run();
```

This example is aspirational and not currently implemented.

Related internal docs: `docs/architecture.md`, `docs/execution-model.md`,
`docs/developer-ergonomics.md`.

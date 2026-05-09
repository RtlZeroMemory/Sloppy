# Project layout

A typical Sloppy project looks like this:

```
my-api/
  sloppy.json
  appsettings.json
  appsettings.Development.json
  src/
    main.ts
    routes/
      users.ts
      orders.ts
  .sloppy/                  ← generated artifacts (don't edit)
```

Two files belong to the project itself: `sloppy.json` and `appsettings.json`.
Everything under `src/` is your code.

## `sloppy.json`

This is the project descriptor. Required, lives at the project root, used by
`sloppy build` and `sloppy run` when invoked without arguments.

```json
{
  "entry":       "src/main.ts",
  "outDir":      ".sloppy",
  "environment": "Development"
}
```

| Field         | Purpose                                              |
| ------------- | ---------------------------------------------------- |
| `entry`       | Application entry source file (relative to project)  |
| `outDir`      | Build output directory (relative; default `.sloppy`) |
| `environment` | Environment name; selects which `appsettings.{Environment}.json` overlay applies |

`sloppy.json` is build/run configuration only. Application-level
configuration lives in `appsettings.json`. See
[reference/sloppy-json.md](../reference/sloppy-json.md) for the full schema.

## `appsettings.json` and overlays

`appsettings.json` is the application config file the runtime overlays into
`config`. The overlay rules are:

1. `appsettings.json` is loaded first.
2. `appsettings.{Environment}.json` is overlaid on top (e.g.
   `appsettings.Development.json`).
3. Environment variables matching the configured prefix are layered on top
   of that.
4. Code-level `builder.config.addObject(...)` calls land last.

```json5
// appsettings.json
{
  "app": {
    "greeting": "hello"
  },
  "logging": {
    "minimumLevel": "info",
    "queueCapacity": 64,
    "console": {
      "enabled": true,
      "format": "pretty"
    },
    "file": {
      "path": "app.jsonl",
      "format": "jsonl"
    }
  }
}
```

```json5
// appsettings.Development.json — overlay
{
  "logging": {
    "minimumLevel": "debug"
  }
}
```

Configuration keys are case-insensitive and use `:` as the separator
internally — both `"app:greeting"` and the nested `{ "app": { "greeting": ... } }`
form work.

`logging.file.path` is used by `sloppy run` for the native JSONL file sink. The
current runtime opens it in append mode and expects the parent directory to
already exist.

For typed access in code, see [API: config](../api/config.md).

## `src/`

Source layout is up to you. The compiler imports relative paths from
`entry`, so any structure is fine as long as you stick to supported syntax
(see [TypeScript support](typescript.md)).

A common pattern is to keep `main.ts` thin and put feature-specific code
under `src/`:

```ts
// src/main.ts
import { Sloppy } from "sloppy";
import { usersModule } from "./users";
import { ordersModule } from "./orders";

const app = Sloppy.create();
app.useModule(usersModule);
app.useModule(ordersModule);

export default app;
```

```ts
// src/users.ts
import { Sloppy, Results } from "sloppy";

export const usersModule = Sloppy.module("users")
    .routes((app) => {
        app.get("/users/{id:int}", (ctx) => Results.ok({ id: ctx.route.id }));
    });
```

For a larger current example, see `examples/prealpha-control-plane/`:

```text
prealpha-control-plane/
  sloppy.json
  appsettings.json
  appsettings.Development.json
  src/
    main.js
    routes/
      projects.js
      apps.js
      builds.js
      deployments.js
      diagnostics.js
      health.js
    services/
    db/
    validation/
```

That example keeps the compiler entry thin, registers feature modules with
`app.useModule(...)`, and uses `sloppy/providers/sqlite` for provider metadata.
The current compiler can extract that function-module shape. More general
helper calls inside module files, request logging imports at the source root,
and `app.mapHealthChecks()` are still covered by app-host tests rather than by
compiled project source.

## `.sloppy/`

Generated artifacts. Sloppy writes:

```
.sloppy/
  app.plan.json
  app.js
  app.js.map
  cache/             ← build cache
```

Add `.sloppy/` to `.gitignore`. It's reproducible from source.

## What about `node_modules`?

Sloppy apps don't import npm packages. The compiler resolves `"sloppy"` and
relative paths only. If you need a third-party utility, vendor it into
your repo or split the work into a service the Sloppy app calls. See
[about/why-no-node-modules.md](../about/why-no-node-modules.md) for the
reasoning.

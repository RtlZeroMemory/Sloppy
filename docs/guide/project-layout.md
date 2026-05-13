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
import { usersModule } from "./users.ts";
import { ordersModule } from "./orders.ts";

const app = Sloppy.create();
app.useModule(usersModule);
app.useModule(ordersModule);

export default app;
```

```ts
// src/users.ts
import { Results } from "sloppy";

export function usersModule(app) {
    app.get("/users/{id:int}", (ctx) => Results.ok({ id: ctx.route.id }));
}
```

Imports are file-local. `main.ts` does not import `Results` unless it contains
handlers that call `Results.*`; route modules import `Results` for their own
handlers.

For a larger source layout, use the default `api` template or inspect
`examples/modules-api/`:

```text
modules-api/
  sloppy.json
  appsettings.json
  appsettings.Development.json
  src/
    main.js
    routes/
      users.js
```

That shape keeps the compiler entry thin and registers feature modules with
`app.useModule(...)`. Use the provider-specific examples when you want to see
SQLite, PostgreSQL, or SQL Server configuration.

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

Sloppy can consume installed pure-JavaScript packages from `node_modules` when
the compiler can resolve, transform, bundle, and execute them inside Sloppy's
runtime boundary. It does not install packages, solve versions, load native
addons, or provide full Node compatibility.

Package files are build input. Packaged apps run from the sealed Sloppy
artifact graph and do not read the original `node_modules` directory for
bundled modules at run time. See [Using installed packages](using-packages.md)
and [Dependency graph](../reference/dependency-graph.md).

Beyond `"sloppy"` itself, the stdlib subpaths cover filesystem, network,
operating system, time, crypto, codec, and worker work:

- [`sloppy/fs`](../api/filesystem.md)
- [`sloppy/net`](../api/network.md) (and [`HttpClient`](../api/http-client.md))
- [`sloppy/os`](../api/os.md)
- [`sloppy/time`](../api/time.md)
- [`sloppy/crypto`](../api/crypto.md)
- [`sloppy/codec`](../api/codec.md)
- [`sloppy/workers`](../api/workers.md)

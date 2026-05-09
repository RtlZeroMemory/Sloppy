# Quickstart

A two-route Sloppy API in under five minutes. You'll create a project, build
artifacts, and run a request through the runtime.

Prerequisites:

- `sloppy` is on your `PATH` ([Install](install.md)).
- Your build executes handlers — `sloppy --version` succeeds and your CLI was
  built/published with V8 enabled. If not, you can still finish the
  `sloppy build` step.

## 1. Create a project

```sh
mkdir hello-api && cd hello-api
mkdir src
```

You're aiming for this layout:

```
hello-api/
  sloppy.json
  src/
    main.ts
```

## 2. Add `sloppy.json`

```json
{
  "entry": "src/main.ts",
  "outDir": ".sloppy",
  "environment": "Development"
}
```

Three fields:

- `entry` — the project entrypoint, relative to `sloppy.json`.
- `outDir` — where compiled artifacts go.
- `environment` — picked up for environment-tagged config; `"Development"` is fine.

## 3. Write the app

`src/main.ts`:

```ts
import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.get("/health", () => Results.text("ok"));

app.get("/hello/{name}", (ctx) =>
    Results.json({ hello: ctx.route.name })
);

export default app;
```

That's the whole app:

- `Sloppy.create()` returns a frozen app builder.
- `app.get("/path", handler)` registers a GET route. `post`, `put`, `patch`,
  and `delete` work the same way.
- `{name}` is a route parameter; `ctx.route.name` reads it back as a string.
- `Results.text(...)` and `Results.json(...)` describe the response.

## 4. Build

```
sloppy build
```

This compiles `src/main.ts` and writes:

```
.sloppy/
  app.plan.json   # the application Plan
  app.js          # the generated bundle
  app.js.map      # source map
```

The Plan is what the runtime validates before doing anything else. Open it
if you're curious — it's deterministic JSON.

## 5. Run a request

`--once` runs a single synthetic request and exits, which is the easiest way
to smoke-test handlers without leaving an HTTP server running:

```
sloppy run --artifacts .sloppy --once GET /hello/Ada
```

Expected response body:

```json
{"hello":"Ada"}
```

You can also point `run` directly at source — it compiles first, then runs
the artifacts:

```
sloppy run src/main.ts --once GET /hello/Ada
```

## 6. Run a server

Drop `--once` to start a real HTTP server bound to `127.0.0.1:5173`:

```
sloppy run --artifacts .sloppy
```

Hit it from another shell:

```
curl http://127.0.0.1:5173/hello/Ada
curl http://127.0.0.1:5173/health
```

`Ctrl+C` shuts it down. Override the bind address with `--host` and `--port`:

```
sloppy run --artifacts .sloppy --host 0.0.0.0 --port 8080
```

## What just happened

- The compiler read your source, extracted route metadata (`GET /health`,
  `GET /hello/{name}`), and emitted artifacts.
- The runtime loaded `app.plan.json`, validated it, built a route table, and
  called your compiled handlers.
- Route parameters were materialized on `ctx.route` before your handler ran.

## Where to go next

- [API: app](api/app.md) — `Sloppy.create()`, builder, modules, providers
- [API: routing](api/routing.md) — patterns, groups, controllers
- [API: results](api/results.md) — every `Results.*` helper
- [Guide: project layout](guide/project-layout.md) — `sloppy.json`, env config, `appsettings.json`
- [CLI: build](cli/build.md) and [CLI: run](cli/run.md) — every flag

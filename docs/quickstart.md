# Quickstart

A Sloppy API in under five minutes. Sloppy is public alpha,
pre-production software: this flow is useful for experiments, demos, and
feedback, but not a production deployment recipe. You'll create a project,
build artifacts, and run a request through the runtime.

Prerequisites:

- `sloppy` is on your `PATH` ([Install](install.md)).
- Your build executes handlers — `sloppy --version` succeeds and your CLI was
  built/published with V8 enabled. If not, you can still finish the
  `sloppy build` step.

## Fast path: API starter

The default `api` template is the recommended backend starter. It includes
routes, SQLite migration metadata, validation, static files, and package flow.

```sh
sloppy create my-api
cd my-api
sloppy build
sloppy db migrate .sloppy --provider main
sloppy routes .sloppy
sloppy run .sloppy --once GET /health
sloppy run .sloppy --once GET /users
sloppy package
sloppy db migrate .sloppy/package --provider main
sloppy run .sloppy/package --once GET /health
```

Use the `minimal-api` walkthrough below when you want the smallest source file
instead of the recommended starter.

## 1. Create a project

```sh
sloppy create hello-api --template minimal-api
cd hello-api
```

That creates this layout:

```text
hello-api/
  .gitignore
  sloppy.json
  appsettings.json
  src/
    main.ts
```

## 2. Check `sloppy.json`

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

## 3. Check the app

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

- `Sloppy.create()` returns a built app. You can still register routes
  until `app.freeze()` is called.
- `app.get("/path", handler)` registers a GET route. `post`, `put`,
  `patch`, and `delete` work the same way.
- `{name}` is a route parameter; `ctx.route.name` reads it back as a string.
- `Results.text(...)` and `Results.json(...)` describe the response.
- Imports are file-local. This single file imports `Results` because its
  handlers call `Results.*`; a thin multi-file entry that only registers route
  modules would import `Sloppy` alone.

## 4. Build

```sh
sloppy build
```

This compiles `src/main.ts` and writes:

```text
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

```sh
sloppy run .sloppy --once GET /hello/Ada
```

Expected response body:

```json
{"hello":"Ada"}
```

You can also point `run` directly at source — it compiles first, then runs
the artifacts:

```sh
sloppy run src/main.ts --once GET /hello/Ada
```

For a normal edit-refresh loop, use experimental `sloppy dev` from the project
directory. Its behavior may change during the public alpha, pre-production
period.

```sh
# Experimental.
sloppy dev
```

It builds once, starts the local server, watches source/config/static/template
inputs, and restarts the server after successful rebuilds. If a rebuild fails,
the previous server keeps running while the diagnostic is printed.

## 6. Package the app

```sh
sloppy package
sloppy run .sloppy/package --once GET /health
```

This validates the generated artifacts and writes:

```text
.sloppy/package/
  manifest.json
  artifacts/
    app.plan.json
    app.js
    app.js.map
```

## 7. Run a server

Drop `--once` to start a real HTTP server bound to `127.0.0.1:5173`:

```sh
sloppy run .sloppy
```

Hit it from another shell:

```sh
curl http://127.0.0.1:5173/hello/Ada
curl http://127.0.0.1:5173/health
```

`Ctrl+C` shuts it down. Override the bind address with `--host` and `--port`:

```sh
sloppy run .sloppy --host 0.0.0.0 --port 8080
```

## Program Mode quick check

Program Mode is the route-free shape for console tools and local programs:

```sh
sloppy create hello-tool --template program
cd hello-tool
sloppy run src/main.ts -- --name Ada
sloppy build
sloppy run .sloppy -- --name Ada
sloppy package
sloppy run .sloppy/package -- --name Ada
```

See [Program Mode](guide/program-mode.md) for args, context, exit codes, and
package layout.

## Package dependency quick check

The `package-api` starter shows compatible installed package bundling. Sloppy
uses dependencies that are already present in `node_modules`; it does not
install registry packages during `sloppy build` or discover `node_modules` at
runtime.

```sh
sloppy create package-demo --template package-api
cd package-demo
npm install --ignore-scripts --no-audit
sloppy build
sloppy deps .sloppy
sloppy deps .sloppy --explain
sloppy run .sloppy --once GET /health
sloppy package
sloppy run .sloppy/package --once GET /health
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
- [Guide: Program Mode](guide/program-mode.md) — console tools and packaged programs
- [CLI: create](cli/create.md), [build](cli/build.md), [dev](cli/dev.md), [run](cli/run.md), and [package](cli/package.md) — every flag

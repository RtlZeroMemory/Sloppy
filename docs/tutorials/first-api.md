# Tutorial: Build Your First Sloppy API

This tutorial starts from a clean folder outside the repository and builds the
same app shape used by `examples/hello-minimal`.

You will build two routes:

- `GET /health` returns `ok`.
- `GET /hello/Ada` returns `{"hello":"Ada"}`.

## Prerequisites

- `sloppy` is installed or extracted locally and available on `PATH`.
- The installed runtime can execute handlers. On Windows source builds, that
  means a V8-enabled build.

```powershell
sloppy --version
```

If you are contributing from a checkout instead of using an installed runtime,
build first with [Building from source](../contributor/building-from-source.md)
and put the built `sloppy` on `PATH`.

If `sloppy run` says it requires a V8-enabled build, your CLI can build
artifacts but cannot execute handlers yet. Use a V8-enabled package or build the
Windows V8 preset from the contributor guide.

## Create The Project

Create a new project folder:

```powershell
$project = Join-Path $HOME "sloppy-tutorial\hello-api"
New-Item -ItemType Directory -Force -Path (Join-Path $project "src") | Out-Null
Set-Location $project
```

You now have this layout:

```text
hello-api/
  sloppy.json
  src/
    main.ts
```

Create `sloppy.json`:

```powershell
@'
{
  "entry": "src/main.ts",
  "outDir": ".sloppy",
  "environment": "Development"
}
'@ | Set-Content -Encoding ASCII -Path "sloppy.json"
```

The file contents are:

```json
{
  "entry": "src/main.ts",
  "outDir": ".sloppy",
  "environment": "Development"
}
```

Create `src/main.ts`:

```powershell
@'
import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.get("/health", () => Results.text("ok")).withName("Health.Get");
app.get("/hello/{name}", (ctx) => Results.json({ hello: ctx.route.name }))
    .withName("Hello.Get");

export default app;
'@ | Set-Content -Encoding ASCII -Path "src\main.ts"
```

The source file contents are:

```ts
import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.get("/health", () => Results.text("ok")).withName("Health.Get");
app.get("/hello/{name}", (ctx) => Results.json({ hello: ctx.route.name }))
    .withName("Hello.Get");

export default app;
```

## Build Artifacts

From the project directory, run:

```powershell
sloppy build
```

Expected output files:

```text
.sloppy/
  app.plan.json
  app.js
  app.js.map
```

## Run One Request

From the project directory, run:

```powershell
sloppy run --artifacts .sloppy --once GET /hello/Ada
```

Expected response body:

```json
{"hello":"Ada"}
```

## What Happened

`build` compiled source input into Plan-backed artifacts inside `.sloppy`.
`run --artifacts` loaded those artifacts, matched `/hello/{name}`, and executed
the handler.

# Tutorial: Build Your First Sloppy API

This tutorial starts from a clean folder and builds the same app shape used by
`examples/hello-minimal`.

You will build two routes:

- `GET /health` returns `ok`.
- `GET /hello/Ada` returns `{"hello":"Ada"}`.

## Prerequisites

Build a V8-enabled Sloppy binary from the repository root:

```powershell
.\tools\windows\resolve-v8-sdk.ps1
.\tools\windows\dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8
.\tools\windows\dev.ps1 build -Preset windows-relwithdebinfo
```

If V8 SDK resolution fails, treat that as an environment blocker and fix it
before continuing.

Set a shell variable for the built runtime. The rest of the tutorial uses this
absolute path so the project can live outside the repository checkout.

```powershell
$repo = (Get-Location).Path
$sloppy = Join-Path $repo "build\windows-relwithdebinfo\sloppy.exe"
```

## Create The Project

Create a new project folder:

```powershell
$project = Join-Path $repo ".sloppy-tutorial\hello-api"
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
& $sloppy build
```

Equivalent CLI shape: `sloppy build`.

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
& $sloppy run --artifacts .sloppy --once GET /hello/Ada
```

Equivalent CLI shape: `sloppy run --artifacts .sloppy --once GET /hello/Ada`.

Expected response body:

```json
{"hello":"Ada"}
```

## What Happened

`build` compiled source input into Plan-backed artifacts inside `.sloppy`.
`run --artifacts` loaded those artifacts, matched `/hello/{name}`, and executed
the handler.

The runtime executes the emitted artifacts.

## Evidence Boundaries

Default evidence (non-V8 build):

- `build` and artifact emission are covered.
- Handler execution is expected to fail with `requires V8-enabled build`.

V8-gated evidence:

- The successful hello response (`{"hello":"Ada"}`) is execution evidence.

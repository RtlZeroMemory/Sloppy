# Hello Minimal Example

This example demonstrates the smallest useful Sloppy app: a health route and a
parameterized JSON route using the current framework subset.

## Requirements

- A handler-capable Sloppy build or package.
- PowerShell examples below assume Windows paths.

## Run

From this directory:

```powershell
..\..\build\windows-relwithdebinfo\sloppy.exe build
..\..\build\windows-relwithdebinfo\sloppy.exe run --once GET /health
..\..\build\windows-relwithdebinfo\sloppy.exe run --once GET /hello/Ada
```

Run from the repository root:

```powershell
.\build\windows-relwithdebinfo\sloppy.exe run examples/hello-minimal/src/main.ts --once GET /hello/Ada
```

## Expected Result

- `GET /health` returns `ok`.
- `GET /hello/Ada` returns `{"hello":"Ada"}`.

## What To Inspect

- `sloppy.json`: the source-input configuration.
- `src/main.ts`: route binding and explicit `Results.text`/`Results.json`
  helpers.
- `.sloppy/app.plan.json`: generated route and handler metadata after
  `sloppy build`.

After building artifacts, inspect the generated app with:

```powershell
..\..\build\windows-relwithdebinfo\sloppy.exe routes --plan .sloppy\app.plan.json
..\..\build\windows-relwithdebinfo\sloppy.exe doctor --plan .sloppy\app.plan.json
..\..\build\windows-relwithdebinfo\sloppy.exe openapi --plan .sloppy\app.plan.json
```

## Current Limits

This example requires handler execution support and covers only the two routes
shown above.

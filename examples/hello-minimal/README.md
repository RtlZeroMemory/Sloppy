# Hello Minimal Example

This is an executable source-input example for the currently supported framework subset.
Run from this directory with a V8-enabled build:

```powershell
..\..\build\windows-relwithdebinfo\sloppy.exe build
..\..\build\windows-relwithdebinfo\sloppy.exe run --once GET /health
..\..\build\windows-relwithdebinfo\sloppy.exe run --once GET /hello/Ada
```

Run from the repository root:

```powershell
.\build\windows-relwithdebinfo\sloppy.exe run examples/hello-minimal/src/main.ts --once GET /hello/Ada
```

The example proves `sloppy run`, `sloppy.json`, a TypeScript-extension source file in the
supported JavaScript subset, route binding, and explicit `Results.text`/`Results.json`
helpers.

Routes:

- `GET /health`
- `GET /hello/{name}`

Expected tooling after building artifacts:

```powershell
..\..\build\windows-relwithdebinfo\sloppy.exe routes --plan .sloppy\app.plan.json
..\..\build\windows-relwithdebinfo\sloppy.exe doctor --plan .sloppy\app.plan.json
..\..\build\windows-relwithdebinfo\sloppy.exe openapi --plan .sloppy\app.plan.json
```

This example requires V8 for execution. It does not prove runtime request validation,
Node/npm/package-manager behavior, public release readiness, production HTTP edge behavior,
or performance.

# Validation Errors Example

Status: Plan/OpenAPI metadata example plus native Plan-backed validation fixture for
schema-backed body bindings.

Build artifacts from this directory:

```powershell
..\..\compiler\target\debug\sloppyc.exe build app.js --out .sloppy
```

Then inspect the Plan-driven tooling:

```powershell
..\..\build\windows-dev\sloppy.exe routes --plan .sloppy\app.plan.json
..\..\build\windows-dev\sloppy.exe doctor --plan .sloppy\app.plan.json
..\..\build\windows-dev\sloppy.exe openapi --plan .sloppy\app.plan.json
```

Route:

- `POST /users`

The example proves schema declaration extraction, `ctx.body.json(UserCreate)` request body
metadata, validation problem OpenAPI components, explicit partial/known metadata reporting,
and the request shape consumed by the native Plan-backed validation foundation.
`invalid-user.http` is a documentation fixture for the intended invalid request shape.

The native runtime now has a bounded Plan-backed validation path for route/query/header
scalars and schema-backed JSON request bodies. This directory still does not prove typed
handler execution, provider/DI integration, custom validators, native JSON fast paths,
public alpha readiness, or production HTTP behavior.

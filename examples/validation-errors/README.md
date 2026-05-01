# Validation Errors Example

Status: Plan/OpenAPI metadata example for schema-backed body bindings.

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
metadata, validation problem OpenAPI components, and explicit partial/known metadata
reporting. `invalid-user.http` is a documentation fixture for the intended invalid
request shape.

Runtime schema validation is not implemented in this slice; do not treat this directory as
a production request-validation runtime. It does not prove native JSON fast paths,
semantic validation responses, public alpha readiness, or production HTTP behavior.

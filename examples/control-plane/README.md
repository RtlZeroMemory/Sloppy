# Control plane

This is a multi-file example for Sloppy app-host and source-input coverage. It
models a small deployment control plane with projects, apps, builds,
deployments, diagnostics, health routes, and a SQLite provider.

The example is intentionally plain JavaScript source with no `package.json`.
No npm package scope is required. The compiler resolves `"sloppy"` and relative
imports from the project root.

## What it covers

- `sloppy build` and `sloppy run` project-mode execution through `sloppy.json`.
- Function modules registered through `app.useModule(...)`.
- Route groups, named routes, path params, query params, JSON request bodies,
  `Results.created`, `Results.badRequest`, and `Results.notFound`.
- SQLite provider metadata and the `data.main` capability in emitted plans.
- App test host dispatch against the same route modules in
  `tests/bootstrap/test_control_plane_dogfood.mjs`.

## Current coverage

Default non-V8 lanes compile the project and verify that `sloppy run` emits
artifacts before reporting the required handler-execution diagnostic. The handler-execution lane runs:

```powershell
sloppy run --once GET /projects?owner=runtime
```

from this directory and expects the seeded `Compiler Platform` project in the
response.

The app test host lane also verifies CORS preflight, request IDs, request
logging redaction, ProblemDetails redaction, request service-scope disposal,
health routes, method mismatch, not-found behavior, and host lifecycle cleanup.

## Current limits

This example stays inside the syntax that `sloppyc` can extract today. Source
root imports therefore use `Sloppy`, `ProblemDetails`, and
`sloppy/providers/sqlite`; route modules import `Results` in the files that
call `Results.*`. Request-id middleware, request logging middleware, and
`app.mapHealthChecks()` are covered by the app test host rather than by the
compiled source project.

The source route modules keep SQL near the handlers because current function
module extraction does not support arbitrary helper calls inside module files.
The `src/services`, `src/db`, and `src/validation` folders document the intended
bounded-context layout without being imported by the compiled app yet.

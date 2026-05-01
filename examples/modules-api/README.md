# Modules API Example

Status: compile-time source-input example for multi-file function modules.

Run from this directory:

```powershell
..\..\build\windows-dev\sloppy.exe run
```

The command compiles through `sloppy.json` and emits `.sloppy\app.plan.json`. Non-V8 builds
stop at the expected V8-required diagnostic after artifact emission; V8 builds can execute
the emitted route handlers.

Routes:

- `GET /health`
- `GET /users`
- `GET /users/{id}`

The example proves relative module imports, multiple function modules from one module file,
route groups, module-attributed Plan routes, route binding, explicit `Results` helpers, and
routes/doctor/openapi metadata over a multi-file source graph.

It does not prove controller/decorator routing, a DI container, dynamic imports,
package-manager resolution, or runtime module loading.

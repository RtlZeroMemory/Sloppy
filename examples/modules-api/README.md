# Modules API Example

This is a compile-time source-input example for multi-file function modules.
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

The example demonstrates relative module imports, multiple function modules from one module file,
route groups, module-attributed Plan routes, route binding, explicit `Results` helpers, and
routes/doctor/openapi metadata over a multi-file source graph.

This is the function-module source-input subset. It is intentionally distinct from the
bootstrap `Sloppy.module(...)` capability/service module shape used by other examples.

## Limitations

Controller/decorator routing, DI container behavior, dynamic imports, package
resolution, and runtime module loading are outside this example.

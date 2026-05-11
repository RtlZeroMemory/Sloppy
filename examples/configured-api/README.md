# Configured API Example

This is a compile-time source-input/config example for the current Plan metadata subset.
Run from this directory:

```powershell
..\..\build\windows-dev\sloppy.exe run
```

On a non-V8 build this still compiles and emits `.sloppy\app.plan.json`, then fails before
serving with the normal V8-required diagnostic. With V8 enabled, it enters the same
artifact runtime path.

The example demonstrates `sloppy.json`, `appsettings.json`, `appsettings.Development.json`, and
Plan-visible `app.config.getString(...)` reads. The `Development` overlay is present so
tooling can inspect the selected environment's config requirements. The `/config` handler
returns static supported-subset JSON because closed-over config values in route
results are outside this example's current compiler metadata path.

Route:

- `GET /config`

Expected tooling after building artifacts:

```powershell
..\..\build\windows-dev\sloppy.exe routes --plan .sloppy\app.plan.json
..\..\build\windows-dev\sloppy.exe doctor --plan .sloppy\app.plan.json
..\..\build\windows-dev\sloppy.exe capabilities --plan .sloppy\app.plan.json
..\..\build\windows-dev\sloppy.exe audit --plan .sloppy\app.plan.json --format json
..\..\build\windows-dev\sloppy.exe openapi --plan .sloppy\app.plan.json
```

## Limitations

This example focuses on the current `--environment` flow and static config reads.
Arbitrary environment providers and broader secret-management flows are outside
this slice.

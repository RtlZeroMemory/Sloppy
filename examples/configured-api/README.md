# Configured API Example

Status: compile-time source-input/config example for the current Plan metadata subset.

Run from this directory:

```powershell
..\..\build\windows-dev\sloppy.exe run
```

On a non-V8 build this still compiles and emits `.sloppy\app.plan.json`, then fails before
serving with the normal V8-required diagnostic. With V8 enabled, it enters the same
artifact runtime path.

The example proves `sloppy.json`, `appsettings.json`, `appsettings.Development.json`, and
Plan-visible `app.config.getString(...)` reads. The `Development` overlay is present so
tooling can inspect the selected environment's config requirements. The `/config` handler
returns static supported-subset JSON because closed-over config values in route results are
not implemented yet.

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

This example does not prove config values flowing into runtime handler responses,
arbitrary environment providers, secret handling, CLI override behavior beyond the
implemented `--environment` option, public alpha readiness, or production configuration
management.

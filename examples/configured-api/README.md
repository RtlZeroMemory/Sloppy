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
Plan-visible `app.config.getString(...)` reads. The `Development` overlay changes
`App:Greeting`.

Route:

- `GET /config`

Expected tooling after building artifacts:

```powershell
sloppy doctor --plan .sloppy\app.plan.json
sloppy audit --plan .sloppy\app.plan.json --format json
```

This example does not prove arbitrary environment providers, secret handling, CLI
override behavior beyond the implemented `--environment` option, public alpha readiness,
or production configuration management.

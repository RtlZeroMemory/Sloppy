# How To Configure An App

Set runtime and provider configuration for a Sloppy app build.

## Prerequisites

- A Sloppy project with `sloppy.json`.
- A source app that reads configuration and/or provider settings.

## Steps

1. Keep project execution config in `sloppy.json`.

```json
{
  "entry": "src/main.ts",
  "outDir": ".sloppy",
  "environment": "Development"
}
```

2. Add app configuration files in project root.

```text
appsettings.json
appsettings.Development.json
appsettings.local.json
appsettings.Development.local.json
.sloppy/secrets.json
.sloppy/secrets.Development.json
```

3. Example SQLite provider config in `appsettings.json`.

```json
{
  "Sloppy": {
    "Providers": {
      "sqlite": {
        "main": {
          "database": "users-api-sqlite-runtime.db"
        }
      }
    }
  }
}
```

4. Optional environment override.

```powershell
$env:Sloppy__Server__Port = "8080"
$env:Sloppy__Providers__sqlite__main__database = "data\\local.db"
```

5. Build with environment selection.

```powershell
sloppy build --environment Development
```

## Expected Result

- Build succeeds and writes artifacts.
- Environment and file config merge into Plan metadata for runtime/config keys.

## Common Failures

- `invalid sloppy.json: unsupported field; supported fields are entry, outDir, and environment`.
- `invalid Sloppy environment variable name ...`: use `Sloppy__...` or `SLOPPY_SLOPPY__...` form.
- `environment override for <key> expects an integer/number/true or false`: env value type does not match existing config type.
- `SLOPPYC_E_CONFIG_MALFORMED`: malformed JSON in appsettings or secrets file.

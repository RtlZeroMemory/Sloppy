# sloppy.json Reference

`sloppy.json` is project-level source-input config for `sloppy run` / `sloppy build`.

It is separate from app/runtime config (`appsettings*.json` and related overrides).
It is optional for direct source commands such as `sloppy run src/main.ts`,
`sloppy build src/main.ts`, and `sloppy package src/main.ts`.

## File Location

- filename: `sloppy.json`
- read from current working directory

## Schema

Supported fields:

- `entry` (required string)
- `outDir` (optional string, default `.sloppy`)
- `environment` (optional string, default `Development`)
- `kind` (optional string, default `web`)
- `capabilities` (optional object)
- `moduleInclude` (optional string array)
- `assetInclude` (optional string array)
- `ffiLibraries` (optional object)

Unknown fields are rejected.

| Field | Type | Required | Default | Description |
| --- | --- | --- | --- | --- |
| `entry` | string | yes | none | Source file to compile from the project root. |
| `outDir` | string | no | `.sloppy` | Output directory for generated artifacts. |
| `environment` | string | no | `Development` | Environment name used during source-input build. |
| `kind` | `"web"` or `"program"` | no | `web` | Source kind for project mode. Existing projects without `kind` stay web projects. |
| `capabilities` | object | no | none | Boolean stdlib capability declarations preserved in the Plan. |
| `moduleInclude` | string array | no | none | Project-relative glob patterns for modules that must be sealed into the artifact graph for computed dynamic imports. |
| `assetInclude` | string array | no | none | Project-relative glob patterns for non-executable assets to record and package. |
| `ffiLibraries` | object | no | none | Local native library paths used by `sloppy package` for Plan FFI library IDs. |

## Source Kind

`kind: "web"` compiles the supported Sloppy web app shape. The entry must export
one Sloppy app as default and register at least one route.

`kind: "program"` compiles a route-free program entrypoint. The compiler emits a
program Plan, skips web route requirements, and records opaque metadata because
Program Mode does not claim a static web app graph. At run time, `main(args,
ctx)` receives arguments passed after `--` plus a Program context with
`kind`, `cwd`, `environment`, and Plan metadata.

Direct source input without `sloppy.json` infers kind. Project mode defaults to
`web` when `kind` is omitted, preserving existing web projects.

## Capabilities

Program projects may declare stdlib capability intent:

```json
{
  "kind": "program",
  "entry": "src/main.ts",
  "outDir": ".sloppy",
  "capabilities": {
    "fs": true,
    "net": true,
    "os": true,
    "time": true,
    "crypto": true,
    "codec": true,
    "workers": true,
    "ffi": true
  }
}
```

Supported names are `fs`, `net`, `os`, `time`, `crypto`, `codec`, and
`workers`, and `ffi`. Values must be booleans. `true` values become Plan capability
metadata and required runtime features; `false` values are ignored.

## Module And Asset Includes

Use `moduleInclude` when source uses computed dynamic imports and the compiler
cannot see the possible target modules from string literals:

```json
{
  "kind": "program",
  "entry": "src/main.ts",
  "moduleInclude": [
    "src/plugins/**/*.js",
    "src/jobs/**/*.ts"
  ]
}
```

Matching modules are transformed, recorded in the dependency graph, emitted
into `app.js`, and made available to runtime dynamic import resolution. This is
still a sealed graph: runtime imports can only resolve to modules already
included in the artifact.

Use `assetInclude` for files that should be recorded and packaged but not
executed as modules:

```json
{
  "assetInclude": [
    "src/views/**/*.html",
    "public/**/*"
  ]
}
```

Glob support is intentionally small and deterministic: `*`, `?`, and `**` are
supported after path normalization to forward slashes. Patterns must be
project-relative. Absolute paths, `..`, empty path segments, and drive-prefixed
paths are rejected. `node_modules` is not searched by include patterns unless
the pattern starts with `node_modules/`.

## FFI Libraries

`ffiLibraries` maps a Plan-visible `ffi.library(...)` ID to a trusted local
native library path. A value can be a single relative path or a per-platform
object. Per-platform keys currently include `windows-x64`, `linux-x64`,
`macos-x64`, and `macos-arm64`.

```json
{
  "entry": "src/main.ts",
  "ffiLibraries": {
    "myhash": {
      "windows-x64": "native/windows-x64/myhash.dll",
      "linux-x64": "native/linux-x64/libmyhash.so",
      "macos-arm64": "native/macos-arm64/libmyhash.dylib"
    }
  }
}
```

These paths are project-local package inputs, not remote downloads. `sloppy
package` copies mapped libraries into `artifacts/native/`, records their
package path and SHA-256 hash, and package runs use that manifest metadata to
resolve the Plan library ID. Unmapped FFI libraries use normal system loader
resolution.

## Size and Encoding Limits

- maximum file bytes: `8192`
- malformed JSON is rejected
- root must be a JSON object

## Path Constraints

`entry` must be a relative path inside project root:

- absolute paths are rejected
- `..` segments are rejected
- empty path segments are rejected

`moduleInclude` and `assetInclude` patterns follow the same project-relative
safety rule and reject absolute paths, `..`, and empty path segments.
`ffiLibraries` paths follow the same project-relative path rules.

## Error Messages

Representative failures:

- `missing entry in sloppy.json`
- `invalid sloppy.json: malformed JSON`
- `invalid sloppy.json: root must be an object`
- `invalid sloppy.json: entry must be a non-empty string`
- `invalid sloppy.json: kind must be web or program`
- `invalid sloppy.json: capability values must be true or false`
- `invalid sloppy.json: moduleInclude must be an array of non-empty relative paths`
- `invalid sloppy.json: assetInclude must be an array of non-empty relative paths`
- `invalid sloppy.json: ffiLibraries paths must be relative project paths`
- `invalid sloppy.json: entry must be a relative path inside the project root`

## Command Integration

### `sloppy run`

When no explicit source file, artifact directory, or `--artifacts` flag is
provided, `sloppy run` loads `sloppy.json`, compiles source input, then runs
generated artifacts. If there is no `sloppy.json` but `.sloppy/app.plan.json`
exists, `sloppy run` treats `.sloppy` as the local artifact directory.

### `sloppy build`

When no explicit source file is provided, `sloppy build` loads `sloppy.json` and compiles using `entry`/`outDir`.

If `sloppy.json` mode is used, `--out` is rejected for `sloppy build`.

## Environment Override Rules

`--environment` overrides `sloppy.json` environment only in source-input mode.

`sloppy run .sloppy --environment ...` and
`sloppy run --artifacts <dir> --environment ...` are rejected because artifact
mode does not compile source input.

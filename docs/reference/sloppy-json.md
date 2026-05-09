# sloppy.json Reference

`sloppy.json` is project-level source-input config for `sloppy run` / `sloppy build`.

It is separate from app/runtime config (`appsettings*.json` and related overrides).

## File Location

- filename: `sloppy.json`
- read from current working directory

## Schema

Supported fields:

- `entry` (required string)
- `outDir` (optional string, default `.sloppy`)
- `environment` (optional string, default `Development`)

Unknown fields are rejected.

## Size and Encoding Limits

- maximum file bytes: `8192`
- malformed JSON is rejected
- root must be a JSON object

## Path Constraints

`entry` must be a relative path inside project root:

- absolute paths are rejected
- `..` segments are rejected
- empty path segments are rejected

## Error Messages

Representative failures:

- `missing entry in sloppy.json`
- `invalid sloppy.json: malformed JSON`
- `invalid sloppy.json: root must be an object`
- `invalid sloppy.json: entry must be a non-empty string`
- `invalid sloppy.json: entry must be a relative path inside the project root`

## Command Integration

### `sloppy run`

When no explicit source file and no `--artifacts` are provided, `sloppy run` loads `sloppy.json`, compiles source input, then runs generated artifacts.

### `sloppy build`

When no explicit source file is provided, `sloppy build` loads `sloppy.json` and compiles using `entry`/`outDir`.

If `sloppy.json` mode is used, `--out` is rejected for `sloppy build`.

## Environment Override Rules

`--environment` overrides `sloppy.json` environment only in source-input mode.

`sloppy run --artifacts <dir> --environment ...` is rejected because artifact mode does not compile source input.

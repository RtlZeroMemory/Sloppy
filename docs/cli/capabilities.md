# `sloppy capabilities`

List the capabilities a Plan declares. Read-only; doesn't enter V8.

```text
sloppy capabilities <artifacts-dir|plan.json> [--format text|json]
sloppy capabilities --plan <path> [--format text|json]
sloppy capabilities --artifacts <dir> [--format text|json]
```

`<path>` is an `app.plan.json` file or a directory containing one.
`--artifacts <dir>` remains the explicit artifact-directory form.

## Output

**Text** (default):

```text
$ sloppy capabilities .sloppy
SCOPE  TOKEN/ROUTE  KIND      ACCESS     REASON    SOURCE
PLAN   data.main    database  readwrite  declared  data.main
```

**JSON**:

```json
$ sloppy capabilities .sloppy --format json
{
  "capabilities": [
    {
      "token": "data.main",
      "kind": "database",
      "access": "readwrite",
      "inference": "declared",
      "provider": "data.main"
    }
  ]
}
```

## Use cases

- Confirm that compiler-inferred capabilities (e.g. from
  `Sqlite<"main">` typed handlers) match what you intended.
- Confirm that Program Mode stdlib imports or `sloppy.json` declarations such
  as `fs` and `time` are visible in the Plan.
- Confirm that `sloppy/ffi` declarations emitted `ffi/use` capability metadata
  and native FFI function rows.
- Review what providers a build will activate at runtime.
- Generate compliance/security reports.

See [`sloppy audit`](audit.md) for the security-oriented version.

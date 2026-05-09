# `sloppy capabilities`

List the capabilities a Plan declares. Read-only; doesn't enter V8.

```
sloppy capabilities --plan <path> [--format text|json]
sloppy capabilities --artifacts <dir> [--format text|json]
```

`<path>` is an `app.plan.json` file or a directory containing one.
`--artifacts <dir>` is equivalent to `--plan <dir>/app.plan.json`.

## Output

**Text** (default):

```
$ sloppy capabilities --plan .sloppy/app.plan.json
data.main      database  provider=sqlite     access=readwrite
queue.emails   queue     -                   -
```

**JSON**:

```
$ sloppy capabilities --plan .sloppy/app.plan.json --format json
[
  {
    "token": "data.main",
    "kind": "database",
    "provider": "sqlite",
    "access": "readwrite",
    "module": null
  }
]
```

## Use cases

- Confirm that compiler-inferred capabilities (e.g. from
  `Sqlite<"main">` typed handlers) match what you intended.
- Review what providers a build will activate at runtime.
- Generate compliance/security reports.

See [`sloppy audit`](audit.md) for the security-oriented version.

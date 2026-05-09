# `sloppy create`

Copy a built-in project template into a new local app directory.

```
sloppy create <name> [--template minimal-api|full-api|dogfood]
                     [--force] [--no-git] [--format text|json]
```

## Templates

| Template | Purpose |
| --- | --- |
| `minimal-api` | Small project with `/health` and `/hello/{name}`. |
| `full-api` | Multi-file API with route modules, config, logging settings, and grouped routes. |
| `dogfood` | Larger multi-file control-plane shaped demo project. |

The generated app has no `package.json` and no npm dependencies. The compiler
resolves `"sloppy"` plus relative imports; user app dependency resolution
through `node_modules` is not part of the alpha track.

## Examples

```
sloppy create my-api
sloppy create my-api --template full-api
sloppy create my-api --template dogfood
sloppy create my-api --format json
```

Then:

```
cd my-api
sloppy build
sloppy run --once GET /health
sloppy package
```

## Overwrite behavior

`create` refuses to copy into a non-empty destination. Use `--force` only when
you intentionally want template files copied over existing files. It does not
delete stale files or replace the directory wholesale.

`--no-git` is reserved for future git initialization behavior. `create`
currently does not run `git init`, and template files such as `.gitignore` are
copied either way.

## JSON output

`--json` and `--format json` print a small structured success object:

```json
{
  "created": true,
  "path": "my-api",
  "template": "minimal-api",
  "next": ["cd my-api", "sloppy build", "sloppy run --once GET /health"]
}
```

Errors are diagnostics on stderr and exit with status 1.

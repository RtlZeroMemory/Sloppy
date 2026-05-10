# `sloppy create`

Copy a built-in project template into a new local app directory.

```sh
sloppy create <name> [--template api|minimal-api|program|cli|package-api|node-compat]
                     [--force] [--no-git] [--format text|json]
```

Default template: `api`.

## Templates

| Template | Purpose |
| --- | --- |
| `api` | SQLite-backed backend starter with routes, services, config, and packaging flow. |
| `minimal-api` | Tiny API for the smallest possible first run. |
| `program` | Small Program Mode starter. |
| `cli` | Practical CLI-style Program Mode starter. |
| `package-api` | Backend starter using a compatible local pure-JS package. |
| `node-compat` | Program starter using supported Node compatibility shims. |

`sloppy create <name>` defaults to `api`. These starters are public alpha
templates: they are intended for real experiments and early apps, but their
structure and supported runtime surface can still change before a stable
release. The `package-api` template includes a local `file:` dependency and
needs `npm install --ignore-scripts --no-audit` before `sloppy build`. Sloppy
can bundle supported package modules from `node_modules`, but it does not
install packages, solve versions, or provide full Node compatibility.

## Examples

```sh
sloppy create my-api
sloppy create my-api --template minimal-api
sloppy create my-tool --template program
sloppy create my-cli --template cli
sloppy create my-package-api --template package-api
sloppy create my-node-tool --template node-compat
sloppy create my-api --format json
```

Then:

```sh
cd my-api
sloppy build
sloppy routes .sloppy
sloppy run .sloppy --once GET /health
sloppy package
```

Template-specific next steps:

```sh
cd my-api
sloppy build
sloppy routes .sloppy
sloppy run .sloppy --once GET /health

cd my-minimal-api
sloppy build
sloppy run .sloppy --once GET /health

cd my-tool
sloppy run src/main.ts -- --name Ada

cd my-cli
sloppy run src/main.ts -- --help

cd my-package-api
npm install --ignore-scripts --no-audit
sloppy build
sloppy deps .sloppy

cd my-node-tool
sloppy build
sloppy run .sloppy
```

## Overwrite behavior

`create` refuses to copy into a non-empty destination. Use `--force` only when
you intentionally want to remove an existing destination directory before
copying the template. This deletes stale files in that directory before the new
template files are written.

`--no-git` is reserved for future git initialization behavior. `create`
currently does not run `git init`, and template files such as `.gitignore` are
copied either way.

## JSON output

`--json` and `--format json` print a small structured success object:

```json
{
  "created": true,
  "path": "my-api",
  "template": "api",
  "next": ["cd my-api", "sloppy build", "sloppy routes .sloppy", "sloppy run .sloppy --once GET /health"]
}
```

Errors are diagnostics on stderr and exit with status 1.

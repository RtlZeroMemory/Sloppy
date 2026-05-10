# Program Mode

Program Mode runs route-free Sloppy source as a console-style program. Use it
for small tools, local automation, background jobs, and worker entrypoints that
need the Sloppy stdlib but do not expose HTTP routes.

## Create A Program

```sh
sloppy create hello-tool --template program
cd hello-tool
sloppy run -- --name Ada
```

The template uses `sloppy.json` to pin the project as Program Mode:

```json
{
  "kind": "program",
  "entry": "src/main.ts",
  "outDir": ".sloppy",
  "environment": "Development"
}
```

Direct source input also works without `sloppy.json`:

```sh
sloppy run src/main.ts -- --name Ada
sloppy build src/main.ts
sloppy package src/main.ts
```

## Entrypoint

The runtime chooses the entrypoint in this order:

1. exported `main(args, ctx)`
2. default exported function `(args, ctx)`
3. top-level module execution only

```ts
export async function main(args, ctx) {
    console.log(`hello ${args[0] ?? "world"}`);
    console.log(`cwd=${ctx.cwd}`);
    return 0;
}
```

Arguments are everything after `--`:

```sh
sloppy run src/main.ts -- one two
sloppy run --artifacts .sloppy -- one two
sloppy run .sloppy/package -- one two
```

`ctx` has this shape:

```ts
{
    kind: "program",
    args: string[],
    cwd: string,
    environment: string,
    plan: {
        kind: "program",
        metadataCompleteness: "opaque"
    }
}
```

`ctx.args` matches `args`. `cwd` is the current working directory of the
`sloppy run` process. `environment` is the source-input environment selected by
`sloppy.json` or `--environment`; artifact/package runs use `Development`
unless the artifact was just compiled in the same command.

## Output And Exit Codes

Program Mode installs a Sloppy-owned `console` while the entry module runs.

| API | Stream |
| --- | --- |
| `console.log`, `console.info`, `console.debug` | stdout |
| `console.warn`, `console.error` | stderr |

Values are formatted safely, including circular objects. Each console call
writes one line. Program console output is currently collected while the
entrypoint runs and written after the entrypoint completes; it is not a
streaming terminal interface.

Returning an integer from `main` sets the process exit code. Valid values are
`0` through `255`. Returning an out-of-range number, throwing, or rejecting
exits non-zero with a diagnostic.

## Build, Inspect, Package

```sh
sloppy build
sloppy routes .sloppy
sloppy capabilities .sloppy --format json
sloppy doctor .sloppy
sloppy audit .sloppy --format json
sloppy package
sloppy run .sloppy/package -- one two
```

Program Plans have `kind: "program"` and empty route/handler arrays. `routes`
reports that the absence of routes is intentional. `openapi` is web-only and
fails clearly for Program Plans.

The package manifest records `kind: "program"` and the canonical copied
artifact paths: `artifacts/app.plan.json`, `artifacts/app.js`, and
`artifacts/app.js.map`. The current package runner validates that the manifest
exists and then loads that canonical `artifacts/` layout.

## Dependencies And Stdlib Boundary

Program Mode can import the documented Sloppy stdlib subpaths such as
`sloppy/fs`, `sloppy/os`, `sloppy/time`, `sloppy/codec`, `sloppy/crypto`,
`sloppy/net`, and `sloppy/workers`. Importing a stdlib subpath records the
matching runtime feature in the Plan.

Program Mode can also bundle compatible installed pure-JavaScript packages from
`node_modules`, CommonJS modules, JSON modules, string-literal dynamic imports,
and computed dynamic imports that resolve inside `moduleInclude` graphs. See
[Using installed packages](using-packages.md).

Program Mode does not install packages, provide full Node globals, load native
addons, implement FFI, or expose raw terminal APIs. Node builtin compatibility
exists only through explicit `node:*` shim modules such as `node:path` and
partial shims such as `node:fs`. Use the Sloppy stdlib surface when it fits:
`File`/`Directory` from `sloppy/fs`, `Process` and `Environment` from
`sloppy/os`, and so on.

## Examples

- `examples/program-hello`
- `examples/program-fs-process`
- `examples/package-zod-like`
- `examples/dynamic-module-include`

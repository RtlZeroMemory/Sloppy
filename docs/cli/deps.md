# `sloppy deps`

Inspect dependency graph metadata emitted by `sloppyc`. `deps` is read-only and
does not enter V8.

```text
sloppy deps <artifacts-dir|plan.json> [--format text|json] [--explain]
sloppy deps --plan <path> [--format text|json] [--explain]
sloppy deps --artifacts <dir> [--format text|json] [--explain]
```

Use `sloppy deps .sloppy` for the common case.

## What It Reads

`deps` first looks for `dependencyGraph` embedded in `app.plan.json`. If the
command was given an artifact directory and the Plan does not embed the graph,
it falls back to `deps.graph.json` next to the Plan.

`--explain` is text-only. It adds a package compatibility summary with package
counts, Node shim counts, and compatibility finding counts. Use `--format json`
when a tool needs the full graph.

If neither exists, the command exits non-zero with:

```text
sloppy deps: Plan has no dependencyGraph metadata
```

## Text Output

```text
Dependency graph: 1 package(s), 4 module(s), 1 asset(s), 1 Node binding(s), 0 finding(s)
Packages (1)
  tiny-pkg  esm
Modules (4)
  src/main.ts  esm
  node_modules/tiny-pkg/index.js  esm
Assets (1)
  public/logo.svg  assetInclude:public/**/*
Node compatibility (1)
  node:path  supported -> sloppy/node/path
Findings (0)
  -

Compatibility explanation
  Packages bundled: 1
  Bundled modules: 4
  Packaged assets: 1
  Node compatibility shims used: 1
    supported=1  partial=0  stubbed=0  unsupported=0
  Compatibility findings: 0
  Shim statuses:
    Node shim matrix (1)
      node:path  supported -> sloppy/node/path
  No dependency compatibility findings were recorded.
  Package graph is sealed for outside-checkout use.
```

## JSON Output

```json
{
  "packages": [],
  "modules": [],
  "assets": [],
  "nodeBuiltins": [],
  "compatibilityFindings": []
}
```

The JSON is a stable summary projection for CLI consumers. Use
`deps.graph.json` when a tool needs the full graph.

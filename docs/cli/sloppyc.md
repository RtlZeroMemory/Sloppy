# `sloppyc`

`sloppyc` is the standalone Sloppy compiler. Most users should call
`sloppy build` or `sloppy run`; those commands invoke `sloppyc` with the right
paths for the current project.

Use `sloppyc` directly when you are testing compiler behavior or building a
lower-level tool around emitted artifacts.

```sh
sloppyc --help
sloppyc --version
sloppyc build <input.js|input.ts> --out <directory> [--kind web|program]
              [--module-include <glob>] [--asset-include <glob>]
```

## Build

```sh
sloppyc build src/main.ts --out .sloppy
```

Output:

```text
.sloppy/
  app.plan.json
  app.js
  app.js.map
  deps.graph.json   optional
```

## Flags

| Flag | Purpose |
| --- | --- |
| `--out <directory>` | Required artifact output directory |
| `--kind web\|program` | Override source kind. Without it, web app source is tried first and route-free non-web source falls back to Program Mode. |
| `--environment <name>` | Select environment-specific config |
| `--host <host>` | Emit server host metadata into the Plan |
| `--port <port>` | Emit server port metadata into the Plan |
| `--config-dir <dir>` | Read config from a directory other than the input root |
| `--config <key=value>` | Apply a config override |
| `--timings-json <file>` | Write compiler phase timing and counter JSON |
| `--diagnostics-timing-json <file>` | Alias for `--timings-json` |
| `--module-include <glob>` | Include matching modules in the sealed artifact graph for computed dynamic imports |
| `--asset-include <glob>` | Record and package matching non-executable assets |

`--timings-json` is contributor measurement output. It is useful for comparing
compiler changes; it is not a public performance guarantee.

`--capability <name>` is an internal CLI handoff used by `sloppy.json`
project mode. Supported values are the documented `sloppy.json` capability
names.

Program Mode uses Oxc parsing and TypeScript transform support before the
compiler rewrites supported ESM imports/exports into the generated artifact
bundle. It supports static relative imports, installed pure-JavaScript package
imports, CommonJS/JSON modules, string-literal dynamic imports, computed
dynamic imports over `--module-include` graphs, documented Sloppy stdlib
imports, and the explicit Node compatibility registry. It rejects unsupported
package export shapes, native addons, unsupported Node builtins, remote
imports, and provider imports with source diagnostics.

## Exit codes

| Code | Meaning |
| --- | --- |
| `0` | Build succeeded |
| `1` | Compilation failed with a diagnostic |
| `2` | CLI usage error |

Diagnostics print to stderr and include stable diagnostic codes where the
compiler has one.

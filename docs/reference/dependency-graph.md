# Dependency Graph Reference

The dependency graph records the modules, installed packages, assets, and Node
compatibility shims that the compiler bundled into an artifact.

It is build-time metadata. The runtime executes the generated `app.js` bundle
and validates Plan/runtime features; it does not read `node_modules` at run
time.

## Where It Appears

When the compiler finds dependency graph content, it writes the graph in two
places:

- `app.plan.json` as `dependencyGraph`
- `.sloppy/deps.graph.json` as an inspectable companion artifact

Packages created by `sloppy package` copy `deps.graph.json` when present and
record it in `manifest.json`:

```json
{
  "schema": "sloppy.app-package.v1",
  "dependencyMode": "bundled",
  "artifacts": {
    "plan": "artifacts/app.plan.json",
    "bundle": "artifacts/app.js",
    "sourceMap": "artifacts/app.js.map",
    "dependencyGraph": "artifacts/deps.graph.json"
  }
}
```

The graph is also copied into `app.js.map` under `x_sloppy.dependencyGraph` so
diagnostic tooling can relate bundled dependency modules to source IDs without
absolute machine paths.

## Top-Level Shape

```json
{
  "schemaVersion": 1,
  "resolver": {
    "profiles": [
      "sloppy-stdlib",
      "relative-source",
      "installed-packages",
      "node-compat-shims"
    ],
    "conditions": ["sloppy", "import", "require", "node", "development", "production", "default"]
  },
  "packages": [],
  "modules": [],
  "assets": [],
  "nodeBuiltins": [],
  "compatibilityFindings": []
}
```

IDs and paths are repo-relative, use forward slashes, and must not include
absolute local machine paths in committed goldens.

| Field | Type | Meaning |
| --- | --- | --- |
| `schemaVersion` | number | Dependency graph schema version. Current value is `1`. |
| `resolver.profiles` | string[] | Resolver profiles that were active for the build. |
| `resolver.conditions` | string[] | Package export/import conditions considered during resolution. |
| `packages` | object[] | Installed package roots included in the graph. |
| `modules` | object[] | Bundled source, package, JSON, CommonJS, and shim modules. |
| `assets` | object[] | Non-executable files included by `assetInclude`. |
| `nodeBuiltins` | object[] | Node compatibility registry entries used by the graph. |
| `compatibilityFindings` | object[] | Package and compatibility warnings or errors surfaced by tooling. |

## Packages

`packages[]` contains installed package records discovered from
`node_modules`:

```json
{
  "name": "tiny-pkg",
  "version": "1.0.0",
  "root": "node_modules/tiny-pkg",
  "packageJson": "node_modules/tiny-pkg/package.json",
  "entry": "node_modules/tiny-pkg/index.js",
  "format": "esm",
  "source": "installed"
}
```

Installed package graph support is experimental. Supported package resolution
starts from what is already installed. Sloppy does not call a registry, install
dependencies, solve semver ranges, or read a lockfile to choose versions.

The supported package subset covers package.json `exports`, `imports`, `main`,
and `type` for common pure-JavaScript packages. Export condition resolution
prefers `sloppy`, then the active module mode (`import` or `require`), then
`node`, `development`, `production`, and `default`. Unsupported export shapes
fail with `SLOPPYC_E_PACKAGE_EXPORT_UNSUPPORTED` instead of silently choosing a
different entry.

## Modules

`modules[]` contains bundled JavaScript, TypeScript, JSON, and compatibility
shim modules:

```json
{
  "id": "src/main.ts",
  "source": "src/main.ts",
  "format": "esm",
  "package": null,
  "imports": ["tiny-pkg", "./plugins/alpha.js"],
  "resolvedImports": [
    {
      "specifier": "tiny-pkg",
      "resolvedId": "node_modules/tiny-pkg/index.js",
      "kind": "package"
    }
  ],
  "dynamicImports": []
}
```

`format` is one of:

- `esm`
- `commonjs`
- `json`

CommonJS modules run in a generated wrapper with `exports`, `module`,
`require`, `require.resolve`, `require.cache`, `__filename`, and `__dirname`.
JSON modules export the parsed JSON value as the module value and default
export.

## Dynamic Imports

String-literal dynamic imports are resolved at build time and recorded in
`dynamicImports[]`.

Computed dynamic imports are runtime-resolved only against the sealed module
graph. Include possible targets with `moduleInclude`:

```json
{
  "moduleInclude": ["src/plugins/**/*.js"]
}
```

If a computed import resolves outside the sealed graph, the generated bundle
throws `SLOPPY_E_MODULE_NOT_FOUND` with a `moduleInclude` hint.

Dynamic import controls when an already bundled module is evaluated. It is not
runtime package discovery.

## Assets

`assets[]` records files included by `assetInclude`:

```json
{
  "path": "public/logo.svg",
  "includedBy": "assetInclude:public/**/*"
}
```

Assets are packaged and inspectable metadata. They are not executable modules.

## Node Builtins

`nodeBuiltins[]` records each Node builtin resolved through the compatibility
registry:

```json
{
  "specifier": "node:path",
  "status": "supported",
  "backing": "sloppy/node/path"
}
```

`status` can be `supported`, `partial`, `stubbed`, or `unsupported`. Stubbed
modules are importable but throw for unsupported operations or report
detection-only values such as non-TTY streams. See
[Node compatibility](node-compatibility.md) for the current registry.

## Compatibility Findings

`compatibilityFindings[]` records warnings and errors from package and Node
compatibility analysis:

```json
{
  "severity": "error",
  "code": "SLOPPYC_E_NATIVE_ADDON_UNSUPPORTED",
  "message": "Package \"sharp\" requires a native Node addon.",
  "source": "node_modules/sharp/package.json"
}
```

Obvious native addon package shapes are rejected because Sloppy does not
support Node native addons or N-API yet. Detection is based on known package
and native-entry patterns such as `.node`, `node-gyp-build`, and `bindings`.
It is not a formal guarantee that every native package shape is recognized.

`sloppy audit` reports dependency graph findings. `sloppy doctor` reports the
dependency graph summary. `sloppy deps` prints the graph directly.
`sloppy deps .sloppy --explain` adds a text compatibility summary for package
review while `--format json` remains the full-fidelity machine-readable graph.
The text summary breaks down Node compatibility shim statuses by
`supported`/`partial`/`stubbed`/`unsupported` and findings by severity.

## Compatibility Matrix

A committed compatibility matrix lives at `tests/fixtures/npm-compat/`. Each
fixture describes a package shape (CJS/ESM main, package `type`, `exports`
string/object/subpath/extensionless/pattern/nested conditions, `imports`
aliases and patterns, self-reference, JSON require, optional dependency
metadata, peer dependency metadata, native addon, unsupported Node builtin,
and dynamic require), and the resolver test exercises each entry. The matrix
is the source of truth for what shapes Sloppy promises to resolve.

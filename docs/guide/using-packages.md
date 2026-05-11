# Using Installed Packages

Installed package graph support is experimental. Sloppy can bundle installed
pure-JavaScript package dependencies when the compiler can resolve, transform,
package, and execute them inside Sloppy's runtime boundary.

This is not a package manager. Use npm, pnpm, Yarn, or another package manager
to install packages first. Sloppy reads the already installed files from
`node_modules` during build and emits a sealed artifact graph.

## Prerequisites

- A Sloppy project with `sloppy.json`.
- An installed package under `node_modules`.
- Package code that uses JavaScript Sloppy can transform and runtime APIs that
  Sloppy supports or shims.

Obvious native Node addon package shapes are rejected with clear diagnostics.
Sloppy does not support Node native addons or N-API yet. Detection is based on
known package and native-entry patterns, not a formal guarantee that every
native package shape is recognized.

Unrestricted Node builtins and packages that rely on implicit Node globals may
also fail with clear diagnostics.

The current Node compatibility layer covers practical subsets of common
pure-JavaScript package assumptions: explicit `node:process`, `node:buffer`,
`node:fs/promises`, `node:assert`, `node:stream`, `node:stream/promises`,
`node:crypto`, path/events, and related utility shims. See the
[Node compatibility reference](../reference/node-compatibility.md) for exact
members and known differences from Node.

## Install With Your Package Manager

```sh
npm install tiny-invariant
```

Sloppy does not install dependencies for you and does not solve semver ranges.
The package manager owns version selection.

## Import The Package

```ts
import invariant from "tiny-invariant";

export function main(args) {
    invariant(args.length > 0, "expected at least one argument");
    console.log(`first arg: ${args[0]}`);
}
```

Build or run the app:

```sh
sloppy build
sloppy run -- one
```

The compiler resolves the package from the importing module by walking upward
to `node_modules/<package>`. Scoped packages and package subpaths are supported
when they resolve through the supported package rules.

## Supported Package Rules

Sloppy supports a practical package.json subset for installed packages:

- `exports` string entries
- `exports` object entries with `sloppy`, `import`, `require`, and `default`
  conditions
- package subpath exports such as `"./feature"`
- `imports` for package-local `#...` specifiers where resolvable
- `main`
- `type: "module"` and `type: "commonjs"`
- extension and directory index resolution for `.ts`, `.tsx`, `.js`, `.mjs`,
  `.cjs`, and `.json`

Condition order is:

- ESM/import: `sloppy`, `import`, `default`
- CommonJS/require: `sloppy`, `require`, `default`

Unsupported package export shapes fail with
`SLOPPYC_E_PACKAGE_EXPORT_UNSUPPORTED`.

## CommonJS And JSON

CommonJS packages can be bundled when their `require(...)` calls are
statically resolvable. ESM imports of CommonJS modules receive the CommonJS
`module.exports` value as the default-like module value. Named imports from
CommonJS are best-effort and should not be treated as full Node semantics.

JSON files can be imported as JSON modules. They are parsed at build time and
emitted into the bundle.

## Dynamic Imports

String-literal dynamic imports are resolved at build time:

```ts
const feature = await import("./features/report.js");
const pkg = await import("tiny-invariant");
```

Computed imports only work against modules already sealed into the artifact
graph. Add possible targets to `moduleInclude`:

```json
{
  "kind": "program",
  "entry": "src/main.ts",
  "moduleInclude": ["src/plugins/**/*.js"]
}
```

At runtime, `import("./plugins/" + name + ".js")` succeeds only when the
resolved module is in that graph. Otherwise the generated bundle throws
`SLOPPY_E_MODULE_NOT_FOUND` with a `moduleInclude` hint.

## Include Assets

Use `assetInclude` for files that should be packaged but not executed:

```json
{
  "assetInclude": ["public/**/*", "src/views/**/*.html"]
}
```

Assets are recorded in the dependency graph and package metadata. They are not
JavaScript modules.

## Inspect The Graph

```sh
sloppy deps .sloppy
sloppy deps .sloppy --format json
sloppy audit .sloppy
sloppy doctor .sloppy
```

`sloppy deps` lists bundled packages, module counts, assets, Node compatibility
shims, and compatibility findings.

## Current Limits

- No registry install or package manager integration.
- No native Node addons or N-API. Obvious native addon shapes are rejected, but
  detection is not a complete native-package classifier.
- No full Node builtin parity. The stream and crypto shims are partial, and
  workers, VM, child process, inspector, REPL, and Node internals remain
  unsupported.
- No implicit global Node runtime identity.
- No unrestricted runtime discovery outside the sealed graph.

Real installed packages work when their JavaScript and runtime API usage are
compatible with Sloppy's loader and shims.

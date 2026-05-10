# Why `node_modules` Is Build Input

Sloppy can consume installed pure-JavaScript packages, but it does not run as
Node and does not treat `node_modules` as a runtime dependency directory.

During build, `sloppyc` may resolve packages from `node_modules`, transform the
supported JavaScript/TypeScript/CommonJS/JSON modules, and emit a sealed Sloppy
artifact graph. At run time, the generated artifact runs without returning to
the original source checkout or package directory for bundled modules.

## The Reasoning

Sloppy's runtime contract starts from the Plan and generated artifacts. That
contract is strongest when the compiler knows what will run before the runtime
starts:

- module IDs are deterministic and repo-relative;
- package entries and export conditions are recorded;
- Node compatibility shims are explicit;
- unsupported native addons and Node builtins fail before packaging;
- packaged apps can run from copied artifacts.

This is different from Node's runtime package model. Node can discover files,
conditional branches, and package internals while the process is already
running. Sloppy intentionally avoids that for packaged artifacts.

## What Works

Use your normal package manager to install dependencies:

```sh
npm install some-package
```

Then import compatible package code from Sloppy source. The supported subset
includes practical package.json `exports`, `imports`, `main`, `type`, extension
resolution, ESM, CommonJS, JSON modules, string-literal dynamic imports, and
computed dynamic imports over `moduleInclude` graphs.

Inspect the result with:

```sh
sloppy deps .sloppy
```

## What Still Does Not Work

Sloppy does not provide:

- registry install or semver solving;
- full Node module resolution parity;
- native Node addons or N-API;
- full Node globals such as process-wide `process` and `Buffer`;
- full Node builtin compatibility;
- runtime discovery outside the sealed artifact graph.

Packages that depend on those surfaces fail with specific diagnostics or shim
errors instead of pretending to work.

## What To Do Instead

| Need | Sloppy option |
| --- | --- |
| A compatible pure-JavaScript utility | Install it with your package manager and let Sloppy bundle it. |
| A small dependency-free helper | Vendor the code into your source tree. |
| A Node native addon | Run that logic in a separate service or wait for a first-party Sloppy feature. |
| Node `fs`, `path`, `crypto`, or similar APIs | Prefer `sloppy/*` APIs, or use the explicit `node:*` compatibility shim when supported. |
| Runtime plugin discovery | Use `moduleInclude` to seal known modules into the artifact graph. |

The long-term direction is selective compatibility, not drop-in Node parity.
See [Node compatibility roadmap](../roadmap/node-compatibility.md).

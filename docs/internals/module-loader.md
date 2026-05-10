# Artifact Module Loader

The artifact module loader is the generated JavaScript machinery that runs
Sloppy's sealed module graph without Node.js.

It is emitted by `sloppyc` into `app.js`. The native runtime validates the Plan
and evaluates the bundle through the V8 bridge; V8 does not resolve files or
load `node_modules` at runtime.

## Responsibilities

The generated loader owns:

- a deterministic module registry;
- module execution state and cache;
- ESM-ish import/export lowering;
- CommonJS `exports`, `module`, and `require` wrappers;
- string-literal dynamic imports over resolved modules;
- computed dynamic imports over modules already included in the graph;
- JSON modules;
- Node compatibility shim modules emitted by the compiler.

The loader does not:

- install packages;
- read `node_modules` at runtime;
- implement full Node module identity;
- load native addons;
- discover files outside the packaged graph.

## Module Records

Each bundled module has an ID and format:

```text
id: "src/main.ts"
format: "esm" | "commonjs" | "json"
```

The compiler emits factories for source modules and shim modules. The loader
stores each module's exports, evaluation state, promise, and error. Re-imports
reuse the cached record.

## Resolution

Compile-time resolution records a map from `(from module, specifier)` to a
sealed module ID. Runtime `require` and string-literal dynamic imports use that
map.

Computed dynamic imports are resolved at runtime only against already emitted
module IDs, extension variants, and directory indexes that are present in the
sealed graph. A miss throws:

```text
SLOPPY_E_MODULE_NOT_FOUND
Dynamic import or require resolved to '<specifier>', but that module was not included in the Sloppy artifact graph.
```

The fix is to add a `moduleInclude` pattern that makes the possible target
modules explicit.

## Cycles

The loader caches modules before factory completion, so basic cycles can make
partial exports visible during evaluation. Exact ESM temporal-dead-zone
semantics are not guaranteed in this foundation. Code that depends on
fine-grained cyclic ESM initialization order should be treated as unverified.

## Program And Web Startup

Program Mode loads the entry module, then calls the named `main` export, a
default function export, or only top-level module code.

Web mode still enters through generated handler registration and the current
app-host bundle shape. Dependency graph metadata can be present in a web Plan,
but route extraction and handler lowering remain the web execution contract.

## Source Maps

`app.js.map` includes `x_sloppy.programModules` and
`x_sloppy.dependencyGraph` when a program/dependency graph is emitted. That
metadata helps diagnostics map bundled dependency modules back to their
repo-relative source IDs without absolute machine paths.

## Contributor Rules

- Keep the loader independent of Node runtime APIs.
- Keep libuv, V8, and native runtime details out of generated public JS.
- Preserve deterministic module IDs and emitted JSON ordering.
- Add tests for both build-time graph metadata and runtime behavior whenever a
  module format or resolution path changes.
- Keep unsupported Node or package behavior failing through explicit
  diagnostics or shim errors, not silent `undefined` behavior.

# Node Compatibility Roadmap

Sloppy's Node compatibility goal is selective package reuse, not Node parity.

The runtime remains Sloppy: a compiler-first app host with Plan validation, a C
kernel, and an isolated V8 bridge. Node APIs enter only through explicit
compatibility shims backed by Sloppy Core APIs or pure JavaScript behavior.

## What Exists Now

The current foundation includes:

- experimental installed package resolution from existing `node_modules`;
- package.json `exports`, `imports`, `main`, and `type` support for common
  pure-JavaScript packages;
- ESM, CommonJS, and JSON module bundling in generated artifacts;
- string-literal dynamic import resolution;
- computed dynamic imports limited to `moduleInclude` graphs;
- `assetInclude` metadata and packaging;
- dependency graph metadata in Plan/source maps and optional `deps.graph.json`;
- `sloppy deps` inspection;
- a Node builtin compatibility registry;
- partial/pure-JS shims for selected builtins, including practical subsets of
  `process`, `Buffer`, `fs/promises`, `assert`, `stream`, `crypto`, `path`,
  `events`, `url`, `querystring`, `util`, `timers`, `os`, `module`,
  `string_decoder`, `zlib`, `constants`, `console`, `perf_hooks`, and
  `diagnostics_channel`;
- importable stubs for detection-heavy modules such as `http`, `https`, and
  `tty`.

This makes compatible pure-JavaScript dependencies usable when their runtime
needs fit Sloppy's loader and shim surface.

## What Is Intentionally Deferred

Deferred work includes:

- package registry install and version solving;
- lockfile-aware dependency policy;
- full Node module identity and resolution parity;
- native Node addons and N-API;
- full Node HTTP client/server compatibility through `node:http` or
  `node:https`;
- full Node streams beyond the small compatibility subset;
- `worker_threads`, `child_process`, `vm`, inspector, REPL, and test-runner
  internals;
- unrestricted Node globals. Bundled package programs may receive temporary
  `global`, `process`, and `Buffer` compatibility globals, but Sloppy does not
  expose process-wide Node identity;
- native-quality synchronous Node crypto semantics; the current hash/HMAC shim
  follows Sloppy's Promise-shaped crypto API;
- package execution outside Sloppy's sealed artifact graph.

Unsupported surfaces should fail clearly during build or through a shim error.
They should not look like supported behavior.

Obvious native addon package shapes are rejected in the current compiler, but
the detection is pattern-based. It looks for known native entry/package signals
and is not a guarantee that every native package shape is recognized.

## Compatibility Strategy

Compatibility grows one builtin or package family at a time:

1. Identify the package behavior Sloppy wants to support.
2. Check whether Sloppy Core already has an equivalent API.
3. Add or extend a shim only for that backed behavior.
4. Record capability and dependency graph metadata.
5. Add diagnostics for unsupported members.
6. Add tests and docs that distinguish implemented and deferred support.

If a package needs a Node API that Sloppy Core does not have, the compatibility
slice either waits for a Sloppy Core API or documents the gap. The shim layer
must not smuggle in a hidden Node runtime.

## User-Facing Rule

Use this wording in product docs:

> Sloppy can consume installed pure-JavaScript package dependencies when they
> can be resolved, transformed, bundled, and executed inside Sloppy's runtime
> boundary. Node API compatibility is partial and grows through explicit shims
> backed by Sloppy Core APIs.

Do not claim full npm compatibility, full Node compatibility, or drop-in
compatibility for existing Node applications.

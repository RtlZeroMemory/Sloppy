# Compiler

## Purpose

`sloppyc` is Sloppy's Rust compiler/build tool. It parses the supported JavaScript source
subset, validates the current application shape, and emits deterministic artifacts for the
runtime host.

## Current Output

`sloppyc build <source.js> --out <dir>` emits deterministic artifacts:

- `app.plan.json`;
- generated `app.js`;
- `app.js.map`.

Artifact hashes use deterministic `sha256:` values. Source-map and artifact paths must be
normalized so tests and package fixtures do not depend on a developer's checkout path.

## Supported Shape

The current compiler supports a narrow source subset:

- recognized first-party imports from `sloppy` modules;
- direct app creation and route registration shapes documented in
  `docs/compiler-supported-syntax.md`;
- inline route handlers that return supported `Results.*` descriptors;
- selected async handler shapes that settle through the current V8 Promise contract;
- provider metadata for first-party database providers;
- capability, route, result, source-map, completeness, and configuration metadata required
  by the current runtime host.

Unsupported syntax must fail with deterministic diagnostics. `dynamic route strings`,
arbitrary module graphs, decorators, controllers, TypeScript lowering, npm packages, and
Node built-ins are not part of the current compiler contract.

## Runtime Boundary

The compiler emits artifacts; it does not execute app code. Runtime execution is owned by
`sloppy run --artifacts` or source-input `sloppy run <source.js>` after compilation. The
compiler does not implement Node package resolution, npm install behavior, or a package
manager.

The compiler-generated JavaScript targets Sloppy's current runtime bridge. It should not
depend on Node, Bun, Deno, or ambient globals outside the documented Sloppy bootstrap
contract. In particular, this compiler does not implement Node package behavior or Node
compatibility.

## Diagnostics

Compiler diagnostics should include stable codes, source spans, clear messages, and hints
that describe the supported contract. Hints must not point users toward unsupported future
features as if they exist.

## Determinism

Compiler tests pin deterministic output. Any output change must update goldens with a
contract explanation. Generated artifacts must not embed private absolute paths, secrets,
timestamps, nondeterministic ordering, or environment-specific values.

## Related Docs

- `docs/compiler-supported-syntax.md`
- `docs/app-plan.md`
- `docs/project/compiler-inference-engine-architecture.md`
- `docs/testing-strategy.md`

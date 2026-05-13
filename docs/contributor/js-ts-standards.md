# JavaScript and TypeScript Standards

## Purpose

Sloppy's JavaScript and TypeScript code is part of the runtime contract. The checked-in
bootstrap stdlib, public examples, compiler fixtures, and future generated app artifacts
are not casual frontend code. They describe the public app-host model that the C runtime
and Rust compiler will eventually validate and execute.

JS/TS in this repository must be simple, explicit, deterministic, and readable. It should
make the app model discoverable without imitating Node, Bun, Deno, or browser compatibility
layers. It must not introduce package-manager assumptions before a scoped packaging story
exists.

For runtime and stdlib implementation details, also follow
[JavaScript stdlib standards](../internals/javascript-stdlib-standards.md).

## Scope

These standards govern:

- bootstrap stdlib modules under `stdlib/sloppy/`;
- public examples under `examples/`;
- compiler fixtures and golden JS/TS inputs;
- future public API surface;
- future generated app artifacts when their shape is owned by this repo.

## Supported JS/TS Style

- Prefer modern ESM syntax: `import`, `export`, and small explicit modules.
- Do not use CommonJS unless a tooling-only file documents the reason.
- Do not use Node-only globals or modules in `stdlib/sloppy/`:
  - `process`;
  - `Buffer`;
  - `fs`;
  - `path`;
  - `require`;
  - `__dirname`;
  - `__filename`.
- Do not use browser DOM APIs in the bootstrap stdlib.
- Do not depend on npm packages in the bootstrap stdlib.
- Do not use dynamic imports in the bootstrap stdlib except behind a documented
  and tested Sloppy-owned loading boundary.
- The worker bridge allows the single bridge-gated dynamic `import(modulePath)` in
  `stdlib/sloppy/workers.js` for `Worker.start()` bootstrap module execution. It must stay
  behind that API and must not become general package, Node, or npm resolution.
- Avoid top-level side effects except intentional export initialization.
- `stdlib/sloppy/internal/runtime-classic.js` is the narrow exception that may publish
  `globalThis.__sloppy_runtime`; it must not add broader globals or Node/browser shims.
- Keep public modules small and stable.

Node may appear only in test infrastructure when the docs say the test is an execution
convenience.

## Runtime Contract Discipline

The bootstrap stdlib must expose plain, stable descriptor objects where possible. Public
descriptors must be intentionally shaped, serializable where practical, and documented by
the behavior docs that introduce them.

Rules:

- Prefer small factory functions that return plain frozen descriptors.
- Avoid magical hidden state. When hidden state is necessary, keep it behind tested public
  APIs and do not expose it as global mutable runtime state.
- Avoid global mutable registries unless the registry is explicitly documented and tested.
- Avoid implicit side effects at import time.
- Avoid relying on object key enumeration order unless the order is documented and tested.
- Avoid dynamic property access for core public APIs when compiler extraction will need to
  understand the call.
- Avoid computed method names for compiler-extractable APIs.
- Avoid runtime-generated routes in examples intended for compiler extraction.
- Avoid conditionally registered routes in compiler-extractable examples.
- Do not use decorators unless a scoped source doc explicitly adopts and tests them.
- Compiler-extractable examples must use literal route strings and explicit calls.

## API Design Rules

The public API should feel like ASP.NET Minimal API-inspired app-host ergonomics, not an
Express clone and not a raw runtime primitive layer.

Rules:

- APIs must be discoverable and boring.
- Do not add fluent chain methods unless each method has clear value and tests.
- Avoid fluent chain explosion.
- Avoid aliases unless the alias is strongly justified by user-facing ergonomics.
- Do not use cute names in public API.
- Do not add broad public surface before the runtime/compiler supports it.
- Do not pretend features exist. Unsupported future features must fail honestly or remain
  out of public examples.
- Keep result descriptors small and serializable.
- Route registration metadata should be explicit.

Good public API shape:

```js
app.mapGet("/users/{id:int}", getUser).withName("Users.Get");
```

Suspicious public API shape:

```js
app.route("users").doMagic().autoInferEverything().please();
```

## Error Behavior

JS bootstrap misuse should fail early with clear `Error` or `TypeError` objects.

Rules:

- Include the API or method name in the message.
- Include the invalid token, route, or module name when safe.
- Never include secrets, passwords, access tokens, or raw connection strings.
- Keep errors deterministic enough for tests.
- Fail early at registration, build, or freeze time.
- Unsupported future features must fail honestly.

Diagnostics-style multi-line messages are acceptable when they are stable and redact
sensitive values.

## Immutability and Freeze Policy

Use `Object.freeze` for descriptor objects and finalized app/module objects when that is
the local style. Freezing is part of the contract: it tells the compiler/runtime boundary
what is stable enough to inspect.

Rules:

- Freeze public descriptors returned by helpers.
- Freeze finalized app/module objects and snapshots where current behavior does so.
- Avoid deep freeze unless necessary and tested.
- Builder/app phase transitions must be explicit.
- Mutation after freeze/build should fail clearly.

## Testing Requirements

- Every public helper gets behavior tests.
- Tests verify documented behavior, not incidental current structure.
- Examples should describe runtime support directly and precisely.
- JS tests should run in the existing V8 harness when module loading supports the scenario.
- Static tests are acceptable only when the execution harness does not support modules yet;
  the docs must say why the test is static.
- Optional Node-based ESM tests may exist as bootstrap test infrastructure only. They must
  not be presented as proof of Sloppy's package resolver or runtime execution support.
- Generated artifact golden tests must be deterministic.

Static scanners are lint gates. They are not substitutes for behavior tests.
`dev.ps1 lint` also runs `tools/windows/check-js-syntax.ps1`, which parses
tracked and newly added `.js`, `.mjs`, and `.cjs` files under source, test,
tool, template, package, example, and benchmark roots with `node --check`.

## Anti-Overengineering for JS/TS

Forbidden unless a future scoped source doc explicitly adopts and tests the behavior:

- dependency injection framework complexity beyond the current service container;
- decorators;
- plugin registries;
- global singletons;
- massive class hierarchies;
- proxy-based magic;
- reflection-like metadata systems;
- runtime schema compiler;
- OpenAPI generator;
- middleware pipeline;
- npm package scaffolding;
- bundler config;
- transpiler config;
- package-manager behavior.

Good examples:

- plain object descriptors;
- small factory functions;
- explicit builder objects;
- simple arrays/maps hidden behind tested APIs;
- simple metadata records.

Bad examples:

- huge abstract base classes;
- dynamically patched prototypes;
- implicit global route registry;
- importing Node modules in stdlib;
- `package.json` only to run examples.

## Comments

JS/TS comments should explain:

- public API contract;
- descriptor shape;
- compiler extraction constraints;
- why something is intentionally narrow;
- future integration boundary.

Do not comment obvious syntax. Prefer clear names and small functions over explanatory
noise.

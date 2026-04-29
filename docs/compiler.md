# Compiler

## Purpose

`sloppyc` exists because Sloppy is not a raw JavaScript runtime. The runtime needs compiled
artifacts: executable JavaScript, source maps, and a plan that describes the native app host
graph.

## Scope

This document defines why the compiler exists, the Rust/Oxc direction, artifact boundaries,
compiler phases, app graph extraction, static and dynamic modes, source map requirements,
golden tests, implementation phases, and acceptance criteria.

## Non-Goals

The compiler extraction MVP does not:

- perform full TypeScript type checking;
- bundle module graphs;
- resolve npm packages or implement package-manager behavior;
- assume Node compatibility;
- execute application JavaScript during compilation;
- extract services, data providers, modules, middleware, or validation schemas;
- implement source-input handoff for `sloppy run` or `app.run`;
- implement Node/npm package resolution, arbitrary import graphs, or runtime module loading;
- expose Rust/C FFI.

## Current Phase

`compiler/` contains the EPIC-21 compiler extraction MVP. `sloppyc build <input.js> --out
<directory>` parses one source file with Oxc, extracts a deliberately narrow Sloppy app
shape, and emits deterministic artifacts:

- `app.plan.json`;
- `app.js`;
- `app.js.map`.

If EPIC-21 GitHub issues are absent, this implementation follows
`docs/project/next-roadmap.md` as the source of truth for the post-roadmap plan.

## Future Phase

The compiler grows from this MVP in later slices:

1. richer route and app-host metadata;
2. source-map fidelity;
3. module extraction;
4. data provider extraction;
5. V8 bootstrap module loading handoff;
6. official TypeScript checking integration.

## Public API Shape

Current MVP:

```powershell
sloppyc --help
sloppyc --version
sloppyc build examples/compiler-hello/app.js --out .sloppy
```

The build command writes only inside the requested output directory. It creates the output
directory when needed and rejects output paths containing `..`.

## Why Rust

Rust is a good fit for compiler tooling because it provides strong package ecosystem support,
memory safety for complex graph processing, and direct access to Oxc. The runtime remains C.

## Why Oxc

Oxc is the parser for the compiler extraction MVP. The MVP uses Oxc for syntax parsing and
explicit AST extraction. It does not run arbitrary JavaScript, invoke Node, resolve
packages, or perform full TypeScript semantic checking.

## No Rust/C FFI For Now

The compiler/runtime boundary is artifacts:

- `app.js`;
- `app.js.map`;
- `app.plan.json`.

No Rust/C FFI is planned for early phases. This keeps process boundaries clear and makes
compiler output testable with golden files.

## Compiler Phases

MVP phases:

1. parse one input file;
2. validate the supported import and app factory shape;
3. extract literal `mapGet` routes and handlers;
4. assign stable numeric handler IDs in source order;
5. rewrite the public `"sloppy"` import into the internal bootstrap runtime handoff;
6. emit `app.plan.json`, `app.js`, and a placeholder `app.js.map`;
7. produce structured compiler diagnostics for unsupported syntax.

## App Graph Extraction

The compiler extraction MVP extracts:

- one app object from `Sloppy.create()` or `Sloppy.createBuilder()` plus `builder.build()`;
- literal `app.mapGet(pattern, handler)` routes;
- simple `const group = app.mapGroup(prefix)` followed by `group.mapGet(child, handler)`;
- optional `.withName("Route.Name")` route names;
- zero-argument handlers or one-argument context handlers whose single parameter is a
  simple identifier and whose body is an inline function/arrow expression returning
  `Results.text(...)`, `Results.json(...)`, `Results.ok(...)`, or `Results.noContent()`;
- result arguments that are inline JSON-safe literals, arrays, object literals, or simple
  request-context property reads such as `route.id` and `query.q`;
- source ranges for copied handler bodies.

Extraction must be deterministic. Import order must not silently decide module ordering.

Unsupported input fails with diagnostics. The MVP accepts only
`import { Sloppy, Results } from "sloppy";` as a public import and rejects arbitrary bare
imports such as `"express"`, `"fs"`, and `"node:fs"` with
`SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER`. The compiler does not implement Node package
resolution, npm resolution, import maps, dynamic imports, relative source module graphs, or
package-manager behavior. It also rejects dynamic route strings, computed method names,
multiple app objects, missing default export, unsupported handler shapes, handlers with
more than one parameter, destructured/default/rest handler parameters, handlers that close
over source-file bindings, TypeScript input or TypeScript-only handler syntax, broad module
graphs, and top-level control flow.

## Static Mode

Static plan mode requires app graph structure to be known before runtime execution. Static
mode enables:

- native route tables;
- handler IDs;
- service graph validation;
- permission audit;
- plan golden tests;
- deterministic startup diagnostics.

## Dynamic Mode

Dynamic mode is future work. It must be explicit, less optimized, and clear in diagnostics.
Dynamic behavior must not silently weaken static plan guarantees.

## Official TypeScript Checking

The compiler extraction MVP is JavaScript-input-only. Oxc is used for parsing and narrow
AST extraction, but the MVP does not lower TypeScript syntax into generated JavaScript.
Official type checking through `tsgo` or `tsc` is planned later. Sloppy should not claim
TypeScript type compatibility from its extractor alone.

## Source Map Requirements

The MVP emits a deterministic placeholder `app.js.map` because Plan v1 currently requires a
`sourceMap` section. It does not claim source fidelity. Real source maps must later support:

- runtime exception diagnostics;
- plan extraction diagnostics;
- generated handler mapping;
- user-facing names and source spans;
- golden tests for stable mapping behavior.

## Public CLI Shape

Current commands:

```powershell
cargo run --manifest-path compiler/Cargo.toml -- --help
cargo run --manifest-path compiler/Cargo.toml -- --version
cargo run --manifest-path compiler/Cargo.toml -- build examples/compiler-hello/app.js --out .sloppy
```

Future commands:

- `sloppyc check`;
- `sloppyc emit-plan`;
- `sloppyc explain`.

See also `docs/execution-model.md`. Dev and production paths must use the same artifact
architecture: `app.js`, `app.js.map`, and `app.plan.json`. The EPIC-22 `sloppy run` MVP
loads those artifacts from `--artifacts <dir>`; source-input build handoff, caching, and
watching remain future work, and runtime code must not invent a separate discovery model.

The bootstrap stdlib source layout now lives under `stdlib/sloppy/` and is staged for
runtime/package use under `lib/sloppy/bootstrap/sloppy/`. The compiler MVP recognizes only
the public bare import `import { Sloppy, Results } from "sloppy";` as input syntax. EPIC-24
rewrites that import away in generated `app.js`: the artifact reads `Results` from
`globalThis.__sloppy_runtime`, which is installed by the runtime-loaded bootstrap asset,
and registers generated handler functions through
`__sloppy_register_handler(handlerId, handler)`. The compiler does not load stdlib assets
itself and does not imply Node or npm compatibility.
`examples/hello/app.js` therefore uses a relative source import from
`stdlib/sloppy/index.js`; that example remains a bootstrap API-shape example. The
compiler-owned runnable artifact example is `examples/compiler-hello/`.
The bootstrap stdlib now also contains the EPIC-14 JavaScript-only `Sloppy.module(...)`
and `builder.addModule(...)` skeleton. The compiler still does not extract modules, sort
module graphs, or emit module plan entries.
The bootstrap stdlib now also contains the EPIC-15 JavaScript-only data/capabilities
foundation: database capability metadata, query template lowering, fake providers, and
transaction callback semantics. The compiler still does not extract capabilities, data
provider registrations, or query template literals.
EPIC-16 adds native SQLite provider tests and the `data.sqlite` stdlib shape, but the
compiler still does not extract SQLite modules, emit data provider plan entries, or lower
application template literals into native provider calls.
EPIC-17 and EPIC-18 add native PostgreSQL and SQL Server provider tests plus
`data.postgres` and `data.sqlserver` stdlib shapes, but the compiler still does not extract
provider modules, emit data provider plan entries, lower application template literals into
native provider calls, or produce metadata for live provider checks.
EPIC-19 CLI introspection reads interim plan-compatible metadata sections from
fixtures/artifacts. The compiler MVP now emits a narrow `routes` metadata section in
`app.plan.json` for CLI tooling and EPIC-22 route handoff; the native Plan v1 parser still
ignores unknown sections, while `sloppy run` reads this metadata directly for dev-only GET
dispatch. EPIC-20 benchmarks measure current native foundations only and do not imply
compiler output performance.
`examples/ergonomics/app.js` follows the same static-example rule for the broader EPIC-13
route group, result helper, and schema skeleton API shape. The compiler MVP extracts only
the tiny route group shape covered by compiler fixtures; it still does not extract schemas,
services, modules, or data providers from those broader examples.

## Internal Architecture

Current Rust layout:

```text
compiler/src/
  main.rs
  sloppyc.rs
compiler/tests/fixtures/
```

Keep the implementation direct until more than one real compiler slice needs additional
module boundaries.

## Data Structures And Schemas

Compiler-owned models should mirror but not blindly duplicate the JSON plan schema:

- source graph;
- module graph;
- route declarations;
- handler table;
- service declarations;
- provider registrations;
- diagnostic spans;
- emitted artifact manifest.

The JSON schema remains the runtime contract.

TASK 06.A defines the first minimal runtime contract shape for handwritten plan fixtures:
`schemaVersion`, `compilerVersion`, `runtimeMinimumVersion`, `stdlibVersion`,
`target.platform`, `target.engine`, bundle path/id/hash, source map path/id/hash, and
handler id/exportName/displayName entries. The compiler MVP emits those fields plus an
unknown-to-native `routes` metadata section for current CLI/future runtime integration.

## Lifecycle Flow

Compiler lifecycle target:

1. parse CLI/config;
2. discover project inputs;
3. parse source;
4. resolve graph;
5. transform;
6. extract app metadata;
7. validate;
8. emit artifacts;
9. write diagnostics.

## Golden Test Requirements

Golden tests cover:

- `hello-mapget`;
- `builder-mapget`;
- `grouped-route`;
- unsupported dynamic route diagnostics;
- missing app/default export diagnostics;
- multiple app diagnostics;
- deterministic generated `app.plan.json` and `app.js`.

Future golden tests should add source-map fidelity, module ordering, and data provider plan
contribution only when those features exist.

Golden files are public contract tests. Updating them requires review.

## Implementation Phases

### Phase A: Placeholder

Historical bootstrap phase. CLI help/version and unit tests only.

Acceptance:

- cargo build passes;
- cargo fmt passes;
- clippy passes;
- cargo test passes.

### Phase B: Fake Plan Emitter

Emit a deterministic hardcoded `app.plan.json` from CLI input.

Acceptance:

- golden plan test;
- no TypeScript parsing;
- plan matches the current minimal Plan v1 fixture field names.

### Phase C: One-Route Extractor

Extract one literal `app.mapGet` route from a tiny TypeScript file.

Acceptance:

- Oxc added intentionally;
- route path and handler name captured;
- diagnostic for nonliteral route;
- golden plan output.

### Phase D: Route Groups

Extract `app.mapGroup(...)` prefix and metadata.

Acceptance:

- grouped route emits combined path;
- tags/names appear in plan;
- duplicate route diagnostics include both source spans.

### Phase E: Module Extraction

Extract declarative modules and deterministic dependencies.

Acceptance:

- topological ordering;
- cycle diagnostic;
- module contributions in plan.

### Phase F: Services

Extract service tokens and lifetimes where statically visible.

Acceptance:

- service token appears in plan;
- duplicate tokens are diagnostics;
- route-required service references are validated.

### Phase G: Data Provider Extraction

Extract provider module registrations.

Acceptance:

- provider token in plan;
- config key references, not secrets;
- permission contribution captured.

### Phase H: Schema Extraction

Extract route body/query/route/header validation metadata.

Acceptance:

- schema section appears in plan;
- route binding references schema ID;
- unsupported dynamic schema gets diagnostic.

### Phase I: Source Maps

Emit source maps for generated JS and plan metadata.

Acceptance:

- runtime exception fixture maps generated location to TypeScript source;
- golden source map fragment is stable.

### Phase J: Cache

Compute cache keys for dev/build artifacts.

Acceptance:

- source, compiler, Oxc, target platform, target engine, and stdlib inputs affect key;
- stale cache is rejected by plan/bundle consistency checks.

## Error And Diagnostic Behavior

Compiler diagnostics must include:

- stable code;
- severity;
- TypeScript source span;
- optional related locations;
- suggested fix where safe;
- generated artifact context when relevant.

## Testing Requirements

- Rust unit tests for CLI and pure extraction helpers;
- golden tests for artifacts;
- diagnostics snapshot tests;
- integration tests with runtime once plan loader exists;
- fuzz tests for parser/extraction boundaries where applicable.

## Quality Gates

- `cargo fmt --check`;
- `cargo clippy -- -D warnings`;
- `cargo test`;
- golden tests in CI once added;
- no compiler dependencies added without spec/test update.

## Acceptance Criteria

Compiler foundation is ready for implementation when:

- artifact boundary is documented;
- plan schema has fixtures;
- fake emitter task is defined;
- source map expectations are documented;
- no Rust/C FFI is assumed;
- Oxc introduction has clear tests and acceptance criteria.

## Open Questions

- Exact project config filename.
- Exact CLI command layout.
- Whether `sloppyc` invokes `tsgo`/`tsc` as subprocess or library later.
- Exact JavaScript module format and bundling strategy.

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

The current compiler skeleton does not:

- parse TypeScript;
- depend on Oxc;
- emit JavaScript;
- emit source maps;
- emit a Sloppy Plan;
- call into C runtime code;
- expose Rust/C FFI.

## Current Phase

`compiler/` is a Rust CLI placeholder. It prints help/version and has small argument tests.
Dependencies are intentionally empty.

## Future Phase

The compiler grows in slices:

1. fake plan emitter;
2. one-route extractor;
3. source map and JS emission;
4. module extraction;
5. data provider extraction;
6. official TypeScript checking integration.

## Public API Shape

Current placeholder:

```powershell
sloppyc --help
sloppyc --version
```

Future public commands are expected to include build/check-oriented flows, but exact syntax
is deferred.

## Why Rust

Rust is a good fit for compiler tooling because it provides strong package ecosystem support,
memory safety for complex graph processing, and direct access to Oxc. The runtime remains C.

## Why Oxc

Oxc is planned as the primary parser, transformer, and app-plan extraction substrate. It is
not added yet because the foundation phase should keep the compiler build small and avoid
pretending extraction exists before its tests and schemas exist.

## No Rust/C FFI For Now

The compiler/runtime boundary is artifacts:

- `app.js`;
- `app.js.map`;
- `app.plan.json`.

No Rust/C FFI is planned for early phases. This keeps process boundaries clear and makes
compiler output testable with golden files.

## Compiler Phases

Target phases:

1. project discovery;
2. parse TypeScript;
3. resolve imports;
4. transform TypeScript to JavaScript;
5. extract app graph metadata;
6. validate static plan restrictions;
7. run official TypeScript checker later;
8. emit artifacts;
9. produce diagnostics.

## App Graph Extraction

The compiler extracts:

- modules;
- routes;
- route groups;
- handlers;
- services;
- middleware;
- endpoint filters;
- result filters;
- capabilities;
- permissions;
- data provider registrations;
- source locations.

Extraction must be deterministic. Import order must not silently decide module ordering.

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

Oxc handles parsing/transform/extraction. Official type checking through `tsgo` or `tsc` is
planned later. Sloppy should not claim TypeScript type compatibility from its extractor
alone.

## Source Map Requirements

Compiler output must include source maps once JavaScript emission exists. Source maps must
support:

- runtime exception diagnostics;
- plan extraction diagnostics;
- generated handler mapping;
- user-facing names and source spans;
- golden tests for stable mapping behavior.

## Public CLI Shape

Placeholder today:

```powershell
cargo run --manifest-path compiler/Cargo.toml -- --help
cargo run --manifest-path compiler/Cargo.toml -- --version
```

Future commands:

- `sloppyc build`;
- `sloppyc check`;
- `sloppyc emit-plan`;
- `sloppyc explain`.

See also `docs/execution-model.md`. Dev and production paths must use the same artifact
architecture: `app.js`, `app.js.map`, and `app.plan.json`. `sloppy run` may add caching and
watching, but it must not invent a separate runtime-only discovery model.

The bootstrap stdlib source layout now lives under `stdlib/sloppy/` and is staged for
runtime/package use under `lib/sloppy/bootstrap/sloppy/`. Compiler import rewriting from
the public `"sloppy"` specifier to those bootstrap modules is future work; the current
compiler placeholder does not read or emit stdlib assets.
`examples/hello/app.js` therefore uses a relative source import from
`stdlib/sloppy/index.js`; that example is not compiler input, compiler output, or proof of
bare import support.
The bootstrap stdlib now also contains the EPIC-14 JavaScript-only `Sloppy.module(...)`
and `builder.addModule(...)` skeleton. The compiler still does not extract modules, sort
module graphs, or emit module plan entries.
The bootstrap stdlib now also contains the EPIC-15 JavaScript-only data/capabilities
foundation: database capability metadata, query template lowering, fake providers, and
transaction callback semantics. The compiler still does not extract capabilities, data
provider registrations, or query template literals.
`examples/ergonomics/app.js` follows the same rule for the EPIC-13 route group, result
helper, and schema skeleton API shape. The compiler still does not extract route groups,
schemas, services, or any `app.plan.json` metadata from these examples.

## Internal Architecture

Planned Rust modules:

```text
compiler/src/
  main.rs
  cli.rs
  project.rs
  parser.rs
  resolver.rs
  transform.rs
  extract/
  plan/
  diagnostics/
  emit/
  golden/
```

Do not create these modules until their first tests exist.

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
handler id/exportName/displayName entries. The compiler placeholder does not emit those
artifacts yet.

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

Golden tests should cover:

- fake plan output;
- one-route extraction;
- generated handler IDs;
- diagnostics;
- source map snippets;
- module ordering;
- data provider plan contribution.

Golden files are public contract tests. Updating them requires review.

## Implementation Phases

### Phase A: Placeholder

Current state. CLI help/version and unit tests only.

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

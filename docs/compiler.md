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

The current compiler pipeline does not:

- perform full TypeScript type checking;
- bundle arbitrary module graphs beyond the supported Sloppy-relative subset;
- resolve npm packages or implement package-manager behavior;
- assume Node compatibility;
- execute application JavaScript during compilation;
- extract services, middleware, validation schemas, arbitrary provider module graphs, or
  module systems beyond supported function modules;
- lower database query templates or open native providers;
- implement `app.run`;
- implement Node/npm package resolution, arbitrary import graphs, or runtime module loading;
- expose Rust/C FFI.

For the exact supported and rejected source shapes, see
[`docs/compiler-supported-syntax.md`](compiler-supported-syntax.md).

## Current Phase

`compiler/` contains the ENGINE-02 compiler/Plan pipeline. `sloppyc build <input.js>
--out <directory>` parses one JavaScript source file with Oxc, extracts the supported
static Sloppy app shape, and emits deterministic artifacts:

- `app.plan.json`;
- `app.js`;
- `app.js.map`.

`examples/compiler-hello/app.js` remains the canonical artifact-path verification fixture.
Repeated builds of that source must produce byte-identical `app.plan.json`, `app.js`, and
`app.js.map` output with stable handler IDs and without absolute local paths, timestamps,
random IDs, or checkout-specific path text.
MAIN1-02 replaces placeholder artifact hashes with deterministic `sha256:` hashes. The
compiler computes those hashes from the emitted `app.js` and `app.js.map` bytes before
writing `app.plan.json`, preserving relative artifact paths and repeated-build identity.
ENGINE-02 broadens the extracted metadata to include GET/POST/PUT/PATCH/DELETE route
methods, direct async handler metadata, minimal SQLite database capability/provider plan
metadata, source locations, feature flags, and real handler-source source-map mappings.
FRAMEWORK-01.B loads built-in defaults, optional `appsettings.json`, optional
`appsettings.{Environment}.json`, canonical `SLOPPY_...` environment variables, and
selected CLI overrides before writing artifacts. The compiler binds
`app.use(sqlite("name"))` to `Sloppy:Providers:sqlite:name`, emits the resolved SQLite
database into Plan provider metadata, and emits redacted configuration metadata for tooling.
COMPILER-30.H/I now emits strong Plan metadata for the supported subset: source-file
hashes, function-module summaries, route and whole-plan completeness, response/binding/
effect facts, provider-kind-aware capability metadata, and compatibility evidence. This
metadata is compiler/tooling input; it does not add package-manager behavior, arbitrary
TypeScript inference, or new provider runtime bridges.

Compiler diagnostics render a single-line source frame when the extractor already has a
source span and source text. That renderer is separate from the generated source-map
artifact; the map now records handler-source mappings, and ENGINE-15.B consumes those
Source Map v3 mappings for V8 exception primary spans when the runtime has a validated
`app.js.map`.

ENGINE-02.E adds the direct run handoff on top of this same compiler pipeline:
`sloppy run <source>` invokes `sloppyc build`, writes generated artifacts, then enters the
same runtime loader used by `sloppy run --artifacts <dir>`. The runtime does not discover
apps from source and no second compiler exists in C. The CLI hands the compiler path and
arguments to the platform process runner directly rather than through a shell command
string. The first slice is rebuild-always and fail-closed; cache key requirements remain
documented until reuse is implemented.

## Future Phase

The compiler grows from this pipeline in later slices:

1. richer route and app-host metadata;
2. async stack/source-frame diagnostics that build on the V8 exception source-map bridge,
   while keeping dynamic route strings out of the supported subset;
3. module extraction;
4. broader data provider extraction;
5. V8 bootstrap module loading handoff;
6. official TypeScript checking integration.

COMPILER-30 (#460) is the next compiler-owned roadmap for that growth. Its source docs are
`docs/project/compiler-inference-engine-architecture.md` and
`docs/project/compiler-inference-issue-index.md`. COMPILER-30 frames the target as deep
static inference for the supported Slop app subset, not arbitrary TypeScript inference.
It owns module graph, symbol binding, Slop DSL recognition, route/group/module extraction,
provider/config/schema/result extraction, function effect summaries, callgraph inference,
capability inference, Plan completeness, source locations, diagnostics, and compiler
goldens. ENGINE-20 / Strong Plan remains the consumer layer for typed Plan use, doctor,
audit, OpenAPI, optimization hooks, and versioning policy.

Manual route-level `uses` metadata is a fallback escape hatch, not the normal workflow.
COMPILER-30 should infer provider/capability effects through direct calls, local helpers,
relative imported helpers, closure-captured provider handles, repository/factory functions,
object-literal methods, and simple class/service instances when statically resolvable.

## Public API Shape

Current compiler command:

```powershell
sloppyc --help
sloppyc --version
sloppyc build examples/compiler-hello/app.js --out .sloppy
sloppyc build app.js --out .sloppy --environment Development --host 127.0.0.1 --port 5173
```

The build command writes only inside the requested output directory. It creates the output
directory when needed and rejects output paths containing `..`.

## Why Rust

Rust is a good fit for compiler tooling because it provides strong package ecosystem support,
memory safety for complex graph processing, and direct access to Oxc. The runtime remains C.

## Why Oxc

Oxc is the parser for the compiler pipeline. The compiler uses Oxc for syntax parsing and
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

Current phases:

1. parse one input file;
2. validate the supported import and app factory shape;
3. extract literal route declarations and handlers;
4. assign stable numeric handler IDs in source order;
5. rewrite the public `"sloppy"` import into the internal bootstrap runtime handoff;
6. emit `app.plan.json`, `app.js`, and `app.js.map`;
7. hash the emitted app and source-map artifacts into the plan;
8. produce structured compiler diagnostics for unsupported syntax.

COMPILER-30 target phases are broader and explicit:

1. lex/parse source;
2. resolve supported imports;
3. build module graph;
4. bind symbols;
5. identify Slop DSL roots;
6. evaluate supported constants;
7. extract routes/groups/modules/providers/config/schema/results;
8. build function callgraph;
9. compute effect summaries;
10. infer provider/capability/config/body/response metadata;
11. compute Plan completeness;
12. validate graph, including missing provider registrations for inferred effects;
13. emit Plan, bundle, source maps, diagnostics, and goldens.

## App Graph Extraction

The compiler extracts:

- one app object from `Sloppy.create()` or `Sloppy.createBuilder()` plus `builder.build()`;
- literal Minimal API `app.get`, `app.post`, `app.put`, `app.patch`, and `app.delete`
  routes, with `mapGet/mapPost/mapPut/mapPatch/mapDelete` retained for compatibility;
- simple `const group = app.group(prefix)` / `app.mapGroup(prefix)` declarations followed
  by grouped route calls for the same supported method set;
- nested literal route groups derived from the app or another known group;
- function modules passed to `app.useModule(...)` from named relative exports, including
  direct module-app routes and routes on nested groups created inside the module;
- optional `.withName("Route.Name")` route names;
- sync handlers and direct async handlers whose body is an inline function/arrow
  expression returning `Results.text(...)`, `Results.json(...)`, `Results.ok(...)`, or
  `Results.noContent()`;
- zero-argument handlers or one-argument context handlers whose single parameter is a
  simple identifier;
- result arguments that are inline JSON-safe literals, arrays, object literals, simple
  request-context property reads such as `ctx.route.id` and `ctx.query.q`, or supported
  body binding helpers such as `ctx.body.json(SchemaName)`;
- source ranges for copied handler bodies;
- `builder.capabilities.addDatabase(token, { provider: "sqlite", access })` and
  `app.use(sqlite("name", options?))` as metadata-only Plan `dataProviders` and database
  `capabilities` entries;
- `schema.object/string/int/number/bool/array` declarations, `app.config.get*` reads,
  request binding helpers, and preliminary `Results.*` response metadata for the supported
  handler subset.
- provider effect metadata with explicit `capabilityKind` and `providerKind`, so the
  compiler is not hard-coded to SQLite or to database-only future providers;
- Plan completeness: `complete` when required route/provider/body/response facts are
  visible, `partial` when optional metadata such as response/body schema is incomplete,
  and invalid diagnostics before artifact emission when runtime-required truth is missing.

Extraction must be deterministic. Import order must not silently decide module ordering.

Unsupported input fails with diagnostics. The compiler accepts only
`import { Sloppy, Results } from "sloppy";` plus optional unaliased `data`, `schema`,
`sloppy/time`, `sloppy/fs`, `sloppy/crypto`, and `sloppy/codec` named imports as public import syntax and rejects arbitrary
bare imports such as `"express"`, `"fs"`, and `"node:fs"` with
`SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER`. The compiler does not implement Node package
resolution, npm resolution, import maps, dynamic imports, arbitrary module graphs, or
package-manager behavior. It resolves only source-local relative function modules in the
documented subset. It also rejects dynamic route strings, computed method names,
multiple app objects, missing default export, unsupported handler shapes, handlers with
more than one parameter, destructured/default/rest handler parameters, handlers that close
over source-file bindings, TypeScript-only handler syntax, broad module
graphs beyond the function-module subset, top-level control flow, HEAD/OPTIONS route declarations, async handler bodies that
do more than directly return a supported `Results.*` descriptor, unsupported provider
metadata, duplicate capability tokens, and secret-bearing provider/capability fields.

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

The current compiler pipeline is JavaScript-input-only. Oxc is used for parsing and narrow
AST extraction, but the compiler does not lower TypeScript syntax into generated
JavaScript.
Official type checking through `tsgo` or `tsc` is planned later. Sloppy should not claim
TypeScript type compatibility from its extractor alone.

## Source Map Requirements

ENGINE-15.A completes the compiler-owned map artifact for the currently supported source
subset. `app.js.map` remains deterministic Source Map v3 output with `sources`,
`sourcesContent`, and generated handler assignment mappings, and now also carries a
Sloppy-owned `x_sloppy` metadata block. That block records source-file hashes, handler
generated/source positions, route/module/schema/provider/capability/effect source
locations where the compiler has them, and keeps multi-file function-module sources in the
same artifact. `app.plan.json` continues to record deterministic `sha256:` hashes for both
`app.js` and `app.js.map`.

This remains compiler-artifact evidence. ENGINE-15.B consumes the generated Source Map v3
mapping table in the V8-gated runtime path, but this does not add TypeScript lowering,
arbitrary bundler source maps, Node/npm resolution, or broader module graph support.

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
loads those artifacts from `--artifacts <dir>`. ENGINE-02.E source-input run invokes
`sloppyc build` first, validates the emitted artifacts through the existing runtime loader,
and keeps watching/hot reload plus cache reuse as future work. Runtime code must not invent
a separate discovery model.

The bootstrap stdlib source layout now lives under `stdlib/sloppy/` and is staged for
runtime/package use under `lib/sloppy/bootstrap/sloppy/`. The compiler recognizes only the
public bare import `import { Sloppy, Results } from "sloppy";` plus optional unaliased
`data`, `schema`, `sloppy/time`, `sloppy/fs`, `sloppy/crypto`, and `sloppy/codec` imports as input syntax. EPIC-24
rewrites that import away in generated `app.js`: the artifact reads `Results` from
`globalThis.__sloppy_runtime`, which is installed by the runtime-loaded bootstrap asset,
assigns each generated handler to its legacy `globalThis.__sloppy_handler_<id>` export name,
and registers that same function through `__sloppy_register_handler(handlerId, handler)`.
The legacy global keeps the no-context runtime-contract ABI explicit while the registered
handler table is the EPIC-24 dispatch path. The compiler does not load stdlib assets itself
and does not imply Node or npm compatibility.
`examples/hello/app.js` therefore uses a relative source import from
`stdlib/sloppy/index.js`; that example remains a bootstrap API-shape example. The
compiler-owned runnable artifact example is `examples/compiler-hello/`.
The bootstrap stdlib now also contains the EPIC-14 JavaScript-only `Sloppy.module(...)`
and `builder.addModule(...)` skeleton. The compiler still does not sort general
package/module graphs or execute module loaders. COMPILER-30.D/H/I does extract the
supported function-module subset and emits route `module` attribution plus top-level
`modules[]` summaries for Strong Plan consumers.
The bootstrap stdlib now also contains the EPIC-15 JavaScript-only data/capabilities
foundation: database capability metadata, query template lowering, fake providers, and
transaction callback semantics. ENGINE-02 extracts only the minimal
`builder.capabilities.addDatabase(...)` SQLite metadata shape into Plan `dataProviders` and
database `capabilities`. COMPILER-30.E also extracts supported `app.use(sqlite(...))`
provider registrations, `app.provider("sqlite:name")` handle lookups, config key reads,
schema declarations, request bindings, and preliminary response metadata. COMPILER-30.F/G
adds the first effect summaries and inferred capability access for direct database provider
calls and same-file helpers that close over static provider handles. The effect model is
not SQLite-only: it records capability kind and provider kind, and can represent
PostgreSQL/SQL Server database metadata even though only the SQLite generated runtime
opener is executable today. PostgreSQL/SQL Server provider-backed generated handlers are
rejected until those JS bridges exist. It still does not extract service lifetimes, fake
providers, query template literals, non-database provider adapters, imported helper
effects, repository/object/class service graphs, or arbitrary TypeScript callgraphs.
EPIC-16 adds native SQLite provider tests and the `data.sqlite` stdlib shape, but the
compiler still does not extract SQLite modules, open native providers, or lower application
template literals into native provider calls.
EPIC-17 and EPIC-18 add native PostgreSQL and SQL Server provider tests plus
`data.postgres` and `data.sqlserver` stdlib shapes, but the compiler still does not extract
provider modules for those providers, lower application template literals into native
provider calls, or produce metadata for live provider checks.
EPIC-19 CLI introspection reads interim plan-compatible metadata sections from
fixtures/artifacts. The compiler now emits the native-validated Plan v1 alpha `routes`,
`dataProviders`, and `capabilities` sections in `app.plan.json` for CLI tooling and
dev-only GET dispatch metadata. EPIC-20 benchmarks measure current native foundations only
and do not imply compiler output performance.
`examples/ergonomics/app.js` follows the same static-example rule for the broader EPIC-13
route group, result helper, and schema skeleton API shape. The compiler now extracts the
COMPILER-30.E schema/config/request/result metadata covered by compiler fixtures, but it
still does not extract services, middleware, filters, broad provider graphs, or provider
effects from those broader examples.

## Internal Architecture

Current Rust layout after COMPILER-30.B/C:

```text
compiler/src/
  main.rs
  lib.rs
  sloppyc.rs
  diagnostic.rs
  source.rs
  parser.rs
  module_graph.rs
  resolver.rs
  symbols.rs
  slop_dsl.rs
  static_eval.rs
  graph.rs
  effects.rs
  capability_inference.rs
  schema_inference.rs
  result_inference.rs
  validation.rs
  plan_emit.rs
  bundle_emit.rs
  source_map.rs
  fixtures.rs
compiler/tests/fixtures/
compiler/tests/compiler_fixture_harness.rs
```

`main.rs` is the thin CLI entrypoint. `lib.rs` exposes the compiler library API used by
tests and future tooling. `sloppyc.rs` still owns artifact extraction and emission, but
COMPILER-30.B/C moves the first parser/resolver/symbol/DSL/static-eval contracts behind
focused module APIs:

- `parser.rs` owns source-type acceptance diagnostics for entry and imported modules;
- `resolver.rs` classifies supported Slop imports and resolves source-local relative
  imports without Node/npm behavior;
- `module_graph.rs` owns deterministic source module graph bookkeeping primitives;
- `symbols.rs` owns Slop symbol binding primitives for app/group/provider/schema/helper
  identifiers;
- `slop_dsl.rs` owns route-method, member-chain, string-argument, provider, Results, and
  context-helper recognition primitives;
- `static_eval.rs` owns supported static string literal, const-alias, and concatenation
  evaluation.

Route graph extraction, provider/config/schema/results metadata extraction, effects,
capabilities, completeness, and Strong Plan emission are now implemented for the supported
COMPILER-30.B through COMPILER-30.H/I subset in `sloppyc.rs`, with focused helper modules
owning parser/resolver/symbol/DSL/static-eval foundations. Remaining module splits should
move behavior behind the listed focused modules without changing the documented artifact
contract.

The current library entrypoints are:

- `compile_file(input, out_dir, options)`;
- `compile_project(input, out_dir, options)`;
- `CompileOptions`;
- `CompileOutput` with Plan, bundle, and source-map artifact contents;
- structured compiler `Diagnostic` with code, severity, message, path, source span, and
  optional hint.

The CLI calls the same library API instead of owning compiler behavior directly.

COMPILER-30 changes that threshold. The compiler should be split into focused modules such
as `diagnostic.rs`, `source.rs`, `syntax.rs` or `ast.rs`, `parser.rs`,
`module_graph.rs`, `resolver.rs`, `symbols.rs`, `slop_dsl.rs`, `static_eval.rs`,
`graph.rs`, `effects.rs`, `capability_inference.rs`, `schema_inference.rs`,
`result_inference.rs`, `plan_emit.rs`, `bundle_emit.rs`, `source_map.rs`,
`validation.rs`, and fixture helpers where needed. Avoid a broad `utils` dumping ground;
each module should have an explicit responsibility and tests.

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
handler id/exportName/displayName entries. MAIN1-02 makes the compiler emit those fields
with deterministic `sha256:` artifact hashes plus the native-validated Plan v1 alpha
`routes` section. ENGINE-02 adds source metadata, handler async flags, route method
metadata for GET/POST/PUT/PATCH/DELETE, feature flags, and minimal SQLite
provider/capability Plan entries for supported `builder.capabilities.addDatabase(...)`
declarations.

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
- `results-json`;
- `function-handler`;
- `http-methods`;
- `async-handler`;
- `provider-capability`;
- `source-map`;
- unsupported dynamic route diagnostics;
- unsupported computed route method diagnostics;
- unsupported async handler body diagnostics;
- unsupported secret-bearing capability metadata diagnostics;
- loop and conditional route registration diagnostics;
- arbitrary bare import and Node import diagnostics;
- missing app/default export diagnostics;
- multiple app diagnostics;
- deterministic generated `app.plan.json`, `app.js`, and `app.js.map`.
- the COMPILER-30.A library API and CLI fixture harness for current hello artifacts,
  invalid-input diagnostics, source-map golden output, and staged generated-artifact
  hygiene.
- COMPILER-30.J fixture coverage for realistic supported apps, partial completeness,
  provider-kind database metadata, zero-route function-module source graph entries, and
  invalid provider/effect shapes.

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

Extract one literal `app.mapGet` route from a tiny JavaScript file.

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

### Phase G: Broad Data Provider Extraction

Extract provider module registrations beyond the ENGINE-02 SQLite metadata shape.

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

### Phase I: Runtime Source-Map Diagnostics

Consume compiler source maps in runtime diagnostics.

Acceptance:

- runtime exception fixture maps generated location to TypeScript source;
- golden source map fragment is stable.

### Phase J: Cache

Compute cache keys for dev/build artifacts.

Acceptance:

- source, compiler, Oxc, target platform, target engine, and stdlib inputs affect key;
- stale cache is rejected by plan/bundle consistency checks.

ENGINE-02.E deliberately does not claim cache reuse. `sloppy run <source>` rebuilds into a
deterministic development output directory, and `sloppy run` with `sloppy.json` rebuilds
into `outDir`. Cache reuse remains blocked on a complete key that includes source/import
hashes, compiler/runtime/stdlib identity, target engine/platform, environment, and relevant
feature/options.

## Error And Diagnostic Behavior

Compiler diagnostics must include:

- stable code;
- severity;
- TypeScript source span;
- optional related locations;
- suggested fix where safe;
- generated artifact context when relevant.

For current `sloppyc` fixture failures, diagnostics with source spans include a
deterministic single-line source frame. Diagnostics without spans still render the stable
path/summary fallback. Richer spans and related compiler locations remain future work.
ENGINE-15.A source maps carry real handler mappings plus stable Sloppy metadata, and
ENGINE-15.B consumes the Source Map v3 mapping table for V8 exception primary spans.

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
## ENGINE-14 Module Loading

ENGINE-14 keeps module loading compiler-owned. Source may use static ESM-style imports for
the supported subset: `"sloppy"`, `"sloppy/providers/sqlite"`, and relative app modules
under the source root. `sloppyc` resolves that graph before runtime startup, rejects
unsupported bare imports and dynamic `import(...)`, and emits the existing classic
`app.js` artifact plus `app.plan.json` and `app.js.map`. The runtime still evaluates the
generated classic artifact through the bootstrap path; it does not implement V8 native ESM,
Node resolution, `node_modules`, `package.json`, or package-manager behavior.

Function modules are supported as named relative exports passed to `app.useModule(...)`.
Inside a function module, the compiler recognizes `app.provider("sqlite:<name>")`,
`app.group(...)`, direct `app.get/post/put/patch/delete(...)`, and literal
`group.get/post/put/patch/delete(...)` registrations on groups derived from the app
parameter. Nested literal module groups compose before route validation. Contributed
routes, provider metadata, capability metadata, module attribution, and module source
files remain Plan/source-map visible.

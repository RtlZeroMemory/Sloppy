# Compiler Inference Engine Architecture

Status: source-of-truth plan for COMPILER-30 (#460). This document does not implement
compiler/runtime/framework features.

## Purpose

COMPILER-30 turns `sloppyc` from a narrow artifact extractor into the Slop application
compiler inference engine. The target is deep static inference for the supported Slop app
subset, not arbitrary TypeScript inference.

The compiler should understand the Slop framework DSL well enough to infer runtime-required
truth for normal Slop applications and to explain clearly when source falls outside that
subset. Runtime-required truth must be Plan-visible. Developer business logic can remain
flexible, but route/provider/capability/config/body/response facts that affect runtime,
doctor, audit, OpenAPI, or startup validation cannot live only in JavaScript control flow.

## Supported App Subset

The supported subset is deliberately Slop-owned:

- static route paths;
- Minimal API route calls: `app.get/post/put/patch/delete`;
- current compatibility route calls such as `mapGet/mapPost/...` while migration is in
  progress;
- route groups with statically known prefixes;
- function modules first, imported from relative modules;
- relative imports resolved by the compiler;
- Slop stdlib imports from `"sloppy"`;
- Slop provider imports such as `"sloppy/providers/sqlite"`;
- provider registration/import descriptors;
- schema declarations using the supported schema DSL;
- config key reads and `bind` calls;
- explicit `Results.*` helpers;
- request binding helpers for route/query/header/body data;
- direct provider calls in handlers;
- local helper functions;
- imported helper functions from relative modules;
- effect summaries for supported function calls.

The compiler must reject or require explicit metadata for:

- dynamic route paths;
- computed route methods;
- unknown bare imports;
- npm/node_modules imports;
- Node builtins and Node compatibility assumptions;
- dynamic imports;
- unknown runtime route generation;
- unresolvable provider usage where capability truth is required;
- arbitrary TypeScript analysis outside the supported Slop app subset.

## Pipeline

Target phases:

1. Lex/parse source.
2. Resolve supported imports.
3. Build module graph.
4. Bind symbols.
5. Identify Slop DSL roots.
6. Evaluate supported constants.
7. Extract routes/groups/modules/providers/config/schema/results.
8. Build function callgraph.
9. Compute effect summaries.
10. Infer provider/capability/config/body/response metadata.
11. Compute Plan completeness.
12. Validate graph.
13. Emit Plan, bundle, source maps, diagnostics, and goldens.

Every phase should produce deterministic data structures and diagnostics. Later phases must
not guess around earlier uncertainty; they either consume proven facts, mark optional
metadata partial, require an escape hatch, or fail invalid runtime contracts.

## Module Graph

The module graph is source-level compiler state, not runtime module loading. It includes:

- entry module;
- resolved relative modules;
- Slop stdlib/provider imports;
- exported function modules and imported helper functions;
- import/export source locations;
- unsupported import diagnostics.

Relative imports resolve before runtime startup. Unknown bare imports, npm/package-manager
resolution, remote imports, and dynamic imports fail unless a future scoped issue changes
the contract.

Cycles are invalid when they prevent deterministic route/provider/effect extraction. If a
later task allows harmless helper cycles, the cycle rule must be documented and tested
before acceptance.

## Symbol Binding

Symbol binding connects source identifiers to Slop concepts:

- app builders and app instances;
- route groups;
- provider descriptors and handles;
- config objects and bound config shapes;
- schema values;
- `Results` helpers;
- request context binding roots;
- local and imported functions.

Binding must preserve source locations for diagnostics and Plan metadata. Aliases are only
supported when a task explicitly adds and tests them. Unknown or ambiguous symbols in
runtime-required positions must not silently become best-effort Plan output.

## Slop DSL Recognition

DSL recognition is explicit and framework-aware. The compiler recognizes supported calls
because they are Slop API shapes, not because they happen to look like arbitrary JavaScript.

Required roots include:

- `Sloppy.create()` and current builder compatibility shapes;
- route calls on apps and groups;
- `app.group(...)`;
- `app.use(...)` and provider imports;
- `app.useModule(...)` or the supported function-module registration shape;
- `app.provider("sqlite:name")` inside function modules;
- schema declarations;
- `Results.json/text/created/noContent/badRequest/notFound/problem`;
- request binding helpers for route/query/header/body;
- config `get*` and `bind` helpers.

Unsupported DSL-like calls should produce stable diagnostics that tell the developer which
supported Slop shape to use.

## Extraction

Route, group, and module extraction emits a source-ordered graph:

- HTTP method;
- route pattern;
- route group prefix;
- handler identity;
- route name/tags/metadata where supported;
- function module attribution;
- source locations;
- duplicate route diagnostics.

Provider/config/schema/results extraction emits metadata:

- provider registrations and imported provider descriptors;
- provider tokens and capability tokens;
- config keys used, bound, or required;
- schema declaration summaries;
- request body/query/route/header binding metadata;
- response kind/schema where inferable from `Results.*`;
- partial metadata markers where behavior can run but optional tooling metadata is
  incomplete.

## Function Effect Summaries

Function summaries describe what a handler or helper does in the supported subset:

- provider reads, writes, or readwrite use;
- config keys used;
- body schema usage;
- response kind and schema if inferable;
- diagnostics or throws if relevant;
- unknown calls;
- completeness impact.

Initial inference must cover:

- direct handler provider calls;
- local helper calls;
- imported relative helper calls;
- closure-captured provider handles;
- repository/factory functions where the provider handle and returned callable/object are
  statically resolvable;
- object-literal methods where the object and method call target are statically
  resolvable;
- simple class instances where constructor/provider capture and method calls are
  statically resolvable;
- provider wrapper annotations if a later task selects that escape hatch.

Unknown calls are not ignored. They either have no runtime-required effect, are covered by
explicit metadata, mark optional metadata partial, or make the route invalid when
capability/provider truth is required.

## Callgraph Inference

The callgraph links handlers to local and relative-import helper functions. It should be
small, deterministic, and source-located:

- direct calls to known functions are traversed;
- imported relative functions are traversed through the module graph;
- recursion and cycles produce diagnostics until a scoped policy supports them;
- unsupported dynamic calls affect completeness.

The callgraph is a compiler analysis artifact. It is not a runtime invocation graph and
must not require executing application code.

## Capability Inference

Capability inference tiers:

1. Direct provider method calls.
2. Local callgraph effects.
3. Imported local callgraph effects.
4. Statically resolvable repository/factory/object-literal/class helper patterns.
5. Provider wrappers such as `db.reads` / `db.writes` if selected later.
6. Explicit route metadata such as `uses`, `body`, or `response` as a fallback escape
   hatch for dynamic/plugin/runtime-only patterns.
7. Advanced manual capability policy.

SQLite method defaults:

- `query` and `queryOne` imply read;
- `exec` implies write unless a later SQL classifier proves a narrower safe case;
- `transaction` implies readwrite unless a later scoped policy proves a narrower summary.

No silent unsound inference is allowed. The compiler must attempt deep static effect
inference through normal Minimal API, function-module, repository, and service-style
patterns before asking developers for hints. If a route might use a provider in a way the
compiler cannot prove, runtime-required capability truth must come from explicit fallback
metadata or the route is invalid.

## Escape Hatches

Escape hatches are fallback metadata, not normal workflow and not permission to hide
runtime truth:

- route-level `uses` metadata for capabilities/providers only when dynamic, plugin, or
  runtime-only behavior cannot be statically proven;
- explicit body/response metadata for custom validation/serialization;
- provider wrapper annotations if a later task introduces them;
- advanced manual capability policy for unusual applications.

Escape hatches must be Plan-visible, source-located, and inspectable by doctor/audit. They
must be rare in normal Slop apps; compiler-inferred capabilities are the default path.
Normal Minimal API, function-module, repository, and service patterns must not require
manual `uses` metadata when their effects are statically resolvable.

## Plan Completeness

Completeness states:

- `complete`: route/body/provider/capability/config/response metadata is Plan-visible.
- `partial`: app can run, but optional metadata such as response shape/OpenAPI is
  incomplete.
- `runtime-only`: developer explicitly opts into runtime-only behavior with required
  capability/provider metadata.
- `invalid`: runtime-required truth is missing.

Invalid examples:

- unknown route path;
- unknown provider registration;
- unresolvable provider/capability usage where enforcement needs truth;
- missing required body metadata for a declared validation path;
- dynamic route generation in the compiler-owned static path.

`runtime-only` is allowed only when explicitly marked and when required provider/capability
metadata is still Plan-visible. It is not a silent fallback.

## Diagnostics

Diagnostics must be deterministic, source-located, and useful:

- explain unsupported source shape;
- distinguish invalid runtime contract from partial optional metadata;
- show what was inferred;
- show why inference stopped;
- point to escape hatches when applicable;
- avoid implying arbitrary TypeScript support.

Doctor and Plan consumers should be able to explain route graph, capability inference,
config requirements, provider usage, validation coverage, response metadata, partial or
unknown routes, startup validation, OpenAPI foundations, and future optimization inputs.

## Test Strategy

COMPILER-30 needs layered tests:

- module-level Rust tests for resolver, symbol binding, DSL recognition, static eval,
  effects, capability inference, schema/result inference, Plan completeness, and validation;
- fixture/golden tests for supported artifacts and diagnostics;
- invalid fixtures for dynamic routes, unsupported imports, capability ambiguity, recursion,
  missing providers, and runtime-only misuse;
- realistic app fixtures for direct handlers, function modules, helper functions, imported
  helpers, repository/factory functions, object-literal methods, simple class instances,
  service-style patterns where statically resolvable, custom validation partial metadata,
  custom response partial metadata, config, schemas, and provider use;
- compatibility goldens for hello, users, modules, config, validation, and Plan versioning.

Tests must verify documented intent, not accidental implementation behavior. Generated
fixtures are allowed only when they are intentional goldens.

## Rust Module Architecture

The compiler must not remain one giant god file. Target modules under `compiler/src`:

- `diagnostic.rs`: diagnostic codes, severities, source ranges, renderable messages.
- `source.rs`: source file identity, text, spans, line/column mapping.
- `syntax.rs` or `ast.rs`: thin parsed-source wrappers over Oxc types.
- `parser.rs`: parse entry points and parse diagnostics.
- `module_graph.rs`: entry/relative module graph construction and cycle policy.
- `resolver.rs`: supported import resolution.
- `symbols.rs`: symbol tables and binding results.
- `slop_dsl.rs`: Slop DSL call recognition.
- `static_eval.rs`: supported constants and literal route/config/schema evaluation.
- `graph.rs`: extracted app graph, routes, groups, modules, handlers.
- `effects.rs`: function summaries and callgraph effect propagation.
- `capability_inference.rs`: provider effect to capability metadata.
- `schema_inference.rs`: schema DSL summaries and request-body metadata.
- `result_inference.rs`: `Results.*` response metadata.
- `plan_emit.rs`: Plan JSON model and emission.
- `bundle_emit.rs`: generated JavaScript bundle emission.
- `source_map.rs`: source-map model and emission.
- `validation.rs`: graph validation and completeness computation.
- `fixtures.rs`: test fixture helpers if they are shared by compiler tests.

Module APIs should be explicit and testable. Avoid broad `utils` dumping grounds. Shared
helpers need a named domain owner.

COMPILER-30.B/C adds the first implementation behind the parser/resolver/symbol/DSL/static
evaluation boundaries without widening into arbitrary TypeScript or package-manager
resolution. `parser.rs` owns entry/module source-type diagnostics, `resolver.rs` owns
source-local relative and Slop-owned import classification, `module_graph.rs` owns
deterministic source graph bookkeeping, `symbols.rs` owns Slop binding primitives,
`slop_dsl.rs` owns explicit Slop DSL recognizers, and `static_eval.rs` owns supported
literal/alias string evaluation.

COMPILER-30.D moves route/group/function-module extraction from compatibility-only status
to the supported inference path: Minimal API route methods, nested literal group prefixes,
function modules imported from source-local files, direct module-app routes, module-group
routes, duplicate method/path validation, and Plan/source-map-visible route locations are
covered. Provider/config/schema/result metadata, effects, capability inference, and Plan
completeness remain future COMPILER-30 tasks.

The fixture harness keeps current behavior stable while preparing for inference:

- success fixtures compare `app.plan.json`, `app.js`, and `app.js.map`;
- rejection fixtures compare stable diagnostics;
- diagnostics fixtures require source-located errors when spans are available;
- source-map fixtures compare deterministic Source Map v3 output;
- multi-file fixtures keep fixture-local source modules under the fixture directory;
- future inference metadata may add expected metadata files only when a scoped task emits
  real metadata.

## Rust Standards

All Rust code must follow existing repo standards:

- rustfmt;
- clippy `-D warnings`;
- meaningful `Result` and error types;
- no `unwrap` / `expect` in production paths except impossible/internal invariants with a
  message and tests;
- no panics for user input;
- deterministic diagnostics;
- explicit ownership;
- small modules;
- focused tests;
- no generated artifacts committed except intentional fixtures/goldens.

## Non-goals

- Arbitrary TypeScript inference.
- Full TypeScript typechecking or lowering in this epic.
- Node/npm/package-manager compatibility.
- Runtime app discovery from source.
- Runtime feature implementation.
- Framework feature implementation outside compiler recognition/metadata.
- Controllers, decorators, full DI, middleware/filter execution, or package modules.
- Doctor/audit/OpenAPI/optimization consumer implementation; those remain ENGINE-20 Strong
  Plan work.

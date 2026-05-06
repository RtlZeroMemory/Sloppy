# Compiler Supported Syntax Matrix

Status: ENGINE-02 source of truth for the supported compiler input subset.

`sloppyc build` is intentionally narrow. It parses one JavaScript source file, extracts a
static Sloppy app shape, emits deterministic artifacts, and rejects unsupported dynamic
JavaScript instead of silently producing a partial plan.

The compiler does not implement a full TypeScript compiler, package resolution, bundling,
Node compatibility, middleware extraction, services/modules/data extraction, decorators,
or arbitrary route registration.

## Alpha Source Policy

Supported input is a single `.js` or `.mjs` file with:

- `import { Sloppy, Results } from "sloppy";`, with optional unaliased `data`;
- optional unaliased `Time`, `Deadline`, `CancellationController`, and Time error imports
  from `"sloppy/time"` for Plan-visible time feature activation;
- optional unaliased `File`, `Directory`, `Path`, `FileHandle`, and `FileWatcher` imports
  from `"sloppy/fs"` for Plan-visible filesystem feature activation;
- exactly one app from `Sloppy.create()` or `Sloppy.createBuilder()` plus `builder.build()`;
- literal top-level `app.mapGet`, `app.mapPost`, `app.mapPut`, `app.mapPatch`, or
  `app.mapDelete` calls, or a simple `app.mapGroup(...)` variable with literal grouped
  calls for the same supported method set;
- inline arrow/function handlers, including direct `async` handlers, that return the
  supported `Results.*` helpers;
- optional `builder.capabilities.addDatabase(...)` database capability metadata;
- `export default app;` for the extracted app variable.

Named handler functions are not supported yet. The current extractor copies inline handler
source slices into the generated classic artifact, so identifier handlers would need a
separate source-copy and closure policy before they can be accepted honestly.

## Source-Input Run Policy

The explicit two-step artifact workflow remains supported:

```powershell
sloppyc build app.js --out .sloppy
sloppy run --artifacts .sloppy
```

`sloppy run <source.js>` is implemented as a shortcut over the same compiler and artifact
runtime path. It invokes `sloppyc build`, writes generated artifacts to
`.sloppy/cache/dev/source-input`, validates the emitted plan, bundle, and source map, then
runs those artifacts exactly as `sloppy run --artifacts` would.

`sloppy run` with no source reads `sloppy.json` in the current directory:

```json
{
  "entry": "app.js",
  "outDir": ".sloppy",
  "environment": "Development"
}
```

`entry` is required. `outDir` defaults to `.sloppy`; `environment` defaults to
`Development`. Unsupported config fields fail clearly in this first project-run config
slice.

The source-input handoff follows the compiler's existing source support. JavaScript input
is supported when it matches the documented subset. `.ts` inputs are parsed only for the
supported Slop-owned subset and for source-located unsupported-TypeScript diagnostics;
`.tsx`, `.jsx`, `.cjs`, `.mts`, and `.cts` remain outside the supported compiler subset.
No full TypeScript typechecking, npm/package-manager behavior, `node_modules` resolution,
broad module graph bundling, watch mode, or hot reload is implemented.

## Matrix

| Shape | Classification | Behavior | Diagnostic expectation | Fixture/test expectation | Roadmap target |
| --- | --- | --- | --- | --- | --- |
| `import { Sloppy, Results } from "sloppy"` | Supported | Accepted as the core public compiler import and rewritten out of generated `app.js`. | None. | Covered by every supported compiler fixture. | Alpha compiler. |
| Optional `data` import from `"sloppy"` | Supported for metadata-bearing fixtures | Accepted only as unaliased `data`; generated `app.js` reads `data` from `globalThis.__sloppy_runtime`. | Aliases or unknown imports use `SLOPPYC_E_UNSUPPORTED_IMPORT`. | `provider-capability` golden fixture. | ENGINE-02 metadata support. |
| Optional `schema` import from `"sloppy"` | Supported for metadata-bearing fixtures | Accepted only as unaliased `schema` and used for static schema metadata extraction. It is not emitted into generated runtime code. | Aliases or unknown imports use `SLOPPYC_E_UNSUPPORTED_IMPORT`. | `metadata-extraction` golden fixture and COMPILER-30.E unit coverage. | COMPILER-30.E schema metadata. |
| Optional `Time`, `Deadline`, `CancellationController`, `TimeoutError`, `CancelledError`, `InvalidDeadlineError`, or `TimerDisposedError` imports from `"sloppy/time"` | Supported for feature metadata and current API contract binding | Accepted only as unaliased named imports. The compiler emits `requiredFeatures: ["stdlib.time"]`. CORE-TIME-01.C/D/G implements the V8-backed delay/timeout/deadline/cancellation subset; CORE-TIME-01.E/F/I adds interval, schedule, fake-clock, example, and conformance evidence in the stdlib/bootstrap lanes. | Aliases, side-effect imports, empty imports, or unknown names use `SLOPPYC_E_UNSUPPORTED_IMPORT`. | `sloppy_time_import_emits_plan_required_feature`, `examples.time.api_shape`, bootstrap Time coverage, and V8 Time smoke coverage. | CORE-TIME-01 Time API. |
| Optional `File`, `Directory`, `Path`, `FileHandle`, or `FileWatcher` imports from `"sloppy/fs"` | Supported for feature metadata and current stdlib binding | Accepted only as unaliased named imports. The compiler emits `requiredFeatures: ["stdlib.fs"]` and binds the current stdlib FS namespace. `FileHandle` is implemented for CORE-FS-01.E/F, and `FileWatcher` is implemented for CORE-FS-01.G as a bounded non-recursive watch helper. | Aliases or unknown names use `SLOPPYC_E_UNSUPPORTED_IMPORT`. | `sloppy_fs_import_emits_plan_required_feature` unit coverage plus CORE-FS-01.I/J source examples. | CORE-FS-01 first filesystem layer. |
| Aliased `Sloppy` or `Results` imports | Rejected with diagnostic | Rejected because extractor matching is explicit. | `SLOPPYC_E_UNSUPPORTED_IMPORT`. | `unsupported-import-alias` diagnostic fixture. | Deferred until a scoped ergonomics task needs aliases. |
| Arbitrary bare imports such as `"express"` | Rejected with diagnostic | Rejected; no package or module resolution runs. | `SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER` with source location. | `unsupported-import-specifier` diagnostic fixture. | Never as Node/npm compatibility; Sloppy-owned imports only when scoped. |
| Node imports such as `"fs"`, `"node:fs"`, `"path"`, or `process` | Rejected / never supported as compatibility | Rejected as unsupported imports or by JS/TS standards checks. | `SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER` for imports. | `node-fs-import` diagnostic fixture and JS/TS standards scanner. | Node compatibility is not a Sloppy goal. |
| One app from `Sloppy.create()` | Supported | Extracts one app variable. | None. | `hello-mapget`, `results-json`, `function-handler`, and `grouped-route`. | Alpha compiler. |
| Builder form `Sloppy.createBuilder()` plus `builder.build()` | Supported | Extracts the built app when `build()` is called on a known builder variable. | None. | `builder-mapget` golden fixture. | Alpha compiler. |
| Multiple apps | Rejected with diagnostic | Rejected even if only one app is exported. | `SLOPPYC_E_MULTIPLE_APPS`. | `multiple-apps` diagnostic fixture. | Deferred until native app graph policy exists. |
| Missing default export | Rejected with diagnostic | Rejected because runtime needs one app artifact contract. | `SLOPPYC_E_MISSING_APP`. | `missing-app` diagnostic fixture. | Alpha compiler requires default export. |
| `app.get("/literal", handler)` and `app.mapGet("/literal", handler)` | Supported | Emits one GET route and one stable handler ID in source order. | None. | `hello-mapget` golden fixture plus COMPILER-30.D Minimal API unit coverage. | Alpha compiler. |
| `app.post/put/patch/delete` and `app.mapPost/mapPut/mapPatch/mapDelete` | Supported metadata extraction | Emits method metadata and handler IDs in source order. Current dev runtime still serves GET only. | None for supported methods. | `http-methods` golden fixture, Plan `valid-route-methods`, and COMPILER-30.D Minimal API unit coverage. | ENGINE-02 compiler metadata; runtime dispatch beyond GET remains separate. |
| `app.mapGet("/literal", () => Results.text(...))` | Supported | Emits generated handler source and route metadata. | None. | `hello-mapget` golden fixture. | Alpha compiler. |
| `Results.text(...)` | Supported | Accepted with inline JSON-safe arguments. | Unsupported values produce handler diagnostics. | `hello-mapget` and `function-handler` golden fixtures. | Alpha compiler. |
| `Results.json(...)` | Supported | Accepted with inline JSON-safe object/array/literal values and simple context reads. | Unsupported values produce handler diagnostics. | `results-json` and `grouped-route` golden fixtures. | Alpha compiler. |
| `Results.ok(...)` and `Results.noContent()` | Supported narrow helper subset | Accepted by extractor and runtime result conversion paths. | Unsupported values produce handler diagnostics. | Rust unit test coverage in `accepts_ok_and_no_content_result_helpers`. | Alpha helper subset. |
| Inline function handler expression | Supported | Copied into generated artifact when the body returns a supported result. | None. | `function-handler` golden fixture. | Alpha compiler. |
| Named handler identifier | Rejected with diagnostic | Rejected to avoid hidden source-copy and closure rules. | `SLOPPYC_E_UNSUPPORTED_HANDLER`. | `unsupported-handler-shape` diagnostic fixture. | Deferred until source-copy/closure policy is designed. |
| Handler with zero parameters or one identifier context parameter | Supported | Handler may read simple context roots such as `ctx.route.id`; COMPILER-30.E emits request binding metadata for `ctx.route.*`, `ctx.query.*`, `ctx.header.*`, `ctx.body.text()`, and `ctx.body.json(SchemaName)`. | Unsupported parameter shapes produce `SLOPPYC_E_UNSUPPORTED_HANDLER_PARAMETERS`. | Supported fixtures plus `unsupported-handler-parameter` and `metadata-extraction`. | Alpha compiler plus COMPILER-30.E binding metadata. |
| Destructured/default/rest or multiple handler parameters | Rejected with diagnostic | Rejected before artifact emission. | `SLOPPYC_E_UNSUPPORTED_HANDLER_PARAMETERS`. | `unsupported-handler-parameter` diagnostic fixture. | Deferred until typed binding policy exists. |
| Closed-over handler values | Rejected with diagnostic | Rejected to prevent generated handlers from losing dependencies. | `SLOPPYC_E_UNSUPPORTED_HANDLER_VALUE`. | `unsupported-handler-capture` diagnostic fixture. | Deferred until module/source-copy policy exists. |
| Async handlers with one direct `Results.*` return | Supported metadata/emission and V8-gated execution | Copied into generated `app.js` as `async`; plan records `handlers[].async` and `features.asyncHandlers`. ENGINE-03 V8 runtime settles the returned Promise when it completes during the owner-thread microtask drain. | None for direct return shape. | `async-handler` golden fixture and V8-gated `conformance.async_handler.run_once`. | ENGINE-02 metadata, ENGINE-03 microtask-only execution. |
| Async handlers with `await`, multiple statements, or non-direct returns | Rejected with diagnostic | Rejected so the compiler does not imply broader Promise/event-loop execution support. | `SLOPPYC_E_UNSUPPORTED_ASYNC_HANDLER_BODY`. | `unsupported-async-handler-body` diagnostic fixture. | Future compiler/runtime async expansion. |
| Top-level await | Deferred / rejected | Rejected as unsupported top-level syntax. | `SLOPPYC_E_UNSUPPORTED_TOP_LEVEL`. | Matrix-documented; add a fixture when diagnostics expand. | Future compiler/runtime async policy. |
| Dynamic route strings | Rejected with diagnostic | Rejected; no partial route extraction. | `SLOPPYC_E_UNSUPPORTED_DYNAMIC_ROUTE_PATTERN`. | `unsupported-dynamic-route` diagnostic fixture. | Dynamic mode is future explicit work. |
| Computed route methods or `app[method](...)` | Rejected with diagnostic | Rejected; extractor requires explicit API calls. | `SLOPPYC_E_UNSUPPORTED_COMPUTED_ROUTE_METHOD`. | `computed-method` diagnostic fixture. | Never for compiler-extractable examples; dynamic mode may revisit separately. |
| Loops registering routes | Rejected with diagnostic | Rejected; no best-effort unrolling. | `SLOPPYC_E_UNSUPPORTED_LOOP_ROUTE_REGISTRATION`. | `loop-route-registration` diagnostic fixture. | Deferred until a deliberate static expansion story exists. |
| Conditionals registering routes | Rejected with diagnostic | Rejected; static plan must not depend on runtime branch state. | `SLOPPYC_E_UNSUPPORTED_CONDITIONAL_ROUTE_REGISTRATION`. | `conditional-route-registration` diagnostic fixture. | Deferred/dynamic mode only. |
| HEAD/OPTIONS route declarations such as `mapHead` | Rejected with diagnostic | Rejected because ENGINE-01 reserves OPTIONS for framework-owned behavior and defers HEAD policy. | `SLOPPYC_E_UNSUPPORTED_HTTP_METHOD`. | `unsupported-http-method` diagnostic fixture. | Future explicit HTTP policy only. |
| `app.group("/prefix")` / `app.mapGroup("/prefix")` plus grouped literal route calls | Supported narrow subset | Emits joined route path and optional route name for supported route methods. Nested group variables compose literal prefixes deterministically. | None. | `grouped-route` golden fixture plus COMPILER-30.D nested group unit coverage. | COMPILER-30.D route graph extraction. |
| Function modules passed to `app.useModule(...)` | Supported narrow subset | A named relative export may receive the app, register literal routes directly on the app parameter or on nested groups created from it, and contribute module-attributed routes. Used modules are preserved in Strong Plan metadata even when they contribute zero routes. | Missing imports/exports or unsupported module shapes produce module diagnostics. | `function-module`, `function-module-same-file`, `function-module-empty`, and COMPILER-30.D module route unit coverage. | COMPILER-30.D route/module extraction plus COMPILER-30.J fixture coverage. |
| Middleware and filters | Deferred | Not extracted. | Unsupported top-level/route-shape diagnostics. | Matrix-documented; add fixtures when first scoped. | Future app-host hardening. |
| `builder.capabilities.addDatabase("token", { provider, access })` | Supported metadata extraction | Emits one Plan `dataProviders` entry and one database `capabilities` entry for first-party database providers (`sqlite`, `postgres`, `sqlserver`). No native provider is opened by the compiler. Capability/effect metadata is keyed by `capabilityKind` and `providerKind`; `dataProviders[]` remains only for database capabilities. | Unsupported token/provider/access/shape diagnostics as applicable. | `provider-capability`, `provider-metadata-multiple-databases`, and COMPILER-30.F/G provider-kind unit coverage. | ENGINE-02 metadata; ENGINE-05/06 runtime enforcement. |
| `import { sqlite } from "sloppy/providers/sqlite"` plus `app.use(sqlite("name", options?))` | Supported metadata extraction | Emits SQLite provider/capability metadata and accepts inline literal `database`. No native provider is opened by the compiler. | Unsupported provider import names or provider option shapes produce provider diagnostics. | `metadata-extraction` and provider config unit coverage. | COMPILER-30.E provider metadata. |
| `const db = app.provider("sqlite:name" \| "postgres:name" \| "sqlserver:name")` | Supported lookup recognition | Recognizes static first-party database provider handles and binds provider token plus provider kind for effect/capability tasks. The normalized capability token remains `data.<name>`. | Dynamic provider tokens or unsupported provider kinds are outside the static subset. | COMPILER-30.E/F/G provider lookup unit coverage. | COMPILER-30.E metadata foundation; effects in COMPILER-30.F/G. |
| Direct database provider calls through a static provider handle | Supported effect inference | The effect model records `capabilityKind`, `providerKind`, provider token, operation, and access. For the database adapter family, `query(...)` and `queryOne(...)` infer `read`; `exec(...)` infers `write` unless the SQL literal starts with `SELECT`; `WITH` defaults to `write`; `transaction(...)` infers `readwrite`. SQLite provider-backed route handlers are wrapped with request-scoped runtime handles in generated `app.js`; PostgreSQL and SQL Server handles are recognized for metadata but rejected when a generated runtime wrapper would be required. | Dynamic provider handles, unsupported provider methods, or non-SQLite generated provider wrappers are not accepted as executable compiler output. | `effects-capability`, `realistic-users-api`, `missing-provider-effect`, `non-sqlite-provider-bridge`, `unsupported-provider-method`, and COMPILER-30.F/G unit coverage. | COMPILER-30.F/G effect and capability inference plus COMPILER-30.J fixture coverage. |
| Same-file helper functions that close over static provider handles | Supported effect inference | Named function helpers and `const helper = (...) => ...` initializers declared after the provider handle may contribute provider effects to routes that call them directly or through another same-file helper. The compiler emits helper source and embeds helpers inside provider-backed handler wrappers so closure-captured provider handles remain request-scoped where a runtime opener exists. | Imported helper effects, broad repository/object/class patterns, non-database provider adapters, recursive helper policy, and dynamic calls remain deferred unless covered by later fixtures. | `effects-capability` golden fixture and helper inference unit coverage. | COMPILER-30.F/G first supported callgraph slice. |
| Strong Plan completeness metadata | Supported metadata emission | Emits route and whole-plan `completeness` plus `strongPlan` evidence. `complete` means required route/provider/body/response metadata is Plan-visible for the supported subset. `partial` is used for runnable routes with incomplete optional metadata, such as `Results.status(ctx.route.code, ...)` or `ctx.body.json()` without a schema. | Missing provider registration for an inferred effect is invalid and produces `SLOPPYC_E_MISSING_PROVIDER` before artifact emission. | `partial-body-without-schema`, `partial-dynamic-status`, compiler unit coverage, and refreshed success fixture goldens. | COMPILER-30.H/I completeness and COMPILER-30.J fixture coverage. |
| `const key = app.config.getString/getInt/getNumber/getBool("Key", default?)` | Supported metadata extraction | Emits `configReads[]` metadata with key, type, default-presence, and source location. Runtime config resolution remains separate. | Non-literal keys produce `SLOPPYC_E_UNSUPPORTED_CONFIG_KEY`. | `metadata-extraction` and COMPILER-30.E unit coverage. | COMPILER-30.E config metadata. |
| `schema.object/string/int/number/bool/array` declarations | Supported metadata extraction | Emits `schemas[]` metadata for literal object fields, nested arrays/objects, and `.optional()`, `.min()`, `.max()`, `.email()` modifiers. | Unsupported/dynamic schema declarations produce `SLOPPYC_E_UNSUPPORTED_SCHEMA`. | `metadata-extraction` and COMPILER-30.E unit coverage. | COMPILER-30.E schema metadata. |
| `Results.text/json/ok/created/accepted/html/status/noContent/badRequest/notFound/problem` metadata | Supported preliminary metadata extraction | Accepted result helpers still govern handler support; routes with request/config/schema metadata include preliminary response kind/status/helper metadata in Plan. | Unsupported helper shapes continue to produce handler diagnostics. | `metadata-extraction` and result helper unit coverage. | COMPILER-30.E results metadata; completeness in COMPILER-30.H/I. |
| Secret-bearing provider/capability fields | Rejected with diagnostic | Rejected before artifact emission. | `SLOPPYC_E_SECRET_PLAN_METADATA`. | `unsupported-secret-capability` diagnostic fixture. | Permanent policy. |
| Non-database provider adapters and broad service extraction | Deferred | The compiler data model is capability-kind/provider-kind based, but first-class non-database provider APIs are not compiler plan input yet. Do not model future clients as database providers just to fit current `dataProviders[]` output. | Unsupported top-level/route-shape diagnostics if used in compiler input. | Matrix-documented; `provider-metadata-multiple-databases` proves the current database family is not SQLite-only. | Later provider/framework tasks. |
| Decorators | Not supported by the current compiler subset | Not accepted as compiler-extractable API shape. | Parse or unsupported syntax diagnostic. | Matrix-documented. | No alpha target. |
| TypeScript input or TS-only handler syntax | Partially parsed for diagnostics | `.ts` input is accepted at the parser boundary for the supported subset, but TypeScript-only handler syntax is rejected; `.tsx/.mts/.cts` remain rejected as unsupported source extensions. | `SLOPPYC_E_UNSUPPORTED_INPUT` for unsupported extensions or `SLOPPYC_E_UNSUPPORTED_TYPESCRIPT_HANDLER` for TS-only handler syntax. | `unsupported-typescript-handler` diagnostic fixture plus parser module extension tests. | Official TypeScript checking/lowering later. |

## COMPILER-30 Target Subset

COMPILER-30 (#460) expands the compiler-owned supported subset in bounded tasks. The target
is deep static inference for supported Slop app patterns, not arbitrary TypeScript.

Target supported shapes:

- static route paths;
- Minimal API `app.get/post/put/patch/delete(...)`, with current `map*` compatibility where
  required by existing artifacts;
- route groups;
- function modules;
- relative imports;
- Slop stdlib imports;
- Slop provider imports;
- provider registrations and provider handles;
- schema declarations;
- config key reads and `bind`;
- request binding helpers for route/query/header/body;
- explicit `Results.*`;
- direct provider calls;
- local helper functions;
- imported relative helper functions;
- repository/factory functions where effects are statically resolvable;
- object-literal methods where effects are statically resolvable;
- simple class instances where effects are statically resolvable;
- effect summaries for supported calls.

Target rejected or metadata-required shapes:

- dynamic route paths;
- unknown bare imports;
- npm/node_modules imports;
- dynamic imports;
- unknown runtime route generation;
- unresolvable provider usage where capability truth is required.

The compiler should fail invalid runtime contracts, mark optional metadata partial when the
app can still run, and allow runtime-only behavior only when the developer explicitly opts
in with required provider/capability metadata.

Route-level `uses: [...]` and manual capability metadata are fallback escape hatches, not
the normal workflow. The compiler must infer provider/capability effects through normal
Minimal API, function-module, repository, factory, object-method, and simple service/class
patterns when they are statically resolvable.

ENGINE-03 only graduates the direct async handler shape whose Promise settles during the V8
owner-thread microtask drain. `SLOPPYC_E_UNSUPPORTED_ASYNC_HANDLER_BODY` remains the
correct compiler response for `await`, multi-statement async bodies, and non-direct returns
until the compiler and runtime can execute those broader shapes honestly.

## Artifact and Diagnostic Policy

Supported builds must emit byte-stable `app.plan.json`, `app.js`, and `app.js.map` without
absolute local paths, timestamps, random IDs, or checkout-specific text. Handler IDs start
at `1` and follow source order. ENGINE-15.A source maps include the authored source list,
`sourcesContent`, generated handler mappings, and deterministic `x_sloppy` metadata for
source-file hashes plus available route/module/schema/provider/capability/effect source
locations. This metadata is for diagnostics consumers; it does not add arbitrary bundler
map support or broaden the supported input syntax.

Rejected input must fail before success artifacts are emitted. Diagnostics include stable
codes, source path, and line/column when the parser exposes a span. The text renderer stays
intentionally small; JSON diagnostics, multi-location source frames, and richer fix metadata
belong to later diagnostics work.

COMPILER-30.B/C adds the first real code behind those boundaries: parser/source-type
entrypoints, supported import classification/resolution, source module graph bookkeeping,
symbol-table primitives, Slop DSL helper recognition, and static string literal/alias
evaluation. The existing artifact extractor now routes source-type checks, relative import
resolution, route-method matching, member-chain matching, and string-argument recognition
through those focused modules while preserving current artifact compatibility.

COMPILER-30.D completes the current route/group/function-module extraction slice: Minimal
API `get/post/put/patch/delete` methods, nested literal route groups, direct and grouped
function-module route contributions, duplicate method/path validation, and module route
source locations are compiler-owned. This does not add middleware/filter/controller
execution, arbitrary TypeScript inference, package resolution, or runtime HTTP dispatch
beyond the existing artifact path.

COMPILER-30.F/G adds the first Plan-visible effect summaries with explicit capability kind
and provider kind. The implemented adapter family is database providers: SQLite,
PostgreSQL, and SQL Server metadata can be represented, while only the current SQLite
runtime shorthand is executable through generated handlers. The implemented callgraph
subset is deliberately small: direct calls through static provider handles and same-file
helpers that close over those handles. Manual capability metadata remains an escape hatch
for dynamic or plugin-style patterns, not a requirement for this supported normal workflow.

COMPILER-30.H/I adds static validation and strong Plan emission for the implemented subset.
Provider/capability metadata remains kind-based: database providers are the first adapter
family, but emitted route effects and data-provider entries carry both `capabilityKind` and
`providerKind` so future provider families can add adapters without rewriting the Plan
shape. Non-database provider adapters are not implemented in this slice.

## ENGINE-14 Module Syntax

Supported source imports are intentionally small:

- `import { Sloppy, Results } from "sloppy";`
- `import { sqlite } from "sloppy/providers/sqlite";`
- named relative imports such as `import { usersModule } from "./modules/users.js";`

The compiler resolves relative imports before runtime startup and rewrites the supported
graph into the classic generated artifact. Unsupported bare imports, Node/npm specifiers,
remote imports, dynamic `import(...)`, missing relative imports, circular relative imports,
missing named exports, and unsupported module shapes fail at compile time with diagnostics.

COMPILER-30.B/C makes the accepted import set explicit:

- relative imports are resolved only as fixture/source-local `.js`, `.mjs`, or `.ts` files;
- `"sloppy"` is the only supported stdlib bare import;
- `"sloppy/providers/sqlite"` is the only supported provider bare import;
- unknown bare, Node/npm, remote, and dynamic imports remain rejected with source-located
  diagnostics.

Function modules may export one named function that receives the app, obtains providers
through `app.provider("sqlite:<name>")`, creates route groups with `app.group(...)`, and
registers literal routes either on the app parameter or on groups derived from it. Nested
literal module groups compose deterministically. Controllers, decorators, package modules,
TS path aliases, and full TypeScript typechecking remain deferred.

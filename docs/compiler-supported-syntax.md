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
- exactly one app from `Sloppy.create()` or `Sloppy.createBuilder()` plus `builder.build()`;
- literal top-level `app.mapGet`, `app.mapPost`, `app.mapPut`, `app.mapPatch`, or
  `app.mapDelete` calls, or a simple `app.mapGroup(...)` variable with literal grouped
  calls for the same supported method set;
- inline arrow/function handlers, including direct `async` handlers, that return the
  supported `Results.*` helpers;
- optional `builder.capabilities.addDatabase(...)` SQLite capability metadata;
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
is supported when it matches the documented subset. TypeScript extensions are accepted at
the CLI boundary only so `sloppyc` can return the documented unsupported-TypeScript
diagnostic. No full TypeScript typechecking, npm/package-manager behavior, `node_modules`
resolution, broad module graph bundling, watch mode, or hot reload is implemented.

## Matrix

| Shape | Classification | Behavior | Diagnostic expectation | Fixture/test expectation | Roadmap target |
| --- | --- | --- | --- | --- | --- |
| `import { Sloppy, Results } from "sloppy"` | Supported | Accepted as the core public compiler import and rewritten out of generated `app.js`. | None. | Covered by every supported compiler fixture. | Alpha compiler. |
| Optional `data` import from `"sloppy"` | Supported for metadata-bearing fixtures | Accepted only as unaliased `data`; generated `app.js` reads `data` from `globalThis.__sloppy_runtime`. | Aliases or unknown imports use `SLOPPYC_E_UNSUPPORTED_IMPORT`. | `provider-capability` golden fixture. | ENGINE-02 metadata support. |
| Aliased `Sloppy` or `Results` imports | Rejected with diagnostic | Rejected because extractor matching is explicit. | `SLOPPYC_E_UNSUPPORTED_IMPORT`. | `unsupported-import-alias` diagnostic fixture. | Deferred until a scoped ergonomics task needs aliases. |
| Arbitrary bare imports such as `"express"` | Rejected with diagnostic | Rejected; no package or module resolution runs. | `SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER` with source location. | `unsupported-import-specifier` diagnostic fixture. | Never as Node/npm compatibility; Sloppy-owned imports only when scoped. |
| Node imports such as `"fs"`, `"node:fs"`, `"path"`, or `process` | Rejected / never supported as compatibility | Rejected as unsupported imports or by JS/TS standards checks. | `SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER` for imports. | `node-fs-import` diagnostic fixture and JS/TS standards scanner. | Node compatibility is not a Sloppy goal. |
| One app from `Sloppy.create()` | Supported | Extracts one app variable. | None. | `hello-mapget`, `results-json`, `function-handler`, and `grouped-route`. | Alpha compiler. |
| Builder form `Sloppy.createBuilder()` plus `builder.build()` | Supported | Extracts the built app when `build()` is called on a known builder variable. | None. | `builder-mapget` golden fixture. | Alpha compiler. |
| Multiple apps | Rejected with diagnostic | Rejected even if only one app is exported. | `SLOPPYC_E_MULTIPLE_APPS`. | `multiple-apps` diagnostic fixture. | Deferred until native app graph policy exists. |
| Missing default export | Rejected with diagnostic | Rejected because runtime needs one app artifact contract. | `SLOPPYC_E_MISSING_APP`. | `missing-app` diagnostic fixture. | Alpha compiler requires default export. |
| `app.mapGet("/literal", handler)` | Supported | Emits one GET route and one stable handler ID in source order. | None. | `hello-mapget` golden fixture. | Alpha compiler. |
| `app.mapPost`, `app.mapPut`, `app.mapPatch`, and `app.mapDelete` | Supported metadata extraction | Emits method metadata and handler IDs in source order. Current dev runtime still serves GET only. | None for supported methods. | `http-methods` golden fixture and Plan `valid-route-methods`. | ENGINE-02 compiler metadata; ENGINE-04 runtime dispatch later. |
| `app.mapGet("/literal", () => Results.text(...))` | Supported | Emits generated handler source and route metadata. | None. | `hello-mapget` golden fixture. | Alpha compiler. |
| `Results.text(...)` | Supported | Accepted with inline JSON-safe arguments. | Unsupported values produce handler diagnostics. | `hello-mapget` and `function-handler` golden fixtures. | Alpha compiler. |
| `Results.json(...)` | Supported | Accepted with inline JSON-safe object/array/literal values and simple context reads. | Unsupported values produce handler diagnostics. | `results-json` and `grouped-route` golden fixtures. | Alpha compiler. |
| `Results.ok(...)` and `Results.noContent()` | Supported narrow helper subset | Accepted by extractor and runtime result conversion paths. | Unsupported values produce handler diagnostics. | Rust unit test coverage in `accepts_ok_and_no_content_result_helpers`. | Alpha helper subset. |
| Inline function handler expression | Supported | Copied into generated artifact when the body returns a supported result. | None. | `function-handler` golden fixture. | Alpha compiler. |
| Named handler identifier | Rejected with diagnostic | Rejected to avoid hidden source-copy and closure rules. | `SLOPPYC_E_UNSUPPORTED_HANDLER`. | `unsupported-handler-shape` diagnostic fixture. | Deferred until source-copy/closure policy is designed. |
| Handler with zero parameters or one identifier context parameter | Supported | Handler may read simple context roots such as `ctx.route.id`. | Unsupported parameter shapes produce `SLOPPYC_E_UNSUPPORTED_HANDLER_PARAMETERS`. | Supported fixtures plus `unsupported-handler-parameter`. | Alpha compiler. |
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
| `app.mapGroup("/prefix")` plus grouped literal route calls | Supported narrow subset | Emits joined route path and optional route name for supported route methods. | None. | `grouped-route` golden fixture. | Alpha compiler. |
| Nested groups, middleware, filters | Deferred | Not extracted. | Unsupported top-level/route-shape diagnostics. | Matrix-documented; add fixtures when first scoped. | Future app-host hardening. |
| `builder.capabilities.addDatabase("token", { provider: "sqlite", access })` | Supported metadata extraction | Emits one Plan `dataProviders` entry and one database `capabilities` entry. No native provider is opened. | Unsupported token/provider/access/shape diagnostics as applicable. | `provider-capability` golden fixture. | ENGINE-02 metadata; ENGINE-05/06 runtime enforcement. |
| Secret-bearing provider/capability fields | Rejected with diagnostic | Rejected before artifact emission. | `SLOPPYC_E_SECRET_PLAN_METADATA`. | `unsupported-secret-capability` diagnostic fixture. | Permanent policy. |
| Services/modules/broad provider extraction | Deferred | Bootstrap-only APIs beyond the minimal database capability metadata are not compiler plan input yet. | Unsupported top-level/route-shape diagnostics if used in compiler input. | Matrix-documented; no success fixtures until implemented. | Later framework/module tasks. |
| Decorators | Not supported by the current compiler subset | Not accepted as compiler-extractable API shape. | Parse or unsupported syntax diagnostic. | Matrix-documented. | No alpha target. |
| TypeScript input or TS-only handler syntax | Rejected with diagnostic | `.ts/.tsx/.mts/.cts` inputs are rejected; handler type annotations are rejected. | `SLOPPYC_E_UNSUPPORTED_TYPESCRIPT_INPUT` or `SLOPPYC_E_UNSUPPORTED_TYPESCRIPT_HANDLER`. | `unsupported-typescript-handler` diagnostic fixture. | Official TypeScript checking/lowering later. |

ENGINE-03 only graduates the direct async handler shape whose Promise settles during the V8
owner-thread microtask drain. `SLOPPYC_E_UNSUPPORTED_ASYNC_HANDLER_BODY` remains the
correct compiler response for `await`, multi-statement async bodies, and non-direct returns
until the compiler and runtime can execute those broader shapes honestly.

## Artifact and Diagnostic Policy

Supported builds must emit byte-stable `app.plan.json`, `app.js`, and `app.js.map` without
absolute local paths, timestamps, random IDs, or checkout-specific text. Handler IDs start
at `1` and follow source order.

Rejected input must fail before success artifacts are emitted. Diagnostics include stable
codes, source path, and line/column when the parser exposes a span. The text renderer stays
intentionally small; JSON diagnostics, multi-location source frames, and richer fix metadata
belong to later diagnostics work.
## ENGINE-14 Module Syntax

Supported source imports are intentionally small:

- `import { Sloppy, Results } from "sloppy";`
- `import { sqlite } from "sloppy/providers/sqlite";`
- named relative imports such as `import { usersModule } from "./modules/users.js";`

The compiler resolves relative imports before runtime startup and rewrites the supported
graph into the classic generated artifact. Unsupported bare imports, Node/npm specifiers,
remote imports, dynamic `import(...)`, missing relative imports, circular relative imports,
missing named exports, and unsupported module shapes fail at compile time with diagnostics.

Function modules may export one named function that receives the app, obtains providers
through `app.provider("sqlite:<name>")`, creates route groups with `app.group(...)`, and
registers literal routes on that group. Controllers, decorators, package modules, TS path
aliases, and full TypeScript typechecking remain deferred.

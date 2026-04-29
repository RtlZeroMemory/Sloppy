# Compiler Supported Syntax Matrix

Status: MAIN1-01 source of truth for the alpha compiler input subset.

`sloppyc build` is intentionally narrow. It parses one JavaScript source file, extracts a
static Sloppy app shape, emits deterministic artifacts, and rejects unsupported dynamic
JavaScript instead of silently producing a partial plan.

The compiler does not implement a full TypeScript compiler, package resolution, bundling,
Node compatibility, middleware extraction, services/modules/data extraction, decorators,
or arbitrary route registration.

## Alpha Source Policy

Supported input is a single `.js` or `.mjs` file with:

- `import { Sloppy, Results } from "sloppy";`;
- exactly one app from `Sloppy.create()` or `Sloppy.createBuilder()` plus `builder.build()`;
- literal top-level `app.mapGet(...)` calls, or a simple `app.mapGroup(...)` variable with
  literal grouped `mapGet(...)` calls;
- inline arrow/function handlers that return the supported `Results.*` helpers;
- `export default app;` for the extracted app variable.

Named handler functions are not supported yet. The current extractor copies inline handler
source slices into the generated classic artifact, so identifier handlers would need a
separate source-copy and closure policy before they can be accepted honestly.

## Source-Input Run Policy

Alpha keeps the explicit two-step artifact workflow:

```powershell
sloppyc build app.js --out .sloppy
sloppy run --artifacts .sloppy
```

`sloppy run <source.js>` remains deferred. The direct source-input path would need a
designed handoff to `sloppyc`, cache keys, stale-artifact detection, source diagnostics,
and rebuild policy. MAIN1-01 deliberately avoids hiding those decisions inside the runtime.

## Matrix

| Shape | Classification | Behavior | Diagnostic expectation | Fixture/test expectation | Roadmap target |
| --- | --- | --- | --- | --- | --- |
| `import { Sloppy, Results } from "sloppy"` | Supported | Accepted as the only public compiler import and rewritten out of generated `app.js`. | None. | Covered by every supported compiler fixture. | Alpha MVP. |
| Aliased `Sloppy` or `Results` imports | Rejected with diagnostic | Rejected because extractor matching is explicit. | `SLOPPYC_E_UNSUPPORTED_IMPORT`. | `unsupported-import-alias` diagnostic fixture. | Deferred until a scoped ergonomics task needs aliases. |
| Arbitrary bare imports such as `"express"` | Rejected with diagnostic | Rejected; no package or module resolution runs. | `SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER` with source location. | `unsupported-import-specifier` diagnostic fixture. | Never as Node/npm compatibility; Sloppy-owned imports only when scoped. |
| Node imports such as `"fs"`, `"node:fs"`, `"path"`, or `process` | Rejected / never supported as compatibility | Rejected as unsupported imports or by JS/TS standards checks. | `SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER` for imports. | `node-fs-import` diagnostic fixture and JS/TS standards scanner. | Node compatibility is not a Sloppy goal. |
| One app from `Sloppy.create()` | Supported | Extracts one app variable. | None. | `hello-mapget`, `results-json`, `function-handler`, and `grouped-route`. | Alpha MVP. |
| Builder form `Sloppy.createBuilder()` plus `builder.build()` | Supported | Extracts the built app when `build()` is called on a known builder variable. | None. | `builder-mapget` golden fixture. | Alpha MVP. |
| Multiple apps | Rejected with diagnostic | Rejected even if only one app is exported. | `SLOPPYC_E_MULTIPLE_APPS`. | `multiple-apps` diagnostic fixture. | Deferred until native app graph policy exists. |
| Missing default export | Rejected with diagnostic | Rejected because runtime needs one app artifact contract. | `SLOPPYC_E_MISSING_APP`. | `missing-app` diagnostic fixture. | Alpha MVP requires default export. |
| `app.mapGet("/literal", handler)` | Supported | Emits one GET route and one stable handler ID in source order. | None. | `hello-mapget` golden fixture. | Alpha MVP. |
| `app.mapGet("/literal", () => Results.text(...))` | Supported | Emits generated handler source and route metadata. | None. | `hello-mapget` golden fixture. | Alpha MVP. |
| `Results.text(...)` | Supported | Accepted with inline JSON-safe arguments. | Unsupported values produce handler diagnostics. | `hello-mapget` and `function-handler` golden fixtures. | Alpha MVP. |
| `Results.json(...)` | Supported | Accepted with inline JSON-safe object/array/literal values and simple context reads. | Unsupported values produce handler diagnostics. | `results-json` and `grouped-route` golden fixtures. | Alpha MVP. |
| `Results.ok(...)` and `Results.noContent()` | Supported narrow helper subset | Accepted by extractor and runtime result conversion paths. | Unsupported values produce handler diagnostics. | Rust unit test coverage in `accepts_ok_and_no_content_result_helpers`. | Alpha MVP helper subset. |
| Inline function handler expression | Supported | Copied into generated artifact when the body returns a supported result. | None. | `function-handler` golden fixture. | Alpha MVP. |
| Named handler identifier | Rejected with diagnostic | Rejected to avoid hidden source-copy and closure rules. | `SLOPPYC_E_UNSUPPORTED_HANDLER`. | `unsupported-handler-shape` diagnostic fixture. | Deferred until source-copy/closure policy is designed. |
| Handler with zero parameters or one identifier context parameter | Supported | Handler may read simple context roots such as `ctx.route.id`. | Unsupported parameter shapes produce `SLOPPYC_E_UNSUPPORTED_HANDLER_PARAMETERS`. | Supported fixtures plus `unsupported-handler-parameter`. | Alpha MVP. |
| Destructured/default/rest or multiple handler parameters | Rejected with diagnostic | Rejected before artifact emission. | `SLOPPYC_E_UNSUPPORTED_HANDLER_PARAMETERS`. | `unsupported-handler-parameter` diagnostic fixture. | Deferred until typed binding policy exists. |
| Closed-over handler values | Rejected with diagnostic | Rejected to prevent generated handlers from losing dependencies. | `SLOPPYC_E_UNSUPPORTED_HANDLER_VALUE`. | `unsupported-handler-capture` diagnostic fixture. | Deferred until module/source-copy policy exists. |
| Async handlers | Deferred / rejected | Rejected by handler-shape validation. | `SLOPPYC_E_UNSUPPORTED_HANDLER` today. | Covered by unsupported handler-shape class; richer async-specific fixture belongs to MAIN1-06 if needed. | MAIN1-05 promise/microtask policy. |
| Top-level await | Deferred / rejected | Rejected as unsupported top-level syntax. | `SLOPPYC_E_UNSUPPORTED_TOP_LEVEL`. | Matrix-documented; add a fixture when MAIN1-06 expands diagnostics. | Future compiler/runtime async policy. |
| Dynamic route strings | Rejected with diagnostic | Rejected; no partial route extraction. | `SLOPPYC_E_UNSUPPORTED_DYNAMIC_ROUTE_PATTERN`. | `unsupported-dynamic-route` diagnostic fixture. | Dynamic mode is future explicit work. |
| Computed route methods or `app[method](...)` | Rejected with diagnostic | Rejected; extractor requires explicit API calls. | `SLOPPYC_E_UNSUPPORTED_COMPUTED_ROUTE_METHOD`. | `computed-method` diagnostic fixture. | Never for compiler-extractable examples; dynamic mode may revisit separately. |
| Loops registering routes | Rejected with diagnostic | Rejected; no best-effort unrolling. | `SLOPPYC_E_UNSUPPORTED_LOOP_ROUTE_REGISTRATION`. | `loop-route-registration` diagnostic fixture. | Deferred until a deliberate static expansion story exists. |
| Conditionals registering routes | Rejected with diagnostic | Rejected; static plan must not depend on runtime branch state. | `SLOPPYC_E_UNSUPPORTED_CONDITIONAL_ROUTE_REGISTRATION`. | `conditional-route-registration` diagnostic fixture. | Deferred/dynamic mode only. |
| Non-GET methods such as `mapPost` | Deferred / rejected | Rejected by compiler extraction; runtime dev dispatch is GET-only. | `SLOPPYC_E_UNSUPPORTED_HTTP_METHOD`. | `unsupported-http-method` diagnostic fixture. | MAIN1-04 or later HTTP hardening. |
| `app.mapGroup("/prefix")` plus grouped literal `mapGet` | Supported narrow subset | Emits joined route path and optional route name. | None. | `grouped-route` golden fixture. | Alpha MVP. |
| Nested groups, middleware, filters | Deferred | Not extracted. | Unsupported top-level/route-shape diagnostics. | Matrix-documented; add fixtures when first scoped. | MAIN1-04/app-host hardening. |
| Services/modules/data providers/capabilities extraction | Deferred | Bootstrap-only APIs are not compiler plan input yet. | Unsupported top-level/route-shape diagnostics if used in compiler input. | Matrix-documented; no success fixtures until implemented. | MAIN1-02/03/08/10. |
| Decorators | Never supported for MVP / deferred only by explicit EPIC | Not accepted as compiler-extractable API shape. | Parse or unsupported syntax diagnostic. | Matrix-documented. | No alpha target. |
| TypeScript input or TS-only handler syntax | Rejected with diagnostic | `.ts/.tsx/.mts/.cts` inputs are rejected; handler type annotations are rejected. | `SLOPPYC_E_UNSUPPORTED_TYPESCRIPT_INPUT` or `SLOPPYC_E_UNSUPPORTED_TYPESCRIPT_HANDLER`. | `unsupported-typescript-handler` diagnostic fixture. | Official TypeScript checking/lowering later. |

## Artifact and Diagnostic Policy

Supported builds must emit byte-stable `app.plan.json`, `app.js`, and `app.js.map` without
absolute local paths, timestamps, random IDs, or checkout-specific text. Handler IDs start
at `1` and follow source order.

Rejected input must fail before success artifacts are emitted. Diagnostics include stable
codes, source path, and line/column when the parser exposes a span. MAIN1-01 keeps the text
renderer intentionally small; JSON diagnostics, source frames, and richer fix metadata
belong to MAIN1-06.
